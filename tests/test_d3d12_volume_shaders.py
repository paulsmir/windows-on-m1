"""Actual C21 FXC bytecode -> DXBC translator -> validated NIR -> Asahi.

Invariant: 3D load/sample/query instructions retain three coordinates and native
SRV-dimension metadata; full-mip typed stores retain image dimension and root UBO.
The fixture observes translated IR and runs the linked production preprocess.
No shader operation or metadata builder is reproduced in the test.

Original repository HLSL, compiled with pinned WDK26100 x64 fxc.exe, entry main,
/O3, /T cs_5_0 (load/store) or ps_5_0 (sample), /Vn g_agx_d3d12_probe_<stem>.
These exact generated headers are used by the hardware probe. Hashes below pin
this compiler evidence, alongside their HLSL inputs; no opaque DXBC copy is added.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, DXBC, local_tree, host_flags

SHADERS = {
    'cs_volume_load': ('B',
        'f1058ec2ff92b62abede3ccc86a3f12d9da54be87384e93f988d555693d6a7f3',
        'f5f08fe1e56775989eb5f1e13e76cfd211f536bfd44f250f886be4aa54bd7d4f'),
    'cs_volume_store': ('C',
        '7cf9e4670837bbf843754e352a333470476b6be5ed40972b7a041dd7b99008bd',
        'aa5926776957b305710deff04583fae5f17d7c55b3b4b1ead696cf6bc9157814'),
    'ps_volume': ('E',
        '15d94087a9ac601b601f4c8e69f656f5e380bb1789d9d03e3bd89ec869d007c7',
        '8e6c2994faa0d6a67d3e79c94beb39f6c3162323f955024b464f395e28eb8e62'),
}


class VolumeShaders(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        if not build or not mesa:
            raise unittest.SkipTest('host Mesa build not available')
        cls.tmp = tempfile.TemporaryDirectory(prefix='d3d12-volume-shaders-')
        cls.binary = Path(cls.tmp.name) / 'volume-shaders'
        libs = [build / p for p in (
            'src/asahi/compiler/libasahi_compiler.a', 'src/asahi/isa/libagx2_disasm.a',
            'src/asahi/layout/libasahi_layout.a', 'src/compiler/nir/libnir.a',
            'src/compiler/libcompiler.a', 'src/util/libmesa_util.a',
            'src/util/libmesa_util_simd.a', 'src/c11/impl/libmesa_util_c11.a',
            'src/util/blake3/libblake3.a')]
        command = [os.environ.get('CC', 'clang'), '-std=c11',
            '-Werror=implicit-function-declaration', *host_flags(build),
            '-I', str(build / 'src/asahi'), '-I', str(mesa / 'src/asahi'),
            '-I', str(DXBC), str(ROOT / 'tests/d3d12_volume_shader_fixture.c'),
            str(DXBC / 'dxbc_to_nir.c'), str(DXBC / 'dxbc_parse.c'),
            '-o', str(cls.binary), *map(str, libs),
            '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread']
        run = subprocess.run(command, capture_output=True, text=True)
        if run.returncode:
            cls.tmp.cleanup()
            raise AssertionError(run.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_shader(self, name):
        mode, header_hash, hlsl_hash = SHADERS[name]
        stem = ROOT / f'drivers/apple-agx/windows/icd/agx_d3d12_probe_{name}'
        header = stem.with_suffix('.h').read_bytes()
        self.assertEqual(hashlib.sha256(header).hexdigest(), header_hash,
                         'FXC fixture changed: update its HLSL/compiler provenance')
        self.assertEqual(hashlib.sha256(stem.with_suffix('.hlsl').read_bytes()).hexdigest(), hlsl_hash)
        array = header.decode().split('const BYTE', 1)[1].split('{', 1)[1].split('}', 1)[0]
        blob = bytes(map(int, re.findall(r'\d+', array)))
        self.assertEqual(blob[:4], b'DXBC')
        path = Path(self.tmp.name) / f'{name}.dxbc'
        path.write_bytes(blob)
        run = subprocess.run([str(self.binary), str(path), mode],
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn(f'PASS C21{mode}', run.stdout)
        self.assertIn('NIR+Asahi', run.stdout)

    def test_texture3d_load_and_dimensions_keep_three_components(self):
        self.run_shader('cs_volume_load')

    def test_rwtexture3d_store_keeps_image_dimension_and_root_ubo(self):
        self.run_shader('cs_volume_store')

    def test_texture3d_sample_level_and_dimensions_keep_three_components(self):
        self.run_shader('ps_volume')
