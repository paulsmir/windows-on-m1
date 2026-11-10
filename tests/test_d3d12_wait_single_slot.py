"""SS1: opt-in single scoreboard slot in agx_insert_waits.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The upstream slot choice ("first slot with nr_pending == 0") puts two loads
issued back to back into slots a and b, so GravityMark's frustum kernel
a8260e4297efffb3 pays two waits per plane test where Apple's twin pays one
(R10, night 2026-11-10). With APPLE_AGX_WAIT_SINGLE_SLOT=1 the projected pass
must queue every asynchronous op on slot a, change nothing but waits, and
remove waits in the four captured culling kernels; the default (variable
absent or any value other than exactly "1") must stay byte-identical, with
and without APPLE_AGX_WAIT_GLOBAL.
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
KERNELS = ('a8260e4297efffb3', '97ae4b027aa43c6c', '3467e84ef582ac2c', 'e5ffd902fc968f7a')
VARIABLES = ('APPLE_AGX_WAIT_SINGLE_SLOT', 'APPLE_AGX_WAIT_GLOBAL')
LINE = re.compile(r'^[ \t]*[0-9a-f]+:[ \t]+[0-9a-f]+[ \t]+(\S+)(.*)$', re.M)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WaitSingleSlot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not all((CAPTURE / f'{k}-s5-r0.bin').exists() for k in KERNELS):
            raise unittest.SkipTest('GravityMark shader captures not available')
        cls.tmp = tempfile.TemporaryDirectory(prefix='ss1-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.tool = load(ROOT / 'tools/agx_shader_cost.py', 'ss1_shader_cost')
        cls.binary = cls.tool.build(Path(cls.tmp.name))
        if not cls.binary:
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')
        cls.cache = {}

    def compile(self, kernel, single, glob=None):
        key = (kernel, single, glob)
        if key not in self.cache:
            env = {k: v for k, v in os.environ.items() if k not in VARIABLES}
            if single is not None:
                env['APPLE_AGX_WAIT_SINGLE_SLOT'] = single
            if glob is not None:
                env['APPLE_AGX_WAIT_GLOBAL'] = glob
            run = subprocess.run([str(self.binary), str(CAPTURE / f'{kernel}-s5-r0.bin'), '--print-selected'],
                                 env=env, capture_output=True, text=True, timeout=600)
            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
            main = run.stdout.split('SELECTED MAIN BEGIN\n')[1].split('SELECTED MAIN END')[0]
            self.cache[key] = (main, [(m.group(1), m.group(2).strip()) for m in LINE.finditer(main)])
        return self.cache[key]

    def test_default_is_the_upstream_slot_choice(self):
        for kernel in KERNELS:
            for glob in (None, '1'):
                off = self.compile(kernel, None, glob)[0]
                for value in ('', '0', '2', 'true'):
                    self.assertEqual(self.compile(kernel, value, glob)[0], off, (kernel, value, glob))

    def test_single_slot_changes_only_waits_and_slots(self):
        for kernel in KERNELS:
            for glob in (None, '1'):
                old = self.compile(kernel, None, glob)[1]
                new = self.compile(kernel, '1', glob)[1]
                self.assertLess(sum(o == 'wait' for o, _ in new), sum(o == 'wait' for o, _ in old), (kernel, glob))
                # the same instructions in the same order, waits aside
                self.assertEqual([o for o, _ in new if o != 'wait'], [o for o, _ in old if o != 'wait'], (kernel, glob))
                # the default really uses both slots; the option never names slot b
                self.assertTrue(any(o == 'wait' and a == 'b' for o, a in old), (kernel, glob))
                self.assertFalse([(o, a) for o, a in new if a == 'b' or a.endswith(', b')], (kernel, glob))
                # operands other than the slot letter are untouched (jump
                # targets are byte offsets and move with the removed waits)
                strip = lambda rows: [(o, '' if o.startswith('jmp') else re.sub(r', [ab]$', '', a))
                                      for o, a in rows if o != 'wait']
                self.assertEqual(strip(new), strip(old), (kernel, glob))


if __name__ == '__main__':
    unittest.main()
