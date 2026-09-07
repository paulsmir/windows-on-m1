from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderPrepatchTests(unittest.TestCase):
    def test_resident_render_is_prepatched_and_submit_adopts_fence(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        render = (RENDER / "src" / "umd_render_windows.c").read_text()
        patch = (RENDER / "src" / "gdi_windows.c").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()
        callbacks = (RENDER / "src" / "callbacks.c").read_text()

        self.assertIn("ADMISSION_PREPATCHED_RENDER", header)
        self.assertIn("AdmissionGdiAdoptPrepatchedPacket", header)
        self.assertIn("AdmissionMemoryRuntimeResolveLocal", render)
        self.assertIn("AdmissionPrepatchedCapture", render)
        self.assertIn("visibleDestination", render)
        self.assertIn("AdmissionGdiAdoptPrepatchedPacket", patch)
        self.assertIn("ADMISSION_PREPATCH_ADOPT_GUARD", guards)
        self.assertIn("PREPATCH_ADOPT_RETURN", patch)
        self.assertIn("AdmissionPrepatchAdoptGuardPending", patch)
        self.assertIn("AdmissionPrepatchAdoptGuardShadow", patch)
        self.assertNotIn("AdmissionPrepatchAdoptGuardAccepted", patch)
        self.assertIn("AppleAgxDmaShadowSeal", patch)
        self.assertIn("AdmissionGdiAdoptPrepatchedPacket", submit)
        self.assertIn("AdmissionPrepatchedActive", callbacks)

    def test_output_patch_reference_is_still_published(self):
        render = (RENDER / "src" / "umd_render_windows.c").read_text()
        self.assertIn("location->AllocationIndex", render)
        self.assertIn("Args->pPatchLocationListOut = location + 1", render)


if __name__ == "__main__":
    unittest.main()
