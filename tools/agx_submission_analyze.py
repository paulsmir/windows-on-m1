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


def analyze(lines, qpc_hz=None):
    rows = collections.defaultdict(list)
    completions, stages = {}, {}
    cpu = collections.defaultdict(list)
    rates = set()
    loss_ranges = []
    resets = errors = missing_timing = 0
    for number, line in enumerate(lines, 1):
        match = re.search(r'^(\S+).*?pid=(\d+).*?qpc=(\d+)(.*)$', line)
        if not match:
            continue
        tag, pid, qpc, tail = match.groups()
        pid, qpc = int(pid), int(qpc)
        words = [int(w, 16) for w in tail.split()]
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
    frames = []
    names = ['draw', 'dispatch', 'copy', 'query', 'indirect_expand', 'other']
    for pid, process_rows in sorted(rows.items()):
        pending = []
        previous = process_rows[0][0]
        for qpc, number, tag, words in process_rows:
            pending.append((tag, words))
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
