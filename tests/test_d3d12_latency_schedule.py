"""RC96: latency-aware AGX scheduling (mesa/scripts/native-asahi-latency-schedule.py).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Asahi's bottom-up pressure scheduler (agx_pressure_schedule.c) places every
memory load right before its first use, so agx_insert_waits stalls on it one
or two instructions later (GravityMark's lighting kernel 806ee7b1bdb1944a:
51 of 83 waits). The projection reschedules each block over the same
dependency graph so independent work separates a load from its use, and
agx_compile.c keeps a rescheduled compile only if the register allocator's
result keeps the pinned compile's occupancy, spills, fills and scratch.
Invariants:
- the projection anchors of the three pinned compiler files occur exactly
  once (a Mesa update that moves them fails the build, not the scheduler);
- the closure projects the units it compiles beside the projected
  agx_compiler.h (every compiler unit sees one agx_context layout);
- APPLE_AGX_LATENCY_SCHED=0 is the pinned compiler, byte for byte;
- by default no shader loses threads or gains spills, fills, scratch or more
  than 1/32 instructions (EXP-style occupancy regressions);
- independent loads are issued ahead of their uses: a kernel of eight float4
  loads summed in a chain waits at most twice instead of seven times.
"""
import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-latency-schedule.py'
CLOSURE = ROOT / 'drivers/apple-agx/mesa/scripts/build-asahi-runtime-closure.py'
FIXTURE = ROOT / 'tests/fixtures/dxil/conv-probe/cs_c47b.dxil'


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def local(*parts):
    tree = os.environ.get('AGX_LOCAL_TREE')
    bases = ([Path(tree)] if tree else []) + [ROOT / '.local', ROOT.parents[1] / '.local']
    for base in bases:
        path = base.joinpath(*parts)
        if path.exists():
            return path
    return None


class Projection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mesa = local('reference', 'mesa')
        if cls.mesa is None:
            raise unittest.SkipTest('pinned Mesa sources not available')
        cls.module = load(SCRIPT, 'native_latency_schedule')

    def test_each_compiler_file_projects_exactly_once(self):
        for relative, project in self.module.FILES.items():
            with self.subTest(file=relative):
                text = (self.mesa / relative).read_text()
                projected = project(text)
                self.assertNotEqual(projected, text)
                with self.assertRaises(RuntimeError):
                    project(projected)
                crlf = self.module.project_bytes(project, text.replace('\n', '\r\n').encode()).decode()
                self.assertEqual(crlf, projected.replace('\n', '\r\n'))

    def test_context_fields_are_appended(self):
        header = self.module.project_compiler_h((self.mesa / 'src/asahi/compiler/agx_compiler.h').read_text())
        body = header[header.index('typedef struct {\n   nir_shader *nir;'):header.index('} agx_context;')]
        self.assertTrue(body.rstrip().endswith('unsigned windows_latency_blocks;'))

    def test_closure_projects_the_units_beside_the_header(self):
        closure = load(CLOSURE, 'closure_latency')
        with tempfile.TemporaryDirectory() as tmp:
            include = Path(tmp) / 'compiler-evidence'
            include.mkdir()
            for relative in self.module.FILES:
                shutil.copy2(self.mesa / relative, include / Path(relative).name)
            units = [include / 'agx_compile.c', include / 'agx_pressure_schedule.c',
                     include / 'agx_register_allocate.c', include / 'agx_insert_waits.c']
            provenance = closure.prepare_latency_schedule(units, include)
            self.assertEqual(set(provenance['files']), {'agx_compiler.h', 'agx_compile.c', 'agx_pressure_schedule.c',
                                                        'agx_register_allocate.c', 'agx_insert_waits.c'})
            for entry in provenance['files'].values():
                self.assertNotEqual(entry['before_sha256'], entry['after_sha256'])
            self.assertIn('agx_win32_compile_function', (include / 'agx_compile.c').read_text())
            elsewhere = Path(tmp) / 'other'
            elsewhere.mkdir()
            shutil.copy2(self.mesa / 'src/asahi/compiler/agx_compile.c', elsewhere / 'agx_compile.c')
            with self.assertRaises(RuntimeError):
                closure.prepare_latency_schedule([elsewhere / 'agx_compile.c', units[1]], include)

    def test_the_closure_projects_after_the_bitfield_header(self):
        text = CLOSURE.read_text()
        self.assertLess(text.index('prepare_unsigned_bitfield_header(\n            compiler_units, out)'),
                        text.index('prepare_latency_schedule(\n            compiler_units, bitfield_include)'))
        self.assertLess(text.index('prepare_latency_schedule(\n            compiler_units, bitfield_include)'),
                        text.index('del bitfield_include'))


