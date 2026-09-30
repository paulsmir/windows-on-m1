"""R148: production batch collection and GpuVA lifecycle, with KMD parser.

The host models VidMm's explicit residency requirement: reserving/mapping a VA
alone does not make its canonical allocation resident. Pool BOs deliberately do
not occur in the unrelated batch handle bitset.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]


class G4MesaPoolResidencyReplay(unittest.TestCase):
    def test_all_pool_slabs_are_resident_uploaded_and_retired(self):
        source = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c").read_text()
        names = ("batch_refuse", "batch_has_render_work", "add_bo", "append_native", "append_attachments",
                 "prepare_process_buffers", "AgxWin32AsahiBatchFinish",
                 "AgxWin32AsahiBatchPoll", "AgxWin32AsahiBatchAbort",
                 "AgxWin32AsahiBatchRelease")
        with tempfile.TemporaryDirectory(prefix="r148-pool-residency-") as directory:
            tmp = Path(directory)
            (tmp / "g4_mesa_pool_functions.inc").write_text("void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);\n" + "\n".join(
                body(source, name) for name in names))
            binary = tmp / "replay"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(tmp), "-I", str(ROOT / "tests"),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                "-I", str(ROOT / "drivers/apple-agx/mesa/winsys"),
                str(ROOT / "tests/g4_mesa_pool_residency_replay.c"),
                str(ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
