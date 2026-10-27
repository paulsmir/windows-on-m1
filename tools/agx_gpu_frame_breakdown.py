#!/usr/bin/env python3
"""Per-frame GPU timeline of a D3D12 UMD log from the RC35 completion stream.

Usage: agx_gpu_frame_breakdown.py UMD.log [--skip 0.3] [--csv frames.csv]
       agx_gpu_frame_breakdown.py --kmd-timing Wom1JobTiming971.bin

The KMD runs one job at a time (one ADMISSION_RENDER_PACKET per adapter;
backend_platform_windows.c kicks the next job only after it observed the
previous completion), so a job's GPU span lies inside
[max(its SubmitQpc, previous own job's CompleteQpc), its firmware end].
That bound is used instead of firmware starts: render starts (objects 28 and
the TA start) are stale on G13, only envelope compute jobs carry a valid
start (EXP1342).

Clocks: firmware ticks run at the QPC frequency with a constant offset
(EXP1112). The offset is the running maximum of (firmware end - CompleteQpc)
over +-WINDOW jobs, i.e. the job with the lowest completion-detection
latency; firmware ends are mapped to QPC with it. A job's span therefore
includes the KMD worker/kick time and any foreign (DWM) job queued ahead of
it; detection latency (CompleteQpc - mapped end) is reported separately.

Frame = interval between two measure-d3d12-present rows; a job belongs to
the frame in which the KMD observed its completion. Per frame roles:
  main      render job with the largest 3D time (fragment end - TA end)
  rc_big    other render jobs whose span is >= 1 ms (the v5 render-compute
            jobs: compute prelude + TA dominate, little 3D)
  r_small   other render jobs, span < 1 ms
  c_work    compute jobs with exact firmware time >= 0.05 ms
  c_empty   compute jobs with exact firmware time < 0.05 ms
  idle      wall time with no job of this process outstanding
rc76 fixed-cost split (per frame, sums):
  c_kick    compute jobs: mapped firmware start - max(submit, previous
            completion) = KMD completion handling of the previous job, the
            worker's BeginJob/build/kick and the firmware's start latency
  r_small_ta small render jobs: mapped TA end - the same bound (kick, firmware
            start, compute prelude and TA); r_small_3d is the rest
rc79 prelude split (completion v3 measure-gpu-prelude rows: the v5 render's
compute prelude firmware start/end, same timebase as the TA end):
  rc_prelude    rc_big jobs with a valid row: prelude end - prelude start
  rc_prelude_kick  their mapped prelude start - the span bound
  rc_ta         their TA end - prelude end (TA barrier wake-up, VDM, vertex
                shading, tiling); rc_prelude_jobs counts them
  prelude       the prelude time of every render job with a valid row
A row is stale (ignored) unless start <= end, the mapped start lies inside
the job's span bound and the end is no later than the TA end.
JSON summary (medians over frames after --skip) on stdout.

--kmd-timing decodes the KMD's Wom1JobTiming971 registry receipt
(render_job_timing.h ADMISSION_JOB_TIMING_STATE v1: the last 64 jobs' QPC per
phase, backend_platform_windows.c AdmissionJobTimingMarkWindows) into
median microseconds per KMD interval; "queued" intervals only count a job
submitted before the previous job's completion was reported, i.e. one the
one-job-at-a-time worker kept waiting.
"""
import argparse
import bisect
import csv
import json
import statistics
import struct

WINDOW = 64
SMALL_RENDER_MS = 1.0
EMPTY_COMPUTE_MS = 0.05
RENDER, COMPUTE = 1, 2


def u64(w, i):
    return w[i] | w[i + 1] << 32


def parse(lines):
    """Return (pid, hz, jobs sorted by sequence, present QPCs) of the
    process with the most completion records."""
    comp, stage, prelude, presents, rates = {}, {}, {}, {}, set()
    for line in lines:
        tag = line.split(' ', 1)[0]
        if tag not in ('measure-gpu-completion', 'measure-gpu-stages',
                       'measure-gpu-prelude', 'measure-d3d12-present'):
            continue
        fields = line.split()
        meta = dict(f.split('=', 1) for f in fields[1:] if '=' in f)
        pid, qpc = int(meta['pid']), int(meta['qpc'])
        words = [int(x, 16) for x in fields[1:] if '=' not in x]
        if tag == 'measure-d3d12-present':
            presents.setdefault(pid, []).append(qpc)
        elif tag == 'measure-gpu-completion' and len(words) == 13:
            seq = u64(words, 0)
            comp.setdefault(pid, {})[seq] = dict(
                seq=seq, kind=words[5], status=words[6],
                sub=u64(words, 7), cmp=u64(words, 9))
            if u64(words, 11):
                rates.add(u64(words, 11))
        elif tag == 'measure-gpu-stages' and len(words) == 10:
            stage.setdefault(pid, {})[u64(words, 0)] = (
                u64(words, 2), u64(words, 4), u64(words, 6), u64(words, 8))
        elif tag == 'measure-gpu-prelude' and len(words) == 6:
            prelude.setdefault(pid, {})[u64(words, 0)] = (u64(words, 2), u64(words, 4))
    if not comp:
        raise ValueError('no measure-gpu-completion rows')
    if len(rates) != 1:
        raise ValueError('need exactly one QPC frequency')
    pid = max(comp, key=lambda p: len(comp[p]))
    jobs = sorted(comp[pid].values(), key=lambda j: j['seq'])
    for j in jobs:
        start, end, _, ta_end = stage.get(pid, {}).get(j['seq'], (0, 0, 0, 0))
        j['start'], j['end'], j['ta_end'] = start, end, ta_end
        j['p_start'], j['p_end'] = prelude.get(pid, {}).get(j['seq'], (0, 0))
    return pid, rates.pop(), jobs, sorted(presents.get(pid, []))


