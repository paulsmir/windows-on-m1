import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers" / "apple-agx" / "windows" / "one-shot" / "apple_agx_gdi_colorfill.c"


class GdiProducerTests(unittest.TestCase):
    def test_direct_display_mode_is_explicit_and_does_not_fabricate_kmd_work(self):
        source = SOURCE.read_text()
        self.assertIn('L"--draw-display"', source)
        function = re.search(
            r"static int draw_display_once\(.*?^}", source, re.S | re.M
        ).group(0)
        self.assertIn("find_display(name", function)
        self.assertIn("open_display(display.DeviceName", function)
        self.assertIn("adapter.AdapterLuid.HighPart", function)
        self.assertIn("PatBlt(display_dc", function)
        self.assertIn("GdiFlush()", function)
        self.assertNotIn("CreateCompatibleBitmap", function)
        self.assertNotIn("CreateCompatibleDC", function)
        self.assertNotIn("D3DKMTRender", function)
        self.assertNotIn("GetPixel", function)


if __name__ == "__main__":
    unittest.main()
