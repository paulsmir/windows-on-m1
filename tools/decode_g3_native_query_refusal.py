#!/usr/bin/env python3
"""Decode Wom1G3NativeQueryRefusal (EXP2206 receipt-only) from a KMD receipt
dump (`Wom1G3NativeQueryRefusal <hex>` lines, as kmd-receipts-*.txt).

Layout and code names come from
drivers/apple-agx/shared/include/apple_agx_g3_native_query_refusal.h.
"""
import re
import struct
import sys
from pathlib import Path

HEADER = Path(__file__).resolve().parents[1] / \
    'drivers/apple-agx/shared/include/apple_agx_g3_native_query_refusal.h'
RECORD = '<8I Q 3Q 3Q 4I 4Q 4I'
FIELDS = ('Code Status Flags Sequence OsProcessId ProcessFlags ContextFlags OpenedFlags Qpc '
          'Device Context KmdProcess QueryAllocation QueryGpuVa QueryAllocationBytes '
          'QueryVersion QueryReserved QueryWrite QueryBytes ContextDevice OpenedDevice '
          'DescriptionSize PidProcess OpenedRuntimeAllocation ProcessContexts '
          'ForeignOsProcessId Reserved').split()


def code_names():
    text = HEADER.read_text()
    names = {}
    for name, value in re.findall(r'#define APPLE_AGX_G3_NQ_([A-Z0-9_]+) (\d+)u', text):
        if name.startswith(('REFUSAL', 'LAST', 'FLAG_')) or not 1 <= int(value) <= 67:
            continue
        if name.endswith(('_BIT', '_MATCH', '_FOUND', '_FOREIGN')):
            continue
        names.setdefault(int(value), name)
    return names


def decode(blob):
    names = code_names()
    version, size, total, dropped = struct.unpack_from('<4I', blob, 0)
    counts = struct.unpack_from('<128I', blob, 16)
    rsize = struct.calcsize(RECORD)
    print('version %d bytes %d total %d dropped %d' % (version, size, total, dropped))
    for code, n in enumerate(counts):
        if n:
            print('  code %2d %-24s %d' % (code, names.get(code, '?'), n))
    for i, label in enumerate(('First', 'Last')):
        rec = dict(zip(FIELDS, struct.unpack_from(RECORD, blob, 16 + 512 + i * rsize)))
        rec['Name'] = names.get(rec['Code'], '?')
        print(label + ':')
        for key in ['Name'] + FIELDS:
            value = rec[key]
            print('  %-24s %s' % (key, hex(value) if isinstance(value, int) and key not in (
                'Code', 'Sequence', 'OsProcessId', 'ProcessContexts', 'ForeignOsProcessId',
                'QueryVersion', 'QueryWrite', 'QueryBytes') else value))


def main(path):
    text = Path(path).read_bytes().decode('latin1')
    match = re.search(r'^Wom1G3NativeQueryRefusal\s+(?:REG_BINARY\s+)?([0-9a-fA-F]+)', text, re.M)
    if not match:
        print('Wom1G3NativeQueryRefusal absent (no refusal since start)')
        return 1
    decode(bytes.fromhex(match.group(1)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
