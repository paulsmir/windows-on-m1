#!/usr/bin/env python3
"""Decode the KMD's GPU fault and reset evidence (G4-r3, EXP1296).

Receipts (REG_BINARY under the APPL0002 device key):
  Wom1GpuFault          v1, 168 bytes: firmware Timeout/Fault events, captured
                        before the firmware is resumed (first, first with a
                        valid FAULT_INFO, last).
  Wom1GpuReset          v1, 96 bytes: the last DxgkDdiResetEngine or
                        DxgkDdiResetFromTimeout and the state it found.
  Wom1FirmwareRecovery  8 dwords (EXP1075/G4-recovery).
Dump blackbox:
  --dbginfo HEX         the DxgkDdiCollectDbgInfo snapshot (v1 32 or v2 56
                        bytes; dxgkrnl trims trailing zero bytes) from the
                        {270A33FD-...} secondary dump data.

Input: raw .bin files (named after the value), a collector state.json with a
Receipts map, or a devnode .reg export.  No live registry access.
Layouts: drivers/apple-agx/render-admission/include/render_gpu_fault.h and
scheduler_windows.c (ADMISSION_SCHEDULER_DEBUG_SNAPSHOT).  FAULT_INFO
(SGX 0x17030, G13) is decoded as in Asahi regs.rs get_fault_info.
"""
import argparse
import base64
import json
from pathlib import Path
import re
import struct

FAULT_NAME = 'Wom1GpuFault'
RESET_NAME = 'Wom1GpuReset'
RECOVERY_NAME = 'Wom1FirmwareRecovery'
NAMES = (FAULT_NAME, RESET_NAME, RECOVERY_NAME)

FAULT_INFO_NOT_READ = 0xacce5515abad1dea
EVENT_KINDS = {0: 'fault', 1: 'flag', 4: 'timeout'}
EVENT_FLAGS = (('resumed', 1), ('info_read', 2), ('info_valid', 4), ('job_known', 8))
FAULT_REASONS = ('unmapped', 'af_fault', 'write_only', 'read_only', 'no_access')
UNIT_LOW = ('DCMP', 'UL1C', 'CMP', 'GSL1', 'IAP', 'VCE', 'TE', 'RAS', 'VDM', 'PPP',
            'IPF', 'IPF_CPF', 'VF', 'VF_CPF', 'ZLS')
UNIT_SPECIAL = {0xa1: 'dPM', 0xa2: 'dCDM_KS(0)', 0xa3: 'dCDM_KS(1)', 0xa4: 'dCDM_KS(2)',
                0xa5: 'dIPP', 0xa6: 'dIPP_CS', 0xa7: 'dVDM_CSD', 0xa8: 'dVDM_SSD',
                0xa9: 'dVDM_ILF', 0xaa: 'dVDM_ILD', 0xab: 'dRDE(0)', 0xac: 'dRDE(1)',
                0xad: 'FC', 0xae: 'GSL2', 0xb8: 'GL2CC_MB'}
UNIT_HIGH = ('gPM_SP', 'gVDM_CSD_SP', 'gVDM_SSD_SP', 'gVDM_ILF_SP', 'gVDM_TFP_SP',
             'gVDM_MMB_SP', 'gCDM_CS_KS0_SP', 'gCDM_CS_KS1_SP', 'gCDM_CS_KS2_SP',
             'gCDM_KS0_SP', 'gCDM_KS1_SP', 'gCDM_KS2_SP', 'gIPP_SP', 'gIPP_CS_SP',
             'gRDE0_SP', 'gRDE1_SP')
QUIESCE = ('none', 'ended', 'timeout', 'worker', 'poll', 'backend', 'state')
RESET_ENTRY = {1: 'DxgkDdiResetEngine', 2: 'DxgkDdiResetFromTimeout'}
PACKET_STATES = ('empty', 'prepared', 'queued', 'active')
PREEMPTION_PHASES = ('idle', 'wait-current-boundary', 'ready-to-notify',
                     'notification-claimed')
FAULT_FILES = {1: 'scheduler_windows.c', 2: 'paging_windows.c',
               3: 'submission_windows.c', 4: 'backend_platform_windows.c'}

