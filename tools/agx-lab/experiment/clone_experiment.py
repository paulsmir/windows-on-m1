#!/usr/bin/env python3
"""Derive the next experiment's guest and launch scripts from the previous one.

Usage: clone_experiment.py OLD_NUM OLD_DIR NEW_NUM NEW_DIR

Needs NEW_DIR/kmd-build-receipt.json and NEW_DIR/package/* (prepare.sh puts
them there). Copies boot/stage/ordered-restart/full-owner/verify-launch,
watch-dwm.ps1, the cleanup directory and pinned.cer from OLD_DIR, rewriting the
experiment number, directory, driver version and package hashes; refreshes
the cleanup durability hashes; prints any leftover reference to the old number.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from labenv import PACKAGE, experiments  # noqa: E402

so, sd, no, nd = sys.argv[1:5]
S = experiments() / sd
D = experiments() / nd


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


r = json.load(open(D / 'kmd-build-receipt.json', encoding='utf-8-sig'))
for n, h in r['ArtifactSha256'].items():
    assert sha(D / n) == h, n
old = {n: sha(S / 'package' / n) for n in PACKAGE}
new = {n: sha(D / 'package' / n) for n in PACKAGE}


def sub(t):
    t = t.replace(sd, nd).replace('EXP' + so, 'EXP' + no).replace('cleanup' + so, 'cleanup' + no)
    for a in ['boot', 'stage', 'ordered-restart', 'verify-launch', 'full-owner']:
        t = t.replace(a + so, a + no)
    t = t.replace('30.0.%s.0' % so, '30.0.%s.0' % no)
    for n in PACKAGE:
        t = t.replace(old[n], new[n]).replace(old[n].upper(), new[n].upper())
    return t


for f in ['boot%s.ps1', 'stage%s.ps1', 'ordered-restart%s.ps1', 'full-owner%s.sh', 'verify-launch%s.py', 'watch-dwm.ps1']:
    src = S / (f % so if '%s' in f else f)
    n = f % no if '%s' in f else f
    (D / n).write_text(sub(src.read_text()))
    os.chmod(D / n, src.stat().st_mode)
(D / ('cleanup' + no)).mkdir(exist_ok=True)
for f in (S / ('cleanup' + so)).iterdir():
    (D / ('cleanup' + no) / f.name).write_text(sub(f.read_text()))
shutil.copy2(S / 'pinned.cer', D / 'pinned.cer')
dur = json.loads((D / ('cleanup' + no) / 'durability.json').read_text())
dur['Scripts'] = {k: sha(D / ('cleanup' + no) / k.split('\\')[-1]) for k in dur['Scripts']}
(D / ('cleanup' + no) / 'durability.json').write_text(json.dumps(dur, indent=2))
left = subprocess.run(
    f"grep -rln 'EXP{so}\\|cleanup{so}\\|30.0.{so}.0' {D}/*.ps1 {D}/*.sh {D}/*.py {D}/cleanup{no}",
    shell=True, capture_output=True, text=True).stdout
print('leftovers:', left or 'none', {n: new[n][:8] for n in PACKAGE})
