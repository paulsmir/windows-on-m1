"""Production Mesa carries authenticated private ranges in AGX4 v3."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"


class G4MesaV2EnvelopeTests(unittest.TestCase):
    def test_native_batch_carries_private_scene_lease(self):
        source = SOURCE.read_text()
        for evidence in (
            "APPLE_AGX_G4_PRIVATE_HEADER_V3 Header;",
            "AppleAgxG4ComposeHeaderV3(",
            "g->Lease.SceneId",
            "AgxWin32GpuvaSubmit(&b->Gpuva",
        ):
            self.assertTrue(evidence in source, f"missing {evidence}")

        self.assertNotIn("g->Process[", source)
        self.assertNotIn("VA render process", source)
