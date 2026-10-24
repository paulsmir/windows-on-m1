#!/usr/bin/env python3
"""Decode the KMD's GPU fault and reset evidence (G4-r3, EXP1296).

Receipts (REG_BINARY under the APPL0002 device key):
  Wom1GpuFault          v1, 168 bytes: firmware Timeout/Fault events, captured
                        before the firmware is resumed (first, first with a
                        valid FAULT_INFO, last).
  Wom1GpuReset          v1 96 / v2 496 / v3 536 bytes: the last DxgkDdiResetEngine or
                        DxgkDdiResetFromTimeout and the state it found; v2
                        adds timing and the last 8 resets; v3 adds reset stages.
  Wom1FirmwareRecovery  8 dwords (EXP1075/G4-recovery).
  Wom1G4OmittedLeaves   v1, 2976 bytes: eight historical successful leaf
                        updates, allocation/PID/VA spans and exact 16 KiB
                        omission bitmap. A match is historical evidence,
                        not proof that a later fault used the same mapping.
  Wom1M2PoolDemand      v7 304 / v8 360 bytes: pool totals; v8 appends the
                        actual floor counter and last reservation failure.
                        Explicit Wom1M2PoolDemand.bin input only, so older
                        JSON/REG fault collections retain their behavior.
  Wom1GpuFaultMapping / Wom1GpuFaultMappingLast v1 256 / v2 912 bytes:
                        first/latest valid fault's active process, fence,
                        VM slot, native graph leaf and four logical
                        PTEs, captured before recovery. Unknown capture state
                        is distinct from an absent mapping; CPU metadata only.
                        v2 freezes up to eight matching paging-ring records.
Dump blackbox:
  --dbginfo HEX         the DxgkDdiCollectDbgInfo snapshot (v1 32 or v2 56
                        bytes; dxgkrnl trims trailing zero bytes) from the
                        {270A33FD-...} secondary dump data.
  --enumtag FILE        cdb ".enumtag" output of a VIDEO_TDR_FAILURE dump:
                        the snapshot is taken from that secondary data.

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
OMITTED_NAME = 'Wom1G4OmittedLeaves'
POOL_NAME = 'Wom1M2PoolDemand'
MAPPING_NAME = 'Wom1GpuFaultMapping'
# Same mapping layout; latest valid event, including an unknown capture result.
MAPPING_LAST_NAME = 'Wom1GpuFaultMappingLast'
NAMES = (FAULT_NAME, RESET_NAME, RECOVERY_NAME, OMITTED_NAME, MAPPING_NAME, MAPPING_LAST_NAME)
RAW_NAMES = NAMES + (POOL_NAME,)

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
RESET_STAGES = ('none', 'validate', 'quiesce', 'private', 'paging',
                'packet-active', 'packet-reset', 'backend-release', 'scheduler', 'complete')
RESET_ENTRY_NAMES = {1: 'DxgkDdiResetEngine', 2: 'DxgkDdiResetFromTimeout'}
PACKET_STATES = ('empty', 'prepared', 'queued', 'active')
PREEMPTION_PHASES = ('idle', 'wait-current-boundary', 'ready-to-notify',
                     'notification-claimed')
FAULT_FILES = {1: 'scheduler_windows.c', 2: 'paging_windows.c',
               3: 'submission_windows.c', 4: 'backend_platform_windows.c'}

EVENT = struct.Struct('<8I2Q')
FAULT_HEADER = struct.Struct('<6I')
RESET = struct.Struct('<22IQ')
RESET_V2_TAIL = struct.Struct('<4I')
RESET_ENTRY = struct.Struct('<12I')
RESET_HISTORY = 8
RESET_V2 = RESET.size + RESET_V2_TAIL.size + RESET_HISTORY * RESET_ENTRY.size
RESET_V3_TAIL = struct.Struct('<10I')
RESET_V3 = RESET_V2 + RESET_V3_TAIL.size
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


def age(value):
    return None if value == 0xffffffff else value


def decode_reset_entry(data, offset):
    (entry_ms, entry, status, quiesce, wait_ms, packet_state, packet_fence,
     aborted, faulted, job_age, poll_age, recoveries) = RESET_ENTRY.unpack_from(data, offset)
    return {'entry_ms': entry_ms, 'entry': RESET_ENTRY_NAMES.get(entry, f'unknown({entry})'),
            'status': f'{status:#010x}', 'quiesce': name_of(QUIESCE, quiesce),
            'wait_ms': wait_ms, 'packet': f'{name_of(PACKET_STATES, packet_state)}/{packet_fence:#x}',
            'last_aborted': f'{aborted:#x}', 'scheduler_faulted_at_entry': fault_code(faulted),
            'job_age_ms': job_age, 'poll_age_ms': age(poll_age),
            'firmware_recoveries': recoveries}


def decode_reset(data):
    if len(data) not in (RESET.size, RESET_V2, RESET_V3):
        raise ValueError('unsupported Wom1GpuReset length')
    (version, size, resets, succeeded, entry, status, faulted, packet_state,
     packet_fence, active, completed, submitted, phase, aborted, quiesce,
     wait_ms, backend, provider, poll_guard, worker, halted, recoveries,
     context) = RESET.unpack_from(data)
    if (version, size) not in ((1, RESET.size), (2, RESET_V2), (3, RESET_V3)) or size != len(data):
        raise ValueError('unsupported Wom1GpuReset version')
    result = {
        'version': version, 'resets': resets, 'succeeded': succeeded,
        'entry': RESET_ENTRY_NAMES.get(entry, f'unknown({entry})'),
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
    if version >= 2:
        entry_ms, job_age, poll_age, count = RESET_V2_TAIL.unpack_from(data, RESET.size)
        if count > RESET_HISTORY:
            raise ValueError('Wom1GpuReset history count out of range')
        base = RESET.size + RESET_V2_TAIL.size
        last = (resets - 1) % RESET_HISTORY if resets else 0
        order = [(last - count + 1 + i) % RESET_HISTORY for i in range(count)]
        result.update(entry_ms=entry_ms, job_age_ms=job_age, poll_age_ms=age(poll_age),
                      history=[decode_reset_entry(data, base + i * RESET_ENTRY.size)
                               for i in order])
        if version == 3:
            stages = RESET_V3_TAIL.unpack_from(data, RESET_V2)
            result['stage'] = name_of(RESET_STAGES, stages[0])
            for entry, index in zip(result['history'], order):
                entry['stage'] = name_of(RESET_STAGES, stages[1 + index])
    return result


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


WATCHDOG_TAG = '{270A33FD-3DA6-460D-BA893C1BAE21E39B}'


def read_enumtag(text, tag=WATCHDOG_TAG):
    """Bytes of one secondary-data tag from cdb ".enumtag" output."""
    data = bytearray()
    inside = False
    for line in text.splitlines():
        if line.startswith('{'):
            if inside:
                break
            inside = line.upper().startswith(tag)
            continue
        if inside:
            match = re.match(r'\s+((?:[0-9A-Fa-f]{2} ){1,16})', line + ' ')
            if match:
                data += bytes.fromhex(match.group(1).replace(' ', ''))
    if not data:
        raise ValueError(f'{tag} not in the .enumtag output')
    return bytes(data)


def dbginfo_from_blackbox(data):
    """The KMD's CollectDbgInfo bytes in dxgkrnl's watchdog secondary data.

    Undocumented layout, read from the EXP1296 dump: two dwords, then
    (offset, section id) dword pairs up to a zero offset; section 1 is the
    driver's buffer (trailing zero bytes dropped) and ends where the next
    section starts."""
    sections = []
    for at in range(8, len(data) - 7, 8):
        offset, ident = struct.unpack_from('<2I', data, at)
        if offset == 0:
            break
        sections.append((offset, ident))
    starts = sorted({offset for offset, _ in sections} | {len(data)})
    for offset, ident in sections:
        if ident == 1 and offset < len(data):
            return data[offset:min(o for o in starts if o > offset)]
    raise ValueError('no driver section in the watchdog secondary data')


def decode_omitted_leaves(data):
    """Decode historical omissions; later updates can repair/reuse these VAs."""
    size, event_size, capacity = 32 + 8 * 368, 368, 8
    if len(data) != size:
        raise ValueError(f'Wom1G4OmittedLeaves: expected {size} bytes, got {len(data)}')
    version, declared, next_slot, count, total, groups = struct.unpack_from('<4I2Q', data)
    if version != 1 or declared != size or next_slot >= capacity or count > capacity or total < count:
        raise ValueError('Wom1G4OmittedLeaves: invalid header')
    events = []
    for n in range(count):
        slot = (next_slot + capacity - count + n) % capacity
        offset = 32 + slot * event_size
        values = struct.unpack_from('<12Q4I', data, offset)
        seq, qpc, process, pid, allocation, base, alloc_bytes, va, span, group_va, first, last, segment, omitted, group_count, flags = values
        if not seq or group_count > 2048 or not omitted or omitted > group_count:
            raise ValueError('Wom1G4OmittedLeaves: invalid event')
        bitmap = int.from_bytes(data[offset+112:offset+368], 'little')
        if bitmap >> group_count or bitmap.bit_count() != omitted:
            raise ValueError('Wom1G4OmittedLeaves: invalid bitmap')
        selected = [group_va + i*0x4000 for i in range(group_count) if bitmap & (1 << i)]
        if selected[0] != first or selected[-1] != last:
            raise ValueError('Wom1G4OmittedLeaves: inconsistent extent')
        ranges = []
        for address in selected:
            start, end = max(address, va), min(address+0x4000, va+span)
            if start >= end:
                raise ValueError('Wom1G4OmittedLeaves: group outside update')
            if ranges and ranges[-1]['end'] == hex(start):
                ranges[-1]['end'] = hex(end)
            else:
                ranges.append({'start': hex(start), 'end': hex(end)})
        events.append(dict(sequence=seq, qpc=qpc, process_id=process, os_pid=pid,
            allocation=hex(allocation), allocation_va=hex(base) if flags & 2 else None,
            allocation_bytes=alloc_bytes if flags & 1 else None, update_va=hex(va),
            update_bytes=span, segment=segment if segment != 0xffffffff else 'mixed',
            omitted_groups=omitted, omitted_ranges=ranges))
    return dict(version=version, total_events=total, total_groups=groups, events=events,
                meaning='historical successful updates, not current mapping proof')


def decode_pool(data):
    """v7 prefix is unchanged; absent v8 observations are unknown, not zero."""
    if len(data) < 16:
        raise ValueError('truncated Wom1M2PoolDemand header')
    version, size, granule, last_window = struct.unpack_from('<4I', data)
    if size != {7: 304, 8: 360}.get(version) or len(data) != size or granule != 65536:
        raise ValueError('unsupported Wom1M2PoolDemand version, length or granule')
    names = (
        'segment_bytes', 'committed_granules', 'peak_granules', 'commits',
        'commit_failures', 'releases', 'busy_retries', 'release_failures',
        'places', 'moves', 'kills', 'unplaces', 'pinned_granules', 'unaligned',
        'ref_underflows', 'commit_calls', 'commit_us', 'commit_us_max',
        'release_us', 'release_us_max', 'candidates', 'executed_seq',
        'release_delay_ms', 'held_granules', 'slabs', 'peak_slabs', 'new_slabs',
        'new_slab_failures', 'freed_slabs', 'slab_contiguous', 'slab_chunked',
        'ram_bytes', 'reserved_bytes', 'peak_reserved_bytes', 'reserve_failures',
        'broker_bytes')
    result = dict(zip(names, struct.unpack_from('<36Q', data, 16)))
    result.update(version=version, granule_bytes=granule, last_window_result=last_window,
                  committed_bytes=result['committed_granules'] * granule,
                  peak_committed_bytes=result['peak_granules'] * granule,
                  held_bytes=result['held_granules'] * granule,
                  floor_refusals=None, last_reserve_failure=None)
    if version == 8:
        result['floor_refusals'], = struct.unpack_from('<Q', data, 304)
        fields = ('request_bytes', 'reserved_bytes', 'held_bytes', 'committed_bytes',
                  'time_ms', 'high_memory', 'low_memory')
        failure = dict(zip(fields, struct.unpack_from('<5Q2I', data, 312)))
        if result['reserve_failures']:
            for field in ('high_memory', 'low_memory'):
                value = failure[field]
                failure[field] = {0: 'clear', 1: 'set', 2: 'unavailable'}.get(
                    value, f'unknown({value})')
            result['last_reserve_failure'] = failure
    return result


def decode_fault_mapping(data):
    """A valid fault's locked CPU metadata, not a hardware PTE read."""
    if len(data) not in (256, 912):
        raise ValueError('unsupported Wom1GpuFaultMapping length')
    words = struct.unpack_from('<12I10Q', data)
    version, size, fence, status, pid, slot, uncertain, cpu_only, job, native, writable, kind = words[:12]
    if (version, size) not in ((1, 256), (2, 912)) or size != len(data) or status > 7:
        raise ValueError('unsupported Wom1GpuFaultMapping version, size or status')
    names = ('context', 'fault_info_raw', 'fault_va', 'graph_process_id', 'root_ipa',
             'process_generation', 'mapping_generation', 'lease_token', 'native_ipa', 'table_ipa')
    result = dict(zip(names, (f'{v:#x}' for v in words[12:])))
    result.update(version=version, fence=f'{fence:#x}',
        status=('unavailable', 'busy', 'identity-mismatch', 'vm-mismatch', 'uncertain',
                'no-table', 'no-shadow', 'captured')[status],
        os_pid=pid, vm_slot=slot, graph_uncertain=bool(uncertain), cpu_only=bool(cpu_only),
        job_in_flight=bool(job), native_present=bool(native) if status >= 6 else None,
        native_writable=bool(writable) if native else None,
        native_kind=kind if native else None,
        meaning='CPU graph/shadow at fault capture; not a hardware page-table read',
        ptes=None, history=None)
    if status == 7:
        result['ptes'] = []
        for i in range(4):
            ipa, allocation, offset, segment, flags = struct.unpack_from('<3Q2I', data, 128+i*32)
            result['ptes'].append(dict(ipa=f'{ipa:#x}', allocation=f'{allocation:#x}',
                allocation_offset=f'{offset:#x}', segment=segment, flags=flags))
    if version == 2:
        next_entry, scanned, matched, count = struct.unpack_from('<4I', data, 256)
        if scanned > 8192 or matched > scanned or count != min(matched, 8):
            raise ValueError('invalid Wom1GpuFaultMapping history counts')
        history = dict(next=next_entry, scanned=scanned, matched=matched,
            truncated=matched > count, events=[],
            meaning='Historical paging observations; no match does not prove never mapped')
        names = ('qpc', 'process_id', 'table_ipa', 'allocation', 'first_va',
                 'mapping_generation', 'first', 'count', 'valid_count', 'flags',
                 'status', 'first_segment', 'sequence', 'match')
        for i in range(count):
            event = dict(zip(names, struct.unpack_from('<6Q8I', data, 272 + i * 80)))
            if event['match'] not in (1, 2, 3):
                raise ValueError('invalid Wom1GpuFaultMapping history match')
            event['match'] = [name for bit, name in ((1, 'va'), (2, 'table-index'))
                              if event['match'] & bit]
            for name in ('process_id', 'table_ipa', 'allocation', 'first_va',
                         'mapping_generation', 'status'):
                event[name] = f'{event[name]:#x}'
            history['events'].append(event)
        result['history'] = history
    return result


