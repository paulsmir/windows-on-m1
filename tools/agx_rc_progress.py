#!/usr/bin/env python3
"""Decode Wom1RcProgress REG_BINARY. Evidence only, never a hardware PASS.

Export the device value as raw binary, then: agx_rc_progress.py receipt.bin --pid N --qpc-begin B --qpc-end E
N is the probe process ID. V1 is R/C only; v2 types both jobs and the barrier.
RFence/CFence are legacy wire names for predecessor/successor. Join them to the KMD
JobTiming971 records; application fence values are a different namespace.
"""
import argparse
import json
from pathlib import Path
import struct

FIELDS = ('Stage RFence CFence ExpectedR ExpectedC EventCount PreviousThreshold '
          'RenderCount ComputeCount RFlight CFlight RSlot CSet CFlags WaitLow '
          'WaitHigh WaitValue QpcLow QpcHigh ProcessId TerminalFence Status Valid').split()
WORD_NAMES = ('threshold_before compute_fw_before render_fw render_driver ta_fw '
              'ta_wptr ta_done render_wptr render_done compute_wptr compute_done '
              'compute_fw_after threshold_after').split()

TYPED_WORD_NAMES = ('threshold_before successor_first_fw_before predecessor_last_fw '
                    'predecessor_driver ta_fw ta_wptr ta_done render_wptr render_done '
                    'compute_wptr compute_done successor_first_fw_after threshold_after').split()


def typed_pair(s):
    running, successor = s['RunningKind'], s['NextKind']
    if running not in (1, 2, 3) or successor not in (1, 2, 3):
        return False
    expected = (2 if running in (1, 2) else 1) if successor == 1 else (3 if running in (1, 2) else 4)
    return s['Barrier'] == expected


def pending_domain(s, version=2):
    w = s['Words']
    if (not typed_pair(s) or s['Valid'] != 8191 or not s['RFence'] or not s['CFence'] or
        not s['ExpectedR'] or not s['ExpectedC'] or not s['RFlight'] or not s['CFlight'] or
        (version == 2 and (s['RFlight'] not in (1, 2) or s['CFlight'] not in (1, 2) or
                           s['RFlight'] == s['CFlight'])) or
        s['WaitValue'] != s['ExpectedR'] or not (s['WaitLow'] or s['WaitHigh']) or
        w[0] != w[12] or reached(w[1], s['ExpectedC']) or reached(w[11], s['ExpectedC']) or
        s['EventCount'] <= s['PreviousThreshold']):
        return None
    if s['CFlags'] == 0 and w[0] == s['PreviousThreshold']:
        return 'staged-progress'
    if s['CFlags'] == 3 and w[0] == s['EventCount']:
        return 'published-progress'
    return None


def reached(observed, expected):
    return expected != 0 and (observed - expected) & 0xffffffff < 0x80000000


def evidence(s, version=2):
    return pending_domain(s, version) if reached(s['Words'][2], s['ExpectedR']) else None


