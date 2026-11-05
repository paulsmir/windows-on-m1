#!/usr/bin/env python3
"""Decode Wom1PreemptTrace (render_preempt_trace.h, version 2).

P1 receipt-only (Wom1JobDiagnostics): the KMD's last 32 node-0
DxgkDdiPreemptCommand boundaries with QPC stamps. Per event this prints the
spans that separate investigation/analysis/P1-present-serialization.md's
hypotheses:

  cmd        PreemptCommand entry -> return
  cmd>ntfy   PreemptCommand entry -> DMA_PREEMPTED DxgkCbNotifyInterrupt
  ntfy>cmt   notify -> commit (preemption phase back to idle)
  cmt>sub    commit -> first non-resubmission SubmitCommandVirtual
             (negative: the submit came before the commit)
  sub>pick   that submit -> the job worker's pickup of its fence
  await      that submit's AwaitWork wait (first wait -> exit; '-' none)

Version 2 splits sub>pick (EXP2173 lost-pickup discriminator):

  sub>disp   submit -> first AdmissionDispatchQueuedWork decision for its
             fence (the call whose queue head it is, or the submit thread's)
  disp>set   that decision -> KeSetEvent(JobThreadWake) for the fence
  set>wake   KeSetEvent -> the job thread's return from its wait
  wake>pick  wait return -> the worker's pickup stamp
  dispatch   the decision (render, cpu, empty, reserved=<fence>, paging,
             active, phase, head=<fence>), +sub when RuntimeSubmit returned
             TRUE, xN when N matching dispatch calls ran before the pickup
  cpu        processor of the KeSetEvent > processor of the wait return

and a hint: H1 (deferred notify/commit), H2 (AwaitWork) or H3 (pickup lag
after a timely commit and submit), whichever spans most of the bubble from
the notify to the pickup.

Usage: agx_preempt_trace.py kmd-receipts.txt   (or a raw REG_BINARY dump)
"""
import argparse
import struct
import sys

VERSION = 2
CAPACITY = 32
HEADER = struct.Struct('<IIIIQ')
QPC_FIELDS = (
    'EnterQpc', 'ExitQpc', 'FirstDeferQpc', 'LastDeferQpc', 'WorkerRetryQpc',
    'DpcRetryQpc', 'ClaimQpc', 'NotifyQpc', 'CommitQpc', 'SubmitQpc',
    'AwaitEnterQpc', 'AwaitWaitQpc', 'AwaitExitQpc', 'PickupQpc',
    'FirstPickupQpc', 'ResubmitQpc', 'DispatchQpc', 'WakeSetQpc', 'WakeQpc')
U32_FIELDS = (
    'PreemptionFence', 'CutoffFence', 'ActiveFence', 'DroppedFence',
    'PendingDropped', 'PendingHeadFence', 'CpuDropped', 'DecidePhase',
    'DecideDispatchedFence', 'DecideFlags', 'DeferMask', 'DeferCount',
    'RetryCount', 'ClaimSource', 'NotifyLastCompletedFence', 'SubmitFence',
    'SubmitPid', 'SubmitFlags', 'SubmitCount', 'AwaitFirstReason',
    'AwaitResult', 'FirstPickupFence', 'ResubmitFence', 'SubmitOrdinal',
    'DispatchOutcome', 'DispatchHeadFence', 'DispatchReservedFence',
    'DispatchCount', 'WakeSetCpu', 'WakeCpu')
FIELDS = QPC_FIELDS + U32_FIELDS
EVENT = struct.Struct('<' + 'Q' * len(QPC_FIELDS) + 'I' * len(U32_FIELDS))
BYTES = HEADER.size + CAPACITY * EVENT.size
# Version 1 (EXP2172/EXP2173 receipts): the same fields without the v2 tail.
V1_QPC, V1_U32 = QPC_FIELDS[:16], U32_FIELDS[:24]
V1_FIELDS = V1_QPC + V1_U32
V1_EVENT = struct.Struct('<' + 'Q' * len(V1_QPC) + 'I' * len(V1_U32))
V1_BYTES = HEADER.size + CAPACITY * V1_EVENT.size

