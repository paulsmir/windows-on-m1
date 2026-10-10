"""EXP1133: a device close must drain the native context before the kernel context goes.

device-terminal lines (stage Closing, CleanupStatus 0, kernel context already
destroyed, 48-63 live BOs) showed ApplicationFrameHost and other apps
destroying D3D devices with a batch still active.  AgxD3d10WindowsDestroyDeviceDdi
destroyed the kernel context first; the active batch could then neither be
submitted nor retired, AgxWin32AsahiContextRetire refused and the close
terminalized, keeping every BO in UMD bookkeeping.  Mesa's
agx_destroy_context flushes and waits (agx_sync_all) before anything else.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp"


class DestroyDeviceDrainsFirst(unittest.TestCase):
    def test_flush_and_retire_precede_kernel_context_destroy(self):
        text = SRC.read_text()
        start = text.index("HRESULT AgxD3d10WindowsDestroyDeviceDdi(")
        body = text[start:text.index("\n}\n", start)]
        destroy = body.index("AdmissionUmdRuntimeDeviceDestroyKernelContext(")
        flush = body.find("->flush(owner->Context")
        retire = body.find("AgxWin32AsahiContextRetire(owner->Context,INFINITE)")
        self.assertTrue(0 <= flush < retire < destroy,
                        "native batches must be submitted and retired before the kernel context is destroyed")
        busy = body.index("HRESULT_FROM_WIN32(ERROR_BUSY)")
        self.assertLess(busy, flush, "a device with extra native contexts is refused before any submission")


if __name__ == "__main__":
    unittest.main()
