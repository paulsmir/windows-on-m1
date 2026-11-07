#!/usr/bin/env python3
"""ANS008 raw-write pattern reference (mirror of drivers/storage/appleans/tools/ansraw/ansraw_pattern.h).

Usage: ans_raw_pattern.py <first_lba> <count>  -> prints the SHA-256 of the pattern range.
"""
import hashlib
import struct
import sys

BLOCK = 4096
SEED = 0x414E533030380000


def block(lba):
    base = SEED ^ ((lba << 12) & 0xFFFFFFFFFFFFFFFF)
    return struct.pack('<512Q', *[base ^ j for j in range(BLOCK // 8)])


def range_sha256(first, count):
    h = hashlib.sha256()
    for lba in range(first, first + count):
        h.update(block(lba))
    return h.hexdigest()


def zero_sha256(count):
    h = hashlib.sha256()
    z = bytes(BLOCK)
    for _ in range(count):
        h.update(z)
    return h.hexdigest()


if __name__ == '__main__':
    first, count = (int(v, 0) for v in sys.argv[1:3])
    print(range_sha256(first, count))
