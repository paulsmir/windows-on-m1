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
rc79 TVB (measure-gpu-tvb: the process buffer manager's cumulative firmware
overflow_count and max_pages after each render): a render's partial renders
are the overflow increase since the process's previous render with a row
(unknown for the first, or after a decrease); partials sums all renders,
rc_partials/main_partials the roles, partial_jobs the renders with a known
count, tvb_max_pages the largest max_pages seen in the frame.
JSON summary (medians over frames after --skip) on stdout.

--kmd-timing decodes the KMD's Wom1JobTiming971 registry receipt
(render_job_timing.h ADMISSION_JOB_TIMING_STATE v1: the last 64 jobs' QPC per
phase, backend_platform_windows.c AdmissionJobTimingMarkWindows) into
median microseconds per KMD interval; "queued" intervals only count a job
submitted before the previous job's completion was reported, i.e. one the
one-job-at-a-time worker kept waiting. Version 2 (rc81) adds per slot the
worker's processor (index | efficiency class << 8 | valid << 31) at the
build and at completion and the BeginJob flags, and the header's worker
flags (dedicated thread, pinned, pinned class and core count). by_kind
splits the CPU-bound intervals into render (a TA kick) and compute jobs;
by_core_class (v2) splits them by the class of the processor that ran them
(build and BeginJob: CpuBuild, completion to job end: CpuComplete);
begin_flags counts the BeginJobs that found the process mapping generation
unchanged since the bind (a skipped reparse would have applied) and those
of private-scene renders. Version 3 (rc83) appends per slot eight QPC
stamps inside the build and the completion (ADMISSION_JOB_SUB); sub_us
gives the medians of the render build split stage (BackendBefore ->
image staged), copy (template objects copied to the firmware-shared
queue objects), relocate, manager (buffer-manager restore), prelude (v5
compute prelude), flush and kick (flushes done -> 3D kick), and of the
completion split save (Complete -> buffer-manager saved), timestamp (->
JobEnd), release (JobEnd -> arena released) and notify; build_flags counts
the InitBM, queue-initialising, prelude, second-slot and restored-manager
builds. rc90 (Wom1JobBuildAhead): ahead_flags counts the jobs whose image
was released right after their kick, that were bound while their
predecessor ran, and that the predecessor's worker claimed at its
completion (no dispatch hop); queued_complete_to_kick is split into
chained and dispatched jobs; ahead_refusals histograms the
render_job_ahead.c refusal numbers per step (release, prebind, chain).
Version 4 (rc95, Wom1JobDepth2 receipt mode) appends five Sub stamps;
depth2 gives the depth-2 decision for each job's Prebound successor
(eligible count, render_job_window.c refusals), the lead from that bind to
the job's completion, and the TA / compute-prelude stamp lags against their
done pointers and the completion (U4: a stamp held until the completion is
gated by the notifier threshold).
rc98 (Wom1JobDepth2Caps): depth2.with_caps counts decisions taken with the
rc98 capabilities (second sidecar set, compute-queue and TA-on-3D
Barriers), sidecar_set_b the jobs whose compute work used job-word set B,
sidecar_set_a the v5 preludes that used set A.
The KMD also exports Wom1JobTiming971Hot, the same layout, only
from a ring whose 64 submissions fall within 500 ms (an application's
steady state, not the idle desktop after it).
rc102 (Wom1JobDepth2Kick, the wired depth-2 window, still v4): depth2.kicked
counts jobs kicked while their predecessor ran, kicks_while_busy those whose
kick (backend_after) precedes the predecessor's completion (proof that two
jobs were in flight), max_in_flight, batch_sizes the reported completions by
batch (2 = the pair the deferred notifier ended together, U4) and barriers
the leading Barrier classes. --kmd-window decodes the cumulative
Wom1JobWindow receipt.
rc100: --kmd-poll decodes Wom1JobPoll (the completion poll: predictive or
rc72 mode, busy_fraction = job time not asleep, sleeps and timer lateness,
completions first seen after a sleep); --kmd-poll-before subtracts an
earlier snapshot of the same boot (one benchmark run).
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
    comp, stage, prelude, tvb, presents, rates = {}, {}, {}, {}, {}, set()
    for line in lines:
        tag = line.split(' ', 1)[0]
        if tag not in ('measure-gpu-completion', 'measure-gpu-stages',
                       'measure-gpu-prelude', 'measure-gpu-tvb',
                       'measure-d3d12-present'):
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
        elif tag == 'measure-gpu-tvb' and len(words) == 4:
            tvb.setdefault(pid, {})[u64(words, 0)] = (words[2], words[3])
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
        j['tvb'] = tvb.get(pid, {}).get(j['seq'])
    last = None
    for j in jobs:
        j['partials'] = None
        if j['kind'] != RENDER or j['tvb'] is None:
            continue
        if last is not None and j['tvb'][0] >= last:
            j['partials'] = j['tvb'][0] - last
        last = j['tvb'][0]
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
        prelude_ms=sum(j['prelude'] for j in render if j['prelude'] is not None),
        partials=sum(j['partials'] or 0 for j in render),
        partial_jobs=sum(1 for j in render if j['partials'] is not None),
        rc_partials=sum(j['partials'] or 0 for j in big),
        main_partials=(main['partials'] or 0) if main else 0,
        tvb_max_pages=max((j['tvb'][1] for j in render if j['tvb']), default=0))


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


