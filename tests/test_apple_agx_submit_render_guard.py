from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxSubmitRenderGuardTests(unittest.TestCase):
    def test_submit_render_emits_exact_dispatch_safe_guard_and_status(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()

        self.assertIn("AdmissionSubmitRenderGuardWindows", header)
        self.assertIn("ADMISSION_SUBMIT_RENDER_GUARD_TAG", guards)
        self.assertIn("AdmissionSubmitRenderGuardWord", guards)
        self.assertIn("AdmissionSubmitRenderGuardWindows", trace)
        self.assertIn("J313_AGX_G2_POWER_CMD_QUERY", trace)
        helper = trace.split(
            "_Use_decl_annotations_ VOID AdmissionSubmitRenderGuardWindows", 1
        )[1].split("static VOID AdmissionSubmitTraceU64", 1)[0]
        self.assertEqual(helper.count("WRITE_REGISTER_ULONG64"), 1)
        self.assertEqual(helper.count("WRITE_REGISTER_ULONG(command"), 1)
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
        self.assertNotIn(
            "AdmissionSubmitRenderGuardWindows(Context, MAXULONG, STATUS_PENDING)",
            submit,
        )

    def test_production_build_has_no_registry_receipt_calls(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        self.assertIn("#define AdmissionSubmitRenderGuardWindows", header)
        self.assertNotIn("AdmissionRecordSubmitRenderGuard", submit)
        self.assertNotIn("IoOpenDeviceRegistryKey", trace)


if __name__ == "__main__":
    unittest.main()
