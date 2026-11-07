"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Dense 32-bit compute shared stores remain scalar after K2. Enabling the new
lowering must reduce retained store instructions while preserving exactly the
written bytes, last-writer values, alias observations and barrier partitions.
Holes, partial masks, narrow values, uncertain addresses and independent load
vectorization controls must not gain a wider or reordered memory access.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_d3d12_latency_schedule import ROOT, load


class SharedStoreVectorize(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='shared-store-vectorize-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.tool = load(ROOT / 'tools/agx_shader_cost.py', 'k3_shader_cost')
        cls.binary = cls.tool.build(Path(cls.tmp.name))
        if not cls.binary:
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')

    def compile(self, setting, mode=0, loads='1', source=None):
        env = {k: v for k, v in os.environ.items()
               if not k.startswith('APPLE_AGX_') and k != 'AGX_MESA_DEBUG'}
        if setting is not None:
            env['APPLE_AGX_SHARED_STORE_VECTORIZE'] = setting
        env['APPLE_AGX_SHARED_LOAD_VECTORIZE'] = loads
        run = subprocess.run([str(self.binary), source or f'--synthetic-shared-stores={mode}',
                              '--no-d3d-robust', '--print-selected'],
                             env=env, capture_output=True, text=True, timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr[-3000:])
        main = run.stdout.split('SELECTED MAIN BEGIN\n')[1].split('SELECTED MAIN END')[0]
        instructions = [m.groups() for m in re.finditer(
            r'^[ \t]*[0-9a-f]+:[ \t]+[0-9a-f]+[ \t]+(\S+)[ \t]*(.*)$', main, re.M)]
        return run.stdout, main, instructions

    @staticmethod
    def stores(instructions):
        return [(i, args) for i, (op, args) in enumerate(instructions) if op == 'lstore']

    def oracle(self, output, unit=4):
        memory = bytearray([0xcc] * 128)
        snapshots, reads, atomics = [], [], []
        for line in output.splitlines():
            if not line.startswith('ORACLE_'):
                continue
            name, *values = line.split()
            values = list(map(int, values))
            if name == 'ORACLE_STORE':
                address, bits, mask, *words = values
                size = bits // 8
                for i, value in enumerate(words):
                    if mask & (1 << i):
                        offset = address + i * size
                        self.assertLessEqual(offset + size, len(memory))
                        memory[offset:offset + size] = value.to_bytes(size, 'little')
            elif name == 'ORACLE_LOAD':
                address, bits, count = values
                size = bits // 8
                self.assertEqual((size * count) % unit, 0)
                reads.extend(int.from_bytes(memory[address + i:address + i + unit], 'little')
                             for i in range(0, size * count, unit))
            elif name == 'ORACLE_ADD':
                address, value = values
                old = int.from_bytes(memory[address:address + 4], 'little')
                atomics.append(old)
                memory[address:address + 4] = ((old + value) & 0xffffffff).to_bytes(4, 'little')
            else:
                self.assertEqual(name, 'ORACLE_BARRIER')
                snapshots.append(bytes(memory))
        self.assertTrue(snapshots, 'oracle did not inspect optimized shared memory')
        return bytes(memory), snapshots, reads, atomics

    def expected_memory(self, mode):
        # Independent, hand-defined fixture contract, not derived from NIR.
        words = list(range(0x40, 0x48))
        if mode in (3, 6):
            words[0] = 0xabcd
        if mode == 4:
            words[0], words[2] = 0x81, 0x83
        if mode == 5:
            words[0] = 0x47
        size = 2 if mode == 7 else 1 if mode == 8 else 4
        stride = 8 if mode == 2 else size
        memory = bytearray([0xcc] * 128)
        for i, word in enumerate(words):
            memory[i * stride:i * stride + size] = word.to_bytes(size, 'little')
        return bytes(memory), words

    def test_dense_stores_reduce_retained_instructions_and_preserve_bytes(self):
        off, off_main, old = self.compile('0')
        on, on_main, new = self.compile('1')
        self.assertEqual(len(self.stores(old)), 8)
        self.assertEqual(len(self.stores(new)), 2, 'eight dwords must use two vector stores')
        self.assertLess(len(new), len(old))
        for result in (off, on):
            memory, snapshots, reads, atomics = self.oracle(result)
            expected, words = self.expected_memory(0)
            self.assertEqual(memory, expected)
            self.assertEqual(snapshots, [expected])
            self.assertCountEqual(reads, words)
            self.assertEqual(atomics, [])
        for value in (None, '', '2', '10', 'true'):
            self.assertEqual(self.compile(value)[1], off_main, value)
        self.assertNotEqual(on_main, off_main)

    def test_barrier_partitions_remain_visible_in_binary_and_oracle(self):
        output, _, instructions = self.compile('1', 1)
        stores = self.stores(instructions)
        barriers = [i for i, (op, _) in enumerate(instructions) if op == 'barrier']
        self.assertEqual(len(stores), 3)
        self.assertEqual(len(barriers), 2)
        self.assertEqual(sum(len(a.rsplit(',', 1)[-1].strip()) for i, a in stores if i < barriers[0]), 3)
        self.assertEqual(sum(len(a.rsplit(',', 1)[-1].strip()) for i, a in stores if barriers[0] < i < barriers[1]), 5)
        memory, snapshots, _, _ = self.oracle(output)
        expected, _ = self.expected_memory(1)
        self.assertEqual(memory, expected)
        self.assertEqual(snapshots, [expected[:12] + bytes([0xcc] * 116), expected])

    def test_holes_masks_overlaps_and_aliasing_keep_memory_semantics(self):
        for mode in (2, 3, 4, 5, 6, 7, 8):
            with self.subTest(mode=mode):
                for setting in ('0', '1'):
                    output, _, instructions = self.compile(setting, mode)
                    memory, snapshots, reads, atomics = self.oracle(output, 2 if mode == 7 else 1 if mode == 8 else 4)
                    expected, words = self.expected_memory(mode)
                    self.assertEqual(memory, expected)
                    self.assertEqual(snapshots, [expected])
                    if mode != 6:  # The vectorizer may legally forward this earlier read.
                        self.assertCountEqual(reads, words)
                    else:
                        self.assertTrue(sorted(reads) in (sorted(words), sorted([0x40, *words])))
                    self.assertEqual(atomics, [0x40] if mode == 5 else [])
                    if mode in (2, 7, 8):
                        self.assertEqual(len(self.stores(instructions)), 8)
                        self.assertTrue(all(a.endswith(', x') for _, a in self.stores(instructions)))

    def test_store_switch_does_not_enable_load_vectorization(self):
        source = '--synthetic-shared-adjacent=0'
        _, _, old = self.compile('0', loads='0', source=source)
        _, _, stores_only = self.compile('1', loads='0', source=source)
        self.assertEqual(sum(o == 'lload' for o, _ in old), 8)
        self.assertEqual(sum(o == 'lload' for o, _ in stores_only), 8)
        # These dynamic 16-bit store addresses have no proven adjacency.
        self.assertEqual(len(self.stores(stores_only)), 16)
        _, _, both = self.compile('1', loads='1', source=source)
        self.assertEqual(sum(o == 'lload' for o, _ in both), 2)


if __name__ == '__main__':
    unittest.main()
