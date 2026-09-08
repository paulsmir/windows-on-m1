from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers" / "apple-agx" / "render-admission"
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxRenderWin32TransportTests(unittest.TestCase):
    def test_device_owned_allocation_ranges_are_validated_atomically(self):
        """Catches using a wrong, stale, active, read-only or partial allocation."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "render_win32_transport_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I",
                str(DRIVER / "include"),
                "-I",
                str(SHARED / "include"),
                str(DRIVER / "tests" / "render_win32_transport_test.c"),
                str(DRIVER / "src" / "render_win32_transport.c"),
                str(SHARED / "src" / "apple_agx_win32_abi.c"),
                "-o",
                str(binary),
            ]
            build = subprocess.run(
                command, cwd=ROOT, text=True, capture_output=True, check=False
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
