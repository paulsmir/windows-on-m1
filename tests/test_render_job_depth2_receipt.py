"""rc95: Wom1JobDepth2 receipt mode (timing v4) -- wiring, sampler, decoder.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The depth-2 window is not wired yet: with Wom1JobDepth2 = 1 the worker only
records (a) the AdmissionJobWindowKickRefusal decision for each job's
Prebound successor, taken right when rc90 prebinds it, with the time of
that bind, and (b) the first poll at which the running job's TA and
compute-prelude done pointers and stamps reached their expected values.
(b) answers U4: the notifier threshold counts every kicked job, so if the
firmware also holds a job's driver stamp until the threshold is reached, a
depth-2 N would be seen only when N+1 ends. The test catches:
  - the switch enabled despite its opt-out or without build-ahead, or a receipt path that
    kicks, publishes or rings anything (receipt mode must not change what
    the firmware gets);
  - a sampler that records a stamp or done pointer before it reached the
    job's expected value, for another fence, or keeps reading after every
    receipt was taken;
  - a decoder that misreads the v4 slot (five Sub stamps appended to v3)
    or the depth-2 refusal byte (AheadRefusals bits 24-31).
"""
from pathlib import Path
import importlib.util
import os
import re
import struct
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"
WORKER = RENDER / "src/backend_platform_windows.c"
TOOL = ROOT / "tools/agx_gpu_frame_breakdown.py"


