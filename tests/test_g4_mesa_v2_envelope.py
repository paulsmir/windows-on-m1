"""Keep the Mesa native submit wire ABI at AGX4 v2 until TA/3D exists."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"


class G4MesaV2EnvelopeTests(unittest.TestCase):
    def test_native_batch_carries_vidmm_process_ranges(self):
        source = SOURCE.read_text()
        for evidence in (
            "APPLE_AGX_G4_PRIVATE_HEADER_V2 Header;",
            "AppleAgxG4ComposeHeaderV2(",
            "g->Process[i]",
            "AgxWin32GpuvaSubmit(&b->Gpuva",
        ):
            self.assertTrue(evidence in source, f"missing {evidence}")
