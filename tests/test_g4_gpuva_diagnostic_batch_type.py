"""EXP842: GPUVA builds store AGX_WIN32_GPUVA_BATCH in agx_batch.windows_batch.
Diagnostics must not reinterpret it as the capture batch (Explorer AV at
AgxWin32AsahiContextDiagnostic+0x7c)."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / "drivers/apple-agx/mesa/winsys"


class GpuvaDiagnosticBatchType(unittest.TestCase):
    def test_gpuva_batch_type_is_not_reinterpreted(self):
        self.assertIn("batch->windows_batch=g;", (ROOT / "agx_win32_gpuva_batch.c").read_text())
        scene = (ROOT / "agx_win32_asahi_scene.c").read_text()
        start = scene.index("void AgxWin32AsahiContextDiagnostic(")
        body = scene[start:scene.index("int AgxWin32AsahiContextDrawReceipt(", start)]
        guard = body.index("#if defined(APPLE_AGX_GPUVA_WINSYS)")
        self.assertIn("AGX_WIN32_ASAHI_BATCH *c=NULL;", body[guard:body.index("#else", guard)])


if __name__ == "__main__":
    unittest.main()
