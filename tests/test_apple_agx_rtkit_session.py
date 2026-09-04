import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxRtkitSessionTests(unittest.TestCase):
    def test_session_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "apple_agx_rtkit_session_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_rtkit_session_test.c"),
                str(SHARED / "src" / "apple_agx_rtkit_session.c"),
                str(SHARED / "src" / "apple_agx_rtkit_boot.c"),
                str(SHARED / "src" / "apple_agx_rtkit.c"),
                str(SHARED / "src" / "apple_agx_asc_transport.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
