#!/usr/bin/env python3
"""Offline composition evidence; no guest access.

EXP1973 example:
  python3 tools/agx_composition_analyze.py --serial full.log --dwm umd.log \
    --app gm1973a-umd.log --same-boot --dwm-pid 1224 --qpc-hz 24000000 \
    --display-ring rcpt-final/Wom1DisplayRing1131.bin

New capture: apple_agx_blt_probe.exe --vsync-follow 300 > vsync.txt
  python3 tools/agx_composition_analyze.py --vsync vsync.txt
Kind 8 is queue entry, retrospectively inserted immediately before kind 3
(matched latch). This measures guest-observed latency, not physical panel phase.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import statistics
import struct


def stats(values):
    values = sorted(values)
    return dict(count=len(values), median=statistics.median(values) if values else None,
                p95=values[min(len(values)-1, int(len(values)*.95))] if values else None,
                max=max(values) if values else None)


def serial_summary(lines):
    applied, latched = [], []
    for line in lines:
        for pattern, dest in [(r'A408 APPLIED swap_id=(\d+)', applied),
                              (r'exact D589 latch swap_id=(\d+)\s*$', latched)]:
            match = re.search(pattern, line)
            if match:
                dest.append(int(match[1]))
    a, b = set(applied), set(latched)
    return dict(applied_records=len(applied), latch_records=len(latched),
                paired_ids=len(a & b), applied_without_latch_text=sorted(a-b),
                latch_without_applied_text=sorted(b-a), cadence_ms=None,
                limitation='Untimestamped text; missing lines do not establish lost swaps. '
                           'D589 may arrive during synchronous A408.')


def display_summary(data, hz):
    if len(data) != 10288 or hz <= 0:
        raise ValueError('Need display-ring v2 (10288 bytes) and positive QPC Hz')
    version, size, count, flips = struct.unpack_from('<4I', data)
    if version != 2 or size != len(data):
        raise ValueError('Unsupported display-ring layout')
    rows = [struct.unpack_from('<3Q4I', data, 48+(i % 256)*40)
            for i in range(max(0, count-256), count)]
    kinds = {}
    for kind in sorted({r[3] for r in rows}):
        group = [r for r in rows if r[3] == kind]
        kinds[str(kind)] = dict(count=len(group),
            flags_detail=dict(Counter(f'{r[4]:x}/{r[5]:x}' for r in group)),
            recorded_gap_ms=stats(r[6]/1000 for r in group if kind in (3, 4)))
    return dict(retained_records=len(rows), overwritten_records=max(0, count-256),
                first_qpc=rows[0][0] if rows else None,
                last_qpc=rows[-1][0] if rows else None, flip_calls_both_kinds=flips,
                qpc_hz=hz, kinds=kinds, filtered=True,
                limitation='Kinds 3/4 retained only for >50ms gaps or changed flags/detail; '
                           'MPO3 forwards to legacy flip, so do not add their counts.')


def app_window(lines):
    times = [int(m[1]) for line in lines
             if line.startswith('measure-d3d12-present ')
             and (m := re.search(r'\bqpc=(\d+)', line))]
    if not times:
        raise ValueError('App trace has no measure-d3d12-present timestamps')
    return min(times), max(times)


def umd_summary(lines, pid, window=None):
    groups = defaultdict(lambda: defaultdict(list))
    rates = set()
    thread_clocks = {}
    for line in lines:
        m = re.search(r'^measure-g4-phase .*?pid=(\d+).*?qpc=(\d+) (.*)$', line)
        if not m or int(m[1]) != pid:
            continue
        words = [int(w, 16) for w in m[3].split()]
        if len(words) != 7 or not words[3]:
            raise ValueError('Malformed measure-g4-phase record')
        rates.add(words[3])
        qpc = int(m[2])
        thread = re.search(r'\btid=(\d+)', line)
        thread = int(thread[1]) if thread else 0
        # Concurrent threads can log out of order; one thread cannot go back
        # in QPC time. Equal frequency alone never proves boot identity.
        if thread in thread_clocks and qpc < thread_clocks[thread]:
            raise ValueError('UMD clock regression; split captures by boot')
        thread_clocks[thread] = qpc
        section = 'all' if window is None else (
            'before' if qpc < window[0] else 'app' if qpc <= window[1] else 'after')
        groups[section][str(words[0])].append(
            (qpc, (words[1] + (words[2] << 32))*1000/words[3]))
    if len(rates) > 1:
        raise ValueError('Mixed QPC frequencies; split captures by boot')
    hz = next(iter(rates), None)
    result = {}
    for section, phases in groups.items():
        result[section] = {}
        for phase, rows in phases.items():
            rows.sort()
            result[section][phase] = dict(count=len(rows),
                duration_ms=stats(r[1] for r in rows),
                event_gap_ms=stats((b[0]-a[0])*1000/hz for a, b in zip(rows, rows[1:])))
    return result


def vsync_summary(lines):
    generations = defaultdict(dict)
    advertised = defaultdict(int)
    duplicates = 0
    errors = []
    clocks = []
    for line in lines:
        if line.startswith('VSYNC_FOLLOW_ERROR'):
            errors.append(line.strip())
        if line.startswith('VSYNC_SAMPLE '):
            sample = dict(re.findall(r'(\w+)=(\S+)', line))
            clocks.append(sample)
            gen, high = int(sample['generation']), int(sample['events'])
            advertised[gen] = max(advertised[gen], high)
            generations[gen]  # a sample-only truncated capture is not empty success
        if not line.startswith('VSYNC_EVENT '):
            continue
        fields = dict(re.findall(r'(\w+)=(\S+)', line))
        required = ('generation', 'seq', 'time100ns', 'kind', 'pending_seq',
                    'pending', 'active_seq', 'active')
        if any(k not in fields for k in required):
            raise ValueError('Use --vsync-follow capture (generation required)')
        row = {k: int(fields[k], 0) for k in required}
        events = generations[row['generation']]
        if row['seq'] in events:
            if events[row['seq']] != row:
                raise ValueError('Conflicting duplicate event')
            duplicates += 1
        events[row['seq']] = row
    result = {}
    for generation, events in generations.items():
        queue = {}
        previous = None
        last_latch = None
        abandoned = 0
        latencies, gaps, latches = [], [], []
        mismatch = unpaired = 0
        for seq, row in sorted(events.items()):
            if previous is not None and seq != previous+1:
                abandoned += len(queue)
                queue.clear()
                last_latch = None  # do not manufacture cadence across missing events
            previous = seq
            if row['kind'] == 8:
                queue[row['pending_seq']] = row
            elif row['kind'] == 3:
                latches.append(row)
                queued = queue.pop(row['active_seq'], None)
                if queued is None:
                    unpaired += 1
                elif (queued['pending'] != row['active'] or
                      queued['time100ns'] > row['time100ns']):
                    mismatch += 1
                else:
                    latencies.append((row['time100ns']-queued['time100ns'])/10000)
                if last_latch is not None:
                    if row['time100ns'] < last_latch['time100ns']:
                        raise ValueError('Nonmonotonic latch clock')
                    gaps.append((row['time100ns']-last_latch['time100ns'])/10000)
                last_latch = row
        high = max(events, default=0)
        expected = max(high, advertised[generation])
        result[str(generation)] = dict(events=len(events), missing_events=expected-len(events),
            unemitted_sample_tail=max(0, advertised[generation]-high),
            unpaired_queue_events=abandoned+len(queue),
            latches=len(latches), matched_queue_latches=len(latencies),
            unpaired_latches=unpaired, identity_mismatches=mismatch,
            queue_to_latch_ms=stats(latencies), latch_gap_ms=stats(gaps),
            first_event=min(events, default=None), last_event=max(events, default=None))
    return dict(generations=result, duplicate_events=duplicates, capture_errors=errors,
                clock_samples=clocks,
                limitation='Kind 8 exists only for completed flips. Pending/hung flips '
                           'remain visible in timer events; initial capture history may be partial.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('serial', 'dwm', 'app', 'display-ring', 'vsync'):
        parser.add_argument('--'+name, type=Path)
    parser.add_argument('--dwm-pid', type=int)
    parser.add_argument('--qpc-hz', type=int)
    parser.add_argument('--same-boot', action='store_true',
                        help='Assert app and DWM inputs came from the same boot; old UMD logs cannot prove this')
    args = parser.parse_args()
    result = {}
    read = lambda path: path.read_text(errors='replace').splitlines()
    try:
        if args.serial:
            result['serial'] = serial_summary(read(args.serial))
        if args.dwm:
            if args.dwm_pid is None:
                raise ValueError('--dwm requires --dwm-pid; never infer process identity')
            if args.app and not args.same_boot:
                raise ValueError('Joining app and DWM requires --same-boot from capture provenance')
            window = app_window(read(args.app)) if args.app else None
            result['app_present_qpc_window'] = window
            result['dwm_phases'] = umd_summary(read(args.dwm), args.dwm_pid, window)
            result['umd_clock_precondition'] = 'One boot per input; --same-boot is caller provenance, not inferred from QPC frequency'
        if args.display_ring:
            result['display_ring'] = display_summary(args.display_ring.read_bytes(), args.qpc_hz or 0)
        if args.vsync:
            result['vsync'] = vsync_summary(read(args.vsync))
        if not result:
            raise ValueError('Select at least one evidence input')
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
