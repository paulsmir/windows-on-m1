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
- clear_absorb_plan (agx_d3d12_windows.cpp) may turn clears into one
  pipe->clear on the draw framebuffer only when that equals running them one
  by one on one-layer framebuffers: whole level, one layer, exactly one
  attachment in the same view format/level/layer, level size == framebuffer
  size, one colour, stencil with depth; later clears override earlier ones.
- The colour conversion of the absorbed path is the one-layer path's (C57).
- The replay defers only clears directly followed by a draw and still runs
  them when the draw does not consume them.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from test_d3d12_view_mapping import block
from test_dxbc_to_nir import local_tree, host_flags

ROOT = Path(__file__).resolve().parents[1]
LIFECYCLE = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
HEADER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h'
SCENE = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c'
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

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

PLAN_PROGRAM = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "util/format/u_format.h"
#include "dxgiformat.h"
using UINT = uint32_t; using LONG = int32_t; using HRESULT = int;
#define FALSE 0
#define TRUE 1
struct RECT { LONG left, top, right, bottom; };
struct AGX_D3D12_GRAPHICS; struct AGX_D3D12_CBV; struct AGX_D3D12_SRV; struct AGX_D3D12_SAMPLER; struct AGX_D3D12_UAV;
@@TYPES@@
static enum pipe_format FormatTranslate(DXGI_FORMAT f, int depth) {
  if (depth) return f == DXGI_FORMAT_D24_UNORM_S8_UINT ? PIPE_FORMAT_Z24_UNORM_S8_UINT
                  : f == DXGI_FORMAT_D32_FLOAT ? PIPE_FORMAT_Z32_FLOAT : PIPE_FORMAT_NONE;
  return f == DXGI_FORMAT_R8G8B8A8_UNORM ? PIPE_FORMAT_R8G8B8A8_UNORM
       : f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ? PIPE_FORMAT_R8G8B8A8_SRGB
       : f == DXGI_FORMAT_R32_UINT ? PIPE_FORMAT_R32_UINT
       : f == DXGI_FORMAT_R16G16B16A16_FLOAT ? PIPE_FORMAT_R16G16B16A16_FLOAT : PIPE_FORMAT_NONE;
}
@@CODE@@
static pipe_resource tex(unsigned w, unsigned h) {
  pipe_resource r = {}; r.target = PIPE_TEXTURE_2D; r.width0 = w; r.height0 = h; r.depth0 = 1; r.array_size = 1; return r;
}
struct Pass {
  pipe_resource rt[4], depth, other, half;
  AGX_D3D12_BUFFER buf[4], dbuf, obuf, hbuf;
  AGX_D3D12_DRAW draw;
  pipe_framebuffer_state fb;
  Pass() {
    memset(&draw, 0, sizeof(draw)); memset(&fb, 0, sizeof(fb));
    for (unsigned i = 0; i < 4; ++i) {
      rt[i] = tex(1280, 720); buf[i] = {}; buf[i].Resource = &rt[i];
      draw.Rtv[i].Texture = &buf[i]; draw.Rtv[i].Format = i == 3 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
      draw.Rtv[i].Layers = 1;
      fb.cbufs[i].texture = &rt[i]; fb.cbufs[i].format = FormatTranslate(draw.Rtv[i].Format, FALSE);
    }
    depth = tex(1280, 720); dbuf = {}; dbuf.Resource = &depth;
    other = tex(1280, 720); obuf = {}; obuf.Resource = &other;
    half = tex(640, 360); hbuf = {}; hbuf.Resource = &half;
    draw.NumRtvs = 4; fb.nr_cbufs = 4;
    draw.Dsv.Texture = &dbuf; draw.Dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; draw.Dsv.Layers = 1;
    fb.zsbuf.texture = &depth; fb.zsbuf.format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
    fb.width = 1280; fb.height = 720; fb.layers = 1;
  }
};
static AGX_D3D12_DRAW_CLEAR rtv(const AGX_D3D12_BUFFER *b, DXGI_FORMAT f, float v) {
  AGX_D3D12_DRAW_CLEAR c = {}; c.Texture = b; c.Format = f; c.Layers = 1;
  for (float &x : c.Color) x = v;
  return c;
}
static AGX_D3D12_DRAW_CLEAR dsv(const AGX_D3D12_BUFFER *b, DXGI_FORMAT f, unsigned flags, float d, unsigned s) {
  AGX_D3D12_DRAW_CLEAR c = {}; c.Texture = b; c.Format = f; c.Layers = 1; c.Depth = 1; c.Flags = flags;
  c.DepthValue = d; c.Stencil = s; return c;
}
static unsigned plan(const Pass &p, const AGX_D3D12_DRAW_CLEAR *c, unsigned n, unsigned *buffers,
                     pipe_color_union *color, double *z, unsigned *s) {
  return clear_absorb_plan(&p.draw, &p.fb, c, n, buffers, color, z, s);
}
int main() {
  unsigned buffers, s; double z; pipe_color_union color;
  { /* EXP2000's shape: four render targets and depth-stencil, then the pass */
    Pass p; AGX_D3D12_DRAW_CLEAR c[5];
    for (unsigned i = 0; i < 4; ++i) c[i] = rtv(&p.buf[i], p.draw.Rtv[i].Format, 0.0f);
    c[4] = dsv(&p.dbuf, DXGI_FORMAT_D24_UNORM_S8_UINT, 3u, 1.5f, 7u);
    assert(plan(p, c, 5, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_OK);
    assert(buffers == ((0xfu << 2) | PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL) && z == 1.0 && s == 7u && color.f[0] == 0.0f);
    /* colours that differ need two pipe->clear calls: not absorbed */
    c[2].Color[1] = 0.5f;
    assert(plan(p, c, 5, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_VALUE && buffers == 0);
    /* in order: the later clear of the same target wins */
    AGX_D3D12_DRAW_CLEAR again[6]; memcpy(again, c, sizeof(c)); again[5] = rtv(&p.buf[2], p.draw.Rtv[2].Format, 0.0f);
    assert(plan(p, again, 6, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_OK);
  }
  { /* one target only, the others keep their contents */
    Pass p; AGX_D3D12_DRAW_CLEAR c = rtv(&p.buf[1], p.draw.Rtv[1].Format, 0.25f);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_OK && buffers == (PIPE_CLEAR_COLOR0 << 1) &&
           color.f[3] == 0.25f);
    /* the clear's view format must be the draw's (sRGB encodes differently) */
    c.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_ATTACHMENT);
    /* a resource that is not an attachment */
    c = rtv(&p.obuf, DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_ATTACHMENT);
    /* another level or layer, two layers */
    c = rtv(&p.buf[0], DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f); c.Layers = 2;
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_ATTACHMENT);
    c.Layers = 1; c.FirstLayer = 1; p.rt[0].array_size = 2;
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_ATTACHMENT);
  }
  { /* rects: a partial one keeps its refusal on the one-layer path */
    Pass p; AGX_D3D12_DRAW_CLEAR c = rtv(&p.buf[0], DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f);
    RECT part = {0, 0, 640, 720}, whole = {-5, 0, 1280, 900};
    c.Rects = &part; c.RectCount = 1;
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_RECT);
    RECT both[2] = {part, whole}; c.Rects = both; c.RectCount = 2;
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_OK);
  }
  { /* the framebuffer is the smallest attachment: a larger target is not wholly cleared */
    Pass p; p.draw.Rtv[3].Texture = &p.hbuf; p.fb.cbufs[3].texture = &p.half; p.fb.width = 640; p.fb.height = 360;
    AGX_D3D12_DRAW_CLEAR c = rtv(&p.buf[0], DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_EXTENT);
  }
  { /* one resource bound twice: its second binding would store unclear tiles */
    Pass p; p.draw.Rtv[1].Texture = &p.buf[0]; p.fb.cbufs[1].texture = &p.rt[0];
    AGX_D3D12_DRAW_CLEAR c = rtv(&p.buf[0], DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_ATTACHMENT);
  }
  { /* depth-stencil: stencil alone is refused by the Windows agx_clear */
    Pass p; AGX_D3D12_DRAW_CLEAR c = dsv(&p.dbuf, DXGI_FORMAT_D24_UNORM_S8_UINT, 2u, 0.0f, 1u);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_STENCIL);
    AGX_D3D12_DRAW_CLEAR two[2] = {c, dsv(&p.dbuf, DXGI_FORMAT_D24_UNORM_S8_UINT, 1u, -1.0f, 0u)};
    assert(plan(p, two, 2, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_OK &&
           buffers == (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL) && z == 0.0 && s == 1u);
    /* the DSV clear's format must be the draw's; a colour clear of the depth is no attachment */
    c = dsv(&p.dbuf, DXGI_FORMAT_D32_FLOAT, 1u, 0.0f, 0u);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_ATTACHMENT);
    c = rtv(&p.dbuf, DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_ATTACHMENT);
    /* stencil on a depth-only format clears nothing, as on the one-layer path */
    p.draw.Dsv.Format = DXGI_FORMAT_D32_FLOAT;
    c = dsv(&p.dbuf, DXGI_FORMAT_D32_FLOAT, 2u, 0.0f, 3u);
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_OK && buffers == 0);
  }
  { /* C57: integer targets take the converted colour, as clear_view_layers does */
    Pass p; p.draw.Rtv[0].Format = DXGI_FORMAT_R32_UINT; p.fb.cbufs[0].format = PIPE_FORMAT_R32_UINT;
    AGX_D3D12_DRAW_CLEAR c = rtv(&p.buf[0], DXGI_FORMAT_R32_UINT, 3.7f); c.Color[1] = -2.0f; c.Color[2] = 5e9f;
    assert(plan(p, &c, 1, &buffers, &color, &z, &s) == AGX_D3D12_ABSORB_OK &&
           color.ui[0] == 3u && color.ui[1] == 0u && color.ui[2] == 0xffffffffu);
  }
  puts("PASS clear absorb plan");
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


