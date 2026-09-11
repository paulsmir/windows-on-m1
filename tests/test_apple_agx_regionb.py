from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxRegionBTests(unittest.TestCase):
    def test_regionb_pointer_codec_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_regionb_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_regionb_test.c"),
                str(SHARED / "src" / "apple_agx_regionb.c"),
                str(SHARED / "src" / "apple_agx_uat.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
