# Off-CPU waits of one thread grouped by the first non-kernel frames of the switch-in stack.
# argv: csv tid [frames per key, default 8]
import sys, collections
from etw_cswitch import cswitch_events
events = cswitch_events(sys.argv[1], int(sys.argv[2]))
agg=collections.Counter(); n=collections.Counter()
skip=('ntoskrnl','hal.dll')
for ts,w,st in events:
    fr=[x for x in st.split(' < ') if not x.startswith(skip)]
    k=' < '.join(fr[:int(sys.argv[3]) if len(sys.argv)>3 else 8]) or '(kernel only)'
    agg[k]+=w; n[k]+=1
tot=sum(agg.values())
print(f'total off-CPU {tot/1e3:.1f} ms in {sum(n.values())} waits')
for k,v in agg.most_common(10): print(f'{v/1e3:9.1f} ms {n[k]:5d} x  {k[:800]}')
