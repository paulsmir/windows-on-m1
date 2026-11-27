#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# irqwin.py <full.log> [windows]: per 10 s HV window (EXITS line = window end): IRQSTAT vectors, DCP swaps, exit classes.
import re,sys
L=open(sys.argv[1],errors='replace').read().splitlines(); nmax=int(sys.argv[2]) if len(sys.argv)>2 else 10**9
w=[]; cur={'swap':0,'irq':{},'ex':None}
for l in L:
    if 'exact D589 latch' in l: cur['swap']+=1
    m=re.search(r'HV: IRQSTAT dt_ms=\d+ (.*)',l)
    if m: cur['irq']=dict(re.findall(r'(v\d+|other)=(\d+)',m.group(1)))
    m=re.search(r'HV: EXITS dt_ms=\d+ (.*)',l)
    if m:
        cur['ex']=dict(re.findall(r'(\w+)=(\d+)',m.group(1))); w.append(cur); cur={'swap':0,'irq':{},'ex':None}
for i,c in enumerate(w[:nmax]):
    ex=c['ex']; print('W%03d t=%4ds swaps=%2d irq=[%s] exits wfi=%s dabt=%s icc=%s impdef=%s irq=%s fiq=%s'%(i,i*10,c['swap'],' '.join('%s=%s'%kv for kv in sorted(c['irq'].items())),ex.get('wfi'),ex.get('dabt'),ex.get('icc'),ex.get('impdef'),ex.get('irq'),ex.get('fiq')))
