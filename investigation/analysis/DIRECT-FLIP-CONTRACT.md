# Direct flip of an application buffer: contract (D2-D4 of DWM-COMPOSITION-LEVER.md)

Written 2026-11-09T21:00:21Z, offline, no hardware. Class of every requirement here: FULL GRAPHICS (WDDM 3.x KMD + D3D11/D3D12
UMD). Nothing is taken from the display-only model. Change class: C (contract) for the truthfulness items, O
(optimisation, needs a measured gain) for enabling the flip itself.

Purpose: DWM-COMPOSITION-LEVER.md section 7 left D2-D4 as "one design that needs the Microsoft contract written
down before any code". This note is that contract. It also corrects D3 and D4, which were stated from EXP1038
(2026-09) and are no longer true of the tree.

## 1. What the tree does today (source reading at 6a53a8646)

| Layer | Fact | Where |
|---|---|---|
| KMD caps | `SupportDirectFlip = TRUE`, `FlipCaps.FlipIndependent = 1`, `FlipOnVSyncMmIo = 1`, `MaxQueuedFlipOnVSync = 1`; no `FlipImmediateMmIo` | render-admission/src/lifecycle.c:792-814 |
| KMD segments | flags set: Aperture, Use64KBPages, CpuVisible, PopulatedFromSystemMemory, PopulatedByReservedDDRByFirmware. `DXGK_SEGMENTFLAGS.DirectFlip` is set on no segment | memory_windows.c:23-51,109-110,184-185 |
| KMD allocation | `ScanoutCapable` = not an R105 native resource, `D3DKMDT_GDISURFACE_TEXTURE`, CpuVisible 0, 4 bytes/pixel, format A8R8G8B8 or A8B8G8R8 (broker formats) | allocation_windows.c:478-485 |
| KMD flip | `SetVidPnSourceAddress` -> `AdmissionScanoutQueuePresent`: refuses unless ScanoutCapable, Width/Height/Pitch/Size equal the committed source mode, format has a broker code, address inside the local segment; the PRESENT carries the allocation's own format (rc110) | display.c:1141, scanout_windows.c:1110-1200 |
| KMD MPO | `SetVidPnSourceAddressWithMultiPlaneOverlay3`: PlaneCount <= 1, LayerIndex 0, forwarded to the legacy path with FlipImmediate/FlipOnNextVSync copied. No `CheckMultiPlaneOverlaySupport3`. `SharedPrimaryTransition` is not read anywhere | display.c:1294-1337, driver.c:62 |
| KMD memory | the scanout view is the whole local segment (`AdmissionMemoryRuntimeScanoutView` = local view, PoolBytes = Bytes), so any local-segment address resolves into the registered pool | memory_runtime_windows.c:861-875 |
| UMD storage | every presentable resource (D3D11 shared back buffer, D3D12 swap-chain buffer) is already a linear GDI-surface allocation in segment 2, 4 bytes/pixel, CpuVisible 0, A8R8G8B8 or A8B8G8R8; R10G10B10A2 / BGRX / MSAA render into a separate texture and are blitted into it at present ("Converted") | umd/src/umd.c:196-222,905-931; mesa/winsys/agx_d3d12_windows.cpp:2455-2514 |
| UMD decision (old) | `AdmissionUmdCheckDirectFlipSupport` -> `AdmissionUmdDirectFlipCompatible`: Flags 0, both Displayable 1 (BGRA only), offered fixed-panel geometry, pairwise equal | umd/src/umd.c:1059-1076, umd/src/direct_flip_contract.c |
| UMD decision (live) | the Mesa WDDM 2.0 device table answers `*supported = FALSE` unconditionally and is the table DWM's D3D11 device gets | mesa/d3d11/agx_wddm2_ddi.cpp:677-682,931 |
| m1n1 broker | one layer; BGRA8888 and RGBA8888; with CAP_SCALED_SOURCE (bit 8) any linear source up to the panel is scaled to the panel rectangle. Bench image 83a9cf74 lacks the bit; port `gpu/scaled-on-ansdirect` has it (not run) | hv_agx_scanout_broker.c:8-10,51-54,227; display.c:721 |

Corrections to DWM-COMPOSITION-LEVER.md section 7 (the table there stays as written):

- D3 was "flip-model back buffers are not scanout-capable (RGBA8, tiled/compressed pool, Displayable 0)". Since
  rc110 this is false in the KMD: an RGBA8 linear GPU-only GDI-surface allocation is ScanoutCapable and its PRESENT
  carries RGBA8888. What remains of D3 is only in the UMD contract: `Displayable` is 1 for BGRA alone and
  `AdmissionUmdDirectFlipCompatible` is called with ExpectedFormat A8R8G8B8, so an RGBA8 swap chain can never pass
  the old check either.
- D4 was "KMD flips only the fixed primary geometry". Since rc106 the KMD flips any allocation that has the
  committed source-mode geometry. What remains of D4: one plane, no `CheckMultiPlaneOverlaySupport3`.
