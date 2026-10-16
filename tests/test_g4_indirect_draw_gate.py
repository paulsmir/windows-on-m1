"""EXP1308: the native draw gate admits indirect draws.

EXP1306/EXP1308 FL 11_0 smoke: DrawInstancedIndirect drew nothing and every
tessellated patch draw was dropped -- reject-batch kind 6 reason 2
(indirect) and reason 3 (indirect with no direct draw: Asahi's own indirect
draws after the tessellator and transform-feedback/indirect-count kernels
pass draws == NULL, agx_state.c 4245/4724). The upstream agx_draw_vbo body
encodes indirect draws (VDM indirect) and records the argument buffer as a
batch read; the KMD only checks the VDM stream address.
Invariant: an indirect draw with an argument buffer (or a stream-output
count) is allowed and prepared without direct draws or an instance count; an
indirect draw with neither is refused with reason 2; direct draws keep the
EXP1288 contract (count and instances required).
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"
LIFECYCLE = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"

PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
struct pipe_resource { int unused; };
struct pipe_stream_output_target { int unused; };
struct pipe_surface { struct pipe_resource *texture; };
struct pipe_framebuffer_state { unsigned nr_cbufs; struct pipe_surface cbufs[8]; struct pipe_surface zsbuf; };
struct agx_context { bool any_faults; struct pipe_framebuffer_state framebuffer; };
struct agx_batch { int unused; };
struct pipe_draw_info { unsigned instance_count; };
struct pipe_draw_indirect_info { unsigned offset, stride, draw_count;
  struct pipe_resource *buffer, *indirect_draw_count;
  struct pipe_stream_output_target *count_from_stream_output; };
struct pipe_draw_start_count_bias { unsigned start, count; int index_bias; };
typedef struct { int Submitted, Rejected; } AGX_G4_BATCH;
static AGX_G4_BATCH capsule_state;
static AGX_G4_BATCH *capsule(struct agx_batch *b) { return b ? &capsule_state : NULL; }
static unsigned refused_kind, refused_reason;
static int batch_refuse(unsigned kind, unsigned reason, unsigned a, unsigned b) {
  (void)a; (void)b; refused_kind = kind; refused_reason = reason; return 0;
}
@@FUNCTIONS@@
int main(void) {
  struct pipe_resource tex = {0}, args = {0};
  struct pipe_stream_output_target so = {0};
  struct agx_context ctx = {0};
  struct agx_batch batch = {0};
  struct pipe_draw_info info = {1};
  struct pipe_draw_indirect_info indirect = {0};
  struct pipe_draw_start_count_bias draw = {0, 0, 0};
  ctx.framebuffer.nr_cbufs = 1; ctx.framebuffer.cbufs[0].texture = &tex;
  indirect.buffer = &args; indirect.draw_count = 1;
  /* DrawInstancedIndirect: one draw record, its count in the buffer. */
  assert(AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, &indirect, &draw, 1));
  assert(AgxWin32AsahiBatchPrepareDraw(&batch, &info, &indirect, &draw));
  /* Asahi's own indirect draws: no direct draws at all. */
  assert(AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, &indirect, NULL, 1));
  assert(AgxWin32AsahiBatchPrepareDraw(&batch, &info, &indirect, NULL));
  /* DrawAuto: the count comes from stream output. */
  indirect.buffer = NULL; indirect.count_from_stream_output = &so;
  assert(AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, &indirect, &draw, 1));
  /* Indirect without arguments: refused, reason 2. */
  indirect.count_from_stream_output = NULL;
  assert(!AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, &indirect, &draw, 1));
  assert(!AgxWin32AsahiBatchPrepareDraw(&batch, &info, &indirect, &draw));
  assert(!AgxWin32AsahiBatchDrawRefused(&ctx, &info, &indirect, &draw, 1));
  assert(refused_kind == 6u && (refused_reason & 2u));
  /* Direct draws keep their contract. */
  draw.count = 3;
  assert(AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, NULL, &draw, 1));
  assert(AgxWin32AsahiBatchPrepareDraw(&batch, &info, NULL, &draw));
  draw.count = 0; /* the vertex count is PrepareDraw's check (EXP1063) */
  assert(!AgxWin32AsahiBatchPrepareDraw(&batch, &info, NULL, &draw));
  draw.count = 3; info.instance_count = 0;
  assert(!AgxWin32AsahiBatchDrawAllowed(&ctx, &info, 0, NULL, &draw, 1));
  return 0;
}
'''


def functions(text):
    names = ("AgxWin32AsahiBatchPrepareDraw", "AgxWin32AsahiBatchDrawAllowed",
             "AgxWin32AsahiBatchDrawRefused")
    return "\n".join([re.search(r"static int indirect_arguments\(.*?\n\}", text, re.S).group(0)] +
                     [re.search(r"int %s\(.*?\n\}" % n, text, re.S).group(0) for n in names])


class IndirectDrawGate(unittest.TestCase):
    def test_indirect_draws_are_allowed(self):
        program = PROGRAM.replace("@@FUNCTIONS@@", functions(SRC.read_text()))
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "gate.c"; exe = Path(tmp) / "gate"
            src.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                           check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_the_draw_wrapper_passes_the_indirect_arguments(self):
        self.assertIn("AgxWin32AsahiBatchPrepareDraw(batch,info,indirect,draws)", LIFECYCLE.read_text())


if __name__ == "__main__":
    unittest.main()
