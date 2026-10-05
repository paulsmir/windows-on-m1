"""Production serializer, poison stack, and actual KMD parser (EXP855D)."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]


class G4MesaAttachmentReplay(unittest.TestCase):
    def test_real_attachment_envelope(self):
        source = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c").read_text()
        with tempfile.TemporaryDirectory(prefix="g4-attachments-") as directory:
            tmp = Path(directory)
            (tmp / "g4_mesa_attachment_functions.inc").write_text("\n".join(
                body(source, n) for n in ("append_native", "append_attachments")))
            binary = tmp / "replay"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-ftrivial-auto-var-init=pattern",
                "-fsanitize=address,undefined", "-I", str(tmp),
                "-I", str(ROOT / "tests"),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/g4_mesa_attachment_replay.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
