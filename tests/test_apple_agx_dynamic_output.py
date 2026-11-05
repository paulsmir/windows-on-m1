from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class AppleAgxDynamicOutputTests(unittest.TestCase):
    def test_triangle_oracle_rejects_clear_replay_and_corruption(self):
        """Catches treating a background clear as dynamic geometry output."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "render_dynamic_output_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"),
                "-I", str(SHARED / "include"),
                str(DRIVER / "tests/render_dynamic_output_test.c"),
                str(DRIVER / "src/render_dynamic_output.c"),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
