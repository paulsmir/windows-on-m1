from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxPatchGuardTests(unittest.TestCase):
    def test_nonpaging_render_patch_emits_one_final_guard(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        patch = (RENDER / "src" / "gdi_windows.c").read_text()

        self.assertIn("ADMISSION_PATCH_RENDER_GUARD_TAG", guards)
        self.assertIn("ADMISSION_PATCH_RENDER_GUARD", guards)
        self.assertIn("AdmissionPatchRenderGuardWindows", header)
        self.assertIn("AdmissionPatchRenderGuardWindows", trace)
        self.assertIn("PATCH_RENDER_RETURN", patch)
        for name in (
            "AdmissionPatchRenderGuardArguments",
            "AdmissionPatchRenderGuardContext",
            "AdmissionPatchRenderGuardShadow",
            "AdmissionPatchRenderGuardLocation",
            "AdmissionPatchRenderGuardTranslate",
            "AdmissionPatchRenderGuardSeal",
            "AdmissionPatchRenderGuardPrepare",
            "AdmissionPatchRenderGuardAccepted",
        ):
            self.assertIn(name, patch)


if __name__ == "__main__":
    unittest.main()
