#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# v857gate.py <full.log>: early USB-path gate (architect 2026-11-27). Windows = HV EXITS-delimited 10 s windows from guest start.
# STUCK when, after window W004, v857 stays at or below the floor (60/10 s) for 3 consecutive windows before any window above it
# AND the launcher has not seen SSH (the caller passes that; this tool only judges the log). Prints one V857GATE line.
import re,sys
L=open(sys.argv[1],errors='replace').read().splitlines(); FLOOR=60; w=[]; cur=None
for l in L:
    m=re.search(r'HV: IRQSTAT dt_ms=\d+ (.*)',l)
    if m: cur=int(dict(re.findall(r'(v\d+|other)=(\d+)',m.group(1))).get('v857',0))
    if 'HV: EXITS dt_ms=' in l: w.append(cur if cur is not None else 0); cur=None
if len(w)<8: print('V857GATE WAIT windows=%d'%len(w)); sys.exit(2)
seq=w[4:8]
if all(v<=FLOOR for v in seq[:3]): print('V857GATE STUCK W004-W006=%s (floor %d): USB path stuck - needs hub power cycle by the owner (charger out, replug adapter, charger in)'%(seq[:3],FLOOR)); sys.exit(1)
print('V857GATE OK W004-W007=%s'%seq); sys.exit(0)
