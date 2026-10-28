#!/usr/bin/env python3
"""RC85: where the start of a D3D12 app goes (GravityMark's first-frame stall).

Usage:
  agx_startup_breakdown.py UMD.log [--times TIMES.txt] [--json]
                           [--present-scan 400] [--top 12]

GravityMark scores the frames presented in a fixed ~167 s window that starts
at "Starting ... Benchmark" (frame 0 of its times file). Its first benchmark
frames create the scene's large resources and compile the draw-time shader
variants, so the frame 1 -> 2 gap of the times file (10-56 s, EXP2000-2015)
is spent inside that window and costs score directly.

The UMD log (APPLE_AGX diagnostics on) has one receipt per line:
  <name> hr=0x... pid=N tid=N session=N qpc=Q v0 v1 ...   (QPC at 24 MHz)
Receipts are written when an operation ends, so the time between two
receipts of one thread is charged to the receipt that closes it ("closing
receipt" attribution). Receipts that carry their own durations refine it:
  measure-bo-create {MiB, class, allocation us, GPU VA bind us, association
      us} (BO creations of at least 10 ms): the closing interval is split into
      KMD allocation, GPU VA bind (Map plus its paging-fence wait, which
      includes the residency of the new BO) and the rest;
  measure-d3d12-wait {1, serial, omitted us, flags, 4 x (phase << 8 | site,
      count, us)} (per Execute): the wait sites of that Execute (agx_win32_wait.h);
  measure-gpuva-map-time {MiB, residency us (MakeResident, its trims and its
      paging wait), Map us} and measure-gpuva-paging-wait {us, site, fence}
      (RC85, binds of at least 10 ms): the bind part of the closing interval;
  measure-d3d12-execute {lists, commands, replay us, submit us, serial} and
      measure-d3d12-replay-time {us x 6, count x 6} (draw, dispatch, copy,
      query, indirect, other; agx_d3d12_ddi.cpp ReplayTimingBucket).
The present thread is charged from the window start, so its buckets add up
to the wall time; another thread is charged only from its first receipt in
the window and its buckets are reported apart, as thread seconds (an upper
bound: they include the app's own time between two driver calls).

Phases:
  load   first receipt .. the present that opens the stall;
  stall  the largest gap between consecutive presents among the first
         --present-scan presents. It is NOT necessarily inside the scored
         window: in EXP2028 (D3D12 HD 50k) it was present 29 -> 30, the
         app's "Creating Scene" load step 2 s before frame 0.
With --times (rc93), the times file's frames are aligned to the app's
presents (the present-gap sequence that best matches the frame-gap
sequence), and three more phases are reported:
  pre_window  first receipt .. frame 0 (not scored: GravityMark's window
              starts at frame 0 and lasts a fixed ~167 s, score = frames);
  frame_1_2, frame_2_3  the scored first-frame stalls;
plus 'scored_stall': the seconds the first --window-frames frames spend
beyond the steady median frame, and the frames that time would have drawn.
For each phase: the time per bucket (shader translation and compile, PSO,
resources, BO allocation, GPU VA bind / paging, residency and trims, uploads,
KMD escapes, GPU submission and waits, other), the shader parts compiled
(count, total and longest closing interval), the uploads, the trims and
the longest single intervals. Nothing is written; output goes to stdout.
"""
import argparse
import json
import re
import sys
from collections import Counter, defaultdict

QPC_HZ = 24_000_000

LINE = re.compile(r'^(\S+) hr=(0x[0-9a-fA-F]+) pid=(\d+) tid=(\d+) session=\d+ qpc=(\d+)((?: [0-9a-fA-F]+)*)\s*$')

