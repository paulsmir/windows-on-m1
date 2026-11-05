#!/usr/bin/env python3
"""Decode measure-d3d12-job-barriers (OV1): the D3D12 ResourceBarrier calls
between consecutive submitted GPU jobs of a D3D12 UMD log.

Usage: agx_job_barriers.py UMD.log [--skip 0.3] [--frames N] [--json]

Enable on the guest: APPLE_AGX_D3D12_JOB_BARRIERS=1 with APPLE_AGX_UMD_TRACE_FILE
set (APPLE_AGX_UMD_REFUSALS_ONLY=1 still passes measure- receipts).

One receipt per submitted Mesa batch, in submission order (emitted from the
measure-batch-deps hook, agx_d3d12_job_barriers.h):
  {kind (1 render, 2 compute), previous kind, barriers, transitions, UAV,
   aliasing, OR StateBefore, OR StateAfter, distinct resources, xor of their
   ids, global (null) UAV barriers, trigger, barriers since previous batch}
trigger: bits 0-7 = the replayed command kind whose owner work submitted the
batch (Mesa's lazy flush of the previous job; 0 = between commands: upload
boundary or the replay's end), bit 8 = more than 32 resources, bits 16-19 =
OR of the barrier flags (1 BEGIN_ONLY, 2 END_ONLY, 8 0022 ALIASING).

Receipt i describes the barriers recorded after the last command that went
to batch i and before the first command of batch i + 1, i.e. the boundary
"K_i -> K_i+1". When one command submits several batches, only the first
carries the gap (the rest show 0 with the same trigger).

Frames are the intervals between measure-d3d12-present rows of the process;
batches are numbered per frame with their kind (C0, R1, ...). The summary
groups frames by their kind sequence and prints, per boundary, the most
common barrier signature and in how many frames it occurred.
"""
import argparse
import collections
import json
import statistics

RENDER, COMPUTE = 1, 2
KIND = {RENDER: 'R', COMPUTE: 'C'}
COMMAND = {0: 'between', 1: 'copy', 2: 'dispatch', 3: 'clear', 4: 'texture-copy', 5: 'draw',
           6: 'clear-depth', 7: 'region-copy', 8: 'clear-uav', 9: 'query', 11: 'indirect',
           12: 'resolve', 0xff: 'other'}
STATES = ((0x1, 'VCB'), (0x2, 'IB'), (0x4, 'RT'), (0x8, 'UAV'), (0x10, 'DEPTH_W'), (0x20, 'DEPTH_R'),
          (0x40, 'NPS_SRV'), (0x80, 'PS_SRV'), (0x100, 'SO'), (0x200, 'INDIRECT'), (0x400, 'COPY_DST'),
          (0x800, 'COPY_SRC'), (0x1000, 'RESOLVE_DST'), (0x2000, 'RESOLVE_SRC'))
WRITE_STATES = 0x4 | 0x8 | 0x10 | 0x100 | 0x400 | 0x1000
VALUES = 13


def states(mask):
    if not mask:
        return 'COMMON'
    names = [n for bit, n in STATES if mask & bit]
    rest = mask & ~sum(bit for bit, _ in STATES)
    if rest:
        names.append('0x%x' % rest)
    return '|'.join(names)


def decode(words):
    return dict(kind=words[0], previous=words[1], barriers=words[2], transitions=words[3], uav=words[4],
                aliasing=words[5], before=words[6], after=words[7], resources=words[8], id_hash=words[9],
                global_uav=words[10], trigger=words[11] & 0xff, overflow=bool(words[11] & 0x100),
                flags=(words[11] >> 16) & 0xf, total=words[12])


def parse(lines):
    """Return (pid, receipts [(qpc, record)], present QPCs) of the process
    with the most job-barrier receipts."""
    rows, presents = {}, {}
    for line in lines:
        tag = line.split(' ', 1)[0]
        if tag not in ('measure-d3d12-job-barriers', 'measure-d3d12-present'):
            continue
        fields = line.split()
        meta = dict(f.split('=', 1) for f in fields[1:] if '=' in f)
        pid, qpc = int(meta['pid']), int(meta['qpc'])
        if tag == 'measure-d3d12-present':
            presents.setdefault(pid, []).append(qpc)
            continue
        words = [int(x, 16) for x in fields[1:] if '=' not in x]
        if len(words) == VALUES:
            rows.setdefault(pid, []).append((qpc, decode(words)))
    if not rows:
        raise ValueError('no measure-d3d12-job-barriers rows (APPLE_AGX_D3D12_JOB_BARRIERS=1?)')
    pid = max(rows, key=lambda p: len(rows[p]))
    return pid, rows[pid], sorted(presents.get(pid, []))


