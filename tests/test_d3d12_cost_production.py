"""The offline cost fixture can compile what the guest compiles, and says so.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
On 2026-11-10 three analyses (the N6 bench cut table, R17, the T5 test) read
offline numbers of GravityMark kernels that were not the guest's compile: the
fixture's default path uses stand-ins for Gallium's binding and sysval
lowerings, was run without the shader's root-descriptor masks, and its
robustness filter left SSBO atomics unchecked although the D3D12 owner's
context lowers them (a8260e4297efffb3: 793 instructions offline, 801 in the
guest's measure-d3d12-shader-stats row). The invariants:
  - `--production` with the masks of the shader's measure-d3d12-root-views
    receipt and the device's soft-fault feature reproduces the guest's
    statistics row for both kernels of the N6 table (EXP2319
    gm2319p0-umd.log, the same rows in every GravityMark log since EXP2280);
  - the fixture's robustness filter lowers exactly the intrinsics
    agx_win32_should_lower_robustness_d3d lowers;
  - a COST line names its shape, and the production shape cannot be had
    without the masks or together with flags that leave it;
  - `agx_shader_cost.py verify` takes masks, feature and row from the log
    and reports EQUAL or the differing fields.
The captures are evidence outside the repository (CAPTURE below, 10476 and
15416 bytes): the tests that need them skip with that reason when absent.
"""
import io
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
from test_dxbc_to_nir import local_tree  # noqa: E402

CAPTURE = Path('~/public_windows/.local/experiments/EXP1969-no-wait/evidence/shader-capture').expanduser()
FIELDS = ('instrs', 'alu', 'gprs', 'threads', 'scratch', 'spills', 'fills', 'loops', 'code', 'preamble')
# hash: (root UAV mask, root CBV mask, the guest's measure-d3d12-shader-stats values)
GUEST = {
    'a8260e4297efffb3': (0x1f, 0x3, (801, 520, 127, 832, 0, 0, 0, 1, 5400, 22)),
    '97ae4b027aa43c6c': (0x7f, 0x3, (1118, 715, 79, 1024, 0, 0, 0, 1, 8026, 30)),
}
BATCH = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'
FIXTURE = ROOT / 'tests/dxil_agx_cost_fixture.c'


def cases(text, start, end):
    """{intrinsic: the first return that follows its case label} of text[start:end]"""
    body = text[text.index(start):]
    body = body[:body.index(end)]
    result, pending = {}, []
    for label, value in re.findall(r'case nir_intrinsic_(\w+):|return ([^;]+);', body):
        if label:
            pending.append(label)
        else:
            result.update(dict.fromkeys(pending, value.strip()))
            pending = []
    return result


def guest_log(rows, features=1):
    """UMD log lines carrying the three receipts of each (hash, uav, cbv, applied, stats)"""
    meta = 'hr=0x00000000 pid=1 tid=1 session=1 qpc=1'
    lines = [f'measure-soft-faults {meta} 00000001 00000003 {features:08x}']
    for h, uav, cbv, applied, stats in rows:
        hi, lo = h[:8], h[8:]
        lines.append(f'measure-d3d12-root-views {meta} 00000005 {lo} {cbv:08x} {uav:08x} 00000003 0000007f '
                     f'{applied:08x}')
        values = ' '.join(f'{v:08x}' for v in stats)
        lines.append(f'measure-d3d12-shader-stats {meta} {hi} {lo} 00000005 00000100 {values} 00000000 00000000')
    return lines


class Receipts(unittest.TestCase):
    def test_masks_feature_and_rows_come_from_the_receipts(self):
        h = 'a8260e4297efffb3'
        uav, cbv, stats = GUEST[h]
        meta = 'hr=0x00000000 pid=1 tid=1 session=1 qpc=1'
        lines = guest_log([(h, uav, cbv, 1, stats), ('1111111122222222', 0x30, 0x1, 2, stats)]) + [
            # other stages and internal programs are not the application's compute shaders
            f'measure-d3d12-root-views {meta} 00000000 97efffb3 00000001 00000001 00000001 00000001 00000001',
            f'measure-d3d12-shader-stats {meta} 00000000 00000000 00000105 ' + ' '.join(['00000007'] * 13)]
        shaders, features = tool.guest_compiles(lines)
        self.assertEqual(features, {1})
        self.assertEqual(shaders[h], dict(stats=[stats], views=[(uav, cbv)]))
        # APPLE_AGX_D3D12_ROOT_BOUNDS=1 (applied 2): the guest compiled without marks
        self.assertEqual(shaders['1111111122222222']['views'], [(0, 0)])
        self.assertEqual(set(shaders), {h, '1111111122222222'})

    def test_a_log_without_the_device_feature_is_refused(self):
        lines = [l for l in guest_log([('a8260e4297efffb3', 0x1f, 0x3, 1, GUEST['a8260e4297efffb3'][2])])
                 if not l.startswith('measure-soft-faults')]
        with self.assertRaisesRegex(RuntimeError, 'measure-soft-faults'):
            tool.verify('unused', lines, CAPTURE)


