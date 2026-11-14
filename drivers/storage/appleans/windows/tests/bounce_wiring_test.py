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
assert build.index('ans_sg_to_prps_ex(') < need, 'the direct PRPs are always built first: every request keeps a direct path'
branch = build[need:]
assert 'a->Direct && !a->DumpMode' in build[need-120:need+10], 'dump instance and broker transport keep their path'
assert 'a->Bounce.enabled && a->Bounce.count && ans_bounce_size_ok(x->Length)' in branch, 'off or no pool: nothing is mapped or diverted'
assert 'StorPortGetSystemAddress(DeviceExtension,Srb,&va)==STOR_STATUS_SUCCESS' in branch
assert 'MmGetPhysicalAddress(va).QuadPart ==' in branch and 'x->BounceMismatch=TRUE' in branch, \
    'the mapping is checked against the first SG address'
assert 'x->BounceUnmapped=TRUE' in branch
assert 'ans_bounce_acquire' not in build, 'no pool state is touched outside the interrupt lock'
assert 'SRB_STATUS' not in branch, 'BuildIo never decides the outcome of a bounce'

start = NATIVE[NATIVE.index('BOOLEAN AppleAnsNativeStart('):NATIVE.index('BOOLEAN AppleAnsNativeInterrupt(')]
acq = start.index('ans_bounce_acquire(&a->Bounce,plan.nvme_opcode==1)')
copy = start.index('RtlCopyMemory(a->BounceBuffer[index],x->BounceVa,plan.transfer_bytes)')
prps = start.index('ans_bounce_prps(a->BouncePa[x->BounceIndex-1],plan.transfer_bytes,&translated)')
admit = start.index('ans_order_admit(')
build_req = start.index('ans_io_request(&plan,prps,')
assert acq < copy < admit < prps < build_req, 'claim, copy the write, then build the request from bounce pages'
bounce = start[start.index('if (x->BounceMismatch)'):admit]
assert 'SRB_STATUS_ERROR' not in bounce and 'SRB_STATUS_INVALID_REQUEST' not in bounce, \
    'a request that cannot be bounced is never failed for that reason'
assert 'a->Bounce.direct_mismatch++' in bounce and 'a->Bounce.direct_unmapped++' in bounce
assert 'if (ans_bounce_wait(&a->Bounce)) {\n                    (void)StorPortDeviceBusy(a,0,0,0,1);\n' \
       '                    AppleAnsComplete(a,srb,SRB_STATUS_BUSY,0);return TRUE;' in bounce, \
    'no buffer: pause the LU and retry while a release is expected, otherwise direct'
assert start.index('x->BounceIndex') > start.index('plan=ans_scsi_prepare('), 'only planned READ/WRITE commands bounce'

comp = NATIVE[NATIVE.index('VOID AppleAnsComplete('):NATIVE.index('static VOID sense(')]
back = comp.index('RtlCopyMemory(bx->BounceVa, a->BounceBuffer[index], length)')
rel = comp.index('ans_bounce_release(&a->Bounce, (int)index)')
done = comp.index('StorPortNotification(RequestComplete, a, srb)')
assert back < rel < done, 'copy the read back and release before the SRB is completed'
assert 'status == SRB_STATUS_SUCCESS && (srb->SrbFlags & SRB_FLAGS_DATA_IN)' in comp[:back]
assert '!a->DumpMode && bx && bx->BounceIndex && bx->BounceIndex <= ANS_BOUNCE_COUNT' in comp[:back], \
    'the index is range-checked and the dump instance never touches the pool'
assert 'peer->reads' not in DIRECT and 'peer->flushes' not in DIRECT, 'direct record fields are not reused'
assert 'ans_bounce_ioctl_reply(srb->DataBuffer,srb->DataTransferLength,&a->Bounce)' in NATIVE
assert NATIVE.count('StorPortNotification(RequestComplete') == 1, 'one completion point'
assert 'RequestComplete' not in DIRECT and 'RequestComplete' not in DRIVER

# EXP2447: the pool is optional and never part of FindAdapter (behaviour: storport_shim_test.c).
resources = DIRECT[DIRECT.index('static BOOLEAN direct_carve('):DIRECT.index('static VOID put(')]
carve = resources[:resources.index('/* Bounce pool (common/ans_bounce.h)')]
assert 'Bounce' not in carve and 'BOUNCE' not in carve, 'FindAdapter resources carry no bounce memory'
define = ADAPTER[ADAPTER.index('#define APPLE_ANS_DIRECT_BYTES'):]
assert 'BOUNCE' not in define[:define.index('\n')], 'the uncached extension keeps its 0.4.8.3 size'
find = DRIVER[DRIVER.index('ULONG AppleAnsFindAdapter('):DRIVER.index('BOOLEAN AppleAnsBuildIo(')]
assert 'Bounce' not in find, 'neither FindAdapter nor HwInitialize builds, configures or needs the pool'
passive = NATIVE[NATIVE.index('BOOLEAN AppleAnsPassiveInitialize('):NATIVE.index('static ans_u32 wire_read32(')]
assert 'AppleAnsDirectBouncePool(a);\n    return TRUE;' in passive, 'the pool outcome never fails initialisation'
print('PASS bounce_wiring')