- So D2 (the UMD answer) is the only gate between a full-screen application buffer and the panel, plus D1 for
  any size other than 2560x1600.

## 2. WINDOWS CONTRACT

Sources: pinned WDK 26100 (`.local/reference/wdk26100`) and Microsoft Learn. The Learn text was read through a
fetch agent; quotes below are its report, the header lines were read directly.

1. The decision belongs to the D3D11 UMD inside dwm.exe, whatever API the application uses.
   `PFND3D11_1DDI_CHECKDIRECTFLIPSUPPORT(hDevice, hResource1, hResource2, CheckDirectFlipFlags, pSupported)`
   (d3d10umddi.h:2802-2803); `D3DDDI_CHECKDIRECTFLIP_IMMEDIATE = 1` (d3dumddi.h:2076). d3d12umddi.h has no
   direct-flip entry at all (grep: only `D3D12DDI_FORMAT_SUPPORT_DISPLAY`, line 691). Learn: DWM calls it to
   verify that the driver can flip between the application's and DWM's managed primary allocations
   (learn.microsoft.com/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d11_1ddi_checkdirectflipsupport).
2. `DXGK_DRIVERCAPS.SupportDirectFlip` (d3dkmddi.h:2441). Learn: TRUE means the driver supports shared managed
   primary allocations and never lets `DxgkDdiSetVidPnSourceAddress` flip to an allocation that is not compatible;
   the UMD validates the pair through CheckDirectFlipSupport
   (learn.microsoft.com/windows-hardware/drivers/display/direct-flip-of-video-memory). Mandatory for a full
   graphics driver from WDDM 1.2.
3. `DXGK_SEGMENTFLAGS.DirectFlip` (d3dkmddi.h:2576, 0x400). Learn: allocations for direct flip must be pinnable
   and scannable in that segment.
4. `DXGK_SETVIDPNSOURCEADDRESS_FLAGS.SharedPrimaryTransition` (d3dkmddi.h:6227) and the MPO plane twin (6293):
   "transitioning to or away from a shared managed primary allocation". Informational for the driver.
5. `DXGK_FLIPCAPS.FlipIndependent`: Learn: WDDM 1.3 and later drivers must set it to 1.
6. Kernel thunks `D3DKMT_PINDIRECTFLIPRESOURCES` / `UNPIN` (d3dkmthk.h:2595-2607) and
   `D3DKMT_DIRECTFLIP_SUPPORT` (2043): dxgkrnl pins the application's buffers while they are scanned.
7. Geometry. Direct flip needs the application buffer to be a valid primary for the current mode: same size as
   the committed source mode and covering the whole output (Learn page of item 2, as reported; independent of
   that, our KMD refuses any other geometry, section 1). A window smaller than the output can leave
   composition only as a second plane, which is the multiplane-overlay contract
   (`DxgkDdiCheckMultiPlaneOverlaySupport3`, optional).