def positions(lines, skip=0.3):
    """rc94 --jobs: medians per job position of the frames with the most
    common job sequence (R render, C compute). A job's launch is its compute
    kick, or a render's prelude kick (lo -> prelude start); to_ta is a
    render's lo -> TA end (launch, prelude and TA). late: submitted (KMD
    SubmitCommand) no earlier than the previous own job's completion, so
    nothing of it could be built while that job ran. foreign: the completion
    sequence skipped a number, i.e. another process's job (the ring is per
    adapter, AdmissionGpuCompletionRead filters by process) completed between
    the previous own job and this one."""
    pid, hz, jobs, presents = parse(lines)
    timeline(jobs, hz)
    previous = {j['seq']: p for p, j in zip([None] + jobs[:-1], jobs)}
    by_cmp = sorted(jobs, key=lambda j: j['cmp'])
    keys = [j['cmp'] for j in by_cmp]
    first = int(len(presents) * skip)
    framed = []
    for a, b in zip(presents[first:-1], presents[first + 1:]):
        js = by_cmp[bisect.bisect_left(keys, a):bisect.bisect_left(keys, b)]
        if js:
            framed.append((''.join('R' if j['kind'] == RENDER else 'C' for j in js), js))
    counts = {}
    for sig, _ in framed:
        counts[sig] = counts.get(sig, 0) + 1
    if not counts:
        return dict(signature='', frames=0, of_frames=0, positions=[])
    signature = max(counts, key=lambda s: (counts[s], s))
    ms = lambda q: q * 1e3 / hz
    cols = [dict() for _ in signature]
    for sig, js in framed:
        if sig != signature:
            continue
        for c, j in zip(cols, js):
            p = previous[j['seq']]
            values = dict(span=j['span'], detect=j['detect'],
                          late=1.0 if p is not None and j['sub'] >= p['cmp'] else 0.0,
                          foreign=1.0 if p is not None and j['seq'] - p['seq'] > 1 else 0.0)
            if j['kind'] == COMPUTE:
                values.update(exact=j['exact'], launch=j['kick'])
            else:
                values.update(d3=j['d3'], launch=j['prelude_kick'], prelude=j['prelude'], ta=j['ta'],
                              to_ta=ms(j['ta_q'] - j['lo']) if j['ta_q'] is not None else None)
            for k, v in values.items():
                if v is not None:
                    c.setdefault(k, []).append(v)
    out = []
    for kind, c in zip(signature, cols):
        row_ = dict(kind=kind)
        for k, v in c.items():
            row_[k] = statistics.mean(v) if k in ('late', 'foreign') else statistics.median(v)
        out.append(row_)
    return dict(signature=signature, frames=counts[signature], of_frames=len(framed), positions=out)


