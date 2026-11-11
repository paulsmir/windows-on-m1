"""S2: the D3D11 UMD's DXGI multiplane-overlay answers follow the kernel.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
(1) Predicate off = today's answers: while the kernel does not answer
    D3DKMTGetMultiPlaneOverlayCaps with two planes, GetMultiplaneOverlayCaps,
    GetMultiplaneOverlayGroupCaps, PresentMultiplaneOverlay(1) and
    CheckMultiplaneOverlayColorSpaceSupport must return exactly the values,
    return codes and receipts of the pre-S2 driver (MaxPlanes 1, no group,
    E_NOTIMPL, Supported FALSE).  A regression there changes what DWM sees
    on every bench boot.
(2) Predicate on: the two-plane caps of S2 specification v1 (MaxPlanes 2,
    one RGB group of two unscaled planes), sRGB 8-bit RGB only, and the
    deterministic translation of a DXGI plane list into
    DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY (one allocation per plane in plane
    order, the DXGI context unchanged, the device's kernel context, no
    broadcast); malformed plane lists are refused before any callback.
(3) The stub structures the checks compile against carry the members, in
    order, of the pinned dxgiddi.h, and the constants equal the pinned
    header values.
Runs the real DDI functions (agx_wddm2_ddi.cpp) and the real header
(agx_dxgi_mpo.h) against stubs.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / "drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp"
D3D11 = ROOT / "drivers/apple-agx/mesa/d3d11"

STUBS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
typedef long HRESULT; typedef unsigned UINT; typedef unsigned long DWORD; typedef int BOOL;
typedef void *HANDLE; typedef UINT D3DKMT_HANDLE; typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
#define TRUE 1
#define FALSE 0
#define APIENTRY
#define S_OK ((HRESULT)0)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define DXGI_DDI_MAX_MULTIPLANE_OVERLAY_ALLOCATIONS 16
#define D3DDDI_MAX_BROADCAST_CONTEXT 64
typedef uintptr_t DXGI_DDI_HDEVICE; typedef uintptr_t DXGI_DDI_HRESOURCE;
struct RECT { long left, top, right, bottom; };
/* Members as in the pinned dxgiddi.h (checked by test_stubs_match_the_pinned_header). */
typedef struct DXGI_DDI_MULTIPLANE_OVERLAY_CAPS { UINT MaxPlanes; UINT NumCapabilityGroups; }
  DXGI_DDI_MULTIPLANE_OVERLAY_CAPS;
typedef struct DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS { UINT NumPlanes; float MaxStretchFactor;
  float MaxShrinkFactor; UINT OverlayCaps; UINT StereoCaps; } DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS;
typedef struct DXGIDDI_MULTIPLANE_OVERLAY_ALLOCATION_INFO { D3DKMT_HANDLE PresentAllocation;
  UINT SubResourceIndex; } DXGIDDI_MULTIPLANE_OVERLAY_ALLOCATION_INFO;
typedef struct DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY { void *pDXGIContext; HANDLE hContext;
  UINT BroadcastContextCount; HANDLE BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
  DWORD AllocationInfoCount;
  DXGIDDI_MULTIPLANE_OVERLAY_ALLOCATION_INFO AllocationInfo[DXGI_DDI_MAX_MULTIPLANE_OVERLAY_ALLOCATIONS];
} DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY;
struct DXGI_DDI_MULTIPLANE_OVERLAY_ATTRIBUTES { UINT Flags; RECT SrcRect, DstRect, ClipRect; };
struct DXGI_DDI_MULTIPLANE_OVERLAY_ATTRIBUTES1 { UINT Flags; RECT SrcRect, DstRect, ClipRect; UINT ColorSpace; };
typedef struct _DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY { UINT LayerIndex; BOOL Enabled;
  DXGI_DDI_HRESOURCE hResource; UINT SubResourceIndex; DXGI_DDI_MULTIPLANE_OVERLAY_ATTRIBUTES PlaneAttributes; }
  DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY;
typedef struct _DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY1 { UINT LayerIndex; BOOL Enabled;
  DXGI_DDI_HRESOURCE hResource; UINT SubResourceIndex; DXGI_DDI_MULTIPLANE_OVERLAY_ATTRIBUTES1 PlaneAttributes; }
  DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY1;
struct DXGI_DDI_PRESENT_FLAGS { UINT Value; };
typedef UINT DXGI_DDI_FLIP_INTERVAL_TYPE;
typedef struct _DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS { DXGI_DDI_HDEVICE hDevice;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; DXGI_DDI_MULTIPLANE_OVERLAY_CAPS MultiplaneOverlayCaps; }
  DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS;
typedef struct _DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS { DXGI_DDI_HDEVICE hDevice;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; UINT GroupIndex;
  DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS MultiplaneOverlayGroupCaps; } DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS;
typedef struct _DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY { DXGI_DDI_HDEVICE hDevice; void *pDXGIContext;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; DXGI_DDI_PRESENT_FLAGS Flags; DXGI_DDI_FLIP_INTERVAL_TYPE FlipInterval;
  UINT PresentPlaneCount; DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY *pPresentPlanes; UINT Reserved; }
  DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY;
typedef struct _DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1 { DXGI_DDI_HDEVICE hDevice; void *pDXGIContext;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; DXGI_DDI_PRESENT_FLAGS Flags; DXGI_DDI_FLIP_INTERVAL_TYPE FlipInterval;
  UINT PresentPlaneCount; DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY1 *pPresentPlanes; }
  DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1;
typedef struct _DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT { DXGI_DDI_HDEVICE hDevice;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; UINT Format; UINT ColorSpace; BOOL Supported; }
  DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT;
#define AGX_DXGI_MPO_DDI_TYPES
#include "agx_dxgi_mpo.h"
'''

HEADER_PROGRAM = STUBS + r'''
int main() {
  /* Predicate: only a successful answer of exactly two RGB-capable planes. */
  assert(AgxDxgiMpoKernelTwoPlanes(1, 2, 2) == 1);
  assert(!AgxDxgiMpoKernelTwoPlanes(0, 2, 2) && !AgxDxgiMpoKernelTwoPlanes(1, 1, 1));
  assert(!AgxDxgiMpoKernelTwoPlanes(1, 2, 1) && !AgxDxgiMpoKernelTwoPlanes(1, 3, 3));
  assert(!AgxDxgiMpoKernelTwoPlanes(1, 0, 0));
  /* Caps off = today's (MaxPlanes 1, no group); on = 2 and one group. */
  DXGI_DDI_MULTIPLANE_OVERLAY_CAPS caps = {77, 77};
  AgxDxgiMpoFillCaps(0, &caps); assert(caps.MaxPlanes == 1 && caps.NumCapabilityGroups == 0);
  AgxDxgiMpoFillCaps(1, &caps); assert(caps.MaxPlanes == 2 && caps.NumCapabilityGroups == 1);
  DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS g = {9, 9.0f, 9.0f, 9, 9};
  assert(!AgxDxgiMpoFillGroupCaps(0, 0, &g) && g.NumPlanes == 9 && g.OverlayCaps == 9);
  assert(!AgxDxgiMpoFillGroupCaps(1, 1, &g) && g.NumPlanes == 9);
  assert(AgxDxgiMpoFillGroupCaps(1, 0, &g));
  assert(g.NumPlanes == 2 && g.MaxStretchFactor == 1.0f && g.MaxShrinkFactor == 1.0f);
  assert(g.OverlayCaps == 0x20u && g.StereoCaps == 0);
  /* Colour space: on, 8-bit BGRA/RGBA UNORM in RGB_FULL_G22_NONE_P709 only. */
  assert(AgxDxgiMpoColorSpaceSupported(1, 87, 0) && AgxDxgiMpoColorSpaceSupported(1, 28, 0));
  for (unsigned f : {87u, 28u}) assert(!AgxDxgiMpoColorSpaceSupported(0, f, 0));
  for (unsigned cs = 1; cs < 30; ++cs) assert(!AgxDxgiMpoColorSpaceSupported(1, 87, cs));
  for (unsigned f : {0u, 24u, 29u, 88u, 91u, 10u, 2u}) assert(!AgxDxgiMpoColorSpaceSupported(1, f, 0));
  /* Plane list precondition. */
  AGX_DXGI_MPO_PLANE p[2] = {{0, 1, 1, 0}, {1, 1, 1, 0}};
  assert(AgxDxgiMpoCheckPlanes(p, 2) == AgxDxgiMpoPlanesOk);
  assert(AgxDxgiMpoCheckPlanes(p, 1) == AgxDxgiMpoPlanesOk);
  p[0].LayerIndex = 1; p[1].LayerIndex = 0; assert(AgxDxgiMpoCheckPlanes(p, 2) == AgxDxgiMpoPlanesOk);
  assert(AgxDxgiMpoCheckPlanes(nullptr, 2) == AgxDxgiMpoPlanesArgument);
  assert(AgxDxgiMpoCheckPlanes(p, 0) == AgxDxgiMpoPlanesCount);
  assert(AgxDxgiMpoCheckPlanes(p, 3) == AgxDxgiMpoPlanesCount);
  p[1].LayerIndex = 1; assert(AgxDxgiMpoCheckPlanes(p, 2) == AgxDxgiMpoPlanesLayerIndex);
  p[0].LayerIndex = 2; assert(AgxDxgiMpoCheckPlanes(p, 2) == AgxDxgiMpoPlanesLayerIndex);
  p[0].LayerIndex = 1; assert(AgxDxgiMpoCheckPlanes(p, 1) == AgxDxgiMpoPlanesLayerIndex);
  p[0].LayerIndex = 0;
  p[1].Enabled = 0; assert(AgxDxgiMpoCheckPlanes(p, 2) == AgxDxgiMpoPlanesDisabled); p[1].Enabled = 1;
  p[1].HasResource = 0; assert(AgxDxgiMpoCheckPlanes(p, 2) == AgxDxgiMpoPlanesResource); p[1].HasResource = 1;
  p[1].SubResourceIndex = 1; assert(AgxDxgiMpoCheckPlanes(p, 2) == AgxDxgiMpoPlanesSubresource);
  p[1].SubResourceIndex = 0;
  /* Callback translation: plane order, DXGI context, kernel context, no broadcast. */
  DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY cb; memset(&cb, 0xab, sizeof(cb));
  int dxgi, kctx; D3DKMT_HANDLE allocations[2] = {0x40000041u, 0x40000082u};
  AgxDxgiMpoFillPresentCb(&cb, &dxgi, &kctx, allocations, p, 2);
  assert(cb.pDXGIContext == &dxgi && cb.hContext == &kctx && cb.BroadcastContextCount == 0);
  assert(cb.AllocationInfoCount == 2);
  assert(cb.AllocationInfo[0].PresentAllocation == 0x40000041u && cb.AllocationInfo[0].SubResourceIndex == 0);
  assert(cb.AllocationInfo[1].PresentAllocation == 0x40000082u && cb.AllocationInfo[1].SubResourceIndex == 0);
  for (unsigned i = 0; i < D3DDDI_MAX_BROADCAST_CONTEXT; ++i) assert(cb.BroadcastContext[i] == nullptr);
  for (unsigned i = 2; i < DXGI_DDI_MAX_MULTIPLANE_OVERLAY_ALLOCATIONS; ++i)
    assert(cb.AllocationInfo[i].PresentAllocation == 0 && cb.AllocationInfo[i].SubResourceIndex == 0);
  /* The neutral plane keeps the four members, booleans as 0/1. */
  AGX_DXGI_MPO_PLANE n = AgxDxgiMpoPlane(1, 7, 1, 3);
  assert(n.LayerIndex == 1 && n.Enabled == 1 && n.HasResource == 1 && n.SubResourceIndex == 3);
  n = AgxDxgiMpoPlane(0, 0, 0, 0);
  assert(n.LayerIndex == 0 && n.Enabled == 0 && n.HasResource == 0 && n.SubResourceIndex == 0);
  return 0;
}
'''

DDI_PROGRAM = STUBS + r'''
struct AGX_D3D10_WINDOWS_DEVICE { int unused; };
struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE { int id; };
struct Device { AGX_D3D10_WINDOWS_DEVICE *windows; };
struct Resource { Device *owner_device; AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *presentation; };
static Device *CastDevice(DXGI_DDI_HDEVICE h) { return reinterpret_cast<Device *>(h); }
static Resource *CastResource(DXGI_DDI_HRESOURCE h) { return reinterpret_cast<Resource *>(h); }
struct Receipt { std::string stage; HRESULT hr; UINT a, b; };
static std::vector<Receipt> receipts;
static void dxgi_receipt(const char *stage, HRESULT hr, UINT a, UINT b) { receipts.push_back({stage, hr, a, b}); }
static bool kernelTwo; static unsigned predicateCalls; static UINT askedSource;
extern "C" BOOL AgxD3d10WindowsKernelMpoTwoPlanes(AGX_D3D10_WINDOWS_DEVICE *, UINT source) {
  ++predicateCalls; askedSource = source; return kernelTwo ? TRUE : FALSE; }
static unsigned submits; static std::vector<int> submitted; static std::vector<UINT> layers; static void *submittedContext;
extern "C" HRESULT AgxD3d10WindowsPresentationSubmitPlanes(AGX_D3D10_WINDOWS_DEVICE *,
    AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE *const *r, const AGX_DXGI_MPO_PLANE *p, UINT n, void *ctx) {
  ++submits; submitted.clear(); layers.clear();
  for (UINT i = 0; i < n; ++i) { submitted.push_back(r[i]->id); layers.push_back(p[i].LayerIndex); }
  submittedContext = ctx; return (HRESULT)0x12345; }
namespace {
@@FUNCTIONS@@
}
static bool same(const Receipt &r, const char *s, HRESULT hr, UINT a, UINT b) {
  return r.stage == s && r.hr == hr && r.a == a && r.b == b; }
int main() {
  AGX_D3D10_WINDOWS_DEVICE w{}; Device dev{&w}, other{&w};
  AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE desk{1}, app{2};
  Resource rd{&dev, &desk}, ra{&dev, &app}, foreign{&other, &app}, plain{&dev, nullptr};
  int ctx;
  /* ---- Predicate off: the pre-S2 answers, codes and receipts. ---- */
  kernelTwo = false;
  DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS c{(uintptr_t)&dev, 0, {55, 55}};
  assert(GetMultiplaneOverlayCaps(&c) == S_OK && c.MultiplaneOverlayCaps.MaxPlanes == 1 &&
         c.MultiplaneOverlayCaps.NumCapabilityGroups == 0);
  assert(GetMultiplaneOverlayCaps(nullptr) == E_INVALIDARG);
  DXGI_DDI_ARG_GETMULTIPLANEOVERLAYGROUPCAPS gc{(uintptr_t)&dev, 0, 0, {5, 5.0f, 5.0f, 5, 5}};
  assert(GetMultiplaneOverlayGroupCaps(&gc) == E_INVALIDARG && gc.MultiplaneOverlayGroupCaps.NumPlanes == 5);
  assert(receipts.size() == 1 && same(receipts[0], "reject-dxgi-mpo-group", E_INVALIDARG, 0, 0));
  assert(GetMultiplaneOverlayGroupCaps(nullptr) == E_INVALIDARG);
  assert(receipts.size() == 2 && same(receipts[1], "reject-dxgi-mpo-group", E_INVALIDARG, ~0u, 0));
  DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY1 planes1[2] = {{0, 1, (uintptr_t)&rd, 0, {}}, {1, 1, (uintptr_t)&ra, 0, {}}};
  DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1 p1{(uintptr_t)&dev, &ctx, 0, {2}, 1, 2, planes1};
  assert(PresentMultiplaneOverlay1(&p1) == E_NOTIMPL && submits == 0);
  assert(receipts.size() == 3 && same(receipts[2], "reject-dxgi-mpo-present1", E_NOTIMPL, 1, 1));
  assert(PresentMultiplaneOverlay1(nullptr) == E_NOTIMPL);
  assert(receipts.size() == 4 && same(receipts[3], "reject-dxgi-mpo-present1", E_NOTIMPL, 0, 1));
  DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY planes0[2] = {{0, 1, (uintptr_t)&rd, 0, {}}, {1, 1, (uintptr_t)&ra, 0, {}}};
  DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY p0{(uintptr_t)&dev, &ctx, 0, {2}, 1, 2, planes0, 0};
  assert(PresentMultiplaneOverlay(&p0) == E_NOTIMPL && submits == 0);
  assert(receipts.size() == 5 && same(receipts[4], "reject-dxgi-mpo-present", E_NOTIMPL, 2, 0));
  assert(PresentMultiplaneOverlay(nullptr) == E_NOTIMPL);
  assert(receipts.size() == 6 && same(receipts[5], "reject-dxgi-mpo-present", E_NOTIMPL, ~0u, 0));
  DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT cs{(uintptr_t)&dev, 0, 87, 0, TRUE};
  assert(CheckMultiplaneOverlayColorSpaceSupport(&cs) == S_OK && cs.Supported == FALSE);
  assert(CheckMultiplaneOverlayColorSpaceSupport(nullptr) == E_INVALIDARG);
  /* A device without a winsys owner never asks the kernel. */
  Device bare{nullptr}; unsigned asked = predicateCalls;
  DXGI_DDI_ARG_GETMULTIPLANEOVERLAYCAPS cb{(uintptr_t)&bare, 0, {}};
  kernelTwo = true;
  assert(GetMultiplaneOverlayCaps(&cb) == S_OK && cb.MultiplaneOverlayCaps.MaxPlanes == 1 &&
         predicateCalls == asked);
  receipts.clear();
  /* ---- Predicate on. ---- */
  c.VidPnSourceId = 0;
  assert(GetMultiplaneOverlayCaps(&c) == S_OK && c.MultiplaneOverlayCaps.MaxPlanes == 2 &&
         c.MultiplaneOverlayCaps.NumCapabilityGroups == 1 && askedSource == 0);
  assert(GetMultiplaneOverlayGroupCaps(&gc) == S_OK && receipts.empty());
  assert(gc.MultiplaneOverlayGroupCaps.NumPlanes == 2 && gc.MultiplaneOverlayGroupCaps.OverlayCaps == 0x20u &&
         gc.MultiplaneOverlayGroupCaps.MaxStretchFactor == 1.0f &&
         gc.MultiplaneOverlayGroupCaps.MaxShrinkFactor == 1.0f && gc.MultiplaneOverlayGroupCaps.StereoCaps == 0);
  gc.GroupIndex = 1;
  assert(GetMultiplaneOverlayGroupCaps(&gc) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-group", E_INVALIDARG, 1, 0));
  cs.Supported = FALSE; assert(CheckMultiplaneOverlayColorSpaceSupport(&cs) == S_OK && cs.Supported == TRUE);
  cs.Format = 28; assert(CheckMultiplaneOverlayColorSpaceSupport(&cs) == S_OK && cs.Supported == TRUE);
  cs.ColorSpace = 12; assert(CheckMultiplaneOverlayColorSpaceSupport(&cs) == S_OK && cs.Supported == FALSE);
  cs.ColorSpace = 0; cs.Format = 91; assert(CheckMultiplaneOverlayColorSpaceSupport(&cs) == S_OK && cs.Supported == FALSE);
  cs.VidPnSourceId = 3; cs.Format = 87; assert(CheckMultiplaneOverlayColorSpaceSupport(&cs) == S_OK && askedSource == 3);
  receipts.clear();
  /* Present: planes in DDI order, DXGI context unchanged, callback result returned. */
  assert(PresentMultiplaneOverlay1(&p1) == (HRESULT)0x12345 && submits == 1);
  assert((submitted == std::vector<int>{1, 2}) && (layers == std::vector<UINT>{0, 1}) && submittedContext == &ctx);
  planes1[0].LayerIndex = 1; planes1[1].LayerIndex = 0;
  assert(PresentMultiplaneOverlay1(&p1) == (HRESULT)0x12345 && (layers == std::vector<UINT>{1, 0}));
  p1.PresentPlaneCount = 1; planes1[0].LayerIndex = 0;
  assert(PresentMultiplaneOverlay1(&p1) == (HRESULT)0x12345 && (submitted == std::vector<int>{1}));
  assert(PresentMultiplaneOverlay(&p0) == (HRESULT)0x12345 && (submitted == std::vector<int>{1, 2}));
  assert(receipts.empty() && submits == 4);
  /* Refusals before any callback, receipted with the reason and plane count. */
  p1.PresentPlaneCount = 2; planes1[1].LayerIndex = 0;
  assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-present1", E_INVALIDARG, AgxDxgiMpoPlanesLayerIndex, 2));
  planes1[1].LayerIndex = 1; planes1[1].hResource = (uintptr_t)&foreign;
  assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-present1", E_INVALIDARG, AgxDxgiMpoPlanesResource, 2));
  planes1[1].hResource = (uintptr_t)&plain; assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG);
  planes1[1].hResource = 0; assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG);
  planes1[1].hResource = (uintptr_t)&ra; planes1[1].Enabled = 0;
  assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-present1", E_INVALIDARG, AgxDxgiMpoPlanesDisabled, 2));
  planes1[1].Enabled = 1; planes1[1].SubResourceIndex = 1;
  assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-present1", E_INVALIDARG, AgxDxgiMpoPlanesSubresource, 2));
  planes1[1].SubResourceIndex = 0;
  p1.PresentPlaneCount = 3;
  assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-present1", E_INVALIDARG, AgxDxgiMpoPlanesCount, 3));
  p1.PresentPlaneCount = 0; assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG);
  p1.PresentPlaneCount = 2; p1.pPresentPlanes = nullptr;
  assert(PresentMultiplaneOverlay1(&p1) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-present1", E_INVALIDARG, AgxDxgiMpoPlanesArgument, 2));
  planes0[1].hResource = (uintptr_t)&foreign;
  assert(PresentMultiplaneOverlay(&p0) == E_INVALIDARG &&
         same(receipts.back(), "reject-dxgi-mpo-present", E_INVALIDARG, AgxDxgiMpoPlanesResource, 2));
  assert(submits == 4);
  return 0;
}
'''


def pinned(name):
    for base in (ROOT / ".local", ROOT.parents[1] / ".local"):
        path = base / "reference" / name
        if path.exists():
            return path.read_text(errors="replace")
    return None


def struct_members(text, name):
    end = re.search(r"\}\s*%s;" % re.escape(name), text)
    assert end, name
    start = text.rindex("typedef struct", 0, end.start())
    body = text[text.index("{", start) + 1:end.start()]
    members = []
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"#[^\n]*", "", body)
    for line in body.split(";"):
        words = re.findall(r"[A-Za-z_]\w*", line.split("[")[0])
        if words:
            members.append(words[-1])
    return members


class Wddm2MpoPlanes(unittest.TestCase):
    def build_and_run(self, program, name):
        with tempfile.TemporaryDirectory(prefix="wddm2-mpo-") as tmp:
            src = Path(tmp) / (name + ".cpp"); exe = Path(tmp) / name
            src.write_text(program)
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-missing-field-initializers",
                            "-fsanitize=address,undefined", "-I", str(D3D11), str(src), "-o", str(exe)],
                           check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_header_answers_and_callback_translation(self):
        self.build_and_run(HEADER_PROGRAM, "header")

    def test_ddi_off_is_today_and_on_follows_the_kernel(self):
        text = DDI.read_text()
        start = text.index("/* S2: multiplane overlays follow the kernel")
        end = text.index("/* No custom present durations")
        self.build_and_run(DDI_PROGRAM.replace("@@FUNCTIONS@@", text[start:end]), "ddi")

    def test_stubs_match_the_pinned_header(self):
        dxgi = pinned("wdk26100/dxgiddi.h")
        if dxgi is None:
            self.skipTest("pinned WDK header not available")
        expected = {
            "DXGI_DDI_MULTIPLANE_OVERLAY_CAPS": ["MaxPlanes", "NumCapabilityGroups"],
            "DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS": ["NumPlanes", "MaxStretchFactor", "MaxShrinkFactor",
                                                       "OverlayCaps", "StereoCaps"],
            "DXGIDDI_MULTIPLANE_OVERLAY_ALLOCATION_INFO": ["PresentAllocation", "SubResourceIndex"],
            "DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY": ["pDXGIContext", "hContext", "BroadcastContextCount",
                                                     "BroadcastContext", "AllocationInfoCount", "AllocationInfo"],
            "DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY1": ["LayerIndex", "Enabled", "hResource", "SubResourceIndex",
                                                     "PlaneAttributes"],
            "DXGI_DDI_PRESENT_MULTIPLANE_OVERLAY": ["LayerIndex", "Enabled", "hResource", "SubResourceIndex",
                                                    "PlaneAttributes"],
            "DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT": ["hDevice", "VidPnSourceId", "Format",
                                                                     "ColorSpace", "Supported"],
            "DXGI_DDI_ARG_PRESENTMULTIPLANEOVERLAY1": ["hDevice", "pDXGIContext", "VidPnSourceId", "Flags",
                                                       "FlipInterval", "PresentPlaneCount", "pPresentPlanes"],
        }
        for name, members in expected.items():
            with self.subTest(struct=name):
                self.assertEqual(struct_members(dxgi, name), members)
                stub = struct_members(STUBS, name)
                self.assertEqual(stub[:len(members)], members)
        self.assertRegex(dxgi, r"DXGI_DDI_MULTIPLANE_OVERLAY_FEATURE_CAPS_RGB\s*=\s*0x20,")
        self.assertRegex(dxgi, r"#define DXGI_DDI_MAX_MULTIPLANE_OVERLAY_ALLOCATIONS\s+16\b")
        # pfnPresentMultiplaneOverlayCb is the callback a D3DWDDM2_0 driver may use:
        # pfnPresentMultiplaneOverlay1Cb is "IS_DXGI1_6_BASE_FUNCTIONS" (WDDM 2.2).
        callbacks = re.search(r"typedef struct DXGI_DDI_BASE_CALLBACKS\s*\{(.*?)\}", dxgi, re.S).group(1)
        self.assertIn("IS_DXGI_MULTIPLANE_OVERLAY_FUNCTIONS macro to check if field is available.\n"
                      "    PFNDDXGIDDI_PRESENT_MULTIPLANE_OVERLAYCB pfnPresentMultiplaneOverlayCb;", callbacks)
        self.assertIn("IS_DXGI1_6_BASE_FUNCTIONS macro to check if field is available.\n"
                      "    PFNDDXGIDDI_PRESENT_MULTIPLANE_OVERLAY1CB pfnPresentMultiplaneOverlay1Cb;", callbacks)
        ukmdt = pinned("wdk26100/d3dukmdt.h")
        fmt = pinned("winsdk26100/shared/dxgiformat.h")
        if ukmdt is not None:
            self.assertRegex(ukmdt, r"D3DDDI_COLOR_SPACE_RGB_FULL_G22_NONE_P709\s*=\s*0,")
        if fmt is not None:
            self.assertRegex(fmt, r"DXGI_FORMAT_R8G8B8A8_UNORM\s*=\s*28,")
            self.assertRegex(fmt, r"DXGI_FORMAT_B8G8R8A8_UNORM\s*=\s*87,")
        header = (D3D11 / "agx_dxgi_mpo.h").read_text()
        for define, value in (("AGX_DXGI_MPO_FEATURE_CAPS_RGB", "0x20u"),
                              ("AGX_DXGI_MPO_FORMAT_R8G8B8A8_UNORM", "28u"),
                              ("AGX_DXGI_MPO_FORMAT_B8G8R8A8_UNORM", "87u"),
                              ("AGX_DXGI_MPO_COLOR_SPACE_SRGB", "0u")):
            self.assertRegex(header, r"#define %s %s\n" % (define, value))

    def test_wddm2_device_uses_the_wddm1_3_callback(self):
        """The present goes through pfnPresentMultiplaneOverlayCb, never the 2.2 callback."""
        winsys = (ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp").read_text()
        body = winsys[winsys.index("HRESULT AgxD3d10WindowsPresentationSubmitPlanes("):]
        body = body[:body.index("\n}\n")]
        self.assertIn("->pfnPresentMultiplaneOverlayCb(", body)
        self.assertNotIn("->pfnPresentMultiplaneOverlay1Cb(", winsys)
        self.assertIn("flush_for_present(Device,Resources,Count)", body)
        self.assertLess(body.index("flush_for_present("), body.index("->pfnPresentMultiplaneOverlayCb("))


if __name__ == "__main__":
    unittest.main()
