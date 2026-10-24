#!/usr/bin/env python3
"""Decode EXP1929 UMD allocation-refusal ownership snapshots from saved traces.

Logical spans, not working set or VidMm physical residency. Categories overlap;
never add canonical/private/merge to pool reserved/held/committed. Transitional, copy-held
and unknown-sized slots are excluded explicitly. The in-progress request is
reported separately. Truncated or incompatible snapshots are errors, not zeros.
"""
import argparse
import json
from pathlib import Path
import re

ROW = re.compile(r'^(measure-memory-(?:refusal|storage)) hr=(0x[0-9a-fA-F]{8}) '
                 r'pid=(\d+) tid=\d+ session=\d+ qpc=\d+ (.*)$')
CATEGORIES = {1: 'canonical', 2: 'private_staging', 3: 'merge_baseline'}
STAGES = {1: 'allocate-callback', 2: 'private-staging'}


def wide(words, index):
    return words[index] | words[index + 1] << 32


def decode(text):
    snapshots = {}
    for number, line in enumerate(text.splitlines(), 1):
        if not line.startswith('measure-memory-'):
            continue
        match = ROW.fullmatch(line)
        if not match:
            raise ValueError(f'line {number}: malformed memory receipt')
        name, status, pid, payload = match.groups()
        fields = payload.split()
        if len(fields) != 16 or any(not re.fullmatch(r'[0-9a-fA-F]{8}', x) for x in fields):
            raise ValueError(f'line {number}: expected 16 dwords')
        words = [int(x, 16) for x in fields]
        if words[0] != 1 or not 1 <= words[5] <= 4 or not int(status, 16) & 0x80000000:
            raise ValueError(f'line {number}: unsupported version, sequence or success status')
        key = (int(pid), wide(words, 1), wide(words, 3), words[5])
        record = snapshots.setdefault(key, {'status': status.lower(), 'header': None, 'storage': {}})
        if record['status'] != status.lower():
            raise ValueError(f'line {number}: inconsistent snapshot status')
        if name == 'measure-memory-refusal':
            if record['header'] is not None:
                raise ValueError(f'line {number}: duplicate snapshot header')
            if words[10] not in STAGES or words[11] & ~7 or words[13]+words[14]+words[15] > words[12]:
                raise ValueError(f'line {number}: incompatible refusal metadata')
            record['header'] = words
        else:
            category = CATEGORIES.get(words[6])
            if category is None or category in record['storage']:
                raise ValueError(f'line {number}: unknown or duplicate storage category')
            if words[7] != words[10]+words[13] or wide(words, 8) != wide(words, 11)+wide(words, 14):
                raise ValueError(f'line {number}: inconsistent storage subsets')
            record['storage'][category] = dict(slots=words[7], bytes=wide(words, 8),
                mapped_slots=words[10], mapped_bytes=wide(words, 11),
                unmapped_slots=words[13], unmapped_bytes=wide(words, 14))
    if not snapshots:
        raise ValueError('no memory refusal snapshots')
    result = []
    for (pid, cookie, token, sequence), record in sorted(snapshots.items()):
        h = record['header']
        if h is None or set(record['storage']) != set(CATEGORIES.values()):
            raise ValueError('incomplete snapshot: header and all three storage categories required')
        if any(s['slots'] > h[12]-h[13]-h[14]-h[15] for s in record['storage'].values()):
            raise ValueError('storage count exceeds eligible slots')
        result.append(dict(pid=pid, owner_cookie=hex(cookie), request_token=hex(token),
            sequence=sequence, status=record['status'],
            request=dict(class_id=h[6], flags=hex(h[7]), bytes=wide(h, 8), stage=STAGES[h[10]],
                         pending=[n for bit, n in enumerate(('canonical','private_staging','borrowed_staging'))
                                  if h[11] & (1 << bit)]),
            active_slots=h[12], transitional_slots_excluded=h[13], unknown_size_slots_excluded=h[14],
            copy_held_slots_excluded=h[15],
            storage=record['storage']))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    args = parser.parse_args()
    try:
        records = decode(args.trace.read_text(encoding='utf-8-sig'))
    except (OSError, UnicodeError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(records, indent=2))


if __name__ == '__main__':
    main()
