# Frame pacing from AppleAgxVsyncTrace: intervals between latch events (k=3), in vblank periods.
# argv: vsync-*.txt ... ; t in 100 ns units. Skips the first/last 2 s of each trace.
import sys, re, collections
for path in sys.argv[1:]:
    ts = [int(m.group(1)) for m in (re.search(r't=(\d+) k=3 ', l) for l in open(path, errors='replace')) if m]
    if len(ts) < 10: print(path.split('/')[-1], 'few latches', len(ts)); continue
    ts.sort(); lo, hi = ts[0] + 2e7, ts[-1] - 2e7
    ts = [t for t in ts if lo <= t <= hi]
    per = 166667.0
    h = collections.Counter(min(4, max(1, round((b - a) / per))) for a, b in zip(ts, ts[1:]))
    n = sum(h.values()); span = (ts[-1] - ts[0]) / 1e7
    print(f"{path.split('/')[-1]:24} {len(ts)/span:5.1f} latches/s  1p {h[1]/n:5.1%}  2p {h[2]/n:5.1%}  3p {h[3]/n:5.1%}  4p+ {h[4]/n:5.1%}  n={n}")
