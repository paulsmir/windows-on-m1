"""N6: prefix-cut section profiler (APPLE_AGX_DIAG_CUT, class D diagnostic).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The profiler reads the GPU time of a kernel section as the difference of two
cut arms, which is only meaningful when a cut build is the uncut binary with
`stop` (88 00) written at one instruction boundary: every byte before the cut
and after its two bytes identical, the same statistics (thread count,
instructions, registers), and the cut points ordered E < B1 < B2 < L in the
program. A cut that moved code, changed the variant selection or landed inside
an instruction would attribute time to the wrong section. Without the variable
(or with a malformed one) the compiler must produce the same bytes.

N6b, point `@<offset>` (a byte offset of the main function, so that a block
inside an `if` or a loop can be named): an offset accepted anywhere but at the
start of a block would write `stop` into operand bytes or into the trailing
padding; the kernel would then run a different instruction instead of a
prefix, and the receipt would still report a cut. A parser that accepted
`@12x` or overflowed on a long number would cut somewhere else than the arm
names. So: an accepted offset changes exactly the two bytes at that offset,
which is an instruction of the uncut listing; every other spelling or offset
leaves the binary untouched.
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
KERNELS = ('a8260e4297efffb3', '97ae4b027aa43c6c')
POINTS = ('E', 'B1', 'B2', 'L')
LINE = re.compile(r'^[ \t]*([0-9a-f]+):[ \t]+[0-9a-f]+[ \t]+(\S+)', re.M)
STATS = ('instrs', 'alu', 'gprs', 'threads', 'scratch', 'spills', 'fills', 'loops', 'code', 'preamble')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class DiagCut(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not all((CAPTURE / f'{k}-s5-r0.bin').exists() for k in KERNELS):
            raise unittest.SkipTest('GravityMark shader captures not available')
        cls.tmp = tempfile.TemporaryDirectory(prefix='n6-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.tool = load(ROOT / 'tools/agx_shader_cost.py', 'n6_shader_cost')
        cls.binary = cls.tool.build(Path(cls.tmp.name))
        if not cls.binary:
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')
        cls.cache = {}

    def compile(self, kernel, value):
        """(main bytes, instruction offsets, statistics) under APPLE_AGX_DIAG_CUT=value"""
        key = (kernel, value)
        if key not in self.cache:
            env = {k: v for k, v in os.environ.items() if k != 'APPLE_AGX_DIAG_CUT'}
            if value is not None:
                env['APPLE_AGX_DIAG_CUT'] = value
            run = subprocess.run([str(self.binary), str(CAPTURE / f'{kernel}-s5-r0.bin'), '--print-selected',
                                  '--hex-main'], env=env, capture_output=True, text=True, timeout=600)
            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
            main = run.stdout.split('SELECTED MAIN BEGIN\n')[1].split('SELECTED MAIN END')[0]
            code = bytes.fromhex(next(l for l in run.stdout.splitlines() if l.startswith('MAIN HEX '))[9:])
            cost = dict(re.findall(r'([a-z-]+) (\S+)', next(l for l in run.stdout.splitlines() if l.startswith('COST '))))
            self.cache[key] = (code, [(int(m.group(1), 16), m.group(2)) for m in LINE.finditer(main)],
                               {k: cost[k] for k in STATS})
        return self.cache[key]

    def cut_offset(self, kernel, point):
        """None when the point does not exist (binary unchanged)"""
        full, lines, stats = self.compile(kernel, None)
        cut, _, cut_stats = self.compile(kernel, f'any:{point}')
        self.assertEqual(cut_stats, stats, (kernel, point))
        self.assertEqual(len(cut), len(full), (kernel, point))
        if cut == full and point != 'E':
            return None
        differing = [i for i in range(len(full)) if full[i] != cut[i]]
        # stop is 88 00; an original byte may already equal one of the two
        self.assertTrue(differing and differing[-1] - differing[0] <= 1, (kernel, point, differing[:8]))
        candidates = [o for o in (differing[0], differing[0] - 1) if o >= 0 and cut[o:o + 2] == b'\x88\x00'
                      and full[:o] == cut[:o] and full[o + 2:] == cut[o + 2:]]
        offsets = {o for o, _ in lines}
        base = min(offsets)
        boundary = [o for o in candidates if o + base in offsets]
        self.assertEqual(len(boundary), 1, (kernel, point, candidates))
        return boundary[0]

    def test_absent_or_malformed_variable_changes_nothing(self):
        for kernel in KERNELS:
            full = self.compile(kernel, None)[0]
            for value in ('', '0', '1', 'any', 'any:', 'any:B3', 'any:e', 'E', '97efffb3', '97efffb3:E',
                          '7aa43c6c:L', '97EFFFB3:E', 'anyy:E', '7aa43c6:E'):
                # a well-formed hash form names a kernel only through the D3D12
                # owner's tag, which the fixture never sets
                self.assertEqual(self.compile(kernel, value)[0], full, (kernel, value))

    def test_cut_is_the_uncut_binary_with_stop_at_an_instruction_boundary(self):
        for kernel in KERNELS:
            offsets = {point: self.cut_offset(kernel, point) for point in POINTS}
            self.assertEqual(offsets['E'], 0, kernel)
            present = [offsets[p] for p in POINTS if offsets[p] is not None]
            self.assertEqual(present, sorted(set(present)), (kernel, offsets))
            self.assertGreater(len(present), 1, (kernel, offsets))

    def at(self, kernel, offset):
        """True: `stop` at exactly that offset, nothing else changed. False: binary unchanged."""
        full, lines, stats = self.compile(kernel, None)
        cut, _, cut_stats = self.compile(kernel, f'any:@{offset}')
        self.assertEqual(cut_stats, stats, (kernel, offset))
        if cut == full:
            return False
        self.assertEqual(cut, full[:offset] + b'\x88\x00' + full[offset + 2:], (kernel, offset))
        self.assertIn(offset + lines[0][0], {o for o, _ in lines}, (kernel, offset))
        return True

    def test_offset_point_is_refused_unless_it_spells_a_decimal_offset(self):
        for kernel in KERNELS:
            full = self.compile(kernel, None)[0]
            for value in ('any:@', 'any:@-1', 'any:@+4', 'any:@4x', 'any:@ 4', 'any:@0x0', 'any:@00000000',
                          'any:@4294967296', 'any:@@0', 'any:@0:E', '97efffb3:@0', '7aa43c6c:@0'):
                self.assertEqual(self.compile(kernel, value)[0], full, (kernel, value))

    def test_offset_point_cuts_only_where_a_block_begins(self):
        for kernel in KERNELS:
            full, lines, _ = self.compile(kernel, None)
            base = lines[0][0]
            order = [o - base for o, _ in lines]
            ops = {o - base: op for o, op in lines}
            # byte by byte over the head of the function: never inside an instruction
            head = [n for n in range(order[12]) if self.at(kernel, n)]
            self.assertTrue(set(head) <= set(order), (kernel, head))
            # the function entry is a block: the same build as E
            self.assertEqual(head[0], 0, kernel)
            self.assertEqual(self.compile(kernel, 'any:@0')[0], self.compile(kernel, 'any:E')[0], kernel)
            # the loop header named by L is a block: the same build as L
            loop = self.cut_offset(kernel, 'L')
            if loop is not None:
                self.assertEqual(self.compile(kernel, f'any:@{loop}')[0], self.compile(kernel, 'any:L')[0], kernel)
            # B<n> cuts behind a barrier inside its block; when that is not the
            # start of a block the offset form refuses it instead of splitting
            for point in ('B1', 'B2'):
                barrier = self.cut_offset(kernel, point)
                if barrier is not None and self.at(kernel, barrier):
                    self.assertEqual(self.compile(kernel, f'any:@{barrier}')[0],
                                     self.compile(kernel, f'any:{point}')[0], (kernel, point))
            # a block inside an `if` can be named: the body behind the first
            # skip-if-no-thread jump begins a block
            skip = next(i for i, o in enumerate(order) if ops[o] == 'jmp_none')
            nested = order[skip + 1]
            self.assertTrue(self.at(kernel, nested), (kernel, nested))
            self.assertNotIn(nested, [self.cut_offset(kernel, p) for p in POINTS], kernel)
            # the instruction behind it, the middle of an instruction, the last
            # instruction's tail, the padding and anything past the code
            end = order[-1]
            for n in (order[skip + 2], nested + 1, end + 1, len(full) - 2, len(full) - 1, len(full), 9999999):
                self.assertFalse(self.at(kernel, n), (kernel, n))

    def test_points_follow_the_marked_instructions(self):
        for kernel in KERNELS:
            _, lines, _ = self.compile(kernel, None)
            base = lines[0][0]
            ops = {o - base: op for o, op in lines}
            order = sorted(ops)
            barriers = [o for o in order if ops[o] == 'barrier']
            for n, point in ((0, 'B1'), (1, 'B2')):
                offset = self.cut_offset(kernel, point)
                if offset is None:
                    continue
                # the instruction right before the cut is a threadgroup barrier,
                # and it is the n-th or a later one (nested barriers are not counted)
                before = order[order.index(offset) - 1]
                self.assertEqual(ops[before], 'barrier', (kernel, point))
                self.assertGreaterEqual(barriers.index(before), n, (kernel, point))


if __name__ == '__main__':
    unittest.main()
