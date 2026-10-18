"""R149: actual attach/bind and pinned producer coordinates meet the G4 parser."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / "drivers/apple-agx/mesa/winsys"
SHARED = ROOT / "drivers/apple-agx/shared"
COMMON = Path(subprocess.check_output(
    ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
    cwd=ROOT, text=True).strip()).parent


class G4UscWindowReplay(unittest.TestCase):
    def test_native_coordinates_match_mapped_usc_window(self):
        # Execute the pinned reference helper in temporary build output only;
        # no external implementation is copied into repository sources.
        native = COMMON / ".local/reference/mesa/src/asahi/lib/agx_device.h"
        with tempfile.TemporaryDirectory(prefix="r149-usc-") as directory:
            tmp = Path(directory)
            (tmp / "usc_functions.inc").write_text(
                body((WINSYS / "agx_win32_asahi_bo.c").read_text(),
                     "AgxWin32AsahiAttach") + "\n" +
                body(native.read_text(), "agx_usc_addr"))
            binary = tmp / "replay"
            command = [os.environ.get("CC", "clang"), "-std=c11",
                "-DAPPLE_AGX_GPUVA_WINSYS", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(tmp),
                "-I", str(WINSYS), "-I", str(SHARED / "include"),
                "-I", str(SHARED / "src"),
                str(ROOT / "tests/g4_usc_window_replay.c"),
                str(WINSYS / "agx_win32_gpuva.c"), "-o", str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
            command.remove("-DAPPLE_AGX_GPUVA_WINSYS")
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