def load():
    spec = importlib.util.spec_from_file_location("agx_gpu_frame_breakdown", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def section(text, start):
    body = text[text.index(start):]
    return body[:body.index("\n}\n")]


class Depth2Wiring(unittest.TestCase):
    def test_switch_is_default_on_and_needs_build_ahead(self):
        text = WORKER.read_text()
        self.assertRegex(text, r'runtime->JobDepth2 = runtime->JobAhead &&\s*\n\s*'
                               r'!AdmissionPlatformSwitchDisabled\(Context, L"Wom1JobDepth2"\)')
        self.assertNotIn('AdmissionPlatformSwitchEnabled(Context, L"Wom1JobDepth2")', text)
        self.assertRegex(text, r"#else\s*\n\s*runtime->JobAhead = FALSE;\s*\n\s*runtime->JobDepth2 = FALSE;")

    def test_receipt_mode_kicks_nothing(self):
        text = WORKER.read_text()
        for name in ("static VOID AdmissionJobWindowNoteWindows(",
                     "static VOID AdmissionJobDepth2SampleQueue(",
                     "static VOID AdmissionJobDepth2SampleWindows(",
                     "static VOID AdmissionJobWindowDescribeLocked("):
            body = section(text, name)
            for forbidden in ("AppleAgxBackendRuntimeSubmit", "SendRunMessage", "PublishU32",
                              "Doorbell", "RunTa", "Run3d", "RunCompute", "SgxBase",
                              "REGISTER_", "AdmissionG4PendingAheadPromoteLocked",
                              "AppleAgxSchedulerActivateFence"):
                self.assertNotIn(forbidden, body, f"{name} {forbidden}")
        note = section(text, "static VOID AdmissionJobWindowNoteWindows(")
        paging = note.index("KeAcquireSpinLock(&adapter->PagingLock")
        sched = note.index("KeAcquireSpinLockAtDpcLevel(&adapter->SchedulerLock)")
        self.assertLess(paging, sched)
        self.assertLess(sched, note.index("AdmissionJobAheadFillLocked(adapter, &ahead)"))
        self.assertLess(note.index("KeReleaseSpinLock(&adapter->PagingLock"),
                        note.index("AdmissionJobWindowKickRefusal(&s)"))
        self.assertIn("ADMISSION_JOB_AHEAD_SHIFT_DEPTH2", note)
        self.assertIn("ADMISSION_JOB_FLAG_DEPTH2_ELIGIBLE", note)

    def test_decision_and_sampler_are_gated_by_the_switch(self):
        text = WORKER.read_text()
        prebind = section(text, "static VOID AdmissionJobAheadPrebindWindows(")
        self.assertRegex(prebind, r"if \(bound && runtime->JobDiagnostics && runtime->JobDepth2 && !runtime->Depth2Noted\)\s*\n\s*"
                                  r"AdmissionJobWindowNoteWindows\(adapter, runtime, fence\);")
        release = section(text, "static VOID AdmissionJobAheadReleaseWindows(")
        at = release.index("AdmissionJobWindowDescribeLocked(")
        self.assertIn("if (runtime->JobDepth2)", release[at - 120:at])
        self.assertLess(release.index("KeAcquireSpinLock(&adapter->SchedulerLock"), at)
        self.assertLess(at, release.index("KeReleaseSpinLock(&adapter->SchedulerLock"))
        worker = section(text, "static VOID AdmissionPlatformWorker(\n    _In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context) {")
        self.assertEqual(worker.count("AdmissionJobDepth2SampleWindows("), 1)
        at = worker.index("AdmissionJobDepth2SampleWindows(")
        self.assertIn("if (runtime->JobDiagnostics && runtime->JobDepth2)", worker[at - 90:at])
        # inside the poll loop, and the per-job state starts clean
        self.assertLess(worker.index("while (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted"), at)
        reset = worker.index("runtime->Depth2Sampled = 0u;")
        self.assertLess(worker.index("ChainNext:"), reset)
        self.assertLess(reset, worker.index("AdmissionGpuvaG3BeginJob("))


SHIM = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_g13_queue_runtime.h"
#include "render_job_timing.h"
#include "render_job_window.h"
#define VOID void

typedef unsigned int ULONG;
typedef unsigned long long ULONGLONG;
typedef struct {
  struct { struct { APPLE_AGX_G13_QUEUE_RUNTIME Runtime; } QueueProvider; } Provider;
  ULONG Depth2Sampled;
} ADMISSION_PLATFORM_RUNTIME;
static ULONGLONG clock_qpc = 1000u;
static ULONGLONG AdmissionJobQpc(void) { return clock_qpc; }
static ULONGLONG subs[ADMISSION_JOB_SUB_COUNT];
static unsigned sub_calls, reads;
static VOID AdmissionJobTimingSubAtWindows(ADMISSION_PLATFORM_RUNTIME *runtime,
    ULONG fence, ADMISSION_JOB_SUB sub, ULONGLONG qpc) {
  (void)runtime;
  assert(fence == 0x8302u);
  ++sub_calls;
  if (!subs[sub]) subs[sub] = qpc;
}
static APPLE_AGX_BACKEND_BOOL read_u32(void *c, const volatile APPLE_AGX_BACKEND_U32 *a,
                                       APPLE_AGX_BACKEND_U32 *v) {
  (void)c; ++reads; *v = *a; return 1;
}
'''

CASE = r'''
static APPLE_AGX_BACKEND_U32 ta_done, ta_stamp, cm_done, cm_stamp;
static void tick(ADMISSION_PLATFORM_RUNTIME *r) {
  clock_qpc += 10u;
  AdmissionJobDepth2SampleWindows(r, 0x8302u);
}
int main(void) {
  static ADMISSION_PLATFORM_RUNTIME r;
  APPLE_AGX_G13_QUEUE_RUNTIME *q = &r.Provider.QueueProvider.Runtime;
  unsigned before;
  q->Io.ReadU32 = read_u32;
  q->Phase = AppleAgxG13QueueRuntimeSubmitted;
  q->PendingFence = 0x8302u;
  q->Config.Ta.GpuDonePointer = &ta_done; q->Config.Ta.Stamp = &ta_stamp;
  q->Config.Compute.GpuDonePointer = &cm_done; q->Config.Compute.Stamp = &cm_stamp;
  q->Config.HasCompute = 1; q->ComputeInRender = 1;
  q->TaPending.ExpectedDonePointer = 3u; q->TaPending.ExpectedStamp = 0x8300u;
  q->ComputePending.ExpectedDonePointer = 7u; q->ComputePending.ExpectedStamp = 0xffffff00u;
  ta_done = 1u; ta_stamp = 0x8200u; cm_done = 6u; cm_stamp = 0xfffffe00u;
  tick(&r);                                          /* t=1010: nothing reached */
  assert(sub_calls == 0u);
  cm_done = 7u; tick(&r);                            /* t=1020: prelude done */
  cm_stamp = 0x00000000u; tick(&r);                  /* t=1030: stamp wrapped past */
  ta_done = 3u; tick(&r);                            /* t=1040: TA done, stamp held */
  q->PendingFence = 0x8303u; ta_stamp = 0x8300u; tick(&r); /* another fence: ignored */
  q->PendingFence = 0x8302u;
  tick(&r);                                          /* t=1060: TA stamp */
  assert(subs[AdmissionJobSubPreludeDone] == 1020u);
  assert(subs[AdmissionJobSubPreludeStamp] == 1030u);
  assert(subs[AdmissionJobSubTaDone] == 1040u);
  assert(subs[AdmissionJobSubTaStamp] == 1060u);
  assert(sub_calls == 4u && r.Depth2Sampled == 0xfu);
  before = reads; tick(&r); tick(&r);
  assert(reads == before && sub_calls == 4u);        /* all taken: no more reads */
  /* a render without prelude and a standalone compute job */
  memset(subs, 0, sizeof(subs)); sub_calls = 0u; r.Depth2Sampled = 0u;
  q->ComputeInRender = 0;
  ta_done = 2u; ta_stamp = 0x8200u; tick(&r);
  ta_done = 3u; ta_stamp = 0x8300u; tick(&r);
  assert(subs[AdmissionJobSubTaDone] && subs[AdmissionJobSubTaStamp] &&
         !subs[AdmissionJobSubPreludeDone] && !subs[AdmissionJobSubPreludeStamp]);
  assert(r.Depth2Sampled == 0xfu);
  memset(subs, 0, sizeof(subs)); sub_calls = 0u; r.Depth2Sampled = 0u;
  q->ComputeTerminal = 1; q->Phase = AppleAgxG13QueueRuntimeComputeSubmitted;
  tick(&r);
  assert(sub_calls == 0u && r.Depth2Sampled == 0xfu);
  /* nothing while the queue runtime holds no job */
  r.Depth2Sampled = 0u; q->ComputeTerminal = 0; q->Phase = AppleAgxG13QueueRuntimeReady;
  before = reads; tick(&r);
  assert(reads == before && r.Depth2Sampled == 0u);
  puts("DEPTH2_SAMPLE_OK");
  return 0;
}
'''


def define_lines(source, prefix):
    return "".join(line + "\n" for line in source.splitlines()
                   if line.startswith("#define " + prefix))


class Depth2Sampler(unittest.TestCase):
    def test_sampler_records_first_reach_only(self):
        backend = WORKER.read_text()
        body = (SHIM + define_lines(backend, "ADMISSION_DEPTH2_SAMPLED_") +
                function_body(backend, "AdmissionJobDepth2SampleQueue") +
                function_body(backend, "AdmissionJobDepth2SampleWindows") + CASE)
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "sample.c"
            exe = Path(tmp) / "sample"
            source.write_text(body)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-Wno-unused-function",
                                    "-I", str(RENDER / "include"), "-I", str(SHARED / "include"),
                                    str(source), str(RENDER / "src/render_job_window.c"),
                                    "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("DEPTH2_SAMPLE_OK", ran.stdout)


class Depth2Decoder(unittest.TestCase):
    def test_v4_slots_and_depth2_summary(self):
        b = load()
        self.assertEqual(b.KMD_SLOT_V4.size, 288)
        self.assertEqual(b.KMD_SUBS_V4[:8], b.KMD_SUBS)
        phases = b.KMD_PHASES
        slots = []
        for i in range(4):
            at = 10000 * (i + 1)
            q = dict(submit=at, worker=at + 10, backend_before=at + 20, kick_3d=at + 300,
                     kick_ta=at + 301 if i & 1 else 0, backend_after=at + 310,
                     first_progress=at + 500, complete=at + 2000, job_end=at + 2050,
                     notify=at + 2100)
            sub = dict(successor_bound=at + 400)
            flags, ahead = 0x100 | 0x200, 0
            if i & 1:   # render: TA stamp held until the completion
                sub.update(ta_done=at + 900, ta_stamp=at + 1990, prelude_done=at + 600,
                           prelude_stamp=at + 1995)
                ahead = 57 << 24
                if i == 3:  # ... and not seen before the completing poll
                    del sub['ta_stamp']
            else:       # compute -> render: eligible
                flags |= 0x800
            slots.append((i + 1, 9452, q, flags, ahead, sub))
        data = bytearray(struct.pack('<IIIIQQ', 4, 32 + 64 * 288, 4, 0, 1000000, 0))
        for i in range(64):
            fence, pid, q, flags, ahead, sub = slots[i] if i < 4 else (0, 0, {}, 0, 0, {})
            data += struct.pack('<IIIIQQ12Q4QII', fence, pid, 0, 0, 0, 0,
                                *[q.get(p, 0) for p in phases], 0, 0, 0, 0, 0, 0)
            data += struct.pack('<IIII', 0, 0, flags, ahead)
            data += struct.pack('<13Q', *[sub.get(n, 0) for n in b.KMD_SUBS_V4])
        out = b.kmd_job_timing(bytes(data))
        self.assertEqual(out['version'], 4)
        d = out['depth2']
        self.assertEqual(d['eligible'], 2)
        self.assertEqual(d['decided'], 4)
        self.assertEqual(d['refusals'], {57: 2})
        self.assertEqual(d['successor_lead_us']['median_us']['lead'], 1600)
        lag = d['stamp_lag_us']['median_us']
        self.assertEqual(lag['ta_done_to_stamp'], 1090)
        self.assertEqual(lag['ta_stamp_to_complete'], 10)
        self.assertEqual(d['stamp_lag_us']['samples']['ta_done_to_stamp'], 1)
        self.assertEqual(d['stamp_unseen'], dict(ta=1, prelude=0))
        self.assertEqual(lag['prelude_done_to_stamp'], 1395)
        self.assertEqual(lag['prelude_stamp_to_complete'], 5)
        # the rc90 bytes are unchanged by the depth-2 byte
        self.assertEqual(out['ahead_refusals'], dict(release={}, prebind={}, chain={}))


if __name__ == "__main__":
    unittest.main()
