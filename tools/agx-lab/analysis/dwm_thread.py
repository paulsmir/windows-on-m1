# The DWM composition thread of an xperf dumper CSV: the tid whose stacks show
# WaitForNextTick most often. argv: csv. Prints "<tid> <stack lines>" for the top 3.
import sys, collections
c = collections.Counter()
with open(sys.argv[1], encoding='utf-8', errors='replace') as f:
    for line in f:
        if line.lstrip().startswith('Stack,') and 'WaitForNextTick' in line:
            p = line.split(',', 5)
            if len(p) >= 6 and p[2].strip().isdigit():
                c[int(p[2])] += 1
for tid, n in c.most_common(3):
    print(tid, n)
