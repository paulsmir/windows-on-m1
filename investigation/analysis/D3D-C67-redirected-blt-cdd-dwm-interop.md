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

## Result (EXP1903, 2026-10-23)

With `DriverSupportsCddDwmInterop = 1`:
- the desktop was unchanged and there were no DWM faults;
- the D3D9 MSAA_PRESENT window centre still read the desktop (0x0c0c0c);
- the KMD wrote no `Wom1G2PresentDdi`, so no redirected Blt reached
  `DxgkDdiPresent`.

The cap is not sufficient and is reverted, because no capability is
advertised without a validated effect.

Next discriminator: the DxgKrnl ETW trace (`EXP801DxgBoot` autologger) of
one D3D9 windowed present. It shows how dxgkrnl services the redirected
Blt for this driver: a present history token, a CPU copy from a lockable
source, a staging surface, or a refusal.

## DxgKrnl ETW (EXP1906E, 2026-10-23, package 30.0.1906.0, no C67)

Captured with logman (Microsoft-Windows-DxgKrnl, all keywords) around the D3D9 probe (MSAA_PRESENT, windowed, shown) and decoded with tracerpt. Evidence: `.local/experiments/EXP1906-zls-c69/evidence/EXP1906E/dxg.csv`.

- The probe process (PID 0x72C) issued 6 kernel presents, event 184, with:
  - flags "Blt | RedirectedBlt", hWnd 0xA02EA, source allocation 0x40001200;
  - status 0xC01E0006 = STATUS_GRAPHICS_PRESENT_OCCLUDED ("nothing to present due to desktop occlusion").
- No present-history token (event 171/215) was created for that process. DWM's own presents are REDIRECTED_COMPOSITION / REDIRECTED_FLIP (flip models), and they work.
- No KMD refusal: there were no Wom1G1bFailure (GetStandardAllocationDriverData) receipts.

So the redirected blt reaches dxgkrnl, but dxgkrnl drops it as occluded before any DDI, because the window has no DX redirection target from DWM.

Next discriminator: the same capture in the GPU-hidden/visible-recovery profile (Microsoft Basic Display, where blt-model windows are composed). It gives the expected event sequence for a working redirected blt; the first event that differs names the missing step on our side.

## Basic Display reference (EXP1906E-basic, 2026-10-23)

The same capture with the visible-recovery profile (Microsoft Basic Display):

| | Basic Display | AppleAgx (EXP1906E) |
|---|---|---|
| event 184 status | 0 | 0xC01E0006 OCCLUDED |
| destination allocation | 0x40000900 | 0x0 |
| event 215 PM_REDIRECTED_BLT token | 6 | 0 |

So dxgkrnl normally holds a destination (DWM's redirection surface for the window) and turns the blt into a present-history token that DWM consumes. Under AGX there is no destination. The next step is to find which object provides it: the redirection surface is created by DWM for legacy (GDI/blt) windows. Compare DWM's CreateAllocation/OpenResource events for that hWnd between the two captures.

The probe's front-buffer pixel (0x0c0c0c) is the same on both drivers, so it does not show composition.
