"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1964 resumed firmware but all three queues remained unretired. Repeated
engine resets and promotion to adapter reset must not turn that resume into
DMA ownership transfer. Every partially retired queue combination must retain
the active scene and pending contexts; only actual retirement permits reset
and restart. This is a safety regression, not proof of hardware recovery.

Compile the production reset DDIs, scheduler and queue runtime together. The
existing replay models Windows services and the backend completion callback;
the firmware model controls only event arrival and shared done pointers.
"""
import unittest

import test_apple_agx_tdr_reset_quiesce as replay


class StalledTdrRecovery(unittest.TestCase):
    def test_engine_adapter_retries_retain_unretired_combined_job(self):
        replay.TdrResetQuiesce().run_replay(CASES, FIRMWARE)


CASES = replay.CASES.split('int main(void) {')[0] + r'''
void ReplayQueueAllowRetirement(unsigned mask);
int main(void) {
  unsigned mask, retry;
  DXGKARG_RESETENGINE reset;
  /* Every proper subset of TA, 3D and compute remains insufficient. */
  for (mask = 0; mask < 7; ++mask) {
    setup(0, 3);
    adapter.SchedulerFaulted = 0;
    use_firmware_runtime = 1;
    ReplayQueueSetup(0xce71cu);
    ReplayQueueAllowRetirement(mask);
    for (retry = 0; retry < 3; ++retry) {
      memset(&reset, 0, sizeof(reset));
      assert((retry < 2 ? AdmissionDdiResetEngine(&adapter, &reset) :
              AdmissionDdiResetFromTimeout(&adapter)) == STATUS_DEVICE_HARDWARE_ERROR);
      assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceTimeout &&
             last_recorded.Stage == AdmissionGpuResetStageQuiesce &&
             last_recorded.WaitMs >= ADMISSION_RESET_QUIESCE_TIMEOUT_MS &&
             last_recorded.Succeeded == 0 && last_recorded.Resets == retry + 1);
      assert(adapter.Scheduler.ActiveFence == 0xce71cu &&
             adapter.Scheduler.CompletedFence == 0xce71bu &&
             adapter.Scheduler.LastAbortedFence == 0);
      assert(owner.Object.FenceOutstanding == 0xce71cu &&
             owner.GpuvaG3PrivateFence == (LONG)0xce71cu &&
             scene_owner.Started && scene_owner.Queued);
      assert(adapter.G4PendingCount == 2 &&
             other.Object.FenceOutstanding == 0xce71du &&
             other.GpuvaG3SecondFence == 0xce71eu);
      assert(reset.LastAbortedFenceId == 0 && private_resets == 0 && rtkit_stops == 0);
      assert(AdmissionDdiRestartFromTimeout(&adapter) == STATUS_DEVICE_NOT_READY);
    }
    assert(paused);
    /* Positive control: real queue retirement, without success stamps,
     * permits the same adapter to reset, abort its culprit and restart. */
    ReplayQueueAllowRetirement(7);
    assert(AdmissionDdiResetFromTimeout(&adapter) == STATUS_SUCCESS);
    assert(last_recorded.LastAbortedFence == 0xce71cu &&
           last_recorded.Quiesce == AdmissionGpuResetQuiesceEnded &&
           last_recorded.Stage == AdmissionGpuResetStageComplete);
    assert(adapter.Scheduler.ActiveFence == 0 && adapter.G4PendingCount == 0);
    assert(adapter.Scheduler.CompletedFence == adapter.Scheduler.LastSubmittedFence);
    assert(owner.GpuvaG3Poisoned && owner.Object.FenceOutstanding == 0);
    assert(AdmissionDdiRestartFromTimeout(&adapter) == STATUS_SUCCESS && !paused);
    assert(!adapter.SchedulerFaulted && !rtkit_stops && !lock_depth);
  }
  puts("stalled combined TDR replay ok");
  return 0;
}
'''

FIRMWARE = r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
static TEST_FIXTURE f;
static unsigned allowed, event_sent;
void ReplayQueueAllowRetirement(unsigned mask) { allowed = mask; }
void ReplayQueueSetup(unsigned fence) {
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
  TestInitialize(&f); TestEnableCompute(&f);
  f.Runtime.Io.Recover = TestRecover;
  s = TestSubmission(&f, fence);
  s.Compute.Source = f.ComputeSource; s.Compute.Destination = f.ComputeDestination;
  s.Compute.Bytes = sizeof(f.ComputeSource);
  s.Compute.GpuAddresses[0] = 0x1500300000ULL;
  s.Compute.GpuAddressCount = 1; s.Compute.ExpectedStamp = fence;
  s.Ta.GpuAddresses[2] = s.Ta.GpuAddresses[1];
  s.Ta.GpuAddresses[1] = s.Ta.GpuAddresses[0];
  s.Ta.GpuAddresses[0] = 0x1500308800ULL;
  s.Ta.GpuAddressCount = 3; s.TaLeadingBarrier = 1;
  assert(AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&f.Runtime, &s) == 0);
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
  if (allowed & 1) f.TaDone = f.TaWrite;
  if (allowed & 2) f.D3Done = f.D3Write;
  if (allowed & 4) f.ComputeDone = f.ComputeWrite;
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime, 1000ULL + poll) == 0);
  assert(f.IoState.Recovers == 1 && f.IoState.Quiesces == 0);
  assert(f.TaStamp == 0 && f.D3Stamp == 0 && f.ComputeStamp == 0);
  if (allowed != 7) {
    assert(f.Runtime.RecoveryPending && f.Runtime.PendingFence != 0);
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
    return 0;
  }
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
  assert(completion.Status == AppleAgxG13QueueCompletionRecovered);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
  return 1;
}
'''
