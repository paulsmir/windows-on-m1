#!/usr/bin/env python3
"""Decode Wom1G3CopyQueryFailure REG_BINARY (v1/16, v2/144, v3/168 bytes).

Input: raw .bin, a collector state.json containing Receipts, or devnode.reg.
Suggested collector export: Wom1G3CopyQueryFailure.bin. No live registry access.
"""
import argparse
import base64
import json
from pathlib import Path
import re
import struct

NAME = 'Wom1G3CopyQueryFailure'
REASONS = ('none', 'no-root', 'no-table', 'leaf-absent',
           'leaf-not-published', 'tail-short')


def decode(data):
    if len(data) < 16:
        raise ValueError('truncated receipt header')
    version, size, predicate, status = struct.unpack_from('<4I', data)
    if version not in (1, 2, 3) or size != {1: 16, 2: 144, 3: 168}[version] or len(data) != size:
        raise ValueError('unsupported version or incorrect receipt length')
    result = dict(version=version, bytes=size, predicate=predicate, status=hex(status))
    if version == 1:
        result['graph_details_available'] = False
        return result
    values = struct.unpack_from('<12I12Q', data)
    keys = ('flags', 'write', 'missing_level', 'missing_index', 'missing_reason',
            'component_reason', 'process_set_root_count', 'context_set_root_count',
            'graph_root_ipa', 'bootstrap_ipa', 'process_last_set_root_ipa',
            'context_last_set_root_ipa', 'query_va', 'query_bytes', 'missing_va',
            'process_generation', 'mapping_generation', 'context_root_ipa',
            'process_id', 'context_token')
    result.update(zip(keys, values[4:]))
    if result['flags'] & ~(255 if version == 3 else 63) or result['write'] not in (0, 1):
        raise ValueError('unknown flags/access value')
    for key in ('missing_reason', 'component_reason'):
        if result[key] >= len(REASONS):
            raise ValueError('unknown walk reason')
        result[key] = REASONS[result[key]]
    for key, bit in (('captured_under_lock', 1), ('process_available', 2),
                     ('context_available', 4), ('request_available', 8),
                     ('range_validated', 16), ('root_is_bootstrap', 32)):
        result[key] = bool(result['flags'] & bit)
    result['graph_details_available'] = result['captured_under_lock'] and result['process_available']
    if version == 3:
        handle, identity, allocation_bytes = struct.unpack_from('<3Q', data, 144)
        result.update(request_allocation_handle=hex(handle),
                      canonical_allocation_identity=hex(identity),
                      canonical_allocation_bytes=allocation_bytes,
                      resident_group_available=bool(result['flags'] & 64),
                      canonical_allocation_available=bool(result['flags'] & 128))
    result['write'] = bool(result['write'])
    for key in ('missing_level', 'missing_index'):
        if result[key] == 0xffffffff:
            result[key] = None
    for key in ('graph_root_ipa', 'bootstrap_ipa', 'process_last_set_root_ipa',
                'context_last_set_root_ipa', 'query_va', 'missing_va',
                'context_root_ipa', 'context_token'):
        result[key] = hex(result[key])
    return result


def read_receipt(path):
    data = path.read_bytes()
    if path.suffix.lower() == '.json':
        saved = json.loads(data)
        return base64.b64decode(saved.get('Receipts', saved)[NAME], validate=True)
    if path.suffix.lower() == '.reg':
        text = data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
        text = re.sub(r'\\\r?\n\s*', '', text)
        matches = re.findall(r'^"' + NAME + r'"=hex:([0-9a-fA-F, ]+)\s*$', text, re.M)
        if len(matches) != 1:
            raise ValueError('expected exactly one matching REG_BINARY value')
        return bytes.fromhex(matches[0].replace(',', ''))
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('receipt', type=Path)
    args = parser.parse_args()
    try:
        result = decode(read_receipt(args.receipt))
    except (ValueError, KeyError, TypeError, OSError) as exc:
        parser.exit(2, 'receipt error: ' + str(exc) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