def timeline(jobs, hz):
    """Annotate jobs with QPC-mapped firmware end, span bound and latency."""
    raw = [j['end'] - j['cmp'] if j['end'] and not j['status'] else None
           for j in jobs]
    prev = None
    for i, j in enumerate(jobs):
        near = [r for r in raw[max(0, i - WINDOW):i + WINDOW + 1] if r is not None]
        offset = max(near) if near else None
        j['lo'] = max(j['sub'], prev['cmp']) if prev else j['sub']
        valid = raw[i] is not None and offset is not None
        j['end_q'] = j['end'] - offset if valid else j['cmp']
        j['end_q'] = min(max(j['end_q'], j['lo']), j['cmp'])
        mapped = lambda ts: min(max(ts - offset, j['lo']), j['end_q']) if valid and ts else None
        j['ta_q'] = mapped(j['ta_end']) if j['kind'] == RENDER else None
        j['span'] = (j['end_q'] - j['lo']) * 1e3 / hz
        j['detect'] = (j['cmp'] - j['end_q']) * 1e3 / hz
        j['d3'] = None
        if j['kind'] == RENDER and valid and j['ta_end'] and j['end'] >= j['ta_end']:
            j['d3'] = min((j['end'] - j['ta_end']) * 1e3 / hz, j['span'])
        j['prelude'] = j['prelude_kick'] = j['ta'] = None
        if (j['kind'] == RENDER and valid and j['p_end'] and j['ta_end'] and
                j['p_start'] <= j['p_end'] <= j['ta_end'] and
                j['p_end'] - j['p_start'] < hz and
                j['lo'] <= j['p_start'] - offset <= j['end_q']):
            j['prelude'] = (j['p_end'] - j['p_start']) * 1e3 / hz
            j['prelude_kick'] = (j['p_start'] - offset - j['lo']) * 1e3 / hz
            j['ta'] = (j['ta_end'] - j['p_end']) * 1e3 / hz
        j['exact'] = None
        j['kick'] = None
        if (j['kind'] == COMPUTE and valid and j['start'] and
                0 <= j['end'] - j['start'] < hz):
            j['exact'] = (j['end'] - j['start']) * 1e3 / hz
            j['kick'] = (mapped(j['start']) - j['lo']) * 1e3 / hz
        prev = j
    return jobs


def frame(js, wall_ms, hz):
    render = [j for j in js if j['kind'] == RENDER]
    compute = [j for j in js if j['kind'] == COMPUTE]
    main = max(render, key=lambda j: j['d3'] or 0, default=None)
    rest = [j for j in render if j is not main]
    big = [j for j in rest if j['span'] >= SMALL_RENDER_MS]
    small = [j for j in rest if j['span'] < SMALL_RENDER_MS]
    work = [j for j in compute if (j['exact'] or 0) >= EMPTY_COMPUTE_MS]
    empty = [j for j in compute if (j['exact'] or 0) < EMPTY_COMPUTE_MS]
    span = lambda group: sum(j['span'] for j in group)
    d3 = lambda group: sum(j['d3'] or 0 for j in group)
    outstanding = sum(j['span'] + j['detect'] for j in js)
    kicks = [j['kick'] for j in compute if j['kick'] is not None]
    small_ta = [(j['ta_q'] - j['lo']) * 1e3 / hz for j in small if j['ta_q'] is not None]
    split = [j for j in big if j['prelude'] is not None]
    return dict(
        wall_ms=wall_ms, jobs=len(js), render_jobs=len(render),
        compute_jobs=len(compute), gpu_span_ms=span(js),
        detect_ms=sum(j['detect'] for j in js),
        idle_ms=max(0.0, wall_ms - outstanding),
        main_span_ms=main['span'] if main else 0.0,
        main_3d_ms=(main['d3'] or 0) if main else 0.0,
        rc_big_jobs=len(big), rc_big_span_ms=span(big), rc_big_3d_ms=d3(big),
        r_small_jobs=len(small), r_small_span_ms=span(small),
        r_small_3d_ms=d3(small),
        c_work_jobs=len(work), c_work_exact_ms=sum(j['exact'] for j in work),
        c_work_span_ms=span(work),
        c_empty_jobs=len(empty), c_empty_span_ms=span(empty),
        c_kick_jobs=len(kicks), c_kick_ms=sum(kicks),
        r_small_ta_ms=sum(small_ta),
        rc_prelude_jobs=len(split), rc_prelude_ms=sum(j['prelude'] for j in split),
        rc_prelude_kick_ms=sum(j['prelude_kick'] for j in split),
        rc_ta_ms=sum(j['ta'] for j in split),
        prelude_ms=sum(j['prelude'] for j in render if j['prelude'] is not None))


