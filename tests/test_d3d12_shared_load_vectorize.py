"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The AGX vectorization modes omit shared memory, leaving adjacent shared reads
scalar. The opt-in must reduce real retained loads without widening accesses
across holes or synchronization, vectorizing stores, or changing the default.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_d3d12_latency_schedule import ROOT, load


class SharedLoadVectorize(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='shared-vectorize-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.tool = load(ROOT / 'tools/agx_shader_cost.py', 'k2_shader_cost')
        cls.binary = cls.tool.build(Path(cls.tmp.name))
        if not cls.binary:
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')

    def compile(self, setting, mode=0):
        env = dict(os.environ)
        for key in ('APPLE_AGX_SHARED_LOAD_VECTORIZE', 'APPLE_AGX_SHARED_STORE_VECTORIZE', 'APPLE_AGX_LOCAL_LOAD_SCHED',
                    'APPLE_AGX_LATENCY_SCHED', 'APPLE_AGX_RA_PACK', 'APPLE_AGX_RA_WEB',
                    'AGX_MESA_DEBUG'):
            env.pop(key, None)
        if setting is not None:
            env['APPLE_AGX_SHARED_LOAD_VECTORIZE'] = setting
        run = subprocess.run([str(self.binary), f'--synthetic-shared-adjacent={mode}',
                              '--no-d3d-robust', '--print-selected'],
                             env=env, capture_output=True, text=True, timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr[-3000:])
        main = run.stdout.split('SELECTED MAIN BEGIN\n')[1].split('SELECTED MAIN END')[0]
        instructions = [m.groups() for m in re.finditer(
            r'^[ \t]*[0-9a-f]+:[ \t]+[0-9a-f]+[ \t]+(\S+)[ \t]*(.*)$', main, re.M)]
        return main, instructions

    @staticmethod
    def loads(instructions):
        return [(i, args) for i, (op, args) in enumerate(instructions) if op == 'lload']

    @staticmethod
    def load_words(loads):
        return sum(len(args.rsplit(',', 1)[-1].strip()) for _, args in loads)

    def test_adjacent_reads_use_vector_loads_only_when_enabled(self):
        off, old = self.compile('0')
        on, new = self.compile('1')
        self.assertEqual(len(self.loads(old)), 8)
        self.assertEqual(len(self.loads(new)), 2, 'adjacent shared loads remain scalar')
        self.assertEqual(self.load_words(self.loads(new)), 8, 'all eight dwords must be read')
        self.assertNotEqual(on, off)
        for value in (None, '', '2', '10', 'true'):
            self.assertEqual(self.compile(value)[0], off, value)
        for instructions in (old, new):
            stores = [i for i, (op, _) in enumerate(instructions) if op == 'lstore']
            barriers = [i for i, (op, _) in enumerate(instructions) if op == 'barrier']
            self.assertEqual(len(stores), 16, 'shared stores must stay scalar')
            self.assertEqual(len(barriers), 1)
            self.assertLess(max(stores), barriers[0])
            self.assertGreater(min(i for i, _ in self.loads(instructions)), barriers[0])

    def test_holes_are_not_fetched(self):
        off, old = self.compile('0', 2)
        on, new = self.compile('1', 2)
        self.assertEqual(len(self.loads(new)), 8)
        self.assertEqual(on, off)

    def test_uncertain_dynamic_adjacency_is_not_forced(self):
        off, old = self.compile('0', 3)
        on, new = self.compile('1', 3)
        self.assertEqual(len(self.loads(new)), 8)
        self.assertEqual(on, off)

    def test_barrier_separates_read_groups(self):
        _, instructions = self.compile('1', 1)
        barriers = [i for i, (op, _) in enumerate(instructions) if op == 'barrier']
        self.assertEqual(len(barriers), 2)
        loads = self.loads(instructions)
        self.assertTrue(any(barriers[0] < i < barriers[1] for i, _ in loads))
        self.assertTrue(any(i > barriers[1] for i, _ in loads))
        self.assertEqual(len(loads), 3, 'three words before barrier, five after')
        self.assertEqual(self.load_words([(i, a) for i, a in loads if i < barriers[1]]), 3)
        self.assertEqual(self.load_words([(i, a) for i, a in loads if i > barriers[1]]), 5)


if __name__ == '__main__':
    unittest.main()
