#!/usr/bin/env python3
"""Decode Wom1RcProgress REG_BINARY. Evidence only, never a hardware PASS.

Export the device value as raw binary, then: agx_rc_progress.py receipt.bin --pid N --qpc-begin B --qpc-end E
N is the Q3RC process ID printed by the probe. Join RFence/CFence to the KMD
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


def reached(observed, expected):
    return expected != 0 and (observed - expected) & 0xffffffff < 0x80000000


def evidence(s):
    w = s['Words']
    if (s['Valid'] != 8191 or not s['ExpectedC'] or not s['RFlight'] or not s['CFlight'] or
        s['WaitValue'] != s['ExpectedR'] or not (s['WaitLow'] or s['WaitHigh']) or
        w[0] != w[12] or not reached(w[2], s['ExpectedR']) or
        reached(w[1], s['ExpectedC']) or reached(w[11], s['ExpectedC']) or
        s['EventCount'] <= s['PreviousThreshold']):
        return None
    if s['CFlags'] == 0 and w[0] == s['PreviousThreshold']:
        return 'staged-progress'
    if s['CFlags'] == 3 and w[0] == s['EventCount']:
        return 'published-progress'
    return None


def decode(data, pid, qpc_begin=0, qpc_end=(1 << 64)-1):
    if len(data) != 18448:
        raise ValueError('expected complete 18448-byte v1 receipt')
    version, size, count, overflow = struct.unpack_from('<4I', data)
    if version != 1 or size != len(data) or count > 128:
        raise ValueError('invalid version/size/count')
    pairs = {}
    for i in range(count):
        words = struct.unpack_from('<36I', data, 16 + 144*i)
        s = dict(zip(FIELDS, words[:23])); s['Words'] = list(words[23:])
        qpc = (s['QpcHigh'] << 32) | s['QpcLow']
        if s['ProcessId'] != pid or not qpc_begin <= qpc <= qpc_end:
            continue
        key = (s['RFence'], s['CFence'], s['ExpectedR'])
        pair = pairs.setdefault(key, dict(render_fence=key[0], compute_fence=key[1],
            evidence='inconclusive', samples=[], negative=set(), reported=[]))
        # The negative must belong to the SAME pending interval. R finishing
        # before commit cannot prove progress under an elevated threshold.
        domain = None
        w = s['Words']
        if (s['Valid'] == 8191 and s['ExpectedC'] and s['RFlight'] and s['CFlight'] and
            s['WaitValue'] == s['ExpectedR'] and (s['WaitLow'] or s['WaitHigh']) and
            w[0] == w[12] and s['EventCount'] > s['PreviousThreshold'] and
            not reached(w[1], s['ExpectedC']) and not reached(w[11], s['ExpectedC'])):
            if s['CFlags'] == 3 and w[0] == s['EventCount']:
                domain = 'published-progress'
            elif s['CFlags'] == 0 and w[0] == s['PreviousThreshold']:
                domain = 'staged-progress'
        interval = (domain, s['ExpectedC'], s['EventCount'], s['PreviousThreshold'])
        if domain and interval in pair['negative'] and not overflow:
            observed = evidence(s)
            if observed == 'published-progress' or (observed and pair['evidence'] == 'inconclusive'):
                pair['evidence'] = observed
        if domain and not reached(w[2], s['ExpectedR']):
            pair['negative'].add(interval)
        if s['Stage'] == 6:
            pair['reported'].append(dict(fence=s['TerminalFence'], status=s['Status']))
        s['sample_index'] = i
        s['qpc'] = (s['QpcHigh'] << 32) | s['QpcLow']
        s['values'] = dict(zip(WORD_NAMES, s['Words']))
        pair['samples'].append(s)
    for pair in pairs.values():
        pair.pop('negative')
        pair['exact_report_order'] = [v['fence'] for v in pair['reported']] == [pair['render_fence'], pair['compute_fence']]
    return dict(version=version, count=count, overflow=overflow, process_id=pid,
                sample_state="no sample" if not count else "sampled",
                qpc_begin=qpc_begin, qpc_end=qpc_end,
                verdict='evidence-only; require Q3RC outputs, exact successful retire order and no reset',
                pairs=list(pairs.values()))


# 1..65 keep the stable render_job_window / Q3 refusal numbering.
DECISION_NAMES = {0: 'allowed', 66: 'no pending successor',
    67: 'running image not released', 68: 'prebind not ready (see detail)',
    69: 'backend kick guard', 70: 'kick disabled', 71: 'unknown reason'}


def decode_decisions(data, pid, qpc_begin=0, qpc_end=(1 << 64)-1):
    size = 20 + 72*4 + 128*40
    if len(data) != size:
        raise ValueError('expected complete 5428-byte v1 decisions receipt')
    version, length, count, next_index, overwritten = struct.unpack_from('<5I', data)
    if (version != 1 or length != size or count > 128 or next_index >= 128 or
        (count < 128 and (next_index != count or overwritten))):
        raise ValueError('invalid decisions header')
    hist = struct.unpack_from('<72I', data, 20)
    records = []
    latest = {}
    for i in range(count):
        index = (next_index+i) % 128 if count == 128 else i
        reason, detail, rf, cf, process, observations, lo, hi, endlo, endhi = struct.unpack_from('<10I', data, 308+index*40)
        first, last = lo | hi << 32, endlo | endhi << 32
        if reason >= 72 or not observations or first > last:
            raise ValueError('invalid decision record')
        r = dict(reason=reason, meaning=DECISION_NAMES.get(reason, 'admission refusal'),
                 detail=detail, render_fence=rf, compute_fence=cf, process_id=process,
                 count=observations, first_qpc=first, last_qpc=last)
        latest[str(reason)] = r
        if process == pid and last >= qpc_begin and first <= qpc_end:
            r = dict(r, partial_interval=first < qpc_begin or last > qpc_end)
            records.append(r)
    return dict(version=version, sample_state='no decision' if not count else 'sampled',
                overwritten=overwritten, process_id=pid, qpc_begin=qpc_begin, qpc_end=qpc_end,
                histogram={str(i): dict(count=n, last_retained=latest.get(str(i)))
                           for i,n in enumerate(hist) if n},
                decisions=records,
                note='Histogram is device-wide. Filtered records are bounded; partial counts include observations outside the interval. Missing/overwritten evidence is inconclusive.')


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
