#!/usr/bin/env python3
"""Decode Wom1SubmitPath1135 (ADMISSION_SUBMIT_PATH, render_admission.h).

Usage: agx_submit_path.py QPC_HZ <snapshot|hex> [<snapshot|hex> ...]
A snapshot is a kmd-receipts file with a line "Wom1SubmitPath1135 <hex>"; a
bare hex string works too. With several inputs the deltas between consecutive
ones are printed as well. v1 (80 bytes) and v2 (112 bytes) are read; v2 adds
why a submission was not admissible (the first failing predicate in code
order), the context-limit refusals whose oldest fence was already reported
to dxgkrnl, and the waits that ended on the deferred path
(Wom1SubmitRequeue).
"""
import json
import struct
import sys
from pathlib import Path

NAME = b'Wom1SubmitPath1135'
WAITS = ('backend submitted', 'worker finishing', 'slot occupied', 'other')
REASONS = ('irql', 'not queueable', 'context limit', 'pending capacity', 'idle', 'faulted')
V1_BYTES, V2_BYTES = 80, 112


def decode(data):
    if len(data) < V1_BYTES:
        raise ValueError(f'receipt is {len(data)} bytes, expected at least {V1_BYTES}')
    version, size, immediate, deferred, binds, _ = struct.unpack_from('<IIiiii', data, 0)
    out = dict(version=version, bytes=size, immediate=immediate, deferred=deferred, binds=binds,
               waits=list(struct.unpack_from('<4i', data, 24)),
               wait_qpc=list(struct.unpack_from('<4q', data, 40)),
               max_wait_qpc=struct.unpack_from('<q', data, 72)[0])
    if version >= 2:
        if size != V2_BYTES or len(data) < V2_BYTES:
            raise ValueError(f'v{version} receipt of {size}/{len(data)} bytes, expected {V2_BYTES}')
        out['not_admissible'] = dict(zip(REASONS, struct.unpack_from('<6i', data, 80)))
        out['context_limit_reported'], out['requeued'] = struct.unpack_from('<2i', data, 104)
    elif version != 1 or size != V1_BYTES:
        raise ValueError(f'unknown receipt version {version} / bytes {size}')
    return out


def load(source):
    path = Path(source)
    if path.is_file():
        for line in path.read_bytes().splitlines():
            if line.startswith(NAME + b' '):
                return bytes.fromhex(line.split()[1].decode())
        raise ValueError(f'{source}: no {NAME.decode()} line')
    return bytes.fromhex(''.join(source.replace(',', ' ').split()))


def delta(new, old, hz):
    waits = [a - b for a, b in zip(new['waits'], old['waits'])]
    qpc = [a - b for a, b in zip(new['wait_qpc'], old['wait_qpc'])]
    out = dict(immediate=new['immediate'] - old['immediate'], deferred=new['deferred'] - old['deferred'],
               binds=new['binds'] - old['binds'], waits=dict(zip(WAITS, waits)),
               mean_wait_us={name: round(q * 1e6 / hz / w) if w else 0
                             for name, q, w in zip(WAITS, qpc, waits)})
    if 'not_admissible' in new and 'not_admissible' in old:
        out['not_admissible'] = {k: new['not_admissible'][k] - old['not_admissible'][k] for k in REASONS}
        out['context_limit_reported'] = new['context_limit_reported'] - old['context_limit_reported']
        out['requeued'] = new['requeued'] - old['requeued']
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    hz = float(argv[1])
    previous = None
    for source in argv[2:]:
        current = decode(load(source))
        shown = dict(current, waits=dict(zip(WAITS, current['waits'])),
                     max_wait_us=round(current['max_wait_qpc'] * 1e6 / hz))
        print(json.dumps({source.split('/')[-1][:40]: shown}, indent=1))
        if previous is not None:
            print(json.dumps({'delta': delta(current, previous, hz)}, indent=1))
        previous = current
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
