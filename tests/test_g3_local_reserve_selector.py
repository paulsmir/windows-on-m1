from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "m1n1_windows/src/hv_agx_local_reserve.c"
TEST = ROOT / "m1n1_windows/tests/hv_agx_local_reserve_test.c"


class LocalReserveSelectorTest(unittest.TestCase):
    def test_real_c_selector_rejects_overlap_and_stage2_discontinuity(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "selector"
            build = subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", str(TEST), str(SOURCE), "-o", str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
