from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxRenderBackendImageTests(unittest.TestCase):
    def test_exact_exp208_image_is_materialized_rebased_and_relocated(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_backend_image_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                "-I", str(SHARED / "include"),
                str(RENDER / "tests" / "render_backend_image_test.c"),
                str(RENDER / "src" / "render_backend_image.c"),
                str(SHARED / "src" / "apple_agx_render_template.generated.c"),
                str(SHARED / "src" / "apple_agx_render_template_rebase.c"),
                str(SHARED / "src" / "apple_agx_relocation.c"),
                str(SHARED / "src" / "apple_agx_exp208_gdi.c"),
                str(SHARED / "src" / "apple_agx_exp208_framebuffer.c"),
                str(SHARED / "src" / "apple_agx_gdi.c"),
                str(SHARED / "src" / "apple_agx_memory.c"),
                str(SHARED / "src" / "apple_agx_exp208_adapter.c"),
                str(SHARED / "src" / "apple_agx_exp208_dynamic.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_production_lifecycle_borrows_the_memory_owner_tail(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        wrapper = (RENDER / "src" / "backend_image_windows.c").read_text()
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()

        self.assertIn("ADMISSION_BACKEND_IMAGE BackendImage", header)
        self.assertIn("AdmissionMemoryRuntimeBackendView", wrapper)
        self.assertIn("AdmissionBackendImagePrepare", wrapper)
        self.assertNotIn("ExAllocatePool", wrapper)
        self.assertLess(lifecycle.index("AdmissionMemoryRuntimeStart(context)"),
                        lifecycle.index("AdmissionBackendImageStart(context)"))
        self.assertLess(lifecycle.index("AdmissionBackendImageStart(context)"),
                        lifecycle.index("AdmissionSchedulerStart(context)"))
        self.assertIn(r"src\render_backend_image.c", project)
        self.assertIn(r"src\backend_image_windows.c", project)
        self.assertIn(
            r"..\shared\src\apple_agx_render_template.generated.c", project)
        self.assertIn(r"..\shared\src\apple_agx_relocation.c", project)
        self.assertIn(r"..\shared\src\apple_agx_exp208_gdi.c", project)
        submission = (RENDER / "src" / "submission_windows.c").read_text()
        scheduler = (RENDER / "src" / "scheduler_windows.c").read_text()
        self.assertIn("AdmissionBackendImageBindSubmission", submission)
        self.assertIn("AdmissionBackendImageReleaseSubmission", scheduler)


if __name__ == "__main__":
    unittest.main()
