# STRETCH-1: hardware scaling of display planes

Status: implemented offline (contract version 4, receipt version 3), not run
on hardware. Classification: DISPLAY_ONLY (DxgkDdiGetMultiPlaneOverlayCaps,
DxgkDdiCheckMultiPlaneOverlaySupport3, DxgkDdiSetVidPnSourceAddressWith
MultiPlaneOverlay3 and the DXGI group caps of the D3D11 UMD). Owning layer:
the AppleAgx KMD MPO path (`drivers/apple-agx/render-admission/src/
mpo_contract.c`, `mpo_windows.c`, `mpo_layers.c`); the m1n1 broker and DCP
swap builder already carry per-layer scaling and are not changed.

Sources inspected:

- m1n1 `/.local/m1n1-wt-s2-broker-v3` (commit 63c13618, "broker ABI V3, two
  DCP layers per commit"): `src/hv_agx_scanout_broker.c`,
  `src/hv_agx_scanout_broker.h`, `src/hv_agx_scanout_layers.h`,
  `src/dcp_iomfb_present.c`, `src/dcp_iomfb_present.h`.
- Asahi DCP driver: `.local/reference/asahi-linux-dcp/drivers/gpu/drm/apple/
  plane.c`, `iomfb_template.c` (`asahi-linux-asahi` in this worktree has no
  `drivers/gpu/drm/apple`).
- macOS kernelcache strings (`.local/reference/macos-kernelcache-j313-
  15.7.8`): only the separate M2Scaler/MSR block ("8 taps, 64 phases") is
  named; the DCP display pipe's scaler lives in DCP firmware, not in the
  kernelcache. No DCP filter evidence.
- Pinned WDK 26100: `d3dkmddi.h:295-311` (overlay flags), `:368-372`
  (stretch quality), `:406-418` (Check3 return info), `:6326-6343`
  (ATTRIBUTES3), `:6514-6534` (post-composition), `:6670-6725` (overlay caps,
  GETMULTIPLANEOVERLAYCAPS, GETPOSTCOMPOSITIONCAPS); `d3dkmthk.h:1383-1388`,
  `:2419`; `dxgiddi.h:314-328`.
- Microsoft Learn: DXGK_MULTIPLANEOVERLAYCAPS, DXGKARG_GETMULTIPLANEOVERLAYCAPS,
  DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY, DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3,
  DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3, DXGKDDI_GETPOSTCOMPOSITIONCAPS,
  KMTQUERYADAPTERINFOTYPE, DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS, "MPO
  Support".

## WINDOWS CONTRACT

- `DXGKARG_GETMULTIPLANEOVERLAYCAPS` (d3dkmddi.h:6695-6704): MaxStretchFactor
  "the maximum stretch factor that can be applied to a plane",
  MaxShrinkFactor "the maximum shrink factor". The DXGK page does not define
  the scale of the shrink factor. The DXGI DDI definition of the same pair
  (DXGI_DDI_MULTIPLANE_OVERLAY_GROUP_CAPS) does: both are "the ratio of the
  final ... size to the original size" (stretch 2.5 = 100 -> 250; shrink
  0.25 = 100 -> 25) and "it's not guaranteed that this ... factor can be
  applied in all scenarios" (Check3 may still refuse a configuration).
- `DXGK_MULTIPLANEOVERLAYCAPS` (d3dkmddi.h:6670-6693): StretchRGB "the
  hardware supports stretching any plane containing RGB data"; StretchYUV
  same for YUV; BilinearFilter "supports bilinear filtering"; HighFilter
  "supports better than bilinear filtering"; Plane0ForVirtualModeOnly
  "will always apply the stretch factor of plane 0 to the hardware cursor".
  Rotation/flip/Shared/Immediate are unrelated to scaling.
- `DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3` (d3dkmddi.h:6326-6343): SrcRect
  "relative to the source resource", DstRect "relative to the monitor
  resolution", ClipRect "additional clipping ... after the data has been
  stretched" ("apply a common stretch factor as the clipping changes when an
  app occludes part of the DstRect"), StretchQuality, Flags (VerticalFlip,
  HorizontalFlip, StaticCheck).
- `DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY` (d3dkmddi.h:368-372): BILINEAR =
  1 "should perform bilinear filtering. If the hardware lacks enough
  resources to perform bilinear shrinking, ... can use point sampling";
  HIGH = 2 "the highest quality filtering that it supports". No value 0 is
  defined.
- `DxgkDdiCheckMultiPlaneOverlaySupport3`: report whether the configuration
  is supported; `ReturnInfo.FailingPlane` is the 4-bit index of the first
  unsupported plane (d3dkmddi.h:412). Nothing scaling-specific is
  documented beyond the attribute meanings.
