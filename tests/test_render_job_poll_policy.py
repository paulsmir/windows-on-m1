"""EXP1073: the KMD job worker polls completion finely, then by timer ticks.

EXP1072 job timing: 62 of 63 jobs completed inside the first 1 ms sleep
after the kick (BackendAfter->Complete median 1510 us, minimum 1041 us), so
every job paid one timer tick of pure detection latency. Invariants of
AdmissionJobPollStallUs:
- right after the kick and until the spin window ends, the next poll is at
  most 20 us away (short jobs are seen within one step);
- the spin is bounded: from the window on, the worker sleeps ticks again, so
  a long job costs at most one window of busy CPU;
- the worker loop uses the policy before its 1 ms KeDelayExecutionThread.
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
int main(void) {
  unsigned long long t = 0, spin = 0;
  assert(AdmissionJobPollStallUs(0) == 20u);
  assert(AdmissionJobPollStallUs(ADMISSION_JOB_POLL_SPIN_WINDOW_US - 1u) == 20u);
  assert(AdmissionJobPollStallUs(ADMISSION_JOB_POLL_SPIN_WINDOW_US) == 0u);
  assert(AdmissionJobPollStallUs(~0ULL) == 0u);
  for (unsigned stall; (stall = AdmissionJobPollStallUs(t)) != 0u; t += stall + 5u) spin += stall;
  assert(spin <= ADMISSION_JOB_POLL_SPIN_WINDOW_US);
  puts("PASS");
  return 0;
}
'''


class JobPollPolicy(unittest.TestCase):
    def test_poll_policy_is_fine_then_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "poll.c"
            exe = Path(directory) / "poll"
            path.write_text(PROGRAM)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-I", str(INCLUDE), str(path), str(SOURCE),
                                    "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_worker_consults_the_policy_before_sleeping(self):
        text = WORKER.read_text()
        stall = text.index("AdmissionJobPollStallUs(")
        self.assertLess(stall, text.index("interval.QuadPart = -10000LL;", stall))
        self.assertIn("KeStallExecutionProcessor(stallUs)", text)


if __name__ == "__main__":
    unittest.main()