class ClearAbsorbPlan(unittest.TestCase):
    def test_plan_equals_the_one_layer_clears(self):
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        sdk = local_tree('reference', 'winsdk26100')
        if not build or not mesa or not sdk:
            self.skipTest('host Mesa or Windows SDK headers unavailable')
        header = HEADER.read_text()
        types = ''
        for end_marker in ('} AGX_D3D12_BUFFER;', '} AGX_D3D12_INDIRECT;', '} AGX_D3D12_DRAW;', '} AGX_D3D12_DRAW_CLEAR;'):
            end = header.index(end_marker) + len(end_marker)
            types += header[header.rfind('typedef struct {', 0, end):end] + '\n'
        start = header.index('enum {\n  AGX_D3D12_ABSORB_OK')
        types = types.replace('typedef struct {\n  AGX_D3D12_GRAPHICS *Pipeline;',
                              '#define AGX_D3D12_VB_SLOTS 16\ntypedef struct {\n  AGX_D3D12_GRAPHICS *Pipeline;')
        types += header[start:header.index('};', start) + 2] + '\n'
        owner = OWNER.read_text()
        code = ''
        for marker in ('static unsigned level_extent(unsigned extent, unsigned level) {',
                       'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {',
                       'static int clear_integer_kind(DXGI_FORMAT format) {',
                       'static union pipe_color_union clear_color_value(',
                       'static unsigned clear_absorb_plan('):
            code += block(owner, marker) + '\n'
        program = PLAN_PROGRAM.replace('@@TYPES@@', types).replace('@@CODE@@', code)
        with tempfile.TemporaryDirectory(prefix='rc76-plan-') as tmp:
            path = Path(tmp) / 'plan.cpp'
            exe = Path(tmp) / 'plan'
            path.write_text(program)
            libs = [build / p for p in ['src/util/libmesa_util.a', 'src/util/libmesa_util_simd.a',
                                         'src/c11/impl/libmesa_util_c11.a', 'src/util/blake3/libblake3.a']]
            run = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-fsanitize=address,undefined',
                                  *host_flags(build), '-I', str(mesa / 'src/gallium/include'),
                                  '-I', str(sdk / 'shared'), str(path), '-o', str(exe), *map(str, libs),
                                  '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_absorbed_colour_is_converted_as_the_one_layer_path(self):
        owner = OWNER.read_text()
        one_layer = block(owner, 'extern "C" HRESULT AgxD3d12OwnerClearRenderTarget(')
        absorbed = block(owner, 'static union pipe_color_union clear_color_value(')
        loop = lambda text: text[text.index('  for (unsigned i = 0; i < 4; ++i) {'):text.index('\n  }\n', text.index('  for (unsigned i = 0; i < 4; ++i) {'))]
        self.assertEqual(loop(one_layer), loop(absorbed))


