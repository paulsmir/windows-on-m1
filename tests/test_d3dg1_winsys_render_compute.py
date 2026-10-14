"""D3D-G1: a render batch carrying compute work is one v5 submission the KMD admits.

Bug caught: the winsys refused every render batch with a CDM encoder
(geometry shaders, stream output, tessellation), and a v5 envelope composed
differently from the KMD parser's contract (command order, barriers, the
compute preemption buffer, residency of the CDM stream, sampler heap and
context store, the written set) would be refused at submit or run the render
before the compute output exists.

The replay runs the production AgxWin32AsahiBatchFinish (and the CDM scope
functions) against host stubs, through the production GPUVA submit, and hands
the submitted private data to the production KMD parser
(AppleAgxG4ParseSubmitComputeEx) with the G13 context-store size.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]


class D3dG1WinsysRenderCompute(unittest.TestCase):
    def test_render_batch_with_compute_is_one_admitted_v5_submission(self):
        source = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c").read_text()
        names = ("batch_refuse", "batch_has_render_work", "submitted_at", "retire_held",
                 "gpuva_color_format_supported", "gpuva_color_class", "compute_only",
                 "render_compute", "AgxWin32AsahiBatchComputeEnter",
                 "AgxWin32AsahiBatchComputeLeave", "AgxWin32AsahiBatchComputeFinalize",
                 "add_bo", "append_native", "append_attachments", "prepare_process_buffers",
                 "pool_extent", "publish_shadows", "AgxWin32AsahiBatchFinish",
                 "release_lease", "AgxWin32AsahiBatchPoll", "AgxWin32AsahiBatchAbort",
                 "AgxWin32AsahiBatchRelease")
        with tempfile.TemporaryDirectory(prefix="d3dg1-winsys-") as directory:
            tmp = Path(directory)
            (tmp / "d3dg1_winsys_functions.inc").write_text(
                "void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);\n"
                "#define AGX_WIN32_ASAHI_FAIL(b, f) ((b)->Failed = 1)\n" +
                "\n".join(body(source, name) for name in names))
            binary = tmp / "replay"
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(tmp), "-I", str(ROOT / "tests"),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                "-I", str(ROOT / "drivers/apple-agx/mesa/winsys"),
                str(ROOT / "tests/d3dg1_winsys_render_compute_replay.c"),
                str(ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("D3D-G1 winsys render+compute envelope: PASS", ran.stdout)

    def test_overlay_passes_the_compute_command_to_finish(self):
        """The Mesa projection hands the batch's compute command to Finish and
        finalizes the render inside the batch scope with or without it."""
        text = (ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py").read_text()
        self.assertIn("AgxWin32AsahiBatchFinish(batch, render, compute)", text)
        flush = text[text.index("s=body(s,'agx_flush_batch'"):]
        flush = flush[:flush.index("agx_batch_submit(ctx, batch, compute, &render);")]
        compute_end = flush.index("compute = &compute_storage;")
        enter = flush.index("if (!AgxWin32AsahiBatchEnter(batch))", compute_end)
        self.assertNotIn("else", flush[compute_end:enter])


if __name__ == "__main__":
    unittest.main()
