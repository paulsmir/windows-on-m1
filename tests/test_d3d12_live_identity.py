"""Execute actual live identity functions with valid owned objects."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class LiveIdentity(unittest.TestCase):
    def test_device_and_pso_identity(self):
        source = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        markers = ('static void PublishDeviceIdentity(',
                   'static void RetireDeviceIdentity(',
                   'extern "C" const void *AgxD3d12ZeroCacheResolveDevice(',
                   'static void PublishPsoIdentity(',
                   'static void RetirePsoIdentity(',
                   'extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(')
        globals_ = ('identity_registry_lock', 'live_identity_devices',
                    'live_identity_psos')
        declarations = []
        for name in globals_:
            declaration = re.search(r'^static .*\b' + name + r'\b[^;]*;', source, re.M)
            self.assertIsNotNone(declaration, name)
            declarations.append(declaration.group())
        actual = '\n'.join(declarations) + '\n'
        actual += '\n'.join(block(source, marker) for marker in markers)
        for marker, call in (
            ('static HRESULT APIENTRY CreateDevice(', 'PublishDeviceIdentity(device)'),
            ('static void APIENTRY DestroyDevice(', 'RetireDeviceIdentity(device)'),
            ('static HRESULT APIENTRY CreatePipelineState(', 'PublishPsoIdentity(pso, device)'),
            ('static VOID APIENTRY DestroyPipelineState(D3D12DDI_HDEVICE handle,',
             'RetirePsoIdentity(pso)')):
            self.assertIn(call, block(source, marker))
        fixture = (ROOT / 'tests/d3d12_live_identity_fixture.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='d3d12-live-identity-') as directory:
            cpp = Path(directory) / 'identity.cpp'
            cpp.write_text(fixture.replace('/* ACTUAL_IDENTITY_FUNCTIONS */', actual))
            for sanitizer in ('address,undefined', 'thread'):
                exe = Path(directory) / ('identity-' + sanitizer.replace(',', '-'))
                build = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                                        '-O1', '-g', '-pthread', '-fsanitize=' + sanitizer,
                                        str(cpp), '-o', str(exe)], capture_output=True,
                                       text=True, timeout=60)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                env = os.environ.copy()
                if sanitizer == 'thread':
                    env['TSAN_OPTIONS'] = 'halt_on_error=1:exitcode=66:symbolize=0'
                run = subprocess.run([str(exe)], env=env, capture_output=True,
                                     text=True, timeout=30)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn('D3D12_LIVE_IDENTITY_PASS', run.stdout)