def frames(rows, presents):
    """Receipts per frame (between consecutive presents); without presents,
    the whole log is one frame."""
    if len(presents) < 2:
        return [[r for _, r in rows]]
    out, i = [], 0
    rows = sorted(rows, key=lambda x: x[0])
    for a, b in zip(presents[:-1], presents[1:]):
        while i < len(rows) and rows[i][0] < a:
            i += 1
        frame = []
        while i < len(rows) and rows[i][0] < b:
            frame.append(rows[i][1])
            i += 1
        out.append(frame)
    return out


def signature(r):
    """A stable, human-readable description of one boundary's barriers."""
    if not r['barriers']:
        return 'none'
    parts = []
    if r['transitions']:
        parts.append('%d transition %s->%s' % (r['transitions'], states(r['before']), states(r['after'])))
    if r['uav']:
        parts.append('%d UAV%s' % (r['uav'], ' (%d global)' % r['global_uav'] if r['global_uav'] else ''))
    if r['aliasing']:
        parts.append('%d aliasing' % r['aliasing'])
    other = r['barriers'] - r['transitions'] - r['uav'] - r['aliasing']
    if other:
        parts.append('%d ranged' % other)
    parts.append('%d%s resources' % (r['resources'], '+' if r['overflow'] else ''))
    if r['flags'] & 3:
        parts.append('split %s' % ('begin' if r['flags'] & 1 else 'end'))
    return ', '.join(parts)


def hazard(r):
    """True when the boundary orders a write before a later access (a write
    state in StateBefore, a UAV or an aliasing barrier)."""
    return bool(r['uav'] or r['aliasing'] or (r['transitions'] and r['before'] & WRITE_STATES))


def labels(frame):
    return ['%s%d' % (KIND.get(r['kind'], '?'), i) for i, r in enumerate(frame)]


def boundary_rows(frame):
    names = labels(frame)
    out = []
    for i, r in enumerate(frame):
        nxt = names[i + 1] if i + 1 < len(frame) else 'next-frame'
        row = dict(r)
        row.update(boundary='%s -> %s' % (names[i], nxt), trigger=COMMAND.get(r['trigger'], str(r['trigger'])),
                   hazard=hazard(r), signature=signature(r), id_hash='%08x' % r['id_hash'])
        out.append(row)
    return out


def summary(frame_list):
    shapes = collections.Counter(''.join(KIND.get(r['kind'], '?') for r in f) for f in frame_list)
    result = []
    for shape, count in shapes.most_common():
        group = [f for f in frame_list if ''.join(KIND.get(r['kind'], '?') for r in f) == shape]
        bounds = []
        for i in range(len(shape)):
            column = [boundary_rows(f)[i] for f in group]
            sigs = collections.Counter((c['signature'], c['id_hash']) for c in column)
            (sig, ids), seen = sigs.most_common(1)[0]
            bounds.append(dict(boundary=column[0]['boundary'], signature=sig, id_hash=ids, frames=seen,
                               of=len(group), hazard_frames=sum(c['hazard'] for c in column),
                               barriers_median=statistics.median(c['barriers'] for c in column),
                               triggers=dict(collections.Counter(c['trigger'] for c in column))))
        result.append(dict(shape=shape, frames=count, boundaries=bounds))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log')
    ap.add_argument('--skip', type=float, default=0.3, help='leading fraction of frames to ignore (warm-up)')
    ap.add_argument('--frames', type=int, default=0, help='also print the raw boundaries of the first N frames')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()
    with open(args.log, 'r', errors='replace') as handle:
        lines = [l for l in handle if l.startswith(('measure-d3d12-job-barriers', 'measure-d3d12-present'))]
    pid, rows, presents = parse(lines)
    frame_list = frames(rows, presents)
    frame_list = [f for f in frame_list[int(len(frame_list) * args.skip):] if f]
    groups = summary(frame_list)
    if args.json:
        print(json.dumps(dict(pid=pid, receipts=len(rows), frames=len(frame_list), groups=groups,
                              first_frames=[boundary_rows(f) for f in frame_list[:args.frames]]), indent=1))
        return
    print('pid %d: %d receipts, %d frames analysed' % (pid, len(rows), len(frame_list)))
    for g in groups[:3]:
        print('\nframe shape %s (%d frames)' % (g['shape'], g['frames']))
        for b in g['boundaries']:
            print('  %-18s %-60s ids %s  [%d/%d frames, hazard in %d, triggers %s]' % (
                b['boundary'] + ':', b['signature'], b['id_hash'], b['frames'], b['of'], b['hazard_frames'],
                ','.join('%s:%d' % kv for kv in sorted(b['triggers'].items()))))
    for n, f in enumerate(frame_list[:args.frames]):
        print('\nframe %d' % n)
        for b in boundary_rows(f):
            print('  %-18s %-60s ids %s trigger %s total %d' % (b['boundary'] + ':', b['signature'], b['id_hash'],
                                                               b['trigger'], b['total']))


if __name__ == '__main__':
    main()
