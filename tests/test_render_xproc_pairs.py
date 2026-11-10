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

SPLIT = r'''
#include <stdio.h>
#include <string.h>
#include "render_job_window.h"
int main(void) {
  static const unsigned kinds[] = {ADMISSION_JOB_WINDOW_KIND_RENDER,
      ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE, ADMISSION_JOB_WINDOW_KIND_COMPUTE,
      ADMISSION_JOB_WINDOW_KIND_PRESENT, ADMISSION_JOB_WINDOW_KIND_OTHER};
  static const unsigned long long procs[][2] = {{100, 200}, {0, 200}, {100, 0}, {0, 0}, {100, 100}};
  static const unsigned caps[] = {ADMISSION_JOB_WINDOW_CAPS_RC98 | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER,
      ADMISSION_JOB_WINDOW_CAPS_RC98 | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER |
      ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS};
  ADMISSION_JOB_WINDOW_XPROC_RECEIPT r;
  unsigned a, b, c, p, i, j, refusals = 0, other = 0, cells = 0, kindcells = 0;
  AdmissionJobWindowXprocReceiptInitialize(&r);
  /* Every decision of a small state space; a 55 is noted, nothing else. */
  for (c = 0; c < 2; ++c) for (p = 0; p < 5; ++p) for (a = 0; a < 5; ++a) for (b = 0; b < 5; ++b) {
    ADMISSION_JOB_WINDOW_KICK_STATE s;
    unsigned refusal;
    if (c == 0 && p != 0) continue; /* capability off: one process pair is enough */
    memset(&s, 0, sizeof(s));
    s.Enabled = 1u; s.InFlight = 1u; s.NextPrebound = 1u; s.QueuedFence = 11u; s.Capabilities = caps[c];
    s.General = 1u; s.Prelude = 1u; s.SameManagerSlot = 1u;
    s.Running.Fence = 10u; s.Running.Kind = kinds[a]; s.Running.Process = procs[p][0]; s.Running.VmSlot = 2u;
    s.Next.Fence = 11u; s.Next.Kind = kinds[b]; s.Next.Process = procs[p][1]; s.Next.VmSlot = 3u;
    refusal = AdmissionJobWindowKickRefusal(&s);
    if (refusal == 55u) { ++refusals; AdmissionJobWindowXprocNote55(&r, s.Capabilities, &s.Running, &s.Next); }
    else if (p != 4 && !(c == 1 && p == 0 && a == 0 && b == 2)) ++other; /* only the admitted pair escapes 55 */
  }
  for (i = 0; i < 4; ++i) for (j = 0; j < 4; ++j) kindcells += r.Refused55Kinds[i][j];
  cells = r.Refused55CapabilityOff + r.Refused55NoProcess + kindcells;
  printf("cells %u refusals %u other %u\n", cells, refusals, other);
  /* off: 25 kind pairs; on: 3 x 25 with a job without a process, and the 25
   * cross-process kind pairs less the admitted one */
  printf("off %u noprocess %u kinds %u\n", r.Refused55CapabilityOff, r.Refused55NoProcess, kindcells);
  printf("admitted cell %u\n", r.Refused55Kinds[ADMISSION_JOB_WINDOW_XPROC_KIND_RENDER]
                                                [ADMISSION_JOB_WINDOW_XPROC_KIND_COMPUTE]);
  printf("classes %u %u %u %u %u %u\n", AdmissionJobWindowXprocKindClass(kinds[0]),
         AdmissionJobWindowXprocKindClass(kinds[1]), AdmissionJobWindowXprocKindClass(kinds[2]),
         AdmissionJobWindowXprocKindClass(kinds[3]), AdmissionJobWindowXprocKindClass(kinds[4]),
         AdmissionJobWindowXprocKindClass(ADMISSION_JOB_WINDOW_KIND_NONE));
  AdmissionJobWindowXprocNote55(0, 0u, 0, 0);
  AdmissionJobWindowXprocNote55(&r, 0u, 0, 0);
  printf("after null %u\n", r.Refused55CapabilityOff);
  puts("null ok");
  return 0;
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
            'init 4 1 0',   # v4 (K1) appends words; the v3 words keep their place
            'due 17 considered 300 allowed 100',
            'codes 55=100 6=100 sum 200 other 0 last 6 11',
            'overflow table 206 other 6 considered 312',
            'kicked 2 fellback 1 failed 1 last 11 10 2 3 undone 12 32',
            'null 0',
        ])

    def test_refusal_55_split(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The split
        decides which cross-process class is worth admitting next. It must
        put every 55 of the kick decision in exactly one cell, by the same
        order of causes as the decision, and nothing that is not a 55."""
        run = build_run(SPLIT, [RENDER / 'src/render_job_window.c'], [RENDER / 'include'])
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(run.stdout.strip().split('\n'), [
            'cells 124 refusals 124 other 0',
            'off 25 noprocess 75 kinds 24',
            'admitted cell 0',
            'classes 0 1 2 3 3 3',
            'after null 25',
            'null ok'])

    def test_refusal_55_split_is_receipt_only(self):
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        self.assertEqual(backend.count('AdmissionJobWindowXprocNote55('), 1)
        at = backend.index('AdmissionJobWindowXprocNote55(')
        guard = backend[backend.rindex('if (', 0, at):at]
        self.assertEqual(guard.strip(), 'if (runtime->CrossProcessPairs && refusal == 55u)')
        self.assertGreater(at, backend.index('refusal = AdmissionJobWindowKickRefusal(&s);',
                                             backend.index('static BOOLEAN AdmissionJobWindowKickWindows(')))
        source = (RENDER / 'src/render_job_window.c').read_text()
        decision = source[source.index('unsigned int AdmissionJobWindowKickRefusal('):]
        decision = decision[:decision.index('\n}\n')]
        self.assertNotIn('Receipt', decision)

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


def function(text, name):
    """The body of the function definition `name(` (first definition)."""
    at = 0
    while True:
        at = text.index(name + '(', at)
        brace, semi = text.find('{', at), text.find(';', at)
        if brace != -1 and (semi == -1 or brace < semi):
            break
        at += len(name)
    depth, i = 0, brace
    while True:
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
        if depth == 0:
            return text[at:i]


class CrossProcessPairWiring(unittest.TestCase):
    """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The capability being
    declared without its prerequisites or by default; a cross-process
    successor begun on the running job's lease, with a LEASE while N runs,
    or built in another slot than the decision saw; a completion or an
    abort that ends the wrong process's graph job; a receipt that is not
    written with the diagnostics off."""

    @classmethod
    def setUpClass(cls):
        cls.backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        cls.g3 = (RENDER / 'src/gpuva_g3_windows.c').read_text()

    def test_switch_is_opt_in_with_prerequisites(self):
        text = self.backend
        at = text.index('runtime->CrossProcessPairs = runtime->Depth2General')
        stmt = text[at:text.index(';', at)]
        for term in ('runtime->JobDepth2Kick', 'Context->G3RetainedSlots != 0u',
                     '1u << ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D',
                     'AdmissionPlatformSwitchEnabled(Context, L"Wom1CrossProcessPairs")'):
            self.assertIn(term, stmt)
        self.assertEqual(text.count('L"Wom1CrossProcessPairs"'), 1)
        self.assertEqual(text.count('|= ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS'), 1)
        after = text[at:at + 700]
        self.assertIn('if (runtime->CrossProcessPairs)\n'
                      '    runtime->Depth2Capabilities |= ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS;',
                      after)
        # the G3 side follows the same decision, on every start
        self.assertIn('Context->G3CrossProcessPairs = runtime->CrossProcessPairs ? 1u : 0u;', text)
        self.assertEqual(text.count('G3CrossProcessPairs ='), 1)
        # the retained-slot switch is read before it
        self.assertLess(text.index('Context->G3RetainedSlots =\n'), at)

    def test_kick_names_and_reproves_the_successor_slot(self):
        kick = function(self.backend, 'AdmissionJobWindowKickWindows')
        slot = kick.index('pendingSlot = AdmissionGpuvaG3RetainedSlot(adapter, pendingContext);')
        locked = kick.index('KeAcquireSpinLock(&adapter->PagingLock, &old_irql);')
        fill = kick.index('s.Next.VmSlot = pendingSlot;')
        decide = kick.index('refusal = AdmissionJobWindowKickRefusal(&s);')
        begin = kick.index('AdmissionGpuvaG3BeginSuccessorJob(adapter, context, next,')
        reproof = kick.index('if (cross && (image->JobVmSlot == 0u || image->JobVmSlot != s.Next.VmSlot ||')
        submit = kick.index('AppleAgxBackendRuntimeSubmitSuccessor(&runtime->Backend, &submission)')
        kicked = kick.index('AdmissionJobWindowXprocKicked(&runtime->XprocReceipt, &s.Running, &s.Next);')
        self.assertLess(slot, locked)  # the graph lock is taken at PASSIVE, before the spin locks
        self.assertLess(locked, fill)
        self.assertLess(fill, decide)
        self.assertLess(decide, begin)
        self.assertLess(begin, reproof)
        self.assertLess(reproof, submit)
        self.assertLess(submit, kicked)
        guard = kick[kick.rindex('if (', 0, fill):fill]
        self.assertIn('runtime->CrossProcessPairs', guard)
        self.assertIn('head->Fence == pendingFence', guard)
        self.assertIn('head->Context == pendingContext', guard)
        tail = kick[reproof:submit]
        self.assertIn('image->JobVmSlot == s.Running.VmSlot', tail)
        self.assertIn('AdmissionGpuvaG3AbortJob(adapter, next);', tail)
        # every decided-but-undone exit is counted (five exits before the hand-over)
        self.assertEqual(kick.count('AdmissionXprocUndoneWindows(runtime, cross, fallbacksBefore, next,'), 6)
        # retried refusals are not counted as decisions
        note = kick[kick.index('cross = runtime->CrossProcessPairs &&'):]
        note = note[:note.index('AdmissionJobWindowXprocNote(') + 40]
        for code in ('4u', '52u', '53u', '54u'):
            self.assertIn('refusal != ' + code, note)

    def test_describe_and_adoption_carry_the_job_slot(self):
        describe = function(self.backend, 'AdmissionJobWindowDescribeLocked')
        self.assertIn('job->VmSlot = image->BoundFence == fence ? image->JobVmSlot : 0u;', describe)
        worker = function(self.backend, 'AdmissionPlatformWorker')
        at = worker.index('if (adopted && runtime->CrossProcessPairs && adapter->G3RetainedSlots &&')
        self.assertLess(at, worker.index('goto AdoptedKicked;'))
        self.assertIn('runtime->RunningVmSlot = adapter->BackendImage.JobVmSlot ?',
                      worker[at:at + 300])

    def test_g3_begins_ends_and_aborts_on_the_job_own_process(self):
        begin = function(self.g3, 'AdmissionGpuvaG3BeginJobAs')
        cross = begin[begin.index('cross = successor && adapter->G3CrossProcessPairs != 0u &&'):]
        self.assertIn('state->ActiveProcess != NULL && state->ActiveProcess != process',
                      cross[:200])
        lease = begin.index('cross ? AppleAgxGpuvaG3GraphBeginLeasedJob(&process->Graph) :')
        r10 = begin.index('refusal = 10u;')
        r11 = begin.index('refusal = 11u;')
        self.assertLess(r10, r11)
        self.assertLess(r11, lease)
        self.assertIn('!adapter->BackendImage.G4Compute', begin[:r10][-200:])
        guard = begin[r10:r11]
        for term in ('process->Graph.LeaseToken == 0ULL', 'process->Graph.JobInFlight',
                     '!state->ActiveProcess->Graph.JobInFlight',
                     'process->Graph.Slot == state->ActiveProcess->Graph.Slot'):
            self.assertIn(term, guard)
        self.assertIn('state->SuccessorProcess = cross ? process : NULL;', begin)
        # without the switch the successor check is the old one
        self.assertIn('(cross || state->ActiveProcess == process) &&', begin)

        complete = function(self.g3, 'AdmissionGpuvaG3CompleteJobTimestamp')
        promote = complete.index('state->ActiveProcess = state->SuccessorProcess;')
        self.assertLess(complete.index('AppleAgxGpuvaG3GraphEndJob(&process->Graph)'), promote)
        self.assertLess(promote, complete.index('state->ActiveFence = state->SuccessorFence;'))

        abort = function(self.g3, 'AdmissionGpuvaG3AbortJob')
        pick = abort.index('if (state->SuccessorFence == fence && state->SuccessorProcess != NULL)\n'
                           '      process = state->SuccessorProcess;')
        self.assertLess(pick, abort.index('AppleAgxGpuvaG3GraphEndJob(&process->Graph)'))
        self.assertEqual(abort.count('state->SuccessorProcess = NULL;'), 2)
        # nothing else writes the field
        self.assertEqual(self.g3.count('state->SuccessorProcess ='), 4)

        slot = function(self.g3, 'AdmissionGpuvaG3RetainedSlot')
        for term in ('ExAcquireFastMutex(&state->Lock);', 'process->Graph.LeaseToken != 0ULL',
                     'process->Graph.SlotTable->Owner[process->Graph.Slot] == &process->Graph',
                     '!process->Poisoned', '!process->Graph.Uncertain'):
            self.assertIn(term, slot)

    def test_receipt_is_exported_with_and_without_diagnostics(self):
        export = self.backend
        self.assertEqual(export.count('L"Wom1CrossProcessPairsReceipt"'), 2)
        off = export.index('if (!runtime->JobDiagnostics && runtime->CrossProcessPairs) {')
        self.assertIn('2ULL * (ULONGLONG)frequency.QuadPart', export[off:off + 500])
        gate = export[export.rindex('if (runtime == NULL', 0, off):off]
        self.assertIn('!runtime->CrossProcessPairs', gate[:gate.index('return;')])
        self.assertIn('AdmissionJobWindowXprocReceiptInitialize(&runtime->XprocReceipt);', export)

    def test_receipt_decoder_matches_the_layout(self):
        import importlib.util
        import struct
        spec = importlib.util.spec_from_file_location('agx_xproc_receipt',
                                                      ROOT / 'tools/agx_xproc_receipt.py')
        tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tool)
        kinds = [0] * 16
        kinds[4 * 2 + 0] = 2   # compute -> render
        kinds[4 * 1 + 2] = 1   # render+prelude -> compute
        words = [3, 240, 9, 5, 4, 1, 0] + [55, 6, 0, 0, 0, 0, 0, 0] + [3, 1, 0, 0, 0, 0, 0, 0] + \
            [0, 711, 710, 2, 3, 6, 705, 700, 7300 | (11 << 16)] + \
            [4, 3, 0, 0, 0, 1, 0, 711, 0x500, 0x400] + [0, 7] + kinds
        out = tool.decode(struct.pack('<60I', *words))
        self.assertEqual(out['refused_55'], dict(capability_off=0, no_process=7, kinds={
            'compute -> render': 2, 'render+prelude -> compute': 1}))
        self.assertEqual(out['lead'], dict(considered=4, waived=3, refused={'3': 1},
                                           last=dict(fence=711, expected=0x500, wait_value=0x400)))
        self.assertEqual((out['considered'], out['allowed'], out['kicked'], out['fell_back']), (9, 5, 4, 1))
        self.assertEqual(out['refusals'], {'55': 3, '6': 1})
        self.assertEqual(out['last_kicked'], dict(fence=711, running_fence=710, running_slot=2, next_slot=3))
        self.assertEqual(out['last_undone'], dict(fence=700, line=7300, begin_refusal=11))
        with self.assertRaises(ValueError):
            tool.decode(b'\0' * 64)

    def test_receipt_abi(self):
        with tempfile.TemporaryDirectory(prefix='xproc-abi-') as tmp:
            src = Path(tmp) / 'abi.c'
            src.write_text('#include "render_job_window.h"\n'
                           '_Static_assert(sizeof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT) == 328, "size");\n'
                           '_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT,'
                           ' RenderConsidered) == 240, "v4 after v3");\n'
                           '_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT,'
                           ' Kicked) == 16, "kicked");\n'
                           '_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT,'
                           ' RefusalCode) == 28, "codes");\n'
                           '_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT,'
                           ' LastFence) == 96, "last");\n'
                           '_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT,'
                           ' LeadConsidered) == 128, "lead");\n'
                           '_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT,'
                           ' Refused55CapabilityOff) == 168, "off");\n'
                           '_Static_assert(__builtin_offsetof(ADMISSION_JOB_WINDOW_XPROC_RECEIPT,'
                           ' Refused55Kinds) == 176, "kinds");\n')
            for target in ('x86_64-pc-windows-msvc', 'aarch64-pc-windows-msvc'):
                for unit in (src, RENDER / 'src/render_job_window.c'):
                    done = subprocess.run(['clang', '-target', target, '-std=c11', '-Wall', '-Wextra',
                                           '-Werror', '-Wconversion', '-Wsign-compare', '-fsyntax-only',
                                           '-I', str(RENDER / 'include'), str(unit)],
                                          capture_output=True, text=True)
                    self.assertEqual(done.returncode, 0, done.stderr)


if __name__ == '__main__':
    unittest.main()
