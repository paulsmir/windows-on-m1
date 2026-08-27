from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxFirmwareTests(unittest.TestCase):
    def test_firmware_header_is_freestanding(self):
        header = (SHARED / "include" / "apple_agx_firmware.h").read_text()
        for hosted_header in ("stdbool.h", "stddef.h", "stdint.h"):
            self.assertNotIn(hosted_header, header)

    def test_rtkit_header_is_freestanding(self):
        header = (SHARED / "include" / "apple_agx_rtkit.h").read_text()
        for hosted_header in ("stdbool.h", "stddef.h", "stdint.h"):
            self.assertNotIn(hosted_header, header)

    def test_rtkit_codec_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_rtkit_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_rtkit_test.c"),
                str(SHARED / "src" / "apple_agx_rtkit.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_firmware_lifecycle_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_firmware_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_firmware_test.c"),
                str(SHARED / "src" / "apple_agx_firmware.c"),
                str(SHARED / "src" / "apple_agx_rtkit.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_rtkit_boot_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_rtkit_boot_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_rtkit_boot_test.c"),
                str(SHARED / "src" / "apple_agx_rtkit_boot.c"),
                str(SHARED / "src" / "apple_agx_rtkit.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
