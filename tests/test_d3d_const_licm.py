"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
L4 hoists groupshared loads and the constant-buffer loads they address out
of loops (C4 traversal: two serialized memory round trips per node visit).
Hoisting a load_shared out of a loop that stores shared memory reads a stale
value: a real miscompile.  Runs the real pass on built NIR: the loads must
leave a writer-free loop (including from a conditional, under soft faults),
must stay in a loop that stores shared memory, and the pass must do nothing
when disabled.  Also pins the native projection's call (soft-fault gated).
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
GENERATOR = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'


class ConstLicm(unittest.TestCase):
    def test_invariant_loads_leave_only_writer_free_loops(self):
        import agx_shader_cost
        with tempfile.TemporaryDirectory() as d:
            binary = agx_shader_cost.build(d)
            if binary is None:
                self.skipTest('host Mesa / dxil-spirv trees absent')
            env = {k: v for k, v in os.environ.items() if k != 'APPLE_AGX_D3D_LICM_CONST'}
            run = lambda e: subprocess.run([str(binary), '--selftest-const-licm'], capture_output=True,
                                           text=True, env=e, check=True).stdout.split('\n')[:2]
            self.assertEqual(run({**env, 'APPLE_AGX_D3D_LICM_CONST': '1'}),
                             ['LICMCONST 0 shared-in-loop 0 const-in-loop 0',
                              'LICMCONST 1 shared-in-loop 1 const-in-loop 1'])
            self.assertEqual(run(env),
                             ['LICMCONST 0 shared-in-loop 1 const-in-loop 1',
                              'LICMCONST 1 shared-in-loop 1 const-in-loop 1'])

    def test_projection_gates_on_soft_faults_after_m3(self):
        text = GENERATOR.read_text()
        m3 = text.index('NIR_PASS(_, nir, agx_win32_hoist_loop_invariants);')
        l4 = text.index('NIR_PASS(_, nir, agx_win32_hoist_constant_loads, agx_win32_soft_faults(dev));')
        self.assertLess(m3, l4)
        self.assertEqual(text.count('agx_win32_hoist_constant_loads'), 1)


if __name__ == '__main__':
    unittest.main()
