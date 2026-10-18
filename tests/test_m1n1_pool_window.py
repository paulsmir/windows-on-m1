"""M2-f2: the hypervisor's pool window keeps its aliasing invariants."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "m1n1_windows/src/hv_agx_pool_window.c"
TEST = ROOT / "m1n1_windows/tests/hv_agx_pool_window_test.c"


class PoolWindowTest(unittest.TestCase):
    def test_window_invariants_and_mmio(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "pool-window"
            build = subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-fsanitize=address,undefined", str(TEST),
                                    str(SOURCE), "-o", str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_hypervisor_builds_and_routes_the_window(self):
        makefile = (ROOT / "m1n1_windows/Makefile").read_text()
        self.assertIn("hv_agx_pool_window.o", makefile)
        mmio = (ROOT / "m1n1_windows/src/hv_agx_power_mmio.c").read_text()
        self.assertIn("hv_agx_pool_window_mmio(&pool_window", mmio)
        self.assertIn("pool_window_select();", mmio)


if __name__ == "__main__":
    unittest.main()
