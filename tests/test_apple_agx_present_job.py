"""G2 HW2: the KMD present job (shared/src/apple_agx_present_job.c).

What it catches: a present job whose CDM stream, USC words or argument block
do not express the requested blt -- wrong launch grid, pipeline or code
offsets, constants not mapped, arguments at the wrong uniforms, rect or
stretch arithmetic errors, or a job built from rects outside a surface. A
CPU model executes the stream exactly as the KMD wrote it and must match a
reference blt; out-of-bounds or malformed requests are refused.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"


class PresentJob(unittest.TestCase):
    def test_cpu_model_matches_reference_blt(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "present_job"
            built = subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(ROOT / "tests/apple_agx_present_job_test.c"),
                str(SHARED / "src/apple_agx_present_job.c"),
                str(SHARED / "src/apple_agx_cdm.c"), "-o", str(exe)],
                capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("present job: PASS", ran.stdout)


if __name__ == "__main__":
    unittest.main()
