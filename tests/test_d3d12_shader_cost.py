"""RC74: AGX cost of D3D12 compute shaders (tools/agx_shader_cost.py).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The D3D buffer-robustness lowering (winsys/agx_win32_d3d_robustness.h) put
each UAV load and store in its own if/else; the AGX backend waits for every
outstanding memory access at the end of a block (agx_insert_waits.c), so an
in-bounds invocation overlapped none of its UAV accesses. Compiled with the
pinned AGX backend (tests/dxil_agx_cost_fixture.c):
- a kernel of K float4 UAV reads and one float4 write must take no branch for
  its loads and one for its store, and wait at most once more than the same
  kernel without robustness (D3D-H6 form: K + 1 branches and wait points);
- the eight consecutive stores of fixtures/dxil/conv-probe/cs_c47b.dxil
  must be tested as one run: no more in-bounds wait points than without
  robustness (D3D-H6 form: 8);
- no scratch or spills are introduced into these kernels;
- an indexable temp of more than 64 bytes read at a run-time index
  (DXBC x#, a DXIL alloca; GravityMark's uint[32] x3 and uint[64] kernels)
  must live on the AGX stack, not in Asahi's register if/else trees: fewer
  instructions and GPRs than APPLE_AGX_D3D_SCRATCH_ARRAYS=0 (Asahi's rule
  alone: more than 256 bytes), while a 64-byte one stays in registers.
RC75: D3D12 root descriptors have no size, so accesses through them are
not checked: a kernel whose UAV slots are all root descriptors
(--root-views) must compile exactly as the unchecked kernel (same
instructions, no branch, its float4 loads whole), while the same slots as
descriptor-table views keep the D3D checks (RED before: the lowering
checked every slot).
RC78: a Texture2DArray sample clamps its layer through a descriptor crawl
(libagx loads of the texture descriptor); the Windows backend has no soft
faults, so Asahi left every crawl under divergent control flow in the
shader (GravityMark 806ee7b1bdb1944a: 30 such samples, 84% of the frame's
compute). A bound texture's crawl reads the per-draw descriptor table and
must be marked speculatable and run in the preamble: the shader of
--synthetic-array-samples=8 then issues no descriptor loads (no IC work),
fewer instructions than APPLE_AGX_D3D_CRAWL_SPECULATE=0, and the NIR handed
to the backend is otherwise unchanged (same ALU, memory and texture ops:
the layer clamp stays). A descriptor-heap handle (DXBC_HEAP_HANDLE_MARK,
an app index into the heap mirror) must not be marked.
RC78: dxil-spirv structures early exits as single-pass loops
(loop { if (c) { ...; break; } else { ...; break; } }), which Asahi's
optimization loop keeps (GravityMark's lighting kernel: 3 of its 24
loops). With Honeykrisp's nir_opt_loop and nir_opt_if in the frontend tail
(dxbc_nir_finalize) --synthetic-break-loops=4 must compile to no loop and
fewer instructions than APPLE_AGX_D3D_CF_OPT=0, with the same ALU and memory
operations.
The receipt ranking must join dispatches with their compute statistics by
the CS hash's low 32 bits, count direct groups and threads, keep indirect
dispatches as counts, and ignore other processes.
"""
from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/fixtures/dxil/conv-probe/cs_c47b.dxil'


