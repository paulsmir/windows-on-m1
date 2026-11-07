"""W1: opt-in cross-block scoreboard state in agx_insert_waits.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Mesa's agx_insert_waits_local drains every scoreboard slot at the end of each
non-exit block, so GravityMark's BVH kernels issue about twice Apple's waits
(G1/EXP2122). With APPLE_AGX_WAIT_GLOBAL=1 the projected pass must remove
those block-end drains for real captured kernels, and the default (variable
absent or any value other than exactly "1") must stay byte-identical to the
local pass.
"""
import importlib.util
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = Path('/Users/pavel/public_windows/.local/experiments/EXP1969-no-wait/evidence/shader-capture')
KERNELS = ('bd59552039e0399a', 'a6e5a25c7bc49152', '97ae4b027aa43c6c')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WaitGlobal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not all((CAPTURE / f'{k}-s5-r0.bin').exists() for k in KERNELS):
            raise unittest.SkipTest('GravityMark shader captures not available')
        cls.tmp = tempfile.TemporaryDirectory(prefix='w1-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.tool = load(ROOT / 'tools/agx_shader_cost.py', 'w1_shader_cost')
        cls.binary = cls.tool.build(Path(cls.tmp.name))
        if not cls.binary:
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')

    def compile(self, kernel, setting):
        env = {k: v for k, v in os.environ.items() if k != 'APPLE_AGX_WAIT_GLOBAL'}
        if setting is not None:
            env['APPLE_AGX_WAIT_GLOBAL'] = setting
        run = subprocess.run([str(self.binary), str(CAPTURE / f'{kernel}-s5-r0.bin'), '--print-selected'],
                             env=env, capture_output=True, text=True, timeout=600)
        self.assertEqual(run.returncode, 0, run.stderr[-3000:])
        main = run.stdout.split('SELECTED MAIN BEGIN\n')[1].split('SELECTED MAIN END')[0]
        ops = [m.group(1) for m in re.finditer(r'^[ \t]*[0-9a-f]+:[ \t]+[0-9a-f]+[ \t]+(\S+)', main, re.M)]
        return main, ops

    def test_default_is_the_local_pass(self):
        for kernel in KERNELS:
            off, _ = self.compile(kernel, None)
            for value in ('', '0', '2', 'true'):
                self.assertEqual(self.compile(kernel, value)[0], off, (kernel, value))

    def test_global_state_removes_block_end_drains(self):
        for kernel in KERNELS:
            _, old = self.compile(kernel, None)
            _, new = self.compile(kernel, '1')
            self.assertLess(new.count('wait'), old.count('wait'), kernel)
            # only waits are removed: every other instruction is kept
            self.assertEqual([o for o in new if o != 'wait'], [o for o in old if o != 'wait'], kernel)


if __name__ == '__main__':
    unittest.main()
