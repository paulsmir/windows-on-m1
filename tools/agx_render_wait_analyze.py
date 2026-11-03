#!/usr/bin/env python3
"""Attribute RenderFence to bounded submission pressure without adding nested costs.

Uses Q18's complete steady-state Execute windows. Inclusive paths and exclusive
leaves are joined by PID/thread/serial. Top-four censoring prevents exact stack
attribution; report conservative bounds, observed counts and remaining unknowns.
"""
import argparse
from collections import Counter, defaultdict
import json
import struct
import agx_paging_wait_analyze as paging


def bucket(words, key):
    low, high = paging.wait_bound(words, {key})  # validates format/flags
    found = next((i for i in range(4, 16, 3) if words[i] == key), None)
    # Each printed bucket/omitted sum truncates less than one microsecond.
    if found is not None:
        return low, high + 1, words[found + 1]
    return 0, high + 1 if words[2] or words[3] & 4 else 0, 0


def slot_bound(wait, path, phase):
    render, render_hi, render_calls = bucket(wait, phase * 256 + 3)
    slot, slot_hi, slot_calls = bucket(path, phase * 256 + 10)
    # All omitted exclusive time could belong to this phase. Add <5 us for
    # quantization of the four retained buckets plus the omitted aggregate.
    phase_hi = sum(wait[i + 2] for i in range(4, 16, 3)
                   if wait[i] >> 8 == phase) + wait[2] + 5
    return dict(render_lower_us=render, render_upper_us=render_hi,
                render_calls=render_calls, slot_lower_us=slot,
                slot_upper_us=slot_hi, slot_calls=slot_calls,
                render_in_slot_lower_us=max(0, render + slot - phase_hi),
                render_in_slot_upper_us=min(render_hi, slot_hi))


def analyze(lines, summary):
    records, last = {}, {}
    reasons, heaps = Counter(), Counter()
    queue_waits = Counter()
    for number, line in enumerate(lines, 1):
        tag = line.split(' ', 1)[0]
        if tag not in ('measure-d3d12-execute', 'measure-d3d12-replay-window',
                       'measure-d3d12-wait', 'measure-d3d12-wait-path',
                       'measure-d3d12-query-time', 'measure-perf-reason',
                       'measure-d3d12-query-heap', 'measure-d3d12-queue-wait'):
            continue
        parts = line.split()
        meta = dict(p.split('=', 1) for p in parts[1:] if '=' in p)
        if int(meta['pid']) != summary['pid']:
            continue
        words = [int(p, 16) for p in parts[1:] if '=' not in p]
        thread = (int(meta['pid']), int(meta['tid']))
        qpc = int(meta['qpc'])
        inside = summary['first_qpc'] <= qpc < summary['last_qpc']
        if tag == 'measure-d3d12-query-heap':
            heaps[str(words[0])] += 1
        elif tag == 'measure-perf-reason':
            if inside:
                text = b''.join(struct.pack('<I', w) for w in words[4:]).split(b'\0')[0].decode()
                reasons[text] += words[1]  # delta count, NOT total/presents
        elif tag == 'measure-d3d12-queue-wait':
            if inside:
                queue_waits[str(words[0])] += 1
        elif tag == 'measure-d3d12-execute':
            key = (*thread, words[4])
            if key in records:
                raise ValueError('duplicate Execute')
            records[key] = dict(line=number)
            last[thread] = key
        elif tag == 'measure-d3d12-replay-window':
            if thread not in last:
                raise ValueError('window without Execute')
            records[last[thread]]['window'] = [paging.gpu.u64(words, i) for i in (0, 2, 4, 6)]
        else:
            key = (*thread, words[1])
            if key not in records:
                raise ValueError('wait/query without Execute')
            label = {'measure-d3d12-wait': 'wait', 'measure-d3d12-wait-path': 'path',
                     'measure-d3d12-query-time': 'queries'}[tag]
            records[key][label] = words
    totals = {phase: defaultdict(int) for phase in (3, 4, 6)}
    paths, queries = defaultdict(lambda: [0, 0]), [0] * 6
    selected, examples = 0, []
    for key, record in records.items():
        if 'window' not in record:
            raise ValueError('missing window')
        a, b, c, hz = record['window']
        if a < summary['first_qpc'] or c >= summary['last_qpc']:
            continue
        if not a <= b <= c or hz != summary['qpc_hz']:
            raise ValueError('invalid window')
        if not all(k in record for k in ('wait', 'path', 'queries')):
            raise ValueError('missing joined profiler record')
        selected += 1
        for phase in totals:
            for k, value in slot_bound(record['wait'], record['path'], phase).items():
                totals[phase][k] += value
        for i in range(4, 16, 3):
            k, count, us = record['path'][i:i+3]
            if k:
                paths[hex(k)][0] += count
                paths[hex(k)][1] += us
        for i, value in enumerate(record['queries'][2:]):
            queries[i] += value
        if len(examples) < 3:
            examples.append(dict(serial=key[2], **record))
    if selected != summary['executes']:
        raise ValueError('Q18/Q19 selection mismatch')
    n = summary['frames']
    return dict(frames=n, executes=selected,
                phases={paging.PHASES[p]: dict(totals=dict(t),
                    mean_ms={k[:-3]: v / (1000 * n) for k, v in t.items() if k.endswith('_us')},
                    observed_calls_per_frame={k: v / n for k, v in t.items() if k.endswith('_calls')})
                        for p, t in totals.items()},
                retained_paths={k: dict(calls=v[0], inclusive_us=v[1], mean_ms=v[1]/(1000*n))
                                for k, v in paths.items()},
                query_commands=dict(zip(('begin_count', 'begin_us', 'end_count', 'end_us',
                                         'resolve_count', 'resolve_us'), queries)),
                query_heap_creations_whole_log=dict(heaps),
                observed_queue_waits=dict(queue_waits),
                perf_reason_deltas_reported_in_window=dict(reasons), evidence=examples,
                caveats=['Inclusive paths overlap: do not add Flush, InflightSlot and RenderFence.',
                         'Observed counts omit buckets outside the top four; missing is not zero.',
                         'Overlap bounds are per-Execute/per-phase set bounds, not recovered call stacks.',
                         'Perf reasons are batched deltas: report time does not locate each event.',
                         'Query heap types describe creations, not per-resolve type/timing.',
                         'CPU blocking overlaps GPU execution and is not an additive frame-time saving.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('--skip', type=float, default=.3)
    args = parser.parse_args()
    with open(args.log) as f:
        summary = paging.analyze(f, args.skip)
    with open(args.log) as f:
        result = analyze(f, summary)
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
