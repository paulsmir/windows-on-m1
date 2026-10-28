"""rc102: the wired depth-2 window in the KMD (Wom1JobDepth2Kick) -- wiring,
receipts and decoder.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The shared state machine is proven by test_render_job_window (real C, two
jobs in flight). This test pins the KMD glue that cannot run on the host:
  - the switch: default off, only on top of Wom1JobDepth2Caps (the second
    sidecar set and the compute-queue Barrier the kick depends on);
  - a kick only from the running job's poll loop, after the rc90 prebind,
    decided under PagingLock + SchedulerLock with the window refusal, and
    the scheduler/entry marked kicked before any firmware-visible step;
  - a kicked successor adopted (activated) inside its own completion before
    the completion reads the packet's context, so a batched pair (U4) is
    reported oldest first in one poll;
  - the reset's quiescence waiting for the newest kicked job, the engine
    reset aborting a kicked fence the firmware never received, and neither
    a preemption nor a reset handing a kicked entry back to VidSch;
  - no SGX MMIO or doorbell in the new KMD functions (rc92);
  - the poll planner (rc100) learning no duration from a job whose end was
    deferred to its successor's or whose poll began after its start;
  - the v4 decoder's wired statistics and the Wom1JobWindow layout (the C
    struct, compiled, decodes back field for field).
"""
from pathlib import Path
import importlib.util
import os
import re
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
WORKER = RENDER / "src/backend_platform_windows.c"
G3 = RENDER / "src/gpuva_g3_windows.c"
SCHED = RENDER / "src/scheduler_windows.c"
TOOL = ROOT / "tools/agx_gpu_frame_breakdown.py"
WORKER_START = ("static VOID AdmissionPlatformWorker(\n"
                "    _In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context) {")


