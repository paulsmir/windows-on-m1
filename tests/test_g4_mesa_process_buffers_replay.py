"""Run the production Mesa TVB preparation body with host allocation callbacks."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"


class G4MesaProcessBuffersReplay(unittest.TestCase):
    def test_private_handoff_uses_kernel_initialized_ranges(self):
        source = SOURCE.read_text()
        match = re.search(r"static int prepare_process_buffers\s*\([^;]*?\)\s*\{",
                          source, re.S)
        self.assertIsNotNone(match)
        depth = 1
        end = match.end()
        while depth:
            if source[end] == "{":
                depth += 1
            elif source[end] == "}":
                depth -= 1
            end += 1
        body = source[match.start():end]
        with tempfile.TemporaryDirectory(prefix="g4-process-buffers-") as tmp:
            tmp = Path(tmp)
            (tmp / "g4_mesa_process_buffers_function.inc").write_text(body)
            binary = tmp / "replay"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(tmp),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/g4_mesa_process_buffers_replay.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
