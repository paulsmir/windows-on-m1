from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxScanoutTests(unittest.TestCase):
    def test_public_header_is_freestanding(self):
        header = (SHARED / "include" / "apple_agx_scanout.h").read_text()
        for hosted_header in ("stdbool.h", "stddef.h", "stdint.h"):
            self.assertNotIn(hosted_header, header)

    def test_v2_capability_bits_match_m1n1(self):
        public = (SHARED / "include" / "apple_agx_scanout.h").read_text()
        broker = (
            ROOT / "m1n1_windows" / "src" / "hv_agx_scanout_broker.h"
        ).read_text()
        for name, bit in (
            ("REPEATED_PRESENT", 4),
            ("LATCHED_RECEIPT", 5),
            ("LATCHED_IRQ", 6),
        ):
            self.assertIn(
                f"APPLE_AGX_SCANOUT_CAP_{name} (1u << {bit}u)", public
            )
            self.assertIn(
                f"HV_AGX_SCANOUT_CAP_{name} (1u << {bit})", broker
            )

    def test_portable_scanout_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_scanout_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_scanout_test.c"),
                str(SHARED / "src" / "apple_agx_scanout.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