EVENT = struct.Struct('<8I2Q')
FAULT_HEADER = struct.Struct('<6I')
RESET = struct.Struct('<22IQ')
DBGINFO_V1 = 32
DBGINFO_V2 = 56


def fault_unit(code):
    if code <= 0x9f:
        low = code & 0xf
        return f'{UNIT_LOW[low]}({code >> 4})' if low < len(UNIT_LOW) else f'unknown({code:#x})'
    if code in UNIT_SPECIAL:
        return UNIT_SPECIAL[code]
    if 0xb0 <= code <= 0xb7:
        return f'GL2CC_META({code & 0xf})'
    if code >= 0xe0:
        return f'{UNIT_HIGH[code & 0xf]}({(code >> 4) & 1})'
    return f'unknown({code:#x})'


def decode_fault_info(value):
    """Asahi regs.rs get_fault_info for G13 (not G14X)."""
    if value == FAULT_INFO_NOT_READ:
        return {'read': False}
    result = {'read': True, 'raw': f'{value:#018x}', 'valid': bool(value & 1)}
    if not result['valid']:
        return result
    reason = (value >> 1) & 7
    unit = (value >> 9) & 0xff
    result.update(
        reason=FAULT_REASONS[reason] if reason < len(FAULT_REASONS) else f'unknown({reason})',
        access='read' if value & (1 << 4) else 'write',
        level=(value >> 7) & 3,
        unit_code=f'{unit:#04x}', unit=fault_unit(unit),
        vm_slot=(value >> 17) & 0x3f,
        sideband=(value >> 23) & 0x7f,
        address=f'{(value >> 30) << 6:#x}')
    return result


def decode_event(data, offset):
    (sequence, kind, fence, pid, ms, halt_count, flags, _reserved,
     context, info) = EVENT.unpack_from(data, offset)
    if sequence == 0:
        return None
    if flags & ~0xf:
        raise ValueError('unknown event flags')
    return {
        'sequence': sequence,
        'event': EVENT_KINDS.get(kind, f'unknown({kind})'),
        'fence': f'{fence:#x}', 'process_id': pid,
        'submit_to_event_ms': ms, 'halt_count': halt_count,
        'flags': [name for name, bit in EVENT_FLAGS if flags & bit],
        'context': f'{context:#x}',
        'fault_info': decode_fault_info(info),
    }


def decode_fault(data):
    version, size, events, resumed, not_halted, valid = FAULT_HEADER.unpack_from(data)
    if version != 1 or size != FAULT_HEADER.size + 3 * EVENT.size or len(data) != size:
        raise ValueError('unsupported Wom1GpuFault version or length')
    base = FAULT_HEADER.size
    return {
        'version': version, 'events': events, 'resumed': resumed,
        'not_halted': not_halted, 'valid_faults': valid,
        'first': decode_event(data, base),
        'first_valid': decode_event(data, base + EVENT.size),
        'last': decode_event(data, base + 2 * EVENT.size),
    }


def fault_code(code):
    if code == 0:
        return None
    return {'raw': f'{code:#x}',
            'file': FAULT_FILES.get(code >> 16, f'unknown({code >> 16})'),
            'line': code & 0xffff}


def name_of(table, value):
    return table[value] if value < len(table) else f'unknown({value})'


def decode_reset(data):
    if len(data) != RESET.size:
        raise ValueError('unsupported Wom1GpuReset length')
    (version, size, resets, succeeded, entry, status, faulted, packet_state,
     packet_fence, active, completed, submitted, phase, aborted, quiesce,
     wait_ms, backend, provider, poll_guard, worker, halted, recoveries,
     context) = RESET.unpack(data)
    if version != 1 or size != RESET.size:
        raise ValueError('unsupported Wom1GpuReset version')
    return {
        'version': version, 'resets': resets, 'succeeded': succeeded,
        'entry': RESET_ENTRY.get(entry, f'unknown({entry})'),
        'status': f'{status:#010x}',
        'scheduler_faulted_at_entry': fault_code(faulted),
        'packet': {'state': name_of(PACKET_STATES, packet_state),
                   'fence': f'{packet_fence:#x}', 'context': f'{context:#x}'},
        'fences': {'active': f'{active:#x}', 'completed': f'{completed:#x}',
                   'last_submitted': f'{submitted:#x}',
                   'last_aborted': f'{aborted:#x}'},
        'preemption': name_of(PREEMPTION_PHASES, phase),
        'quiesce': name_of(QUIESCE, quiesce), 'wait_ms': wait_ms,
        'backend_phase': backend, 'provider_phase': provider,
        'last_poll_guard': poll_guard,
        'worker': {'scheduled': bool(worker & 1), 'active': bool(worker & 2)},
        'firmware_halted': None if halted == 0xffffffff else halted,
        'firmware_recoveries': recoveries,
    }


