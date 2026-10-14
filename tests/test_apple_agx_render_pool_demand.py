"""M2-f3: segment-3 granules commit on first reference and release only when
nothing can still reach them."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class RenderPoolDemandTest(unittest.TestCase):
    def test_granule_state_machine(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_pool_demand_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_pool_demand_test.c"),
                str(RENDER / "src" / "render_pool_demand.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_render_project_compiles_the_pool(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn(r"src\render_pool_demand.c", project)


if __name__ == "__main__":
    unittest.main()