KMD_PHASES = ('submit', 'worker', 'begin_before', 'begin_after', 'backend_before', 'kick_ta',
              'kick_3d', 'backend_after', 'first_progress', 'complete', 'job_end', 'notify')
KMD_HEADER = struct.Struct('<IIIIQQ')
KMD_SLOT = struct.Struct('<IIIIQQ12Q4QII')
KMD_SLOT_V2 = struct.Struct('<IIIIQQ12Q4QIIIIII')
KMD_SUBS = ('staged', 'copied', 'relocated', 'managed', 'prelude', 'flushed', 'saved', 'released')
KMD_SLOT_V3 = struct.Struct('<IIIIQQ12Q4QIIIIII8Q')
# rc95 v4: five more Sub stamps (Wom1JobDepth2 receipt mode), v3 offsets kept.
KMD_SUBS_V4 = KMD_SUBS + ('successor_bound', 'ta_done', 'ta_stamp', 'prelude_done',
                          'prelude_stamp')
KMD_SLOT_V4 = struct.Struct('<IIIIQQ12Q4QIIIIII13Q')
KMD_SLOTS = 64
FLAG_MAPPING_UNCHANGED, FLAG_PRIVATE_SCENE, FLAG_BEGIN_JOB = 1, 2, 4
BUILD_FLAGS = dict(init_bm=0x8, init_queues=0x10, prelude=0x20, slot1=0x40, manager_restored=0x80)
# rc90 build-ahead (Wom1JobBuildAhead): slot flags and the AheadRefusals word
# (render_job_ahead.c refusal numbers: release bits 0-7, prebind 8-15, chain
# 16-23).
AHEAD_FLAGS = dict(released=0x100, prebound=0x200, chained=0x400)
AHEAD_STEPS = dict(release=0, prebind=8, chain=16)
# rc95 (Wom1JobDepth2 receipt mode): the successor could have been kicked to
# the firmware while this job ran (render_job_window.c refusal in bits 24-31).
FLAG_DEPTH2_ELIGIBLE = 0x800
DEPTH2_SHIFT = 24
# rc98 (Wom1JobDepth2Caps): the job's compute work used the sidecar's
# job-word set B; its depth-2 decision had the rc98 capabilities declared.
FLAG_SIDECAR_SET_B = 0x1000
FLAG_DEPTH2_CAPS = 0x2000
# rc102 (Wom1JobDepth2Kick): kicked behind its predecessor, reported in its
# predecessor's batch, leading Barrier class in bits 17-19.
FLAG_DEPTH2_KICKED = 0x4000
FLAG_DEPTH2_BATCHED = 0x8000
DEPTH2_BARRIER_SHIFT = 17
BARRIER_CLASSES = {1: 'ta_on_compute', 2: 'ta_on_3d', 3: 'compute_on_3d', 4: 'compute_on_compute'}


def _sub_spans(s, us):
    """rc83 v3: the render build and completion split by the Sub stamps; an
    unreached stamp (0) is skipped, so each span starts at the last one."""
    out = {}
    build = [('stage', 'staged'), ('copy', 'copied'), ('relocate', 'relocated'),
             ('manager', 'managed'), ('prelude', 'prelude'), ('flush', 'flushed'),
             ('kick', 'kick_3d')]
    last = s['backend_before']
    for name, point in build:
        at = s.get('sub_' + point) if point != 'kick_3d' else s['kick_3d']
        if at and last:
            out[name] = us(last, at)
            last = at
    complete = [('save', 'sub_saved'), ('timestamp', 'job_end'), ('release', 'sub_released'),
                ('notify', 'notify')]
    last = s['complete']
    for name, point in complete:
        at = s.get(point)
        if at and last:
            out[name] = us(last, at)
            last = at
    return out
CPU_INTERVALS = ('begin_job', 'build', 'complete_to_job_end', 'job_end_to_notify')


def _medians(spans):
    return dict(median_us={k: statistics.median(v) for k, v in spans.items() if v},
                samples={k: len(v) for k, v in spans.items()})


