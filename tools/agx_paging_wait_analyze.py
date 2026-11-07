#!/usr/bin/env python3
"""Stream a depth-one UMD trace; quantify CPU waits and bound gap attribution.

Wait records contain four exclusive buckets and an omitted total, NOT wait
start/end timestamps. Absent buckets are censored. Bounds below are temporal
possibility bounds, not causal savings. Own-process KMD gaps can contain foreign
GPU work. Firmware estimates use agx_gpu_frame_breakdown's documented bounds.
Only parsed completion/Execute records are retained, never the whole log.
"""
import argparse
import bisect
from collections import defaultdict
import json
import statistics

import agx_gpu_frame_breakdown as gpu

SITES = ('Unknown CpuFence PagingFence RenderFence MakeResident Allocate Lock '
         'Escape CopyEscape Submit InflightSlot BatchPoll BatchSlot SyncWriter '
         'SyncReaders Flush StagingBlit TransferMap QueryCreate QueryCommand '
         'QueryIncrement ContextRetire OwnerLock Event QueryWriters BoCreate '
         'CountMap SceneEscape PoolMapEscape NativeReadyEscape CompletionEscape '
         'TimestampEscape FrameArmEscape AllocateCanonical LockHash LockTransfer '
         'Evict MapGpuVa ReserveGpuVa FreeGpuVa').split()
PHASES = 'other query_begin query_end query_resolve draw dispatch submit'.split()
TARGETS = {'paging': {phase * 256 + 2 for phase in range(7)},
           'pool_map': {phase * 256 + 28 for phase in range(7)},
           'draw_paging': {0x402}, 'draw_pool_map': {0x41c}}


def wait_bound(words, keys):
    """Microsecond lower/upper for selected keys; rounded to receipt precision.

    An omitted key cannot exceed the fourth retained bucket (plus <1 us
    quantization per bucket, deliberately not invented as a measured value).
    The omitted total is shared: target upper bounds are NOT additive.
    """
    if len(words) != 16 or words[0] != 1 or words[3] & ~4:
        raise ValueError('invalid wait profiler record')
    top = {words[i]: words[i + 2] for i in range(4, 16, 3) if words[i]}
    lower = sum(value for key, value in top.items() if key in keys)
    extra = min(words[2], len(keys - top.keys()) * min(top.values(), default=0))
    return lower, lower + extra


def overlap(interval, intervals):
    a, b = interval
    if b <= a:
        return 0
    index = max(0, bisect.bisect_right(intervals, (a, float('inf'))) - 1)
    total = 0
    for i in range(index, len(intervals)):
        x, y = intervals[i]
        if x >= b:
            break
        total += max(0, min(b, y) - max(a, x))
    return total


def stats(values):
    return dict(mean=statistics.mean(values), median=statistics.median(values),
                p95=sorted(values)[min(len(values) - 1, int(len(values) * .95))])