def load():
    spec = importlib.util.spec_from_file_location("agx_gpu_frame_breakdown", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def section(text, start):
    body = text[text.index(start):]
    return body[:body.index("\n}\n")]


class KickWiring(unittest.TestCase):
    def test_switch_is_default_off_and_needs_the_rc98_caps(self):
        text = WORKER.read_text()
        self.assertEqual(text.count('L"Wom1JobDepth2Kick"'), 1)
        self.assertRegex(text, r'runtime->JobDepth2Kick = runtime->Depth2Capabilities != 0u &&\s*'
                               r'AdmissionPlatformSwitchEnabled\(Context, L"Wom1JobDepth2Kick"\)')
        self.assertLess(text.index('L"Wom1JobDepth2Caps"'), text.index('L"Wom1JobDepth2Kick"'))

    def test_kick_only_from_the_poll_loop_after_the_prebind(self):
        text = WORKER.read_text()
        worker = section(text, WORKER_START)
        self.assertEqual(worker.count("AdmissionJobWindowKickWindows("), 1)
        loop = worker[worker.index("while (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&"):]
        call = loop.index("AdmissionJobWindowKickWindows(")
        self.assertLess(loop.index("AdmissionJobAheadPrebindWindows("), call)
        self.assertIn("runtime->JobDepth2Kick && prebindDone && !kickDone",
                      loop[max(0, call - 300):call])
        # the loop ends when the backend no longer holds this job
        self.assertRegex(worker, r"while \(runtime->Backend\.Phase == AppleAgxBackendRuntimeSubmitted &&\s*"
                                 r"!drilled &&\s*"
                                 r"runtime->Backend\.PendingSubmission\.Submission\.Fence ==\s*"
                                 r"description\.Fence")

    def test_decision_marks_the_kick_before_the_firmware_sees_it(self):
        kick = section(WORKER.read_text(), "static BOOLEAN AdmissionJobWindowKickWindows(")
        locked = kick[kick.index("KeAcquireSpinLock(&adapter->PagingLock"):
                      kick.index("KeReleaseSpinLock(&adapter->PagingLock")]
        self.assertIn("AdmissionJobWindowKickRefusal(&s)", locked)
        self.assertIn("AdmissionG4PendingKickLocked(adapter, next)", locked)
        self.assertLess(locked.index("AdmissionJobWindowKickRefusal(&s)"),
                        locked.index("AdmissionG4PendingKickLocked(adapter, next)"))
        after = kick[kick.index("KeReleaseSpinLock(&adapter->PagingLock"):]
        begin = after.index("AdmissionGpuvaG3BeginSuccessorJob(")
        submit = after.index("AppleAgxBackendRuntimeSubmitSuccessor(")
        self.assertLess(begin, submit)
        for refusal in ("s.NextNeedsIdle", "s.SetsValid", "s.Capabilities = runtime->Depth2Capabilities"):
            self.assertIn(refusal, kick)
        # the scheduler side of the mark is the queued fence becoming kicked
        g3 = section(G3.read_text(), "_Use_decl_annotations_ BOOLEAN AdmissionG4PendingKickLocked(")
        self.assertIn("AppleAgxSchedulerKickQueuedFence(&adapter->Scheduler, 0u, 0u, fence)", g3)
        self.assertIn("entry->State = AdmissionG4PendingKicked;", g3)

    def test_batched_successor_is_adopted_inside_its_completion(self):
        complete = section(WORKER.read_text(), "static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete(")
        adopt = complete.index("AdmissionG4PendingAdoptLocked(adapter, Fence)")
        self.assertLess(adopt, complete.index("RenderPacket.Description.ContextToken"))
        self.assertLess(adopt, complete.index("AdmissionGpuvaG3CompleteJobTimestamp("))
        self.assertIn("ADMISSION_JOB_FLAG_DEPTH2_BATCHED", complete)
        adopt_locked = section(G3.read_text(), "_Use_decl_annotations_ BOOLEAN AdmissionG4PendingAdoptLocked(")
        self.assertLess(adopt_locked.index("AdmissionRenderPacketQueue("),
                        adopt_locked.index("AppleAgxSchedulerActivateFence("))
        self.assertIn("AdmissionRenderPacketActivate(&adapter->RenderPacket, fence)", adopt_locked)

    def test_g3_successor_joins_the_lease_and_is_promoted(self):
        g3 = G3.read_text()
        begin = section(g3, "static NTSTATUS AdmissionGpuvaG3BeginJobAs(")
        self.assertIn("AppleAgxGpuvaG3GraphBeginNestedJob(&process->Graph)", begin)
        self.assertIn("state->ActiveProcess == process", begin)
        complete = section(g3, "BOOLEAN AdmissionGpuvaG3CompleteJobTimestamp(")
        self.assertIn("state->ActiveFence = state->SuccessorFence;", complete)

    def test_reset_waits_for_and_aborts_the_kicked_job(self):
        quiesce = section(WORKER.read_text(), "_Use_decl_annotations_ NTSTATUS AdmissionPlatformRuntimeQuiesceActive(")
        self.assertIn("AppleAgxSchedulerKickedFence(&Context->Scheduler)", quiesce)
        self.assertIn("runtime->Backend.SuccessorPending", quiesce)
        sched = SCHED.read_text()
        entry = section(sched, "static NTSTATUS AdmissionResetQuiesceActive(")
        self.assertIn("AppleAgxSchedulerKickedFence(&Context->Scheduler)", entry)
        reset = section(sched, "static __declspec(noinline) NTSTATUS AdmissionResetEngineInternal(")
        self.assertLess(reset.index("kickedAborted = AppleAgxSchedulerKickedFence"),
                        reset.index("reset = AppleAgxSchedulerResetEngine("))
        self.assertLess(reset.index("AdmissionG4PendingAbortKickedLocked(context)"),
                        reset.index("AdmissionG4PendingDropLocked(context, TRUE)"))
        self.assertIn("AdmissionGpuvaG3AbortJob(context, kickedAborted)", reset)
        drop = section(G3.read_text(), "_Use_decl_annotations_ VOID AdmissionG4PendingDropLocked(")
        self.assertIn("entry->State == AdmissionG4PendingKicked", drop)
        unbind = section(G3.read_text(), "_Use_decl_annotations_ VOID AdmissionG4PendingAheadUnbind(")
        self.assertIn("AdmissionG4PendingKicked", unbind)

    def test_new_functions_touch_no_sgx_and_ring_no_doorbell(self):
        text = WORKER.read_text()
        for name in ("static BOOLEAN AdmissionJobWindowKickWindows(",
                     "static BOOLEAN AdmissionJobWindowAdoptWindows(",
                     "static BOOLEAN AdmissionJobWindowNextWindows(",
                     "static APPLE_AGX_BACKEND_BOOL AdmissionBuildLeadBarrier("):
            body = section(text, name)
            for forbidden in ("SgxBase", "READ_REGISTER", "WRITE_REGISTER", "Doorbell",
                              "SendRunMessage", "AppleAgxBackendRuntimeSubmit(&"):
                self.assertNotIn(forbidden, body, name)

    def test_poll_planner_learns_only_depth1_durations(self):
        worker = section(WORKER.read_text(), WORKER_START)
        finish = worker[worker.index("AdmissionJobPollFinish(&runtime->JobPoll,"):]
        finish = finish[:finish.index(";")]
        self.assertIn("kickedBehind || adopted ? 0ULL : pollKey", finish)


WINDOW_DUMP = r'''
#include <stdio.h>
#include "render_job_window.h"
int main(void) {
  ADMISSION_JOB_WINDOW_RECEIPT r;
  const unsigned char *p = (const unsigned char *)&r;
  unsigned i;
  AdmissionJobWindowReceiptInitialize(&r);
  AdmissionJobWindowReceiptKick(&r, 41u, ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE, 2u);
  AdmissionJobWindowReceiptKick(&r, 43u, ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D, 2u);
  AdmissionJobWindowReceiptReported(&r, 1u);
  AdmissionJobWindowReceiptReported(&r, 2u);
  AdmissionJobWindowReceiptReported(&r, 2u);
  r.Adopted = 3u; r.KickFailures = 1u; r.LastKickFailure = 5000u; r.ResetAborts = 1u;
  for (i = 0; i < sizeof(r); ++i) printf("%02x", p[i]);
  putchar('\n');
  return 0;
}
'''


class KickReceipts(unittest.TestCase):
    def test_window_receipt_decodes_the_c_struct(self):
        b = load()
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "dump.c"
            exe = Path(directory) / "dump"
            source.write_text(WINDOW_DUMP)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(RENDER / "include"), str(source),
                            str(RENDER / "src/render_job_window.c"), "-o", str(exe)], check=True)
            raw = bytes.fromhex(subprocess.run([str(exe)], check=True, capture_output=True,
                                               text=True).stdout.strip())
        out = b.kmd_job_window(b"Wom1JobWindow " + raw.hex().encode() + b"\n")
        self.assertEqual(out["kicks"], 2)
        self.assertEqual(out["max_in_flight"], 2)
        self.assertEqual(out["batch_sizes"], {1: 1, 2: 2})
        self.assertEqual(out["barriers"], dict(ta_on_compute=1, ta_on_3d=0, compute_on_3d=1,
                                               compute_on_compute=0))
        self.assertEqual((out["adopted"], out["kick_failures"], out["last_kick_failure_line"],
                          out["reset_aborts"], out["last_kicked_fence"]), (3, 1, 5000, 1, 43))

    def test_v4_wired_statistics(self):
        b = load()
        phases = b.KMD_PHASES
        slots = []
        for i in range(4):
            fence = i + 1
            at = 10000 * (i + 1)
            q = dict(submit=at, worker=at + 10, backend_before=at + 20, kick_3d=at + 300,
                     kick_ta=at + 301 if i & 1 else 0, backend_after=at + 310,
                     first_progress=at + 500, complete=at + 2000, job_end=at + 2050,
                     notify=at + 2100)
            flags = 0x100
            if i in (1, 3):   # kicked behind its predecessor, 600 us before its end
                q['backend_after'] = 10000 * i + 1400
                flags |= b.FLAG_DEPTH2_KICKED | (1 << b.DEPTH2_BARRIER_SHIFT if i == 1 else
                                                 3 << b.DEPTH2_BARRIER_SHIFT)
            if i == 3:
                flags |= b.FLAG_DEPTH2_BATCHED
            slots.append((fence, 9452, q, flags))
        data = bytearray(struct.pack('<IIIIQQ', 4, 32 + 64 * 288, 4, 0, 1000000, 0))
        for i in range(64):
            fence, pid, q, flags = slots[i] if i < 4 else (0, 0, {}, 0)
            data += struct.pack('<IIIIQQ12Q4QII', fence, pid, 0, 0, 0, 0,
                                *[q.get(p, 0) for p in phases], 0, 0, 0, 0, 0, 0)
            data += struct.pack('<IIII', 0, 0, flags, 0)
            data += struct.pack('<13Q', *([0] * 13))
        wired = b.kmd_job_timing(bytes(data))['depth2']['wired']
        self.assertEqual(wired['kicked'], 2)
        self.assertEqual(wired['kicks_while_busy'], 2)
        self.assertEqual(wired['max_in_flight'], 2)
        self.assertEqual(wired['batch_sizes'], {1: 2, 2: 1})
        self.assertEqual(wired['barriers'], dict(compute_on_3d=1, ta_on_compute=1))
        self.assertEqual(wired['kick_lead_us']['median_us']['lead'], 600)


