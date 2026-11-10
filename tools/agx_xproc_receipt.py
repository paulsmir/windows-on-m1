#!/usr/bin/env python3
"""Decode Wom1CrossProcessPairsReceipt (C1/C2a/K1, ADMISSION_JOB_WINDOW_XPROC_RECEIPT v3 and v4).

Usage: agx_xproc_receipt.py <hex string | file with the REG_BINARY bytes>
The value is little-endian u32 words (render_job_window.h). v3 (60 words)
has the split of refusal 55: capability off, a job without a process, and
the refused kind pairs (running -> next). v4 (82 words) keeps those 60 and
appends the K1 class (Wom1CrossProcessRenderPairs, a render of another
process behind a plain render) as "render_pairs": considered / allowed /
kicked / fell back / failed, and which gate refused: one VM slot (6), the
manager-slot proof (58) by reason, or any other term. The general counters
count every cross-process pair, the K1 class included.
"""
import json
import struct
import sys
from pathlib import Path

CODES = 8
SIZE = 240
SIZES = {3: SIZE, 4: 328}
LEAD_REASONS = 5
KINDS = ('render', 'render+prelude', 'compute', 'other')
# ADMISSION_JOB_WINDOW_XPR_MANAGER_*: why the manager-slot proof failed
MANAGER_REASONS = ('none', 'unproven', 'unbound', 'all_bound', 'poisoned', 'pending')


def render_pairs(w):
    """The v4 words (60..81)."""
    def reason(value):
        return MANAGER_REASONS[value] if value < len(MANAGER_REASONS) else str(value)
    return dict(considered=w[60], allowed=w[61], kicked=w[62], fell_back=w[63], failed=w[64],
                refused_same_vm_slot=w[65],
                refused_manager_58={MANAGER_REASONS[i]: w[66 + i]
                                    for i in range(len(MANAGER_REASONS)) if w[66 + i]},
                refused_other=w[72],
                last_refusal=dict(code=w[73], fence=w[74], manager_reason=reason(w[75])),
                last_kicked=dict(fence=w[76], running_fence=w[77], running_vm_slot=w[78],
                                 next_vm_slot=w[79], running_manager_slot=w[80],
                                 next_manager_slot=w[81]))


def decode(data):
    if len(data) < SIZE:
        raise ValueError(f'receipt is {len(data)} bytes, expected {SIZE}')
    w = struct.unpack_from('<60I', data)
    if SIZES.get(w[0]) != w[1]:
        raise ValueError(f'unknown receipt version {w[0]} / bytes {w[1]}')
    if len(data) < w[1]:
        raise ValueError(f'receipt v{w[0]} is {len(data)} bytes, expected {w[1]}')
    out = decode_v3(w)
    if w[0] >= 4:
        out['render_pairs'] = render_pairs(struct.unpack_from('<82I', data))
    return out


def decode_v3(w):
    refusals = {str(w[7 + i]): w[15 + i] for i in range(CODES) if w[15 + i]}
    return dict(version=w[0], considered=w[2], allowed=w[3], kicked=w[4], fell_back=w[5],
                failed=w[6], refusals=refusals, refusal_other=w[23],
                last_kicked=dict(fence=w[24], running_fence=w[25], running_slot=w[26], next_slot=w[27]),
                last_refusal=dict(code=w[28], fence=w[29]),
                last_undone=dict(fence=w[30], line=w[31] & 0xffff, begin_refusal=(w[31] >> 16) & 0xff),
                lead=dict(considered=w[32], waived=w[33],
                          refused={str(i): w[34 + i] for i in range(LEAD_REASONS) if w[34 + i]},
                          last=dict(fence=w[39], expected=w[40], wait_value=w[41])),
                refused_55=dict(capability_off=w[42], no_process=w[43],
                                kinds={f'{KINDS[r]} -> {KINDS[n]}': w[44 + 4 * r + n]
                                       for r in range(4) for n in range(4) if w[44 + 4 * r + n]}))


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