def analyze(lines, skip=0.3):
    pid, hz, jobs, presents = parse(lines)
    timeline(jobs, hz)
    by_cmp = sorted(jobs, key=lambda j: j['cmp'])
    keys = [j['cmp'] for j in by_cmp]
    first = int(len(presents) * skip)
    frames = []
    for a, b in zip(presents[first:-1], presents[first + 1:]):
        js = by_cmp[bisect.bisect_left(keys, a):bisect.bisect_left(keys, b)]
        if js:
            frames.append(frame(js, (b - a) * 1e3 / hz, hz))
    summary = dict(pid=pid, qpc_hz=hz, jobs=len(jobs), frames=len(frames),
                   failed_jobs=sum(1 for j in jobs if j['status']),
                   estimate='span = mapped firmware end - max(submit, previous completion); '
                            'includes KMD kick and foreign jobs queued ahead')
    if frames:
        summary['median'] = {k: statistics.median(f[k] for f in frames)
                             for k in frames[0]}
    return frames, summary


KMD_PHASES = ('submit', 'worker', 'begin_before', 'begin_after', 'backend_before', 'kick_ta',
              'kick_3d', 'backend_after', 'first_progress', 'complete', 'job_end', 'notify')
KMD_HEADER = struct.Struct('<IIIIQQ')
KMD_SLOT = struct.Struct('<IIIIQQ12Q4QII')
KMD_SLOTS = 64


def kmd_job_timing(data):
    """Median microseconds per KMD interval of a Wom1JobTiming971 snapshot."""
    version, size, count, _, hz, _ = KMD_HEADER.unpack_from(data, 0)
    if version != 1 or size != KMD_HEADER.size + KMD_SLOTS * KMD_SLOT.size or len(data) < size or not hz:
        raise ValueError('not an ADMISSION_JOB_TIMING_STATE v1 snapshot')
    slots = []
    for i in range(KMD_SLOTS):
        fields = KMD_SLOT.unpack_from(data, KMD_HEADER.size + i * KMD_SLOT.size)
        q = dict(zip(KMD_PHASES, fields[6:18]))
        if fields[0] and all(q[p] for p in ('submit', 'worker', 'backend_after', 'complete', 'notify')):
            slots.append(dict(fence=fields[0], pid=fields[1], **q))
    slots.sort(key=lambda s: s['submit'])
    us = lambda a, b: (b - a) * 1e6 / hz
    spans = {k: [] for k in ('begin_job', 'build', 'worker_to_kick', 'kick_to_progress',
                             'progress_to_complete', 'complete_to_notify',
                             'queued_notify_to_worker', 'queued_complete_to_kick')}
    for prev, s in zip([None] + slots[:-1], slots):
        if s['begin_after'] and s['begin_before']:
            spans['begin_job'].append(us(s['begin_before'], s['begin_after']))
        if s['backend_before']:
            spans['build'].append(us(s['backend_before'], s['backend_after']))
        spans['worker_to_kick'].append(us(s['worker'], s['backend_after']))
        if s['first_progress']:
            spans['kick_to_progress'].append(us(s['backend_after'], s['first_progress']))
            spans['progress_to_complete'].append(us(s['first_progress'], s['complete']))
        spans['complete_to_notify'].append(us(s['complete'], s['notify']))
        if prev is not None and s['submit'] < prev['notify'] <= s['worker']:
            spans['queued_notify_to_worker'].append(us(prev['notify'], s['worker']))
            spans['queued_complete_to_kick'].append(us(prev['complete'], s['backend_after']))
    return dict(jobs_total=count, slots=len(slots), pids=sorted({s['pid'] for s in slots}),
                median_us={k: statistics.median(v) for k, v in spans.items() if v},
                samples={k: len(v) for k, v in spans.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', nargs='?')
    parser.add_argument('--skip', type=float, default=0.3,
                        help='fraction of leading frames to drop (load, warm-up)')
    parser.add_argument('--csv')
    parser.add_argument('--kmd-timing', help='decode a Wom1JobTiming971 registry snapshot instead')
    args = parser.parse_args()
    if args.kmd_timing:
        with open(args.kmd_timing, 'rb') as handle:
            print(json.dumps(kmd_job_timing(handle.read()), indent=1))
        return
    if not args.log:
        parser.error('a UMD log or --kmd-timing is required')
    with open(args.log, errors='ignore') as handle:
        frames, summary = analyze(handle, args.skip)
    if args.csv and frames:
        with open(args.csv, 'w', newline='') as out:
            writer = csv.DictWriter(out, fieldnames=list(frames[0]))
            writer.writeheader()
            writer.writerows(frames)
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
