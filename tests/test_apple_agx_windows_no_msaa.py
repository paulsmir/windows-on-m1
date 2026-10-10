"""EXP1162: the Windows Asahi screen must not advertise multisampling.

CS 1.6 (GoldSrc) drew into a 4x MSAA framebuffer because the Asahi screen
reported sample counts 2/4 as supported, and the G4 batch gate refused every
batch (render->samples must be 1): a black window. Until the G4 builder
encodes multisampled targets, the Windows screen sets Asahi's own
AGX_DBG_NOMSAA, so is_format_supported refuses sample_count > 1 and st/mesa
exposes no multisample framebuffer. Both sides must change together.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"
BATCH = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"


class WindowsScreenAdvertisesNoMsaa(unittest.TestCase):
    def test_screen_refuses_samples_the_g4_gate_refuses(self):
        gate = BATCH.read_text()
        projection = GENERATOR.read_text()
        if "render->samples!=1" in gate:
            self.assertIn("agx_screen->dev.debug |= AGX_DBG_NOMSAA;", projection)
            create = projection[projection.index("AgxWin32AsahiScreenCreate(AGX_WIN32_ASAHI_BACKEND"):]
            self.assertLess(create.index("AGX_DBG_NOMSAA"), create.index("agx_init_screen_caps(screen);"))


if __name__ == "__main__":
    unittest.main()
