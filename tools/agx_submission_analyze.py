#!/usr/bin/env python3
"""Analyze RC35 receipts. All hex words are unsigned; pairs are low/high.

Completion QPC endpoints measure queued/outstanding work, never GPU busy.
Firmware stage intervals are mapped to QPC by aligning the final firmware end
with first KMD completion observation. This is an ESTIMATE: polling/notification
latency and hypervisor clock offsets are not calibrated away. Invalid/missing
ranges stay unknown. Intervals are unioned before intersection, so overlapping
TA/fragment or jobs are not counted twice. Receipts can arrive in later frames.

Usage: agx_submission_analyze.py UMD.log --csv frames.csv [--qpc-hz 24000000]
Old logs need explicit QPC frequency; new logs carry it. JSON summary on stdout.
"""
import argparse
import collections
import csv
import json
import re
import statistics
from pathlib import Path


def u64(words, at):
    return words[at] | words[at + 1] << 32


def union(intervals):
    merged = []
    for start, end in sorted(intervals):
        if end <= start:
            continue
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
        else:
            merged.append((start, end))
    return merged


def overlap(left, right):
    left, right = union(left), union(right)
    i = j = 0
    total = 0
    while i < len(left) and j < len(right):
        total += max(0, min(left[i][1], right[j][1]) - max(left[i][0], right[j][0]))
        if left[i][1] < right[j][1]:
            i += 1
        else:
            j += 1
    return total


# RC42 trace ABI: high byte is replay phase, low byte is stable CPU call site.
WAIT_PHASES = ['other', 'query_begin', 'query_end', 'query_resolve', 'draw', 'dispatch', 'submit']
WAIT_SITES = ['unused', 'cpu_fence', 'paging_fence', 'render_fence', 'make_resident',
              'allocate', 'lock', 'escape', 'copy_escape', 'submit_callback',
              'inflight_slot', 'batch_poll', 'batch_slot', 'sync_writer',
              'sync_readers', 'flush', 'staging_blit', 'transfer_map', 'query_create',
              'query_command', 'query_increment', 'context_retire', 'owner_lock',
              'event', 'query_writers', 'bo_create', 'count_map', 'scene_escape',
              'pool_map_escape', 'native_ready_escape', 'completion_escape', 'timestamp_escape',
              'frame_arm_escape', 'allocate_canonical', 'lock_hash', 'lock_transfer',
              'evict', 'map_gpu_va', 'reserve_gpu_va', 'free_gpu_va']


def wait_name(site):
    phase, call = divmod(site, 256)
    return ((WAIT_PHASES[phase] if phase < len(WAIT_PHASES) else str(phase)) + '.' +
            (WAIT_SITES[call] if call < len(WAIT_SITES) else str(call)))


def wait_frame(pending, sites, tag='measure-d3d12-wait', prefix='wait'):
    waits = [w for t, w in pending if t == tag]
    queries = [w for t, w in pending if t == 'measure-d3d12-query-time']
    executions = sum(t == 'measure-d3d12-execute' for t, _ in pending)
    complete = (bool(waits) and len(waits) == executions and
                not any(w[3] & 3 for w in waits) and
                not any(t == 'wait-orphan' for t, _ in pending))
    frame = dict(wait_profiled_executes=len(waits) if waits else None,
                 wait_profile_complete=complete if waits or any(t == 'measure-d3d12-execute' and len(w) >= 5 and w[4] for t,w in pending) else None,
                 wait_omitted_ms=sum(w[2] for w in waits)/1000 if waits else None,
                 wait_flags=0 if waits else None)
    for w in waits: frame['wait_flags'] |= w[3]
    for site in sorted(sites):
        us = count = 0
        exact = complete
        for w in waits:
            entries = {w[i]: (w[i+1], w[i+2]) for i in range(4, 16, 3) if w[i]}
            if site in entries:
                count += entries[site][0]; us += entries[site][1]
            elif w[2] or w[3] & 4:
                exact = False  # censored by this Execute's top-N limit
        frame[f'wait_{site}_ms'] = us/1000 if exact else None
        frame[f'wait_{site}_count'] = count if exact else None
        frame[f'wait_{site}_lower_ms'] = us/1000 if waits and not any(w[3] & 1 for w in waits) else None
    query_complete = complete and {w[1] for w in queries} == {w[1] for w in waits}
    for i, phase in enumerate(['begin', 'end', 'resolve']):
        frame[f'query_{phase}_count'] = sum(w[2+i*2] for w in queries) if query_complete else None
        frame[f'query_{phase}_ms'] = sum(w[3+i*2] for w in queries)/1000 if query_complete else None
    if prefix != 'wait':
        return {prefix + k[4:]: v for k,v in frame.items() if k.startswith('wait_')}
    return frame


