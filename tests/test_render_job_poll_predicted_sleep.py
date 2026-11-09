"""rc100: predicted-sleep completion poll, host model on EXP2034 job durations.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The rc72 schedule (ADMISSION_JOB_POLL_SPIN_WINDOW_US 50000, step 20 us,
backend_platform_windows.c KeStallExecutionProcessor) busy-polls every
GravityMark job from kick to completion on a pinned P-core: the CPU is 100 %
busy for the whole GPU time on a fanless J313 whose CPU and GPU share one
~10 W budget (fable-gap: EXP2031c ran 49 % at 1278 MHz against 92 % cool).
This model replays the EXP2034 Wom1JobTiming971Hot job durations (HD 50k and
FHD 100k, tests/fixtures/exp2034_job_durations.inc) through the production
planner (render_job_timing.c AdmissionJobPoll*) and a simulated worker loop
(poll cost, spin step, timer lateness) and checks:
- RED on the rc72 policy (Wom1JobPollSpinUs = 50000): busy fraction ~1.0;
- GREEN on the rc100 default: busy fraction <= 0.15 of GPU time with a
  high-resolution timer (lateness up to 60 us), and the median detection
  latency within +50 us of the rc72 median, p90 within +150 us;
- with a timer that is late by up to 300 us, or the 1 ms tick fallback, the
  learned slack keeps the median latency within +50 us (busy rises instead);
- Wom1JobPollSpinUs = N reproduces AdmissionJobPollStallUs (N = 50000) and
  every mode still sleeps ticks from 50 ms on (TDR path), never plans a
  sleep past the guarded prediction, never sleeps longer than the chunk.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/include"
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/render_job_timing.c"
FIXTURES = ROOT / "tests/fixtures"
WORKER = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"
sys.path.insert(0, str(ROOT / "tools"))

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render_job_timing.h"
typedef struct { int ctx, kind, dma, us; } EXP2034_JOB;
#include "exp2034_job_durations.inc"
#define POLL_COST_US 5ull   /* provider poll + RTKit drain per loop */
#define WAKE_COST_US 5ull   /* timer set, wait, context switch back */
#define PASSES 6
#define WARM_PASSES WARM
enum { TIMER_HIGHRES, TIMER_SLOW, TIMER_TICK };
static unsigned int rng = 1u;
static unsigned int next_rand(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static int cmp(const void *a, const void *b) {
  unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b;
  return x < y ? -1 : x > y;
}
typedef struct { double busy; unsigned long long median, p90, max; double mean; unsigned jobs; } RESULT;
static RESULT run(const EXP2034_JOB *trace, unsigned n, unsigned window, int timer) {
  static unsigned long long late[4096];
  ADMISSION_JOB_POLL_STATE st;
  unsigned long long busy = 0, gpu = 0, sum = 0, clock = 0;
  unsigned pass, i, jobs = 0;
  RESULT r;
  AdmissionJobPollInitialize(&st, window,
      timer == TIMER_TICK ? 0u : ADMISSION_JOB_POLL_FLAG_HIGHRES);
  rng = 7u;
  for (pass = 0; pass < PASSES; ++pass)
    for (i = 0; i < n; ++i) {
      /* +-6 % deterministic jitter per pass (thermal clock changes) */
      unsigned long long d = (unsigned long long)trace[i].us * (940u + next_rand() % 121u) / 1000u;
      unsigned long long key = AdmissionJobPollBegin(&st, 0x1000u + trace[i].ctx,
          (unsigned)trace[i].kind, (unsigned)trace[i].dma);
      unsigned pred = AdmissionJobPollPredict(&st, key);
      unsigned long long t = 0, last = 0, b = 0;
      int after = 0;
      if (d == 0) d = 1;
      for (;;) {
        unsigned us = 0, action;
        t += POLL_COST_US; b += POLL_COST_US;
        if (t >= d) break;
        last = t;
        action = AdmissionJobPollPlan(&st, t, pred, 0u, &us);
        if (action == ADMISSION_JOB_POLL_SPIN) {
          assert(us == ADMISSION_JOB_POLL_SPIN_STEP_US);
          t += us; b += us; after = 0;
        } else if (action == ADMISSION_JOB_POLL_SLEEP) {
          unsigned long long actual;
          assert(us > 0u && us <= ADMISSION_JOB_POLL_SLEEP_CHUNK_US);
          if (timer == TIMER_HIGHRES) actual = us + 5u + next_rand() % 56u;
          else if (timer == TIMER_SLOW) actual = us + next_rand() % 301u;
          else { /* KeDelayExecutionThread at 1 ms resolution: next tick at or after due */
            unsigned long long due = clock + t + us;
            actual = (due + 999u) / 1000u * 1000u - (clock + t);
          }
          AdmissionJobPollNoteSleep(&st, us, actual);
          t += actual; b += WAKE_COST_US; after = 1;
        } else {
          unsigned long long now = clock + t;
          t += (now + 1000u) / 1000u * 1000u + 1000u - now; after = 1;
        }
      }
      AdmissionJobPollFinish(&st, key, pred, t, last, after);
      clock += t + 50u;
      if (pass < WARM_PASSES) continue; /* steady state: keys learned */
      busy += b; gpu += d; sum += t - d;
      late[jobs++] = t - d;
    }
  qsort(late, jobs, sizeof(late[0]), cmp);
  r.busy = (double)busy / (double)gpu;
  r.median = late[jobs / 2]; r.p90 = late[jobs * 9 / 10]; r.max = late[jobs - 1];
  r.mean = (double)sum / jobs; r.jobs = jobs;
  return r;
}
static void show(const char *name, RESULT r) {
  printf("%-22s busy %5.3f  latency median %4llu p90 %4llu mean %7.1f max %5llu us (%u jobs)\n",
         name, r.busy, r.median, r.p90, r.mean, r.max, r.jobs);
}
int main(void) {
  const struct { const char *name; const EXP2034_JOB *t; unsigned n; } traces[] = {
    {"HD50k", EXP2034_HD, sizeof(EXP2034_HD) / sizeof(EXP2034_HD[0])},
    {"FHD100k", EXP2034_FHD, sizeof(EXP2034_FHD) / sizeof(EXP2034_FHD[0])},
  };
  int ok = 1;
  unsigned i;
  for (i = 0; i < 2; ++i) {
    char label[64];
    RESULT old = run(traces[i].t, traces[i].n, 50000u, TIMER_HIGHRES);
    RESULT hi = run(traces[i].t, traces[i].n, ADMISSION_JOB_POLL_PREDICTIVE, TIMER_HIGHRES);
    RESULT slow = run(traces[i].t, traces[i].n, ADMISSION_JOB_POLL_PREDICTIVE, TIMER_SLOW);
    RESULT tick = run(traces[i].t, traces[i].n, ADMISSION_JOB_POLL_PREDICTIVE, TIMER_TICK);
    snprintf(label, sizeof(label), "%s rc72 50000", traces[i].name); show(label, old);
    snprintf(label, sizeof(label), "%s rc100 highres", traces[i].name); show(label, hi);
    snprintf(label, sizeof(label), "%s rc100 late300", traces[i].name); show(label, slow);
    snprintf(label, sizeof(label), "%s rc100 tick", traces[i].name); show(label, tick);
    if (old.busy < 0.9) { puts("rc72 baseline is not a busy loop?"); ok = 0; }
    if (hi.busy > 0.15) { printf("RED %s: busy %.3f > 0.15\n", traces[i].name, hi.busy); ok = 0; }
    if (hi.median > old.median + 50u) { printf("RED %s: median latency\n", traces[i].name); ok = 0; }
    if (hi.p90 > old.p90 + 150u) { printf("RED %s: p90 latency\n", traces[i].name); ok = 0; }
    if (slow.median > old.median + 50u) { printf("RED %s: late300 median\n", traces[i].name); ok = 0; }
    if (tick.median > old.median + 50u) { printf("RED %s: tick median\n", traces[i].name); ok = 0; }
  }
  if (!ok) return 1;
  puts("PASS");
  return 0;
}
'''

INVARIANTS = r'''
#include <assert.h>
#include <stdio.h>
#include "render_job_timing.h"
int main(void) {
  ADMISSION_JOB_POLL_STATE st;
  unsigned long long t, key, key2;
  unsigned us, pred, action, slack;
  /* the kill switch at 50000 is exactly the rc72 schedule */
  AdmissionJobPollInitialize(&st, ADMISSION_JOB_POLL_SPIN_WINDOW_US, 0u);
  for (t = 0; t < 120000u; t += 7u) {
    action = AdmissionJobPollPlan(&st, t, 5000u, 0u, &us);
    assert(action == (AdmissionJobPollStallUs(t) ? ADMISSION_JOB_POLL_SPIN : ADMISSION_JOB_POLL_TICK));
    assert(action == ADMISSION_JOB_POLL_TICK || us == AdmissionJobPollStallUs(t));
  }
  AdmissionJobPollInitialize(&st, 0u, 0u);
  assert(AdmissionJobPollPlan(&st, 0u, 0u, 0u, &us) == ADMISSION_JOB_POLL_TICK);
  /* predictive: two samples before any sleep; shortest of the history */
  AdmissionJobPollInitialize(&st, ADMISSION_JOB_POLL_PREDICTIVE, ADMISSION_JOB_POLL_FLAG_HIGHRES);
  key = AdmissionJobPollBegin(&st, 0x10u, 1u, 72u);
  assert(key && AdmissionJobPollPredict(&st, key) == 0u);
  assert(AdmissionJobPollPlan(&st, 5u, 0u, 0u, &us) == ADMISSION_JOB_POLL_SPIN && us == 20u);
  AdmissionJobPollFinish(&st, key, 0u, 6000u, 5980u, 0);
  assert(AdmissionJobPollPredict(&st, key) == 0u);
  AdmissionJobPollFinish(&st, key, 0u, 5000u, 4980u, 0);
  assert(AdmissionJobPollPredict(&st, key) == 5000u);
  /* a completion seen after a sleep teaches the last running poll */
  AdmissionJobPollFinish(&st, key, 5000u, 4900u, 4100u, 1);
  assert(AdmissionJobPollPredict(&st, key) == 4100u);
  assert(st.Stats.Misses == 1u && st.Stats.MissWindowUs == 800u);
  /* same job signature after a different predecessor is another key */
  key2 = AdmissionJobPollBegin(&st, 0x10u, 1u, 72u);
  assert(key2 != key);
  /* sleeps end at least guard + slack before the prediction, chunked */
  pred = 13000u; slack = AdmissionJobPollSlackUs(&st);
  assert(slack == ADMISSION_JOB_POLL_HIGHRES_SLACK_US);
  for (t = 0; t < 60000u; t += 3u) {
    action = AdmissionJobPollPlan(&st, t, pred, 0u, &us);
    if (t >= ADMISSION_JOB_POLL_SPIN_WINDOW_US) { assert(action == ADMISSION_JOB_POLL_TICK); continue; }
    if (action == ADMISSION_JOB_POLL_SLEEP && t < pred) {
      assert(us <= ADMISSION_JOB_POLL_SLEEP_CHUNK_US);
      assert(t + us + slack + ADMISSION_JOB_POLL_GUARD_MAX_US <= pred);
    }
    if (action == ADMISSION_JOB_POLL_SLEEP && t >= pred)
      assert(us == ADMISSION_JOB_POLL_OVERRUN_SLEEP_US);
    if (t >= pred + 300u) assert(action == ADMISSION_JOB_POLL_SLEEP);
    if (t + slack + 400u < pred - 300u) assert(action == ADMISSION_JOB_POLL_SLEEP);
    if (t > pred - 300u && t < pred + 300u) assert(action == ADMISSION_JOB_POLL_SPIN);
  }
  /* rc116 (EXP2061a): a timer that wakes late (learned lateness above the
   * high-resolution slack) makes the overrun sleep cost more than it
   * saves: the overrunning job is spun on */
  {
    ADMISSION_JOB_POLL_STATE late;
    unsigned int i;
    AdmissionJobPollInitialize(&late, ADMISSION_JOB_POLL_PREDICTIVE, ADMISSION_JOB_POLL_FLAG_HIGHRES);
    for (i = 0; i < ADMISSION_JOB_POLL_LATENESS; ++i) AdmissionJobPollNoteSleep(&late, 100u, 530u);
    assert(AdmissionJobPollPlan(&late, 1100u, 1000u, 0u, &us) == ADMISSION_JOB_POLL_SPIN);
    assert(us == ADMISSION_JOB_POLL_SPIN_STEP_US);
  }
  /* a sleep cap (prebind retry) bounds every sleep */
  assert(AdmissionJobPollPlan(&st, 0u, 13000u, 250u, &us) == ADMISSION_JOB_POLL_SLEEP && us == 250u);
  /* the guard is a tenth of short jobs: a 1 ms job sleeps, a 200 us one spins */
  assert(AdmissionJobPollPlan(&st, 5u, 1000u, 0u, &us) == ADMISSION_JOB_POLL_SLEEP);
  assert(us == 1000u - 100u - slack - 5u);
  assert(AdmissionJobPollPlan(&st, 5u, 200u, 0u, &us) == ADMISSION_JOB_POLL_SPIN);
  /* lateness is learned (largest of the last 8) and capped; without a
   * high-resolution timer an overrunning job is spun on */
  AdmissionJobPollInitialize(&st, ADMISSION_JOB_POLL_PREDICTIVE, 0u);
  assert(AdmissionJobPollPlan(&st, 14000u, 13000u, 0u, &us) == ADMISSION_JOB_POLL_SPIN);
  assert(AdmissionJobPollSlackUs(&st) == ADMISSION_JOB_POLL_TICK_SLACK_US);
  for (us = 0; us < ADMISSION_JOB_POLL_LATENESS; ++us) AdmissionJobPollNoteSleep(&st, 500u, 530u);
  assert(AdmissionJobPollSlackUs(&st) == 30u);
  AdmissionJobPollNoteSleep(&st, 500u, 100000u);
  assert(AdmissionJobPollSlackUs(&st) == ADMISSION_JOB_POLL_LATENESS_MAX_US);
  assert(st.Stats.LateMaxUs == 99500u);
  /* the context table and key table recycle without losing new keys */
  AdmissionJobPollInitialize(&st, ADMISSION_JOB_POLL_PREDICTIVE, 1u);
  for (t = 0; t < 1000u; ++t) {
    key = AdmissionJobPollBegin(&st, 0x100u + t % 23u, (unsigned)(t % 2u), (unsigned)(t % 97u));
    AdmissionJobPollFinish(&st, key, 0u, 1000u + t, 990u + t, 0);
    AdmissionJobPollFinish(&st, key, 0u, 1000u + t, 990u + t, 0);
    assert(AdmissionJobPollPredict(&st, key) == 1000u + t);
  }
  assert(AdmissionJobPollBegin(0, 1u, 1u, 1u) == 0u);
  assert(AdmissionJobPollPredict(0, 1u) == 0u);
  puts("PASS");
  return 0;
}
'''


RECEIPT = r'''
#include <stdio.h>
#include "render_job_timing.h"
int main(void) {
  ADMISSION_JOB_POLL_STATE st;
  unsigned long long key;
  unsigned us;
  AdmissionJobPollInitialize(&st, ADMISSION_JOB_POLL_PREDICTIVE, ADMISSION_JOB_POLL_FLAG_HIGHRES);
  key = AdmissionJobPollBegin(&st, 7u, 1u, 72u);
  AdmissionJobPollFinish(&st, key, 0u, 4000u, 3990u, 0);
  AdmissionJobPollFinish(&st, key, 0u, 4000u, 3990u, 0);
  (void)AdmissionJobPollPlan(&st, 10u, 4000u, 0u, &us);
  AdmissionJobPollNoteSleep(&st, 1000u, 1040u);
  AdmissionJobPollNoteSleep(&st, 1000u, 1010u);
  AdmissionJobPollFinish(&st, key, 4000u, 4000u, 3700u, 1);
  fwrite(&st, sizeof(st), 1, stdout);
  return 0;
}
'''


def build_and_run(test, program, warm=1, binary=False):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "model.c"
        exe = Path(directory) / "model"
        path.write_text(program)
        built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                "-Werror", f"-DWARM={warm}", "-I", str(INCLUDE), "-I", str(FIXTURES), str(path),
                                str(SOURCE), "-o", str(exe)], capture_output=True, text=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=not binary)
        test.assertEqual(ran.returncode, 0, ran.stderr if binary else ran.stdout + ran.stderr)
        return ran.stdout


class JobPollPredictedSleep(unittest.TestCase):
    def test_exp2034_busy_fraction_and_latency(self):
        out = build_and_run(self, PROGRAM)
        if os.environ.get("AGX_POLL_MODEL_VERBOSE"):
            print(out)

    def test_exp2034_including_learning(self):
        """The first pass (keys not learned yet: two samples each) included:
        the busy bound is looser, the latency bounds are the same."""
        out = build_and_run(self, PROGRAM.replace("hi.busy > 0.15", "hi.busy > 0.25"), warm=0)
        if os.environ.get("AGX_POLL_MODEL_VERBOSE"):
            print(out)

    def test_planner_invariants(self):
        build_and_run(self, INVARIANTS)

    def test_wom1_job_poll_receipt_decodes(self):
        """The decoder's layout equals ADMISSION_JOB_POLL_STATE v1 (a wrong
        offset would misreport the poll's busy fraction on hardware)."""
        import agx_gpu_frame_breakdown as tool
        raw = build_and_run(self, RECEIPT, binary=True)
        self.assertEqual(len(raw), tool.POLL_BYTES)
        out = tool.kmd_job_poll(raw)
        self.assertEqual(out["mode"], "predictive")
        self.assertTrue(out["high_resolution_timer"])
        self.assertEqual((out["jobs"], out["predicted_jobs"], out["sleeps"], out["spins"]), (3, 1, 2, 0))
        self.assertEqual((out["job_us"], out["slept_us"], out["late_max_us"]), (12000, 2050, 40))
        self.assertEqual((out["seen_after_sleep"], out["seen_after_sleep_window_us"], out["keys"]), (1, 300, 1))
        self.assertEqual(out["busy_fraction"], round(9950 / 12000, 4))
        self.assertEqual(out["slack_us"], 100)
        text = ("Wom1Other = 1\nWom1JobPoll " + raw.hex() + "\n").encode()
        delta = tool.kmd_job_poll(text, before=raw)
        self.assertEqual((delta["jobs"], delta["job_us"]), (0, 0))

    def test_worker_wiring(self):
        """The G3 worker plans every poll with AdmissionJobPollPlan, sleeps on
        the high-resolution timer (bounded wait), learns at the loop exit
        from the start of the poll that saw the completion, reads the kill
        switch Wom1JobPollSpinUs, deletes the timer once no worker runs and
        exports Wom1JobPoll; the non-G3 worker keeps the rc72 schedule."""
        text = WORKER.read_text()
        plan = text.index("action = AdmissionJobPollPlan(&runtime->JobPoll")
        loop = text.rindex("while (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&", 0, plan)
        self.assertIn("pollIteration = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;",
                      text[loop:plan])
        # IRQ-completion step 2 inserts the wait block (Wom1IrqCompletion,
        # default on, active only with the ACPI mailbox line armed) between the
        # planner call and the spin/sleep branches; the planner's own schedule
        # below it is unchanged.
        body = text[plan:plan + 2600]
        self.assertIn("AdmissionIrqWaitActive(runtime)", body)
        self.assertRegex(body, r"ADMISSION_JOB_POLL_SPIN\) \{\s*pollAfterSleep = FALSE;\s*"
                               r"KeStallExecutionProcessor\(planUs\);\s*continue;")
        self.assertIn("AdmissionJobPollSleepWindows(runtime, planUs", body)
        self.assertIn("#else\n      ULONG stallUs = AdmissionJobPollStallUs(elapsedUs);", body)
        finish = text.index("AdmissionJobPollFinish(&runtime->JobPoll", plan)
        self.assertLess(finish, text.index("AdmissionRenderCorrelationWorkerWindows(\n      adapter, description.Fence, FALSE", plan))
        sleep = text[text.index("static NTSTATUS AdmissionJobPollSleepWindows("):]
        sleep = sleep[:sleep.index("\n}\n")]
        self.assertIn("timeout.QuadPart = interval.QuadPart - 20000LL;", sleep)
        self.assertIn("ExCancelTimer(runtime->JobPollTimer, NULL)", sleep)
        self.assertIn("KeDelayExecutionThread(KernelMode, FALSE, &interval)", sleep)
        self.assertIn('AdmissionPlatformDwordValue(Context, L"Wom1JobPollSpinUs", &spinUs)', text)
        self.assertIn("ExAllocateTimer(AdmissionJobPollTimerCallback,\n          runtime, EX_TIMER_HIGH_RESOLUTION)", text)
        destroy = text[text.index("static NTSTATUS AdmissionPlatformDestroy("):]
        self.assertLess(destroy.index("Runtime->JobThreadObject = NULL;"),
                        destroy.index("ExDeleteTimer(Runtime->JobPollTimer, TRUE, TRUE, NULL)"))
        self.assertIn('RtlInitUnicodeString(&name, L"Wom1JobPoll");', text)
        self.assertIsNone(re.search(r"#include\s*<std", text))


if __name__ == "__main__":
    unittest.main()