def decode(data, pid, qpc_begin=0, qpc_end=(1 << 64)-1):
    if len(data) < 16:
        raise ValueError('truncated progress header')
    version, size, count, overflow = struct.unpack_from('<4I', data)
    stride = {1: 144, 2: 156}.get(version)
    if stride is None or size != len(data) or size != 16 + 128*stride or count > 128:
        raise ValueError('invalid version/size/count')
    pairs = {}
    terminals = []
    for i in range(count):
        words = struct.unpack_from('<%dI' % (stride//4), data, 16 + stride*i)
        s = dict(zip(FIELDS, words[:23])); s['Words'] = list(words[23:36])
        s.update(zip(('RunningKind', 'NextKind', 'Barrier'), words[36:39] if version == 2 else (1, 3, 3)))
        qpc = (s['QpcHigh'] << 32) | s['QpcLow']
        if s['ProcessId'] != pid or not qpc_begin <= qpc <= qpc_end:
            continue
        key = (s['RFence'], s['CFence'], s['ExpectedR'],
               s['RunningKind'], s['NextKind'], s['Barrier'])
        pair = pairs.setdefault(key, dict(render_fence=key[0], compute_fence=key[1],
            predecessor_fence=key[0], successor_fence=key[1],
            running_kind=key[3], next_kind=key[4], barrier=key[5],
            evidence='inconclusive', samples=[], negative=set(), reported=[]))
        # The negative must belong to the SAME pending interval. R finishing
        # before commit cannot prove progress under an elevated threshold.
        domain = pending_domain(s, version)
        w = s['Words']
        interval = (domain, s['ExpectedC'], s['EventCount'], s['PreviousThreshold'],
                    s['RFlight'], s['CFlight'], s['WaitLow'], s['WaitHigh'], s['WaitValue'])
        if domain and interval in pair['negative'] and not overflow:
            observed = evidence(s, version)
            if observed == 'published-progress' or (observed and pair['evidence'] == 'inconclusive'):
                pair['evidence'] = observed
        if domain and not reached(w[2], s['ExpectedR']):
            pair['negative'].add(interval)
        if s['Stage'] == 6:
            terminals.append((i, dict(fence=s['TerminalFence'], status=s['Status'])))
        s['sample_index'] = i
        s['qpc'] = (s['QpcHigh'] << 32) | s['QpcLow']
        s['values'] = dict(zip(TYPED_WORD_NAMES if version == 2 else WORD_NAMES, s['Words']))
        pair['samples'].append(s)
    for pair in pairs.values():
        pair.pop('negative')
        # RcCurrent changes when a successor becomes the next pair's head.
        # Join original fences across all filtered records, retaining every
        # report in receipt order. Never borrow a terminal preceding this pair.
        first_index = pair['samples'][0]['sample_index']
        fences = (pair['predecessor_fence'], pair['successor_fence'])
        pair['reported'] = [report for index, report in terminals
                            if index >= first_index and report['fence'] in fences]
        pair['exact_report_order'] = [v['fence'] for v in pair['reported']] == [pair['render_fence'], pair['compute_fence']]
    return dict(version=version, count=count, overflow=overflow, process_id=pid,
                sample_state="no sample" if not count else "sampled",
                qpc_begin=qpc_begin, qpc_end=qpc_end,
                verdict='evidence-only; require exact probe outputs, successful retire order and no reset',
                pairs=list(pairs.values()))


# 1..65 keep the stable render_job_window / Q3 refusal numbering.
DECISION_NAMES = {0: 'allowed', 66: 'no pending successor',
    67: 'running image not released', 68: 'prebind not ready (see detail)',
    69: 'backend kick guard', 70: 'kick disabled', 71: 'unknown reason'}


def decode_decisions(data, pid, qpc_begin=0, qpc_end=(1 << 64)-1):
    if len(data) < 20:
        raise ValueError('truncated decisions header')
    version, length, count, next_index, overwritten = struct.unpack_from('<5I', data)
    stride = {1: 40, 2: 52, 3: 52}.get(version)
    expected_size = 8640 if version == 3 else 308 + 128*(stride or 0)
    if stride is None or length != len(data) or length != expected_size:
        raise ValueError('invalid decisions header')
    rings = [(308, 128, count, next_index, overwritten, False)]
    idle_overwritten = 0
    if version == 3:
        idle_count, idle_next, idle_overwritten = struct.unpack_from('<3I', data, 6964)
        rings.append((6976, 32, idle_count, idle_next, idle_overwritten, True))
    entries = []
    for offset, capacity, used, position, lost, idle in rings:
        if (used > capacity or position >= capacity or
            (used < capacity and (position != used or lost))):
            raise ValueError('invalid decisions ring')
        for i in range(used):
            index = (position+i) % capacity if used == capacity else i
            words = struct.unpack_from('<%dI' % (stride//4), data, offset+index*stride)
            if version == 3 and (words[0] == 66) != idle:
                raise ValueError('decision in wrong ring')
            entries.append(words)
    # Across rings there is no append order. Use observation QPC explicitly;
    # coalesced records remain intervals, not a total order of every poll.
    if version == 3:
        entries.sort(key=lambda w: (w[9], w[8], w[7], w[6]))
    hist = struct.unpack_from('<72I', data, 20)
    records = []
    latest = {}
    for words in entries:
        reason, detail, rf, cf, process, observations, lo, hi, endlo, endhi = words[:10]
        running, successor, barrier = words[10:13] if version >= 2 else (0, 0, 0)
        first, last = lo | hi << 32, endlo | endhi << 32
        if reason >= 72 or not observations or first > last:
            raise ValueError('invalid decision record')
        r = dict(reason=reason, meaning=DECISION_NAMES.get(reason, 'admission refusal'),
                 detail=detail, render_fence=rf, compute_fence=cf, process_id=process,
                 predecessor_fence=rf, successor_fence=cf,
                 running_kind=running, next_kind=successor, barrier=barrier,
                 count=observations, first_qpc=first, last_qpc=last)
        latest[str(reason)] = r
        if process == pid and last >= qpc_begin and first <= qpc_end:
            r = dict(r, partial_interval=first < qpc_begin or last > qpc_end)
            records.append(r)
    return dict(version=version, sample_state='no decision' if not entries else 'sampled',
                idle_overwritten=idle_overwritten, overwritten=overwritten, process_id=pid, qpc_begin=qpc_begin, qpc_end=qpc_end,
                histogram={str(i): dict(count=n, last_retained=latest.get(str(i)))
                           for i,n in enumerate(hist) if n},
                decisions=records,
                note='Histogram is device-wide. V3 overwritten counts non-idle records; idle_overwritten counts reason 66. Filtered records are bounded; partial counts include observations outside the interval. Missing/overwritten evidence is inconclusive.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--decisions', type=Path, help='optional raw Wom1RcDecisions value')
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--qpc-begin', type=int, required=True)
    parser.add_argument('--qpc-end', type=int, required=True)
    args = parser.parse_args()
    if not 0 <= args.qpc_begin < args.qpc_end < 1 << 64:
        parser.error('invalid QPC interval')
    try:
        result = decode(args.receipt.read_bytes(), args.pid, args.qpc_begin, args.qpc_end)
        if args.decisions:
            result['kick_decisions'] = decode_decisions(args.decisions.read_bytes(), args.pid,
                                                       args.qpc_begin, args.qpc_end)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