def decode_recovery(data):
    if len(data) != 32:
        raise ValueError('unsupported Wom1FirmwareRecovery length')
    values = struct.unpack('<8I', data)
    keys = ('recoveries', 'last_kind', 'last_fence', 'last_ms', 'max_ms',
            'deadline_expiries', 'longest_job_ms', 'longest_job_fence')
    result = dict(zip(keys, values))
    result['last_kind'] = EVENT_KINDS.get(result['last_kind'], result['last_kind'])
    for key in ('last_fence', 'longest_job_fence'):
        result[key] = f'{result[key]:#x}'
    return result


def decode_dbginfo(data):
    """DxgkDdiCollectDbgInfo snapshot; dxgkrnl's blackbox drops trailing zeros."""
    if len(data) < 8:
        raise ValueError('truncated debug snapshot')
    version, size = struct.unpack_from('<2I', data)
    if (version, size) not in ((1, DBGINFO_V1), (2, DBGINFO_V2)) or len(data) > size:
        raise ValueError('unsupported debug snapshot version or length')
    data = data + bytes(size - len(data))
    words = struct.unpack(f'<{size // 4}I', data)
    result = {
        'version': version, 'completed': f'{words[2]:#x}',
        'last_submitted': f'{words[3]:#x}', 'active': f'{words[4]:#x}',
        'preemption': name_of(PREEMPTION_PHASES, words[5]), 'contexts': words[6],
        'faulted': bool(words[7] & 1), 'paging_pending': bool(words[7] & 2),
    }
    if version == 2:
        platform = words[11]
        result.update(
            scheduler_faulted=fault_code(words[8]),
            packet={'state': name_of(PACKET_STATES, words[9]), 'fence': f'{words[10]:#x}'},
            platform=None if platform == 0xffffffff else {
                'backend_phase': platform & 0xff, 'provider_phase': (platform >> 8) & 0xff,
                'last_poll_guard': (platform >> 16) & 0xff,
                'worker_scheduled': bool(platform & (1 << 24)),
                'resetting': bool(platform & (1 << 25)),
                'stopping': bool(platform & (1 << 26))},
            resets=words[12], resets_succeeded=words[13])
    return result


DECODERS = {FAULT_NAME: decode_fault, RESET_NAME: decode_reset,
            RECOVERY_NAME: decode_recovery}


def read_receipts(path):
    data = path.read_bytes()
    if path.suffix.lower() == '.json':
        saved = json.loads(data)
        saved = saved.get('Receipts', saved)
        return {name: base64.b64decode(saved[name], validate=True)
                for name in NAMES if name in saved}
    if path.suffix.lower() == '.reg':
        text = data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
        text = re.sub(r'\\\r?\n\s*', '', text)
        found = {}
        for name in NAMES:
            matches = re.findall(r'^"' + name + r'"=hex:([0-9a-fA-F, ]+)\s*$', text, re.M)
            if len(matches) > 1:
                raise ValueError(f'more than one {name} value')
            if matches:
                found[name] = bytes.fromhex(matches[0].replace(',', ''))
        return found
    for name in NAMES:
        if path.stem == name:
            return {name: data}
    raise ValueError(f'{path.name}: name a raw file after one of {", ".join(NAMES)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('inputs', nargs='*', type=Path)
    parser.add_argument('--dbginfo', help='CollectDbgInfo snapshot bytes as hex')
    args = parser.parse_args()
    result = {}
    for path in args.inputs:
        for name, data in read_receipts(path).items():
            result[name] = DECODERS[name](data)
    if args.dbginfo:
        result['CollectDbgInfo'] = decode_dbginfo(bytes.fromhex(re.sub(r'\s', '', args.dbginfo)))
    if not result:
        parser.error('nothing to decode')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
