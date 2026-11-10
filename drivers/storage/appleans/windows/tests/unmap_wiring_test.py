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
# Qualification gate (ANS-TRIM-UNMAP.md): policy sits between the resolver and admission, the
# request builder gets an independent mode check, and the policy is read where the registry
# may be read (FindAdapter) and never by the dump instance.
gate = body.index('ans_trim_gate(&a->Trim,&plan,a->Pages[tag])')
assert resolve < gate < body.index('ans_order_admit('), 'the policy gate follows the resolver'
assert re.search(r'plan\.nvme_opcode!=ANS_NVME_DSM \|\| a->Trim\.config\.mode==ANS_TRIM_DEALLOCATE',
                 body), 'the request builder refuses deallocate outside DEALLOCATE mode'
DRIVER = (ROOT / 'driver.c').read_text()
find = DRIVER[DRIVER.index('ULONG AppleAnsFindAdapter('):DRIVER.index('BOOLEAN AppleAnsInitialize(')]
assert find.index('if (dump) return dump_find(a, ConfigInfo);') < find.index('unmap_config(a);'), \
    'the dump instance keeps the zeroed policy (ANS_TRIM_OFF)'
assert 'ans_trim_config_make(have, value[0], value[1], value[2])' in DRIVER
assert 'if (a->Trim.config.mode == ANS_TRIM_OFF) a->Namespace.deallocate = 0;' in DIRECT
assert 'Trim' not in dump, 'the dump instance has no UNMAP policy'
assert 'ans_trim_done(&a->Trim' in DIRECT and 'ans_trim_queued(&a->Trim)' in NATIVE
print('PASS unmap_policy_wiring')
# Missed-interrupt recovery (ANSQ-T4): the periodic timer reaps an overdue CQE under the same
# lock and through the same drain as the ISR; the dump instance (no timer) is unaffected.
timer = DIRECT[DIRECT.index('VOID AppleAnsDirectTimer('):DIRECT.index('static VOID direct_retire(PAPPLE_ANS_ADAPTER a, UCHAR status)\n{')]
lock, over, drain, unlock = (timer.index(x) for x in ('StorPortAcquireSpinLock(a, InterruptLock',
    'ansd_overdue(&a->Ansd, 1)', '(void)AppleAnsDirectInterrupt(a);', 'StorPortReleaseSpinLock(a, &lock)'))
assert lock < over < drain < unlock, 'overdue completions are reaped under InterruptLock'
print('PASS missed_interrupt_wiring')
# Reject receipt: every refusal of an unknown SRB function, foreign SRB_IO_CONTROL or unknown CDB
# opcode is recorded before the request is completed.
for needle in ('ans_reject_note(&a->Rejects,ANS_REJECT_IO_CONTROL,', 'ans_reject_note(&a->Rejects,ANS_REJECT_CDB,x->Cdb[0])',
               'ans_reject_note(&a->Rejects,ANS_REJECT_SRB_FUNCTION,srb->Function)',
               'ans_reject_ioctl_reply(srb->DataBuffer,srb->DataTransferLength,&a->Rejects)'):
    assert needle in NATIVE, needle
print('PASS reject_receipt_wiring')
