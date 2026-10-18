import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"


class AppleAgxG13ComputeWorkTests(unittest.TestCase):
    def test_source_derived_g13_v13_5_compute_work_layout(self):
        """Catches drift in CDM/preemption/microsequence/stamp offsets."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "apple_agx_g13_compute_work_test"
            build = subprocess.run(
                [
                    os.environ.get("CC", "clang"),
                    "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined",
                    "-DAPPLE_AGX_COMPUTE_WORK_STANDALONE=1",
                    "-I", str(SHARED / "include"),
                    str(SHARED / "tests/apple_agx_g13_compute_work_test.c"),
                    str(SHARED / "src/apple_agx_g13_compute_work.c"),
                    "-o", str(binary),
                ],
                cwd=ROOT, text=True, capture_output=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], cwd=ROOT, text=True,
                                    capture_output=True)
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
