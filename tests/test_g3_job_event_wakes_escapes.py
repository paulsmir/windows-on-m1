"""EXP1138: escapes that wait for their process's job must wake when it ends.

With two submissions in flight DWM's composition thread spent ~1 s per 15.9 s
(1236 waits) in AdmissionGpuvaG3PrivateEscape: the private ACQUIRE/PREPARE
and copy escapes polled JobInFlight/LeaseToken with 1 ms timer sleeps.  A
job's end now sets the G3 JobEvent and the waits block on it (1 ms backstop),
with their 3 s bound measured in wall-clock time.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c"
PAGING = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c"


def function(text, name):
    m = re.search(r"(?m)^(?:static )?(?:NTSTATUS|BOOLEAN) " + name + r"\(", text)
    start = text.index("{", m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}"); end += 1
    return text[m.start():end]


class JobEventWakesEscapes(unittest.TestCase):
    def test_complete_job_signals_after_unlock(self):
        body = function(SRC.read_text(), "AdmissionGpuvaG3CompleteJobTimestamp")
        unlock = body.rindex("ExReleaseFastMutex(&state->Lock);")
        signal = body.index("KeSetEvent(&state->JobEvent")
        self.assertLess(unlock, signal)

    def test_job_waits_hold_no_process_lock(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? L2 step 1: job
        completion takes the completing process's lock (P) before the state
        lock. A JobEvent wait that keeps P deadlocks against that completion
        until the 3 s bound, so every wait loop drops P with the state lock."""
        sources = ((SRC, "AdmissionGpuvaG3PrivateEscape"), (SRC, "AdmissionGpuvaG3CopyEscape"),
                   (PAGING, "AdmissionGpuvaG3BuildPagingBuffer"))
        waits = 0
        for path, name in sources:
            body = function(path.read_text(), name)
            for m in re.finditer(r"KeWaitForSingleObject\(&state->JobEvent", body):
                loop = body[body.rindex("KeClearEvent(&state->JobEvent);", 0, m.start()):m.start()]
                self.assertIn("AdmissionG3FindLockedProcess(", loop, name)
                tail = loop[loop.rindex("ExReleaseFastMutex(&state->Lock);"):]
                self.assertIn("KeReleaseGuardedMutex(&", tail, name)
                waits += 1
        self.assertEqual(waits, 4)

    def test_job_waits_block_on_the_event_with_wall_clock_bounds(self):
        text = SRC.read_text()
        for name in ("AdmissionGpuvaG3PrivateEscape", "AdmissionGpuvaG3CopyEscape"):
            body = function(text, name)
            loop = body[body.index("KeClearEvent(&state->JobEvent);"):]
            loop = loop[:loop.index("KeWaitForSingleObject(&state->JobEvent")]
            self.assertIn("JobInFlight", loop, name)
            self.assertIn("KeQueryInterruptTime()>=deadline", loop, name)
            self.assertNotIn("wait_ms>=3000u", body, name)
        private = function(text, "AdmissionGpuvaG3PrivateEscape")
        self.assertNotIn("KeDelayExecutionThread", private)

    def test_paging_waits_block_on_the_event(self):
        """rc117 (EXP2064): UPDATE_PAGE_TABLE and FLUSH_TLB builds waited for
        the process's job with 1 ms timer sleeps (15309 builds, 15.2 s). Both
        waits must wake on JobEvent, with the 3 s bound in wall-clock time."""
        body = function(PAGING.read_text(), "AdmissionGpuvaG3BuildPagingBuffer")
        self.assertNotIn("KeDelayExecutionThread", body)
        self.assertEqual(body.count("KeClearEvent(&state->JobEvent);"), 2)
        self.assertEqual(body.count("KeWaitForSingleObject(&state->JobEvent"), 2)
        self.assertEqual(body.count("KeQueryInterruptTime() >= deadline"), 2)
        self.assertNotIn("wait_ms >= job_wait_limit_ms", body)


if __name__ == "__main__":
    unittest.main()
