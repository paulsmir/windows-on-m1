"""T7d: exercise the real paging publication decision without a guest/broker."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"
PAGING = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c"


class ApertureNativeLeaf(unittest.TestCase):
    def test_scattered_replacement_retires_native_leaf_and_preserves_logical_pages(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Publishing four scattered PFNs as one native leaf aliases unrelated
        memory. Keeping the old leaf on replacement exposes stale backing.
        The real KMD must invalidate it, preserve all logical PFNs for CPU
        paging, and republish only after a complete contiguous group returns.
        This is a safety invariant, not a fix for GravityMark residency.
        """
        with tempfile.TemporaryDirectory(prefix="t7d-leaf-") as directory:
            directory = Path(directory)
            (directory / "leaf.inc").write_text(
                function_body(PAGING.read_text(), "AdmissionG3UpdateLeaf"))
            binary = directory / "replay"
            build = subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(directory),
                "-I", str(SHARED / "include"),
                str(ROOT / "tests/g4_aperture_native_leaf.c"),
                str(SHARED / "src/apple_agx_gpuva_g3_translation.c"),
                "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True,
                                 timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("T7d native-leaf safety: PASS", run.stdout)
