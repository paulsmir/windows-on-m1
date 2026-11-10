"""T5b: samples reach texture state registers (steps A and B2), default off.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
  - a silent default change: with both switches off the guest's GravityMark
    lighting kernel 806ee7b1bdb1944a must still compile to its guest
    measure-d3d12-shader-stats row and the DXIL frontend must still hand out
    SRV slots in (space, register) order;
  - step A (APPLE_AGX_D3D12_CRAWL_BINDFUL): a sample whose crawl forced the
    bindless handle of a table entry c < 16 must take register c, while the
    crawl that clamps its layer stays (a lost clamp samples past the array),
    and D3D12 heap handles (DXBC_HEAP_HANDLE_MARK) stay bindless (their
    descriptor is not in any register);
  - step B2 (APPLE_AGX_D3D12_SRV_STATIC_FIRST): the map stays dense and
    unique, every declaration keeps consecutive slots in register order (a
    dynamically indexed t[N] is addressed as base + index), single-register
    declarations come first; the DDI side of a table that spans several
    declarations is test_d3d12_register_spaces (static-first);
  - the switches reach the compile only through the D3D12 owner: the backend
    flag for A (projected after agx_nir_lower_texture, before the crawl
    speculation) and the SRV limit flag for B2; no getenv in the frontend.
The capture is evidence outside the repository (EXP1969); tests that need it
skip with that reason when absent.
"""
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / 'tools'))
import agx_shader_cost as tool  # noqa: E402

CAPTURE = Path('/Users/pavel/public_windows/.local/experiments/EXP1969-no-wait/evidence/shader-capture')
LIGHTING = '806ee7b1bdb1944a'
LIGHTING_FLAGS = ('--production', '--root-views=e,3', '--soft-fault-loads')
LIGHTING_GUEST = (3450, 2808, 207, 512, 0, 0, 0, 21, 23978, 229)  # EXP2319 gm2319p0-umd.log
FIELDS = ('instrs', 'alu', 'gprs', 'threads', 'scratch', 'spills', 'fills', 'loops', 'code', 'preamble')
BATCH = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
FRONTEND = ROOT / 'drivers/apple-agx/mesa/dxil/agx_dxil_to_nir.c'


def samples(stdout):
    """SAMPLES line -> (reg (inner, loop, other), bindless (inner, loop, other), moved)"""
    line = next(l for l in stdout.splitlines() if l.startswith('SAMPLES '))
    v = list(map(int, re.findall(r'\d+', line)))
    return tuple(v[0:3]), tuple(v[3:6]), v[6]


def cost_fields(stdout):
    line = next(l for l in stdout.splitlines() if l.startswith('COST '))
    return dict(re.findall(r'([a-z-]+) (\S+)', line[5:]))


class Sources(unittest.TestCase):
    def test_step_a_runs_after_the_crawls_are_built_and_only_under_the_backend_flag(self):
        text = BATCH.read_text()
        body = text[text.index('def project_crawl_bindful('):text.index('def project_d3d_robustness(')]
        self.assertIn("function(state,'agx_shader_initialize')", body)
        lower = body.index('   NIR_PASS(_, nir, agx_nir_lower_texture);\n\'\'\',')
        self.assertIn('if (windows_bindful && windows_bindful->D3dCrawlBindful)', body[lower:])
        self.assertIn('NIR_PASS(_, nir, agx_win32_d3d_crawl_bindful, so->info.nr_bindful_textures,', body)
        # the robustness projection runs first, so the crawl speculation follows step A
        calls = text[text.index('    project_query_timeline(out, save)\n'):]
        self.assertLess(calls.index('project_d3d_robustness(out,save)'), calls.index('project_crawl_bindful(out,save)'))

    def test_switches_are_read_once_by_the_owner_and_b2_is_a_limit_flag(self):
        owner = OWNER.read_text()
        self.assertIn('static const bool on = t5b_switch("APPLE_AGX_D3D12_CRAWL_BINDFUL");', owner)
        self.assertIn('static const bool on = t5b_switch("APPLE_AGX_D3D12_SRV_STATIC_FIRST");', owner)
        self.assertIn('owner->Backend.D3dCrawlBindful = t5b_crawl_bindful() ? 1 : 0;', owner)
        self.assertIn('flagged[DXBC_BINDING_SRV] |= DXBC_BINDING_SRV_STATIC_FIRST;', owner)
        self.assertIn('dxil_binding_limits(), options, &dxil)', owner)
        frontend = FRONTEND.read_text()
        self.assertNotIn('getenv', frontend[frontend.index('\nfreeze('):frontend.index('\nplaced(')])
        self.assertIn('job->limits[DXBC_BINDING_SRV] & DXBC_BINDING_SRV_STATIC_FIRST', frontend)


