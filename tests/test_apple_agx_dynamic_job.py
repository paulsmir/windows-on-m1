from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class AppleAgxDynamicJobTests(unittest.TestCase):
    def test_copy_once_relocation_and_rollback(self):
        """Catches patching mutable UMD object bytes or retaining partial state."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "apple_agx_dynamic_job_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"),
                "-I", str(SHARED / "include"),
                str(DRIVER / "tests/apple_agx_dynamic_job_test.c"),
                str(DRIVER / "src/apple_agx_dynamic_job.c"),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
