"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
M1: the DXIL translation forms byte offsets as (i & M) * 2^s. The mask is an
identity only when M keeps the low 32 - s bits; dropping it then lets the
load/store vectorizer see adjacent words (GravityMark C4 scalar loads).
Dropping a narrower mask would change addresses (a real miscompile), and a
kept identity mask hides vectorization. Runs the real pass on built NIR.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))


class IndexMasks(unittest.TestCase):
    def test_identity_masks_only(self):
        import agx_shader_cost
        with tempfile.TemporaryDirectory() as d:
            binary = agx_shader_cost.build(d)
            if binary is None:
                self.skipTest('host Mesa / dxil-spirv trees absent')
            env = {k: v for k, v in os.environ.items() if k != 'APPLE_AGX_DXIL_INDEX_MASK'}
            r = subprocess.run([str(binary), '--selftest-index-masks'], capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(r.stdout.split('\n')[:4],
                             ['MASK 0 dropped', 'MASK 1 kept', 'MASK 2 dropped', 'MASK 3 dropped'])
            r = subprocess.run([str(binary), '--selftest-index-masks'], capture_output=True, text=True,
                               env={**env, 'APPLE_AGX_DXIL_INDEX_MASK': '0'})
            self.assertEqual(r.stdout.count('kept'), 4)


if __name__ == '__main__':
    unittest.main()
