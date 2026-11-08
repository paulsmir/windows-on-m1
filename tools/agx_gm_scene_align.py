#!/usr/bin/env python3
"""Align a GravityMark D3D12 UMD log with Apple's mtlsplitd CSV by scene and compare per-job GPU time.

GravityMark's camera path advances per frame, not per second, so a run at a lower frame rate
reaches each scene segment later.  Comparing run medians therefore compares different scenes
(EXP2268 offline: our C2 looked 2x slower than Apple's c11 until the series were aligned).

  export-ours  UMD.log out.csv      per-frame CRCRCRCR position times (C0/C2/C4/C6 exact, R spans) + t
  export-apple splitd.csv out.csv   per-frame Apple encoder-group sums (C0blk=enc 0-9, C2=11+13, C4=15,
                                    C6=18+19, R1=16+17, R3=20, R5=10, R7=14) + t
  compare      ours.csv apple.csv [mtltime-hd.csv]
                                    best time lag by C2 cross-correlation, then per-10-s ratios and,
                                    with the mtltime CSV, our wall vs Apple's unsplit GPU frame.
"""
import bisect, collections, csv, math, statistics as st, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

GROUPS = {'C0blk': [0, 1, 3, 5, 7, 9], 'C2': [11, 13], 'C4': [15], 'C6': [18, 19],
          'R1': [16, 17], 'R3': [20], 'R5': [10], 'R7': [14]}


def export_ours(log, out):
    import agx_gpu_frame_breakdown as B
    lines = open(log, 'rb').read().decode('utf-8', 'ignore').splitlines()
    pid, hz, jobs, presents = B.parse(lines)
    B.timeline(jobs, hz)
    by_cmp = sorted(jobs, key=lambda j: j['cmp']); keys = [j['cmp'] for j in by_cmp]
    t0 = presents[0]
    w = csv.writer(open(out, 'w')); w.writerow(['idx', 't', 'wall', 'sig', 'C0', 'C2', 'C4', 'C6', 'R1', 'R3', 'R5', 'R7', 'span'])
    for i, (a, b) in enumerate(zip(presents[:-1], presents[1:])):
        js = by_cmp[bisect.bisect_left(keys, a):bisect.bisect_left(keys, b)]
        if not js: continue
        sig = ''.join('R' if j['kind'] == B.RENDER else 'C' for j in js)
        row = [i, round((a - t0) / hz, 3), round((b - a) * 1e3 / hz, 3), sig]
        if sig == 'CRCRCRCR':
            row += [round(js[k]['exact'], 3) if js[k]['exact'] is not None else '' for k in (0, 2, 4, 6)]
            row += [round(js[k]['span'], 3) for k in (1, 3, 5, 7)]
        else: row += [''] * 8
        row.append(round(sum(j['span'] for j in js), 3)); w.writerow(row)


def export_apple(src, out):
    rows = [r for r in csv.reader(open(src)) if r and not r[0].startswith('#')]
    fr = collections.defaultdict(lambda: collections.defaultdict(float)); fstart = {}; nd = collections.Counter()
    for r in rows:
        if len(r) >= 6 and r[1].isdigit():
            f = int(r[1]); e = int(r[2]) if r[2].isdigit() else -1
            if r[0] == 'compute-d' and len(r) >= 14: s = float(r[12]); d = (float(r[13]) - s) * 1e3; nd[f] += 1
            else:
                try: s = float(r[4]); d = (float(r[5]) - s) * 1e3
                except ValueError: continue
            fr[f][e] += d; fstart[f] = min(fstart.get(f, s), s)
    t0 = min(fstart.values())
    w = csv.writer(open(out, 'w')); w.writerow(['idx', 't'] + list(GROUPS) + ['all', 'ndisp'])
    for f in sorted(fr):
        w.writerow([f, round(fstart[f] - t0, 3)] + [round(sum(fr[f].get(e, 0) for e in GROUPS[g]), 3) for g in GROUPS]
                   + [round(sum(fr[f].values()), 3), nd[f]])


def compare(ours_csv, apple_csv, mtltime=None, res=1.0):
    ours = [r for r in csv.DictReader(open(ours_csv)) if r['sig'] == 'CRCRCRCR' and r['C2']]
    apple = list(csv.DictReader(open(apple_csv)))
    def series(rows, key):
        b = collections.defaultdict(list)
        for r in rows: b[int(float(r['t']) // res)].append(float(r[key]))
        return {k: st.median(v) for k, v in b.items()}
    o, a = series(ours, 'C2'), series(apple, 'C2')
    def corr(lag):
        xs = [(v, a[t - lag]) for t, v in o.items() if (t - lag) in a]
        if len(xs) < 30: return None
        mx = st.mean(x for x, _ in xs); my = st.mean(y for _, y in xs)
        sx = math.sqrt(sum((x - mx) ** 2 for x, _ in xs)); sy = math.sqrt(sum((y - my) ** 2 for _, y in xs))
        return sum((x - mx) * (y - my) for x, y in xs) / (sx * sy) if sx and sy else None
    scan = [(corr(l), l) for l in range(-5, 60)]
    best = max((c, l) for c, l in scan if c is not None)
    lag = best[1]
    print(f"lag (ours t = apple t + lag): {lag} s, C2 corr {best[0]:.3f}")
    ob = collections.defaultdict(lambda: collections.defaultdict(list)); ab = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in ours:
        bn = int(float(r['t']) // 10)
        for k in ('C0', 'C2', 'C4', 'C6', 'wall'): ob[bn][k].append(float(r[k]))
        ob[bn]['rsum'].append(sum(float(r[k]) for k in ('R1', 'R3', 'R5', 'R7')))
    for r in apple:
        bn = int((float(r['t']) + lag) // 10)
        for k in GROUPS: ab[bn][k].append(float(r[k]))
        ab[bn]['all'].append(float(r['all'])); ab[bn]['rsum'].append(sum(float(r[k]) for k in ('R1', 'R3', 'R5', 'R7')))
    mt = collections.defaultdict(list)
    if mtltime:
        cb = sorted((float(r[4]), (float(r[5]) - float(r[4])) * 1e3) for r in csv.reader(open(mtltime)) if r and r[0] == 'cb')
        for s, d in cb: mt[int((s - cb[0][0] + lag) // 10)].append(d)
    print("  t    n   C0 ours/apple  C2  C4  C6 | render sum | our wall | apple split-sum | apple unsplit GPU")
    for bn in sorted(ob):
        if bn not in ab or len(ob[bn]['wall']) < 200: continue
        s = f"{bn * 10:4d} {len(ob[bn]['wall']):4d}"
        for g, ag in (('C0', 'C0blk'), ('C2', 'C2'), ('C4', 'C4'), ('C6', 'C6')):
            s += f"  {st.median(ob[bn][g]):5.2f}/{st.median(ab[bn][ag]):5.2f}"
        s += f" | {st.median(ob[bn]['rsum']):5.2f}/{st.median(ab[bn]['rsum']):5.2f} | {st.median(ob[bn]['wall']):5.2f} | {st.median(ab[bn]['all']):5.2f}"
        if mt.get(bn): s += f" | {st.median(mt[bn]):5.2f}"
        print(s)


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'export-ours': export_ours(sys.argv[2], sys.argv[3])
    elif cmd == 'export-apple': export_apple(sys.argv[2], sys.argv[3])
    elif cmd == 'compare': compare(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    else: sys.exit(__doc__)
