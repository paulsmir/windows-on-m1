# Off-CPU waits inside slow DWM frames (>= 16 ms), excluding the known driver waits,
# grouped by the first user-mode frames of the switch-in stack. argv: csv tid
import sys, collections
from etw_cswitch import cswitch_events, frame_bounds
events = cswitch_events(sys.argv[1], int(sys.argv[2]))
agg = collections.Counter(); n = collections.Counter()
for a, b, start, end in frame_bounds(events):
    if end - start < 16000: continue
    for e in events[a + 1:b]:
        if any(k in e[2] for k in ('AgxWin32GpuvaRetire','AdmissionGpuvaG3PrivateEscape','WaitForSynchronizationObjectFromCpu')): continue
        fr = [x for x in e[2].split(' < ') if not x.startswith(('ntoskrnl', 'hal.dll', 'ntdll.dll!Nt', 'ntdll.dll!Zw', 'win32u.dll'))]
        k = ' < '.join(fr[:9]) or '(kernel only)'
        agg[k] += e[1]; n[k] += 1
for k, v in agg.most_common(10): print(f'{v/1e3:9.1f} ms {n[k]:5d} x  {k[:700]}')
