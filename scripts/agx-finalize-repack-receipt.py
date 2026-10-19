#!/usr/bin/env python3
"""Finalize a separate receipt after repack; never modify signed package bytes."""
import argparse
import hashlib
import json
from pathlib import Path

ARTIFACTS = ('AppleAgxRenderAdmission.inf', 'AppleAgxRenderAdmission.sys',
             'AppleAgxRenderAdmissionUmd.dll', 'appleagxrenderadmission.cat',
             'AppleAgxOpenGL32.dll', 'AppleAgxRenderAdmissionUmd32.dll')
UNCHANGED = ('AppleAgxRenderAdmission.sys', 'AppleAgxRenderAdmissionUmd.dll')


def finalize(raw_receipt, package, output, script):
    if output.exists():
        raise ValueError('output already exists')
    raw = raw_receipt.read_bytes()
    receipt = json.loads(raw.decode('utf-8-sig'))
    if 'Repackage' in receipt:
        raise ValueError('expected original pre-repack receipt')
    built = dict(receipt['ArtifactSha256'])
    final = {name: hashlib.sha256((package / name).read_bytes()).hexdigest()
             for name in ARTIFACTS}
    for name in UNCHANGED:
        if final[name] != built[name]:
            raise ValueError('built binary changed: ' + name)
    if hashlib.sha256((package / 'arm64-build.log').read_bytes()).hexdigest() != receipt['BuildLogSha256']:
        raise ValueError('build log changed')
    receipt['Repackage'] = {'Script': script, 'BuiltArtifactSha256': built,
                          'OriginalReceiptSha256': hashlib.sha256(raw).hexdigest()}
    receipt['ArtifactSha256'] = final
    receipt['Verdict'] = 'R85_BUILD_REPACKAGED'
    with output.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2)
        stream.write('\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw-receipt', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--script', required=True)
    args = parser.parse_args()
    finalize(args.raw_receipt, args.package, args.output, args.script)
