"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2005 and EXP2007 (fault_control 0 and 0xb): after a valid MMU fault in a
standalone compute job the firmware was resumed but never retired the entry
(done 1 / write 2) and sent no ChannelError, so every reset failed (0x116).
rc77 adds a KMD-initiated RecoverChannel (Asahi fw/channels.rs:187,
gpu.rs:1459) as the last step before a reset gives up.

Invariants of AppleAgxG13QueueRuntimeForceChannelRecovery:
- refused before any firmware recovery and when nothing is pending;
- sends exactly one RecoverChannel per unretired pipe, with that pipe's
  QueueInfo address and the job's expected stamp, and only once per job;
- skips pipes that already retired or were already recovered by a
  ChannelError;
- never completes the job by itself: only done == write, observed by the
  provider's next CheckTimeout, returns ownership (a completion taken inside
  the call would make that CheckTimeout fail with InvalidState), and a
  transport failure latches ChannelRecoveryFailed;
- events seen while RecoveryPending are counted by kind for the receipt.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'

PROGRAM = r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
static unsigned channel_calls, channel_pipe, channel_value, channel_fail;
static unsigned long long channel_queue;
static APPLE_AGX_BACKEND_BOOL RecoverChannelFake(void *Context, APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_BACKEND_U32 Pipe, APPLE_AGX_BACKEND_U64 Queue, APPLE_AGX_BACKEND_U32 Value) {
  (void)Context; assert(Fence == 0x16160u);
  ++channel_calls; channel_pipe = Pipe; channel_queue = Queue; channel_value = Value;
  return channel_fail ? APPLE_AGX_BACKEND_FALSE : APPLE_AGX_BACKEND_TRUE;
}
static void submit(TEST_FIXTURE *f) {
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
  TestInitialize(f); TestEnableCompute(f);
  f->Runtime.Io.Recover = TestRecover;
  f->Runtime.Io.RecoverChannel = RecoverChannelFake;
  memset(&s, 0, sizeof(s));
  s.Fence = 0x16160u; s.Timestamp = 1; s.NowTicks = 1000ULL;
  s.Compute.Source = f->ComputeSource; s.Compute.Destination = f->ComputeDestination;
  s.Compute.Bytes = sizeof(f->ComputeSource);
  s.Compute.GpuAddresses[0] = 0x1500300000ULL;
  s.Compute.GpuAddressCount = 1; s.Compute.ExpectedStamp = 0x16160u;
  s.ComputeOnly = APPLE_AGX_BACKEND_TRUE;
  f->ComputeWrite = f->ComputeDone = 930u; f->ComputeStamp = 0x1615cu;
  assert(AppleAgxG13QueueRuntimeSubmitCompute(&f->Runtime, &s) == 0);
}
static void timeout(TEST_FIXTURE *f, unsigned counter) {
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE] = {0};
  PutU32(event, AppleAgxG13EventTimeout); PutU32(event + 4, counter);
  assert(AppleAgxG13QueueRuntimeHandleEvent(&f->Runtime, event, sizeof(event)) == 0);
}
int main(void) {
  static TEST_FIXTURE f;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  APPLE_AGX_BACKEND_U32 issued = 99u;
  /* Refused without a firmware recovery (nothing proves the halt was handled). */
  submit(&f); channel_calls = 0; channel_fail = 0;
  assert(AppleAgxG13QueueRuntimeForceChannelRecovery(&f.Runtime, &issued) ==
         AppleAgxG13QueueRuntimeResultInvalidState && issued == 0u && !channel_calls);
  /* EXP2007: Timeout (+FAULT_INFO), resume, entry 930 -> 931 never retires. */
  timeout(&f, 5u);
  assert(f.Runtime.RecoveryPending && f.IoState.Recovers == 1u);
  assert(f.Runtime.PostRecoveryEvents == 0u);
  timeout(&f, 5u);                      /* a stale duplicate is still counted */
  assert(f.Runtime.PostRecoveryEvents == 1u &&
         f.Runtime.PostRecoveryKindMask == (1u << AppleAgxG13EventTimeout) &&
         f.Runtime.LastPostRecoveryKind == AppleAgxG13EventTimeout &&
         f.IoState.Recovers == 1u);
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime, 2000ULL) == 0);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
  assert(AppleAgxG13QueueRuntimeForceChannelRecovery(&f.Runtime, &issued) == 0);
  assert(issued == 4u && channel_calls == 1u && channel_pipe == AppleAgxG13QueueCompute &&
         channel_queue == f.Runtime.Config.Compute.QueueInfoGpuAddress &&
         channel_value == 0x16160u && f.Runtime.ForcedChannelMask == 4u);
  /* The command is not retirement. */
  assert(f.Runtime.RecoveryPending &&
         !AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
  assert(f.ComputeStamp == 0x1615cu && f.ComputeDone == 930u);
  /* Once per job. */
  assert(AppleAgxG13QueueRuntimeForceChannelRecovery(&f.Runtime, &issued) ==
         AppleAgxG13QueueRuntimeResultInvalidState && issued == 0u && channel_calls == 1u);
  /* The firmware retires the entry: only now does the job complete, as Recovered. */
  f.ComputeDone = f.ComputeWrite;
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime, 2001ULL) == 0);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion) &&
         completion.Fence == 0x16160u &&
         completion.Status == AppleAgxG13QueueCompletionRecovered);
  assert(!f.Runtime.ForcedChannelRecovery && f.Runtime.PostRecoveryEvents == 0u);

  /* An already retired pipe gets no command; the poll completes the job. */
  submit(&f); channel_calls = 0;
  timeout(&f, 6u);
  f.ComputeDone = f.ComputeWrite;
  assert(AppleAgxG13QueueRuntimeForceChannelRecovery(&f.Runtime, &issued) == 0 &&
         issued == 0u && !channel_calls);
  /* The provider's ordinary poll takes the completion, not the force call. */
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime, 2002ULL) == 0);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion) &&
         completion.Status == AppleAgxG13QueueCompletionRecovered);

  /* A transport failure is latched: no second publication, no completion. */
  submit(&f); channel_calls = 0; channel_fail = 1;
  timeout(&f, 7u);
  assert(AppleAgxG13QueueRuntimeForceChannelRecovery(&f.Runtime, &issued) ==
         AppleAgxG13QueueRuntimeResultResetFailed && issued == 4u && channel_calls == 1u &&
         f.Runtime.ChannelRecoveryFailed);
  f.ComputeDone = f.ComputeWrite;
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&f.Runtime, 3000ULL) ==
         AppleAgxG13QueueRuntimeResultResetFailed);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime, &completion));
  assert(QueueRuntimeFailures == 0u);
  puts("forced channel recovery ok");
  return 0;
}
'''


class ForcedChannelRecovery(unittest.TestCase):
    def test_forced_recover_channel_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'replay.c'
            source.write_text(PROGRAM)
            binary = Path(tmp) / 'replay'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I', str(SHARED / 'include'), '-I', str(SHARED / 'tests'),
                            str(source), str(SHARED / 'src/apple_agx_g13_queue_runtime.c'),
                            str(SHARED / 'src/apple_agx_g13_codec.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
