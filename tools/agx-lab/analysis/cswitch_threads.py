# Per-thread run/wait attribution from xperf dumper CSwitch + Stack lines.
# argv: csv process-substring [top-threads]
import sys, collections
path, proc = sys.argv[1], sys.argv[2]
top_n = int(sys.argv[3]) if len(sys.argv) > 3 else 2
run = collections.Counter(); out_ts = {}; in_ts = {}; out_reason = {}
wait = collections.defaultdict(collections.Counter); waitn = collections.defaultdict(collections.Counter)
reason_t = collections.defaultdict(collections.Counter)
pend = None; frames = []; first = last = None
def flush():
    global pend, frames
    if pend:
        tid, dt = pend
        sig = [f for f in frames if not f.startswith(('ntoskrnl.exe', 'hal.dll', 'ntdll.dll!Nt', 'ntdll.dll!Zw', 'win32u.dll'))]
        k = ' < '.join(sig[:16]) if sig else '(kernel only)'
        wait[tid][k] += dt; waitn[tid][k] += 1
    pend = None; frames = []
with open(path, encoding='utf-8', errors='replace') as f:
    for line in f:
        s = line.lstrip()
        if s.startswith('Stack,'):
            if pend:
                p = s.split(',', 5)
                if len(p) >= 6 and p[2].strip().isdigit() and int(p[2]) == pend[0]:
                    frames.append(p[5].strip())
            continue
        if not s.startswith('CSwitch,'):
            continue
        flush()
        p = [x.strip() for x in s.split(',')]
        if len(p) < 14 or not p[1].isdigit(): continue
        ts = int(p[1]); first = first or ts; last = ts
        newp, newtid, oldp, oldtid, oldstate, wr = p[2], int(p[3]), p[8], int(p[9]), p[12], p[13]
        if proc in oldp:
            if oldtid in in_ts: run[oldtid] += ts - in_ts.pop(oldtid)
            out_ts[oldtid] = ts; out_reason[oldtid] = (oldstate, wr)
        if proc in newp:
            in_ts[newtid] = ts
            if newtid in out_ts:
                dt = ts - out_ts.pop(newtid)
                reason_t[newtid][out_reason.get(newtid, ('?', '?'))] += dt
                pend = (newtid, dt)
flush()
span = (last - first) / 1e6
print('span %.2f s' % span)
for tid, r in run.most_common(top_n):
    tw = sum(wait[tid].values())
    print('\n== tid %d running %.2f s (%.0f%%), waiting %.2f s' % (tid, r / 1e6, 100 * r / 1e6 / span, tw / 1e6))
    for (st, wr), v in reason_t[tid].most_common(6): print('   out-state %-8s %-14s %8.1f ms' % (st, wr, v / 1e3))
    for k, v in wait[tid].most_common(12):
        print('%9.1f ms %6d x  %s' % (v / 1e3, waitn[tid][k], k[:1500]))
