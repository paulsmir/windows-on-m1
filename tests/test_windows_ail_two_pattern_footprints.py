"""Native two-pattern complete block footprints; no production admission."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT,local_tree,host_flags
class WindowsAilTwoPatternFootprints(unittest.TestCase):
    def test_native_mip_blocks_align_do_not_overlap_and_fit(self):
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        if not mesa or not build:self.skipTest('pinned Mesa/host AIL unavailable')
        libs=[build/p for p in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
        with tempfile.TemporaryDirectory(prefix='windows-ail-swizzle-') as tmp:
            exe=Path(tmp)/'compare'
            r=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-fsanitize=address,undefined',*host_flags(build),
                str(ROOT/'tests/windows_ail_two_pattern_footprint_fixture.c'),'-o',str(exe),*map(str,libs),
                '-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread','-lc++'],capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('NATIVE_TWO_PATTERN_FOOTPRINT_PASS resources=49150 mips=73724 disjoint_full_blocks=62939144',r.stdout)
            print(r.stdout.strip())
