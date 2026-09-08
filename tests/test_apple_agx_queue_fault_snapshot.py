from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class QueueFaultSnapshotTests(unittest.TestCase):
    def test_bounded_fault_snapshot_is_captured_without_blocking_flush(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()
        self.assertIn("ADMISSION_QUEUE_FAULT_SNAPSHOT", header)
        self.assertIn("AdmissionRecordQueueFaultSnapshot", header)
        self.assertIn("ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS 50ULL", worker)
        poll = worker.index("AppleAgxPlatformProviderPoll(")
        capture = worker.index("AdmissionCaptureQueueFaultSnapshot(", poll)
        self.assertGreater(capture, poll)
        self.assertIn("AppleAgxRegionBMemoryFaultInfo", worker)
        self.assertIn("ADMISSION_REGIONC_FAULT_INFO_OFFSET", worker)
        self.assertIn('L"Wom1QueueFaultSnapshot"', receipts)
        fault_writer = receipts[receipts.index(
            "VOID AdmissionRecordQueueFaultSnapshot("):receipts.index(
                "VOID AdmissionRecordUmdRenderGuard(")]
        self.assertNotIn("ZwFlushKey", fault_writer)
        correlation = (RENDER / "src" /
                       "render_call_correlation_windows.c").read_text()
        self.assertIn("status = ZwFlushKey(key);", correlation)
        failure = worker.index("AdmissionRecordEventDrain(adapter, &eventReceipt)")
        immediate = worker.index("AdmissionCaptureQueueFaultSnapshot(", failure)
        drain_trace = worker.index("AdmissionProviderDrainTraceWindows(", failure)
        self.assertLess(immediate, drain_trace)
        self.assertIn("currentTaRead, currentD3Read, TRUE, &snapshot", worker)
        self.assertIn("currentTaRead, currentD3Read, FALSE, &snapshot", worker)
        self.assertIn("(!AllowEarly &&", worker)


if __name__ == "__main__":
    unittest.main()
