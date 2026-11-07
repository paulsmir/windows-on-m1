"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Faulted render, compute, and combined submissions retain DMA ownership until
all firmware done pointers retire; killed work need not write success stamps. Stale timeout events cannot abort
a successor; a new Fault must still resume a new halt during recovery.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class QueueRecovery(unittest.TestCase):
    def test_render_compute_recovery_ownership(self):
        shared = ROOT / 'drivers/apple-agx/shared'
        program = ('#include "apple_agx_g13_queue_runtime_test.c"\n'
                   '#undef assert\n#include <assert.h>\n' + QUEUE_CASES)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'queue.c'
            path.write_text(program)
            binary = Path(tmp) / 'queue'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(shared / 'include'),
                            '-I', str(shared / 'tests'), str(path),
                            str(shared / 'src/apple_agx_g13_queue_runtime.c'),
                            str(shared / 'src/apple_agx_g13_codec.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

QUEUE_CASES = r'''
int main(void) {
  unsigned mode;
  for (mode=0; mode<3; ++mode) {
    TEST_FIXTURE f;
    APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
    APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
    unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE] = {0};
    TestInitialize(&f); TestEnableCompute(&f);
    f.Runtime.Io.Recover=TestRecover;
    s=TestSubmission(&f, 101u);
    s.Compute.Source=f.ComputeSource; s.Compute.Destination=f.ComputeDestination;
    s.Compute.Bytes=sizeof(f.ComputeSource);
    s.Compute.GpuAddresses[0]=0x1500300000ULL;
    s.Compute.GpuAddressCount=1u; s.Compute.ExpectedStamp=0xffffff00u;
    if (mode==0) {
      assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s)==0);
    } else if (mode==1) {
      s.ComputeOnly=1;
      assert(AppleAgxG13QueueRuntimeSubmitCompute(&f.Runtime,&s)==0);
    } else {
      s.Ta.GpuAddresses[2]=s.Ta.GpuAddresses[1];
      s.Ta.GpuAddresses[1]=s.Ta.GpuAddresses[0];
      s.Ta.GpuAddresses[0]=0x1500308800ULL;
      s.Ta.GpuAddressCount=3; s.TaLeadingBarrier=1;
      assert(AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&f.Runtime,&s)==0);
    }
    PutU32(event, mode==1 ? AppleAgxG13EventFault : AppleAgxG13EventTimeout);
    assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
    assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
    /* Fault has no counter: it can be a new halt of a dependent queue,
     * unlike a repeat Timeout with the same counter. Never suppress it. */
    assert(f.IoState.Recovers==(mode==1 ? 2u : 1u) && f.IoState.Quiesces==0);
    /* No deadline manufactures retirement and no second submission is admitted. */
    assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,100000ULL)==0);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
    assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s)!=0);
    f.TaDone=f.TaWrite; f.D3Done=f.D3Write;
    /* Killed commands never execute their final stamp writes. */
    if (mode) {
      assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,100001ULL)==0);
      assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
      /* Beyond the expected stamp across wrap is legal, but a missing
       * compute consumer retirement still holds the entire submission. */
      /* Even a killed compute job must return its ring entry. */
      assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,100002ULL)==0);
      assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
      f.ComputeDone=f.ComputeWrite;
    }
    f.IoState.FailReads=1;
    assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,100003ULL)==
           AppleAgxG13QueueRuntimeResultMemoryFailed);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
    f.IoState.FailReads=0;
    if (mode!=1) {
      f.TaDone=APPLE_AGX_G13_RING_CAPACITY;
      assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,100004ULL)==
             AppleAgxG13QueueRuntimeResultInvalidState);
      assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
      f.TaDone=f.TaWrite;
    }
    assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime,100005ULL)==0);
    assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
    assert(completion.Status==AppleAgxG13QueueCompletionRecovered &&
           completion.Fence==101u && !f.Runtime.RecoveryPending);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
    assert(f.IoState.Quiesces==0);
    assert(f.TaStamp==0 && f.D3Stamp==0 && f.ComputeStamp==0);
    if (mode==0) {
      /* A queued old Timeout must not abort the successor or attempt to
       * resume firmware that is already running (m1n1 stale-counter guard). */
      s=TestSubmission(&f,102u); s.Ta.GpuAddressCount=1u;
      assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s)==0);
      assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
      assert(f.IoState.Recovers==1 && !f.Runtime.RecoveryPending);
      assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&completion));
      PutU32(event+4,1u);
      assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==0);
      assert(f.IoState.Recovers==2 && f.Runtime.RecoveryPending);
    }
  }
  return 0;
}
'''
