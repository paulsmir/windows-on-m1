"""Run AGX4 parsing and the TA/3D builder through the real m1n1 v5 broker C."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"
M1N1 = ROOT / "m1n1_windows"


class G4RealBrokerBuilderReplay(unittest.TestCase):
    def test_dwm_sized_native_frame_uses_published_process_ranges(self):
        with tempfile.TemporaryDirectory(prefix="g4-broker-builder-") as tmp:
            tmp = Path(tmp)
            source = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c").read_text()
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
            (tmp / "g4_mesa_prepare.inc").write_text(source[match.start():end])
            binary = tmp / "replay"
            sources = [
                ROOT / "tests/g4_real_broker_builder_replay.c",
                SHARED / "src/apple_agx_g4_builder.c",
                SHARED / "src/apple_agx_g4_submit.c",
                SHARED / "src/apple_agx_render_template.generated.c",
                SHARED / "src/apple_agx_render_template_rebase.c",
                SHARED / "src/apple_agx_render_template_vm_slot.c",
                SHARED / "src/apple_agx_relocation.c",
                SHARED / "src/apple_agx_exp208_adapter.c",
                SHARED / "src/apple_agx_uat.c",
                M1N1 / "src/hv_agx_gpuva_v5.c",
            ]
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-O1", "-g",
                "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                "-fsanitize=address,undefined", "-I", str(tmp),
                "-I", str(SHARED / "include"),
                *map(str, sources), "-o", str(binary),
            ], check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=30)
