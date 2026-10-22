"""C53 B0/B4: the winsys GPU VA space host test (agx_win32_gpuva_test.c).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Reserved ranges must be
tile aligned, map at allocation offsets in 4 KiB pages, treat uncertain or
fenceless maps as terminal, and (C53 B4) reserve and free one guard tile
after every range, so that no mapping or merged UMD alias continues into the
next reservation and a free gives back exactly what was reserved. The C file
existed since B0 but no suite ran it.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'


class GpuvaRanges(unittest.TestCase):
    def test_winsys_gpuva_host_test(self):
        with tempfile.TemporaryDirectory(prefix='c53-gpuva-') as tmp:
            exe = Path(tmp) / 'gpuva'
            # the B0 test leaves later ops members to zero-initialisation
            built = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-missing-field-initializers', '-fsanitize=address,undefined',
                                    '-I', str(WINSYS), '-I', str(ROOT / 'drivers/apple-agx/shared/include'),
                                    str(WINSYS / 'agx_win32_gpuva.c'), str(WINSYS / 'agx_win32_gpuva_test.c'),
                                    '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('agx_win32_gpuva_test: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
