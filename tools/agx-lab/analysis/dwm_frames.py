# Per-frame breakdown of DWM's composition thread from an xperf dumper CSV.
# argv: csv tid. Frame = wake from WaitForNextTick .. next switch-out into WaitForNextTick.
import sys, collections, statistics
from etw_cswitch import cswitch_events, frame_bounds
path, tid = sys.argv[1], int(sys.argv[2])
cats = [('retire', 'AgxWin32GpuvaRetire'), ('private-escape', 'AdmissionGpuvaG3PrivateEscape'),
        ('escape', 'AdmissionDdiEscape'), ('sync', 'WaitForSynchronizationObjectFromCpu'),
        ('mutex', 'FastMutex'), ('present', 'Present')]
events = cswitch_events(path, tid)
rows = []
for a, b, start, end in frame_bounds(events):
    c = collections.Counter(); waits = 0
    for e in events[a + 1:b]:
        waits += e[1]
        for name, key in cats:
            if key in e[2]: c[name] += e[1]; break
        else: c['other'] += e[1]
    rows.append((end - start, waits, c, events[b][1]))
dur = [r[0] for r in rows]
print(f'frames {len(rows)}  busy median {statistics.median(dur)/1e3:.2f} ms  p90 {sorted(dur)[int(len(dur)*0.9)]/1e3:.2f}  max {max(dur)/1e3:.2f}')
for lim in (8000, 12000, 16000):
    print(f'  busy > {lim/1e3:.0f} ms: {sum(d > lim for d in dur)} frames')
tot = collections.Counter()
for r in rows: tot.update(r[2])
print('  waits inside frames per frame (ms):', {k: round(v / len(rows) / 1e3, 2) for k, v in tot.most_common()})
run = [r[0] - r[1] for r in rows]
print(f'  running per frame median {statistics.median(run)/1e3:.2f} ms')
slow = sorted(rows, key=lambda r: -r[0])[:5]
for r in slow: print('  slow frame', round(r[0]/1e3, 2), 'ms waits', {k: round(v/1e3, 2) for k, v in r[2].most_common(4)})
