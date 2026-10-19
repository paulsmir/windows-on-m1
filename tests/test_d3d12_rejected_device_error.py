"""Execute current D3D12 CreateDevice and rejected core DDI for two devices."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
WDK = ROOT / '.local/reference/wdk26100/d3d12umddi.h'
WDK_SHA256 = 'd338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28'


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class RejectedDeviceError(unittest.TestCase):
    def test_rejection_uses_invoked_device_callback(self):
        self.assertEqual(hashlib.sha256(WDK.read_bytes()).hexdigest(), WDK_SHA256)
        wdk = WDK.read_text(encoding='utf-8-sig')
        table = re.search(r'typedef struct D3D12DDI_DEVICE_FUNCS_CORE_0003\s*\{(.*?)\}\s*D3D12DDI_DEVICE_FUNCS_CORE_0003', wdk, re.S)
        self.assertIsNotNone(table)
        slots = re.findall(r'\b(pfn\w+)\s*;', table.group(1))
        self.assertEqual(len(slots), 89)
        self.assertEqual(slots.index('pfnGetImplicitPhysicalAdapterMask'), 84)
        source = SOURCE.read_text()
        selected = 'AGX_ASSIGN(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask, AGX_REJECT(D3D12DDI_DEVICE_FUNCS_CORE_0003, pfnGetImplicitPhysicalAdapterMask));'
        self.assertEqual(source.count(selected), 1)
        rejected = source[source.index('template<class T> struct TableOf;'):source.index('\nstatic HRESULT APIENTRY GetVersions(')]
        actual = (block(source, 'struct AgxD3d12Device {') + ';\n' +
                  block(source, 'static void ReportRejectedDeviceError(D3D12DDI_HDEVICE handle, HRESULT error) {') + '\n' +
                  block(source, 'static HRESULT APIENTRY CreateDevice(') + '\n')
        fixture = (ROOT / 'tests/d3d12_rejected_device_error_fixture.cpp').read_text()
        fixture = fixture.replace('/* ACTUAL_REJECTED */', rejected)
        fixture = fixture.replace('/* ACTUAL_DEVICE */', actual)
        fixture = fixture.replace('/* ACTUAL_TABLE_ASSIGNMENT */', selected)
        with tempfile.TemporaryDirectory(prefix='d3d12-device-error-') as directory:
            cpp = Path(directory) / 'fixture.cpp'
            exe = Path(directory) / 'fixture'
            cpp.write_text(fixture)
            build = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                                    '-fsanitize=address,undefined', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
