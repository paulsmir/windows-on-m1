"""Replay source-backed G4 scene bindings against the production constructor."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class G4BuilderTest(unittest.TestCase):
    def test_process_scene_bindings(self):
        with tempfile.TemporaryDirectory(prefix="g4-builder-") as directory:
            binary = Path(directory) / "replay"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_g4_builder_test.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_builder.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_render_template.generated.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_render_template_rebase.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_render_template_vm_slot.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_relocation.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_exp208_adapter.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
