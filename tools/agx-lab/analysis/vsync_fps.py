# Flips per window from an AppleAgxVsyncTrace log (t= in 100 ns, as= flip counter).
# argv: vsync-*.txt [window seconds, default 10]. Prints "<start>s <flips/s>" per window.
import sys,re
pts=[]
for l in open(sys.argv[1],errors='replace'):
    m=re.search(r't=(\d+) .*as=(\d+)',l)
    if m: pts.append((int(m.group(1)),int(m.group(2))))
if not pts: sys.exit()
w=float(sys.argv[2]) if len(sys.argv)>2 else 10.0
t0=pts[0][0]; out=[]; i=0
while i<len(pts):
    ts=pts[i][0]; a0=pts[i][1]; j=i
    while j<len(pts) and pts[j][0]-ts<w*1e7: j+=1
    a1=pts[j-1][1]; dt=(pts[j-1][0]-ts)/1e7
    if dt>0: out.append('%5.0fs %5.1f' % ((ts-t0)/1e7,(a1-a0)/dt))
    i=j
print(' | '.join(out))
