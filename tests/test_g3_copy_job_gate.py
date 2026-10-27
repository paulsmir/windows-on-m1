"""RC68: the KMD copy escape waits for a GPU job only when the job may use the slot.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1986/RC66: every QUERY
waited until this process had no job in flight (gpuva_g3_windows.c R157 loop,
predicates 42-44), so the first copy escape of each submission drained the
GPU (0.5-1.3 ms, 26 submissions per Execute). RC68 lets a QUERY/UPLOAD/
DOWNLOAD run beside the caller's own job when the UMD proves no submission of
its device that may still run holds the slot. The invariant this test pins:
whenever the gate lets an UPLOAD/DOWNLOAD run while a job is active, no
incomplete submission references that slot. In particular a job in flight
reading the BO while an UPLOAD writes it must still wait: an unproven request,
a proof for a slot an unretired submission holds without its fence reached, a
job of another context, or a presentation allocation all wait. The race is
modelled with the production helpers of apple_agx_g3_copy_gate.h; the source
checks pin where the KMD and UMD use them.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx"
GATE = DRIVER / "shared/include/apple_agx_g3_copy_gate.h"
KMD = DRIVER / "render-admission/src/gpuva_g3_windows.c"

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_g3_copy_gate.h"
enum { Q = APPLE_AGX_G3_COPY_QUERY, U = APPLE_AGX_G3_COPY_UPLOAD, D = APPLE_AGX_G3_COPY_DOWNLOAD };
#define F APPLE_AGX_G3_COPY_FLAG_UNREFERENCED
#define IDLE APPLE_AGX_G3_COPY_GATE_IDLE
#define OVERLAP APPLE_AGX_G3_COPY_GATE_OVERLAP
#define WAIT APPLE_AGX_G3_COPY_GATE_WAIT

/* UMD slot/fence bookkeeping as umd_gpuva_windows.c keeps it: make_resident
 * holds (CopyHeld, ++SubmissionHolds), submit queues (CopyHeld=0,
 * LastUseFence=fence), the winsys retires after the fence (--holds). One
 * context completes in fence order. */
#define SLOTS 4
#define SUBS 64
typedef struct { unsigned holds; int held; unsigned long long last; } SLOT;
typedef struct { unsigned long long fence; unsigned mask; int retired; } SUB;
static SLOT slot[SLOTS]; static SUB sub[SUBS]; static unsigned nsub, completed_idx;
static unsigned long long completed_fence, seen_fence; /* GPU truth, UMD view */
static unsigned rng = 12345u;
static unsigned rnd(unsigned n) { rng = rng * 1103515245u + 12345u; return (rng >> 16) % n; }
static int referenced_by_incomplete(unsigned s) {
  for (unsigned i = 0; i < nsub; ++i)
    if (sub[i].fence > completed_fence && (sub[i].mask & (1u << s))) return 1;
  return 0;
}
static int active_job(void) { /* oldest incomplete submission runs */
  for (unsigned i = 0; i < nsub; ++i) if (sub[i].fence > completed_fence) return (int)i;
  return -1;
}
static void wait_fence(unsigned long long f) { /* the GPU reaches f, the UMD sees it */
  if (f > completed_fence) completed_fence = f;
  if (f > seen_fence) seen_fence = f;
  while (completed_idx < nsub && sub[completed_idx].fence <= completed_fence) ++completed_idx;
}
static unsigned proof(unsigned s, int terminal) {
  return AppleAgxG3CopySlotUnreferenced(slot[s].holds, slot[s].held, 0u, 0, 0, terminal,
      slot[s].last, seen_fence) ? F : 0u;
}
/* One upload of a held slot exactly as transfer_slot/transfer_heap_slot do
 * it: wait LastUseFence when another submission holds it, then the proof,
 * then the KMD gate against the own active job. */
static void checked_upload(unsigned s, int skip_wait) {
  if (!skip_wait && slot[s].holds > 1u && slot[s].last) wait_fence(slot[s].last);
  const unsigned flags = proof(s, 0);
  const int job = active_job();
  const unsigned gate = AppleAgxG3CopyJobGate(U, flags, job >= 0, 1, 0);
  if (gate != WAIT) assert(!referenced_by_incomplete(s));
  if (job < 0) assert(gate == IDLE);
}

static void gate_table(void) {
  /* No job of this process: nothing to wait for, proof or not. */
  for (unsigned op = Q; op <= D; ++op) {
    assert(AppleAgxG3CopyJobGate(op, 0u, 0, 0, 0) == IDLE);
    assert(AppleAgxG3CopyJobGate(op, F, 0, 1, 1) == IDLE);
  }
  /* Own job + proof: QUERY, UPLOAD and DOWNLOAD run beside it. */
  for (unsigned op = Q; op <= D; ++op) assert(AppleAgxG3CopyJobGate(op, F, 1, 1, 0) == OVERLAP);
  for (unsigned op = Q; op <= D; ++op) {
    assert(AppleAgxG3CopyJobGate(op, 0u, 1, 1, 0) == WAIT);  /* no proof: R157 */
    assert(AppleAgxG3CopyJobGate(op, F, 1, 0, 0) == WAIT);   /* another context's job */
    assert(AppleAgxG3CopyJobGate(op, F, 1, 1, 1) == WAIT);   /* presentation allocation */
  }
  assert(AppleAgxG3CopyJobGate(3u, F, 1, 1, 0) == WAIT);     /* unknown operation */
  assert(AppleAgxG3CopyJobGate(U, APPLE_AGX_G3_COPY_RESULT_OVERLAPPED | APPLE_AGX_G3_COPY_RESULT_WAITED,
                               1, 1, 0) == WAIT);           /* an echoed outcome is no proof */
  assert(!(APPLE_AGX_G3_COPY_RESERVED_MASK & ~0x301u) && (APPLE_AGX_G3_COPY_RESERVED_MASK & F));
}

static void proof_table(void) {
  /* Only the hold being built: no other submission may use the slot. */
  assert(AppleAgxG3CopySlotUnreferenced(1u, 1, 0u, 0, 0, 0, 0ull, 0ull));
  assert(AppleAgxG3CopySlotUnreferenced(0u, 0, 0u, 0, 0, 0, 0ull, 0ull));
  /* Another unretired holder: proven only once its fence was seen reached. */
  assert(!AppleAgxG3CopySlotUnreferenced(2u, 1, 0u, 0, 0, 0, 7ull, 6ull));
  assert(AppleAgxG3CopySlotUnreferenced(2u, 1, 0u, 0, 0, 0, 7ull, 7ull));
  assert(!AppleAgxG3CopySlotUnreferenced(2u, 1, 0u, 0, 0, 0, 0ull, ~0ull));
  /* An unheld slot with one queued holder (FinishDirectMap-like path that
   * skips the wait because holds == 1) is not proven. */
  assert(!AppleAgxG3CopySlotUnreferenced(1u, 0, 0u, 0, 0, 0, 9ull, 8ull));
  /* Malformed: held without its own hold. */
  assert(!AppleAgxG3CopySlotUnreferenced(0u, 1, 0u, 0, 0, 0, 0ull, 0ull));
  /* Users outside the holds, or a terminal device: never proven. */
  assert(!AppleAgxG3CopySlotUnreferenced(1u, 1, 1u, 0, 0, 0, 0ull, 0ull));
  assert(!AppleAgxG3CopySlotUnreferenced(1u, 1, 0u, 1, 0, 0, 0ull, 0ull));
  assert(!AppleAgxG3CopySlotUnreferenced(1u, 1, 0u, 0, 1, 0, 0ull, 0ull));
  assert(!AppleAgxG3CopySlotUnreferenced(1u, 1, 0u, 0, 0, 1, 0ull, 0ull));
}

/* The RC68 race: submission 1 (in flight) reads slot 0; submission 2 is
 * being built with slot 0 dirty. Without the LastUseFence wait the UPLOAD
 * would carry no proof and must wait for the job; with it, it runs. */
static void race_case(void) {
  memset(slot, 0, sizeof(slot)); memset(sub, 0, sizeof(sub)); nsub = 0;
  completed_fence = seen_fence = 0;
  slot[0].held = 1; ++slot[0].holds;                       /* hold set of job 1 */
  sub[nsub++] = (SUB){1ull, 1u, 0}; slot[0].held = 0; slot[0].last = 1ull;
  slot[0].held = 1; ++slot[0].holds;                       /* job 2 holds it too */
  assert(active_job() == 0 && referenced_by_incomplete(0));
  unsigned flags = proof(0, 0);
  assert(!flags);
  assert(AppleAgxG3CopyJobGate(U, flags, 1, 1, 0) == WAIT);
  assert(AppleAgxG3CopyJobGate(Q, flags, 1, 1, 0) == WAIT);
  wait_fence(slot[0].last);                                /* transfer_slot's wait */
  assert(!referenced_by_incomplete(0));
  flags = proof(0, 0);
  assert(flags == F && AppleAgxG3CopyJobGate(U, flags, active_job() >= 0, 1, 0) == IDLE);
  /* Slot 1 is used only by the new submission while job 1 still runs:
   * the drain RC66 measured is gone. */
  memset(slot, 0, sizeof(slot)); memset(sub, 0, sizeof(sub)); nsub = 0;
  completed_fence = seen_fence = 0;
  slot[0].held = 1; ++slot[0].holds; sub[nsub++] = (SUB){1ull, 1u, 0};
  slot[0].held = 0; slot[0].last = 1ull;
  slot[1].held = 1; ++slot[1].holds;
  flags = proof(1, 0);
  assert(flags == F && active_job() == 0);
  assert(AppleAgxG3CopyJobGate(Q, flags, 1, 1, 0) == OVERLAP);
  assert(AppleAgxG3CopyJobGate(U, flags, 1, 1, 0) == OVERLAP);
  assert(!referenced_by_incomplete(1));
  /* The same slot while another context's job runs still waits. */
  assert(AppleAgxG3CopyJobGate(U, flags, 1, 0, 0) == WAIT);
}

/* Random interleavings of hold/upload/submit/complete/retire; up to
 * ADMISSION_UMD_IN_FLIGHT_SETS - 1 = 3 unretired submissions. */
static void random_model(void) {
  for (unsigned round = 0; round < 2000u; ++round) {
    memset(slot, 0, sizeof(slot)); memset(sub, 0, sizeof(sub));
    nsub = 0; completed_idx = 0; completed_fence = seen_fence = 0;
    unsigned long long next = 1ull; unsigned retired = 0;
    for (unsigned step = 0; step < 40u && nsub < SUBS; ++step) {
      /* GPU progress, sometimes without the UMD looking. */
      if (completed_idx < nsub && rnd(3) == 0) completed_fence = sub[completed_idx++].fence;
      /* Retire the oldest after waiting its fence (winsys). */
      if (retired < nsub && (nsub - retired >= 3u || rnd(3) == 0)) {
        wait_fence(sub[retired].fence);
        for (unsigned s = 0; s < SLOTS; ++s)
          if (sub[retired].mask & (1u << s)) --slot[s].holds;
        sub[retired++].retired = 1;
      }
      /* Build the next submission: hold, upload dirty held slots, queue. */
      unsigned mask = 1u + rnd((1u << SLOTS) - 1u);
      for (unsigned s = 0; s < SLOTS; ++s)
        if (mask & (1u << s)) { slot[s].held = 1; ++slot[s].holds; }
      for (unsigned s = 0; s < SLOTS; ++s)
        if ((mask & (1u << s)) && rnd(2)) checked_upload(s, 0);
      sub[nsub++] = (SUB){next, mask, 0};
      for (unsigned s = 0; s < SLOTS; ++s)
        if (slot[s].held) { slot[s].held = 0; slot[s].last = next; }
      ++next;
      /* An unheld upload path that skipped the wait never runs beside a
       * job that uses the slot either. */
      checked_upload(rnd(SLOTS), 1);
    }
  }
}

int main(void) {
  gate_table();
  proof_table();
  race_case();
  random_model();
  puts("copy job gate PASS");
  return 0;
}
'''


def kmd_function(text, name):
    m = re.search(r"(?m)^(?:static )?(?:NTSTATUS|BOOLEAN) " + name + r"\(", text)
    start = text.index("{", m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}"); end += 1
    return text[m.start():end]


class CopyJobGate(unittest.TestCase):
    def test_race_model_with_production_helpers(self):
        self.assertTrue(GATE.is_file(), "apple_agx_g3_copy_gate.h missing")
        with tempfile.TemporaryDirectory(prefix="g3-copy-job-gate-") as path:
            src = Path(path) / "gate.c"
            exe = Path(path) / "gate"
            src.write_text(HARNESS)
            build = subprocess.run([
                "clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(DRIVER / "shared/include"), str(src), "-o", str(exe),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("copy job gate PASS", run.stdout)

    def test_kmd_copy_escape_uses_the_gate(self):
        text = KMD.read_text()
        self.assertIn('#include "apple_agx_g3_copy_gate.h"', text)
        body = kmd_function(text, "AdmissionGpuvaG3CopyEscape")
        self.assertIn("(q->Reserved&~APPLE_AGX_G3_COPY_RESERVED_MASK) || q->Reserved2", body)
        loop = body[body.index("KeClearEvent(&state->JobEvent);"):]
        loop = loop[:loop.index("KeWaitForSingleObject(&state->JobEvent")]
        self.assertIn("AppleAgxG3CopyJobGate(", loop)
        self.assertIn("state->ActiveContext==(ADMISSION_RENDER_CONTEXT *)args->hContext", loop)
        self.assertIn("gate!=APPLE_AGX_G3_COPY_GATE_WAIT", loop)
        # The final, same-lock decision knows the allocation: presentation waits.
        final = body[body.index("41u, STATUS_INVALID_PARAMETER, Unlock);"):body.index("62u, STATUS_DEVICE_BUSY")]
        self.assertIn("allocation->Presentation!=0u", final)
        for predicate in ("42u", "43u", "44u"):
            line = next(l for l in final.splitlines() if f", {predicate}, STATUS_DEVICE_BUSY" in l)
            self.assertIn("gate==APPLE_AGX_G3_COPY_GATE_WAIT &&", line)
        self.assertLess(body.index("q->Reserved=outcome;"),
                        body.index("RtlCopyMemory(args->pPrivateDriverData,q,sizeof(*q));status=STATUS_SUCCESS;"))

    def test_active_context_follows_active_process(self):
        text = KMD.read_text()
        begin = kmd_function(text, "AdmissionGpuvaG3BeginJob")
        self.assertIn("state->ActiveProcess = process;\n    state->ActiveContext = context;", begin)
        complete = kmd_function(text, "AdmissionGpuvaG3CompleteJobTimestamp")
        self.assertIn("state->ActiveProcess = NULL;\n    state->ActiveContext = NULL;", complete)
        self.assertEqual(text.count("state->ActiveProcess = NULL;"), 1)


if __name__ == "__main__":
    unittest.main()
