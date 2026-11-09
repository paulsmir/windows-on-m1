#!/usr/bin/env python3
"""UNMAP reaches the controller only through the validated Dataset Management path.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
native.c is not host-compiled. These are the wrapper-side invariants of
investigation/analysis/ANS-TRIM-UNMAP.md that the C suites cannot see: the parameter
list is resolved into the tag's private page before admission, only a data-out SRB with
a mapped buffer is accepted, the broker transport never advertises or submits
deallocate, and the dump instance never inherits the capability.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE = (ROOT / 'native.c').read_text()
DIRECT = (ROOT / 'direct.c').read_text()
start = NATIVE.index('BOOLEAN AppleAnsNativeStart(')
body = NATIVE[start:NATIVE.index('BOOLEAN AppleAnsNativeInterrupt(')]
resolve = body.index('ans_scsi_unmap_resolve(&a->Namespace,&plan,srb->DataBuffer,a->Pages[tag])')
assert resolve < body.index('ans_order_admit('), 'the list is resolved before the task is admitted'
assert body.index('for(tag=ANS_ADMIN_TAGS') < resolve, 'the range page belongs to the reserved tag'
guard = body[body.index('plan.nvme_opcode==ANS_NVME_DSM &&'):resolve]
for needle in ('SRB_FLAGS_DATA_OUT', 'SRB_FLAGS_DATA_IN', '!srb->DataBuffer',
               'srb->DataTransferLength!=x->Length'):
    assert needle in guard, f'UNMAP SRB shape check is missing {needle}'
assert re.search(r'plan\.nvme_opcode==ANS_NVME_DSM && a->Direct && !a->Namespace\.read_only', body), \
    'deallocate is submitted only on the writable direct transport'
init = NATIVE[NATIVE.index('BOOLEAN AppleAnsNativeInitialize('):start]
assert 'a->Namespace.deallocate = 0;' in init, 'the broker transport never advertises UNMAP'
dump = DIRECT[DIRECT.index('BOOLEAN AppleAnsDirectDumpInitialize('):DIRECT.index('static VOID direct_retire(PAPPLE_ANS_ADAPTER a, UCHAR status);')]
assert 'a->Namespace = (struct ans_namespace){0};' in dump and 'deallocate' not in dump, \
    'the dump instance does not advertise UNMAP'
print('PASS unmap_wiring')