Consequence for capability truthfulness (AGENTS: "advertise a bit only when its DDI is implemented and describes
current behavior"):

- `SupportDirectFlip = TRUE` describes behaviour the KMD does implement (shared primaries exist; an incompatible
  flip is refused with counters). It is not a false advertisement. A UMD that always answers FALSE is permitted.
- `FlipIndependent = 1` is required, not optional.
- The missing segment flag is the one inconsistency: the cap says direct-flip allocations are supported and no
  segment says where they may live. Whether dxgkrnl needs the flag before it pins a buffer is unknown (U2).

## 3. AGX/ASAHI CONTRACT

Display on this machine is DCP, driven by m1n1 through the iBoot DCP interface, not by the AGX firmware.

1. The broker scans one linear 32-bit surface from the registered pool; formats BGRA8888 and RGBA8888
   (hv_agx_scanout_broker.c:8-10). A PRESENT names an offset in the pool, a format code and, with
   CAP_SCALED_SOURCE, a source size that DCP scales to the panel rectangle.
2. One swap is latched per vertical blank; a second request while one is pending is answered busy
   (`AppleAgxFixedPanelPresentPending` -> STATUS_DEVICE_BUSY, scanout_windows.c:1095-1099). There is no tearing
   path.
3. The iBoot DCP interface addresses layers by id (`dcp_ib_swap_set_layer(iboot, layer_id, layer, src_rect,
   dst_rect)`, m1n1 src/dcp_iboot.h:139-140). The broker uses one. A second layer is not implemented, not
   measured. Hypothesis only: it could carry an overlay plane.
4. AGX renders directly into the linear presentable allocation (it is the render target today, both APIs). No
   copy is needed to make a buffer scannable; the constraint is linear layout, which presentable resources
   already have.

## 4. TRANSLATION

Two different paths; they must not be mixed.

P1, full-screen buffer equal to the committed mode (exclusive or borderless full screen):

| Windows side | Translation | DCP side |
|---|---|---|
| DWM: CheckDirectFlipSupport(app buffer, DWM buffer, flags) | TRUE iff both resources are presentable records (linear, segment 2, 4 bytes/pixel, CpuVisible 0, format A8R8G8B8 or A8B8G8R8), their Width/Height/Pitch/Size are equal, neither carries ADMISSION_PRESENT_RESOURCE_CONVERTED, and flags are 0. Formats need not be equal: the KMD sends each flip's own format | none |
| dxgkrnl pins the buffers | they live in the local segment = registered pool; flag the segment DirectFlip | pool already registered at start |
| SetVidPnSourceAddress / MPO3 plane 0 with the app allocation | unchanged: ScanoutCapable + committed geometry + broker format, PRESENT with the allocation's format | swap of one layer, latched at vblank |
| SharedPrimaryTransition | count it in the display receipt; no action | none |
| flags = D3DDDI_CHECKDIRECTFLIP_IMMEDIATE | FALSE: no tearing path exists (3.2) and FlipImmediateMmIo is not advertised | none |

This is a rule over data both processes already have: the opened resource carries the creator's
`ADMISSION_ALLOCATION_DESCRIPTION` and resource-data flags (umd.c:880-931). It replaces
`AdmissionUmdDirectFlipCompatible`'s BGRA-only / Displayable rule, which predates rc110; that function stays for
its other callers (ResourceIsExact, RotateResourceIdentities) until they are reviewed separately.

P2, a window smaller than the output (GravityMark HD windowed, the 0.85 ms DWM row): direct flip cannot apply.
It needs a second DCP layer in the broker plus `CheckMultiPlaneOverlaySupport3` with a stretched plane. Not
designed here; parked until P1 is measured, because P1 shows what removing the DWM job is worth.

ATOMIC CONTRACT for the P1 package: (a) segment `DirectFlip` flag on the local segment, (b) the UMD rule above in
the Mesa WDDM 2.0 table. They are invalid separately: Microsoft documents SupportDirectFlip, the segment flag and
CheckDirectFlipSupport as one feature (the Learn page of 2.2 lists them together), and (b) alone would answer TRUE
for buffers that no segment declares pinnable for direct flip. `SupportDirectFlip` itself does not change.

Deterministic part, RED/GREEN applies: the UMD rule is a pure function. The invariant its test catches: UMD says
TRUE for a pair the KMD's `AdmissionScanoutQueuePresent` would refuse (a TRUE followed by a refused flip is a
black frame or a failed present). The test must drive both predicates from one table of descriptions.

## 5. WHAT IS STILL UNKNOWN

- U1 (dxgkrnl ordering). Whether a sync-interval-0 full-screen application is offered direct flip without the
  IMMEDIATE flag when the driver lacks FlipImmediateMmIo, or stays composed. GravityMark's benchmark presents with
  interval 0, so this decides whether P1 gives the benchmark anything.
- U2 (dxgkrnl ordering). Whether dxgkrnl refuses to pin direct-flip buffers when no segment has the DirectFlip
  flag, i.e. whether (a) is required or only declarative.
- U3 (hardware, already queued). Whether exclusive full screen already bypasses DWM with the UMD answering
  FALSE. The rc110 comment in scanout_windows.c says dxgkrnl flips an exclusive full-screen D3D12 primary in
  RGBA8; EXP2301 could not test it because the mode set failed (D1). The repeat of F0 answers it with no package.
- U4 (hardware). The gain. Unmeasured; EXP2301 arm B was invalid.
- U5. GravityMark's swap-chain format and whether its buffers are Converted, read from the D3D12 UMD log of the
  F0 repeat, not guessed.

U1, U2 and U4 are dxgkrnl ordering or hardware behaviour, so a hardware discriminator is allowed for them; U3
and U5 are answered by the F0 repeat that is already queued.

## 6. Order of work and checkpoints

1. D1 checkpoint (queued with fork 6): m1n1 `gpu/scaled-on-ansdirect` image sha256 b87eda82...9bfb, expect
   BrokerCaps 0x1ff, ScaledModes 1, 11 modes. Recovery: bench m1n1 83a9cf74.
2. F0 repeat, no package change: GravityMark HD windowed vs full screen at a real 1280x720 mode, same boot.
   Receipts: ETW Present_Info mode per GravityMark present, dwm Flip count, Wom1DisplayModes
   LastSourceAddressFormat/Width/Height and LayoutRefusals/GeometryRefusals/FormatRefusals, gm err log and
   present size first (memory: fullscreen arm validity). Outcomes: (i) full screen already flips the app buffer
   and FPS rises -> P1 needs no UMD change for exclusive mode, measure only; (ii) still RedirectedFlip -> build the
   P1 package; (iii) mode set fails again -> D1 not fixed, stop.
3. P1 package only on outcome (ii): the atomic pair of section 4, one experiment, same-boot ABBA against the
   production package, failure criterion = any Layout/Geometry/Format refusal after a TRUE answer, any DWM fault,
   or no change of Present_Info mode.
4. P2 design only if step 2 or 3 shows a gain worth a second DCP layer.