def kmd_job_timing(data):
    """Median microseconds per KMD interval of a Wom1JobTiming971 snapshot."""
    version, size, count, worker, hz, _ = KMD_HEADER.unpack_from(data, 0)
    layout = {1: KMD_SLOT, 2: KMD_SLOT_V2, 3: KMD_SLOT_V3, 4: KMD_SLOT_V4}.get(version)
    if (layout is None or size != KMD_HEADER.size + KMD_SLOTS * layout.size or
            len(data) < size or not hz):
        raise ValueError('not an ADMISSION_JOB_TIMING_STATE v1/v2/v3/v4 snapshot')
    sub_names = KMD_SUBS_V4 if version >= 4 else KMD_SUBS
    slots = []
    for i in range(KMD_SLOTS):
        fields = layout.unpack_from(data, KMD_HEADER.size + i * layout.size)
        q = dict(zip(KMD_PHASES, fields[6:18]))
        if fields[0] and all(q[p] for p in ('submit', 'worker', 'backend_after', 'complete', 'notify')):
            cpu_build, cpu_complete, flags, ahead = fields[24:28] if version >= 2 else (0, 0, 0, 0)
            subs = (dict(zip(('sub_' + n for n in sub_names), fields[28:28 + len(sub_names)]))
                    if version >= 3 else {})
            slots.append(dict(fence=fields[0], pid=fields[1], cpu_build=cpu_build,
                              cpu_complete=cpu_complete, flags=flags, ahead=ahead, **q, **subs))
    slots.sort(key=lambda s: s['submit'])
    us = lambda a, b: (b - a) * 1e6 / hz
    spans = {k: [] for k in ('begin_job', 'build', 'worker_to_kick', 'kick_to_progress',
                             'progress_to_complete', 'complete_to_notify',
                             'queued_notify_to_worker', 'queued_complete_to_kick',
                             'queued_complete_to_kick_chained',
                             'queued_complete_to_kick_dispatched')}
    kinds = {kind: {k: [] for k in CPU_INTERVALS} for kind in ('render', 'compute')}
    subs = {}
    classes = {'render': {}, 'compute': {}}
    for prev, s in zip([None] + slots[:-1], slots):
        kind = 'render' if s['kick_ta'] else 'compute'
        cpu = {}
        if s['begin_after'] and s['begin_before']:
            cpu['begin_job'] = (us(s['begin_before'], s['begin_after']), s['cpu_build'])
            spans['begin_job'].append(cpu['begin_job'][0])
        if s['backend_before']:
            cpu['build'] = (us(s['backend_before'], s['backend_after']), s['cpu_build'])
            spans['build'].append(cpu['build'][0])
        if s['job_end']:
            cpu['complete_to_job_end'] = (us(s['complete'], s['job_end']), s['cpu_complete'])
            cpu['job_end_to_notify'] = (us(s['job_end'], s['notify']), s['cpu_complete'])
        for name, (value, where) in cpu.items():
            kinds[kind][name].append(value)
            if where >> 31:
                bucket = classes[kind].setdefault(where >> 8 & 0xff, {k: [] for k in CPU_INTERVALS})
                bucket[name].append(value)
        if version >= 3 and kind == 'render':
            for name, value in _sub_spans(s, us).items():
                subs.setdefault(name, []).append(value)
        spans['worker_to_kick'].append(us(s['worker'], s['backend_after']))
        if s['first_progress']:
            spans['kick_to_progress'].append(us(s['backend_after'], s['first_progress']))
            spans['progress_to_complete'].append(us(s['first_progress'], s['complete']))
        spans['complete_to_notify'].append(us(s['complete'], s['notify']))
        if prev is not None and s['submit'] < prev['notify'] <= s['worker']:
            spans['queued_notify_to_worker'].append(us(prev['notify'], s['worker']))
            spans['queued_complete_to_kick'].append(us(prev['complete'], s['backend_after']))
            spans['queued_complete_to_kick_chained' if s['flags'] & AHEAD_FLAGS['chained']
                  else 'queued_complete_to_kick_dispatched'].append(
                      us(prev['complete'], s['backend_after']))
    out = dict(version=version, jobs_total=count, slots=len(slots),
               pids=sorted({s['pid'] for s in slots}),
               median_us={k: statistics.median(v) for k, v in spans.items() if v},
               samples={k: len(v) for k, v in spans.items()},
               by_kind={kind: _medians(v) for kind, v in kinds.items()})
    if version >= 2:
        out['worker_flags'] = dict(thread=bool(worker & 1), pinned=bool(worker & 2),
                                   efficiency_class=worker >> 8 & 0xff, cores=worker >> 16 & 0xff)
        out['by_core_class'] = {kind: {c: _medians(v) for c, v in sorted(buckets.items())}
                                for kind, buckets in classes.items()}
        begun = [s for s in slots if s['flags'] & FLAG_BEGIN_JOB]
        out['begin_flags'] = dict(
            begin_jobs=len(begun),
            mapping_unchanged=sum(1 for s in begun if s['flags'] & FLAG_MAPPING_UNCHANGED),
            private_scene=sum(1 for s in begun if s['flags'] & FLAG_PRIVATE_SCENE))
    if version >= 3:
        out['sub_us'] = _medians(subs)
        out['build_flags'] = {name: sum(1 for s in slots if s['flags'] & bit)
                              for name, bit in BUILD_FLAGS.items()}
        out['ahead_flags'] = {name: sum(1 for s in slots if s['flags'] & bit)
                              for name, bit in AHEAD_FLAGS.items()}
        refusals = {}
        for step, shift in AHEAD_STEPS.items():
            counts = {}
            for s in slots:
                value = s['ahead'] >> shift & 0xff
                if value:
                    counts[value] = counts.get(value, 0) + 1
            refusals[step] = dict(sorted(counts.items()))
        out['ahead_refusals'] = refusals
    if version >= 4:
        out['depth2'] = _depth2(slots, us)
    return out


