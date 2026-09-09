# Standard Present boundary after EXP682

Native red and four alternating native output frames are hardware proven.
This source review does not change KMD/UMD capabilities or execute a new EXP.

## First Windows prerequisite

EXP644: late interactive BLT returned PRESENT_OCCLUDED; DWM reported
MILERR_DEVICE_CREATION_FAILURE. EXP647: exclusive VidPN ownership denied.
EXP648: UMD loaded, pipeline support zero, CreateDevice not called.
These remain current: `umd.c:AdmissionUmdGetCaps` still returns pipeline0,
and CreateDevice registers a limited D3DWDDM1_3 table. The selected contract
is instead D3D10_0/FL10_0 with Mesa D3D frontend reuse. The mandatory inventory
in `docs/superpowers/specs/accelerated-desktop-contract.json` remains incomplete.
Do not run another exclusive-owner or windowed-occlusion discriminator.

## Exact mapping

| Boundary | Current source | Required contract |
|---|---|---|
| Runtime device | umd.c GetCaps0 and partial device table | Complete selected D3D10 frontend/backend obligations before advertising |
| Mesa DXGI | DxgiFns.cpp _Present flushes software frontbuffer | Actual Windows allocation handles through runtime present callback |
| Mesa device | Device.cpp returns DXGI_STATUS_NO_REDIRECTION | Working shared resource/redirection contract for DWM |
| Mesa target | d3d10_gdi.c creates softpipe/llvmpipe; D3DKMT.cpp contains software shims | Hardware Windows winsys; exclude software target and fake device handles |
| Current native output | 16x256 allocation,16x16 tiled rendered range | A displayable resource with explicit format/layout/geometry and retained lifetime |
| Source address | display.c accepts2560x1600/stride10240/A8R8G8B8 | Same completed displayable allocation handed through standard source address |
| Current UMD Present | DXGIDDICB_PRESENT with hSrcAllocation/hContext/pDXGIContext | Preserve that supported callback model in selected frontend |

Microsoft DXGIDDICB_PRESENT requires the allocation handle returned by the
runtime allocator, the existing KMD context and unchanged opaque DXGI context.
SetVidPnSourceAddress consumes a segment-relative primary address and private
allocation properties such as pitch/swizzle. A small tiled render attachment
cannot be silently described as a linear full-size primary.

Primary sources:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgiddicb_present
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_setvidpnsourceaddress
- `.local/reference/wdk26100/dxgiddi.h`, `d3d10umddi.h`, `d3dkmthk.h`
- Pinned Mesa9aa1215f: frontends/d3d10umd/{Adapter,Device,DxgiFns,D3DKMT}.cpp,
  targets/d3d10umd/{meson.build,d3d10_gdi.c}, licenses/MIT.
- Production umd.c, umd_win32_screen.c, present_windows.c, display.c,
  standard_present_windows.c, scanout_windows.c.

## Causal decision

The next integration is the already selected hardware Mesa D3D frontend with
device-scoped Windows callback/winsys ownership and shared resource support.
Its screen factory must receive a real Windows device context; upstream
Adapter creates a screen before CreateDevice, whereas the current Windows
winsys needs the device's allocator/context callbacks. Resolve this ownership
bridge explicitly rather than introducing a process-global device singleton.
Keep pipeline mask0 until the full mandatory contract and backend features pass.

Adding BLT acceptance to the limited old UMD would not resolve the earlier
runtime device prerequisite and would not satisfy the requested same-allocation
Present test. That tempting side patch is not selected. No implementation was
made in the old UMD during this review.

Once the frontend and displayable resource contract are executable, one
runtime-managed hardware test may correlate render allocation/fence through
Present callback, KMD source address, exact A408/D589 and physical panel image.
Do not use CPU untile/copy or a capability-bit toggle to claim that acceptance.
