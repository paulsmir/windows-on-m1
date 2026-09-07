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
        self.assertIn("ADMISSION_TA_MICROSEQUENCE_TIMESTAMP_START_OFFSET 0x18cu", worker)
        self.assertIn("ADMISSION_TA_MICROSEQUENCE_TIMESTAMP_END_OFFSET 0x1ccu", worker)
        self.assertIn("ADMISSION_TA_MICROSEQUENCE_RETIRE_OFFSET 0x28cu", worker)
        self.assertIn("ADMISSION_TA_MICROSEQUENCE_OPCODE_COUNT 6u", header)
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

    def test_retirement_receipt_captures_pending_stamp_owner(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()

        self.assertIn("ADMISSION_TA_RETIRE_RECEIPT", header)
        self.assertIn("ADMISSION_REGIONC_PENDING_STAMPS_OFFSET 0x111a8u", worker)
        self.assertIn("ADMISSION_REGIONC_PENDING_STAMPS_BYTES 0x800u", header)
        self.assertIn("ADMISSION_TA_FINALIZE_RETIRE_BYTES 0x88u", header)
        self.assertIn("AdmissionCaptureTaRetire(", worker)
        self.assertIn("AdmissionRecordTaRetire(adapter, &taRetire)", worker)
        self.assertIn('L"Wom1TaRetireReceipt"', receipts)

        capture = worker.index("static BOOLEAN AdmissionCaptureTaRetire(")
        capture_end = worker.index("static BOOLEAN AdmissionCaptureKTrace(", capture)
        capture_body = worker[capture:capture_end]
        self.assertNotIn("WriteU32", capture_body)
        self.assertNotIn("WriteU64", capture_body)
        self.assertIn("Runtime->TransportIo.ReadU32", capture_body)

    def test_temporal_receipt_distinguishes_finalize_restart(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        worker = (RENDER / "src" / "backend_platform_windows.c").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()

        self.assertIn("ADMISSION_TA_TEMPORAL_RECEIPT", header)
        self.assertIn("ADMISSION_TA_TEMPORAL_SAMPLE_COUNT 2u", header)
        self.assertIn("ADMISSION_TA_TEMPORAL_SECOND_DELAY_MS 100ULL", worker)
        self.assertIn("AdmissionCaptureTaTemporalSample(", worker)
        self.assertIn("AdmissionRecordTaTemporal(adapter, &taTemporal)", worker)
        self.assertIn('L"Wom1TaTemporalReceipt"', receipts)

        capture = worker.index("static BOOLEAN AdmissionCaptureTaTemporalSample(")
        capture_end = worker.index("static BOOLEAN AdmissionCaptureKTrace(", capture)
        capture_body = worker[capture:capture_end]
        self.assertNotIn("WriteU32", capture_body)
        self.assertNotIn("WriteU64", capture_body)


if __name__ == "__main__":
    unittest.main()
