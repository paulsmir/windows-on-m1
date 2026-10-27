"""rc76: D3D12 clears become the load-op clears of the draw pass that follows.

EXP2000 (GravityMark D3D12 HD/FHD, tools/agx_gpu_frame_breakdown.py and the
per-job split in .local/handoff/codex/rc76/REPORT.md): 8 of 11 render jobs a
frame were small passes of ~0.6 ms GPU span (3D ~0.16 ms), 5 of them back to
back right before one big pass; replay-time counts 8 clears ('other') per
Execute and perf receipts 8 'Flush reader due to: Write from other batch'.
The owner cleared every RTV/DSV on its own one-layer framebuffer, a separate
Asahi batch that the next pass (a different framebuffer key) flushed as a
clear-only render job and then had to reload.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- The Windows agx_clear wrapper (native-asahi-batch-lifecycle.py) faulted the
  context on any colour buffer but COLOR0, so a draw pass could not take the
  clears of its other render targets; it must accept COLORn exactly when
  colour buffer n's nibble of color_clear_mask is whole, and keep refusing
  stencil without depth and partial masks.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIFECYCLE = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'

WRAPPER_PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define unlikely(x) (x)
#define PIPE_CLEAR_DEPTH (1 << 0)
#define PIPE_CLEAR_STENCIL (1 << 1)
#define PIPE_CLEAR_COLOR0 (1 << 2)
#define PIPE_CLEAR_COLOR (0xffu << 2)
#define PIPE_MAX_COLOR_BUFS 8
#include <stdint.h>
union pipe_color_union { float f[4]; };
struct pipe_scissor_state { int unused; };
struct pipe_context { int unused; };
struct agx_batch { int entered; };
struct agx_context { struct pipe_context base; bool any_faults; struct agx_batch batch; };
static unsigned bodies, body_buffers;
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static bool agx_render_condition_check(struct agx_context *ctx) { (void)ctx; return true; }
static struct agx_batch *agx_get_batch(struct agx_context *ctx) { return &ctx->batch; }
static int AgxWin32AsahiBatchEnter(struct agx_batch *b) { ++b->entered; return 1; }
static int AgxWin32AsahiBatchLeave(struct agx_batch *b) { return b->entered-- > 0; }
static void agx_clear_windows_body(struct pipe_context *pctx, unsigned buffers, uint32_t color_clear_mask,
    uint8_t stencil_clear_mask, const struct pipe_scissor_state *scissor_state,
    const union pipe_color_union *color, double depth, unsigned stencil) {
  (void)pctx; (void)color_clear_mask; (void)stencil_clear_mask; (void)scissor_state; (void)color;
  (void)depth; (void)stencil; ++bodies; body_buffers = buffers;
}
static void agx_clear(struct pipe_context *pctx, unsigned buffers,
          uint32_t color_clear_mask, uint8_t stencil_clear_mask,
          const struct pipe_scissor_state *scissor_state,
          const union pipe_color_union *color, double depth, unsigned stencil)
@@WRAPPER@@
static int run(unsigned buffers, uint32_t mask) {
  struct agx_context ctx = {0};
  union pipe_color_union color = {{0}};
  unsigned before = bodies;
  agx_clear(&ctx.base, buffers, mask, 0xff, NULL, &color, 1.0, 0);
  assert(!ctx.batch.entered);
  return !ctx.any_faults && bodies == before + 1 && body_buffers == buffers;
}
int main(void) {
  /* D3D10/D3D12 one-layer clears: COLOR0 with its nibble, depth, depth+stencil */
  assert(run(PIPE_CLEAR_COLOR0, 0xf));
  assert(run(PIPE_CLEAR_DEPTH, 0));
  assert(run(PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL, 0));
  /* rc76: a draw pass's four render targets and its depth-stencil at once */
  assert(run((0xfu << 2) | PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL, 0xffffffffu));
  assert(run(PIPE_CLEAR_COLOR0 << 7, 0xf0000000u));
  /* a colour buffer whose nibble is not whole, stencil alone */
  assert(!run(PIPE_CLEAR_COLOR0 << 1, 0xf));
  assert(!run(PIPE_CLEAR_COLOR0 | (PIPE_CLEAR_COLOR0 << 1), 0x7f));
  assert(!run(PIPE_CLEAR_STENCIL, 0));
  assert(!run(1u << 10, 0xffffffffu));
  return 0;
}
'''

def wrapper_source():
    source = LIFECYCLE.read_text()
    clear = source[source.index('    # Clear is native state mutation/upload'):]
    match = re.search(r"wrapper=signature\+'''(\{.*?\n\})\n'''", clear, re.S)
    if not match:
        raise AssertionError('Windows agx_clear wrapper not found')
    return match.group(1)


class WindowsClearWrapper(unittest.TestCase):
    def test_every_colour_buffer_with_a_whole_nibble(self):
        program = WRAPPER_PROGRAM.replace('@@WRAPPER@@', wrapper_source())
        with tempfile.TemporaryDirectory(prefix='rc76-clear-') as tmp:
            path = Path(tmp) / 'clear.c'
            exe = Path(tmp) / 'clear'
            path.write_text(program)
            build = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-function', '-fsanitize=address,undefined', str(path), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
