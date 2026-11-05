from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MESA = ROOT / ".local/reference/mesa"
BUILD = ROOT / ".local/accelerated-desktop-ad03/mesa-build"
WINSYS = ROOT / "drivers/apple-agx/mesa/winsys"
SHARED = ROOT / "drivers/apple-agx/shared"


class AppleAgxWin32PipeScreenTests(unittest.TestCase):
    def test_actual_pipe_screen_uses_only_windows_owned_buffers(self):
        """Catches substituting a software screen or DRM/fd BO owner."""
        source = (WINSYS / "agx_win32_pipe_screen.c").read_text()
        for forbidden in (
            "xf86drm", "drmIoctl", "drmSyncobj", "softpipe",
            "llvmpipe", "gdi_create_sw_winsys",
        ):
            self.assertNotIn(forbidden, source)
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "agx_win32_pipe_screen_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(BUILD / "src"), "-I", str(BUILD),
                "-I", str(MESA / "include"), "-I", str(MESA / "src"),
                "-I", str(MESA / "src/gallium/include"),
                "-I", str(MESA / "src/gallium/auxiliary"),
                "-I", str(WINSYS), "-I", str(SHARED / "include"),
                str(WINSYS / "agx_win32_pipe_screen_test.c"),
                str(WINSYS / "agx_win32_pipe_screen.c"),
                str(WINSYS / "agx_win32_screen.c"),
                str(WINSYS / "agx_win32_transport.c"),
                str(SHARED / "src/apple_agx_win32_abi.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
