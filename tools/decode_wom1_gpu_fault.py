#!/usr/bin/env python3
"""Decode the KMD's GPU fault and reset evidence (G4-r3, EXP1296).

Receipts (REG_BINARY under the APPL0002 device key):
  Wom1GpuFault          v1, 168 bytes: firmware Timeout/Fault events, captured
                        before the firmware is resumed (first, first with a
                        valid FAULT_INFO, last).
  Wom1GpuReset          v1 96 / v2 496 / v3 536 / v4 792 bytes: the last DxgkDdiResetEngine or
                        DxgkDdiResetFromTimeout and the state it found; v2
                        adds timing and the last 8 resets; v3 adds reset stages;
                        v4 adds before/after quiesce queue retirement samples.
  Wom1FirmwareRecovery  8 dwords (EXP1075/G4-recovery).
  Wom1GpuFaultPostResume v1, 184 bytes (rc77): at the end of each reset
                        wait, RegionC fault_control, SGX 0x17620/0x17630
                        (Wom1GpuFaultControlRead), FAULT_INFO, the first 64
                        firmware-status bytes, live pending stamps, RegionC
                        fault_info, compute rptr/done/write, events received
                        while recovery was pending and the optional KMD
                        RecoverChannel (Wom1GpuFaultRecoverChannel).
  Wom1SoftFaultState    v1, 40 bytes (rc82): at GPU runtime start, whether the
                        KMD reports soft faults to the UMD (Wom1SoftFaults=1
                        and RegionC fault_control 0xb), with SGX 0x17620/
                        0x17630 (Wom1GpuFaultControlRead).
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
# rc77: firmware state at the end of each reset quiescence wait.
POST_RESUME_NAME = 'Wom1GpuFaultPostResume'
# rc82: whether the KMD reports soft faults to the UMD (Wom1SoftFaults = 1).
SOFT_FAULTS_NAME = 'Wom1SoftFaultState'
NAMES = (FAULT_NAME, RESET_NAME, RECOVERY_NAME, OMITTED_NAME, MAPPING_NAME, MAPPING_LAST_NAME,
         POST_RESUME_NAME, SOFT_FAULTS_NAME)
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
QUIESCE = ('none', 'ended', 'timeout', 'worker', 'poll', 'backend', 'state',
           'unsubmitted')
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
RECOVERY_SNAPSHOT_HEADER = struct.Struct('<5I')
RECOVERY_QUEUE = struct.Struct('<9I')
RECOVERY_SNAPSHOT_SIZE = RECOVERY_SNAPSHOT_HEADER.size + 3 * RECOVERY_QUEUE.size
RESET_V4 = RESET_V3 + 2 * RECOVERY_SNAPSHOT_SIZE
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


def decode_retirement(data, offset):
    captured, fence, phase, recovery, mask = RECOVERY_SNAPSHOT_HEADER.unpack_from(data, offset)
    if captured not in (0, 1) or recovery not in (0, 1) or mask & ~7:
        raise ValueError('invalid retirement snapshot flags')
    if not captured:
        return {'captured': False}
    result = {'captured': True, 'fence': f'{fence:#x}', 'runtime_phase': phase,
              'recovery_pending': bool(recovery), 'queues': {},
              'meaning': 'sequential shared-memory observations, not atomic GPU state'}
    for index, name in enumerate(('ta', '3d', 'compute')):
        if not mask & (1 << index):
            continue
        values = RECOVERY_QUEUE.unpack_from(
            data, offset + RECOVERY_SNAPSHOT_HEADER.size + index * RECOVERY_QUEUE.size)
        event, flags, reads, stamp, expected, done, write, expected_done, capacity = values
        if flags & ~3 or reads & ~7:
            raise ValueError('invalid retirement queue flags')
        result['queues'][name] = {
            'event_number': event, 'event_seen': bool(flags & 1), 'complete': bool(flags & 2),
            'read_mask': reads, 'stamp': f'{stamp:#x}' if reads & 1 else None,
            'expected_stamp': f'{expected:#x}', 'done': done if reads & 2 else None,
            'write': write if reads & 4 else None,
            'expected_done': expected_done, 'capacity': capacity}
    return result


def decode_reset(data):
    if len(data) not in (RESET.size, RESET_V2, RESET_V3, RESET_V4):
        raise ValueError('unsupported Wom1GpuReset length')
    (version, size, resets, succeeded, entry, status, faulted, packet_state,
     packet_fence, active, completed, submitted, phase, aborted, quiesce,
     wait_ms, backend, provider, poll_guard, worker, halted, recoveries,
     context) = RESET.unpack_from(data)
    if (version, size) not in ((1, RESET.size), (2, RESET_V2), (3, RESET_V3), (4, RESET_V4)) or size != len(data):
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
        if version >= 3:
            stages = RESET_V3_TAIL.unpack_from(data, RESET_V2)
            result['stage'] = name_of(RESET_STAGES, stages[0])
            for entry, index in zip(result['history'], order):
                entry['stage'] = name_of(RESET_STAGES, stages[1 + index])
        if version >= 4:
            result['retirement_before'] = decode_retirement(data, RESET_V3)
            result['retirement_after'] = decode_retirement(data, RESET_V3 + RECOVERY_SNAPSHOT_SIZE)
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
    """Preserve v7-v10 prefixes; v10 separates pinned native-ring backing;
    v11 (rc86) appends the creation cost split and the empty-slab cache;
    v12 (rc88) appends the background slab prefill and failed-attempt time;
    v13 (rc93) appends the large-page cache misses and the per-shape slab time;
    v14 (rc104) appends the floor wait and the live promised allocations;
    v15 (rc109) appends the graphics policy segment 3 was sized from."""
    if len(data) < 16:
        raise ValueError('truncated Wom1M2PoolDemand header')
    version, size, granule, last_window = struct.unpack_from('<4I', data)
    if size != {7: 304, 8: 360, 9: 432, 10: 448, 11: 576, 12: 608, 13: 640, 14: 704, 15: 752}.get(version) or len(data) != size or granule != 65536:
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
                  floor_refusals=None, last_reserve_failure=None, refusal_counts=None,
                  native_staging_bytes=None, total_promised_bytes=None, total_held_bytes=None,
                  creation_cost=None,
                  event_sample={7: None, 8: 'post_failure', 9: 'admission_decision',
                                10: 'admission_decision', 11: 'admission_decision',
                                12: 'admission_decision', 13: 'admission_decision',
                                14: 'admission_decision', 15: 'admission_decision'}[version])
    if version >= 8:
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
    if version >= 9:
        reasons = ('low_memory', 'signal_unavailable', 'backing_allocation', 'arithmetic')
        result['refusal_counts'] = dict(zip(reasons, struct.unpack_from('<4Q', data, 400)))
        failure = result['last_reserve_failure']
        if failure is not None:
            reason, _reserved, required, held, cap, private = struct.unpack_from('<2I4Q', data, 360)
            failure.update(reason=({0: 'none'} | dict(enumerate(reasons, 1))).get(
                reason, f'unknown({reason})'), required_granules=required,
                held_granules=held, reserve_cap_granules=cap, private_granules=private)
    if version >= 10:
        result['native_staging_bytes'], result['total_promised_bytes'] = struct.unpack_from('<2Q', data, 432)
        result['total_held_bytes'] = result['held_bytes'] + result['native_staging_bytes']
    if version >= 11:
        names = ('slab_alloc_calls', 'slab_alloc_us', 'slab_alloc_us_max', 'slab_check_us',
                 'slab_free_us', 'slab_free_us_max', 'reserve_calls', 'reserve_us',
                 'reserve_us_max', 'reserve_no_growth', 'lease_zero_count', 'lease_zero_us',
                 'lease_zero_skips', 'spare_limit_slabs', 'empty_slabs', 'cache_drops')
        cost = dict(zip(names, struct.unpack_from('<16Q', data, 448)))
        # Microseconds; Windows zeroes pages inside the allocation call.
        cost['meaning'] = ('slab_alloc: MmAllocatePagesForMdlEx incl. zeroing; reserve: '
                           'KMD share of pfnAllocateCb; commit_us: window MAP_AT')
        result['creation_cost'] = cost
    result['prefill'] = None
    if version >= 12:
        names = ('prefill_slabs', 'prefill_us', 'prefill_discards', 'slab_fail_us')
        prefill = dict(zip(names, struct.unpack_from('<4Q', data, 576)))
        prefill['meaning'] = ('slabs a work item added to the empty-slab cache outside the '
                              'pool lock; slab_fail_us: unusable MmAllocatePagesForMdlEx calls')
        result['prefill'] = prefill
    result['large_pages'] = None
    if version >= 13:
        names = ('fast_misses', 'contiguous_us', 'chunked_us', 'construct_large_pages')
        large = dict(zip(names, struct.unpack_from('<4Q', data, 608)))
        large['contiguous_ms_per_slab'] = (round(large['contiguous_us'] / result['slab_contiguous'] / 1000, 3)
                                           if result['slab_contiguous'] else None)
        large['chunked_ms_per_slab'] = (round(large['chunked_us'] / result['slab_chunked'] / 1000, 3)
                                        if result['slab_chunked'] else None)
        large['meaning'] = ('rc93: fast_misses = 2 MiB attempts refused because the large-page cache '
                            'was empty (then 64 KiB chunks); *_us = successful attempts by shape; '
                            'construct_large_pages 1 = Wom1PoolConstructLargePages kill switch on')
        result['large_pages'] = large
    result['pressure_wait'] = None
    if version >= 14:
        names = ('budget_ms', 'waits', 'wait_ms', 'wait_ms_max', 'recovered', 'failed',
                 'fast_fails', 'reserved_allocations')
        wait = dict(zip(names, struct.unpack_from('<8Q', data, 640)))
        wait['meaning'] = ('rc104: creations refused at the LowMemory floor that waited '
                           '(budget_ms each, 0 = Wom1PoolPressureWaitMs kill switch), their wall '
                           'ms, those that then reserved or still failed, floor refusals that '
                           'skipped the wait in the fast-fail window; reserved_allocations = '
                           'live allocations holding reserved_bytes')
        result['pressure_wait'] = wait
    result['graphics_policy'] = None
    if version >= 15:
        share, base = struct.unpack_from('<2Q', data, 704)
        pool_percent, percent, max_percent, cpu_only, flags, _reserved = struct.unpack_from('<6I', data, 720)
        headroom, = struct.unpack_from('<Q', data, 744)
        result['graphics_policy'] = dict(
            graphics_share_bytes=share, base_segment_bytes=base,
            extension_bytes=max(result['segment_bytes'] - base, 0),
            pool_percent=pool_percent,
            system_partition_commit_limit_percentage=percent,
            system_partition_commit_limit_percentage_max=max_percent,
            system_partition_cpu_only_memory_mib=cpu_only,
            memory_manager_key=bool(flags & 1), percentage_set=bool(flags & 2),
            written_after_boot=bool(flags & 4), pool_percent_set=bool(flags & 8),
            headroom_refusals=headroom,
            meaning=('rc109: segment_bytes = MIN(Wom1PoolPercent of RAM, VidMm share) - aperture '
                     'reserve, capped by window/broker; base_segment_bytes = the default-formula '
                     'size (Wom1PoolPercent=0); growth past it needs HighMemoryCondition '
                     '(headroom_refusals)'))
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
            event['kind'] = {0x10: 'table-reset', 0x20: 'system-retire',
                             0x40: 'invalidated-logical-owner'}.get(
                                 event['flags'], 'leaf-update')
            if event['flags'] == 0x40:
                event['meaning'] = ('Previous logical owner of these invalidated 4-KiB pages; '
                                    'historical token, not current ownership or native residency')
            event['match'] = [name for bit, name in ((1, 'va'), (2, 'table-index'))
                              if event['match'] & bit]
            for name in ('process_id', 'table_ipa', 'allocation', 'first_va',
                         'mapping_generation', 'status'):
                event[name] = f'{event[name]:#x}'
            history['events'].append(event)
        result['history'] = history
    return result



POST_EVENT_KINDS = {**EVENT_KINDS, 7: 'grow_tvb', 8: 'channel_error'}
FORCE_STATES = {0: 'disabled', 1: 'not-reached', 2: 'sent', 3: 'refused'}
RUNTIME_RESULTS = {0: 'ok'}


def decode_post_resume(data):
    if len(data) != 184:
        raise ValueError('unsupported Wom1GpuFaultPostResume length')
    version, size, fence, wait_ms, fault_control, sgx_read, r17620, r17630 = \
        struct.unpack_from('<8I', data, 0)
    if version not in (1, 2) or size != 184:
        raise ValueError('unsupported Wom1GpuFaultPostResume version')
    fault_info = struct.unpack_from('<Q', data, 32)[0]
    status = struct.unpack_from('<16I', data, 40)
    pending = struct.unpack_from('<I', data, 104)[0]
    region_fault = struct.unpack_from('<6I', data, 108)
    (rptr_read, rptr, done, write, events, kinds, last_kind,
     force_state, force_result, force_mask, force_at,
     poll_failure) = struct.unpack_from('<12I', data, 132)
    return {
        'version': version, 'fence': f'{fence:#x}', 'wait_ms': wait_ms,
        'regionc_fault_control': f'{fault_control:#x}',
        'sgx_fault_control': ({'0x17620': f'{r17620:#x}', '0x17630': f'{r17630:#x}'}
                              if sgx_read else None),
        'fault_info': decode_fault_info(fault_info),
        'firmware_status': {'halt_count': status[4], 'halted': status[8],
                            'resume': status[12], 'words': [f'{w:#x}' for w in status]},
        'pending_stamps_active': pending,
        'regionc_fault_info': [f'{w:#x}' for w in region_fault],
        'compute': {'rptr': rptr if rptr_read else None, 'done': done, 'write': write},
        'events_after_resume': {'count': events,
                                'kinds': [POST_EVENT_KINDS.get(k, k) for k in range(32)
                                          if kinds & (1 << k)],
                                'last': POST_EVENT_KINDS.get(last_kind, last_kind) if events else None},
        'recover_channel': {'state': FORCE_STATES.get(force_state, force_state),
                            'result': RUNTIME_RESULTS.get(force_result, force_result),
                            'pipes': [n for b, n in ((1, 'ta'), (2, '3d'), (4, 'compute'))
                                      if force_mask & b],
                            'at_ms': force_at},
        'poll_failure': decode_poll_failure(poll_failure) if version >= 2 else None,
        'meaning': 'sequential CPU reads after the firmware resume, not atomic GPU state',
    }


RTKIT_SESSION_RESULTS = {0: 'ok', 1: 'invalid_argument', 2: 'invalid_state', 3: 'timeout',
                         4: 'clock_regression', 5: 'transport_failed',
                         6: 'protocol_violation', 7: 'cleanup_failed', 8: 'firmware_crashed'}
POLL_GUARDS = {0: 'ok', 1: 'invalid', 2: 'drain_events', 3: 'clock', 4: 'timeout_check',
               5: 'timeout_apply'}
# AppleAgxPlatformDrainGuard* (apple_agx_platform_provider.h). rc97: 10 is the
# composer refusing an already acknowledged event (EXP2030).
DRAIN_GUARDS = {0: 'ok', 1: 'invalid', 2: 'binding', 3: 'flush_state', 4: 'read_pointer',
                5: 'write_pointer', 6: 'pointer_range', 7: 'flush_message',
                8: 'prepare_event', 9: 'publish_read', 10: 'apply_event'}


def decode_poll_failure(value):
    """rc92 PollFailure (render_gpu_fault.h ADMISSION_GPU_POST_RESUME_POLL_*)."""
    if value == 0:
        return None
    source, ingest, drain, low = value >> 24, (value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff
    if source == 1:
        return {'source': 'rtkit_drain', 'result': RTKIT_SESSION_RESULTS.get(low, low)}
    if source == 2:
        return {'source': 'provider_poll', 'poll_guard': POLL_GUARDS.get(low, low),
                'drain_guard': DRAIN_GUARDS.get(drain, drain), 'ingest_guard': ingest}
    return {'source': source, 'raw': f'{value:#x}'}


def decode_soft_faults(data):
    if len(data) != 40:
        raise ValueError('unsupported Wom1SoftFaultState length')
    (version, size, requested, regionc_read, fault_control, active,
     sgx_read, r17620, r17630, _reserved) = struct.unpack_from('<10I', data, 0)
    if (version, size) != (1, 40):
        raise ValueError('unsupported Wom1SoftFaultState version')
    return {
        'version': version, 'requested': bool(requested),
        'regionc_fault_control': f'{fault_control:#x}' if regionc_read else None,
        'active': bool(active),
        'sgx_fault_control': ({'0x17620': f'{r17620:#x}', '0x17630': f'{r17630:#x}'}
                              if sgx_read else None),
        'meaning': 'active = Wom1SoftFaults=1 and RegionC fault_control 0xb; '
                   'the UMD then reports DRM_ASAHI_FEATURE_SOFT_FAULTS to Mesa',
    }


DECODERS = {MAPPING_NAME: decode_fault_mapping, MAPPING_LAST_NAME: decode_fault_mapping,
            OMITTED_NAME: decode_omitted_leaves, FAULT_NAME: decode_fault, RESET_NAME: decode_reset,
            RECOVERY_NAME: decode_recovery, POOL_NAME: decode_pool,
            POST_RESUME_NAME: decode_post_resume, SOFT_FAULTS_NAME: decode_soft_faults}


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