DEFER = ((0x1, 'worker'), (0x2, 'paging'), (0x4, 'idle'), (0x8, 'claimed'),
         (0x10, 'boundary'), (0x20, 'faulted'), (0x40, 'no-claim'),
         (0x100, 'cmd-phase'), (0x200, 'cmd-dispatched'), (0x400, 'cmd-paging'))
CLAIM = {1: 'cmd', 2: 'passive', 3: 'dispatch'}
PHASE = {0: 'idle', 1: 'wait-boundary', 2: 'ready', 3: 'claimed'}
AWAIT = {4: 'backend-submitted', 8: 'runtime', 16: 'slot-owned'}
DEFAULT_HZ = 24_000_000
DISPATCH = {1: 'render', 2: 'cpu', 3: 'empty', 4: 'reserved', 5: 'paging',
            6: 'active', 7: 'phase', 8: 'head'}
DISPATCH_SUBMITTED, DISPATCH_RETRY, DISPATCH_NODE1, DISPATCH_SUBMITTER = (
    0x100, 0x200, 0x400, 0x800)


def registry_value(data, name='Wom1PreemptTrace'):
    """Raw bytes, or the hex of `name` in a kmd-receipts text dump."""
    try:
        text = data.decode('ascii')
    except UnicodeDecodeError:
        return data
    for line in text.splitlines():
        if line.startswith(name + ' '):
            return bytes.fromhex(line.split()[1])
    raise ValueError(name + ' not found')


def parse(raw):
    if len(raw) < HEADER.size:
        raise ValueError('short Wom1PreemptTrace')
    version, size, count, nxt, hz = HEADER.unpack_from(raw)
    layout, names_, want = {VERSION: (EVENT, FIELDS, BYTES),
                            1: (V1_EVENT, V1_FIELDS, V1_BYTES)}.get(
                                version, (None, None, None))
    if layout is None or size != want or len(raw) < want:
        raise ValueError(f'unsupported Wom1PreemptTrace v{version} {size} bytes '
                         f'(want v{VERSION} {BYTES} or v1 {V1_BYTES})')
    events = []
    for i in range(CAPACITY):
        e = dict.fromkeys(FIELDS, 0)
        e.update(zip(names_, layout.unpack_from(raw, HEADER.size + i * layout.size)))
        events.append(e)
    if count <= CAPACITY:
        order = list(range(count))
    else:
        order = [(nxt + i) % CAPACITY for i in range(CAPACITY)]
    return {'version': version, 'count': count, 'next': nxt, 'hz': hz or DEFAULT_HZ,
            'events': [events[i] for i in order]}


def span(e, a, b, hz):
    """Microseconds from stamp a to stamp b; None when either is missing."""
    if not e[a] or not e[b]:
        return None
    return (e[b] - e[a]) * 1e6 / hz


def spans(e, hz):
    await_us = None
    if e['AwaitExitQpc'] and e['AwaitWaitQpc']:
        await_us = span(e, 'AwaitWaitQpc', 'AwaitExitQpc', hz)
    return {
        'cmd': span(e, 'EnterQpc', 'ExitQpc', hz),
        'cmd>ntfy': span(e, 'EnterQpc', 'NotifyQpc', hz),
        'ntfy>cmt': span(e, 'NotifyQpc', 'CommitQpc', hz),
        'cmt>sub': span(e, 'CommitQpc', 'SubmitQpc', hz),
        'sub>pick': span(e, 'SubmitQpc', 'PickupQpc', hz),
        'await': await_us,
        'sub>disp': span(e, 'SubmitQpc', 'DispatchQpc', hz),
        'disp>set': span(e, 'DispatchQpc', 'WakeSetQpc', hz),
        'set>wake': span(e, 'WakeSetQpc', 'WakeQpc', hz),
        'wake>pick': span(e, 'WakeQpc', 'PickupQpc', hz),
    }


def dispatch(e):
    """The first dispatch decision for SubmitFence, '-' when none ran."""
    if not e['DispatchQpc']:
        return '-'
    o = e['DispatchOutcome']
    out = DISPATCH.get(o & 0xff, str(o & 0xff))
    if o & 0xff == 4:
        out += f"={e['DispatchReservedFence']}"
    elif o & 0xff == 8:
        out += f"={e['DispatchHeadFence']}"
    for bit, name in ((DISPATCH_SUBMITTED, '+sub'), (DISPATCH_RETRY, '+retry'),
                      (DISPATCH_NODE1, '+node1'), (DISPATCH_SUBMITTER, '@submitter')):
        if o & bit:
            out += name
    if e['DispatchCount'] > 1:
        out += f"x{e['DispatchCount']}"
    return out


