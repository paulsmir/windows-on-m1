"""EXP1288: the native draw gate admits depth-only draws.

EXP1288 depth prepass (agx_depth_smoke x24p/a24p, PACKAGE1288): every draw
of the depth-only pass was dropped by AgxWin32AsahiBatchDrawAllowed
(reject-batch kind 6 reason 8, no colour buffer: nr_cbufs 0, depth bound),
so the colour pass found no depth. D3D-G7 admitted depth-only renders at the
batch gate and in the KMD but left the draw gate requiring colour buffer 0.
Invariant: a draw with colour buffer 0 bound, or with no colour buffer and a
depth buffer, is allowed; a draw with neither, or with colour slots but
buffer 0 unbound, is refused and reported with its reason bits.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"

PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
struct pipe_resource { int unused; };
struct pipe_surface { struct pipe_resource *texture; };
struct pipe_framebuffer_state { unsigned nr_cbufs; struct pipe_surface cbufs[8]; struct pipe_surface zsbuf; };
struct agx_context { bool any_faults; struct pipe_framebuffer_state framebuffer; };
struct pipe_draw_info { unsigned instance_count; };
struct pipe_stream_output_target { int unused; };
struct pipe_draw_indirect_info { struct pipe_resource *buffer;
  struct pipe_stream_output_target *count_from_stream_output; };
struct pipe_draw_start_count_bias { unsigned start, count; int index_bias; };
static unsigned refused_kind, refused_reason;
static int batch_refuse(unsigned kind, unsigned reason, unsigned a, unsigned b) {
  (void)a; (void)b; refused_kind = kind; refused_reason = reason; return 0;
}
@@FUNCTIONS@@
int main(void) {
  struct pipe_resource tex = {0};
  struct agx_context ctx = {0};
  struct pipe_draw_info info = {1};
  struct pipe_draw_start_count_bias draw = {0, 3, 0};
  ctx.framebuffer.nr_cbufs = 1; ctx.framebuffer.cbufs[0].texture = &tex;
  assert(AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, NULL, &draw, 1));
  /* EXP1288: a depth-only draw. */
  ctx.framebuffer.nr_cbufs = 0; ctx.framebuffer.cbufs[0].texture = NULL;
  ctx.framebuffer.zsbuf.texture = &tex;
  assert(AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, NULL, &draw, 1));
  /* Neither colour nor depth: refused, reason 8. */
  ctx.framebuffer.zsbuf.texture = NULL;
  assert(!AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, NULL, &draw, 1));
  assert(!AgxWin32AsahiBatchDrawRefused(&ctx, &info, NULL, &draw, 1));
  assert(refused_kind == 6u && refused_reason == 8u);
  /* Colour slots with buffer 0 unbound: refused, reason 16. */
  ctx.framebuffer.nr_cbufs = 2; ctx.framebuffer.cbufs[1].texture = &tex;
  ctx.framebuffer.zsbuf.texture = &tex;
  assert(!AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, NULL, &draw, 1));
  assert(!AgxWin32AsahiBatchDrawRefused(&ctx, &info, NULL, &draw, 1));
  assert(refused_kind == 6u && refused_reason == 16u);
  /* A depth-only draw is never reported as refused for its colour. */
  ctx.framebuffer.nr_cbufs = 0; ctx.framebuffer.cbufs[1].texture = NULL;
  refused_reason = 0;
  info.instance_count = 0;
  assert(!AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, NULL, &draw, 1));
  assert(!AgxWin32AsahiBatchDrawRefused(&ctx, &info, NULL, &draw, 1));
  assert(refused_reason == 4u);
  return 0;
}
'''


def functions(text):
    allowed = re.search(r"int AgxWin32AsahiBatchDrawAllowed\(.*?\n\}", text, re.S).group(0)
    refused = re.search(r"int AgxWin32AsahiBatchDrawRefused\(.*?\n\}", text, re.S).group(0)
    helper = re.search(r"static int indirect_arguments\(.*?\n\}", text, re.S).group(0)
    return helper + "\n" + allowed + "\n" + refused


class DepthOnlyDrawGate(unittest.TestCase):
    def test_depth_only_draws_are_allowed(self):
        program = PROGRAM.replace("@@FUNCTIONS@@", functions(SRC.read_text()))
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "gate.c"; exe = Path(tmp) / "gate"
            src.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                           check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
