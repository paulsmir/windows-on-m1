"""S2: scanout broker ABI V3 client (shared apple_agx_scanout.c).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  A V3 register access
(offset >= 0xa8) while the Wom1MpoTwoPlanes switch is off or the broker does
not advertise CAPABILITIES bit 9 (that broker refuses the access); the V3
negotiation landing after REGISTER_POOL or without its read-back; a
COMMIT_LAYERS whose MMIO order or values differ from the broker hand-back
steps 4-8; a refusal code (12..24) that is not surfaced to the caller; and a
second commit queued before the first one latched.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class RenderScanoutV3Tests(unittest.TestCase):
    def test_v3_client_against_fake_broker(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_scanout_v3_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_scanout_v3_test.c"),
                str(SHARED / "src" / "apple_agx_scanout.c"),
                str(SHARED / "src" / "apple_agx_fixed_panel.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                 text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("apple_agx_scanout_v3_test: OK", run.stdout)

    def test_header_stays_freestanding(self):
        header = (SHARED / "include" / "apple_agx_scanout.h").read_text()
        for hosted in ("stdbool.h", "stddef.h", "stdint.h"):
            self.assertNotIn(hosted, header)
        # MSVC C4201: the layer record is a plain struct.
        self.assertNotIn("union", header)


if __name__ == "__main__":
    unittest.main()
