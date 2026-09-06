from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxSubmitRenderGuardTests(unittest.TestCase):
    def test_submit_render_persists_exact_guard_and_status(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()

        self.assertIn("AdmissionRecordSubmitRenderGuard", header)
        self.assertIn('L"Wom1SubmitRenderGuard"', receipts)
        self.assertIn('L"Wom1SubmitRenderStatus"', receipts)
        self.assertIn("ADMISSION_SUBMIT_RENDER_GUARD", guards)
        self.assertIn("AdmissionSubmitRenderGuardContextMagic", submit)
        self.assertIn("AdmissionSubmitRenderGuardContextDevice", submit)
        self.assertIn("AdmissionSubmitRenderGuardAdapter", submit)
        self.assertIn("AdmissionSubmitRenderGuardSystem", submit)
        self.assertIn("AdmissionSubmitRenderGuardSchedulerInactive", submit)
        self.assertIn("AdmissionSubmitRenderGuardNode", submit)
        self.assertIn("AdmissionSubmitRenderGuardEngine", submit)
        self.assertIn("AdmissionSubmitRenderGuardFence", submit)
        self.assertIn("AdmissionSubmitRenderGuardAccepted", submit)

    def test_production_build_has_no_registry_receipt_calls(self):
        submit = (RENDER / "src" / "submission_windows.c").read_text()
        self.assertIn("#if !defined(APPLE_AGX_SUBMIT_QUALIFICATION)", submit)
        self.assertIn("#define AdmissionRecordSubmitRenderGuard", submit)


if __name__ == "__main__":
    unittest.main()
