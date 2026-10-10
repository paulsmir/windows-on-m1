# Kernel-only waits (no user-mode frame) of the DWM composition thread, in slow
# frames (>= 16 ms) and in all frames. argv: csv tid
import sys, collections
from etw_cswitch import cswitch_events, frame_bounds
events = cswitch_events(sys.argv[1], int(sys.argv[2]))
agg = collections.Counter(); n = collections.Counter(); allagg=collections.Counter(); alln=collections.Counter()
for a, b, start, end in frame_bounds(events):
    slow = end - start >= 16000
    for e in events[a + 1:b]:
        fr = [x for x in e[2].split(' < ') if not x.startswith(('ntoskrnl', 'hal.dll', 'ntdll.dll!Nt', 'ntdll.dll!Zw', 'win32u.dll'))]
        if fr: continue
        k = ' < '.join(x for x in e[2].split(' < ')[:14])
        allagg[k]+=e[1]; alln[k]+=1
        if slow: agg[k] += e[1]; n[k] += 1
print('slow frames:')
for k, v in agg.most_common(6): print(f'{v/1e3:9.1f} ms {n[k]:5d} x  {k[:900]}')
print('all frames:')
for k, v in allagg.most_common(4): print(f'{v/1e3:9.1f} ms {alln[k]:5d} x  {k[:300]}')