class Compiled(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='t5b-bindful-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.binary = tool.build(Path(cls.tmp.name))
        if not cls.binary:
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')

    def run_fixture(self, *args):
        r = subprocess.run([str(self.binary), *map(str, args)], capture_output=True, text=True, timeout=600)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        return r.stdout

    def lighting(self, *flags):
        blob = CAPTURE / f'{LIGHTING}-s5-r0.bin'
        if not blob.exists():
            self.skipTest(f'GravityMark capture {blob} is not in the repository')
        return self.run_fixture(blob, *LIGHTING_FLAGS, '--print-samples', '--print-bindings', *flags)

    def srv_map(self, stdout):
        """[(id, reg, slot)] of the SRV bindings"""
        rows = re.findall(r'BINDING kind 1 space 0 reg (\d+) slot (\d+) flags 0', stdout)
        ids = re.findall(r'BINDING kind 1 ', stdout)
        self.assertEqual(len(rows), len(ids), 'heap or other-space SRVs are not expected here')
        return [(int(r), int(s)) for r, s in rows]

    def test_both_switches_off_is_the_guest_compile_and_the_register_order_map(self):
        out = self.lighting()
        c = cost_fields(out)
        self.assertEqual(tuple(int(c[k]) for k in FIELDS), LIGHTING_GUEST)
        self.assertEqual(samples(out), ((3, 13, 5), (16, 12, 1), 0))
        srv = self.srv_map(out)
        self.assertEqual([s for _, s in srv], list(range(len(srv))))
        self.assertEqual([r for r, _ in sorted(srv, key=lambda x: x[1])], sorted(r for r, _ in srv))

    def test_step_a_moves_only_crawl_bindless_samples_below_16_and_keeps_the_crawls(self):
        off, a = self.lighting(), self.lighting('--crawl-bindful')
        # entry 6 (2DArray, one sample outside loops) takes register 6; the
        # shadow map (entry 17) and the dynamic arrays stay bindless
        self.assertEqual(samples(a), ((3, 13, 6), (16, 12, 0), 1))
        # the layer-clamp crawls are still there (and still speculated)
        self.assertEqual(cost_fields(a)['crawl-speculated'], cost_fields(off)['crawl-speculated'])
        self.assertEqual(self.srv_map(a), self.srv_map(off))

    def test_step_a_leaves_heap_handles_and_takes_every_slot_array_sample(self):
        slot = self.run_fixture('--synthetic-array-samples=4', '--production', '--root-views=0,0', '--crawl-bindful')
        self.assertEqual(samples(slot), ((0, 0, 4), (0, 0, 0), 4))
        heap = self.run_fixture('--synthetic-array-samples=4,heap', '--production', '--root-views=0,0', '--crawl-bindful')
        self.assertEqual(samples(heap), ((0, 0, 0), (0, 0, 4), 0))
        plain = self.run_fixture('--synthetic-array-samples=4,heap', '--production', '--root-views=0,0')
        self.assertEqual(cost_fields(plain)['binary-fnv'], cost_fields(heap)['binary-fnv'])

    def test_step_b2_map_is_dense_contiguous_and_single_registers_first(self):
        out = self.lighting('--srv-static-first')
        srv = dict(self.srv_map(out))  # reg -> slot
        self.assertEqual(sorted(srv.values()), list(range(len(srv))))
        # declarations of the lighting kernel: t0..t6 and t17 single, t7[8], t15[2]
        singles = [0, 1, 2, 3, 4, 5, 6, 17]
        ranges = [list(range(7, 15)), [15, 16]]
        self.assertEqual(sorted(srv[r] for r in singles), list(range(len(singles))))
        self.assertEqual([srv[r] for r in singles], sorted(srv[r] for r in singles))
        for regs in ranges:
            slots = [srv[r] for r in regs]
            self.assertEqual(slots, list(range(slots[0], slots[0] + len(regs))))
            self.assertGreater(slots[0], max(srv[r] for r in singles))
        # the arm compiles the same samples; only A moves them
        self.assertEqual(samples(out)[2], 0)

    def test_steps_a_and_b2_leave_only_dynamic_samples_bindless(self):
        out = self.lighting('--crawl-bindful', '--srv-static-first')
        reg, bindless, moved = samples(out)
        self.assertEqual(bindless, (0, 4, 0))
        self.assertEqual(moved, 25)
        c = cost_fields(out)
        self.assertLessEqual(int(c['gprs']), LIGHTING_GUEST[2])
        self.assertLess(int(c['instrs']), LIGHTING_GUEST[0])
        self.assertEqual(int(c['spills']), 0)


if __name__ == '__main__':
    unittest.main()
