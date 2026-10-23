"""Static SM5.1 namespace mapping: prevent aliases before compiler/root replay."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dxbc_parse import ROOT, DXBC, FIXTURES

class DxbcStaticBindings(unittest.TestCase):
    def test_static_map_and_fail_closed_mutations(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'static'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-I', str(DXBC),
                str(ROOT / 'tests/dxbc_static_bindings_fixture.c'), str(DXBC / 'dxbc_parse.c'),
                '-o', str(exe)], check=True)
            for mode in range(12):
                with self.subTest(mode=mode):
                    r = subprocess.run([str(exe), str(FIXTURES / 'cs51static.dxbc'), str(mode)],
                        capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stdout+r.stderr)

    def test_static_spaces_translate_to_distinct_nir_bindings(self):
        from test_dxbc_to_nir import local_tree, host_flags
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        if not build or not mesa:
            self.skipTest('host Mesa build not available')
        libs = [build / p for p in (
            'src/asahi/compiler/libasahi_compiler.a', 'src/asahi/isa/libagx2_disasm.a',
            'src/asahi/layout/libasahi_layout.a', 'src/compiler/nir/libnir.a',
            'src/compiler/libcompiler.a', 'src/util/libmesa_util.a',
            'src/util/libmesa_util_simd.a', 'src/c11/impl/libmesa_util_c11.a',
            'src/util/blake3/libblake3.a')]
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'nir-static'
            r = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11',
                '-Werror=implicit-function-declaration', *host_flags(build),
                '-I', str(build / 'src/asahi'), '-I', str(mesa / 'src/asahi'),
                '-I', str(DXBC), str(ROOT / 'tests/dxbc_static_bindings_nir_fixture.c'),
                str(DXBC / 'dxbc_to_nir.c'), str(DXBC / 'dxbc_parse.c'), str(DXBC / 'dxbc_minmax.c'), '-o', str(exe),
                *map(str, libs), '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(exe), '--dimensions'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
            r = subprocess.run([str(exe), str(FIXTURES / 'cs51static.dxbc')],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
            self.assertIn('store-slots=0,1 cbv-slot=0 NIR+Asahi', r.stdout)

            for name, mode in [('cs50', '--legacy'), ('vs40', '--legacy'),
                               ('ps41', '--legacy'), ('ps51', '--ranges'),
                               ('cs51static', '--translate-fail')]:
                with self.subTest(name=name):
                    r = subprocess.run([str(exe), str(FIXTURES / f'{name}.dxbc'), mode],
                        capture_output=True, text=True, timeout=60)
                    self.assertEqual(r.returncode, 0, r.stdout+r.stderr)

            # Decode the self-authored pinned-FXC probe headers so this test
            # validates precisely the graphics bytecode the hardware uses.
            import re
            for name, mode, expected in [
                ('ps_spaces', '--pixel-uav', 'u1/space4->slot0'),
                ('tex_spaces', '--texture', 't0/space5->slot0 s0/space6->slot0'),
                ('ps_cube', '--cube', 'cube t0'),
                ('ps_cube_array', '--cube-array', 'cube-array t0'),
                ('ps_cube_array_second', '--cube-array', 'cube-array t0')]:
                with self.subTest(graphics=name):
                    header = (ROOT / 'drivers/apple-agx/windows/icd' /
                              f'agx_d3d12_probe_{name}.h').read_text()
                    array = header.split('const BYTE', 1)[1].split('{', 1)[1].split('}', 1)[0]
                    container = Path(tmp) / f'{name}.dxbc'
                    container.write_bytes(bytes(map(int, re.findall(r'\d+', array))))
                    r = subprocess.run([str(exe), str(container), mode],
                        capture_output=True, text=True, timeout=60)
                    self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
                    self.assertIn(expected + ' NIR+Asahi', r.stdout)