def _depth2(slots, us):
    """rc95 v4 (Wom1JobDepth2 receipt mode). eligible/refusals: the depth-2
    decision for each job's Prebound successor (render_job_window.c);
    successor_lead_us: from that bind to the job's completion, the time a
    depth-2 kick would have had the successor in the firmware already.
    stamp_lag_us (U4): TA and prelude stamps against their done pointers and
    the job's completion; a stamp that waits for the completion is held by
    the notifier threshold, so a depth-2 job N would be seen only once N+1
    ended. stamp_unseen counts jobs whose done pointer was seen but whose
    stamp was not before the poll that completed the job (the same
    verdict: held until the threshold)."""
    refusals, unseen = {}, dict(ta=0, prelude=0)
    lead, lag = [], {k: [] for k in ('ta_done_to_stamp', 'ta_stamp_to_complete',
                                     'prelude_done_to_stamp', 'prelude_stamp_to_complete')}
    for s in slots:
        value = s['ahead'] >> DEPTH2_SHIFT & 0xff
        if value:
            refusals[value] = refusals.get(value, 0) + 1
        if s.get('sub_successor_bound'):
            lead.append(us(s['sub_successor_bound'], s['complete']))
        for queue in ('ta', 'prelude'):
            done, stamp = s.get('sub_%s_done' % queue), s.get('sub_%s_stamp' % queue)
            if done and stamp:
                lag[queue + '_done_to_stamp'].append(us(done, stamp))
            elif done:
                unseen[queue] += 1
            if stamp:
                lag[queue + '_stamp_to_complete'].append(us(stamp, s['complete']))
    by_fence = {s['fence']: s for s in slots}
    kicked = [s for s in slots if s['flags'] & FLAG_DEPTH2_KICKED]
    busy = [s for s in kicked if (s['fence'] - 1) in by_fence and
            s['backend_after'] < by_fence[s['fence'] - 1]['complete']]
    batched = sum(1 for s in slots if s['flags'] & FLAG_DEPTH2_BATCHED)
    barriers = {}
    for s in kicked:
        name = BARRIER_CLASSES.get(s['flags'] >> DEPTH2_BARRIER_SHIFT & 7, 'none')
        barriers[name] = barriers.get(name, 0) + 1
    wired = dict(kicked=len(kicked), kicks_while_busy=len(busy),
                 max_in_flight=2 if busy else 1,
                 batch_sizes={1: len(slots) - 2 * batched, 2: batched},
                 barriers=dict(sorted(barriers.items())),
                 kick_lead_us=_medians(dict(lead=[
                     us(s['backend_after'], by_fence[s['fence'] - 1]['complete']) for s in busy])))
    return dict(wired=wired,
                eligible=sum(1 for s in slots if s['flags'] & FLAG_DEPTH2_ELIGIBLE),
                decided=sum(1 for s in slots if s.get('sub_successor_bound')),
                refusals=dict(sorted(refusals.items())),
                successor_lead_us=_medians(dict(lead=lead)),
                stamp_lag_us=_medians(lag), stamp_unseen=unseen,
                with_caps=sum(1 for s in slots if s['flags'] & FLAG_DEPTH2_CAPS),
                sidecar_set_b=sum(1 for s in slots if s['flags'] & FLAG_SIDECAR_SET_B),
                sidecar_set_a=sum(1 for s in slots if s['flags'] & BUILD_FLAGS['prelude'] and
                                  not s['flags'] & FLAG_SIDECAR_SET_B))


