"""rc72: completion-detection latency model of the KMD job worker.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1989 (GravityMark D3D12, tools/agx_gpu_frame_breakdown.py): every job that
ran 2 ms or longer was observed 1.02-1.20 ms (median) after its firmware end,
because after ADMISSION_JOB_POLL_SPIN_WINDOW_US = 2000 the worker slept one
1 ms timer tick per poll (KeDelayExecutionThread rounds up to the next tick).
4.5-7 such jobs per frame cost 6.3 ms (HD) / 7.2 ms (FHD) on the app's
render_fence critical path. Jobs under 2 ms, polled every 20 us, were seen
after 0.03 ms. The render-admission device has no firmware completion
interrupt (its only interrupt resource is the synthetic scanout edge 889;
interrupt.c AdmissionFindBrokerResource), so the worker must poll.

This test simulates the worker's poll schedule (AdmissionJobPollStallUs, the
1 ms tick sleep) against a job that ends at time D and checks:
- every job up to the EXP1989 maximum span (45.2 ms) is detected within one
  poll step plus the poll cost (the old 2 ms window gave ~1000 us: RED);
- the busy-poll is still bounded: a job longer than the window (a hung GPU
  waiting for TDR) spins at most the window, then sleeps ticks, and its
  detection latency stays below two ticks.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "drivers/apple-agx/render-admission/include"
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/render_job_timing.c"
WORKER = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include "render_job_timing.h"
#define POLL_COST_US 5ull   /* one loop body: provider poll, RTKit drain */
#define TICK_US 1000ull     /* ExSetTimerResolution(1 ms) */
/* First poll at or after end; *spin = busy-wait spent until then. */
static unsigned long long detect(unsigned long long end,
                                 unsigned long long *spin) {
  unsigned long long t = 0;
  *spin = 0;
  for (;;) {
    unsigned int stall;
    t += POLL_COST_US;
    if (t >= end) return t - end;
    stall = AdmissionJobPollStallUs(t);
    if (stall) { t += stall; *spin += stall; continue; }
    /* KeDelayExecutionThread(-1 ms): at least one tick, ends on a tick */
    t = (t + TICK_US) / TICK_US * TICK_US + TICK_US;
  }
}
int main(void) {
  unsigned long long end, spin, worst = 0;
  for (end = 1; end <= 45200ull; end += 7ull) {
    unsigned long long late = detect(end, &spin);
    if (late > worst) worst = late;
    if (late > ADMISSION_JOB_POLL_SPIN_STEP_US + POLL_COST_US) {
      printf("job of %llu us detected %llu us late\n", end, late);
      return 1;
    }
  }
  printf("worst latency up to 45.2 ms: %llu us\n", worst);
  /* bounded spin for a job that outlives the window (TDR path) */
  for (end = ADMISSION_JOB_POLL_SPIN_WINDOW_US + 1ull;
       end <= ADMISSION_JOB_POLL_SPIN_WINDOW_US + 20000ull; end += 13ull) {
    unsigned long long late = detect(end, &spin);
    assert(spin <= ADMISSION_JOB_POLL_SPIN_WINDOW_US);
    assert(late <= 2ull * TICK_US + POLL_COST_US);
  }
  detect(2000000ull, &spin); /* 2 s: the dxgkrnl TDR timeout */
  assert(spin <= ADMISSION_JOB_POLL_SPIN_WINDOW_US);
  puts("PASS");
  return 0;
}
'''


class JobPollLatency(unittest.TestCase):
    def test_long_jobs_are_detected_within_one_poll_step(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "latency.c"
            exe = Path(directory) / "latency"
            path.write_text(PROGRAM)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-I", str(INCLUDE), str(path), str(SOURCE),
                                    "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_every_stall_step_rechecks_stop_reset_and_phase(self):
        """A busy-poll step must return to the loop condition, so Stopping,
        Resetting (TDR, ResetEngine), the TDR drill and the job phase are
        re-evaluated every step and the fallback sleep's failure path stays."""
        text = WORKER.read_text()
        stall = text.index("AdmissionJobPollStallUs(")
        loop = text.rindex("while (runtime->Backend.Phase == AppleAgxBackendRuntimeSubmitted &&", 0, stall)
        head = text[loop:text.index("{", loop)]
        for needle in ("!drilled", "&runtime->Stopping", "&runtime->Resetting"):
            self.assertIn(needle, head)
        self.assertRegex(text[stall:stall + 600],
                         r"KeStallExecutionProcessor\(stallUs\);\s*continue;")
        tail = text[stall:stall + 1200]
        self.assertIn("KeDelayExecutionThread(", tail)
        self.assertIn("SchedulerFaulted", tail)


if __name__ == "__main__":
    unittest.main()