DECODERS = {MAPPING_NAME: decode_fault_mapping, MAPPING_LAST_NAME: decode_fault_mapping,
            OMITTED_NAME: decode_omitted_leaves, FAULT_NAME: decode_fault, RESET_NAME: decode_reset,
            RECOVERY_NAME: decode_recovery, POOL_NAME: decode_pool}


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
    for name in RAW_NAMES:
        if path.stem == name:
            return {name: data}
    raise ValueError(f'{path.name}: name a raw file after one of {", ".join(RAW_NAMES)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('inputs', nargs='*', type=Path)
    parser.add_argument('--dbginfo', help='CollectDbgInfo snapshot bytes as hex')
    parser.add_argument('--enumtag', type=Path, help='cdb .enumtag output of a 0x116 dump')
    args = parser.parse_args()
    result = {}
    for path in args.inputs:
        for name, data in read_receipts(path).items():
            result[name] = DECODERS[name](data)
    if args.dbginfo:
        result['CollectDbgInfo'] = decode_dbginfo(bytes.fromhex(re.sub(r'\s', '', args.dbginfo)))
    if args.enumtag:
        raw = args.enumtag.read_bytes()
        text = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
        result['CollectDbgInfo'] = decode_dbginfo(dbginfo_from_blackbox(read_enumtag(text)))
    if not result:
        parser.error('nothing to decode')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