# rc100: Wom1JobPoll, ADMISSION_JOB_POLL_STATE (render_job_timing.h) v1.
POLL_HEADER = struct.Struct('<IIIIII8I')
POLL_STATS = ('jobs', 'predicted_jobs', 'sleeps', 'spins', 'ticks', 'seen_after_sleep',
              'job_us', 'slept_us', 'sleep_requested_us', 'late_us', 'late_max_us',
              'seen_after_sleep_window_us')
POLL_STATS_STRUCT = struct.Struct('<12Q')
POLL_CONTEXT = struct.Struct('<QIIII')
POLL_ENTRY = struct.Struct('<Q4IIIII')
POLL_CONTEXTS, POLL_KEYS = 8, 64
POLL_BYTES = POLL_HEADER.size + POLL_STATS_STRUCT.size + POLL_CONTEXTS * POLL_CONTEXT.size + \
    POLL_KEYS * POLL_ENTRY.size
POLL_PREDICTIVE = 0xffffffff


def _registry_value(data, name):
    """Raw bytes, or the hex of `name` in a kmd-receipts text dump."""
    try:
        text = data.decode('ascii')
    except UnicodeDecodeError:
        return data
    for line in text.splitlines():
        if line.startswith(name + ' '):
            return bytes.fromhex(line.split()[1])
    raise ValueError(name + ' not found')


def kmd_job_poll(data, before=None):
    """rc100 completion-poll receipt: mode, CPU busy fraction of the poll
    (job time not spent asleep), sleeps and their lateness, completions seen
    by the first poll after a sleep. `before`: an earlier snapshot of the
    same boot; the counters are then the run's differences."""
    def stats(raw):
        raw = _registry_value(raw, 'Wom1JobPoll')
        version, size, spin, flags, _, _, *lateness = POLL_HEADER.unpack_from(raw, 0)
        if version != 1 or size != POLL_BYTES or len(raw) < size:
            raise ValueError('not an ADMISSION_JOB_POLL_STATE v1 snapshot')
        values = dict(zip(POLL_STATS, POLL_STATS_STRUCT.unpack_from(raw, POLL_HEADER.size)))
        keys = sum(1 for i in range(POLL_KEYS) if POLL_ENTRY.unpack_from(
            raw, POLL_HEADER.size + POLL_STATS_STRUCT.size + POLL_CONTEXTS * POLL_CONTEXT.size +
            i * POLL_ENTRY.size)[0])
        return spin, flags, lateness, values, keys
    spin, flags, lateness, now, keys = stats(data)
    if before is not None:
        old = stats(before)[3]
        now = {k: (v if k == 'late_max_us' else v - old[k]) for k, v in now.items()}
    jobs, job_us = now['jobs'], now['job_us']
    out = dict(mode='predictive' if spin == POLL_PREDICTIVE else 'rc72 spin %u us' % spin,
               high_resolution_timer=bool(flags & 1), keys=keys, slack_us=max(lateness), **now)
    if job_us:
        out['busy_fraction'] = round((job_us - min(now['slept_us'], job_us)) / job_us, 4)
    if jobs:
        out['predicted_share'] = round(now['predicted_jobs'] / jobs, 4)
        out['seen_after_sleep_share'] = round(now['seen_after_sleep'] / jobs, 4)
    if now['sleeps']:
        out['late_mean_us'] = round(now['late_us'] / now['sleeps'], 1)
    if now['seen_after_sleep']:
        out['seen_after_sleep_window_mean_us'] = round(
            now['seen_after_sleep_window_us'] / now['seen_after_sleep'], 1)
    return out