class RobustnessFilter(unittest.TestCase):
    def test_fixture_filter_is_the_d3d_owner_filter(self):
        mesa = local_tree('reference', 'mesa')
        if not mesa:
            self.skipTest('pinned Mesa sources not available')
        gallium = cases((mesa / 'src/gallium/drivers/asahi/agx_state.c').read_text(),
                        '\nshould_lower_robustness(', '\n}\n')
        d3d = cases(BATCH.read_text(), '\nagx_win32_should_lower_robustness_d3d(', '\n}')
        fixture = cases(FIXTURE.read_text(), '\ngl_part_of_d3d_robustness(', '\n}\n')
        self.assertEqual(set(d3d.values()), {'false'})
        self.assertEqual(set(d3d), {'load_ubo', 'load_ssbo', 'store_ssbo'})
        expected = {k: v for k, v in gallium.items() if k not in d3d}
        # the context's robustness: *gl_robust there, the fixture's d3d_robust flag here
        self.assertEqual({k: v.replace('*gl_robust', '*(const bool *)data') for k, v in expected.items()}, fixture)
        self.assertIn('ssbo_atomic', fixture)
        self.assertIn('NIR_PASS(_, nir, nir_lower_robust_access, gl_part_of_d3d_robustness, &d3d_robust);',
                      FIXTURE.read_text())


class Compiled(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='cost-production-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.binary = tool.build(Path(cls.tmp.name))
        if not cls.binary:
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')

    def captures(self):
        missing = [h for h in GUEST if not (CAPTURE / f'{h}-s5-r0.bin').exists()]
        if missing:
            self.skipTest(f'GravityMark shader captures {missing} are not in the repository and not at {CAPTURE}')

    def run_fixture(self, *args):
        return subprocess.run([str(self.binary), *map(str, args)], capture_output=True, text=True, timeout=600)

    def test_production_compile_is_the_guest_compile(self):
        self.captures()
        for h, (uav, cbv, guest) in GUEST.items():
            with self.subTest(shader=h):
                c = tool.cost(self.binary, CAPTURE / f'{h}-s5-r0.bin', '--production', f'--root-views={uav:x},{cbv:x}',
                              '--soft-fault-loads')
                self.assertEqual(c['shape'], 'production')
                self.assertEqual(tuple(c[k] for k in FIELDS), guest)

    def test_every_cost_line_names_its_shape(self):
        self.assertEqual(tool.cost(self.binary, '--synthetic-uav-reads=4')['shape'], 'stand-in')
        self.assertEqual(tool.cost(self.binary, '--synthetic-uav-reads=4', '--production', '--root-views=0,0')['shape'],
                         'production')

    def test_production_needs_the_masks_and_refuses_other_shapes(self):
        missing = self.run_fixture('--synthetic-uav-reads=4', '--production')
        self.assertEqual(missing.returncode, 2)
        self.assertIn('--root-views=UAV,CBV is required', missing.stderr)
        self.assertNotIn('COST', missing.stdout)
        # a malformed mask pair is a missing one
        self.assertEqual(self.run_fixture('--synthetic-uav-reads=4', '--production', '--root-views=1f').returncode, 2)
        for flag in ('--no-d3d-robust', '--assume-in-bounds', '--soft-fault', '--fs-backend'):
            with self.subTest(flag=flag):
                other = self.run_fixture('--synthetic-uav-reads=4', '--production', '--root-views=0,0', flag)
                self.assertEqual(other.returncode, 2)
                self.assertIn('not the production shape', other.stderr)
                self.assertNotIn('COST', other.stdout)

    def test_verify_prints_equal_or_the_differing_fields(self):
        self.captures()
        (a, (auav, acbv, arow)), (b, (buav, bcbv, brow)) = GUEST.items()
        wrong = (brow[0] + 2, *brow[1:8], brow[8] - 6, brow[9])
        out = io.StringIO()
        result = tool.verify(self.binary, guest_log([(a, auav, acbv, 1, arow), (b, buav, bcbv, 1, wrong),
                                                     ('1111111122222222', 0, 0, 0, arow)]), CAPTURE, out=out)
        self.assertEqual(result, (1, 1, 1))
        lines = out.getvalue().splitlines()
        self.assertEqual(lines[0], '1111111122222222 NOT COMPARED (no capture)')
        self.assertEqual(lines[1], f'{b} root-views=7f,3 --soft-fault-loads DIFF offline-guest: instrs-2 code+6 '
                                   f'| guest {"/".join(map(str, wrong))}')
        self.assertEqual(lines[2], f'{a} root-views=1f,3 --soft-fault-loads EQUAL | guest {"/".join(map(str, arow))}')
        self.assertEqual(lines[3], '/'.join(FIELDS) + ': equal 1 different 1 not-compared 1')
        # without the device's soft faults the same shader is another compile
        out = io.StringIO()
        self.assertEqual(tool.verify(self.binary, guest_log([(a, auav, acbv, 1, arow)], features=0), CAPTURE, [a[8:]],
                                     out=out)[:2], (0, 1))
        self.assertIn('DIFF offline-guest:', out.getvalue())


if __name__ == '__main__':
    unittest.main()
