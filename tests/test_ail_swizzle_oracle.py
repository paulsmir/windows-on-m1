"""C21 original address/extent oracle compared with actual linked AIL.

Catches wrong Morton ordering, NPOT tile-row arithmetic, per-mip tile changes,
full-chain allocation and constant Z stride; a Windows custom-swizzle decoder
must match this oracle before layout admission. No GPU, BO, Screen or context.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree, host_flags


class AilSwizzleOracle(unittest.TestCase):
    def test_actual_ail_layout_tile_detile(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not mesa or not build:
            self.skipTest('pinned Mesa / host AIL unavailable')
        hashes = {
            'layout.c': '2620c7369759a671e606e4720b70e5b4fefb94f103ac3c085fe4b61afcfd0457',
            'layout.h': 'e4c55c1541406d384e5c67df6ff81e0b45b38d424e287cf5c4256a8de822f2a3',
            'tiling.cc': '160a85b334883f79cecb6f10d5a80e9070d72044ab23089f91503d15351db17f',
        }
        for name, expected in hashes.items():
            self.assertEqual(hashlib.sha256((mesa / 'src/asahi/layout' / name).read_bytes()).hexdigest(), expected,
                             'review actual AIL contract before changing the source pin')
        libs = [build / p for p in (
            'src/asahi/layout/libasahi_layout.a', 'src/util/libmesa_util.a',
            'src/util/libmesa_util_simd.a', 'src/c11/impl/libmesa_util_c11.a',
            'src/util/blake3/libblake3.a')]
        with tempfile.TemporaryDirectory(prefix='ail-swizzle-') as tmp:
            binary = Path(tmp) / 'oracle'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11',
                            '-fsanitize=address,undefined', *host_flags(build),
                            str(ROOT / 'tests/ail_swizzle_oracle_fixture.c'), '-o', str(binary),
                            *map(str, libs), '-L/opt/homebrew/opt/zstd/lib',
                            '-lzstd', '-lz', '-lpthread', '-lc++'], check=True, timeout=30)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=30)
        self.assertIn('AIL_ORACLE_PASS cases=260 levels=960 slices=1980 logical_bytes=50285968', result.stdout)
        print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
