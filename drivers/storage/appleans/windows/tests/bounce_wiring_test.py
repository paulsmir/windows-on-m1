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

start = NATIVE[NATIVE.index('static BOOLEAN start_request(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb)\n{'):NATIVE.index('BOOLEAN AppleAnsNativeInterrupt(')]
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
assert 'if (ans_bounce_must_wait(&a->Bounce)) return FALSE;' in bounce, \
    'no buffer: the request waits in the driver while a release is expected, otherwise direct'
# EXP2479: the pool being full is never answered BUSY, and no BUSY answer changes the request.
assert 'SRB_STATUS_BUSY' not in bounce and 'StorPortDeviceBusy' not in NATIVE + DRIVER, \
    'a full pool is ordinary load, not a BUSY answer'
entry = NATIVE[NATIVE.index('BOOLEAN AppleAnsNativeStart('):NATIVE.index('static BOOLEAN start_request(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb)\n{')]
assert entry.index('if (a->WaitHead && srb->SrbExtension) { wait_append(a,srb,TRUE);return TRUE; }') < \
    entry.index('if (!start_request(a,srb)) wait_append(a,srb,FALSE);') < entry.index('AppleAnsWaitDrain(a);'), \
    'a later request never passes a waiting one'
drain = NATIVE[NATIVE.index('VOID AppleAnsWaitDrain('):NATIVE.index('VOID AppleAnsWaitRetire(')]
assert drain.index('a->WaitHead=x->WaitNext') < drain.index('if (!start_request(a,srb))'), \
    'a waiting request is unlinked before it is started (it may be completed inside)'
assert NATIVE.count('AppleAnsWaitRetire(a,status);') == 1 and 'AppleAnsWaitRetire(a, status);' in DIRECT, \
    'reset and stop end the waiting requests too'
assert 'AppleAnsDispatch(a);\n    AppleAnsWaitDrain(a);\n    return claimed;' in DIRECT, \
    'every interrupt that completed commands starts the waiting list'
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