class ClearAbsorbWiring(unittest.TestCase):
    def test_draw_consumes_or_runs_the_clears(self):
        owner = OWNER.read_text()
        draw = block(owner, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        bound = draw.index('owner->DrawFramebufferValid = true;')
        absorb = draw.index('draw_absorb_clears(owner, draw, &fb)')
        self.assertLess(bound, absorb)
        self.assertLess(absorb, draw.index('pipe->bind_vs_state('))
        self.assertLess(absorb, draw.index('->draw_vbo('))
        # the clears' own framebuffers replaced the draw's: bind it again
        self.assertIn('if (!owner->DrawFramebufferValid) {', draw[absorb:draw.index('pipe->bind_vs_state(')])
        absorb_fn = block(owner, 'static HRESULT draw_absorb_clears(')
        self.assertIn('owner->DrawClears = NULL;', absorb_fn)
        self.assertLess(absorb_fn.index('AgxWin32AsahiClearIsFast(pipe, buffers)'), absorb_fn.index('pipe->clear(pipe, buffers'))
        self.assertIn('return run_draw_clears(owner, clears, count);', absorb_fn)
        wrapper = block(owner, 'extern "C" HRESULT AgxD3d12OwnerDrawWithClears(')
        self.assertLess(wrapper.index('owner->DrawClears = clears;'), wrapper.index('HRESULT hr = AgxD3d12OwnerDraw(owner, draw);'))
        # a draw that returned before binding its framebuffer still has its clears run
        tail = wrapper[wrapper.index('HRESULT hr = AgxD3d12OwnerDraw(owner, draw);'):]
        self.assertIn('if (owner->DrawClears) {', tail)
        self.assertIn('run_draw_clears(owner, clears, count)', tail)
        run = block(owner, 'static HRESULT run_draw_clears(')
        self.assertIn('AgxD3d12OwnerClearDepthStencil(owner,', run)
        self.assertIn('AgxD3d12OwnerClearRenderTarget(owner,', run)
        scene = SCENE.read_text()
        fast = block(scene, 'int AgxWin32AsahiClearIsFast(')
        self.assertIn('agx_get_batch(ctx)', fast)
        self.assertIn('!(buffers & (batch->draw | batch->load))', fast)
        self.assertIn('int AgxWin32AsahiClearIsFast(struct pipe_context *, unsigned Buffers);',
                      (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.h').read_text())

    def test_replay_defers_only_clears_before_a_draw(self):
        ddi = DDI.read_text()
        run = block(ddi, 'static UINT DeferredClearRun(')
        self.assertIn('list->command[d].kind != kCommandDraw', run)
        self.assertIn('end - c > kDeferredClears', run)
        replay = block(ddi, 'static HRESULT ReplayAndSubmit(')
        loop = replay[replay.index('UINT deferred_first = 0'):replay.index('QueryPerformanceCounter(&replayed);')]
        self.assertIn('ClearAbsorbEnabled() ? DeferredClearRun(list, c, &draw_index) : 0u', loop)
        # deferred clears skip their own branch and go to that draw only
        self.assertLess(loop.index('if (c >= deferred_first && c < deferred_end) {'), loop.index('command.kind == kCommandClear) {'))
        self.assertIn('if (deferred_end > deferred_first && c == deferred_draw) {', loop)
        self.assertIn('&list->command[deferred_first], deferred_end - deferred_first, &absorb);', loop)
        self.assertIn('"measure-d3d12-clear-absorb"', replay)
        draw = block(ddi, 'static __declspec(noinline) HRESULT ReplayDraw(')
        self.assertIn('AgxD3d12OwnerDrawWithClears(owner, &draw, scratch->clears, clear_count, &absorbed, &reason)', draw)
        # a failure before the owner sees the draw still runs the clears first
        failed = draw[draw.index('SetStageHeap(owner, s,'):]
        self.assertLess(failed.index('AgxD3d12OwnerDrawWithClears(owner, nullptr, scratch->clears, clear_count'),
                        failed.index('return hr;'))
        self.assertIn('GetEnvironmentVariableA("APPLE_AGX_D3D12_CLEAR_ABSORB"', block(ddi, 'static bool ClearAbsorbEnabled('))


if __name__ == '__main__':
    unittest.main()
