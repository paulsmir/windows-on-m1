"""C1 (Wom1CrossProcessPairs): cross-process pairs in the depth-two window.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The depth-two window kicks job N+1 to the firmware while N runs. Until C1
every pair of two processes was refused (55). C1 admits exactly one
cross-process class: a compute job behind a running plain render
(COMPUTE_ON_3D, the lead Barrier kept), and only when both processes hold
their own, different VM slots. A wider admission is a real defect: two
renders of two processes would share the live buffer-manager binding, a
zero slot means the job would be built for the baseline slot 1 while another
process's translation is live, and an equal slot means one translation for
two address spaces. So:
  X1 with the capability off the decision is today's: every cross-process
     pair is 55 whatever the slots say, and the slots never change the
     decision of a same-process pair;
  X2 with the capability on, a cross-process pair of any class other than
     plain render -> compute stays 55;
  X3 the admitted class with a zero or an equal slot is refused 6 and never
     kicked;
  X4 the admitted pair still obeys every later refusal (idle-queue needs,
     sidecar sets, Barrier availability and kick classes, paging);
  X5 the receipt counts decisions, kicks and refusals by code, so a bench
     run can prove kicked cross-process pairs without the diagnostics ring.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


def build_run(code, sources, includes):
    with tempfile.TemporaryDirectory(prefix='xproc-') as tmp:
        src = Path(tmp) / 'test.c'
        src.write_text(code)
        exe = Path(tmp) / 'test'
        cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', *(f'-I{p}' for p in includes),
               str(src), *map(str, sources), '-o', str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)


DECISION = r'''
#include <stdio.h>
#include <string.h>
#include "render_job_window.h"

#define K_R ADMISSION_JOB_WINDOW_KIND_RENDER
#define K_RP ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE
#define K_C ADMISSION_JOB_WINDOW_KIND_COMPUTE
#define K_P ADMISSION_JOB_WINDOW_KIND_PRESENT
#define K_O ADMISSION_JOB_WINDOW_KIND_OTHER
#define XP ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS
#define BASE_CAPS (ADMISSION_JOB_WINDOW_CAPS_RC98 | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER)

static unsigned failures;
static void expect(const char *what, unsigned got, unsigned want) {
  if (got != want) { printf("FAIL %s: got %u want %u\n", what, got, want); ++failures; }
}

/* A pair every state check lets through: N (fence 10) runs alone, N+1
 * (fence 11) is the Prebound head and the scheduler's next fence. */
static ADMISSION_JOB_WINDOW_KICK_STATE pair(unsigned rk, unsigned nk,
    unsigned long long rp, unsigned long long np, unsigned rs, unsigned ns,
    unsigned caps) {
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  memset(&s, 0, sizeof(s));
  s.Enabled = 1u; s.InFlight = 1u; s.NextPrebound = 1u; s.QueuedFence = 11u;
  s.Capabilities = caps;
  s.General = 1u; s.Prelude = 1u; s.SameManagerSlot = 1u;
  s.Running.Fence = 10u; s.Running.Kind = rk; s.Running.Process = rp;
  s.Running.SceneId = 7ULL; s.Running.Context = rp * 16ULL; s.Running.VmSlot = rs;
  s.Next.Fence = 11u; s.Next.Kind = nk; s.Next.Process = np;
  s.Next.SceneId = 7ULL; s.Next.Context = np * 16ULL; s.Next.VmSlot = ns;
  return s;
}

int main(void) {
  static const unsigned kinds[] = {K_R, K_RP, K_C, K_P, K_O};
  static const unsigned slots[] = {0u, 2u, 3u};
  static const unsigned caps[] = {BASE_CAPS, BASE_CAPS | ADMISSION_JOB_WINDOW_CAP_PRESENT_PAIRS};
  unsigned a, b, c, i, j;
  ADMISSION_JOB_WINDOW_KICK_STATE s;
  ADMISSION_JOB_WINDOW_JOB none;

  /* X1: capability off. */
  for (c = 0; c < 2; ++c)
    for (a = 0; a < 5; ++a)
      for (b = 0; b < 5; ++b) {
        unsigned same0;
        s = pair(kinds[a], kinds[b], 100, 100, 0, 0, caps[c]);
        same0 = AdmissionJobWindowKickRefusal(&s);
        for (i = 0; i < 3; ++i)
          for (j = 0; j < 3; ++j) {
            s = pair(kinds[a], kinds[b], 100, 200, slots[i], slots[j], caps[c]);
            expect("off cross", AdmissionJobWindowKickRefusal(&s), 55u);
            s = pair(kinds[a], kinds[b], 100, 100, slots[i], slots[j], caps[c]);
            expect("off same slots", AdmissionJobWindowKickRefusal(&s), same0);
            /* the capability never changes a same-process decision */
            s = pair(kinds[a], kinds[b], 100, 100, slots[i], slots[j], caps[c] | XP);
            expect("on same", AdmissionJobWindowKickRefusal(&s), same0);
          }
      }
  s = pair(K_R, K_C, 100, 100, 0, 0, BASE_CAPS);
  expect("same R->C kicks", AdmissionJobWindowKickRefusal(&s), 0u);

  /* X2: capability on, every class but plain render -> compute stays 55. */
  for (c = 0; c < 2; ++c)
    for (a = 0; a < 5; ++a)
      for (b = 0; b < 5; ++b) {
        if (kinds[a] == K_R && kinds[b] == K_C) continue;
        s = pair(kinds[a], kinds[b], 100, 200, 2, 3, caps[c] | XP);
        expect("on other class", AdmissionJobWindowKickRefusal(&s), 55u);
      }
  s = pair(K_R, K_C, 0, 200, 2, 3, BASE_CAPS | XP);
  expect("no running process", AdmissionJobWindowKickRefusal(&s), 55u);
  s = pair(K_R, K_C, 100, 0, 2, 3, BASE_CAPS | XP);
  expect("no next process", AdmissionJobWindowKickRefusal(&s), 55u);

  /* The admitted pair. */
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP);
  expect("admitted", AdmissionJobWindowKickRefusal(&s), 0u);
  expect("admitted barrier", AdmissionJobWindowBarrierEx(s.Running.Kind, s.Next.Kind,
      s.Capabilities), ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D);

  /* X3: zero or equal slots. */
  s = pair(K_R, K_C, 100, 200, 0, 3, BASE_CAPS | XP);
  expect("running slot 0", AdmissionJobWindowKickRefusal(&s), 6u);
  s = pair(K_R, K_C, 100, 200, 2, 0, BASE_CAPS | XP);
  expect("next slot 0", AdmissionJobWindowKickRefusal(&s), 6u);
  s = pair(K_R, K_C, 100, 200, 0, 0, BASE_CAPS | XP);
  expect("both slots 0", AdmissionJobWindowKickRefusal(&s), 6u);
  s = pair(K_R, K_C, 100, 200, 3, 3, BASE_CAPS | XP);
  expect("equal slots", AdmissionJobWindowKickRefusal(&s), 6u);
  expect("named", ADMISSION_JOB_WINDOW_REFUSAL_CROSS_PROCESS_INELIGIBLE, 6u);

  /* X4: later refusals still apply to the admitted pair. */
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP); s.NextNeedsIdle = 1u;
  expect("needs idle", AdmissionJobWindowKickRefusal(&s), 61u);
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP);
  s.SetsValid = 1u; s.RunningSet = 1u; s.NextSet = 1u;
  expect("same set", AdmissionJobWindowKickRefusal(&s), 62u);
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP);
  s.KickClasses = 1u << ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE;
  expect("class off", AdmissionJobWindowKickRefusal(&s), 64u);
  s = pair(K_R, K_C, 100, 200, 2, 3,
           (BASE_CAPS & ~ADMISSION_JOB_WINDOW_CAP_COMPUTE_BARRIER) | XP);
  expect("no compute barrier", AdmissionJobWindowKickRefusal(&s), 60u);
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP); s.PagingPending = 1u;
  expect("paging", AdmissionJobWindowKickRefusal(&s), 54u);
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP); s.PreemptionPhase = 1u;
  expect("preemption", AdmissionJobWindowKickRefusal(&s), 4u);
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP); s.WindowPoisoned = 1u;
  expect("poisoned", AdmissionJobWindowKickRefusal(&s), 51u);

  /* The pair classifier the receipt uses. */
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS);
  expect("cross", AdmissionJobWindowCrossProcess(&s.Running, &s.Next), 1u);
  s = pair(K_R, K_C, 100, 100, 2, 2, BASE_CAPS);
  expect("same", AdmissionJobWindowCrossProcess(&s.Running, &s.Next), 0u);
  memset(&none, 0, sizeof(none));
  expect("none", AdmissionJobWindowCrossProcess(&none, &s.Next), 0u);
  expect("null", AdmissionJobWindowCrossProcess(0, &s.Next), 0u);

  /* OV2 and OV5 never waive a cross-process pair. */
  s = pair(K_R, K_C, 100, 200, 2, 3, BASE_CAPS | XP); s.Next.Independent = 1u;
  s.Next.Context = s.Running.Context;
  expect("ov2 cross", AdmissionJobWindowOv2Refusal(1u,
      ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D, &s.Running, &s.Next),
      ADMISSION_JOB_WINDOW_OV2_CONTEXT);

  printf("failures %u\n", failures);
  return failures != 0u;
}
'''

RECEIPT = r'''
#include <stdio.h>
#include <string.h>
#include "render_job_window.h"
int main(void) {
  ADMISSION_JOB_WINDOW_XPROC_RECEIPT r;
  ADMISSION_JOB_WINDOW_JOB n, m;
  unsigned i, due = 0u, code55 = 0u, code6 = 0u, sum = 0u;
  memset(&n, 0, sizeof(n)); memset(&m, 0, sizeof(m));
  n.Fence = 10u; n.VmSlot = 2u; m.Fence = 11u; m.VmSlot = 3u;
  memset(&r, 0xff, sizeof(r));
  AdmissionJobWindowXprocReceiptInitialize(&r);
  printf("init %u %u %u\n", r.Version, r.Bytes == sizeof(r), r.Considered);
  /* the first 16 decisions are exported, then every 256th */
  for (i = 0; i < 300u; ++i)
    due += AdmissionJobWindowXprocNote(&r, i % 3u == 0u ? 0u : i % 3u == 1u ? 55u : 6u, &n, &m);
  printf("due %u considered %u allowed %u\n", due, r.Considered, r.Allowed);
  for (i = 0; i < ADMISSION_JOB_WINDOW_XPROC_CODES; ++i) {
    if (r.RefusalCode[i] == 55u) code55 = r.RefusalCount[i];
    if (r.RefusalCode[i] == 6u) code6 = r.RefusalCount[i];
    sum += r.RefusalCount[i];
  }
  printf("codes 55=%u 6=%u sum %u other %u last %u %u\n", code55, code6, sum,
         r.RefusalOther, r.LastRefusal, r.LastRefusedFence);
  /* more distinct codes than the table holds go to RefusalOther */
  for (i = 0; i < 12u; ++i) (void)AdmissionJobWindowXprocNote(&r, 100u + i, &n, &m);
  sum = 0u;
  for (i = 0; i < ADMISSION_JOB_WINDOW_XPROC_CODES; ++i) sum += r.RefusalCount[i];
  printf("overflow table %u other %u considered %u\n", sum, r.RefusalOther, r.Considered);
  AdmissionJobWindowXprocKicked(&r, &n, &m);
  AdmissionJobWindowXprocKicked(&r, &n, &m);
  AdmissionJobWindowXprocUndone(&r, 1u, 11u, 0x10u);
  AdmissionJobWindowXprocUndone(&r, 0u, 12u, 0x20u);
  printf("kicked %u fellback %u failed %u last %u %u %u %u undone %u %u\n", r.Kicked,
         r.FellBack, r.Failed, r.LastFence, r.LastRunningFence, r.LastRunningSlot,
         r.LastNextSlot, r.LastUndoneFence, r.LastUndoneDetail);
  /* null receipts and jobs are ignored */
  printf("null %u\n", AdmissionJobWindowXprocNote(0, 0u, &n, &m) +
                      AdmissionJobWindowXprocNote(&r, 0u, 0, &m));
  AdmissionJobWindowXprocKicked(0, &n, &m);
  AdmissionJobWindowXprocUndone(0, 1u, 1u, 1u);
  return 0;
}
'''


class CrossProcessPairDecision(unittest.TestCase):
    def test_decision(self):
        run = build_run(DECISION, [RENDER / 'src/render_job_window.c'], [RENDER / 'include'])
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(run.stdout.strip(), 'failures 0')

    def test_receipt(self):
        run = build_run(RECEIPT, [RENDER / 'src/render_job_window.c'], [RENDER / 'include'])
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(run.stdout.split('\n')[:-1], [
            'init 1 1 0',
            'due 17 considered 300 allowed 100',
            'codes 55=100 6=100 sum 200 other 0 last 6 11',
            'overflow table 206 other 6 considered 312',
            'kicked 2 fellback 1 failed 1 last 11 10 2 3 undone 12 32',
            'null 0',
        ])

    def test_refusal_number_is_free(self):
        """6 must not collide with a kick refusal or a decision reason."""
        header = (RENDER / 'include/render_job_window.h').read_text()
        self.assertIn('#define ADMISSION_JOB_WINDOW_REFUSAL_CROSS_PROCESS_INELIGIBLE 6u', header)
        source = (RENDER / 'src/render_job_window.c').read_text()
        body = source[source.index('unsigned int AdmissionJobWindowKickRefusal('):]
        body = body[:body.index('\n}\n')]
        self.assertNotIn('return 6u', body)
        self.assertEqual(body.count('ADMISSION_JOB_WINDOW_REFUSAL_CROSS_PROCESS_INELIGIBLE'), 1)
        # the decision histogram keeps kick refusals below its own reasons
        rc = (RENDER / 'include/render_rc_progress.h').read_text()
        self.assertIn('66 no pending head', rc)


if __name__ == '__main__':
    unittest.main()
