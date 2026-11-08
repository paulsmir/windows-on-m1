#!/usr/bin/env python3
"""Histogram of measure-d3d12-ov2-conflict rows (OV6b) per render index.

Usage: agx_ov6_conflict_decode.py UMD.log
Values: render, x heap, x lo lo/hi, x hi lo/hi, x flags, y heap, y lo, y hi lo/hi, y flags.
"""
import collections
import re
import sys

SRC = [(0x008, 'VB'), (0x010, 'IB'), (0x020, 'IND_ARGS'), (0x040, 'IND_CNT'), (0x080, 'VIEW'),
       (0x100, 'ROOT'), (0x200, 'SO'), (0x400, 'CLEAR'), (0x800, 'TARGET')]


def kinds(flags):
    return '+'.join(n for b, n in SRC if flags & b) or 'none'


def main():
    table = collections.defaultdict(collections.Counter)
    with open(sys.argv[1], errors='replace') as log:
        for line in log:
            if 'measure-d3d12-ov2-conflict' not in line or 'qpc=' not in line:
                continue
            v = [int(x, 16) for x in re.findall(r'\b[0-9a-f]{8}\b', line.split('qpc=')[1])]
            if len(v) < 12:
                continue
            xlo, xhi = v[2] | v[3] << 32, v[4] | v[5] << 32
            ylo, yhi = v[8], v[9] | v[10] << 32
            overlap = max(0, min(xhi, yhi) - max(xlo, ylo))
            key = (kinds(v[6]), 'write' if v[6] & 1 else 'read', kinds(v[11]), 'write' if v[11] & 1 else 'read',
                   'same-heap' if v[1] == v[7] else 'heap-id-differs', overlap)
            table[v[0]][key] += 1
    for render in sorted(table):
        print(f'render {render}: {sum(table[render].values())} rows')
        for key, count in table[render].most_common(5):
            print(f'  {count:6d} render {key[0]} {key[1]} vs compute {key[2]} {key[3]} {key[4]} overlap {key[5]}')


if __name__ == '__main__':
    main()