- `DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3`: the driver programs
  the planes it was asked for; for scaling that means SrcRect -> DstRect.
- `DxgkDdiGetPostCompositionCaps`: "required for any WDDM 2.2 driver that
  wants to support post composition scaling". Post-composition is a
  transform of the composed output (DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION:
  flags, SrcRect, DstRect, rotation), not per-plane scaling. A driver that
  scales planes individually does not need it; this driver does not
  register it and keeps refusing non-identity post-composition in Check3.
- `KMTQAITYPE_MULTIPLANEOVERLAY_STRETCH_SUPPORT` (d3dkmthk.h:2419,
  D3DKMT_MULTIPLANEOVERLAY_STRETCH_SUPPORT {VidPnSourceId, Update,
  Supported}): "specifies whether the driver supports multiplane overlay
  stretch operations". How dxgkrnl derives `Supported` is not documented.
  There is no DRIVERCAPS field for stretch (d3dkmddi.h:2442-2481).
- DXGI DDI (`dxgiddi.h:314-328`): there is no stretch feature bit, only
  BILINEAR_FILTER (0x80) and HIGH_FILTER (0x100); the group's factors carry
  the capability.

## AGX-ASAHI CONTRACT

Verified in m1n1 (gpu/s2-broker-v3, 63c13618):

| Limit | Source | Notes |
|---|---|---|
| Scaled-source capability = bit 8 | broker.h:28; set by `hv_agx_scanout_broker_init_v2` broker.c:375 | Two layers (bit 9) are offered only on a V2 broker (broker.c:383-387), which always sets bit 8; the V3 layer path (`layer_validate`) does not itself test bit 8. |
| Surface W/H 1..2560 x 1..1600 | broker.c:134-136 (refusal 16) | per axis |
| Stride >= W*4 and multiple of 64 | broker.c:137-138 (17) | |
| Surface size == stride * H | broker.c:139-140 (16) | |
| Source rect non-empty, inside the surface | broker.c:146-148 (19) | integer origin and size |
| Destination >= 32x32 and inside the 2560x1600 panel | broker.c:149-155 (20); MIN_EXTENT broker.h:60 | per axis |
| Scale: dst <= 2*src AND src <= 2*dst | `scale_ok` broker.c:107-111, applied per axis broker.c:156-157 (21); MAX_SCALE broker.h:61 | per axis; shrink allowed down to 1/2 |
| Same limits in the DCP swap builder | `layer_scale_ok` dcp_iomfb_present.c:176-180, applied :198-202 | |
| Per-slot source and destination rects reach the swap | hv_agx_scanout_layers.h:52-55; dcp_iomfb_present.c:251-257 | |
| Slot-0 destination not full panel -> black background | dcp_iomfb_present.c:276-277 | letterbox bars are black |

