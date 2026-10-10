#!/usr/bin/env python3
"""Write the guest manifest and the host hardware manifest of a new experiment.

Usage: write_manifests.py OLD_NUM OLD_DIR NEW_NUM NEW_DIR "<purpose>"

manifest.json (read by the guest stage/cleanup scripts) gets the experiment,
driver version, source commit, the boot identity just recorded on the guest
(evidence/boot-identity.json) and the package hashes. hardware-manifest.json
(checked by verify-launch<NUM>.py before every launch) lists every launch
artifact with its SHA-256.
"""
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from labenv import experiments  # noqa: E402

so, sd, no, nd, purpose = sys.argv[1:6]
S = experiments() / sd
D = experiments() / nd


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


m = json.loads((S / 'manifest.json').read_text())
b = json.loads((D / 'evidence/boot-identity.json').read_text())
r = json.load(open(D / 'kmd-build-receipt.json', encoding='utf-8-sig'))
m.update(Experiment='EXP' + no, Version='30.0.%s.0' % no, SourceCommit=r['SourceCommit'],
         BootLastBoot=b['BootLastBoot'], BootEventRecordId=b['BootEventRecordId'],
         BootEventStart=b['BootEventStart'],
         Package={n: sha(D / 'package' / n) for n in m['Package']}, Purpose=purpose)
(D / 'manifest.json').write_text(json.dumps(m, indent=2))
hw = json.loads((S / 'hardware-manifest.json').read_text())
arts = {}
for p in hw['Artifacts']:
    q = p.replace(sd, nd).replace('cleanup' + so, 'cleanup' + no)
    for a in ['boot', 'stage', 'ordered-restart', 'verify-launch', 'full-owner']:
        q = q.replace(a + so, a + no)
    if Path(q).exists():
        arts[q] = sha(q)
hw.update(Experiment='EXP' + no, SourceCommit=r['SourceCommit'], Artifacts=arts)
(D / 'hardware-manifest.json').write_text(json.dumps(hw, indent=2))
print(len(arts), 'artifacts; manifest', sha(D / 'manifest.json')[:8], 'hw', sha(D / 'hardware-manifest.json')[:8])
