#!/usr/bin/env python3
"""Bounce wiring (wrapper code is not host-compiled).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A misaligned data address reaching the controller, a bounce buffer shared or leaked, a write
submitted before its data was copied, or a read completed before its data was copied back.
"""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
DRIVER = (ROOT / 'driver.c').read_text()
NATIVE = (ROOT / 'native.c').read_text()
DIRECT = (ROOT / 'direct.c').read_text()
ADAPTER = (ROOT / 'adapter.h').read_text()

build = DRIVER[DRIVER.index('BOOLEAN AppleAnsBuildIo('):DRIVER.index('BOOLEAN AppleAnsStartIo(')]
need = build.index('ans_bounce_needed(')
assert need < build.index('ans_sg_to_prps_ex('), 'the alignment decision precedes any PRP built from the SG list'
branch = build[need:build.index('/* Direct mode accepts the memory manager')]
assert 'a->Direct && !a->DumpMode' in build[need-120:need+10], 'dump instance and broker transport keep their path'
assert 'StorPortGetSystemAddress(DeviceExtension,Srb,&va)==STOR_STATUS_SUCCESS' in branch
assert 'x->BounceDefer=TRUE' in branch and 'return TRUE;' in branch
assert 'ans_bounce_acquire' not in build, 'no pool state is touched outside the interrupt lock'

start = NATIVE[NATIVE.index('BOOLEAN AppleAnsNativeStart('):NATIVE.index('BOOLEAN AppleAnsNativeInterrupt(')]
acq = start.index('ans_bounce_acquire(&a->Bounce,plan.nvme_opcode==1)')
copy = start.index('RtlCopyMemory(a->BounceBuffer[index],x->BounceVa,plan.transfer_bytes)')
prps = start.index('ans_bounce_prps(a->BouncePa[x->BounceIndex-1],plan.transfer_bytes,&translated)')
admit = start.index('ans_order_admit(')
build_req = start.index('ans_io_request(&plan,prps,')
assert acq < copy < admit < prps < build_req, 'claim, copy the write, then build the request from bounce pages'
assert 'AppleAnsComplete(a,srb,SRB_STATUS_BUSY,0);return TRUE;' in start[acq:copy], 'no buffer: retry, not an error'
assert start.index('x->BounceIndex') > start.index('plan=ans_scsi_prepare('), 'only planned READ/WRITE commands bounce'

comp = NATIVE[NATIVE.index('VOID AppleAnsComplete('):NATIVE.index('static VOID sense(')]
back = comp.index('RtlCopyMemory(bx->BounceVa, a->BounceBuffer[index], length)')
rel = comp.index('ans_bounce_release(&a->Bounce, (int)index)')
done = comp.index('StorPortNotification(RequestComplete, a, srb)')
assert back < rel < done, 'copy the read back and release before the SRB is completed'
assert 'status == SRB_STATUS_SUCCESS && (srb->SrbFlags & SRB_FLAGS_DATA_IN)' in comp[:back]
assert NATIVE.count('StorPortNotification(RequestComplete') == 1, 'one completion point'
assert 'RequestComplete' not in DIRECT and 'RequestComplete' not in DRIVER

carve = DIRECT[DIRECT.index('static BOOLEAN direct_carve('):DIRECT.index('BOOLEAN AppleAnsDirectResources(')]
assert 'a->BouncePa[i][p]' in carve and 'ANS_BOUNCE_COUNT * ANS_BOUNCE_BYTES' in ADAPTER
print('PASS bounce_wiring')
