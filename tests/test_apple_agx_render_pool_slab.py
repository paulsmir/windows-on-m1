"""M2-f3e: segment-3 granules come from 2 MiB slabs, packed, returned when empty."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class RenderPoolSlabTest(unittest.TestCase):
    def test_slab_allocator(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_pool_slab_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_pool_slab_test.c"),
                str(RENDER / "src" / "render_pool_slab.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_render_project_compiles_the_slabs(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn(r"src\render_pool_slab.c", project)


if __name__ == "__main__":
    unittest.main()