PLAN_PROBE = r"""
#include <stdio.h>
#include <string.h>
#include "apple_agx_g13_queue_provider.h"
static APPLE_AGX_BACKEND_U64 ring[3][64];
static volatile APPLE_AGX_BACKEND_U32 wp[3], dp[3], st[3];
static APPLE_AGX_BACKEND_BOOL fl(void *c, const void *a, APPLE_AGX_BACKEND_U32 b) { (void)c; (void)a; (void)b; return 1; }
static void ba(void *c) { (void)c; }
static APPLE_AGX_BACKEND_BOOL pu(void *c, volatile APPLE_AGX_BACKEND_U32 *a, APPLE_AGX_BACKEND_U32 v) { (void)c; *a = v; return 1; }
static APPLE_AGX_BACKEND_BOOL rd(void *c, const volatile APPLE_AGX_BACKEND_U32 *a, APPLE_AGX_BACKEND_U32 *v) { (void)c; *v = *a; return 1; }
static APPLE_AGX_BACKEND_BOOL se(void *c, APPLE_AGX_BACKEND_U32 q, const unsigned char m[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) { (void)c; (void)q; (void)m; return 1; }
static APPLE_AGX_BACKEND_BOOL qu(void *c, APPLE_AGX_BACKEND_U32 f) { (void)c; (void)f; return 1; }
static APPLE_AGX_BACKEND_BOOL bu(void *c, const APPLE_AGX_BACKEND_JOB_IMAGE *j, APPLE_AGX_BACKEND_U32 f,
                                 APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *s) { (void)c; (void)j; (void)f; (void)s; return 0; }
/* Dirties the stack the way a kernel thread's stack is dirty. */
static void scribble(void) { volatile unsigned char junk[4096]; memset((void *)junk, 0xa5, sizeof(junk)); }
int main(void) {
  static APPLE_AGX_G13_QUEUE_PROVIDER p;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG c;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO io;
  APPLE_AGX_G13_QUEUE_PROVIDER_IO pio;
  APPLE_AGX_BACKEND_IO q;
  APPLE_AGX_G13_QUEUE_JOB_PLAN plan;
  unsigned i;
  memset(&c, 0, sizeof(c)); memset(&io, 0, sizeof(io)); memset(&q, 0, sizeof(q));
  for (i = 0; i < 3; ++i) {
    APPLE_AGX_G13_QUEUE_BINDING *b = i == 0 ? &c.Ta : i == 1 ? &c.D3 : &c.Compute;
    b->QueueType = i; b->QueueInfoGpuAddress = 0x1000ULL * (i + 1); b->RingCpuAddress = ring[i];
    b->RingCapacity = 64u; b->CpuWritePointer = &wp[i]; b->GpuDonePointer = &dp[i];
    b->Stamp = &st[i]; b->EventNumber = 7u + i;
  }
  c.HasCompute = 1; c.TimeoutTicks = 1000ULL;
  io.FlushForDevice = fl; io.MemoryBarrier = ba; io.PublishU32 = pu; io.ReadU32 = rd;
  io.SendRunMessage = se; io.Quiesce = qu;
  pio.Context = &p; pio.BuildSubmission = bu;
  if (!AppleAgxG13QueueProviderInitialize(&p, &c, &io, &pio)) return 2;
  q.Context = &p;
  if (!AppleAgxG13QueueProviderInstall(&q) || !q.Queues.Create(&p)) return 3;
  scribble();
  memset(&plan, 0, sizeof(plan));
  if (!AppleAgxG13QueueProviderPlanJob(&p, &plan)) return 4;
  printf("successor=%u\n", (unsigned)plan.Successor);
  return plan.Successor ? 1 : 0;
}
"""


class PlanIsDeterministic(unittest.TestCase):
    """EXP2039 (0x116 at boot): AppleAgxG13QueueProviderPlanJob left the
    rc102 Successor field of its stack candidate unset and copied it out, so
    the KMD build of an ordinary depth-1 job saw a garbage Successor, tried
    to build a leading Barrier with no running job and failed the first
    submit (backend Ready, provider Created, SchedulerFaulted at the submit
    line). The probe compiles the real provider with uninitialised locals
    filled with a pattern, as a dirty kernel stack would."""

    def test_depth1_plan_is_never_a_successor_plan(self):
        shared = ROOT / "drivers/apple-agx/shared"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "plan.c"
            exe = Path(directory) / "plan"
            source.write_text(PLAN_PROBE)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-ftrivial-auto-var-init=pattern", "-I", str(shared / "include"),
                            str(source), str(shared / "src/apple_agx_g13_codec.c"),
                            str(shared / "src/apple_agx_g13_queue_runtime.c"),
                            str(shared / "src/apple_agx_g13_queue_provider.c"),
                            "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("successor=0", ran.stdout)


if __name__ == "__main__":
    unittest.main()
