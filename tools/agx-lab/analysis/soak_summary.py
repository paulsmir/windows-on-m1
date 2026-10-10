# Soak summary (probes/desktop-loads.sh with CYCLES>1): per cycle flips (steady 10 s windows), pacing, DWM handles/private MB, private-pool stats.
import sys,re,struct,collections,os
E=sys.argv[1]  # evidence dir
def latches(p):
    try: return sorted(int(m.group(1)) for m in (re.search(r't=(\d+) k=3 ',l) for l in open(p,errors='replace')) if m)
    except FileNotFoundError: return []
def pacing(ts):
    if len(ts)<10: return None
    lo,hi=ts[0]+2e7,ts[-1]-2e7; ts=[t for t in ts if lo<=t<=hi]; per=166667.0
    h=collections.Counter(min(4,max(1,round((b-a)/per))) for a,b in zip(ts,ts[1:])); n=sum(h.values()) or 1
    return h[1]/n
def peak10(ts):
    best=0
    for i,t in enumerate(ts):
        j=i
        while j<len(ts) and ts[j]<t+1e8: j+=1
        best=max(best,(j-i)/10)
    return best
def pp(path):
    try: b=open(path,'rb').read()
    except FileNotFoundError: return None
    v=struct.unpack('<%dQ'%(len(b)//8),b); ps=v[-16:]
    return dict(hit=ps[0],miss=ps[1],trim=ps[2],pressure=ps[3],mapped=ps[5],unmapped=ps[6],reap=ps[8])
def dwm(path):
    try:
        for l in open(path,errors='replace'):
            f=l.split()
            if len(f)>=4 and f[1]=='dwm': return f[0],f[2],f[3]
    except FileNotFoundError: pass
    return None
loads=['npmove','nptype','exmove','gdimove','gdiresize']
for c in range(1,5):
    row=[]
    for t in loads:
        ts=latches(f'{E}/vsync-{t}-c{c}.txt'); p=pacing(ts)
        row.append(f"{t} {peak10(ts):4.1f}/{(p or 0)*100:4.1f}%")
    s=pp(f'{E}/rcpt-settings-c{c}/Wom1G3PagingProfile.bin')
    print(f"c{c}: "+' | '.join(row)); print('    dwm(pid,handles,MB)',dwm(f'{E}/procs-settings-c{c}.txt'),'pp',s)
