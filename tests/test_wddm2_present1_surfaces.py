"""EXP1328: Present1 with several surfaces presents them together.

Invariant caught: once shared composition masks were admitted, the FL 11_0
desktop (LogonUI, explorer, StartMenuExperienceHost) called Present1 with
two to four surfaces 1095 times; the DDI refused every call with more than
one surface (reject-dxgi-present1, E_NOTIMPL). DXGI_DDI_ARG_PRESENT1: with
several surfaces hDstResource is NULL and the driver passes only the last
source to pfnPresentCb. Every surface must be one of this device's
presentation resources (subresource 0) under Present's flag and interval
rules; they are submitted together, in order, and the single-surface call
keeps its Present path. Runs the real Present1 against stubs.
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
#include <vector>
typedef long HRESULT; typedef unsigned UINT;
#define APIENTRY
#define S_OK ((HRESULT)0)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
typedef uintptr_t DXGI_DDI_HDEVICE; typedef uintptr_t DXGI_DDI_HRESOURCE;
enum DXGI_DDI_FLIP_INTERVAL_TYPE { DXGI_DDI_FLIP_INTERVAL_IMMEDIATE = 0, DXGI_DDI_FLIP_INTERVAL_ONE = 1,
  DXGI_DDI_FLIP_INTERVAL_TWO = 2 };
struct DXGI_DDI_PRESENT_FLAGS { UINT Value; };
struct DXGI_DDI_ARG_PRESENTSURFACE { DXGI_DDI_HRESOURCE hSurface; UINT SubResourceIndex; };
struct DXGI_DDI_ARG_PRESENT1 { DXGI_DDI_HDEVICE hDevice; const DXGI_DDI_ARG_PRESENTSURFACE *phSurfacesToPresent;
  UINT SurfacesToPresent; DXGI_DDI_HRESOURCE hDstResource; UINT DstSubResourceIndex; void *pDXGIContext;
  DXGI_DDI_PRESENT_FLAGS Flags; DXGI_DDI_FLIP_INTERVAL_TYPE FlipInterval; };
struct DXGI_DDI_ARG_PRESENT { DXGI_DDI_HDEVICE hDevice; DXGI_DDI_HRESOURCE hSurfaceToPresent;
  UINT SrcSubResourceIndex; DXGI_DDI_HRESOURCE hDstResource; UINT DstSubResourceIndex; void *pDXGIContext;
  DXGI_DDI_PRESENT_FLAGS Flags; DXGI_DDI_FLIP_INTERVAL_TYPE FlipInterval; };
struct AGX_D3D10_WINDOWS_DEVICE { int unused; };
struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE { int id; };
struct Device { AGX_D3D10_WINDOWS_DEVICE *windows; };
struct Resource { Device *owner_device; AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation; };
static Device *CastDevice(DXGI_DDI_HDEVICE h) { return reinterpret_cast<Device *>(h); }
static Resource *CastResource(DXGI_DDI_HRESOURCE h) { return reinterpret_cast<Resource *>(h); }
static unsigned receipts, singles;
static void dxgi_receipt(const char *, HRESULT, UINT, UINT) { ++receipts; }
static std::vector<int> submitted; static void *submittedContext; static unsigned submits;
static HRESULT AgxD3d10WindowsPresentationSubmitSurfaces(AGX_D3D10_WINDOWS_DEVICE *,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *const *r, UINT n, void *ctx) {
  ++submits; submitted.clear(); for (UINT i = 0; i < n; ++i) submitted.push_back(r[i]->id);
  submittedContext = ctx; return S_OK; }
static HRESULT single(DXGI_DDI_ARG_PRESENT *a) { ++singles; return a->hSurfaceToPresent ? S_OK : E_INVALIDARG; }
static struct { HRESULT (*pfnPresent)(DXGI_DDI_ARG_PRESENT *); } dxgi11 = {single};
@@FUNCTION@@
int main() {
  AGX_D3D10_WINDOWS_DEVICE w{}; Device dev{&w}, other{&w};
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE p1{1}, p2{2}, p3{3};
  Resource r1{&dev, &p1}, r2{&dev, &p2}, r3{&dev, &p3}, foreign{&other, &p3}, plain{&dev, nullptr};
  DXGI_DDI_ARG_PRESENTSURFACE s[3] = {{(uintptr_t)&r1, 0}, {(uintptr_t)&r2, 0}, {(uintptr_t)&r3, 0}};
  int ctx;
  DXGI_DDI_ARG_PRESENT1 a{(uintptr_t)&dev, s, 3, 0, 0, &ctx, {0x2u}, DXGI_DDI_FLIP_INTERVAL_ONE};
  assert(Present1(&a) == S_OK && submits == 1 && singles == 0 && receipts == 0);
  assert((submitted == std::vector<int>{1, 2, 3}) && submittedContext == &ctx);
  a.SurfacesToPresent = 2; a.Flags.Value = 0x1u; a.FlipInterval = DXGI_DDI_FLIP_INTERVAL_IMMEDIATE;
  assert(Present1(&a) == S_OK && submits == 2 && (submitted == std::vector<int>{1, 2}));
  /* One surface keeps the Present path. */
  a.SurfacesToPresent = 1;
  assert(Present1(&a) == S_OK && singles == 1 && submits == 2);
  /* Refusals: a destination, Present's flags and intervals, a subresource,
     a foreign or non-presentation surface, a missing array. */
  a.SurfacesToPresent = 3; a.hDstResource = (uintptr_t)&r1;
  assert(Present1(&a) == E_INVALIDARG); a.hDstResource = 0;
  a.Flags.Value = 0x4u; assert(Present1(&a) == E_INVALIDARG); a.Flags.Value = 0x2u;
  a.FlipInterval = DXGI_DDI_FLIP_INTERVAL_TWO; assert(Present1(&a) == E_INVALIDARG);
  a.FlipInterval = DXGI_DDI_FLIP_INTERVAL_ONE;
  s[1].SubResourceIndex = 1; assert(Present1(&a) == E_INVALIDARG); s[1].SubResourceIndex = 0;
  s[2].hSurface = (uintptr_t)&foreign; assert(Present1(&a) == E_INVALIDARG);
  s[2].hSurface = (uintptr_t)&plain; assert(Present1(&a) == E_INVALIDARG);
  a.phSurfacesToPresent = nullptr; assert(Present1(&a) == E_INVALIDARG);
  assert(submits == 2 && receipts == 7);
  assert(Present1(nullptr) == E_INVALIDARG);
  return 0;
}
'''


class Wddm2Present1Surfaces(unittest.TestCase):
    def test_several_surfaces_are_submitted_together(self):
        text = SRC.read_text()
        function = re.search(r"HRESULT APIENTRY\nPresent1\(.*?\n\}", text, re.S).group(0)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "p.cpp"; exe = Path(tmp) / "p"
            src.write_text(PROGRAM.replace("@@FUNCTION@@", function))
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Werror", "-Wno-unused-function",
                            "-fsanitize=address,undefined", str(src), "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
