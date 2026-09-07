from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class TaProgressReceiptTests(unittest.TestCase):
    def test_receipt_captures_only_source_derived_progress_state(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()

        self.assertIn("ADMISSION_TA_PROGRESS_RECEIPT", header)
        self.assertIn("ADMISSION_TA_WORK_TIMESTAMP_TAIL_OFFSET 0x5b4u", worker)
        self.assertIn("ADMISSION_TA_STATS_TIMESTAMPS_OFFSET 0x5d0u", worker)
        self.assertIn("ADMISSION_TA_MICROSEQUENCE_FINALIZE_OFFSET 0x208u", worker)
        self.assertIn("AdmissionCaptureTaProgress(", worker)
        self.assertIn("AdmissionRecordTaProgress(adapter, &taProgress)", worker)
        self.assertIn("ADMISSION_QUEUE_FAULT_SNAPSHOT_DELAY_MS", worker)
        self.assertIn('L"Wom1TaProgressReceipt"', receipts)

        capture = worker.index("static BOOLEAN AdmissionCaptureTaProgress(")
        capture_end = worker.index("static BOOLEAN AdmissionCaptureKTrace(", capture)
        capture_body = worker[capture:capture_end]
        self.assertNotIn("WriteU32", capture_body)
        self.assertNotIn("WriteU64", capture_body)
        self.assertNotIn("RtlCopyMemory((PVOID)", capture_body)

    def test_progress_receipt_is_qualification_only(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        start = header.index("NTSTATUS AdmissionPresentSubmitTraced")
        qualification = header.index(
            "#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)", start
        )
        receipt = header.index("VOID AdmissionRecordTaProgress", qualification)
        fallback = header.index("#else", qualification)
        self.assertGreater(receipt, qualification)
        self.assertLess(receipt, fallback)
        self.assertIn("#define AdmissionRecordTaProgress(Context, Receipt)", header)


if __name__ == "__main__":
    unittest.main()
