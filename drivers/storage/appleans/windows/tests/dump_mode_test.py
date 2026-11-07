#!/usr/bin/env python3
"""Boot-start contract for native Windows boot (ANS010).

Invariant: the crash-dump/hibernation copy of the miniport (DumpMode != 0) must refuse in
HwFindAdapter before it parses resources or reaches the EL2 broker; otherwise a native
bugcheck would re-claim the broker owned by the live instance at HIGH_LEVEL. The INF must
stay boot-start in the SCSI Miniport group, or the installed C: cannot boot through it.
"""
from pathlib import Path
import re
WIN = Path(__file__).resolve().parents[1]
src = (WIN / 'driver.c').read_text()
start = src.index('ULONG AppleAnsFindAdapter(')
body = src[start:src.index('\n}\n', start)]
guard = re.search(r'if \(ConfigInfo->DumpMode != 0\) \{[^}]*return SP_RETURN_NOT_FOUND;', body)
assert guard, 'DumpMode refusal missing from AppleAnsFindAdapter'
for later in ('struct ans_resources', 'ans_resources_', 'AppleAnsNative', 'Wire.'):
    i = body.find(later, body.index('RtlZeroMemory'))
    assert i < 0 or guard.start() < i, later
inf = (WIN / 'AppleAns.inf').read_text()
for line in ('ServiceType=1', 'StartType=0', 'LoadOrderGroup=SCSI Miniport', 'ErrorControl=1'):
    assert re.search(r'^' + re.escape(line) + r'\s*$', inf, re.M), line
print('AppleAns boot-start/dump-mode contract PASS')
