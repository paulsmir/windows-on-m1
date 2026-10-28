"""rc80: TVB (tiler heap) growth past the 32-block floor.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2003b/EXP2010: the firmware buffer manager overflowed its fixed 4 MiB heap
3-7 times per frame (Stats max_pages 1104 against 128 backed pages), so
GravityMark ran partial renders inside TA. The growth policy must follow
Asahi buffer.rs auto_grow (2 x max_pages above the backed pages grows to
3 x max_pages), stay inside the heap range and fall back softly; the TVB
window must continue the heap VA in the private window's root slot with the
leaf/PTE geometry the KMD maps; and root parking must accept the TVB leaves
(the single-leaf check refuses them, which would fail InitialUpdate).
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'


class TvbGrowthTests(unittest.TestCase):
    def test_policy_geometry_fallback_and_root_parking(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp) / 'tvb')
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I', str(SHARED / 'include'),
                str(ROOT / 'tests/g4_tvb_growth_test.c'),
                str(SHARED / 'src/apple_agx_gpuva_g3_graph.c'),
                str(SHARED / 'src/apple_agx_gpuva_broker_v5_client.c'),
                '-o', exe], check=True)
            result = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('g4_tvb_growth_test: PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
