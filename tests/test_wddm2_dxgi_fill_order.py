"""EXP1328: the WDDM 2.0 DXGI entries are filled after the DXGI 1.1 ones.

Invariant caught: AgxWddm2_0FillDxgiFuncs keeps a copy of the DXGI 1.1
entries (pfnPresent, pfnBlt) that Present1 and Blt1 forward to. The projected
CreateDevice called it in the WDDM 2.0 block, before the frontend wrote those
entries, so the copy held NULL and every Present1 on a WDDM 2.0 device (the
interface the runtime uses at FL 11_0) called address 0: dwm.exe restarted
every few seconds, M365Copilot and opengl32's redirection present crashed
(agx_gl_smoke.dmp: d3d11!NDXGI::CDevice::PresentImpl -> Present1 -> 0).
Checks the projected Device.cpp (see test_d3d10_shader_fields_init.py).
"""
from pathlib import Path
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


class Wddm2DxgiFillOrder(unittest.TestCase):
    def test_dxgi_1_4_entries_follow_the_dxgi_1_1_entries(self):
        with tempfile.TemporaryDirectory(prefix="wddm2-dxgi-order-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            text = (out / "src/gallium/frontends/d3d10umd/Device.cpp").read_text()
            body = function_body(text, "CreateDevice")
            fill = body.index("AgxWddm2_0FillDxgiFuncs(")
            self.assertEqual(body.count("AgxWddm2_0FillDxgiFuncs("), 1)
            for entry in ("->pfnPresent =", "->pfnGetGammaCaps =", "->pfnSetDisplayMode =",
                          "->pfnSetResourcePriority =", "->pfnQueryResourceResidency =",
                          "->pfnRotateResourceIdentities =", "->pfnBlt =",
                          "->pfnResolveSharedResource ="):
                with self.subTest(entry=entry):
                    self.assertIn(entry, body)
                    self.assertLess(body.rindex(entry), fill)
            self.assertLess(body.index("AgxWddm2_0FillDeviceFuncs("), fill)


if __name__ == "__main__":
    unittest.main()
