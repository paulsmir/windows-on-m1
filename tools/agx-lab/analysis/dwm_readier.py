# Who readies DWM after WrResource waits, and what was the readier doing (its last switch-in stack) -- grouped.
import sys, collections
path, tid = sys.argv[1], int(sys.argv[2])
rows=[]; last=None; pend_stack=None
state={}  # tid -> last switch-out reason for DWM
waiting=None
ready_by=collections.Counter(); ready_t=collections.Counter()
cur_stack_for=None; stacks={}
lines=open(path, encoding='utf-8', errors='replace')
mode=None; mode_tid=None; buf=[]
def flush():
    global mode,buf
    if mode=='ready' and buf: ready_stacks[mode_key].append(' < '.join(buf[:12]))
    mode=None; buf=[]
ready_stacks=collections.defaultdict(list); mode_key=None
for line in lines:
    s=line.lstrip()
    if s.startswith('Stack,'):
        if mode=='ready':
            p=s.split(',',5)
            if len(p)>=6: buf.append(p[5].strip())
        continue
    if s.startswith('CSwitch,'):
        flush()
        p=[x.strip() for x in s.split(',')]
        if len(p)<14 or not p[1].isdigit(): continue
        if int(p[9])==tid: waiting=(int(p[1]),p[13])
        continue
    if s.startswith('ReadyThread,'):
        flush()
        p=[x.strip() for x in s.split(',')]
        if len(p)<6 or not p[1].isdigit(): continue
        if int(p[5])==tid and waiting and waiting[1]=='WrResource':
            mode='ready'; mode_key=p[2].split('(')[0].strip()+' tid '+p[3]
            ready_by[mode_key]+=1
        continue
flush()
for k,v in ready_by.most_common(5):
    print(v,k)
    c=collections.Counter(ready_stacks[k])
    for st,m in c.most_common(3): print('   ',m,'x',st[:900])
