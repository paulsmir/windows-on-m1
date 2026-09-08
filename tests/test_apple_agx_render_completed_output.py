from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class CompletedOutputTests(unittest.TestCase):
    def test_output_idle_is_generation_safe(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "output_queue"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests/render_output_queue_test.c"),
                str(RENDER / "src/render_output_queue.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_present_query_is_built_from_verified_frame_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "present_query"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests/render_qualification_test.c"),
                str(RENDER / "src/render_qualification.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_transaction_owned_output_and_display_lease(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "completed_output"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                "-I", str(SHARED / "include"),
                str(RENDER / "tests/render_completed_output_test.c"),
                str(RENDER / "src/render_completed_output.c"),
                str(RENDER / "src/render_allocation.c"),
                str(RENDER / "src/render_qualification.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_windows_wiring_uses_transaction_and_display_lease(self):
        backend = (RENDER / "src/backend_platform_windows.c").read_text()
        scanout = (RENDER / "src/scanout_windows.c").read_text()
        submit = (RENDER / "src/submission_windows.c").read_text()
        allocation = (RENDER / "src/allocation_windows.c").read_text()
        producer = (
            ROOT / "drivers/apple-agx/windows/one-shot/apple_agx_d3dkmt_render.c"
        ).read_text()

        self.assertIn("ADMISSION_COMPLETED_OUTPUT CompletedOutput", backend)
        self.assertIn("AdmissionCompletedOutputContains(", backend)
        self.assertLess(
            backend.index("AdmissionBackendImageCaptureOutput("),
            backend.index("AdmissionBackendImageReleaseSubmission("),
        )
        self.assertIn("AdmissionCompletedOutputMarkPacketRetired", backend)
        self.assertIn("AdmissionCompletedOutputMarkNotified", backend)
        self.assertIn("AdmissionCompletedOutputBeginPresent", backend)
        self.assertIn("AdmissionCompletedOutputTransferToDisplay", scanout)
        self.assertIn("ADMISSION_DISPLAY_OUTPUT_LEASE ActiveLease", scanout)
        self.assertIn("AdmissionScanoutAllowsRender", submit)
        self.assertIn("AdmissionScanoutRetireAllocation", allocation)
        self.assertNotIn("AdmissionRecordVisibleAgx(Context, &receipt)", scanout)
        self.assertIn("WaitForPresentation(", producer)
        self.assertNotIn("Sleep(10000u);", producer)


if __name__ == "__main__":
    unittest.main()
