from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxUatTests(unittest.TestCase):
    def test_uat_header_is_freestanding(self):
        header = (SHARED / "include" / "apple_agx_uat.h").read_text()
        for hosted_header in ("stdbool.h", "stddef.h", "stdint.h"):
            self.assertNotIn(hosted_header, header)

    def test_uat_descriptor_codec_suite(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_uat_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_uat_test.c"),
                str(SHARED / "src" / "apple_agx_uat.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
