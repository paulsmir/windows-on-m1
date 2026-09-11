from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class QueueSubmissionReceiptTests(unittest.TestCase):
    def test_receipt_is_crash_durable_before_progress_polling(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()

        self.assertIn("ADMISSION_QUEUE_SUBMISSION_RECEIPT", header)
        self.assertIn("AdmissionRecordQueueSubmission", header)
        self.assertIn("AdmissionCaptureQueueSubmission", worker)
        capture = worker.index("AdmissionCaptureQueueSubmission(", worker.index("result = AppleAgxBackendRuntimeSubmit"))
        polling = worker.index("while (runtime->Backend.Phase", capture)
        self.assertLess(capture, polling)
        self.assertIn('L"Wom1QueueSubmissionReceipt"', receipts)
        self.assertGreaterEqual(receipts.count("ZwFlushKey(key)"), 2)

    def test_receipt_carries_exact_publication_and_run_messages(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        for name in (
            "TaQueueInfoGpuAddress",
            "D3QueueInfoGpuAddress",
            "TaWorkAddresses",
            "D3WorkAddresses",
            "TaCpuWritePointer",
            "D3CpuWritePointer",
            "TaExpectedDonePointer",
            "D3ExpectedDonePointer",
            "TaChannelReadPointer",
            "TaChannelWritePointer",
            "D3ChannelReadPointer",
            "D3ChannelWritePointer",
            "TaRunMessage",
            "D3RunMessage",
        ):
            self.assertIn(name, header)


if __name__ == "__main__":
    unittest.main()