# closing receipt -> bucket
BUCKETS = {
    'shader-translate': (
        'measure-d3d12-shader', 'measure-d3d12-graphics-shader', 'measure-d3d12-graphics-shader-slots',
        'measure-d3d12-compute', 'measure-d3d12-library', 'measure-d3d12-root-views',
        'measure-d3d12-shader-cache'),
    'shader-compile': ('measure-d3d12-shader-stats',),
    'pso': (
        'measure-d3d12-pso', 'measure-d3d12-state', 'measure-d3d12-graphics', 'measure-d3d12-root-signature',
        'measure-d3d12-root-signature-1-1', 'measure-d3d12-command-signature', 'measure-d3d12-pso-worker'),
    'resource': (
        'measure-d3d12-heap', 'measure-d3d12-heap-sizes', 'measure-d3d12-create-heap-resource',
        'measure-d3d12-committed-size', 'measure-d3d12-committed-texture', 'measure-d3d12-texture',
        'measure-d3d12-placed', 'measure-d3d12-allocation-info', 'measure-d3d12-existing-allocation-info',
        'measure-d3d12-srv', 'measure-d3d12-uav', 'measure-d3d12-rtv', 'measure-d3d12-dsv', 'measure-d3d12-cbv',
        'measure-d3d12-descriptor-heap', 'measure-d3d12-descriptor-guard', 'measure-d3d12-query-heap',
        'measure-d3d12-fence', 'measure-d3d12-command-list', 'measure-d3d12-subresource-info',
        'measure-native-arena-class', 'measure-native-ring-fallback', 'measure-native-ring-map',
        'measure-bo-cpu-access', 'measure-d3d12-owner', 'measure-d3d12-upload-ring-init'),
    'residency': (
        'measure-d3d12-make-resident', 'measure-make-resident-trimmed', 'measure-make-resident-cant-trim',
        'measure-trim', 'measure-touch', 'measure-d3d12-trim-disabled', 'measure-gpu-only-map',
        'measure-d3d12-resident-headroom', 'reject-make-resident'),
    'upload': (
        'measure-d3d12-staged-upload', 'measure-d3d12-copy-texture', 'measure-d3d12-texture-copy',
        'measure-d3d12-reinterpret-copy', 'measure-d3d12-upload-ring', 'measure-d3d12-heap-staging'),
    'escape': ('measure-copy-escape-gate', 'measure-copy-escape-class', 'measure-native-ready-cache',
               'measure-soft-faults'),
}
NAME_BUCKET = {name: bucket for bucket, names in BUCKETS.items() for name in names}
# Execute-time receipts: command replay, GPU submission and CPU waits on the GPU
GPU_PREFIXES = ('measure-gpu-', 'measure-render-fence', 'measure-present', 'measure-perf-reason')
GPU_NAMES = (
    'execute', 'dispatch', 'draw', 'execute-indirect', 'counted-path', 'wait', 'wait-path', 'queue-wait',
    'owner-time', 'owner-submit', 'owner-fences', 'replay-window', 'replay-time', 'query-time', 'dispatch-time',
    'clear-absorb', 'present', 'prepare-present', 'presentation')
ORDER = ('shader-translate', 'shader-compile', 'pso', 'resource', 'bo-allocate', 'bo-bind-paging', 'residency',
         'upload', 'escape', 'gpu', 'other')

# agx_win32_wait.h enum AgxWaitSite (1-based; a versioned trace ABI)
WAIT_SITES = (
    None, 'CpuFence', 'PagingFence', 'RenderFence', 'MakeResident', 'Allocate', 'Lock', 'Escape', 'CopyEscape',
    'Submit', 'InflightSlot', 'BatchPoll', 'BatchSlot', 'SyncWriter', 'SyncReaders', 'Flush', 'StagingBlit',
    'TransferMap', 'QueryCreate', 'QueryCommand', 'QueryIncrement', 'ContextRetire', 'OwnerLock', 'Event',
    'QueryWriters', 'BoCreate', 'CountMap', 'SceneEscape', 'PoolMapEscape', 'NativeReadyEscape',
    'CompletionEscape', 'TimestampEscape', 'FrameArmEscape', 'AllocateCanonical', 'LockHash', 'LockTransfer',
    'Evict', 'MapGpuVa', 'ReserveGpuVa', 'FreeGpuVa')


