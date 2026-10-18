"""EXP1330: ClearView clears rectangles without pipe->clear_render_target.

Invariant caught: with FL 11_0 reported, dwm.exe, LogonUI and M365Copilot
faulted at address 0 (reject-null-call: the UMD at the ClearView rectangle
loop, called from d3d11 on behalf of d2d1). ClearView called
pipe->clear_render_target for each rectangle, and Asahi implements no
clear_render_target (and no scissored clears). The rectangles are cleared
through the winsys blitter helper, once per non-empty rectangle, with the
colour converted for the view's format; a helper failure is refused, the
whole-view clear keeps the ClearRenderTargetView path, and UAV rectangles
stay refused. Runs the real ClearView against stubs.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp"

PROGRAM = r'''
#include <cassert>
#include <cstdint>
#include <climits>
#include <vector>
typedef long HRESULT; typedef unsigned UINT; typedef float FLOAT; typedef void VOID; typedef long LONG;
#define APIENTRY
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_FAIL ((HRESULT)0x80004005L)
enum D3D11DDI_HANDLETYPE { D3D10DDI_HT_RENDERTARGETVIEW = 4, D3D11DDI_HT_UNORDEREDACCESSVIEW = 13, OTHER = 99 };
struct D3D10DDI_HDEVICE { void *pDrvPrivate; };
struct D3D10DDI_HRENDERTARGETVIEW { void *pDrvPrivate; };
struct D3D11DDI_HUNORDEREDACCESSVIEW { void *pDrvPrivate; };
struct D3D10_DDI_RECT { LONG left, top, right, bottom; };
enum pipe_format { FMT_UNORM, FMT_UINT, FMT_SINT };
union pipe_color_union { float f[4]; int i[4]; unsigned ui[4]; };
struct pipe_surface { pipe_format format; };
struct pipe_context { void (*clear_render_target)(pipe_context *, pipe_surface *, const pipe_color_union *,
  unsigned, unsigned, unsigned, unsigned, bool); };
struct Device { pipe_context *pipe; };
struct RenderTargetView { pipe_surface surface; };
static Device *CastDevice(D3D10DDI_HDEVICE h) { return static_cast<Device *>(h.pDrvPrivate); }
static RenderTargetView *CastRenderTargetView(D3D10DDI_HRENDERTARGETVIEW h) {
  return static_cast<RenderTargetView *>(h.pDrvPrivate); }
static bool util_format_is_pure_integer(pipe_format f) { return f != FMT_UNORM; }
static bool util_format_is_pure_sint(pipe_format f) { return f == FMT_SINT; }
static unsigned refusals, wholeClears, uavClears;
static void refuse(D3D10DDI_HDEVICE, const char *, HRESULT, UINT) { ++refusals; }
struct Rect { unsigned x, y, w, h; pipe_color_union c; };
static std::vector<Rect> cleared; static bool helperFails;
static int AgxD3d10WindowsClearRenderTargetRect(pipe_context *p, pipe_surface *s, const pipe_color_union *c,
    UINT x, UINT y, UINT w, UINT h) {
  assert(p && s && c); if (helperFails) return 0; cleared.push_back({x, y, w, h, *c}); return 1; }
static void wholeRtv(D3D10DDI_HDEVICE, D3D10DDI_HRENDERTARGETVIEW, FLOAT *) { ++wholeClears; }
static void wholeUav(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW, const FLOAT *) { ++uavClears; }
static struct { void (*pfnClearRenderTargetView)(D3D10DDI_HDEVICE, D3D10DDI_HRENDERTARGETVIEW, FLOAT *);
  void (*pfnClearUnorderedAccessViewFloat)(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW, const FLOAT *); }
  base11 = {wholeRtv, wholeUav};
@@FUNCTION@@
int main() {
  pipe_context pipe = {nullptr};  /* Asahi: no clear_render_target */
  Device dev = {&pipe}; D3D10DDI_HDEVICE h = {&dev};
  RenderTargetView rtv = {{FMT_UNORM}};
  const FLOAT color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
  D3D10_DDI_RECT rects[3] = {{1, 2, 33, 18}, {5, 5, 5, 9}, {0, 0, 32, 32}};
  ClearView(h, D3D10DDI_HT_RENDERTARGETVIEW, &rtv, color, rects, 3);
  assert(cleared.size() == 2 && refusals == 0);
  assert(cleared[0].x == 1 && cleared[0].y == 2 && cleared[0].w == 32 && cleared[0].h == 16);
  assert(cleared[0].c.f[1] == 0.5f && cleared[1].w == 32 && cleared[1].h == 32);
  /* Integer views get converted, saturated values. */
  RenderTargetView urtv = {{FMT_UINT}}, irtv = {{FMT_SINT}};
  const FLOAT ic[4] = {3.0f, -1.0f, 5e9f, 0.0f / 0.0f};
  cleared.clear(); ClearView(h, D3D10DDI_HT_RENDERTARGETVIEW, &urtv, ic, rects, 1);
  assert(cleared[0].c.ui[0] == 3u && cleared[0].c.ui[1] == 0u && cleared[0].c.ui[2] == UINT32_MAX &&
         cleared[0].c.ui[3] == 0u);
  cleared.clear(); ClearView(h, D3D10DDI_HT_RENDERTARGETVIEW, &irtv, ic, rects, 1);
  assert(cleared[0].c.i[0] == 3 && cleared[0].c.i[1] == -1 && cleared[0].c.i[2] == INT32_MAX &&
         cleared[0].c.i[3] == 0);
  /* The whole view keeps ClearRenderTargetView. */
  ClearView(h, D3D10DDI_HT_RENDERTARGETVIEW, &rtv, color, nullptr, 0);
  assert(wholeClears == 1);
  /* A helper failure is refused. */
  helperFails = true; ClearView(h, D3D10DDI_HT_RENDERTARGETVIEW, &rtv, color, rects, 1);
  assert(refusals == 1); helperFails = false;
  /* UAV: whole view only; other views refused. */
  ClearView(h, D3D11DDI_HT_UNORDEREDACCESSVIEW, &rtv, color, nullptr, 0);
  assert(uavClears == 1);
  ClearView(h, D3D11DDI_HT_UNORDEREDACCESSVIEW, &rtv, color, rects, 1);
  ClearView(h, OTHER, &rtv, color, nullptr, 0);
  ClearView(h, D3D10DDI_HT_RENDERTARGETVIEW, nullptr, color, nullptr, 0);
  assert(refusals == 4);
  return 0;
}
'''


class Wddm2ClearViewRects(unittest.TestCase):
    def test_rectangles_use_the_winsys_clear(self):
        text = SRC.read_text()
        function = re.search(r"void APIENTRY\nClearView\(.*?\n\}", text, re.S).group(0)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "c.cpp"; exe = Path(tmp) / "c"
            src.write_text(PROGRAM.replace("@@FUNCTION@@", function))
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                           check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
