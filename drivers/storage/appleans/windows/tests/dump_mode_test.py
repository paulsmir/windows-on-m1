#!/usr/bin/env python3
"""Boot-start and crash-dump contract for native Windows boot (ANS010, ANS-DUMP-MODE DUMP1).

Invariants:
- The crash-dump/hibernation copy of the miniport (DumpMode != 0) refuses in HwFindAdapter
  before it parses resources, maps anything or reaches a transport. Until the polled dump
  path exists (DUMP2), a bugcheck must restart without the dump image touching the SSD.
- The only thing the dump copy may do first is validate the live instance's hand-over record
  and write a receipt into it; it never dereferences an unvalidated record and uses no
  registry, pool, DPC, timer or MMIO API.
- The live instance hands the record out only through SRB_FUNCTION_DUMP_POINTERS, sealed, and
  only when the feature bit that makes Storport send that SRB is set.
- The INF stays boot-start in the SCSI Miniport group, or the installed C: cannot boot.
"""
from pathlib import Path
import re
WIN = Path(__file__).resolve().parents[1]
src = (WIN / 'driver.c').read_text()
start = src.index('ULONG AppleAnsFindAdapter(')
body = src[start:src.index('\n}\n', start)]
guard = re.search(r'if \(ConfigInfo->DumpMode != 0\) \{([^}]*)return SP_RETURN_NOT_FOUND;', body)
assert guard, 'DumpMode refusal missing from AppleAnsFindAdapter'
assert re.sub(r'\s+', '', guard.group(1)) == \
    'dump_receipt(ConfigInfo);a->Stage=AppleAnsStageResourcesRejected;', guard.group(1)
length = body.index('ConfigInfo->Length < sizeof(*ConfigInfo)')
assert length < guard.start(), 'DumpMode read before the structure length is checked'
for later in ('AccessRanges', 'struct ans_resources', 'ans_resources_', 'AppleAnsNative',
              'AppleAnsDirect', 'Wire.'):
    i = body.find(later, body.index('RtlZeroMemory'))
    assert i < 0 or guard.end() < i, later
rs = src.index('static VOID dump_receipt(')
receipt = src[rs:src.index('\n}\n', rs)]
valid = receipt.index('if (!ans_dump_context_valid(c, APPLE_ANS_DIRECT_BYTES)) return;')
for use in ('c->', 'ans_dump_context_receipt('):
    i = receipt.find(use)
    assert i < 0 or valid < i, use
assert 'ans_dump_context_receipt(' in receipt
for banned in ('StorPort', 'Registry', 'ExAllocate', 'Dpc', 'Timer', 'Read' + 'Register',
               'Write' + 'Register', 'ansd_', 'Wire'):
    assert banned not in receipt, banned
assert re.search(r'FeatureSupport = [^;]*STOR_FEATURE_DUMP_POINTERS', src)
assert 'STOR_FEATURE_DUMP_RESUME_CAPABLE' not in src  # hibernation resume stays unsupported
native = (WIN / 'native.c').read_text()
ps = native.index('static BOOLEAN dump_pointers(')
pointers = native[ps:native.index('\n}\n', ps)]
seal = pointers.index('ans_dump_context_seal(')
refuse = pointers.index('if(!sealed) { AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE; }')
publish = pointers.index('p->MiniportPrivateDumpData=&a->DumpContext;')
assert seal < refuse < publish, 'record published before it is sealed'
assert 'a->Direct && a->Ready && a->Common' in pointers[:seal]
assert native.count('MiniportPrivateDumpData=') == 1
assert 'if(srb->Function==SRB_FUNCTION_DUMP_POINTERS) return dump_pointers(a,srb);' in native
inf = (WIN / 'AppleAns.inf').read_text()
for line in ('ServiceType=1', 'StartType=0', 'LoadOrderGroup=SCSI Miniport', 'ErrorControl=1'):
    assert re.search(r'^' + re.escape(line) + r'\s*$', inf, re.M), line
print('AppleAns boot-start/dump-mode contract PASS')
