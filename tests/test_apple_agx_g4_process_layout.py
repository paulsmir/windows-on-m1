"""Exercise process buffer capacities shared by Mesa and AGX4 parsing."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class G4ProcessLayoutTests(unittest.TestCase):
    def test_render_geometry_sets_process_buffer_minima(self):
        with tempfile.TemporaryDirectory(prefix="g4-layout-") as directory:
            binary = Path(directory) / "layout-test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I",
                str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_g4_process_layout_test.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
