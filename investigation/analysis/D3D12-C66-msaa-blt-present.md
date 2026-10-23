# D3D12 C66: presenting a multisampled render target (D3D9On12 blt present)

## Why now

EXP1899G: Portal 2 (D3D9, `-windowed`, 1280x720) ran through D3D9On12.
Its window appeared, and then the first present failed:
`measure-d3d12-present` returned reason 2 (the source surface has no
presentation record) with Flags=1 (Blt) and no destination. After that
the device was removed and the game exited.

The trace shows two 1280x720 surfaces created at device creation:
- BGRA8 (0x57), render target + SRV, 14.1 MiB = 1280x720x4x4, so a 4x
  back buffer;
- a 4x D24S8 depth surface next to it.

The C7b presentable rule excludes `SampleDesc.Count != 1` (EXP1829 C32A).
That rule exists because a presentation allocation is single-sampled, and
a 4x target must not silently become 1x. Every single-sampled 1280x720
BGRA8 target in the trace did get a presentation allocation (bind
`0x103`).

## WINDOWS CONTRACT

- The D3D12 Present DDI (`PFND3D12DDI_PRESENT_0003`,
  `D3D12DDIARG_PRESENT_0001`) passes the source surfaces with DXGI
  present flags. For Blt, the runtime submits the kernel present of the
  returned `hSrcAllocation` on `hContext`.
- D3D9 allows a multisampled back buffer for `D3DSWAPEFFECT_DISCARD`, and
  its present shows the resolved image.
- D3D12TranslationLayer `ImmediateContext::Present` (MIT,
  github.com/microsoft/D3D12TranslationLayer) resolves a multisampled
  source into its own temporary only when the present has a destination.
  EXP1899 had no destination, so the D3D12 driver received the
  multisampled resource. Its receipt has reason 2 with `dst 0`.

## AGX/ASAHI CONTRACT

- Presentation allocations are linear BGRA8/RGBA8 at 1x: the admission
  UMD, the KMD and DWM (C7b, C40).
- AGX renders 2x/4x into twiddled multisampled textures (C32).
- Gallium `blit` from a multisampled source into a single-sampled
  destination is a resolve (u_blitter, Asahi `agx_blit`).

## TRANSLATION

This is the C40 pattern with a sample count:
- A shareable 2x/4x render target (BGRA8/RGBA8/10-bit, one mip and layer,
  runtime resource handle) gets the single-sampled presentation
  allocation, as a 1x target does.
- It also gets its own multisampled Asahi texture of the resource's full
  size (`TextureBytes`). The D3D12 resource (RTV, resolves, copies) is
  that texture.
- `AgxD3d12OwnerPreparePresent` blits that texture into the presentation
  import, a resolve, before the flush for present.
- A 10-bit 4x target resolves and converts in the same blit.
- The present receipt now also carries the source's format, size and
  bytes, so another refused source can be identified from its receipt.

## WHAT IS STILL UNKNOWN

- Whether D3D9On12 gives the 4x back buffer a runtime resource handle.
  Every single-sampled render target in the trace had one; if this one
  does not, the new receipt shows a 0xe1 source with reason 2 again.
- The cost: every shareable MSAA render target of an app now also holds
  one 1x linear allocation. The regression suite and the MSAA steps
  (C32A/C61D) measure it.

## Checkpoint

The D3D9 probe step `AGX_D3D9_ON12_MSAA_PRESENT` (and `_DEFAULT`, the
same path through the `"<>"` route):
- a shown window with a 4x back buffer, three clear+present frames, then
  `TestCooperativeLevel`;
- PASS when each present succeeds and the device is not lost;
- `measure-d3d12-present` reason 0 for the 4x source;
- the front-buffer centre is printed.

Real-app confirmation is Portal 2 reaching its menu.
