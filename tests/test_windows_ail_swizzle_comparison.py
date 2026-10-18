"""Original0004 nonstacked candidate vs actual AIL; no production caps/BO/GPU."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT,local_tree,host_flags
class WindowsAilSwizzleComparison(unittest.TestCase):
    def test_actual_ail_npothreshold_mip_z_byte_addresses_and_pattern_budget(self):
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        if not mesa or not build:self.skipTest('pinned Mesa/host AIL unavailable')
        libs=[build/p for p in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
        with tempfile.TemporaryDirectory(prefix='windows-ail-swizzle-') as tmp:
            exe=Path(tmp)/'compare'
            r=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-fsanitize=address,undefined',*host_flags(build),
                str(ROOT/'tests/windows_ail_swizzle_comparison_fixture.c'),'-o',str(exe),*map(str,libs),
                '-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread','-lc++'],capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('WINDOWS_AIL_0004_SUBSET_PASS layouts=72',r.stdout)
            self.assertIn('native_distinct_patterns=7 per_layout_slots=6 GENERAL_ADMISSION_UNPROVEN',r.stdout)
            print(r.stdout.strip())
