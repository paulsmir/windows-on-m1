"""RQ-1: the real KMD ISR survives a swap the broker fails after acceptance.

Real bug (E5-RQ, architect 2026-11-12): any IRQ_ERROR latched runtime->Faulted
for good, kept PresentGate/PendingValid and the client's PresentPending set, so
one failed swap made every later commit DEVICE_BUSY and StopDevice fail.
Invariants: the failed flip is retried from the vertical tick (3 tries), its
PresentIds are reported exactly once (when the retry latches, or at the next
vsync after the last try), the gate is released only then, the held set goes
after the retry resolved, a foreign error changes nothing, a failed PASSIVE
present stays fail-closed, and no retry runs once stopping.
"""
import unittest
from test_scanout_vsync_replay import ROOT, compile_and_run


class ScanoutRq1RetryTests(unittest.TestCase):
    def test_failed_swap_retry_and_report(self):
        compile_and_run((ROOT / 'tests/fixtures/rq1_retry_scenarios.c').read_text())


if __name__ == '__main__':
    unittest.main()
