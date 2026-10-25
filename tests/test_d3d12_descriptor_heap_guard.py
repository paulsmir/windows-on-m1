"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The opt-in heap-end guard must be in the same allocation, initialized in the
GPU mirror, and excluded from logical descriptor counts. Removing the extra
backing or initializing only the cached CPU shadow exposes a neighbour again.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_root_signature_identity import block

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

class DescriptorHeapGuard(unittest.TestCase):
    def test_actual_create_guard_on_off_and_failure(self):
        source = DDI.read_text()
        actual = block(source, 'static UINT64 DescriptorHeapBytes(') + '\n' + block(source, 'static HRESULT APIENTRY CreateDescriptorHeap(')
        fixture = (ROOT / 'tests/d3d12_descriptor_heap_guard_fixture.cpp').read_text()
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / 'guard.cpp', Path(tmp) / 'guard'
            cpp.write_text(fixture.replace('/* CREATE */', actual))
            build = subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_guard_is_not_an_extra_logical_descriptor(self):
        source = DDI.read_text()
        walk = block(source, 'static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,\n                                bool *minmax) {')
        self.assertIn('DescriptorHeapBytes(b->descriptors)', walk)
        self.assertIn('DescriptorHeapBytes(b->sampler_descriptors)', walk)
        for marker in ('static AgxD3d12Heap *HeapAt(', 'static AgxD3d12Heap *MirroredHeapAt(', 'static void MirrorHeapForms('):
            self.assertIn('DescriptorHeapBytes(heap)', block(source, marker))