def analyze(lines, qpc_hz=None):
    rows = collections.defaultdict(list)
    completions, stages = {}, {}
    cpu = collections.defaultdict(list)
    rates = set()
    loss_ranges = []
    resets = errors = missing_timing = 0
    wait_sites, path_sites, wait_seen = set(), set(), {}
    execute_keys = set()
    for number, line in enumerate(lines, 1):
        match = re.search(r'^(\S+).*?pid=(\d+).*?qpc=(\d+)(.*)$', line)
        if not match:
            continue
        tag, pid, qpc, tail = match.groups()
        pid, qpc = int(pid), int(qpc)
        words = [int(w, 16) for w in tail.split()]
        if tag == 'measure-d3d12-execute' and len(words) >= 5 and words[4]:
            identity = (pid, words[4])
            if identity in execute_keys:
                raise ValueError('Duplicate Execute serial; split process lifetimes or corrupt captures')
            execute_keys.add(identity)
        if tag in ('measure-d3d12-wait', 'measure-d3d12-query-time', 'measure-d3d12-wait-path'):
            expected = 8 if tag == 'measure-d3d12-query-time' else 16
            if len(words) != expected or words[0] != 1:
                raise ValueError('Unsupported or truncated RC42 wait receipt')
            key = (pid, tag, words[1])
            if key in wait_seen:
                if wait_seen[key] != words:
                    raise ValueError('Conflicting Execute wait serial; split process lifetimes')
                continue
            wait_seen[key] = words
            if tag != 'measure-d3d12-query-time':
                present = [words[i] for i in range(4, 16, 3) if words[i]]
                if len(present) != len(set(present)):
                    raise ValueError('Duplicate wait site within an Execute')
                (wait_sites if tag == 'measure-d3d12-wait' else path_sites).update(present)
        rows[pid].append((qpc, number, tag, words))
        if tag == 'measure-gpu-completion' and len(words) == 13:
            key = (pid, u64(words, 0))
            if key in completions and completions[key] != words:
                raise ValueError('Conflicting completion sequence; split adapter lifetimes/captures')
            completions[key] = words
            if u64(words, 11): rates.add(u64(words, 11))
        elif tag == 'measure-gpu-stages' and len(words) == 10:
            key = (pid, u64(words, 0))
            if key in stages and stages[key] != words:
                raise ValueError('Conflicting stage sequence; split adapter lifetimes/captures')
            stages[key] = words
        elif tag == 'measure-gpu-coverage' and len(words) >= 7:
            if u64(words, 4):
                if len(words) < 11:
                    raise ValueError('Loss report lacks starting cursor')
                loss_ranges.append((u64(words, 9), u64(words, 9) + u64(words, 4)))
            if words[6]:
                raise ValueError('Adapter cursor reset; split capture at reset before analyzing intervals')
            resets += words[6]
            if len(words) >= 9: missing_timing = max(missing_timing, u64(words, 7))
        elif tag == 'measure-gpu-collection-error':
            errors += 1
        elif tag == 'measure-d3d12-replay-window' and len(words) == 8:
            cpu[pid].append((u64(words, 0), u64(words, 2)))
            if u64(words, 6): rates.add(u64(words, 6))
    if qpc_hz:
        rates.add(qpc_hz)
    if len(rates) != 1:
        raise ValueError('Need one QPC frequency; use --qpc-hz for old logs; split different boots/clocks')
    hz = rates.pop()
    if hz <= 0:
        raise ValueError('QPC frequency must be positive')
    outstanding, estimated = collections.defaultdict(list), collections.defaultdict(list)
    valid_stages = invalid_stages = failed = invalid_completions = 0
    per_pid_jobs = collections.Counter()
    for (pid, seq), words in completions.items():
        start, end = u64(words, 7), u64(words, 9)
        per_pid_jobs[pid] += 1
        if not start or end < start:
            invalid_completions += 1
            continue
        outstanding[pid].append((start, end))
        if words[6]:
            failed += 1
            continue
        expected = 2 if words[5] == 1 else 1
        raw = stages.get((pid, seq))
        if not raw:
            invalid_stages += expected
            continue
        firmware_end = u64(raw, 4)
        pairs = [(u64(raw, 2), firmware_end)]
        if words[5] == 1:
            pairs.append((u64(raw, 6), u64(raw, 8)))
        for begin, finish in pairs:
            # Starts can be stale on G13. Reject an inverted/zero interval,
            # a stage after final completion, or one extending before submit
            # under the documented completion-end alignment estimate.
            mapped_begin = end - (firmware_end - begin)
            mapped_end = end - (firmware_end - finish)
            if (not begin or not finish or finish < begin or not firmware_end or
                    finish > firmware_end or mapped_begin < start or mapped_end > end):
                invalid_stages += 1
                continue
            valid_stages += 1
            estimated[pid].append((mapped_begin, mapped_end))
    orphan_keys = {(pid, serial) for pid, _, serial in wait_seen} - execute_keys
    orphan_pids = {pid for pid, _ in orphan_keys}
    frames = []
    names = ['draw', 'dispatch', 'copy', 'query', 'indirect_expand', 'other']
    for pid, process_rows in sorted(rows.items()):
        pending = []
        previous = process_rows[0][0]
        for qpc, number, tag, words in process_rows:
            if tag in ('measure-d3d12-wait', 'measure-d3d12-query-time', 'measure-d3d12-wait-path'):
                continue  # joined below to the Execute which owns this serial
            pending.append((tag, words))
            if tag == 'measure-d3d12-execute' and len(words) >= 5 and words[4]:
                for companion in ('measure-d3d12-wait', 'measure-d3d12-query-time', 'measure-d3d12-wait-path'):
                    payload = wait_seen.get((pid, companion, words[4]))
                    if payload is not None: pending.append((companion, payload))
            if tag != 'measure-d3d12-present':
                continue
            def sums(name, length):
                out = [0] * length
                for t, w in pending:
                    if t == name:
                        for i, value in enumerate(w[:length]): out[i] += value
                return out
            window = [(previous, qpc)]
            execute = sums('measure-d3d12-execute', 4)
            draw = sums('measure-d3d12-owner-time', 9)
            dispatch = sums('measure-d3d12-dispatch-time', 5)
            replay = sums('measure-d3d12-replay-time', 12)
            has_replay = any(t == 'measure-d3d12-replay-time' for t, _ in pending)
            has_dispatch = any(t == 'measure-d3d12-dispatch-time' for t, _ in pending)
            frame = dict(pid=pid, frame=sum(f['pid'] == pid for f in frames)+1,
                         end_line=number, start_qpc=previous, end_qpc=qpc,
                         wall_ms=(qpc-previous)*1000/hz,
                         replay_ms=execute[2]/1000, submit_ms=execute[3]/1000,
                         draw_vbo_ms=draw[2]/1000,
                         replay_remainder_ms=(execute[2]-sum(draw[1:4]))/1000,
                         outstanding_ms=overlap(outstanding[pid], window)*1000/hz if per_pid_jobs[pid] else None,
                         cpu_outstanding_overlap_ms=overlap(union(cpu[pid]),
                             [(max(a,previous),min(b,qpc)) for a,b in outstanding[pid]])*1000/hz if per_pid_jobs[pid] and cpu[pid] else None,
                         gpu_busy_estimate_ms=overlap(estimated[pid], window)*1000/hz if estimated[pid] else None,
                         gpu_cpu_overlap_estimate_ms=overlap(union(cpu[pid]),
                             [(max(a,previous),min(b,qpc)) for a,b in estimated[pid]])*1000/hz if estimated[pid] and cpu[pid] else None)
            for i, name in enumerate(names):
                frame[f'replay_{name}_ms'] = replay[i]/1000 if has_replay else None
                frame[f'replay_{name}_count'] = replay[6+i] if has_replay else None
            for i, name in enumerate(['bind', 'heap', 'launch', 'unbind'], 1):
                frame[f'dispatch_{name}_ms'] = dispatch[i]/1000 if has_dispatch else None
            frame['dispatch_owner_calls'] = dispatch[0] if has_dispatch else None
            # Includes translation plus timing overhead; expansion is separate
            # because its nested owner dispatches are not in the direct bucket.
            frame['dispatch_translation_envelope_ms'] = (replay[1]-sum(dispatch[1:]))/1000 if has_replay and has_dispatch and not replay[10] else None
            frame['replay_unbucketed_ms'] = (execute[2]-sum(replay[:6]))/1000 if has_replay else None
            if pid in orphan_pids: pending.append(('wait-orphan', []))
            frame.update(wait_frame(pending, wait_sites))
            frame.update(wait_frame(pending, path_sites, 'measure-d3d12-wait-path', 'wait_path'))
            frames.append(frame)
            previous, pending = qpc, []
    summary = dict(qpc_hz=hz, frames=len(frames), completion_records=len(completions),
                   valid_stages=valid_stages, invalid_or_missing_stages=invalid_stages,
                   failed_jobs=failed, invalid_completion_intervals=invalid_completions,
                   missing_timing_capture_attempts=missing_timing,
                   overwritten_adapter_records=sum(b-a for a,b in union(loss_ranges)), cursor_resets=resets, collection_errors=errors,
                   timing_complete=False,
                   clock_mapping='ESTIMATE: final firmware end aligned to first KMD completion QPC; polling delay and hypervisor drift remain',
                   coverage='Observed records only; tail in-flight jobs require a later drain. Loss counts are adapter-wide. No total GPU-busy claim.',
                   median_frame_ms=statistics.median(f['wall_ms'] for f in frames[1:]) if len(frames)>1 else None)
    steady = [f for f in frames if f['frame'] > 1]
    def median(values):
        values = [v for v in values if v is not None]
        return statistics.median(values) if values else None
    summary['wait_orphan_executes'] = len(orphan_keys)
    summary['wait_orphan_ids'] = [dict(pid=pid, serial=serial) for pid, serial in sorted(orphan_keys)]
    summary['wait_site_medians'] = {
        str(site): dict(name=wait_name(site),
            median_ms=median(f[f'wait_{site}_ms'] for f in steady),
            median_lower_ms=median(f[f'wait_{site}_lower_ms'] for f in steady),
            median_count=median(f[f'wait_{site}_count'] for f in steady),
            exact_frames=sum(f[f'wait_{site}_ms'] is not None for f in steady),
            observed_frames=sum(f[f'wait_{site}_lower_ms'] is not None for f in steady))
        for site in sorted(wait_sites)}
    summary['wait_path_medians'] = {
        str(site): dict(name=wait_name(site),
            median_ms=median(f[f'wait_path_{site}_ms'] for f in steady),
            median_lower_ms=median(f[f'wait_path_{site}_lower_ms'] for f in steady),
            median_count=median(f[f'wait_path_{site}_count'] for f in steady),
            exact_frames=sum(f[f'wait_path_{site}_ms'] is not None for f in steady),
            observed_frames=sum(f[f'wait_path_{site}_lower_ms'] is not None for f in steady))
        for site in sorted(path_sites)}
    summary['query_median_ms'] = {phase: median(f[f'query_{phase}_ms'] for f in steady)
                                  for phase in ['begin', 'end', 'resolve']}
    summary['wait_coverage'] = ('Exclusive CPU wall time in instrumented scopes; includes callback CPU work. '
        'Per-site lower bounds when top-N omits entries; null means missing or censored. Orphan companions make their process coverage incomplete. '
        'Medians exclude the first partial frame of EACH process. Query and wait-path timings are inclusive; do not sum them with waits or with each other. Companions join by PID/Execute serial, not arrival frame.')
    summary['wait_complete_frames'] = sum(f['wait_profile_complete'] is True for f in steady)
    return frames, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--qpc-hz', type=int)
    parser.add_argument('--csv', type=Path)
    args = parser.parse_args()
    frames, summary = analyze(args.log.read_text(errors='replace').splitlines(), args.qpc_hz)
    if args.csv and frames:
        with args.csv.open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=frames[0])
            writer.writeheader(); writer.writerows(frames)
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
