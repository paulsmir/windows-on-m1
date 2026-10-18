"""EXP1381: execute actual device initialization against reused poisoned storage.

The runtime owns allocation, while the DDI owns initialization of every private
field. This catches stale lazy-owner, paging and heap state before first use.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    pos = source.index('{', start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class DeviceInitialization(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        actual = block(source, 'struct AgxD3d12Device {') + ';\n'
        for marker in ('static HRESULT APIENTRY CreateDevice(',
                       'static void OwnerError(', 'static AGX_D3D12_OWNER *EnsureOwner('):
            actual += block(source, marker) + '\n'
        cls.tmp = tempfile.TemporaryDirectory(prefix='d3d12-device-initialization-')
        cpp = Path(cls.tmp.name) / 'device.cpp'
        fixture = (ROOT / 'tests/d3d12_device_initialization_fixture.cpp').read_text()
        cpp.write_text(fixture.replace('/* PRODUCTION_DEVICE */', actual))
        cls.exe = Path(cls.tmp.name) / 'device'
        build = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                                '-fsanitize=address,undefined', str(cpp), '-o', str(cls.exe)],
                               capture_output=True, text=True)
        if build.returncode:
            cls.tmp.cleanup()
            raise AssertionError(build.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, mode):
        run = subprocess.run([str(self.exe), mode], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_poisoned_reused_device_gets_fresh_lazy_owner(self):
        self.run_case('success')

    def test_poisoned_reused_device_caches_owner_failure(self):
        self.run_case('failure')

    def test_invalid_args_leave_storage_untouched(self):
        self.run_case('invalid')
