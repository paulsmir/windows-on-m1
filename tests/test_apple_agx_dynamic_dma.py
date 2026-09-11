from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class AppleAgxDynamicDmaTests(unittest.TestCase):
    def test_dynamic_job_is_carried_inside_the_exact_dma_submission(self):
        """Catches pointer carry, truncation or mutation between Render and worker."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "render_dynamic_dma_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"),
                "-I", str(SHARED / "include"),
                str(DRIVER / "tests/render_dynamic_dma_test.c"),
                str(DRIVER / "src/render_dynamic_dma.c"),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