def analyze(lines, skip=.3):
    if not 0 <= skip < 1:
        raise ValueError('skip must be in [0,1)')
    executions, last = {}, {}
    long_paging = []

    def collect():
        for number, line in enumerate(lines, 1):
            if line.startswith(('measure-gpu-completion ', 'measure-gpu-stages ',
                                'measure-gpu-prelude ', 'measure-gpu-tvb ',
                                'measure-d3d12-present ')):
                yield line
                continue
            tag = line.split(' ', 1)[0]
            if tag not in ('measure-d3d12-execute', 'measure-d3d12-replay-window',
                           'measure-d3d12-wait', 'measure-gpuva-paging-wait'):
                continue
            parts = line.split()
            meta = dict(p.split('=', 1) for p in parts[1:] if '=' in p)
            words = [int(p, 16) for p in parts[1:] if '=' not in p]
            thread = (int(meta['pid']), int(meta['tid']))
            qpc = int(meta['qpc'])
            if tag == 'measure-gpuva-paging-wait':
                long_paging.append((thread[0], qpc, number, words))
            elif tag == 'measure-d3d12-execute':
                key = (*thread, words[4])
                if key in executions:
                    raise ValueError('duplicate Execute serial')
                executions[key] = dict(pid=thread[0], serial=words[4], line=number,
                                       replay=words[2], submit=words[3], qpc=qpc)
                last[thread] = key
            elif tag == 'measure-d3d12-replay-window':
                if thread not in last:
                    raise ValueError('replay window without same-thread Execute')
                executions[last[thread]]['window'] = [gpu.u64(words, i) for i in (0, 2, 4, 6)]
            else:
                key = (*thread, words[1])
                if key not in executions:
                    raise ValueError('wait without same-thread Execute serial')
                wait_bound(words, set())
                executions[key]['wait'] = words

    pid, hz, jobs, presents = gpu.parse(collect())
    gpu.timeline(jobs, hz)
    first = int(len(presents) * skip)
    boundaries = presents[first:]
    if len(boundaries) < 2:
        raise ValueError('need two steady-state presents')
    frame_count = len(boundaries) - 1
    frames = [defaultdict(float) for _ in range(frame_count)]
    gaps, foreign = [], []
    for previous, job in zip(jobs, jobs[1:]):
        if job['sub'] > previous['cmp']:
            gap = (previous['cmp'], job['sub'])
            gaps.append(gap)
            if job['seq'] != previous['seq'] + 1:
                foreign.append(gap)
    ms = lambda ticks: ticks * 1000 / hz
    cpu_windows = []
    bucket_keys = {0x402, 0x41c, 0x602, 0x61c}
    chosen, evidence = [], []
    for ex in executions.values():
        if ex['pid'] != pid:
            continue
        if 'window' not in ex or 'wait' not in ex:
            raise ValueError('incomplete Execute record')
        start, replayed, submitted, frequency = ex['window']
        if frequency != hz or not start <= replayed <= submitted <= ex['qpc']:
            raise ValueError('invalid Execute window')
        # Only complete windows inside the measurement interval. No assigning
        # partial waits to a frame or prorating an aggregate wait by wall time.
        if start < boundaries[0] or submitted >= boundaries[-1]:
            continue
        index = bisect.bisect_right(boundaries, submitted) - 1
        f, words = frames[index], ex['wait']
        chosen.append(ex)
        cpu_windows.append((start, submitted))
        f['replay'] += ex['replay'] / 1000
        f['submit'] += ex['submit'] / 1000
        f['executes'] += 1
        f['omitted'] += words[2] / 1000
        total = words[2] + sum(words[i + 2] for i in range(4, 16, 3))
        # ExecuteBegin precedes OwnerLock/StageUploads and replay-window.start.
        # Phase zero and omitted waits can be outside the recorded window;
        # only retained command/submit phases support a nonzero lower bound.
        in_window = sum(words[i + 2] for i in range(4, 16, 3) if words[i] >> 8)
        f['all_waits'] += total / 1000
        for i in range(4, 16, 3):
            key, count, duration = words[i:i+3]
            if key:
                bucket_keys.add(key)
                f[f'bucket_{key}'] += duration / 1000
                f[f'calls_{key}'] += count
        gap_ms = ms(overlap((start, submitted), gaps))
        window_ms = ms(submitted - start)
        f['all_wait_overlap_lower'] += max(0, in_window / 1000 - (window_ms - gap_ms))
        f['all_wait_overlap_upper'] += min(total / 1000, gap_ms)
        for name, keys in TARGETS.items():
            lower, upper = wait_bound(words, keys)
            f[name + '_lower'] += lower / 1000
            f[name + '_upper'] += upper / 1000
            f[name + '_overlap_upper'] += min(upper / 1000, gap_ms)
        if words[3] & 4:
            f['truncated_executes'] += 1
        if len(evidence) < 3:
            evidence.append(dict(line=ex['line'], serial=ex['serial'], window=ex['window'], wait=words))
    cpu_windows.sort()
    if any(a[1] > b[0] for a, b in zip(cpu_windows, cpu_windows[1:])):
        raise ValueError('overlapping Execute windows: serial depth-one analysis required')
    sorted_jobs = sorted(jobs, key=lambda j: j['cmp'])
    job_times = [j['cmp'] for j in sorted_jobs]
    for i, (a, b) in enumerate(zip(boundaries, boundaries[1:])):
        f = frames[i]
        f['own_complete_to_next_submit'] = ms(overlap((a, b), gaps))
        f['foreign_sequence_gap'] = ms(overlap((a, b), foreign))
        f['inside_execute'] = ms(sum(overlap((max(a, x), min(b, y)), gaps)
                                    for x, y in cpu_windows if x < b and y > a))
        f['wall'] = ms(b - a)
        js = sorted_jobs[bisect.bisect_left(job_times, a):bisect.bisect_left(job_times, b)]
        f.update({'gpu_' + k: v for k, v in gpu.frame(js, f['wall'], hz).items()})
    summarize = lambda names: {k: stats([f[k] for f in frames]) for k in names}
    return dict(
        pid=pid, qpc_hz=hz, total_presents=len(presents), frames=frame_count,
        first_qpc=boundaries[0], last_qpc=boundaries[-1], executes=len(chosen),
        cpu_ms=summarize(('wall', 'replay', 'submit', 'all_waits', 'omitted')),
        buckets_ms={hex(k): dict(name=PHASES[k >> 8] + ':' + SITES[k & 255],
                                 **stats([f[f'bucket_{k}'] for f in frames]),
                                 calls_per_frame=statistics.mean(f[f'calls_{k}'] for f in frames))
                    for k in sorted(bucket_keys)},
        target_bounds_ms={name: dict(mean_lower=statistics.mean(f[name + '_lower'] for f in frames),
                                    mean_upper=statistics.mean(f[name + '_upper'] for f in frames))
                          for name in TARGETS},
        gaps_ms=summarize(('own_complete_to_next_submit', 'inside_execute',
                           'foreign_sequence_gap', 'all_wait_overlap_lower',
                           'all_wait_overlap_upper', 'paging_overlap_upper', 'pool_map_overlap_upper')),
        gpu_estimates=summarize(sorted(k for k in frames[0] if k.startswith('gpu_'))),
        truncated_executes=sum(f['truncated_executes'] for f in frames),
        long_paging_waits=[dict(line=n, qpc=q, words=w) for p, q, n, w in long_paging
                           if p == pid and boundaries[0] <= q < boundaries[-1]],
        evidence=evidence,
        caveats=['Top-four buckets are lower observations; target upper bounds share omitted time.',
                 'Bounds are at integer-us trace precision (each emitted duration truncates <1 us).',
                 'Own-process gaps may contain other processes GPU work; waits lack interval timestamps.',
                 'GPU spans include launch overhead; stale firmware starts are not used.',
                 'CPU wait time overlaps GPU work: do not add the two rankings.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('--skip', type=float, default=.3)
    args = parser.parse_args()
    with open(args.log, errors='strict') as handle:
        print(json.dumps(analyze(handle, args.skip), indent=2))


if __name__ == '__main__':
    main()
