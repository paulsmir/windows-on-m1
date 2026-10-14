"""EXP1287: the native draw entry evaluates the render condition first.

EXP1287 predication smoke (px/pa, PACKAGE1287): reject-batch kind 8 reason 8
(Leave of a batch that was never entered, fence 0, draws 1) on the first
predicated draw, then every value 0 (the context faulted). Asahi implements
the render condition on the CPU (agx_render_condition_check ->
agx_get_query_result): it flushes and retires the predicate's writer -- the
current batch, which the Windows wrapper had already taken and entered; the
slot then holds a new batch the body draws into, and the wrapper's Leave of
that slot refuses it. Upstream agx_draw_vbo evaluates the condition before
anything else; the projection keeps that order by evaluating it in the
wrapper, before the batch is taken, and removes it from the body.

Invariant: a draw whose render condition retires the current batch enters,
draws into and leaves the same batch; a draw the condition skips touches no
batch; the context stays usable.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"
BODY_CHECK_REMOVED = ("drawbody=replace(drawbody,\n"
                      "        '   if (unlikely(!agx_render_condition_check(ctx)))"
                      "\\n      return;\\n\\n','')")

PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define unlikely(x) (x)
struct pipe_screen { int unused; };
struct pipe_context { struct pipe_screen *screen; };
struct agx_device { int unused; };
static struct agx_device the_device;
static struct agx_device *agx_device(struct pipe_screen *s) { (void)s; return &the_device; }
static void AgxWin32AsahiPublishPages(struct agx_device *d) { (void)d; }
struct pipe_draw_info { unsigned instance_count; };
struct pipe_draw_indirect_info { bool count_from_stream_output; };
struct pipe_draw_start_count_bias { unsigned start, count; int index_bias; };
struct agx_batch { bool capsule; int entered; unsigned draws; };
struct agx_context { struct pipe_context base; bool any_faults; struct agx_batch *batch;
                     bool predicate_written; bool predicate_passes; };
static struct agx_batch slots[2];
static unsigned bodies;
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static struct agx_batch *agx_get_batch(struct agx_context *ctx) {
  if (!ctx->batch) { ctx->batch = &slots[0]; ctx->batch->capsule = true; }
  return ctx->batch;
}
/* agx_render_condition_check -> agx_get_query_result: the current batch
 * wrote the predicate, so it is flushed, completes and is retired; the
 * context's next batch reuses the slot (EXP1287: a new capsule, not entered). */
static bool agx_render_condition_check(struct agx_context *ctx) {
  if (ctx->predicate_written && ctx->batch) {
    ctx->batch->capsule = true; ctx->batch->entered = 0; ctx->batch->draws = 0;
    ctx->predicate_written = false;
  }
  return ctx->predicate_passes;
}
static void agx_legalize_feedback_loops(struct agx_context *ctx) { (void)ctx; }
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
static int AgxWin32AsahiBatchEnter(struct agx_batch *b) { if (!b->capsule) return 0; ++b->entered; return 1; }
static int AgxWin32AsahiBatchLeave(struct agx_batch *b) {
  if (!b->capsule || !b->entered) return 0;
  --b->entered; return 1;
}
/* The projected upstream body (the condition is evaluated here unless the
 * projection removed it). */
static void agx_draw_vbo_windows_body(struct pipe_context *p, const struct pipe_draw_info *i, unsigned d,
    const struct pipe_draw_indirect_info *ind, const struct pipe_draw_start_count_bias *dr, unsigned n) {
  struct agx_context *ctx = agx_context(p);
  (void)i; (void)d; (void)ind; (void)dr; (void)n;
  @@BODY_CHECK@@
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
  ctx.predicate_passes = true;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && bodies == 1 && slots[0].draws == 1 && !slots[0].entered);
  /* The current batch wrote the predicate; the condition passes. */
  ctx.predicate_written = true;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && bodies == 2 && slots[0].draws == 1 && !slots[0].entered);
  /* It fails: the draw touches no batch. */
  ctx.predicate_written = true; ctx.predicate_passes = false;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && bodies == 2 && !slots[0].entered);
  /* The context still draws. */
  ctx.predicate_passes = true;
  agx_draw_vbo(&ctx.base, &info, 0, NULL, &six, 1);
  assert(!ctx.any_faults && bodies == 3);
  return 0;
}
'''


def build_program(source):
    match = re.search(r"wrapper=signature\+'''(\{.*?\n\})\n'''", source, re.S)
    if not match:
        raise AssertionError("native draw wrapper not found")
    body_check = "" if BODY_CHECK_REMOVED in source else \
        "if (unlikely(!agx_render_condition_check(ctx))) return;"
    return PROGRAM.replace("@@WRAPPER@@", match.group(1)).replace("@@BODY_CHECK@@", body_check)


class NativeDrawRenderConditionTests(unittest.TestCase):
    def test_render_condition_retiring_the_batch_does_not_fault_the_draw(self):
        program = build_program(GENERATOR.read_text())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "render_condition.c"
            exe = Path(directory) / "render_condition"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                            str(path), "-o", str(exe)], check=True, cwd=ROOT)
            ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_clear_evaluates_the_condition_before_the_batch(self):
        source = GENERATOR.read_text()
        clear = source[source.index("    # Clear is native state mutation/upload"):]
        clear = clear[:clear.index("    s=s[:decl]+signature.replace('agx_clear(")]
        self.assertLess(clear.index("agx_render_condition_check(ctx)"),
                        clear.index("struct agx_batch *batch = agx_get_batch(ctx);"))
        self.assertIn("clearbody=replace(s[a:b],", source)


if __name__ == "__main__":
    unittest.main()