def last_main_listing(dump):
    """The last post-RA listing (physical registers) of the dump."""
    lines = dump.split('\n')
    starts = [i for i, line in enumerate(lines) if line.startswith('block0 {')]
    sections = [lines[a:b] for a, b in zip(starts, starts[1:] + [len(lines)])]
    sections = [s for s in sections if any('$r' in line for line in s)]
    return [line.strip() for line in sections[-1] if line.startswith('   ') and not line.strip().startswith('}')]


def load_wait_distances(listing):
    """Per wait: instructions since the oldest memory/texture load it covers."""
    distances, first, index = [], None, 0
    for line in listing:
        if line.startswith('wait'):
            if first is not None:
                distances.append(index - first)
            first = None
            continue
        if first is None and re.search(r'= (device_load|texture_sample|texture_load|image_load)\b', line):
            first = index
        index += 1
    return distances


class Compiled(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load(ROOT / 'tools/agx_shader_cost.py', 'agx_shader_cost')
        cls.tmp = tempfile.TemporaryDirectory(prefix='latency-schedule-')
        try:
            pinned, projected = Path(cls.tmp.name) / 'pinned', Path(cls.tmp.name) / 'projected'
            pinned.mkdir()
            projected.mkdir()
            cls.pinned = cls.tool.build(pinned, pinned=True)
            cls.projected = cls.tool.build(projected) if cls.pinned else None
        except Exception:
            cls.tmp.cleanup()
            raise
        if not cls.pinned or not cls.projected:
            cls.tmp.cleanup()
            raise unittest.SkipTest('host Mesa / dxil-spirv trees not available')
        capture = local('experiments', 'EXP1969-no-wait', 'evidence', 'shader-capture', '806ee7b1bdb1944a-s5-r0.bin')
        cls.sources = [('--synthetic-uav-reads=8', '--no-d3d-robust'), ('--synthetic-uav-reads=8',),
                       ('--synthetic-array-samples=4',), ('--synthetic-local-array=32',), (str(FIXTURE),)]
        if capture:
            cls.sources.append((str(capture), '--root-views=e,3', '--soft-fault-loads'))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def cost(self, binary, source, target=None):
        env = {'APPLE_AGX_LATENCY_SCHED': target} if target is not None else {}
        result = self.tool.cost(binary, source[0], *source[1:], env=env)
        self.assertTrue(result, source)
        return result

    def test_kill_switch_is_the_pinned_compiler(self):
        for source in self.sources:
            with self.subTest(source=source):
                self.assertEqual(self.cost(self.projected, source, '0'), self.cost(self.pinned, source))

    def test_latency_never_costs_occupancy_spills_or_scratch(self):
        for source in self.sources:
            with self.subTest(source=source):
                pinned, latency = self.cost(self.pinned, source), self.cost(self.projected, source)
                self.assertGreaterEqual(latency['threads'], pinned['threads'])
                for key in ('spills', 'fills', 'scratch'):
                    self.assertLessEqual(latency[key], pinned[key], key)
                self.assertLessEqual(latency['instrs'], pinned['instrs'] + pinned['instrs'] // 32)

    def test_input_remat_is_opt_in_and_removes_planet_spills(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        RC124 (default level 2 = inputs and read-only global loads, =1
        inputs only) re-issues pure values far from their def. With
        APPLE_AGX_REMAT_INPUTS=0 every source compiles as before; on the
        GravityMark HD 100k full-screen PS (EXP2139 capture) it must cut
        spills without occupancy or instruction growth."""
        for source in self.sources:
            with self.subTest(source=source):
                self.assertEqual(self.tool.cost(self.projected, source[0], *source[1:],
                                                env={'APPLE_AGX_REMAT_INPUTS': '0'}),
                                 self.cost(self.projected, source))
        planet = local('experiments', 'EXP2138-nullps', 'evidence', 'EXP2139-capture', 'capture',
                       '428487dae22a5191-s4-r0.bin')
        if not planet:
            self.skipTest('EXP2139 capture not available')
        args = ('--fs-backend', '--assume-in-bounds')
        base = self.tool.cost(self.projected, str(planet), *args, env={'APPLE_AGX_REMAT_INPUTS': '0'})
        remat = self.tool.cost(self.projected, str(planet), *args, env={'APPLE_AGX_REMAT_INPUTS': '1'})
        self.assertTrue(base and remat)
        self.assertGreaterEqual(remat['threads'], base['threads'])
        self.assertLess(remat['spills'], base['spills'] // 2)
        self.assertLess(remat['instrs'], base['instrs'])
        loads = self.tool.cost(self.projected, str(planet), *args, env={'APPLE_AGX_REMAT_INPUTS': '2'})
        self.assertEqual(self.tool.cost(self.projected, str(planet), *args, env={}), loads)
        self.assertTrue(loads)
        self.assertGreaterEqual(loads['threads'], base['threads'])
        self.assertEqual(loads['spills'], 0)
        self.assertLess(loads['instrs'], remat['instrs'])

    def test_opt_in_shared_loads_issue_ahead_of_their_uses(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The latency classifier omits local_load, leaving independent shared
        reads adjacent to their consumers even when scheduling is enabled.
        The opt-in must separate reads without removing stores or the barrier,
        and must preserve the existing compiler binary when disabled.
        """
        source = ('--synthetic-shared-reads=8', '--no-d3d-robust')
        results, distances = {}, {}
        for setting in ('0', '1'):
            env = {'APPLE_AGX_LOCAL_LOAD_SCHED': setting, 'APPLE_AGX_RA_PACK': '0'}
            results[setting] = self.tool.cost(self.projected, source[0], *source[1:], env=env)
            run = subprocess.run([str(self.projected), *source, '--print-selected'],
                                 capture_output=True, text=True, env={**os.environ, **env}, timeout=300)
            self.assertEqual(run.returncode, 0, run.stderr[-2000:])
            # Inspect only the retained binary. AGX_MESA_DEBUG also prints
            # rejected variants and the preamble, which cannot prove this.
            main = run.stdout.split('SELECTED MAIN BEGIN\n')[1].split('SELECTED MAIN END')[0]
            listing = [m.groups() for m in re.finditer(
                r'^\s*[0-9a-f]+:\s+[0-9a-f]+\s+(\S+)\s*(.*)$', main, re.M)]
            ds = []
            for i, (op, args) in enumerate(listing):
                if op != 'lload':
                    continue
                dest = args.split(',')[0].lstrip('$^')
                for j in range(i + 1, len(listing)):
                    if dest in re.findall(r'\br\d+[lh]?\b', listing[j][1].partition(',')[2]):
                        ds.append(j - i)
                        break
            self.assertEqual(len(ds), 8)
            stores = [i for i, (op, _) in enumerate(listing) if op == 'lstore']
            loads = [i for i, (op, _) in enumerate(listing) if op == 'lload']
            barriers = [i for i, (op, _) in enumerate(listing) if op == 'barrier']
            self.assertEqual(len(stores), 8)
            self.assertEqual(len(barriers), 1)
            self.assertLess(max(stores), barriers[0])
            self.assertLess(barriers[0], min(loads))
            distances[setting] = ds
        self.assertGreater(sum(distances['1']), sum(distances['0']), distances)
        self.assertNotEqual(results['1']['binary-fnv'], results['0']['binary-fnv'])
        self.assertGreaterEqual(results['1']['threads'], results['0']['threads'])
        for key in ('spills', 'fills', 'scratch'):
            self.assertLessEqual(results['1'][key], results['0'][key])
        with patch.dict(os.environ):
            os.environ.pop('APPLE_AGX_LOCAL_LOAD_SCHED', None)
            default = self.tool.cost(self.projected, source[0], *source[1:], env={'APPLE_AGX_RA_PACK': '0'})
        self.assertEqual(default, results['0'])
        for value in ('', '2', '10', 'true'):
            invalid = self.tool.cost(self.projected, source[0], *source[1:],
                                     env={'APPLE_AGX_RA_PACK': '0', 'APPLE_AGX_LOCAL_LOAD_SCHED': value})
            self.assertEqual(invalid, results['0'], value)
        disabled = self.tool.cost(self.projected, source[0], *source[1:],
                                  env={'APPLE_AGX_LATENCY_SCHED': '0', 'APPLE_AGX_LOCAL_LOAD_SCHED': '1'})
        self.assertEqual(disabled, self.tool.cost(self.pinned, source[0], *source[1:]))

    def test_independent_loads_issue_ahead_of_their_uses(self):
        source = ('--synthetic-uav-reads=8', '--no-d3d-robust')
        dumps = {}
        for name, binary in (('pinned', self.pinned), ('latency', self.projected)):
            run = subprocess.run([str(binary), *source], capture_output=True, text=True, timeout=300,
                                 env={**os.environ, 'AGX_MESA_DEBUG': 'shaders'})
            self.assertEqual(run.returncode, 0, run.stderr[-2000:])
            dumps[name] = load_wait_distances(last_main_listing(run.stdout))
        self.assertEqual(len(dumps['pinned']), 7)
        self.assertTrue(all(d <= 3 for d in dumps['pinned']), dumps['pinned'])
        self.assertLessEqual(len(dumps['latency']), 2, dumps['latency'])
        self.assertGreaterEqual(min(dumps['latency']), 8, dumps['latency'])


if __name__ == '__main__':
    unittest.main()
