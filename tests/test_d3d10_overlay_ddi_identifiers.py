"""The d3d10umd projection names only DDI identifiers the pinned WDK defines.

Bug caught (closure 030, D3D-G5): the overlay advertised
D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE, which d3d10umddi.h does not
define (the DDI has MULTISAMPLE_RENDERTARGET and MULTISAMPLE_LOAD; the
runtime derives resolve support), and the Windows build failed only on the
builder. Every D3D10/11 DDI token the projection writes must exist in the
pinned WDK headers.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
WDK = ROOT / ".local/reference/wdk26100"
TOKEN = re.compile(r"\b(?:D3D10_DDI_|D3D10DDI_|D3D11DDI_|D3D11_1DDI_|D3D10_1DDI_|"
                   r"D3D10DDIARG_|D3D11DDIARG_)\w+")


class OverlayDdiIdentifiers(unittest.TestCase):
    def test_every_ddi_identifier_is_defined_by_the_pinned_wdk(self):
        headers = sorted(WDK.glob("*.h"))
        if not headers:
            self.skipTest("pinned WDK headers are not present")
        wdk = "".join(path.read_text(errors="replace") for path in headers)
        tokens = sorted(set(TOKEN.findall(SOURCE.read_text())))
        self.assertTrue(tokens)
        missing = [token for token in tokens
                   if not re.search(r"\b" + re.escape(token) + r"\b", wdk)]
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
