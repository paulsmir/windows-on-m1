"""rc98: the TDR reset path with a compute job in the sidecar's job-word set B.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
With Wom1JobDepth2Caps = 1, envelope compute jobs and v5 preludes alternate
the sidecar's job-word sets, so the job a TDR drill leaves with the
firmware may own set B. Every reader of a job's words must follow that
job's set: RC40's timestamp publication and the completion telemetry read
the job's own block through Job->ComputeTimestampBlock, never a fixed set A
offset. The rc97 (EXP2030) and rc92 invariants must hold unchanged:
  - a healthy set B completion publishes its own (set B) firmware end before
    Windows sees the fence, not set A's stale one;
  - the drilled (poisoned) set B job is retired by the reset quiescence
    without writing into its context, exactly once, with no PollFailure,
    and the scheduler reset reports it as LastAbortedFence;
  - a second reset finds no active job;
  - after the reset, the next compute job of the new queue lifetime starts
    again in set A (AppleAgxRenderSharedMemoryInitializeComputeQueue) --
    asserted in test_render_sidecar_sets_rc98.

The real AdmissionResetQuiesceActive, AdmissionPlatformRuntimeQuiesceActive,
AdmissionBackendComplete, AdmissionNotifyCompletionAtInterrupt,
AdmissionGpuTimestampEndNanoseconds, AdmissionG3PublishTimestampLocked and
AdmissionGpuvaG3CompleteJobTimestamp run with the real platform provider,
composer, backend runtime and scheduler, as in the rc97 replay
(test_g4_tdr_drill_poisoned_completion, whose shim this test reuses).
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_g4_tdr_drill_poisoned_completion import SHIM, SHARED, RENDER, extract

CASES = r'''
#include "apple_agx_render_shared_memory.h"
#define DRILL_FENCE 0x1d65au
#define COMPUTE_EVENT 0x23u
static ADMISSION_CONTEXT a;
static ADMISSION_PLATFORM_RUNTIME r;
static ADMISSION_G3_STATE s;
static ADMISSION_G3_PROCESS process;
static ADMISSION_RENDER_CONTEXT drilled;
static unsigned char event_state[J313_AGX_G2_CHANNEL_STATE_STRIDE];
static unsigned char event_ring[APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT * APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
static unsigned char sidecar[0x10000];
static unsigned char arena[64];
#define SET_B_BLOCK APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(APPLE_AGX_COMPUTE_JOB_WORD_SET_B, \
                                                      APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET)

static APPLE_AGX_BACKEND_BOOL flush_cpu(void *c, const void *p, APPLE_AGX_BACKEND_U32 n) {
  (void)c; (void)p; (void)n; return 1; }

static void push(APPLE_AGX_BACKEND_U32 index, APPLE_AGX_BACKEND_U32 queue, APPLE_AGX_BACKEND_U32 event,
                 APPLE_AGX_BACKEND_U32 done, APPLE_AGX_BACKEND_U32 fence) {
  APPLE_AGX_BACKEND_U32 w[5] = {queue, event, 7u, done, fence};
  memcpy(event_ring + index * APPLE_AGX_G13_EVENT_MESSAGE_SIZE, w, sizeof(w));
}

/* A drilled G4 compute job whose words are in set B: its compute event waits
 * unread in the event ring; backend Submitted, provider Created. */
static void setup(void) {
  APPLE_AGX_BACKEND_U32 *pointers = (APPLE_AGX_BACKEND_U32 *)event_state;
  const ULONGLONG set_a_end = 0x111ULL, set_b_end = 24000001ULL; /* 1000000041 ns */
  memset(&a, 0, sizeof(a)); memset(&r, 0, sizeof(r)); memset(&s, 0, sizeof(s));
  memset(&process, 0, sizeof(process)); memset(&drilled, 0, sizeof(drilled));
  memset(local_bytes, 0, sizeof(local_bytes)); memset(event_state, 0, sizeof(event_state));
  memset(sidecar, 0, sizeof(sidecar));
  /* set A's block holds a stale end of an older job */
  memcpy(sidecar + APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET + 8u, &set_a_end, 8u);
  memcpy(sidecar + SET_B_BLOCK + 8u, &set_b_end, 8u);
  notifications = lock_depth = end_calls = published = 0; now_ms = 506748u;
  a.InterfaceValid = 1; a.Interface.DxgkCbNotifyInterrupt = notify;
  a.Interface.DxgkCbQueueDpc = dpc; a.Interface.DxgkCbSynchronizeExecution = sync_callback;
  AppleAgxSchedulerInitialize(&a.Scheduler);
  a.Scheduler.CompletedFence = a.Scheduler.LastSubmittedFence = DRILL_FENCE - 1u;
  assert(AppleAgxSchedulerQueueFence(&a.Scheduler, 0, 0, DRILL_FENCE));
  assert(AppleAgxSchedulerActivateFence(&a.Scheduler, 0, 0, DRILL_FENCE));
  a.RenderPacket.State = AdmissionRenderPacketActive;
  a.RenderPacket.Description.Fence = DRILL_FENCE;
  a.RenderPacket.Description.ContextToken = (UINT64)(uintptr_t)&drilled;
  a.SchedulerFaulted = 0x4132e;
  a.GpuvaG3State = &s; a.PlatformRuntime = &r; live_state = &s;
  drilled.Object.FenceOutstanding = DRILL_FENCE; drilled.GpuvaG3Process = &process;
  s.ActiveProcess = &process; s.ActiveFence = DRILL_FENCE;
  process.Graph.JobInFlight = 1; process.Graph.LeaseToken = 1;
  pte = (APPLE_AGX_GPUVA_G3_LOGICAL_PTE){0x100000, 1, APPLE_AGX_GPUVA_G3_VALID | APPLE_AGX_GPUVA_G3_WRITE,
                                         ADMISSION_MEMORY_LOCAL_SEGMENT};
  r.Adapter = &a; r.BackendStarted = 1; r.WorkIdle.Signaled = 1;
  r.Backend.Phase = AppleAgxBackendRuntimeSubmitted;
  r.Backend.Io.Context = &r;
  r.Backend.Io.Complete = AdmissionBackendComplete;
  r.Backend.Io.Memory.FlushForCpu = flush_cpu;
  r.Backend.ArenaCpuAddress = arena; r.Backend.ArenaBytes = sizeof(arena);
  r.Backend.PendingSubmission.Submission.Fence = DRILL_FENCE;
  r.Backend.PendingJob.Kind = AppleAgxBackendJobCompute;
  r.Backend.PendingJob.ComputeWorkAddresses[0] = 0x1500390000ULL +
      APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(APPLE_AGX_COMPUTE_JOB_WORD_SET_B, APPLE_AGX_COMPUTE_WORK_OFFSET);
  r.Backend.PendingJob.ComputeWorkAddressCount = 1u;
  r.Backend.PendingJob.ComputeEvent = COMPUTE_EVENT;
  r.Backend.PendingJob.ComputeExpectedStamp = 7u;
  r.Backend.PendingJob.ComputeExpectedDonePointer = 1206u;
  r.Backend.PendingJob.ComputeTimestampBlock = sidecar + SET_B_BLOCK;
  r.Backend.PendingJob.CommandVa = 0x1500a00000ULL;
  r.Backend.PendingJob.TimestampPublishVa = 0x1234500008ULL;
  r.Provider.Initialized = 1;
  r.Provider.Runtime = &r.Backend;
  r.Provider.Composer.Initialized = 1;
  r.Provider.Composer.Runtime = &r.Backend;
  r.Provider.Composer.QueueProvider = &r.Provider.QueueProvider;
  r.Provider.QueueProvider.Phase = AppleAgxG13QueueProviderCreated;
  r.Provider.Transport = (APPLE_AGX_PLATFORM_TRANSPORT_IO){&r, t_flush, t_flush, t_barrier,
      t_publish, t_read, t_doorbell, t_quiesce, t_now};
  r.Provider.Channels.Event.StateCpuAddress = event_state;
  r.Provider.Channels.Event.RingCpuAddress = event_ring;
  pointers[APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET / 4u] = 0x40u;
  pointers[APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET / 4u] = 0x41u;
  push(0x40u, AppleAgxBackendQueueCompute, COMPUTE_EVENT, 1206u, DRILL_FENCE);
}

static NTSTATUS reset_quiesce(const char *entry) {
  NTSTATUS status;
  RtlZeroMemory(&a.GpuReset, sizeof(a.GpuReset));
  { ULONG unsubmitted = 0u;
    status = AdmissionResetQuiesceActive(&a, &unsubmitted);
    assert(unsubmitted == 0u); /* rc102: the drilled job reached the firmware */ }
  if (status != STATUS_SUCCESS)
    printf("RED %s: status %#x quiesce %u poll_failure %#x backend_phase %u\n", entry,
           (unsigned)status, a.GpuReset.Quiesce, r.PostResume.PollFailure, (unsigned)r.Backend.Phase);
  return status;
}

int main(void) {
  APPLE_AGX_U32 aborted = 0u;
  ULONGLONG written = 0u;
  /* Healthy set B completion: RC40 publishes the job's own (set B) end. */
  setup();
  {
    APPLE_AGX_BACKEND_U32 drained = 0u, completed = 0u;
    assert(AppleAgxPlatformProviderPoll(&r.Provider, 64u, &drained, &completed));
    assert(drained == 1u && completed == DRILL_FENCE && published && end_calls == 1u);
    memcpy(&written, local_bytes + 8, 8);
    if (written != 1000000041ULL) {
      printf("RED set B job published %llu, not its own end\n", (unsigned long long)written);
      return 1;
    }
    assert(notifications == 1u && observed.InterruptType == DXGK_INTERRUPT_DMA_COMPLETED);
  }
  /* The drill: the set B job's context is poisoned before the poll. */
  setup();
  if (reset_quiesce("ResetEngine#1") != STATUS_SUCCESS) return 1;
  assert(drilled.GpuvaG3Poisoned);
  assert(a.GpuReset.Quiesce == AdmissionGpuResetQuiesceEnded && a.GpuReset.WaitMs == 0u);
  assert(!r.PostResume.PollFailure && r.Backend.Phase == AppleAgxBackendRuntimeReady);
  assert(notifications == 1u && observed.InterruptType == DXGK_INTERRUPT_DMA_COMPLETED &&
         observed.DmaCompleted.SubmissionFenceId == DRILL_FENCE);
  assert(end_calls == 1u && !published && s.LastCompletedFence == DRILL_FENCE && !s.ActiveProcess);
  memcpy(&written, local_bytes + 8, 8);
  assert(written == 0u);
  assert(a.RenderPacket.State == 0u && drilled.Object.FenceOutstanding == 0u);
  assert(AppleAgxSchedulerActiveFence(&a.Scheduler, 0, 0) == 0u);
  assert(AppleAgxSchedulerResetEngine(&a.Scheduler, 0, 0, &aborted));
  assert(aborted == DRILL_FENCE);
  assert(reset_quiesce("ResetEngine#2") == STATUS_SUCCESS && notifications == 1u);
  puts("SET_B_RESET_OK");
  return 0;
}
'''


class TdrDrillSetB(unittest.TestCase):
    def test_reset_retires_a_set_b_compute_job(self):
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        gpuva = (RENDER / 'src/gpuva_g3_windows.c').read_text()
        scheduler = (RENDER / 'src/scheduler_windows.c').read_text()
        provider = (SHARED / 'src/apple_agx_platform_provider.c').read_text()
        notification = re.search(r'typedef struct _ADMISSION_COMPLETION_NOTIFICATION \{.*?'
                                 r'} ADMISSION_COMPLETION_NOTIFICATION;', backend, re.S)[0]
        program = '\n'.join(
            [SHIM, notification] +
            [extract(provider, n) for n in (
                'AppleAgxPlatformZero', 'AppleAgxPlatformCopy',
                'AppleAgxPlatformTransportValid', 'AppleAgxPlatformProviderDrainEvents',
                'AppleAgxPlatformProviderPoll')] +
            [extract(gpuva, n) for n in (
                'AdmissionG3PublishTimestampLocked', 'AdmissionGpuvaG3CompleteJobTimestamp')] +
            [extract(backend, n) for n in (
                'AdmissionRenderTimestampPair', 'AdmissionGpuTimestampEndNanoseconds',
                'AdmissionNotifyCompletionAtInterrupt',
                'AdmissionBackendComplete', 'AdmissionPostResumePollByte',
                'AdmissionPostResumePollFailure', 'AdmissionPlatformRuntimeQuiesceActive')] +
            [extract(scheduler, 'AdmissionResetQuiesceActive'), CASES])
        with tempfile.TemporaryDirectory(prefix='rc98-drill-') as tmp:
            path = Path(tmp) / 'drill.c'
            path.write_text(program)
            binary = Path(tmp) / 'drill'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined',
                            '-I', str(SHARED / 'include'), '-I', str(RENDER / 'include'),
                            str(path), str(SHARED / 'src/apple_agx_backend_runtime.c'),
                            str(SHARED / 'src/apple_agx_platform_composer.c'),
                            str(SHARED / 'src/apple_agx_scheduler.c'),
                            str(SHARED / 'src/apple_agx_submission.c'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('SET_B_RESET_OK', result.stdout)

    def test_no_kmd_reader_uses_a_fixed_sidecar_offset(self):
        """Readers of a job's words go through the job image (its set)."""
        for name in ('backend_platform_windows.c', 'scheduler_windows.c', 'gpuva_g3_windows.c'):
            text = (RENDER / 'src' / name).read_text()
            self.assertIsNone(re.search(r'APPLE_AGX_COMPUTE_(WORK|MICROSEQUENCE|TA_BARRIER|'
                                        r'TIMESTAMP|TIMESTAMP_MICROSEQUENCE)_OFFSET', text), name)
        provider = (SHARED / 'src/apple_agx_platform_provider.c').read_text()
        self.assertIsNone(re.search(r'APPLE_AGX_COMPUTE_\w+_OFFSET', provider))


if __name__ == '__main__':
    unittest.main()
