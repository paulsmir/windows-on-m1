#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# defjudge.py <defwatch.log> <kit run.log>: per GM arm (GM_START..GM_RESULT, host UTC), INVALID if a WindowsUpdate install event
# (ev19 'Installation Successful', guest UTC stamp; guest clock must be synced) falls inside the arm, or the signature/engine seen by
# watcher polls inside the arm differs from the last poll before GM_START. VBS-timer ev19 lines are ignored.
import re,sys
def t(s): h,m,x=s.split(':'); return int(h)*3600+int(m)*60+int(x)
W=[]; INST=set()
for l in open(sys.argv[1],errors='replace'):
    m=re.match(r'(\d\d:\d\d:\d\d)Z DEFWATCH sig=(\S+) sigupd=\S+ engine=(\S+) ev19=\S*T(\d\d:\d\d:\d\d) ev19msg=(.*)',l)
    if m:
        W.append((t(m.group(1)),m.group(2),m.group(3)))
        if 'Installation Successful' in m.group(5): INST.add((t(m.group(4)),m.group(5)[:90]))
st={}; en={}
for l in open(sys.argv[2],errors='replace'):
    m=re.match(r'(\d\d:\d\d:\d\d)Z GM_START (\S+)',l)
    if m: st[m.group(2)]=t(m.group(1))
    m=re.match(r'(\d\d:\d\d:\d\d)Z GM_RESULT (\S+)',l)
    if m: en[m.group(2)]=t(m.group(1))
for a in st:
    e=en.get(a); b=[w for w in W if w[0]<=st[a]]
    if e is None or not b: print('DEFJUDGE %s NO_DATA (arm open or no poll before start)'%a); continue
    ref=b[-1][1:]; chg=[w for w in W if st[a]<w[0]<=e and w[1:]!=ref]; ins=[i for i in INST if st[a]<=i[0]<=e]
    v='INVALID' if (chg or ins) else 'OK'
    print('DEFJUDGE %s %s ref=%s/%s%s%s'%(a,v,ref[0],ref[1],(' poll_change=%s/%s'%chg[0][1:]) if chg else '',(' install_in_arm=[%s]'%ins[0][1]) if ins else ''))
