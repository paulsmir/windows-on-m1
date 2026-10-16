"""EXP1319: the GPUVA winsys keeps Asahi's scratch allocation.

Invariant caught: the Windows projection of agx_build_pipeline replaced
Asahi's scratch block with "capture.Failed = 1; return 0" for every shader
with scratch_size > 0 and forced the USC spill size to 0, so a fragment
shader that spills (an indexable temporary above 256 bytes) was drawn with
USC pipeline address 0 and hung the GPU (agx_scratch_smoke n=24 DEVICE_HUNG,
dwmcore.dll_003d8800 on both shader paths), and no fragment helper ever
reached the render command. On the GPUVA winsys (APPLE_AGX_GPUVA_WINSYS) the
pipeline allocates scratch per stage, marks the batch (fs_scratch,
vs_scratch, cs_scratch) and packs the spill-size bucket exactly as Asahi
does; the construction-address path keeps its rejection.
Runs the real projection (see test_d3d10_shader_fields_init.py).
"""
from pathlib import Path
import re
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


def preprocess(text, gpuva):
    """Resolve #ifdef APPLE_AGX_GPUVA_WINSYS / #else / #endif (non-nested
    inside the function)."""
    out, stack = [], []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped == "#ifdef APPLE_AGX_GPUVA_WINSYS":
            stack.append(gpuva)
            continue
        if stripped == "#else" and stack:
            stack[-1] = not stack[-1]
            continue
        if stripped == "#endif" and stack:
            stack.pop()
            continue
        if all(stack):
            out.append(line)
    return "\n".join(out)


class GpuvaPipelineScratch(unittest.TestCase):
    def test_scratch_is_allocated_on_the_gpuva_winsys(self):
        with tempfile.TemporaryDirectory(prefix="gpuva-scratch-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            # the projected producer replaces agx_build_pipeline in agx_state.c
            state = (out / "src/gallium/drivers/asahi/agx_win32_pipeline.inc").read_text()
            body = function_body(state, "agx_build_pipeline")
            gpuva = preprocess(body, True)
            construction = preprocess(body, False)
            for needle in ("agx_scratch_alloc(&ctx->scratch_fs, max_scratch_size, max_subgroups);",
                           "batch->fs_scratch = true;", "batch->vs_scratch = true;",
                           "batch->cs_scratch = true;",
                           "? agx_scratch_get_bucket(cs->b.info.scratch_size)"):
                with self.subTest(needle=needle):
                    self.assertIn(needle, gpuva)
                    self.assertNotIn(needle, construction)
            self.assertNotIn("linked_scratch_unsupported", gpuva)
            self.assertIsNone(re.search(r"capture\.Failed = 1;\s*\(void\)AgxWin32AsahiPipelineFinish\(&capture, b\.head\);\s*return 0;\s*\}\s*\n\s*if \(stage == MESA_SHADER_FRAGMENT\)", gpuva))
            self.assertIn("linked_scratch_unsupported", construction)
            self.assertIn("cfg.spill_size = 0; /* Nonzero scratch rejected above. */", construction)


if __name__ == "__main__":
    unittest.main()
