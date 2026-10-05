from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MESA = ROOT / "drivers/apple-agx/mesa/winsys"
SHARED = ROOT / "drivers/apple-agx/shared"


class AppleAgxWin32ScreenTests(unittest.TestCase):
    def test_device_query_classed_buffers_and_reset_are_fail_closed(self):
        """Catches reusing DRM VA/fence ownership or stale Windows BO state."""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "agx_win32_screen_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(MESA), "-I", str(SHARED / "include"),
                str(MESA / "agx_win32_screen_test.c"),
                str(MESA / "agx_win32_screen.c"),
                str(MESA / "agx_win32_transport.c"),
                str(SHARED / "src/apple_agx_win32_abi.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