# agx_d3d12_ddi.cpp ReplayTimingBucket: measure-d3d12-replay-time slots
REPLAY_KINDS = ('draw', 'dispatch', 'copy', 'query', 'indirect', 'other')


def bucket_of(name):
    if name in NAME_BUCKET:
        return NAME_BUCKET[name]
    if name.startswith(GPU_PREFIXES) or name.startswith('measure-d3d12-') and name[14:] in GPU_NAMES:
        return 'gpu'
    return 'other'


def parse(lines, present_limit=None):
    """[(qpc, name, hr, pid, tid, values)] of the well-formed receipt lines, in file order,
    up to and including the present_limit-th present (all lines when None)."""
    rows = []
    presents = 0
    for line in lines:
        m = LINE.match(line)
        if not m:
            continue
        values = [int(v, 16) for v in m.group(6).split()]
        rows.append((int(m.group(5)), m.group(1), int(m.group(2), 16), int(m.group(3)), int(m.group(4)), values))
        if m.group(1) == 'measure-d3d12-present':
            presents += 1
            if present_limit is not None and presents >= present_limit:
                break
    return rows


def main_pid(rows):
    """The process with the most presents (the app), else the most receipts."""
    presents = Counter(r[3] for r in rows if r[1] == 'measure-d3d12-present')
    return (presents or Counter(r[3] for r in rows)).most_common(1)[0][0] if rows else None


def find_stall(rows, scan):
    """(start qpc, end qpc, present index) of the largest present gap among the first scan presents."""
    presents = [r[0] for r in rows if r[1] == 'measure-d3d12-present'][:scan]
    if len(presents) < 2:
        return None
    k = max(range(1, len(presents)), key=lambda i: presents[i] - presents[i - 1])
    return presents[k - 1], presents[k], k


def split_bo_create(interval, values):
    """{bucket: ticks} of an interval closed by measure-bo-create."""
    alloc = values[2] * QPC_HZ // 1_000_000 if len(values) > 2 else 0
    bind = values[3] * QPC_HZ // 1_000_000 if len(values) > 3 else 0
    bind = min(bind, interval)
    alloc = min(alloc, interval - bind)
    return {'bo-bind-paging': bind, 'bo-allocate': alloc, 'resource': interval - bind - alloc}


def present_thread(rows, start, end):
    """The thread that presents (the app's render thread) in (start, end], else the busiest one."""
    presents = Counter(r[4] for r in rows if r[1] == 'measure-d3d12-present' and start < r[0] <= end + 1)
    if not presents:
        presents = Counter(r[4] for r in rows if start < r[0] <= end)
    return presents.most_common(1)[0][0] if presents else None


