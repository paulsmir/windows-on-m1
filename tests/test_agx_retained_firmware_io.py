from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"


class AgxRetainedFirmwareIoTests(unittest.TestCase):
    def test_fixed_firmware_io_ownership(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "retained_firmware_io"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-g",
                "-I", str(SHARED / "include"),
                str(ROOT / "tests/agx_retained_firmware_io_test.c"),
                str(ROOT / "m1n1_windows/src/hv_agx_retained_root.c"),
                str(SHARED / "src/apple_agx_uat.c"),
                str(SHARED / "src/apple_agx_uat_table.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
