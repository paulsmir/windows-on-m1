#!/usr/bin/env python3
"""Standard install contract: permanent dedicated signer (never removed, unlike the AGX experiment
signer), signer/hash bound to the package, ImagePath bound to the exact store copy, records and
devnode only when missing (ANS010-R2/R5), and the dual-boot guard (ANS010-R6/ANS012-R1)."""
from pathlib import Path
src = (Path(__file__).resolve().parents[2] / 'tools/install-appleans.ps1').read_text()
for needle in ("'Root', 'TrustedPublisher'", '$st.Add($cert)', 'signed by', 'AppleAns.sys hash mismatch',
               'pnputil.exe /add-driver', 'SubKeyCount -ge 3', "Service -ne 'AppleAns'", '/RU SYSTEM',
               "'ans-dual-boot.ps1') install", 'driver store copies of the requested AppleAns.sys'):
    assert needle in src, needle
assert '.Remove(' not in src, 'the AppleAns signer must stay trusted permanently'
assert src.index('$records.Count') < src.index('reg import $work\\records.reg')
print('AppleAns standard install contract PASS')
