"""EXP1061: a D3D draw of zero vertices or instances must not poison the context.

EXP1061 Notepad (pid 5580): measure-native-first-fault at agx_state.c line
5421, the native draw entry refusing PrepareDraw/Enter on the second draw of
a batch; from then on every flush of the context is dropped (any_faults), so
its windows show uninitialized surfaces (noise) and no text. Draw(0) and
DrawIndexed(0) are legal no-ops in D3D10/11, and d3d10umd forwards them
unchanged (util_draw_arrays), so the entry's count check turned a no-op into
a permanent fault. Invariants:
- a direct single draw with count 0 or instance_count 0 is skipped without a
  batch, a body call or a fault;
- a real draw still enters, runs and leaves the batch once;
- a refused entry still marks the context faulted (fail closed);
- EXP1063: a draw DrawAllowed refuses before any batch work (EXP1062 Notepad:
  no color buffer or indirect) is dropped when the backend asks, without
  poisoning the context; the patch-list backend still fails closed.
- EXP1069: every draw entry publishes the context's device zero/scratch pages
  before any descriptor work, including draws that end up skipped.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"

PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
struct pipe_screen { int unused; };
struct pipe_context { struct pipe_screen *screen; };
struct agx_device { int unused; };
static struct agx_device the_device;
static struct agx_device *agx_device(struct pipe_screen *s) { (void)s; return &the_device; }
static unsigned publishes;
static void AgxWin32AsahiPublishPages(struct agx_device *d) { assert(d == &the_device); ++publishes; }
struct pipe_draw_info { unsigned instance_count; };
struct pipe_draw_indirect_info { bool count_from_stream_output; };
struct pipe_draw_start_count_bias { unsigned start, count; int index_bias; };
struct agx_batch { int entered; };
struct agx_context { struct pipe_context base; bool any_faults; struct agx_batch *batch; bool prepare_fails, no_color, refusal_poisons; };
static struct agx_batch the_batch;
static unsigned gets, bodies, leaves;
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static struct agx_batch *agx_get_batch(struct agx_context *ctx) { (void)ctx; ++gets; return &the_batch; }
static void AgxWin32AsahiBatchTraceDraw(struct agx_context *c, struct agx_batch *b, unsigned p) { (void)c; (void)b; (void)p; }
/* EXP1189: the entry legalizes before taking the batch (no-ops here). */
static void agx_legalize_feedback_loops(struct agx_context *ctx) { (void)ctx; }
static void agx_legalize_xfb(struct agx_context *ctx) { (void)ctx; }
static struct agx_context *current;
/* Mirrors agx_win32_gpuva_batch.c. */
static int AgxWin32AsahiBatchDrawAllowed(struct agx_context *ctx, const struct pipe_draw_info *info,
    unsigned drawid, const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draws, unsigned count) {
  (void)drawid;
  return ctx && !ctx->any_faults && info && draws && count && !indirect && info->instance_count &&
         !ctx->no_color;
}
static unsigned refusals;
static int AgxWin32AsahiBatchDrawRefused(struct agx_context *ctx, const struct pipe_draw_info *info,
    const struct pipe_draw_indirect_info *indirect, const struct pipe_draw_start_count_bias *draws,
    unsigned count) {
  (void)info; (void)indirect; (void)draws; (void)count; ++refusals;
  return ctx->refusal_poisons;
}
static int AgxWin32AsahiBatchPrepareDraw(struct agx_batch *b, const struct pipe_draw_info *info,
    const struct pipe_draw_start_count_bias *draw) {
  (void)b;
  return !current->prepare_fails && info && draw && draw->count && info->instance_count;
}
static int AgxWin32AsahiBatchEnter(struct agx_batch *b) { if (b->entered) return 0; b->entered = 1; return 1; }
static int AgxWin32AsahiBatchLeave(struct agx_batch *b) { if (!b->entered) return 0; b->entered = 0; ++leaves; return 1; }
static void agx_draw_vbo_windows_body(struct pipe_context *p, const struct pipe_draw_info *i, unsigned d,
    const struct pipe_draw_indirect_info *ind, const struct pipe_draw_start_count_bias *dr, unsigned n) {
  (void)p; (void)i; (void)d; (void)ind; (void)dr; (void)n; ++bodies;
}
static void agx_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws, unsigned num_draws)
@@WRAPPER@@
int main(void) {
  struct pipe_screen screen = {0};
  struct agx_context ctx = {0}; current = &ctx; ctx.base.screen = &screen;
  struct pipe_draw_info info = {.instance_count = 1};
  struct pipe_draw_start_count_bias zero = {0, 0, 0}, six = {0, 6, 0};
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &zero, 1);
  assert(!ctx.any_faults && gets == 0 && bodies == 0);
  info.instance_count = 0;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && gets == 0 && bodies == 0);
  info.instance_count = 1;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && gets == 1 && bodies == 1 && leaves == 1 && !the_batch.entered);
  /* EXP1063: a draw refused before batch work (no color buffer) is dropped
   * when the backend says so, and still fails closed otherwise. */
  ctx.no_color = true;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && refusals == 1 && gets == 1 && bodies == 1);
  ctx.no_color = false;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && gets == 2 && bodies == 2);
  ctx.no_color = true; ctx.refusal_poisons = true;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(ctx.any_faults && refusals == 2 && bodies == 2);
  ctx.any_faults = false; ctx.no_color = false; ctx.refusal_poisons = false;
  ctx.prepare_fails = true;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(ctx.any_faults && bodies == 2);
  assert(publishes == 7);
  return 0;
}
'''


class NativeEmptyDrawTests(unittest.TestCase):
    def test_zero_count_draw_is_a_no_op(self):
        source = GENERATOR.read_text()
        match = re.search(r"wrapper=signature\+'''(\{.*?\n\})\n'''", source, re.S)
        self.assertIsNotNone(match, "native draw wrapper not found")
        program = PROGRAM.replace("@@WRAPPER@@", match.group(1))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty_draw.c"
            exe = Path(directory) / "empty_draw"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-fsanitize=address,undefined", str(path), "-o", str(exe)],
                           check=True, cwd=ROOT)
            ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
