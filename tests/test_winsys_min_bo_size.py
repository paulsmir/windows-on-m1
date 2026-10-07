"""EXP1008: native BOs are never a single 64 KiB GPU page.

EXP995-EXP1007: 60 of 62 copy QUERY failures named a 64 KiB slot; the KMD
leaf ring (EXP1006/1007) shows VidMm sent no valid UpdatePageTable for those
mappings, including for an explicit re-map, while adjacent larger BOs of the
same process were populated. Invariant: GPUVA-winsys BO sizes are rounded to
64 KiB and are at least 128 KiB.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c'
HDR = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.h'


class MinNativeBoSize(unittest.TestCase):
    def test_minimum(self):
        m = re.search(r'#define AGX_WIN32_MIN_NATIVE_BO_BYTES (0x[0-9a-fA-F]+)u', HDR.read_text())
        self.assertIsNotNone(m)
        self.assertGreaterEqual(int(m.group(1), 16), 0x20000)
        text = SRC.read_text()
        create = text[text.index('struct agx_bo *agx_bo_create('):]
        gpuva = create[create.index('#ifdef APPLE_AGX_GPUVA_WINSYS'):create.index('#else')]
        self.assertLess(gpuva.index('bytes=(bytes+0xffff)'), gpuva.index('AGX_WIN32_MIN_NATIVE_BO_BYTES'))
        self.assertIn('if(bytes<AGX_WIN32_MIN_NATIVE_BO_BYTES) bytes=AGX_WIN32_MIN_NATIVE_BO_BYTES;', gpuva)


if __name__ == '__main__':
    unittest.main()
