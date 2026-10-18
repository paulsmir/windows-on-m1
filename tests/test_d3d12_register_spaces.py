"""Execute production root resolution against static SM5.1 binding metadata.

Real invariant: u0/space1 and u0/space2 name different resources, while a CBV
uses its own namespace. The old ResolveRoot rejects both at reason5.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'


def function_end(text, start):
    opening = text.index('{', start)
    depth = 1
    at = opening + 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return at


class StaticRegisterSpaces(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        text = DDI.read_text()
        resolve = text.index('static UINT ResolveRoot(')
        start = text.rfind('/* C17: namespace-aware root resolution */', 0, resolve)
        if start < 0:
            start = resolve
        code = text[start:function_end(text, resolve)]
        fixture = (ROOT / 'tests/d3d12_register_spaces_fixture.cpp').read_text()
        cls.tmp = tempfile.TemporaryDirectory(prefix='d3d12-spaces-')
        source = Path(cls.tmp.name) / 'root.cpp'
        cls.binary = Path(cls.tmp.name) / 'root'
        source.write_text(fixture.replace('/* PRODUCTION_ROOT_RESOLUTION */', code))
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('C++ compiler required')
        subprocess.run([compiler, '-std=c++11', '-fsanitize=address,undefined', '-I', str(ROOT / 'drivers/apple-agx/mesa/dxbc'),
                        str(source), '-o', str(cls.binary)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_equal_registers_in_different_spaces_remain_distinct(self):
        result = subprocess.run([str(self.binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_namespace_refuses_instead_of_aliasing(self):
        result = subprocess.run([str(self.binary), 'missing'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sparse_table_indices_and_descriptor_spans(self):
        self.run_case('table')

    def test_static_and_table_samplers_match_namespaces(self):
        self.run_case('sampler')

    def test_root_constant_copy_and_bounds(self):
        self.run_case('constants')

    def test_stage_visibility_isolates_equal_semantic_keys(self):
        self.run_case('visibility')

    def test_ambiguous_sources_and_maps_refuse(self):
        self.run_case('ambiguity')

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
