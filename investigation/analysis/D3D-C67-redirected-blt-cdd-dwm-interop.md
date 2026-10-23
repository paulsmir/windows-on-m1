# C67: legacy redirected (blt-model) presentation — CDD-DWM interop

## Why now

Several kinds of windows are never composed by DWM:
- D3D9 windowed games through D3D9On12, e.g. Portal 2 in EXP1901G:
  10816 successful D3D12 Present-DDI Blt presents with `hDstResource`
  NULL, while the 1280x720 popup window shows the desktop through it;
- DXGI blt-model swapchains (EXP1231-D1);
- opengl32 redirection (EXP1231).

The KMD's DWM DDI probe counted 0 `DxgkDdiPresent` Blt calls on any
context (EXP1231-D2). dxgkrnl never asks this KMD to perform a redirected
blt. Flip-model presentation works.

## WINDOWS CONTRACT

Sources: pinned WDK 26100 `d3dkmddi.h` `DXGK_PRESENTATIONCAPS`, Microsoft
Learn `_DXGK_PRESENTATIONCAPS`, and `D3DKMDT_GDISURFACETYPE`.

- `SupportKernelModeCommandBuffer`: GDI hardware acceleration through
  `DxgkDdiRenderKm`. Report it only with a cache-coherent aperture.
  AppleAgx reports 0 (lifecycle.c).
- `DriverSupportsCddDwmInterop` (bit 0x100): "the display miniport driver
  supports Canonical Display Driver (CDD) present operations to texture
  allocations that are created by the user-mode driver for the Desktop
  Windows Manager (DWM) to use". It is ignored when GDI hardware
  acceleration is reported; a driver with GDI acceleration must support
  these presents anyway. AppleAgx never sets it.
- `D3DKMDT_GDISURFACE_TEXTURE`: the DWM redirection surface. It is not
  CPU-visible, is created shared by VidMm, is opened by a UMD as a
  composition texture, and is a render target for DirectX rendering and a
  GDI blt source or destination.
- A Blt present without a destination (`D3DDDICB_PRESENT.hDstAllocation`
  0, D3D9/DXGI blt model to a redirected window) leaves the destination to
  kernel mode: the window's redirection surface.

## AGX CONTRACT

- `AdmissionDdiPresent` routes `Flags.Value == 1` (Blt only) to
  `AdmissionPresentBlt` (present_windows.c). Both paths exist:
  - patch-list (GDI/system) contexts;
  - GPU-VA contexts (`ADMISSION_PRESENT_BLT_GPUVA_VERSION`, G2 HW2b, as a
    compute job).
- Endpoints must be allocations opened on the presenting device
  (`AdmissionPresentOpen`); failures are receipted
  (`AdmissionRecordPresentOpenFailure`, `Wom1G2PresentDdi` for VA app
  contexts).
- GDI-surface standard allocations, including `D3DKMDT_GDISURFACE_TEXTURE`
  and the staging types, are described in allocation_windows.c.

## TRANSLATION

Report `PresentationCaps.DriverSupportsCddDwmInterop = 1`. The
redirection surfaces are then DWM textures that this driver's UMD creates
(the presentation-allocation contract). dxgkrnl can then service a
redirected Blt through `DxgkDdiPresent(Blt, src = app back buffer,
dst = redirection texture)`, which the existing `AdmissionPresentBlt`
performs.

## WHAT IS STILL UNKNOWN

The documentation does not say which DDI traffic the bit changes:
- whether CDD's own GDI redirection moves to `DxgkDdiPresent` into DWM
  textures; that affects every GDI window;
- whether dxgkrnl then sends app Blts to the KMD at all, and with which
  flags and source/destination allocation types.

This is the genuinely undocumented boundary, so the experiment is a
measurement first:
- the KMD's present receipts (`Wom1G2PresentDdi`, present-open failures,
  the DWM DDI probe counts);
- the desktop: explorer windows still drawn, no DWM faults.

## Checkpoint

One variable: the cap bit.
- **Pass:** the Portal 2 / D3D9 probe window shows its frames (screen
  pixel at the window centre) and `Wom1G2PresentDdi` shows Blt calls with
  status 0; the desktop is unchanged.
- **Fail-safe:** a black or garbled desktop → rec<N>h (visible recovery).
  The present receipts then name the refused Blt.
