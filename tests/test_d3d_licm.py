"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
M3: Asahi never runs nir_opt_licm, so a loop-invariant texture descriptor
crawl (the D3D layer clamp of an array sample) runs on every iteration of a
loop around the sample: 2 device loads and their waits per iteration in
GravityMark's planet PS ray-march loops. The real pass must leave no crawl
load inside such a loop, the control variable must restore the old
placement, and the native projection must run the pass after RC78's crawl
placement in agx_shader_initialize.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
GENERATOR = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'


def loop_loads(listing):
    pcs = [(int(m.group(1), 16), m.group(2)) for m in
           re.finditer(r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)', listing, re.M)]
    loops = []
    for m in re.finditer(r'^\s*([0-9a-f]+):.*jmp_any\s+(-\d+)', listing, re.M):
        end = int(m.group(1), 16)
        start = end + int(m.group(2))
        loops.append(sum(1 for pc, op in pcs if start <= pc <= end and op == 'load'))
    return loops


class D3dLicm(unittest.TestCase):
    def test_crawl_loads_leave_the_loop(self):
        import agx_shader_cost
        with tempfile.TemporaryDirectory() as d:
            binary = agx_shader_cost.build(d)
            if binary is None:
                self.skipTest('host Mesa / dxil-spirv trees absent')
            env = {k: v for k, v in os.environ.items() if k != 'APPLE_AGX_D3D_LICM'}
            run = lambda e: subprocess.run([str(binary), '--synthetic-loop-samples=2', '--print-selected'],
                                           capture_output=True, text=True, env=e, check=True).stdout
            self.assertEqual(loop_loads(run({**env, 'APPLE_AGX_D3D_LICM': '0'})), [2, 2])
            self.assertEqual(loop_loads(run(env)), [0, 0])

    def test_projection_runs_licm_after_crawl_placement(self):
        text = GENERATOR.read_text()
        crawl = text.index('agx_win32_speculate_texture_crawls,')
        licm = text.index('NIR_PASS(_, nir, agx_win32_hoist_loop_invariants);')
        self.assertLess(crawl, licm)
        self.assertLess(licm, text.index("''')", crawl))


if __name__ == '__main__':
    unittest.main()
