from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderAllocationTests(unittest.TestCase):
    def test_portable_allocation_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_allocation_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_allocation_test.c"),
                str(RENDER / "src" / "render_allocation.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_wddm_allocation_callbacks_use_portable_contract(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        callbacks = (RENDER / "src" / "callbacks.c").read_text()
        windows = (RENDER / "src" / "allocation_windows.c").read_text()
        self.assertIn(r"src\render_allocation.c", project)
        self.assertIn(r"src\allocation_windows.c", project)
        for name in (
            "AdmissionDdiGetStandardAllocationDriverData",
            "AdmissionDdiCreateAllocation",
            "AdmissionDdiDestroyAllocation",
            "AdmissionDdiDescribeAllocation",
            "AdmissionDdiOpenAllocation",
            "AdmissionDdiCloseAllocation",
        ):
            self.assertNotIn(f"FAIL2({name}", callbacks)
            self.assertIn(name, windows)
        self.assertIn("AdmissionMemoryReady(&context->Memory)", windows)
        self.assertIn("ADMISSION_MEMORY_LOCAL_SEGMENT", windows)


if __name__ == "__main__":
    unittest.main()
