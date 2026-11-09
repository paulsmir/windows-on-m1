#!/usr/bin/env python3
"""Histogram of measure-d3d12-ov8-range and -barrier rows (OV8 step 1b) per render.

Usage: agx_ov8_pair_decode.py UMD.log
"""
import collections
import re
import sys

SRC = [(4, 'VERTEX'), (0x008, 'VB'), (0x010, 'IB'), (0x020, 'IND_ARGS'), (0x040, 'IND_CNT'), (0x080, 'VIEW'),
       (0x100, 'ROOT'), (0x200, 'SO'), (0x400, 'CLEAR'), (0x800, 'TARGET')]


def kinds(flags):
    return '+'.join(n for b, n in SRC if flags & b) or 'none'


def rows(path, tag):
    with open(path, errors='replace') as log:
        for line in log:
            if line.startswith(tag + ' ') and 'qpc=' in line:
                yield [int(x, 16) for x in re.findall(r'\b[0-9a-f]{8}\b', line.split('qpc=')[1])]


def main():
    path = sys.argv[1]
    table = collections.defaultdict(collections.Counter)
    offsets = collections.defaultdict(set)
    for v in rows(path, 'measure-d3d12-ov8-range'):
        key = (kinds(v[4]), 'w' if v[4] & 1 else 'r', kinds(v[8]), 'w' if v[8] & 1 else 'r',
               'same-heap' if v[1] == v[5] else 'other-heap', v[3], v[7])
        table[v[0]][key] += 1
        offsets[v[0]].add((v[2], v[6]))
    for render in sorted(table):
        print(f'range render {render}: distinct (render lo, compute lo) pairs {len(offsets[render])}')
        for key, count in table[render].most_common(4):
            print(f'  {count:5d} {key}')
    barriers = collections.defaultdict(collections.Counter)
    for v in rows(path, 'measure-d3d12-ov8-barrier'):
        barriers[v[0]][(['transition', 'aliasing', 'uav', 'other'][v[1]], hex(v[2]), hex(v[3]), hex(v[4]))] += 1
    for render in sorted(barriers):
        print(f'barrier render {render}:')
        for key, count in barriers[render].most_common(4):
            print(f'  {count:5d} {key}')


if __name__ == '__main__':
    main()
