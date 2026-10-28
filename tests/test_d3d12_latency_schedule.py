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
import shutil
import tempfile
import unittest
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
            units = [include / 'agx_compile.c', include / 'agx_pressure_schedule.c']
            provenance = closure.prepare_latency_schedule(units, include)
            self.assertEqual(set(provenance['files']), {'agx_compiler.h', 'agx_compile.c', 'agx_pressure_schedule.c'})
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


if __name__ == '__main__':
    unittest.main()
