from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxRenderGdiTests(unittest.TestCase):
    def test_portable_colorfill_preparation_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_gdi_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                "-I", str(SHARED / "include"),
                str(RENDER / "tests" / "render_gdi_test.c"),
                str(RENDER / "src" / "render_gdi.c"),
                str(SHARED / "src" / "apple_agx_gdi.c"),
                str(SHARED / "src" / "apple_agx_dma_shadow.c"),
                str(SHARED / "src" / "apple_agx_memory.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_wddm_wrapper_is_narrow_and_hardware_inert(self):
        callbacks = (RENDER / "src" / "callbacks.c").read_text()
        wrapper = (RENDER / "src" / "gdi_windows.c").read_text()
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()

        self.assertNotIn("FAIL2(AdmissionDdiRenderKm", callbacks)
        self.assertNotIn("FAIL2(AdmissionDdiPatch", callbacks)
        self.assertIn("DXGK_GDIOP_COLORFILL", wrapper)
        self.assertIn("DXGK_GDIROPCF_PATCOPY", wrapper)
        self.assertIn("AdmissionMemoryRuntimeResolveLocal", wrapper)
        self.assertIn("AdmissionGdiPatchAuthorized", wrapper)
        self.assertIn("AppleAgxDmaShadowSeal", wrapper)
        self.assertIn("AdmissionNonPagingPrivateRangeCovers", wrapper)
        self.assertNotIn("AppleAgxBackendRuntimeSubmit", wrapper)
        self.assertNotIn("FeatureReadyMask", wrapper)
        self.assertNotIn("APPLE_AGX_EXP208_SUPPORTED_GDI_PRIMITIVE_MASK",
                         wrapper)
        self.assertIn(r"src\render_gdi.c", project)
        self.assertIn(r"src\gdi_windows.c", project)
        self.assertIn(r"..\shared\src\apple_agx_gdi.c", project)
        self.assertIn(r"..\shared\src\apple_agx_dma_shadow.c", project)


if __name__ == "__main__":
    unittest.main()