def analyse(rows, start, end, primary=None):
    """Bucket the receipts closing in (start, end] and collect the details of that window.

    The primary thread (default: the present thread) is charged from the window start, so its
    buckets add up to the wall time of the window. Another thread is charged only from its first
    receipt in the window (its time before that is unknown, mostly idle); its buckets are thread
    seconds kept apart in 'other_buckets'."""
    if primary is None:
        primary = present_thread(rows, start, end)
    out = {'seconds': (end - start) / QPC_HZ, 'primary_tid': primary, 'buckets': defaultdict(int),
           'other_buckets': defaultdict(int), 'receipts': Counter(),
           'shader': {'parts': 0, 'internal': 0, 'ticks': 0, 'max': 0, 'max_qpc': 0, 'translate': 0},
           'pso': {'count': 0, 'ticks': 0, 'max': 0},
           'bo': [], 'binds': [], 'trims': {'count': 0, 'bytes': 0, 'rounds': 0, 'pending': 0},
           'uploads': {'staged': 0, 'texture_copy': 0, 'bytes_bound': 0, 'recorded': 0, 'ticks': 0},
           'waits': defaultdict(lambda: [0, 0]), 'longest': [], 'threads': Counter(),
           'execute': {'count': 0, 'replay_us': 0, 'submit_us': 0, 'max_us': 0,
                       'kinds_us': [0] * 6, 'kinds_count': [0] * 6}}
    last = {primary: start}
    for r in sorted(rows, key=lambda r: r[0]):
        qpc, name, hr, _, tid, values = r
        if qpc <= start or qpc > end:
            if qpc <= start and tid == primary:
                last[tid] = start
            continue
        prev = last.get(tid)
        last[tid] = qpc
        interval = qpc - prev if prev is not None else 0
        out['receipts'][name] += 1
        out['threads'][tid] += interval
        buckets = out['buckets'] if tid == primary else out['other_buckets']
        if name == 'measure-gpuva-map-time' and len(values) >= 3:
            bind = min((values[1] + values[2]) * QPC_HZ // 1_000_000, interval)
            buckets['bo-bind-paging'] += bind
            buckets['bo-allocate'] += interval - bind
            out['binds'].append({'at': (qpc - start) / QPC_HZ, 'mib': values[0], 'residency_s': values[1] / 1e6,
                                 'map_s': values[2] / 1e6, 'map_hr': '%08x' % hr})
        elif name == 'measure-gpuva-paging-wait':
            buckets['bo-bind-paging'] += interval
            if out['binds'] and values:
                out['binds'][-1]['bind_wait_s'] = values[0] / 1e6
        elif name == 'measure-bo-create':
            for b, t in split_bo_create(interval, values).items():
                buckets[b] += t
            if len(values) >= 5:
                out['bo'].append({'at': (qpc - start) / QPC_HZ, 'mib': values[0], 'class': values[1],
                                  'allocate_s': values[2] / 1e6, 'bind_s': values[3] / 1e6,
                                  'associate_s': values[4] / 1e6, 'tid': tid})
        else:
            buckets[bucket_of(name)] += interval
        out['longest'].append((interval, qpc, name, tid, values[:6]))
        # per-kind times are wall time of the primary thread only; counts include every thread
        interval = interval if tid == primary else 0
        if name == 'measure-d3d12-shader-stats':
            s = out['shader']
            s['parts'] += 1
            s['internal'] += 1 if len(values) > 2 and values[2] & 0x100 else 0
            s['ticks'] += interval
            if interval > s['max']:
                s['max'], s['max_qpc'] = interval, qpc
        elif bucket_of(name) == 'shader-translate':
            out['shader']['translate'] += interval
        elif name == 'measure-d3d12-pso':
            p = out['pso']
            p['count'] += 1
            p['ticks'] += interval
            p['max'] = max(p['max'], interval)
        elif name == 'measure-trim':
            out['trims']['count'] += 1
            out['trims']['bytes'] += (values[1] | values[2] << 32) if len(values) > 2 else 0
        elif name == 'measure-make-resident-trimmed':
            out['trims']['rounds'] += values[1] if len(values) > 1 else 0
            out['trims']['pending'] += 1 if hr == 0x8000000A else 0
        elif name in ('measure-d3d12-staged-upload', 'measure-d3d12-texture-copy'):
            u = out['uploads']
            u['ticks'] += interval
            if name == 'measure-d3d12-staged-upload':
                u['staged'] += 1
            else:
                u['texture_copy'] += 1
                # {to buffer, width | height << 16, pitch}: pitch x height texel rows bounds the bytes
                # (a block-compressed copy has height / 4 rows)
                if len(values) > 2 and not values[0]:
                    u['bytes_bound'] += values[2] * (values[1] >> 16)
        elif name == 'measure-d3d12-copy-texture':
            out['uploads']['recorded'] += 1
            out['uploads']['ticks'] += interval
        elif name == 'measure-d3d12-execute' and len(values) >= 4:
            e = out['execute']
            e['count'] += 1
            e['replay_us'] += values[2]
            e['submit_us'] += values[3]
            e['max_us'] = max(e['max_us'], values[2] + values[3])
        elif name == 'measure-d3d12-replay-time' and len(values) >= 12:
            e = out['execute']
            for k in range(6):
                e['kinds_us'][k] += values[k]
                e['kinds_count'][k] += values[6 + k]
        elif name == 'measure-d3d12-wait' and len(values) >= 16:
            for j in range(4):
                key, count, us = values[4 + 3 * j:7 + 3 * j]
                if count:
                    site = key & 0xff
                    label = WAIT_SITES[site] if site < len(WAIT_SITES) and WAIT_SITES[site] else str(site)
                    out['waits'][label][0] += count
                    out['waits'][label][1] += us
    return out


def summarise(window, top):
    total = sum(window['buckets'].values()) or 1
    s, p, u = window['shader'], window['pso'], window['uploads']
    other = {b: round(window['other_buckets'].get(b, 0) / QPC_HZ, 3) for b in ORDER
             if window['other_buckets'].get(b, 0)}
    upload_s = u['ticks'] / QPC_HZ
    return {
        'seconds': round(window['seconds'], 3),
        'primary_tid': window['primary_tid'],
        'other_thread_buckets_s': other,
        'buckets': {b: {'s': round(window['buckets'].get(b, 0) / QPC_HZ, 3),
                        'pct': round(100 * window['buckets'].get(b, 0) / total, 1)} for b in ORDER},
        'shader': {'parts': s['parts'], 'internal_parts': s['internal'],
                   'compile_s': round(s['ticks'] / QPC_HZ, 3), 'longest_s': round(s['max'] / QPC_HZ, 3),
                   'translate_s': round(s['translate'] / QPC_HZ, 3)},
        'pso': {'count': p['count'], 's': round(p['ticks'] / QPC_HZ, 3), 'longest_s': round(p['max'] / QPC_HZ, 3)},
        'uploads': {'staged': u['staged'], 'texture_copy': u['texture_copy'], 'recorded': u['recorded'],
                    's': round(upload_s, 3), 'bytes_upper_bound': u['bytes_bound']},
        'bo_creates': sorted(window['bo'], key=lambda b: -b['bind_s'] - b['allocate_s'])[:top],
        'binds': sorted(window['binds'], key=lambda b: -(b['residency_s'] + b['map_s'] + b.get('bind_wait_s', 0)))[:top],
        'trims': window['trims'],
        'waits_us': {k: {'count': v[0], 's': round(v[1] / 1e6, 3)}
                     for k, v in sorted(window['waits'].items(), key=lambda kv: -kv[1][1])},
        'longest': [{'s': round(t / QPC_HZ, 3), 'receipt': n, 'tid': tid, 'values': ['%x' % v for v in vals]}
                    for t, _, n, tid, vals in sorted(window['longest'], reverse=True)[:top]],
        'threads': {str(k): round(v / QPC_HZ, 3) for k, v in window['threads'].most_common(8)},
        'execute': {'count': window['execute']['count'],
                    'replay_s': round(window['execute']['replay_us'] / 1e6, 3),
                    'submit_s': round(window['execute']['submit_us'] / 1e6, 3),
                    'longest_s': round(window['execute']['max_us'] / 1e6, 3),
                    'replay_by_kind': {k: {'count': window['execute']['kinds_count'][i],
                                           's': round(window['execute']['kinds_us'][i] / 1e6, 3)}
                                       for i, k in enumerate(REPLAY_KINDS)}},
    }


def times_gap(lines):
    """(frame-0 seconds, frame 1 -> 2 gap seconds, frames) of a GravityMark times file."""
    frames = []
    for line in lines:
        m = re.match(r'\s*(\d+):\s+([0-9.]+)\s+([0-9.]+)', line)
        if m:
            frames.append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
    if len(frames) < 3:
        return None
    return {'frame0_s': frames[0][1], 'gap_1_2_s': frames[2][2], 'frames': len(frames)}


def times_frames(lines):
    """[(index, seconds, gap seconds)] of a GravityMark times file."""
    frames = []
    for line in lines:
        m = re.match(r'\s*(\d+):\s+([0-9.]+)\s+([0-9.]+)', line)
        if m:
            frames.append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
    return frames


def align_frames(present_qpcs, frames, span=30):
    """Index of the present that is the times file's frame 0, or None.

    Frame i's gap (frames[i][2], i >= 1) is matched against present gaps
    (present k + i minus present k + i - 1); the offset k with the smallest
    total absolute difference over the first `span` frames wins."""
    n = min(span, len(frames) - 1)
    if n < 2 or len(present_qpcs) < n + 1:
        return None
    gaps = [(present_qpcs[j] - present_qpcs[j - 1]) / QPC_HZ for j in range(1, len(present_qpcs))]
    best = None
    for k in range(0, len(present_qpcs) - n):
        cost = sum(abs(gaps[k + i - 1] - frames[i][2]) for i in range(1, n + 1))
        if best is None or cost < best[0]:
            best = (cost, k)
    return best[1]


def scored_stall(frames, count=10):
    """Seconds the first `count` scored frames spend beyond the median frame
    of the run, and the frames that time would have drawn."""
    gaps = sorted(f[2] for f in frames[1:])
    if not gaps:
        return None
    median = gaps[len(gaps) // 2]
    excess = sum(max(0.0, f[2] - median) for f in frames[1:count + 1])
    window = frames[-1][1] - frames[0][1]
    return {'median_frame_s': round(median, 6), 'excess_s': round(excess, 3),
            'lost_frames': round(excess / median) if median > 0 else None,
            'window_s': round(window, 3), 'frames': len(frames)}


def report(lines, scan=400, top=12, times=None, window_frames=10):
    rows = parse(lines, scan + 1)
    pid = main_pid(rows)
    rows = [r for r in rows if r[3] == pid]
    result = {'pid': pid, 'receipts': len(rows)}
    frames = None
    if times is not None:
        times = list(times)
        result['times'] = times_gap(times)
        frames = times_frames(times)
        result['scored_stall'] = scored_stall(frames, window_frames)
    if not rows:
        return result
    first = rows[0][0]
    stall = find_stall(rows, scan)
    if stall is None:
        result['load'] = summarise(analyse(rows, first - 1, max(r[0] for r in rows)), top)
        return result
    a, b, k = stall
    result['stall_present'] = k
    result['stall_starts_s'] = round((a - first) / QPC_HZ, 3)
    result['load'] = summarise(analyse(rows, first - 1, a), top)
    result['stall'] = summarise(analyse(rows, a, b), top)
    if frames:
        qpcs = [r[0] for r in rows if r[1] == 'measure-d3d12-present']
        k = align_frames(qpcs, frames)
        if k is not None:
            result['frame0_present'] = k
            result['frame0_after_first_receipt_s'] = round((qpcs[k] - first) / QPC_HZ, 3)
            result['stall_in_scored_window'] = k < result['stall_present']
            result['pre_window'] = summarise(analyse(rows, first - 1, qpcs[k]), top)
            for name, i in (('frame_1_2', 2), ('frame_2_3', 3)):
                if k + i < len(qpcs):
                    result[name] = summarise(analyse(rows, qpcs[k + i - 1], qpcs[k + i]), top)
    return result


def table(result):
    out = ['pid %s, %d receipts' % (result.get('pid'), result.get('receipts', 0))]
    if result.get('times'):
        t = result['times']
        out.append('times: frame 0 at %.3f s, frame 1 -> 2 gap %.3f s, %d frames' %
                   (t['frame0_s'], t['gap_1_2_s'], t['frames']))
    if 'stall_present' in result:
        out.append('stall: present %d -> %d, starts %.3f s after the first receipt' %
                   (result['stall_present'] - 1, result['stall_present'], result['stall_starts_s']))
    if 'frame0_present' in result:
        out.append('frame 0 = present %d, %.3f s after the first receipt; largest present gap %s the '
                   'scored window' % (result['frame0_present'], result['frame0_after_first_receipt_s'],
                                     'inside' if result['stall_in_scored_window'] else 'BEFORE'))
    if result.get('scored_stall'):
        s = result['scored_stall']
        out.append('scored window %.3f s, %d frames, median frame %.6f s; first frames lose %.3f s '
                   '(~%s frames)' % (s['window_s'], s['frames'], s['median_frame_s'], s['excess_s'],
                                     s['lost_frames']))
    for phase in ('load', 'stall', 'pre_window', 'frame_1_2', 'frame_2_3'):
        w = result.get(phase)
        if not w:
            continue
        out.append('')
        out.append('== %s: %.3f s (wall, present thread %s)' % (phase, w['seconds'], w['primary_tid']))
        for b in ORDER:
            v = w['buckets'][b]
            if v['s']:
                out.append('  %-16s %9.3f s %5.1f%%' % (b, v['s'], v['pct']))
        if w['other_thread_buckets_s']:
            out.append('  other threads (thread-seconds): ' +
                       ', '.join('%s %.3f' % kv for kv in w['other_thread_buckets_s'].items()))
        s = w['shader']
        out.append('  shaders: %d parts (%d internal), compile %.3f s (longest %.3f s), translate %.3f s' %
                   (s['parts'], s['internal_parts'], s['compile_s'], s['longest_s'], s['translate_s']))
        p = w['pso']
        out.append('  PSOs: %d, %.3f s (longest %.3f s)' % (p['count'], p['s'], p['longest_s']))
        u = w['uploads']
        out.append('  uploads: %d staged, %d CPU texture copies (<= %.1f MB), %d recorded, %.3f s' %
                   (u['staged'], u['texture_copy'], u['bytes_upper_bound'] / 1e6, u['recorded'], u['s']))
        t = w['trims']
        out.append('  trims: %d (%.1f MiB), %d trim rounds, %d E_PENDING make-resident' %
                   (t['count'], t['bytes'] / 2**20, t['rounds'], t['pending']))
        for bo in w['bo_creates'][:6]:
            out.append('  bo-create %4d MiB class %d at +%.3f s: allocate %.3f s, bind %.3f s' %
                       (bo['mib'], bo['class'], bo['at'], bo['allocate_s'], bo['bind_s']))
        e = w['execute']
        if e['count']:
            out.append('  executes: %d, replay %.3f s, submit+wait %.3f s (longest %.3f s); replay %s' %
                       (e['count'], e['replay_s'], e['submit_s'], e['longest_s'],
                        ', '.join('%s %d/%.3f s' % (k, v['count'], v['s'])
                                  for k, v in e['replay_by_kind'].items() if v['count'])))
        for b in w['binds'][:6]:
            out.append('  bind %4d MiB at +%.3f s: residency (MakeResident, trims, its paging wait) %.3f s, '
                       'Map %.3f s, bind paging wait %.3f s' % (b['mib'], b['at'], b['residency_s'], b['map_s'],
                                                                 b.get('bind_wait_s', 0)))
        if w['waits_us']:
            out.append('  execute waits: ' + ', '.join('%s %d/%.3f s' % (k, v['count'], v['s'])
                                                       for k, v in list(w['waits_us'].items())[:6]))
        for item in w['longest'][:8]:
            out.append('  %8.3f s  %s %s' % (item['s'], item['receipt'], ' '.join(item['values'])))
    return '\n'.join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('log')
    ap.add_argument('--times')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--present-scan', type=int, default=400)
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--window-frames', type=int, default=10)
    args = ap.parse_args(argv)
    times = None
    if args.times:
        with open(args.times, errors='replace') as f:
            times = f.readlines()
    with open(args.log, errors='replace') as f:
        result = report(f, args.present_scan, args.top, times, args.window_frames)
    print(json.dumps(result, indent=1) if args.json else table(result))
    return 0


if __name__ == '__main__':
    sys.exit(main())
