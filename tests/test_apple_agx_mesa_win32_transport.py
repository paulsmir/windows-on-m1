from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"
WINSYS = ROOT / "drivers" / "apple-agx" / "mesa" / "winsys"


class AppleAgxMesaWin32TransportTests(unittest.TestCase):
    def test_clear_builder_emits_exact_immutable_command(self):
        """Catches pointerful, stale, undersized or malformed Mesa commands."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "agx_win32_transport_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I",
                str(WINSYS),
                "-I",
                str(SHARED / "include"),
                str(WINSYS / "agx_win32_transport_test.c"),
                str(WINSYS / "agx_win32_transport.c"),
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
