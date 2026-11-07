#!/usr/bin/env python3
"""Original EXP1397 CPU input/output oracle; not a shader execution simulator.

Prepare 173 active lanes in three64-threadgroups. Every buffer has192words;
masked tail outputs retain distinct sentinels. All arithmetic is unsigned32.
Run only after parent EXP1397 BEFORE registration.
"""
import json
import struct
import sys
from pathlib import Path
MASK = (1 << 32) - 1
COUNT = 173
LANES = 192
SEED = 0x13579BDF
SENTINEL_X = 0xD15EA5E1
SENTINEL_Y = 0x6BADC0DE

def u32(value):
    return value & MASK

def vectors():
    a = [u32(u32(0x9E3779B9 * (i + 1)) ^ u32(0x2468ACE1 + 17 * i * i))
         for i in range(LANES)]
    b = [u32(u32(0x80000000 + i * 0x10203) ^ u32(0xA5A5A5A5 - 13 * i))
         for i in range(LANES)]
    x = [u32(3 * a[i] + 5 * b[i] + SEED) if i < COUNT else SENTINEL_X
         for i in range(LANES)]
    y = [u32(7 * a[i] - 2 * b[i] + 11 * SEED + i) if i < COUNT else SENTINEL_Y
         for i in range(LANES)]
    # Distinguish SRV swaps, output swaps, CBV seed loss and overflow mistakes.
    assert any(x[i] != u32(3 * b[i] + 5 * a[i] + SEED) for i in range(COUNT))
    assert any(y[i] != u32(7 * b[i] - 2 * a[i] + 11 * SEED + i) for i in range(COUNT))
    assert any(x[i] != y[i] for i in range(COUNT))
    assert any(3 * a[i] + 5 * b[i] + SEED > MASK for i in range(COUNT))
    assert any(7 * a[i] - 2 * b[i] + 11 * SEED + i < 0 or
               7 * a[i] - 2 * b[i] + 11 * SEED + i > MASK for i in range(COUNT))
    return {'count': COUNT, 'lanes': LANES, 'seed': SEED,
            'workgroup': [64, 1, 1], 'dispatch': [3, 1, 1],
            'cbv_b0_space3': [COUNT, SEED, 0, 0],
            'srv_t0_space2': a, 'srv_t0_space7': b,
            'initial_u0_space1': [SENTINEL_X] * LANES,
            'initial_u0_space9': [SENTINEL_Y] * LANES,
            'expected_u0_space1': x, 'expected_u0_space9': y}

if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: oracle.py OUTPUT_DIRECTORY')
    target = Path(sys.argv[1])
    target.mkdir(parents=True, exist_ok=True)
    data = vectors()
    (target / 'vectors.json').write_text(json.dumps(data, indent=2) + '\n')
    for name, words in data.items():
        if name.startswith(('srv_', 'initial_', 'expected_', 'cbv_')):
            (target / (name + '.bin')).write_bytes(struct.pack('<' + 'I' * len(words), *words))
    print('EXP1397_CPU_ORACLE_READY active=173 lanes=192 tail=19 uint32=YES distinct_spaces=YES')
