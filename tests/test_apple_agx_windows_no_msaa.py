"""EXP1162 / D3D-G5: only the D3D UMD's screen advertises multisampling.

CS 1.6 (GoldSrc) drew into a 4x MSAA framebuffer as soon as the Asahi screen
reported sample counts 2/4, and the G4 batch gate then refused every batch:
a black window. The OpenGL ICD therefore keeps Asahi's AGX_DBG_NOMSAA, so
is_format_supported refuses sample_count > 1 and st/mesa exposes no
multisample framebuffer. D3D-G5: the KMD encodes 2x/4x renders, the gate
admits them, and the D3D UMD (and only it) sets Backend.Multisample before
the screen exists, so its screen advertises 2x/4x for D3D resources.
Bug caught: a GL ICD screen advertising MSAA (the EXP1162 black window back),
or a D3D screen advertising MSAA the gate refuses.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"
BATCH = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"
D3D = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp"
WGL = ROOT / "drivers/apple-agx/windows/icd/agx_wgl_icd.cpp"


class OnlyD3dAdvertisesMsaa(unittest.TestCase):
    def test_screen_refuses_msaa_unless_the_owner_asked(self):
        projection = GENERATOR.read_text()
        create = projection[projection.index("AgxWin32AsahiScreenCreate(AGX_WIN32_ASAHI_BACKEND"):]
        line = "if (!backend->Multisample) agx_screen->dev.debug |= AGX_DBG_NOMSAA;"
        self.assertIn(line, create)
        self.assertLess(create.index(line), create.index("agx_init_screen_caps(screen);"))

    def test_only_the_d3d_owner_asks_for_msaa_before_the_screen(self):
        d3d = D3D.read_text()
        # EXP1287 diagnostic: the D3D owner may gate the request by a switch.
        ask = "owner->Backend.Multisample=" if "owner->Backend.Multisample=" in d3d else None
        self.assertIsNotNone(ask)
        self.assertLess(d3d.index(ask),
                        d3d.index("owner->Screen=AgxWin32AsahiScreenCreateForWindows("))
        self.assertNotIn("Multisample", WGL.read_text())

    def test_gate_admits_the_counts_the_screen_advertises(self):
        gate = BATCH.read_text()
        self.assertNotIn("render->samples!=1 ||", gate)
        self.assertIn("(render->samples!=1 && render->samples!=2 && render->samples!=4)", gate)


if __name__ == "__main__":
    unittest.main()
