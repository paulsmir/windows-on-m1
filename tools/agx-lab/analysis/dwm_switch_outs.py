# DWM composition thread switch-outs: group off-CPU time by (OldState, WaitReason, preempting process) for all frames and slow frames.
import sys, collections
path, tid = sys.argv[1], int(sys.argv[2])
outs=[]  # (ts_out, state, reason, newproc, cpu, ts_in)
cur=None
for line in open(path, encoding='utf-8', errors='replace'):
    s=line.lstrip()
    if not s.startswith('CSwitch,'): continue
    p=[x.strip() for x in s.split(',')]
    if len(p)<17 or not p[1].isdigit(): continue
    ts=int(p[1]); newtid=int(p[3]); oldtid=int(p[9])
    if oldtid==tid: cur=[ts,p[12],p[13],p[2].split('(')[0].strip(),p[16],None]
    if newtid==tid and cur: cur[5]=ts; outs.append(tuple(cur)); cur=None
tot=collections.Counter(); n=collections.Counter()
for o in outs:
    k=(o[1],o[2],o[3] if o[1]=='Running' or o[2] in ('WrDispatchInt','WrPreempted') else '')
    tot[k]+=o[5]-o[0]; n[k]+=1
span=(outs[-1][5]-outs[0][0])/1e6
print(f'switch-outs {len(outs)} over {span:.1f} s')
for k,v in tot.most_common(14): print(f'{v/1e3:9.1f} ms {n[k]:6d} x avg {v/n[k]:7.0f} us  {k}')
