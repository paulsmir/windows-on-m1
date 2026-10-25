"""C21G: the Texture3D layout registry addresses every texel as AIL does.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A Texture3D's device-
dependent layout (pattern slots, mip transitions, row and depth strides)
must address exactly the bytes the AGX GPU-tiled layout uses, or a runtime
CPU copy through the layout reads or writes the wrong texels. A wrong bit
order, tile shape, slot advance, prefix merge or row stride shows up as a
MISMATCH against the linked AIL for some element size, side, depth or mip
count. Square textures of every size and chain must fit the two-slot 0003
interface (the C21 registry's domain grows; it must not shrink).
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree, host_flags, HOST_LIBS, CXX_RUNTIME


class VolumeRegistry(unittest.TestCase):
    def test_registry_addresses_match_ail(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not mesa or not build:
            self.skipTest('pinned Mesa/host AIL unavailable')
        libs = [build / p for p in ['src/asahi/layout/libasahi_layout.a', 'src/util/libmesa_util.a',
                                    'src/util/libmesa_util_simd.a', 'src/c11/impl/libmesa_util_c11.a',
                                    'src/util/blake3/libblake3.a']]
        with tempfile.TemporaryDirectory(prefix='volume-registry-') as tmp:
            exe = Path(tmp) / 'registry'
            r = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-fsanitize=address,undefined',
                                *host_flags(build), '-I', str(ROOT / 'tests'), '-I', str(ROOT / 'drivers/apple-agx/d3d12'),
                                str(ROOT / 'tests/d3d12_volume_registry_fixture.c'), '-o', str(exe), *map(str, libs),
                                '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread', *HOST_LIBS, CXX_RUNTIME],
                               capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr[-4000:])
            r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=600)
            self.assertEqual(r.returncode, 0, r.stdout[-4000:] + r.stderr[-4000:])
            m = re.search(r'VOLUME_REGISTRY_PASS resources=(\d+) admitted6=(\d+) admitted2=(\d+) square_refused=0', r.stdout)
            self.assertTrue(m, r.stdout[-2000:])
            resources, six, two = map(int, m.groups())
            # most of the grid fits the six-slot 0022 interface
            self.assertGreater(six, resources * 9 // 10)
            # C21H: block-compressed (BC1, BC7) volumes, element coordinates in blocks
            bc = re.search(r'BC_VOLUME_REGISTRY resources=(\d+) admitted6=(\d+) square_refused2=(\d+)', r.stdout)
            self.assertTrue(bc, r.stdout[-2000:])
            self.assertGreater(int(bc.group(2)), int(bc.group(1)) * 9 // 10)
            print('\n'.join(r.stdout.strip().splitlines()[-2:]))


if __name__ == '__main__':
    unittest.main()