# rc102: Wom1JobWindow, ADMISSION_JOB_WINDOW_RECEIPT (render_job_window.h) v1.
WINDOW_RECEIPT = struct.Struct('<IIII3I5IIIIIII')


def kmd_job_window(data):
    """rc102 cumulative receipt of the wired depth-2 window."""
    raw = _registry_value(data, 'Wom1JobWindow')
    fields = WINDOW_RECEIPT.unpack_from(raw, 0)
    version, size = fields[0], fields[1]
    if version != 1 or size != WINDOW_RECEIPT.size or len(raw) < size:
        raise ValueError('not an ADMISSION_JOB_WINDOW_RECEIPT v1 snapshot')
    kicks, max_in_flight = fields[2], fields[3]
    reported = fields[4:7]
    barriers = fields[7:12]
    adopted, failures, last_failure, aborts, last_kicked = fields[12:17]
    return dict(kicks=kicks, max_in_flight=max_in_flight,
                batch_sizes={1: reported[1], 2: reported[2]},
                barriers={BARRIER_CLASSES[i]: barriers[i] for i in BARRIER_CLASSES},
                adopted=adopted, kick_failures=failures, last_kick_failure_line=last_failure,
                reset_aborts=aborts, last_kicked_fence=last_kicked)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', nargs='?')
    parser.add_argument('--skip', type=float, default=0.3,
                        help='fraction of leading frames to drop (load, warm-up)')
    parser.add_argument('--csv')
    parser.add_argument('--kmd-timing', help='decode a Wom1JobTiming971(Hot) registry snapshot instead')
    parser.add_argument('--kmd-poll', help='rc100: decode a Wom1JobPoll snapshot (raw or kmd-receipts text)')
    parser.add_argument('--kmd-poll-before', help='an earlier Wom1JobPoll snapshot of the same boot (deltas)')
    parser.add_argument('--kmd-window', help='rc102: decode a Wom1JobWindow snapshot (raw or kmd-receipts text)')
    parser.add_argument('--jobs', action='store_true',
                        help='rc94: add per-position medians of the most common job sequence')
    args = parser.parse_args()
    if args.kmd_window:
        with open(args.kmd_window, 'rb') as handle:
            print(json.dumps(kmd_job_window(handle.read()), indent=1))
        return
    if args.kmd_poll:
        with open(args.kmd_poll, 'rb') as handle:
            data = handle.read()
        before = None
        if args.kmd_poll_before:
            with open(args.kmd_poll_before, 'rb') as handle:
                before = handle.read()
        print(json.dumps(kmd_job_poll(data, before), indent=1))
        return
    if args.kmd_timing:
        with open(args.kmd_timing, 'rb') as handle:
            print(json.dumps(kmd_job_timing(handle.read()), indent=1, default=str))
        return
    if not args.log:
        parser.error('a UMD log or --kmd-timing is required')
    with open(args.log, errors='ignore') as handle:
        lines = [line for line in handle if line.startswith(('measure-gpu-', 'measure-d3d12-present'))]
    frames, summary = analyze(lines, args.skip)
    if args.jobs:
        summary['jobs_by_position'] = positions(lines, args.skip)
    if args.csv and frames:
        with open(args.csv, 'w', newline='') as out:
            writer = csv.DictWriter(out, fieldnames=list(frames[0]))
            writer.writeheader()
            writer.writerows(frames)
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
