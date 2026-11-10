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
        extent = text.index("static UINT64 DescriptorHeapBytes(")
        code = text[extent:function_end(text, extent)] + "\n" + text[start:function_end(text, resolve)]
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

    def test_unset_root_descriptors_do_not_refuse(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1835: the
        # PipelineStateCache sample left a root CBV it does not read unset
        # and every draw was refused (reason 6).
        self.run_case('unset')

    def test_heap_ranges_resolve_to_heap_indices(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? D3D12 C47: a heap
        # range must read the heap index of its first register's table
        # entry (range offset + register delta), must not collide with a slot
        # SRV of the same slot number, and must refuse when no table covers
        # it.
        self.run_case('heap')

    def test_static_first_slot_order_binds_every_register_of_a_table(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? T5b step B2: the
        # DXIL frontend may give single-register SRVs their slots before
        # ranges, so one descriptor table's registers are no longer in slot
        # order; a resolver that bound "slot of the table's first register
        # + k" would put views on the wrong slots.
        self.run_case('static-first')

    def run_case(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
