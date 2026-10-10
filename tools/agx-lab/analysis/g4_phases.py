# G4 phase timings from the UMD trace (measure-g4-phase lines). argv: umd.log
import sys,collections
# measure-g4-phase values: phase, dt_lo, dt_hi, freq, count(site for phase2), bytes_lo, bytes_hi(shape hi)
st=collections.defaultdict(list); first=last=None
for line in open(sys.argv[1],'rb'):
    if b'measure-g4-phase' not in line: continue
    p=line.decode('latin1').split()
    q=int(p[5].split('=')[1]); v=[int(x,16) for x in p[6:13]]
    first=first if first is not None else q; last=q
    dt=(v[1]|v[2]<<32)/v[3]*1e3
    key=(v[0],v[4]) if v[0]==2 else (v[0],)
    st[key].append(dt)
span=(last-first)/v[3] if first is not None else 0
print('span %.1fs'%span)
for k in sorted(st):
    a=sorted(st[k]); n=len(a)
    print(k,'n',n,'sum %.0fms'%sum(a),'med %.2f p90 %.2f max %.1f'%(a[n//2],a[int(n*.9)],a[-1]))