def load_tool():
    spec = importlib.util.spec_from_file_location('agx_shader_cost', ROOT / 'tools/agx_shader_cost.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def row(name, words, pid=7):
    return f'{name} hr=0x00000000 pid={pid} tid=1 session=1 qpc=1 ' + ' '.join(f'{w:08x}' for w in words)


def stats(hash_hi, hash_lo, instrs, block, stage=5):
    v = [0] * 16
    v[0], v[1], v[2], v[3], v[4], v[6], v[7], v[14] = hash_hi, hash_lo, stage, 400, instrs, 100, 384, block
    return row('measure-d3d12-shader-stats', v)


class ReceiptRanking(unittest.TestCase):
    def test_dispatches_join_their_compute_statistics(self):
        tool = load_tool()
        b8x8 = 8 | 8 << 11 | 1 << 22
        b256 = 256 | 1 << 11 | 1 << 22
        lines = [
            stats(0x1, 0xaaaa0001, 2800, b8x8),
            stats(0x1, 0xaaaa0001, 900, b8x8),          # a smaller variant: the largest one counts
            stats(0x2, 0xbbbb0002, 300, b256),
            stats(0x3, 0xcccc0003, 100, 0, stage=0),     # a vertex shader is not a compute shader
            row('measure-d3d12-dispatch', [0, 240, 135, 1, 3, 0xaaaa0001]),
            row('measure-d3d12-dispatch', [0, 240, 135, 1, 3, 0xaaaa0001]),
            row('measure-d3d12-dispatch', [0, 0, 0, 0, 2, 0xbbbb0002]),   # indirect
            row('measure-d3d12-dispatch', [0, 4, 1, 1, 2, 0xbbbb0002]),
            row('measure-d3d12-dispatch', [7, 4, 1, 1, 2, 0xbbbb0002]),   # refused: not run
            row('measure-d3d12-dispatch', [0, 99, 1, 1, 2, 0xbbbb0002], pid=9),  # another process
            row('measure-d3d12-dispatch', [0, 1, 1, 1, 2, 0xdddd0004]),   # no statistics (a compile before diagnostics)
        ]
        result = tool.rank_log(lines)
        self.assertEqual(result['pid'], 7)
        ranking = {r['tag']: r for r in result['ranking']}
        big = ranking['aaaa0001']
        self.assertEqual((big['dispatches'], big['direct'], big['indirect'], big['groups']), (2, 2, 0, 2 * 240 * 135))
        self.assertEqual(big['threads'], 2 * 240 * 135 * 64)
        self.assertEqual(big['instrs'], 2800)
        self.assertEqual(big['score'], 2 * 240 * 135 * 64 * 2800)
        self.assertEqual(big['block'], (8, 8, 1))
        small = ranking['bbbb0002']
        self.assertEqual((small['dispatches'], small['direct'], small['indirect'], small['groups']), (2, 1, 1, 4))
        self.assertEqual(small['threads'], 4 * 256)
        self.assertIsNone(ranking['dddd0004']['instrs'])
        self.assertEqual(result['ranking'][0]['tag'], 'aaaa0001')


class CompiledCost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool()
        cls.tmp = tempfile.TemporaryDirectory(prefix='shader-cost-')
        try:
            cls.binary = cls.tool.build(cls.tmp.name)
        except Exception:
            cls.tmp.cleanup()
            raise
        if not cls.binary:
            cls.tmp.cleanup()
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def costs(self, source):
        robust = self.tool.cost(self.binary, source)
        unchecked = self.tool.cost(self.binary, source, '--no-d3d-robust')
        self.assertTrue(robust and unchecked, source)
        return robust, unchecked

    def test_uav_reads_overlap_and_the_store_is_one_branch(self):
        for reads in (1, 4, 8):
            with self.subTest(reads=reads):
                robust, unchecked = self.costs(f'--synthetic-uav-reads={reads}')
                self.assertEqual(robust['hot-branches'], 1)
                self.assertLessEqual(robust['hot-mem-blocks'], unchecked['hot-mem-blocks'] + 1)
                self.assertEqual((robust['scratch'], robust['spills']), (0, 0))

    def test_indexed_temp_arrays_live_on_the_stack(self):
        asahi_rule = {'APPLE_AGX_D3D_SCRATCH_ARRAYS': '0'}
        for elements in (17, 32, 64):
            with self.subTest(elements=elements):
                stack = self.tool.cost(self.binary, f'--synthetic-local-array={elements}')
                registers = self.tool.cost(self.binary, f'--synthetic-local-array={elements}', env=asahi_rule)
                self.assertGreaterEqual(stack['scratch'], 4 * elements)
                self.assertEqual(registers['scratch'], 0)
                self.assertLess(stack['instrs'], registers['instrs'])
                self.assertLess(stack['gprs'], registers['gprs'])
        small = self.tool.cost(self.binary, '--synthetic-local-array=16')
        self.assertEqual(small, self.tool.cost(self.binary, '--synthetic-local-array=16', env=asahi_rule))
        self.assertEqual(small['scratch'], 0)

    def test_root_descriptor_slots_compile_unchecked(self):
        for source in ('--synthetic-uav-reads=4', FIXTURE):
            with self.subTest(source=str(source)):
                robust, unchecked = self.costs(source)
                root = self.tool.cost(self.binary, source, '--root-views=ff,3fff')
                self.assertGreater(root['root-marked'], 0)
                for key in ('instrs', 'alu', 'gprs', 'loads', 'stores', 'hot-branches', 'nir-mem'):
                    self.assertEqual(root[key], unchecked[key], key)
                self.assertLess(root['instrs'], robust['instrs'])
                # a slot that is not a root descriptor keeps its checks
                table = self.tool.cost(self.binary, source, '--root-views=0,0')
                self.assertEqual(table, robust)

    def test_array_sample_crawls_run_in_the_preamble(self):
        asahi_rule = {'APPLE_AGX_D3D_CRAWL_SPECULATE': '0'}
        source = '--synthetic-array-samples=8'
        hoisted = self.tool.cost(self.binary, source)
        shader = self.tool.cost(self.binary, source, env=asahi_rule)
        self.assertGreaterEqual(hoisted['crawl-speculated'], 8)
        self.assertEqual(shader['crawl-speculated'], 0)
        self.assertEqual(hoisted['ic'], 0)
        self.assertGreater(shader['ic'], 0)
        self.assertLess(hoisted['instrs'], shader['instrs'])
        self.assertLessEqual(hoisted['gprs'], shader['gprs'])
        self.assertGreater(hoisted['preamble'], shader['preamble'])
        for key in ('nir-alu', 'nir-mem', 'nir-tex', 'loops'):
            self.assertEqual(hoisted[key], shader[key], key)
        heap = self.tool.cost(self.binary, source + ',heap')
        self.assertEqual(heap['crawl-speculated'], 0)
        self.assertEqual(heap, self.tool.cost(self.binary, source + ',heap', env=asahi_rule))

    def test_single_pass_loops_fold_into_their_branches(self):
        source = '--synthetic-break-loops=4'
        folded = self.tool.cost(self.binary, source)
        kept = self.tool.cost(self.binary, source, env={'APPLE_AGX_D3D_CF_OPT': '0'})
        self.assertEqual(kept['loops'], 4)
        self.assertEqual(folded['loops'], 0)
        self.assertLess(folded['instrs'], kept['instrs'])
        self.assertLessEqual(folded['gprs'], kept['gprs'])
        for key in ('nir-alu', 'nir-mem', 'loads', 'stores'):
            self.assertEqual(folded[key], kept[key], key)

    def test_a_run_of_uav_stores_is_tested_once(self):
        robust, unchecked = self.costs(FIXTURE)
        self.assertEqual(robust['hot-mem-blocks'], unchecked['hot-mem-blocks'])
        self.assertEqual(robust['hot-branches'], 1)
        self.assertEqual((robust['scratch'], robust['spills']), (0, 0))


if __name__ == '__main__':
    unittest.main()
