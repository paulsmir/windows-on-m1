"""EXP1029: the GPUVA native batch accepts the colour formats it encodes.

EXP1028: 67 reject-batch (Finish precondition) for a shell process whose
single render target was PIPE_FORMAT_R8G8B8A8_UNORM; the precondition only
admitted B8G8R8A8_UNORM, although the native batch encodes the format in its
own PBE/EOT state and the KMD treats attachments as pointer ranges.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c'


class GpuvaColorFormats(unittest.TestCase):
    def test_formats(self):
        text = SRC.read_text()
        self.assertIn('!gpuva_color_format_supported(batch->key.cbufs[0].format)', text)
        m = re.search(r'static int gpuva_color_format_supported\(unsigned format\) \{.*?\n\}', text, re.S)
        self.assertIsNotNone(m)
        body = ('#include <assert.h>\n#include <stdio.h>\nenum { PIPE_FORMAT_NONE, PIPE_FORMAT_B8G8R8A8_UNORM, '
                'PIPE_FORMAT_B8G8R8A8_SRGB, PIPE_FORMAT_B8G8R8X8_UNORM, PIPE_FORMAT_B8G8R8X8_SRGB, '
                'PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_FORMAT_R16G16B16A16_FLOAT, PIPE_FORMAT_R10G10B10A2_UNORM, '
                'PIPE_FORMAT_Z24_UNORM_S8_UINT, PIPE_FORMAT_R8_UNORM };\n' + m.group(0) +
                '\nint main(void){ assert(gpuva_color_format_supported(PIPE_FORMAT_R8G8B8A8_UNORM));'
                ' assert(gpuva_color_format_supported(PIPE_FORMAT_B8G8R8A8_UNORM));'
                ' assert(!gpuva_color_format_supported(PIPE_FORMAT_Z24_UNORM_S8_UINT));'
                ' assert(!gpuva_color_format_supported(PIPE_FORMAT_R8_UNORM));'
                ' assert(!gpuva_color_format_supported(PIPE_FORMAT_NONE)); puts("PASS"); return 0; }\n')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'f.c'; exe = Path(tmp) / 'f'
            src.write_text(body)
            built = subprocess.run(['clang', '-Wall', '-Werror', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            self.assertIn('PASS', subprocess.run([str(exe)], text=True, capture_output=True).stdout)


if __name__ == '__main__':
    unittest.main()
