"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2005 (rc37 drill): a standalone compute job (ComputeTerminal) took an MMU
fault (Timeout with valid FAULT_INFO, CDM unmapped read at 0x10000). The
firmware was resumed once, but the compute queue stayed at done 1 / write 2:
two ResetEngine calls and ResetFromTimeout each failed quiescence with
STATUS_DEVICE_HARDWARE_ERROR (c0000483) and Windows bugchecked 0x116.

The existing stalled-TDR replay covers only the combined TA/3D/compute job.
This replay pins the compute-only shape of that receipt: while the compute
done pointer has not reached its published endpoint, every reset fails closed
and keeps the culprit fence, its context and the other contexts' work;
nothing writes a stamp, stops RTKit or fabricates an aborted fence. Once the
firmware retires the compute entry (what rc77's fault_control 0xb is meant to
allow), ResetFromTimeout succeeds, aborts exactly the culprit and restarts.

Compile the production reset DDIs, scheduler and queue runtime together
(test_apple_agx_tdr_reset_quiesce harness); the firmware model controls only
event arrival and the shared compute done pointer.
"""
import unittest

import test_apple_agx_tdr_reset_quiesce as replay


class MmuFaultComputeTdr(unittest.TestCase):
    def test_unretired_compute_fault_fails_closed_until_retirement(self):
        replay.TdrResetQuiesce().run_replay(CASES, FIRMWARE)


CASES = replay.CASES.split('int main(void) {')[0] + r'''
void ReplayQueueAllowRetirement(unsigned allowed);
int main(void) {
  unsigned retry;
  DXGKARG_RESETENGINE reset;
  setup(0, 3);
  adapter.SchedulerFaulted = 0;
  use_firmware_runtime = 1;
  ReplayQueueSetup(0xce71cu);
  ReplayQueueAllowRetirement(0);
  /* EXP2005 history (fence 0x120d there; the harness fence here):
   * ResetEngine, ResetEngine, ResetFromTimeout. */
  for (retry = 0; retry < 3; ++retry) {
    memset(&reset, 0, sizeof(reset));
    assert((retry < 2 ? AdmissionDdiResetEngine(&adapter, &reset) :
            AdmissionDdiResetFromTimeout(&adapter)) == STATUS_DEVICE_HARDWARE_ERROR);
    assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceTimeout &&
           last_recorded.Stage == AdmissionGpuResetStageQuiesce &&
           last_recorded.Succeeded == 0 && last_recorded.Resets == retry + 1);
    assert(adapter.Scheduler.ActiveFence == 0xce71cu &&
           adapter.Scheduler.LastAbortedFence == 0);
    assert(owner.Object.FenceOutstanding == 0xce71cu && scene_owner.Started);
    assert(adapter.G4PendingCount == 2);
    assert(reset.LastAbortedFenceId == 0 && private_resets == 0 && rtkit_stops == 0);
    assert(AdmissionDdiRestartFromTimeout(&adapter) == STATUS_DEVICE_NOT_READY);
  }
  /* Positive control: the firmware retires the faulted compute entry
   * without writing its success stamp. */
  ReplayQueueAllowRetirement(1);
  assert(AdmissionDdiResetFromTimeout(&adapter) == STATUS_SUCCESS);
  assert(last_recorded.LastAbortedFence == 0xce71cu &&
         last_recorded.Quiesce == AdmissionGpuResetQuiesceEnded &&
         last_recorded.Stage == AdmissionGpuResetStageComplete);
  assert(adapter.Scheduler.ActiveFence == 0 && adapter.G4PendingCount == 0);
  assert(owner.GpuvaG3Poisoned && owner.Object.FenceOutstanding == 0);
  assert(AdmissionDdiRestartFromTimeout(&adapter) == STATUS_SUCCESS && !paused);
  assert(!adapter.SchedulerFaulted && !rtkit_stops && !lock_depth);
  puts("compute mmu fault TDR replay ok");
  return 0;
}
'''

FIRMWARE = r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
static TEST_FIXTURE f;
static unsigned allowed, event_sent;
void ReplayQueueAllowRetirement(unsigned value) { allowed = value; }
void ReplayQueueSetup(unsigned fence) {
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
  TestInitialize(&f); TestEnableCompute(&f);
  f.Runtime.Io.Recover = TestRecover;
  memset(&s, 0, sizeof(s));
  s.Fence = fence; s.Timestamp = 0x1122334455667788ULL; s.NowTicks = 1000ULL;
  s.Compute.Source = f.ComputeSource; s.Compute.Destination = f.ComputeDestination;
  s.Compute.Bytes = sizeof(f.ComputeSource);
  s.Compute.GpuAddresses[0] = 0x1500300000ULL;
  s.Compute.GpuAddressCount = 1; s.Compute.ExpectedStamp = fence;
  s.ComputeOnly = APPLE_AGX_BACKEND_TRUE;
  /* EXP2005: the previous compute job retired (done 1); this one is 1 -> 2. */
  f.ComputeWrite = f.ComputeDone = 1u; f.ComputeStamp = fence - 4u;
  assert(AppleAgxG13QueueRuntimeSubmitCompute(&f.Runtime, &s) == 0);
  assert(f.Runtime.ComputeTerminal && f.ComputeWrite == 2u);
  allowed = event_sent = 0;
}
int ReplayQueuePoll(int poll) {
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE] = {0};
  if (!event_sent) {
    PutU32(event, AppleAgxG13EventTimeout);
    assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime, event, sizeof(event)) == 0);
    event_sent = 1;
  }
  if (allowed) f.ComputeDone = f.ComputeWrite;
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime, 1000ULL + poll) == 0);
  assert(f.IoState.Recovers == 1 && f.IoState.Quiesces == 0);
  assert(f.ComputeStamp == f.Runtime.ComputePending.ExpectedStamp - 4u);
  if (!allowed) {
    assert(f.Runtime.RecoveryPending && f.Runtime.PendingFence == 0xce71cu);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
    return 0;
  }
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
  assert(completion.Fence == 0xce71cu &&
         completion.Status == AppleAgxG13QueueCompletionRecovered);
  return 1;
}
'''


if __name__ == '__main__':
    unittest.main()
