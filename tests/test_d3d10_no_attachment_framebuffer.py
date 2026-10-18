"""D3D-G11: a framebuffer without attachments is sized by the viewports.

EXP1314 stats smoke: every stream-output draw with neither a render target
nor a depth-stencil view bound (D3D11 SO pass, null pixel shader) was
dropped at the native draw gate (reject-batch kind 6 reason 8), so SO
statistics read 0. Mesa's D3D10 frontend sizes the framebuffer with
util_framebuffer_min_size, which leaves 0x0 when nothing is bound; Gallium
sizes a framebuffer without attachments by width/height/layers/samples.
Invariant: with no attachment bound the framebuffer covers the bound
viewports (rounded up, at most 16384, at least 1x1, one layer and sample),
SetViewports rebinds it only when that size changes, and a framebuffer with
an attachment keeps its views' size.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"

SHIM = r'''
#include <assert.h>
#include <stdbool.h>
typedef unsigned UINT;
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
struct pipe_resource { int unused; };
struct pipe_surface { struct pipe_resource *texture; };
struct pipe_framebuffer_state { unsigned width, height; unsigned short layers; unsigned char samples;
  unsigned char nr_cbufs; struct pipe_surface cbufs[8], zsbuf; };
struct pipe_context { unsigned binds; struct pipe_framebuffer_state active; };
typedef struct { float TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth; } D3D10_DDI_VIEWPORT;
typedef struct { struct pipe_framebuffer_state fb; UINT agx_viewport_extent[2]; } Device;
static Device *CastDevice(Device *d) { return d; }
static void set_fb(struct pipe_context *p, const struct pipe_framebuffer_state *f) { p->active = *f; ++p->binds; }
@@HELPER@@
static void viewports(Device *hDevice, struct pipe_context *pipe, UINT NumViewports,
                      const D3D10_DDI_VIEWPORT *pViewports) {
@@VIEWPORTS@@
int main(void) {
  struct pipe_context pipe = {0};
  struct pipe_resource tex = {0};
  Device d = {0};
  /* Nothing bound, no viewport yet: 1x1. */
  assert(AgxD3d10NoAttachmentSize(&d));
  assert(d.fb.width == 1 && d.fb.height == 1 && d.fb.layers == 1 && d.fb.samples == 1);
  D3D10_DDI_VIEWPORT vp[2] = {{0, 0, 63.5f, 32, 0, 1}, {10, 20, 30, 40, 0, 1}};
  viewports(&d, &pipe, 2, vp);
  assert(pipe.binds == 1 && pipe.active.width == 64 && pipe.active.height == 60);
  /* The same size is not rebound. */
  viewports(&d, &pipe, 2, vp);
  assert(pipe.binds == 1);
  /* Clamped to the render-target window; negative and NaN edges count 0. */
  D3D10_DDI_VIEWPORT big = {-100, 0, 1e9f, 16, 0, 1};
  viewports(&d, &pipe, 1, &big);
  assert(pipe.active.width == 16384 && pipe.active.height == 16);
  D3D10_DDI_VIEWPORT odd = {-50, 0, 10, 0.0f / 0.0f, 0, 1};
  viewports(&d, &pipe, 1, &odd);
  assert(pipe.active.width == 1 && pipe.active.height == 1);
  /* With a render target bound the views' size stands. */
  d.fb.nr_cbufs = 1; d.fb.cbufs[0].texture = &tex; d.fb.width = 800; d.fb.height = 600;
  UINT binds = pipe.binds;
  viewports(&d, &pipe, 2, vp);
  assert(pipe.binds == binds && d.fb.width == 800 && d.fb.height == 600);
  d.fb.nr_cbufs = 0; d.fb.zsbuf.texture = &tex;
  assert(!AgxD3d10NoAttachmentSize(&d) && d.fb.width == 800);
  return 0;
}
'''


class NoAttachmentFramebuffer(unittest.TestCase):
    def test_viewports_size_the_framebuffer(self):
        text = SCRIPT.read_text()
        helper = re.search(r"static inline bool\nAgxD3d10NoAttachmentSize\(Device \*pDevice\)\n\{.*?\n\}",
                           text, re.S).group(0)
        vp = re.search(r"\n(   Device \*pDevice = CastDevice\(hDevice\);\n   UINT extent\[2\].*?"
                       r"pipe->set_framebuffer_state\(pipe, &pDevice->fb\);\n\})", text, re.S).group(1)
        vp = vp.replace("pipe->set_framebuffer_state(pipe, &pDevice->fb)", "set_fb(pipe, &pDevice->fb)")
        self.assertIn("(void)AgxD3d10NoAttachmentSize(pDevice);\n   pipe->set_framebuffer_state(pipe, &pDevice->fb);",
                      text, "SetRenderTargets sizes an attachment-less framebuffer before binding it")
        program = SHIM.replace("@@HELPER@@", helper).replace("@@VIEWPORTS@@", vp)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "fb.c"; exe = Path(tmp) / "fb"
            src.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                           check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
