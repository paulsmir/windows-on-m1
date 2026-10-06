"""EXP979: dispose() must not free a VA that a held screen slot still names.

Leaf page-table history at Explorer's copy predicate57 showed the failing VA
unmapped 60 ms earlier and reused by other allocations every ~200 ms: dispose
freed the BO's VA before AgxWin32NativeDeviceDestroyBo, which can refuse while
the slot is held, leaving a live slot naming a released VA.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / "drivers/apple-agx/mesa/winsys"
SHARED = ROOT / "drivers/apple-agx/shared"


class BoDisposeOrder(unittest.TestCase):
    def test_held_slot_keeps_its_va(self):
        source = (WINSYS / "agx_win32_asahi_bo.c").read_text()
        with tempfile.TemporaryDirectory(prefix="exp979-dispose-") as directory:
            tmp = Path(directory)
            (tmp / "dispose_function.inc").write_text(body(source, "dispose"))
            binary = tmp / "replay"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11",
                "-DAPPLE_AGX_GPUVA_WINSYS", "-Wall", "-Wextra",
                "-Wno-unused-function", "-fsanitize=address,undefined",
                "-I", str(tmp), "-I", str(WINSYS), "-I", str(SHARED / "include"),
                str(ROOT / "tests/g4_bo_dispose_replay.c"),
                str(WINSYS / "agx_win32_gpuva.c"), "-o", str(binary)], check=True)
            ran = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("EXP979 dispose order: PASS", ran.stdout)


if __name__ == "__main__":
    unittest.main()