def cpus(e):
    if not e['WakeSetQpc']:
        return '-'
    return f"{e['WakeSetCpu']}>{e['WakeCpu'] if e['WakeQpc'] else '-'}"


def hint(e, hz):
    """The hypothesis whose interval covers most of notify -> pickup."""
    if not e['NotifyQpc'] or not e['PickupQpc']:
        return '?'
    lo, hi = e['NotifyQpc'], e['PickupQpc']
    if hi <= lo:
        return '-'
    bubble = (hi - lo) * 1e6 / hz

    def inside(a, b):
        """Microseconds of [a, b] within the bubble."""
        if not a or not b:
            return 0.0
        return max(min(b, hi) - max(a, lo), 0) * 1e6 / hz

    h1 = inside(lo, e['CommitQpc'])
    h2 = inside(e['AwaitWaitQpc'], e['AwaitExitQpc'])
    ready = max(x for x in (e['CommitQpc'], e['SubmitQpc'], e['AwaitExitQpc'],
                            lo) if x)
    h3 = inside(ready, hi)
    best = max((h1, 'H1'), (h2, 'H2'), (h3, 'H3'))
    return f'{best[1]} {100.0 * best[0] / bubble:.0f}% of {bubble:.0f}us'


def names(mask, table):
    out = [n for bit, n in table if mask & bit]
    return '|'.join(out) if out else '-'


def fmt(v):
    return '-' if v is None else f'{v:.0f}'


def report(trace, out=sys.stdout):
    hz = trace['hz']
    print(f"Wom1PreemptTrace v{trace.get('version', VERSION)}: {trace['count']} preemptions, "
          f"{len(trace['events'])} in ring, QPC {hz} Hz", file=out)
    cols = ('cmd', 'cmd>ntfy', 'ntfy>cmt', 'cmt>sub', 'sub>pick', 'await',
            'sub>disp', 'disp>set', 'set>wake', 'wake>pick')
    print('preempt  dropped  submit(pid)        ' +
          ' '.join(f'{c:>8}' for c in cols) +
          '  dispatch / cpu / defer / claim / path / hint', file=out)
    for e in trace['events']:
        s = spans(e, hz)
        submit = f"{e['SubmitFence']}({e['SubmitPid']})" if e['SubmitFence'] else '-'
        dropped = e['DroppedFence'] or (e['PendingHeadFence'] if e['PendingDropped'] else 0)
        flags = e['SubmitFlags']
        path = 'deferred' if flags & 1 else 'immediate' if flags & 2 else '-'
        if e['AwaitFirstReason']:
            path += '/' + AWAIT.get(e['AwaitFirstReason'], str(e['AwaitFirstReason']))
        claim = CLAIM.get(e['ClaimSource'] & 0xff, '-')
        retries = e['RetryCount']
        extra = []
        if not e['DecideFlags'] & 0x80000000:
            extra.append('refused/paging')
        if retries:
            extra.append(f'retry w{retries & 0xff} d{(retries >> 8) & 0xff}')
        if e['FirstPickupFence'] and e['FirstPickupFence'] != e['SubmitFence']:
            extra.append(f"first-pick {e['FirstPickupFence']}")
        print(f"{e['PreemptionFence']:<8} {dropped or '-':<8} {submit:<18} " +
              ' '.join(f'{fmt(s[c]):>8}' for c in cols) +
              f"  {dispatch(e)} / {cpus(e)} / {names(e['DeferMask'], DEFER)}"
              f" / {claim} / {path} / {hint(e, hz)}"
              + (' / ' + ', '.join(extra) if extra else ''), file=out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('receipts', help='kmd-receipts text file or raw Wom1PreemptTrace bytes')
    args = parser.parse_args(argv)
    with open(args.receipts, 'rb') as f:
        report(parse(registry_value(f.read())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
