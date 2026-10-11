"""EXP1189: the native draw entry legalizes before it takes the batch.

EXP1189 CS 1.6 benchmark (PACKAGE1189, second hl.exe of the boot, during
load): reject-batch kind 8 detail 1 (BatchLeave on a batch with no capsule,
fence 0, draws 0), then measure-native-first-fault at the generated
agx_state.c line 5455 -- the Leave of the draw entry -- with the context's
current batch initialized and holding one draw. Every later batch was
dropped (0 batches per frame) and the timedemo "ran" at 53 fps on frozen
frames.

Upstream agx_draw_vbo legalizes feedback loops and transform-feedback writes
*before* agx_get_batch ("once we have the batch we're not allowed to flush
the bound render targets"). The Windows entry wrapper took and entered the
batch before calling the body, so a feedback-loop decompression in the
body's legalize step could flush the just-taken batch; still empty, the
flush aborts and resets it (capsule freed), the body draws into a new
batch, and the wrapper's Leave on the old pointer faults the context.

Invariant: when legalizing flushes the current batch, the batch the wrapper
enters and leaves is the batch the body draws into, and the context stays
usable; an ordinary draw still enters, runs and leaves once.
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
static void AgxWin32AsahiPublishPages(struct agx_device *d) { (void)d; }
struct pipe_draw_info { unsigned instance_count; };
struct pipe_draw_indirect_info { bool count_from_stream_output; };
struct pipe_draw_start_count_bias { unsigned start, count; int index_bias; };
/* capsule: the Windows batch record; a reset batch has none (freed). */
struct agx_batch { bool capsule; int entered; unsigned draws; };
struct agx_context { struct pipe_context base; bool any_faults; struct agx_batch *batch;
                     bool feedback_loop; };
static struct agx_batch slots[4];
static unsigned next_slot, bodies;
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static struct agx_batch *agx_get_batch(struct agx_context *ctx) {
  if (!ctx->batch) { ctx->batch = &slots[next_slot++]; ctx->batch->capsule = true; }
  return ctx->batch;
}
/* agx_legalize_feedback_loops: a compressed texture that is also the bound
 * render target is decompressed once; decompression flushes the target's
 * writer, the current batch. An empty batch is aborted and reset (EXP1189). */
static void agx_legalize_feedback_loops(struct agx_context *ctx) {
  if (!ctx->feedback_loop) return;
  ctx->feedback_loop = false;
  if (ctx->batch && !ctx->batch->draws) {
    ctx->batch->capsule = false;
    ctx->batch->entered = 0;
    ctx->batch = NULL;
  }
}
static void agx_legalize_xfb(struct agx_context *ctx) { (void)ctx; }
static void AgxWin32AsahiBatchTraceDraw(struct agx_context *c, struct agx_batch *b, unsigned p) { (void)c; (void)b; (void)p; }
static int AgxWin32AsahiBatchDrawAllowed(struct agx_context *ctx, const struct pipe_draw_info *info,
    unsigned drawid, const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draws, unsigned count) {
  (void)drawid;
  return ctx && !ctx->any_faults && info && draws && count && !indirect && info->instance_count;
}
static int AgxWin32AsahiBatchDrawRefused(struct agx_context *ctx, const struct pipe_draw_info *info,
    const struct pipe_draw_indirect_info *indirect, const struct pipe_draw_start_count_bias *draws,
    unsigned count) {
  (void)ctx; (void)info; (void)indirect; (void)draws; (void)count; return 1;
}
static int AgxWin32AsahiBatchPrepareDraw(struct agx_batch *b, const struct pipe_draw_info *info,
    const struct pipe_draw_start_count_bias *draw) {
  return b->capsule && info && draw && draw->count && info->instance_count;
}
/* Mirrors agx_win32_gpuva_batch.c: Enter is a depth; Leave refuses a batch
 * without a capsule (reject-batch kind 8 detail 1) or not entered. */
static int AgxWin32AsahiBatchEnter(struct agx_batch *b) { if (!b->capsule) return 0; ++b->entered; return 1; }
static int AgxWin32AsahiBatchLeave(struct agx_batch *b) {
  if (!b->capsule || !b->entered) return 0;
  --b->entered; return 1;
}
/* The upstream body: legalize, then take the batch and draw into it. */
static void agx_draw_vbo_windows_body(struct pipe_context *p, const struct pipe_draw_info *i, unsigned d,
    const struct pipe_draw_indirect_info *ind, const struct pipe_draw_start_count_bias *dr, unsigned n) {
  struct agx_context *ctx = agx_context(p);
  (void)i; (void)d; (void)ind; (void)dr; (void)n;
  agx_legalize_feedback_loops(ctx);
  agx_legalize_xfb(ctx);
  struct agx_batch *batch = agx_get_batch(ctx);
  batch->draws++;
  ++bodies;
}
static void agx_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws, unsigned num_draws)
@@WRAPPER@@
int main(void) {
  struct pipe_screen screen = {0};
  struct agx_context ctx = {0}; ctx.base.screen = &screen;
  struct pipe_draw_info info = {.instance_count = 1};
  struct pipe_draw_start_count_bias six = {0, 6, 0};
  /* An ordinary draw: one batch entered, drawn into and left. */
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && bodies == 1 && ctx.batch == &slots[0] && slots[0].draws == 1 &&
         !slots[0].entered);
  /* A fresh, empty batch (e.g. after a framebuffer change) whose first draw
   * hits a feedback loop: legalizing resets it before the draw. */
  ctx.batch = NULL;
  (void)agx_get_batch(&ctx);   /* slot 1 taken by earlier state work, still empty */
  ctx.feedback_loop = true;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && bodies == 2);
  assert(ctx.batch && ctx.batch->capsule && ctx.batch->draws == 1 && !ctx.batch->entered);
  /* The context still draws afterwards. */
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && bodies == 3 && ctx.batch->draws == 2);
  return 0;
}
'''


class NativeDrawLegalizeOrderTests(unittest.TestCase):
    def test_feedback_loop_flush_does_not_fault_the_draw(self):
        source = GENERATOR.read_text()
        match = re.search(r"wrapper=signature\+'''(\{.*?\n\})\n'''", source, re.S)
        self.assertIsNotNone(match, "native draw wrapper not found")
        program = PROGRAM.replace("@@WRAPPER@@", match.group(1))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "legalize_order.c"
            exe = Path(directory) / "legalize_order"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                            str(path), "-o", str(exe)], check=True, cwd=ROOT)
            ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
