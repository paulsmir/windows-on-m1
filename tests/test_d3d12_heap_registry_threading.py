"""Valid concurrent heap metadata operations must retain entries without races."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from tests.test_d3d12_root_signature_identity import block

ROOT = Path(__file__).resolve().parents[1]


class HeapRegistryThreading(unittest.TestCase):
    def test_concurrent_valid_heap_registry(self):
        source = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        actual = '\n'.join(block(source, marker) for marker in (
            'static void RegisterHeap(', 'static void UnregisterHeap(',
            'static AgxD3d12Heap *HeapAt('))
        fixture = (ROOT / 'tests/d3d12_heap_registry_threading_fixture.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='agx-heap-registry-') as directory:
            cpp, exe = Path(directory) / 'registry.cpp', Path(directory) / 'registry'
            cpp.write_text(fixture.replace('/* CURRENT_FUNCTIONS */', actual))
            build = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                                    '-O1', '-g', '-pthread', '-fsanitize=thread',
                                    str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            env = os.environ.copy()
            env['TSAN_OPTIONS'] = 'halt_on_error=1:exitcode=66:symbolize=0'
            run = subprocess.run([str(exe)], env=env, capture_output=True,
                                 text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('HEAP_REGISTRY_CONCURRENT_VALID_LIFETIMES_PASS', run.stdout)
