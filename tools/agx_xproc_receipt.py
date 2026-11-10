#!/usr/bin/env python3
"""Decode Wom1CrossProcessPairsReceipt (C1, ADMISSION_JOB_WINDOW_XPROC_RECEIPT v1).

Usage: agx_xproc_receipt.py <hex string | file with the REG_BINARY bytes>
The value is 32 little-endian u32 (render_job_window.h).
"""
import json
import struct
import sys
from pathlib import Path

CODES = 8
SIZE = 128


def decode(data):
    if len(data) < SIZE:
        raise ValueError(f'receipt is {len(data)} bytes, expected {SIZE}')
    w = struct.unpack_from('<32I', data)
    if w[0] != 1 or w[1] != SIZE:
        raise ValueError(f'unknown receipt version {w[0]} / bytes {w[1]}')
    refusals = {str(w[7 + i]): w[15 + i] for i in range(CODES) if w[15 + i]}
    return dict(version=w[0], considered=w[2], allowed=w[3], kicked=w[4], fell_back=w[5],
                failed=w[6], refusals=refusals, refusal_other=w[23],
                last_kicked=dict(fence=w[24], running_fence=w[25], running_slot=w[26], next_slot=w[27]),
                last_refusal=dict(code=w[28], fence=w[29]),
                last_undone=dict(fence=w[30], line=w[31] & 0xffff, begin_refusal=(w[31] >> 16) & 0xff))


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    source = argv[1]
    path = Path(source)
    if path.is_file():
        raw = path.read_bytes()
        try:
            data = bytes.fromhex(''.join(raw.decode('ascii').replace(',', ' ').split()))
        except (UnicodeDecodeError, ValueError):
            data = raw
    else:
        data = bytes.fromhex(''.join(source.replace(',', ' ').split()))
    print(json.dumps(decode(data), indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
