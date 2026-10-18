"""M2-f1: the contiguous-pool probe's frame analysis is exact."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class RenderPoolProbeTest(unittest.TestCase):
    def test_frame_analysis(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_pool_probe_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_pool_probe_test.c"),
                str(RENDER / "src" / "render_pool_probe.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_render_project_compiles_the_probe(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn(r"src\render_pool_probe.c", project)


if __name__ == "__main__":
    unittest.main()