Asahi (`plane.c`): `drm_atomic_helper_check_plane_state(..., FRAC_16_16(1,
2), FRAC_16_16(4, 1), true, true)` (plane.c:92-95). In DRM the limits are on
src/dst per axis, so this admits up to 2x upscale and up to 4x downscale; the
comment above it (plane.c:81-82) says the opposite ("downscaling to 2x and
upscaling to 4x"). Every blended surface must be >= 32x32 on screen or DCP
crashes (plane.c:102-111). DCP source rects are integers
(`drm_to_dcp_rect_fp`, plane.c:202-207, 327); per-layer src/dst rects go to
the swap unchanged (iomfb_template.c:1366-1367). No restriction on two
scaled layers at once is stated anywhere. No filter is selected or
documented.

The 2x-each-way limit is this project's own conservative intersection of the
Asahi code and its comment, not a measured DCP limit (dcp_iomfb_present.h:
41-44). No hardware run so far has scaled a DCP layer: every two-plane
commit was 1:1.

## TRANSLATION

Caps (`AdmissionMpoCapsFor`, mpo_contract.c; written as IEEE bits):

| Field | Value with broker bit 8 and two layers effective (source 0) | Otherwise | Why truthful |
|---|---|---|---|
| MaxPlanes / MaxRGBPlanes | 2 | 2 effective, else 1 (unchanged) | |
| OverlayCaps.StretchRGB | 1 | 0 | RGB layers scale within the limits below; Check3 refuses the rest |
| OverlayCaps.StretchYUV | 0 | 0 | no YUV planes (MaxYUVPlanes 0) |
| OverlayCaps.BilinearFilter | 0 | 0 | DCP filter unknown (see UNKNOWN) |
| OverlayCaps.HighFilter | 0 | 0 | unknown |
| OverlayCaps.Plane0ForVirtualModeOnly | 0 | 0 | the KMD has no hardware cursor bound to a plane |
| MaxStretchFactor | 2.0 | 1.0 | = broker MAX_SCALE |
| MaxShrinkFactor | 0.5 | 1.0 | = 1 / MAX_SCALE, DXGI convention (final / original) |

The UMD reports the same factors in its DXGI group (2.0 / 0.5, OverlayCaps
RGB only) when the KMD publishes the new adapter feature bit
`AGX_WIN32_DEVICE_FEATURE_MPO_STRETCH` (0x40), set only with
`AdmissionScanoutMpoScaledSource`.

Check3 rule table (contract version 4; numeric reasons stable, receipts
index them):

| Old rule | New rule |
|---|---|
| 10 Scaling: any src size != dst size refused | 10 only when the broker did not advertise bit 8 (`Input->ScalingSupported` 0): identical to version 3 |
| - | 32 ScaleBeyondLimit: dst > 2*src or src > 2*dst on either axis (after 11/12/13, so the factor is computed on valid rects) |
| - | 33 StretchQuality: a scaled plane whose StretchQuality is neither 1 nor 2 |
| - | 34 StretchFilter: a scaled plane asking BILINEAR while the scaler filter is not known to be bilinear or better (today: always, the filter is Unknown). HIGH is accepted: it asks for "the highest quality filtering that it supports". |
| 8 TopNotFullPanel | unchanged: LayerIndex 0 stays full panel, so only LayerIndex 1 is scaled in a pair |
| 12 DestinationTooSmall (< 32) | unchanged, before the factor check |
| 17 Clip (clip must contain the destination) | unchanged: a clip smaller than a scaled destination would need a fractional source crop; DCP source rects are integers |
| one plane (A1.1): `AdmissionMpoEvaluatePrimary`, rects ignored | `AdmissionMpoEvaluateOne`: Src == Dst == full panel -> the same `AdmissionMpoEvaluatePrimary` answer and V2 PRESENT; any other plane -> one layer record: 5 Format, 6 ColorSpace, 35 OnePlaneBlend (blend not 0/1), then the layer rules (10..17, 32..34, 18..21, 25) |
| StretchQuality never examined | examined only for a scaled plane; 1:1 answers are unchanged |

The one-plane change also fixes a latent defect: with rects ignored, a
one-plane flip whose SrcRect/DstRect were not the full panel was accepted
and the V2 PRESENT showed the whole surface on the whole panel.

Set3: the two-plane path is unchanged (`AdmissionMpoBuildCommit` already
wrote SrcOrigin/SrcSize and DstOrigin/DstSize separately, mpo_layers.c);
the scaled plane's rects reach the V3 layer record as given. One plane:
the full-screen primary keeps the V2 PRESENT; any other single plane (or the
removal while slot 1 may show) is evaluated with `AdmissionMpoEvaluateOne`
and sent as one COMMIT_LAYERS record built by `AdmissionMpoBuildRemoval`
(flags 0, its own rects); the broker removes slot 1 on that commit if it was
latched (broker.c `RemoveSlot1`).

Receipts (Wom1MpoReceipt version 3, reasons array 40): `ScalingSupported`,
the last caps answer (`CapsStretchRgb`, `CapsMaxStretchBits`,
`CapsMaxShrinkBits`), Check3 scaled supported/refused counts and the last
scaled plane's reason, StretchQuality, source size, destination size and
per-axis factor x1000; Set3 `ScaledTwoPlaneCommits`,
`OnePlaneRecordCommits`, `ScaledOnePlaneCommits`.

No new registry switch: every path is behind the existing Wom1MpoTwoPlanes
effective predicate.

### WHAT REAL BUG OR INVARIANT WILL THE TESTS CATCH

- A scaled plane answered "supported" whose record the broker refuses
  (beyond 2x on one axis, below 32x32): a refused flip after Check3 said
  yes. Caught by the scale boundary table (at, one pixel beyond, per axis,
  per direction, both plane orders) and by the randomised sweeps
  `TestSweepTableImpliesBroker` / `TestSweepOnePlaneRecord` against the
  broker reference predicate (mutating the limit to 3x fails both).
- Scaling accepted or advertised without broker bit 8; caps factors that
  differ from the table's limit (Windows promised a factor Check3 refuses).
- A bilinear promise the hardware is not known to keep (34; BilinearFilter
  bit 0).
- Any 1:1 answer changing (StretchQuality ignored when unscaled; reason 10
  kept without the capability).
- A non-full-screen single plane taking the V2 PRESENT path.
- The record of a scaled plane losing its separate source/destination
  (cropped origin, size).

## WHAT IS STILL UNKNOWN

1. The DCP scaler's filter (point, bilinear, polyphase). Not in Asahi, m1n1
   or the kernelcache. Until measured, BilinearFilter/HighFilter stay 0 and
   BILINEAR requests on scaled planes are refused (34). If DWM always asks
   BILINEAR, no scaled plane is shown until this is measured.
2. The firmware's real scale limits. Asahi's code admits 2x up / 4x down,
   its comment says 4x up / 2x down; neither is measured here. The broker
   and the caps use 2x both ways.
3. Behaviour at the limit and with two scaled layers (Asahi states no
   restriction; this table never scales LayerIndex 0, so it never asks).
4. Which StretchQuality value DWM sends for scaled planes, and whether it
   sends scaled planes at all with these caps (receipt
   `LastScaledStretchQuality`, Check3 reason counts 32..34).
5. How dxgkrnl derives `KMTQAITYPE_MULTIPLANEOVERLAY_STRETCH_SUPPORT`
   (undocumented; HYPOTHESIS: from the DXGK caps StretchRGB / factors).
6. Whether dxgkrnl interprets the DXGK MaxShrinkFactor with the DXGI
   convention (0.5 = shrink to half). The DXGK page is silent.
7. ClipRect: Microsoft calls it "relative to the DstRect"; the table, as in
   version 3, compares it with DstRect in the same (monitor) space and
   requires it to contain the destination. A dxgkrnl trace with an occluded
   scaled plane would settle the space.

## Missing for a complete production feature

- Measure the DCP filter and set `ADMISSION_MPO_SCALER_FILTER_CURRENT`
  (enables BILINEAR requests and the BilinearFilter bit; HighFilter only if
  it is better than bilinear).
- Measure the firmware's per-axis up/down limits on the panel and raise
  `HV_AGX_SCANOUT_V3_MAX_SCALE` (m1n1 broker + DCP builder) and
  `ADMISSION_MPO_MAX_SCALE` together; caps follow automatically. What to
  measure: one layer at factors 2, 3, 4 up and 1/2, 1/3, 1/4 down per axis,
  swap result and picture, with the m1n1 crash recovery ready.
- Scaled clipping (ClipRect smaller than a scaled destination) would need
  sub-pixel source cropping; DCP takes integer source rects, so it stays
  refused unless DCP is shown to accept fixed-point rects.
- Scaling the LayerIndex 0 (desktop) plane in a pair (rule 8) and a third
  plane: not offered.
- YUV planes and StretchYUV: no YUV scan-out formats in the broker.
- Post-composition scaling (DxgkDdiGetPostCompositionCaps): not registered.
- The HD/FHD full-screen game case additionally depends on DWM choosing
  independent flip with a scaled plane; not observed yet.

## First hardware checkpoint (proposal, not run)

WHY THIS HYPOTHESIS: (1) the broker and DCP builder accept scaled records
within 2x (broker.c:156-157, dcp_iomfb_present.c:198-202) but no scaled
swap has ever run; (2) the filter is unknown, so the KMD will refuse
BILINEAR; (3) the full-screen-game case goes through the one-record path.

Step 0 (m1n1-only, isolates DCP): one COMMIT_LAYERS with one record, a
1280x800 test pattern (1-pixel black/white columns and rows, a 1-pixel grid
every 64 px, a colour ramp) scaled to 2560x1600, from the m1n1 proxy. Look
at the panel: geometry (grid lines at 128 px), and the 1-pixel columns
(doubled sharp pixels = point/nearest; uniform grey = bilinear or
polyphase). Then 1.5x (1706x1066 -> uneven doubling under point sampling).
Failure: swap error, DCP crash (recover with the known-good m1n1) or a
distorted picture.

Step 1 (Windows, after step 0 passes): the STRETCH-1 package with the
two-plane switch; run a windowed D3D11 swap chain with
DXGI_SCALING_STRETCH presenting a 1280x720 buffer into a 1920x1080 client
area, then the same full-screen at 1280x800. Read Wom1MpoReceipt:
ScalingSupported 1, CapsStretchRgb 1, Caps bits 0x40000000/0x3f000000,
Check3ScaledSupported/Refused with LastScaledReason, LastScaledStretchQuality
and the factors, ScaledTwoPlaneCommits / ScaledOnePlaneCommits,
BrokerRefusals 0. Expected if DWM asks HIGH: scaled commits > 0 and the
picture correct; if it asks BILINEAR: reason 34 counted and DWM composes
(unchanged picture) - that result then makes step 0's filter measurement
the gate. Recovery: the existing two-plane-off profile.
