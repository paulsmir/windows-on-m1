#include "apple_agx_fixed_panel.h"
#include "apple_agx_fixed_panel_modes.h"

#define APPLE_AGX_FIXED_PANEL_NULL ((void *)0)

static APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelMapScanoutResult(
    APPLE_AGX_SCANOUT_RESULT Result) {
  switch (Result) {
  case AppleAgxScanoutOk:
    return AppleAgxFixedPanelOk;
  case AppleAgxScanoutInvalidArgument:
  case AppleAgxScanoutNotQualified:
  case AppleAgxScanoutSequenceExhausted:
    return AppleAgxFixedPanelInvalidState;
  case AppleAgxScanoutTimeout:
  case AppleAgxScanoutPollLimit:
  case AppleAgxScanoutClockRegression:
    return AppleAgxFixedPanelTimeout;
  case AppleAgxScanoutNotQuiesced:
    return AppleAgxFixedPanelNotQuiesced;
  case AppleAgxScanoutBusy:
    return AppleAgxFixedPanelPresentPending;
  case AppleAgxScanoutTransportFailed:
    return AppleAgxFixedPanelTransportFailed;
  default:
    return AppleAgxFixedPanelBrokerRejected;
  }
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelInitialize(
    APPLE_AGX_FIXED_PANEL *Panel, const APPLE_AGX_SCANOUT_IO *Io,
    APPLE_AGX_SCANOUT_U64 PoolIpa, APPLE_AGX_SCANOUT_U64 PoolBytes,
    APPLE_AGX_SCANOUT_U64 FirstSequence, APPLE_AGX_SCANOUT_U32 MaxPolls) {
  APPLE_AGX_SCANOUT_RESULT result;

  if (Panel == APPLE_AGX_FIXED_PANEL_NULL ||
      Io == APPLE_AGX_FIXED_PANEL_NULL || PoolIpa == 0ULL ||
      (PoolIpa & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      PoolBytes < APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      PoolBytes > APPLE_AGX_SCANOUT_J313_POOL_MAX_SIZE ||
      (PoolBytes & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL)
    return AppleAgxFixedPanelInvalidArgument;

  Panel->PoolIpa = PoolIpa;
  Panel->PoolBytes = PoolBytes;
  Panel->ActiveOffset = 0ULL;
  Panel->LastSwapId = 0u;
  Panel->Started = APPLE_AGX_SCANOUT_FALSE;
  Panel->Committed = APPLE_AGX_SCANOUT_FALSE;
  Panel->Visible = APPLE_AGX_SCANOUT_TRUE;
  Panel->PresentConsumed = APPLE_AGX_SCANOUT_FALSE;
  Panel->DeferredOffset = 0ULL;
  Panel->DeferredValid = APPLE_AGX_SCANOUT_FALSE;
  Panel->Ownership = AppleAgxFixedPanelUnregistered;
  Panel->RequestTwoLayers = APPLE_AGX_SCANOUT_FALSE;
  result = AppleAgxScanoutInitialize(&Panel->Scanout, Io, FirstSequence,
                                     MaxPolls);
  return AppleAgxFixedPanelMapScanoutResult(result);
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelStart(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 DeadlineMs) {
  APPLE_AGX_SCANOUT_RESULT result;

  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || Panel->Started)
    return AppleAgxFixedPanelInvalidState;
  result = AppleAgxScanoutQualify(&Panel->Scanout);
  if (result != AppleAgxScanoutOk)
    return AppleAgxFixedPanelMapScanoutResult(result);
  /* S2 (broker report steps 1-2): negotiate V3 while UNREGISTERED, before
   * REGISTER_POOL, and only when asked and advertised.  A refused or
   * ignored negotiation leaves the panel on V2, exactly as without it. */
  Panel->Scanout.TwoLayers = APPLE_AGX_SCANOUT_FALSE;
  if (Panel->RequestTwoLayers &&
      AppleAgxScanoutTwoLayersAdvertised(&Panel->Scanout))
    (void)AppleAgxScanoutNegotiateTwoLayers(&Panel->Scanout);
  result = AppleAgxScanoutRegisterPool(&Panel->Scanout, Panel->PoolIpa,
                                       Panel->PoolBytes, DeadlineMs);
  if (result != AppleAgxScanoutOk) {
    APPLE_AGX_SCANOUT_U32 state;
    /* REGISTER was submitted.  Only an explicit UNREGISTERED state proves
     * that the caller may release the DMA-visible pool and broker mapping. */
    Panel->Ownership = AppleAgxFixedPanelOwnershipUnknown;
    if (Panel->Scanout.Io.Read32(
            Panel->Scanout.Io.Context,
            APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_STATE,
            &state) &&
        state == APPLE_AGX_SCANOUT_STATE_UNREGISTERED)
      Panel->Ownership = AppleAgxFixedPanelUnregistered;
    return AppleAgxFixedPanelMapScanoutResult(result);
  }
  Panel->Ownership = AppleAgxFixedPanelRegistered;
  Panel->Started = APPLE_AGX_SCANOUT_TRUE;
  Panel->Committed = APPLE_AGX_SCANOUT_FALSE;
  Panel->Visible = APPLE_AGX_SCANOUT_TRUE;
  Panel->PresentConsumed = APPLE_AGX_SCANOUT_FALSE;
  return AppleAgxFixedPanelOk;
}

/* rc106: the laptop source modes come from the shared table
 * (apple_agx_fixed_panel_modes.h), which the UMD's primary and direct-flip
 * gates use as well; index 0 is the native J313 mode. */
APPLE_AGX_SCANOUT_U32 AppleAgxFixedPanelSourceModeCount(
    APPLE_AGX_SCANOUT_BOOL Scaled) {
  return Scaled ? APPLE_AGX_FIXED_PANEL_MODE_COUNT : 1u;
}

APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelSourceModeAt(
    APPLE_AGX_SCANOUT_U32 Index, APPLE_AGX_SCANOUT_BOOL Scaled,
    APPLE_AGX_FIXED_PANEL_SOURCE_MODE *Mode) {
  APPLE_AGX_SCANOUT_U32 width;
  APPLE_AGX_SCANOUT_U32 height;
  if (Mode == APPLE_AGX_FIXED_PANEL_NULL ||
      Index >= AppleAgxFixedPanelSourceModeCount(Scaled))
    return APPLE_AGX_SCANOUT_FALSE;
  width = AppleAgxFixedPanelModeSizes[Index][0];
  height = AppleAgxFixedPanelModeSizes[Index][1];
  Mode->Width = width;
  Mode->Height = height;
  Mode->Stride = width * 4u;
  Mode->SurfaceBytes = (APPLE_AGX_SCANOUT_U64)width * 4u * height;
  return APPLE_AGX_SCANOUT_TRUE;
}

APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelSourceModeFind(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_SCANOUT_U32 Stride, APPLE_AGX_SCANOUT_BOOL Scaled,
    APPLE_AGX_FIXED_PANEL_SOURCE_MODE *Mode) {
  APPLE_AGX_FIXED_PANEL_SOURCE_MODE candidate = {0u, 0u, 0u, 0ULL};
  APPLE_AGX_SCANOUT_U32 index;
  for (index = 0u; AppleAgxFixedPanelSourceModeAt(index, Scaled, &candidate);
       ++index) {
    if (candidate.Width == Width && candidate.Height == Height &&
        candidate.Stride == Stride) {
      if (Mode != APPLE_AGX_FIXED_PANEL_NULL)
        *Mode = candidate;
      return APPLE_AGX_SCANOUT_TRUE;
    }
  }
  return APPLE_AGX_SCANOUT_FALSE;
}

APPLE_AGX_SCANOUT_U32 AppleAgxFixedPanelScalingSupport(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_SCANOUT_BOOL Scaled) {
  if (!AppleAgxFixedPanelSourceModeFind(Width, Height, Width * 4u, Scaled,
                                        APPLE_AGX_FIXED_PANEL_NULL))
    return 0u;
  if (Width == APPLE_AGX_SCANOUT_J313_WIDTH &&
      Height == APPLE_AGX_SCANOUT_J313_HEIGHT)
    return APPLE_AGX_FIXED_PANEL_SCALING_BIT(
        AppleAgxFixedPanelScalingIdentity);
  return APPLE_AGX_FIXED_PANEL_SCALING_BIT(
             AppleAgxFixedPanelScalingCentered) |
         APPLE_AGX_FIXED_PANEL_SCALING_BIT(
             AppleAgxFixedPanelScalingStretched) |
         APPLE_AGX_FIXED_PANEL_SCALING_BIT(
             AppleAgxFixedPanelScalingAspectRatioCenteredMax);
}

APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelResolveScaling(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_SCANOUT_BOOL Scaled, APPLE_AGX_SCANOUT_U32 Requested,
    APPLE_AGX_FIXED_PANEL_SCALING *Resolved) {
  APPLE_AGX_SCANOUT_U32 support =
      AppleAgxFixedPanelScalingSupport(Width, Height, Scaled);
  APPLE_AGX_FIXED_PANEL_SCALING scaling = AppleAgxFixedPanelScalingIdentity;
  if (Resolved == APPLE_AGX_FIXED_PANEL_NULL || support == 0u)
    return APPLE_AGX_SCANOUT_FALSE;
  APPLE_AGX_SCANOUT_BOOL native =
      (support & APPLE_AGX_FIXED_PANEL_SCALING_BIT(
                     AppleAgxFixedPanelScalingIdentity)) != 0u
          ? APPLE_AGX_SCANOUT_TRUE
          : APPLE_AGX_SCANOUT_FALSE;
  if (Requested == 0u || Requested == 254u || Requested == 255u)
    scaling = native ? AppleAgxFixedPanelScalingIdentity
                     : AppleAgxFixedPanelScalingAspectRatioCenteredMax;
  else if (Requested >= (APPLE_AGX_SCANOUT_U32)AppleAgxFixedPanelScalingIdentity &&
           Requested <= (APPLE_AGX_SCANOUT_U32)
                            AppleAgxFixedPanelScalingAspectRatioCenteredMax)
    scaling = native ? AppleAgxFixedPanelScalingIdentity
                     : (APPLE_AGX_FIXED_PANEL_SCALING)Requested;
  else
    return APPLE_AGX_SCANOUT_FALSE;
  if ((support & APPLE_AGX_FIXED_PANEL_SCALING_BIT(scaling)) == 0u)
    return APPLE_AGX_SCANOUT_FALSE;
  *Resolved = scaling;
  return APPLE_AGX_SCANOUT_TRUE;
}

APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelDestination(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_FIXED_PANEL_SCALING Scaling,
    APPLE_AGX_FIXED_PANEL_RECT *Rect) {
  const APPLE_AGX_SCANOUT_U64 panel_width = APPLE_AGX_SCANOUT_J313_WIDTH;
  const APPLE_AGX_SCANOUT_U64 panel_height = APPLE_AGX_SCANOUT_J313_HEIGHT;
  APPLE_AGX_SCANOUT_U64 width = Width;
  APPLE_AGX_SCANOUT_U64 height = Height;
  if (Rect == APPLE_AGX_FIXED_PANEL_NULL || Width == 0u || Height == 0u ||
      width > panel_width || height > panel_height)
    return APPLE_AGX_SCANOUT_FALSE;
  switch (Scaling) {
  case AppleAgxFixedPanelScalingIdentity:
    if (width != panel_width || height != panel_height)
      return APPLE_AGX_SCANOUT_FALSE;
    break;
  case AppleAgxFixedPanelScalingStretched:
    width = panel_width;
    height = panel_height;
    break;
  case AppleAgxFixedPanelScalingCentered:
    break;
  case AppleAgxFixedPanelScalingAspectRatioCenteredMax:
    /* Largest rectangle with the source aspect ratio, rounded to the
     * nearest pixel and centred (letterbox or pillarbox bars). */
    if (width * panel_height >= height * panel_width) {
      height = (height * panel_width + width / 2u) / width;
      width = panel_width;
    } else {
      width = (width * panel_height + height / 2u) / height;
      height = panel_height;
    }
    break;
  default:
    return APPLE_AGX_SCANOUT_FALSE;
  }
  Rect->Width = (APPLE_AGX_SCANOUT_U32)width;
  Rect->Height = (APPLE_AGX_SCANOUT_U32)height;
  Rect->X = (APPLE_AGX_SCANOUT_U32)((panel_width - width) / 2u);
  Rect->Y = (APPLE_AGX_SCANOUT_U32)((panel_height - height) / 2u);
  return APPLE_AGX_SCANOUT_TRUE;
}

APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelScaledSourceSupported(
    const APPLE_AGX_FIXED_PANEL *Panel) {
  return Panel != APPLE_AGX_FIXED_PANEL_NULL && Panel->Started &&
                 Panel->Scanout.AbiVersion ==
                     APPLE_AGX_SCANOUT_ABI_VERSION_V2 &&
                 (Panel->Scanout.Capabilities &
                  APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE) != 0u
             ? APPLE_AGX_SCANOUT_TRUE
             : APPLE_AGX_SCANOUT_FALSE;
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelCommitMode(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_U32 TargetId, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Stride,
    APPLE_AGX_SCANOUT_U32 Format, APPLE_AGX_FIXED_PANEL_SCALING Scaling) {
  APPLE_AGX_FIXED_PANEL_SOURCE_MODE mode = {0u, 0u, 0u, 0ULL};
  APPLE_AGX_FIXED_PANEL_RECT rect = {0u, 0u, 0u, 0u};
  APPLE_AGX_SCANOUT_BOOL scaled;
  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || !Panel->Started)
    return AppleAgxFixedPanelInvalidState;
  if (SourceId != APPLE_AGX_FIXED_PANEL_SOURCE_ID ||
      TargetId != APPLE_AGX_FIXED_PANEL_TARGET_ID)
    return AppleAgxFixedPanelInvalidTopology;
  scaled = AppleAgxFixedPanelScaledSourceSupported(Panel);
  if ((APPLE_AGX_SCANOUT_U32)Scaling <
          (APPLE_AGX_SCANOUT_U32)AppleAgxFixedPanelScalingIdentity ||
      (APPLE_AGX_SCANOUT_U32)Scaling >
          (APPLE_AGX_SCANOUT_U32)AppleAgxFixedPanelScalingAspectRatioCenteredMax ||
      !AppleAgxFixedPanelSourceModeFind(Width, Height, Stride, scaled,
                                        &mode) ||
      (AppleAgxFixedPanelScalingSupport(Width, Height, scaled) &
       APPLE_AGX_FIXED_PANEL_SCALING_BIT(Scaling)) == 0u ||
      !AppleAgxFixedPanelDestination(Width, Height, Scaling, &rect) ||
      (Format != APPLE_AGX_SCANOUT_FORMAT_BGRA8888 &&
       (Format != APPLE_AGX_SCANOUT_FORMAT_RGBA8888 ||
        (Panel->Scanout.Capabilities & APPLE_AGX_SCANOUT_CAP_RGBA8888) == 0u)))
    return AppleAgxFixedPanelInvalidMode;
  Panel->Scanout.PresentFormat = Format;
  Panel->Scanout.PresentWidth = mode.Width;
  Panel->Scanout.PresentHeight = mode.Height;
  Panel->Scanout.PresentStride = mode.Stride;
  Panel->Scanout.PresentSurfaceBytes = mode.SurfaceBytes;
  Panel->Scanout.PresentDestOrigin = rect.X | (rect.Y << 16u);
  Panel->Scanout.PresentDestSize = rect.Width | (rect.Height << 16u);
  Panel->Committed = APPLE_AGX_SCANOUT_TRUE;
  return AppleAgxFixedPanelOk;
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelSetPresentFormat(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 Format) {
  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || !Panel->Started ||
      !Panel->Committed)
    return AppleAgxFixedPanelInvalidState;
  if (Format != APPLE_AGX_SCANOUT_FORMAT_BGRA8888 &&
      (Format != APPLE_AGX_SCANOUT_FORMAT_RGBA8888 ||
       (Panel->Scanout.Capabilities & APPLE_AGX_SCANOUT_CAP_RGBA8888) == 0u))
    return AppleAgxFixedPanelInvalidMode;
  Panel->Scanout.PresentFormat = Format;
  return AppleAgxFixedPanelOk;
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelCommit(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_U32 TargetId, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Stride,
    APPLE_AGX_SCANOUT_U32 Format) {
  return AppleAgxFixedPanelCommitMode(Panel, SourceId, TargetId, Width,
                                      Height, Stride, Format,
                                      AppleAgxFixedPanelScalingIdentity);
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelSetVisible(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_BOOL Visible) {
  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || !Panel->Started)
    return AppleAgxFixedPanelInvalidState;
  if (SourceId != APPLE_AGX_FIXED_PANEL_SOURCE_ID)
    return AppleAgxFixedPanelInvalidTopology;
  Panel->Visible = Visible ? APPLE_AGX_SCANOUT_TRUE
                           : APPLE_AGX_SCANOUT_FALSE;
  return AppleAgxFixedPanelOk;
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelPresent(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SegmentId,
    APPLE_AGX_SCANOUT_U64 SegmentOffset,
    APPLE_AGX_SCANOUT_U64 DeadlineMs, APPLE_AGX_SCANOUT_U32 *SwapId) {
  APPLE_AGX_SCANOUT_U64 offset;
  APPLE_AGX_SCANOUT_RESULT result;

  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || SwapId == 0 ||
      !Panel->Started || !Panel->Committed)
    return AppleAgxFixedPanelInvalidState;
  *SwapId = 0u;
  if (!Panel->Visible)
    return AppleAgxFixedPanelNotVisible;
  if (SegmentId != APPLE_AGX_FIXED_PANEL_SEGMENT_ID)
    return AppleAgxFixedPanelInvalidSegment;
  /* DXGKARG_SETVIDPNSOURCEADDRESS.PrimaryAddress is already an address
   * within PrimarySegment, not an absolute GPU VA. */
  offset = SegmentOffset;
  if ((offset & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      offset > Panel->PoolBytes - APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
    return AppleAgxFixedPanelInvalidSurface;
  /* ABI v1 proves APPLIED, not display latch/vblank.  It is therefore safe
   * only for the initial fixed scanout handoff, not reusable WDDM flips. */
  if (Panel->PresentConsumed)
    return AppleAgxFixedPanelSinglePresentOnly;
  result = AppleAgxScanoutPresentApplied(&Panel->Scanout, offset, DeadlineMs,
                                         SwapId);
  if (result != AppleAgxScanoutOk)
    return AppleAgxFixedPanelMapScanoutResult(result);
  Panel->ActiveOffset = offset;
  Panel->LastSwapId = *SwapId;
  Panel->PresentConsumed = APPLE_AGX_SCANOUT_TRUE;
  return AppleAgxFixedPanelOk;
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelQueuePresent(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SegmentId,
    APPLE_AGX_SCANOUT_U64 SegmentOffset,
    APPLE_AGX_SCANOUT_U64 *Sequence) {
  APPLE_AGX_SCANOUT_RESULT result;

  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || Sequence == 0 ||
      !Panel->Started || !Panel->Committed)
    return AppleAgxFixedPanelInvalidState;
  *Sequence = 0ULL;
  if (SegmentId != APPLE_AGX_FIXED_PANEL_SEGMENT_ID)
    return AppleAgxFixedPanelInvalidSegment;
  if ((SegmentOffset & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      Panel->Scanout.PresentSurfaceBytes == 0ULL ||
      Panel->PoolBytes < Panel->Scanout.PresentSurfaceBytes ||
      SegmentOffset > Panel->PoolBytes - Panel->Scanout.PresentSurfaceBytes)
    return AppleAgxFixedPanelInvalidSurface;
  /* A WDDM mode set programs the new primary while the source is hidden and
   * shows it on SetVidPnSourceVisibility(TRUE); keep only the latest one. */
  if (!Panel->Visible) {
    Panel->DeferredOffset = SegmentOffset;
    Panel->DeferredValid = APPLE_AGX_SCANOUT_TRUE;
    return AppleAgxFixedPanelDeferred;
  }
  Panel->DeferredValid = APPLE_AGX_SCANOUT_FALSE;

  result = AppleAgxScanoutQueuePresent(&Panel->Scanout, SegmentOffset,
                                       Sequence);
  return AppleAgxFixedPanelMapScanoutResult(result);
}

APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelTakeDeferred(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 *SegmentOffset) {
  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || SegmentOffset == 0 ||
      !Panel->Visible || !Panel->DeferredValid)
    return APPLE_AGX_SCANOUT_FALSE;
  *SegmentOffset = Panel->DeferredOffset;
  Panel->DeferredOffset = 0ULL;
  Panel->DeferredValid = APPLE_AGX_SCANOUT_FALSE;
  return APPLE_AGX_SCANOUT_TRUE;
}

APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelTwoLayers(
    const APPLE_AGX_FIXED_PANEL *Panel) {
  return Panel != APPLE_AGX_FIXED_PANEL_NULL && Panel->Started &&
                 Panel->Scanout.TwoLayers
             ? APPLE_AGX_SCANOUT_TRUE
             : APPLE_AGX_SCANOUT_FALSE;
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelQueueCommitLayers(
    APPLE_AGX_FIXED_PANEL *Panel, const APPLE_AGX_SCANOUT_LAYER *Layers,
    APPLE_AGX_SCANOUT_U32 Count, APPLE_AGX_SCANOUT_U64 *Sequence) {
  APPLE_AGX_SCANOUT_RESULT result;

  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || Sequence == 0 ||
      !Panel->Started || !Panel->Committed)
    return AppleAgxFixedPanelInvalidState;
  *Sequence = 0ULL;
  if (!Panel->Visible)
    return AppleAgxFixedPanelNotVisible;
  result = AppleAgxScanoutQueueCommitLayers(&Panel->Scanout, Layers, Count,
                                            Sequence);
  return AppleAgxFixedPanelMapScanoutResult(result);
}

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelStop(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 DeadlineMs) {
  APPLE_AGX_SCANOUT_RESULT result;

  if (Panel == APPLE_AGX_FIXED_PANEL_NULL ||
      Panel->Ownership != AppleAgxFixedPanelRegistered)
    return AppleAgxFixedPanelInvalidState;
  result = AppleAgxScanoutRelease(&Panel->Scanout, DeadlineMs);
  if (result != AppleAgxScanoutOk)
    return AppleAgxFixedPanelMapScanoutResult(result);
  Panel->Started = APPLE_AGX_SCANOUT_FALSE;
  Panel->Committed = APPLE_AGX_SCANOUT_FALSE;
  Panel->Visible = APPLE_AGX_SCANOUT_FALSE;
  Panel->ActiveOffset = 0ULL;
  Panel->LastSwapId = 0u;
  Panel->PresentConsumed = APPLE_AGX_SCANOUT_FALSE;
  Panel->DeferredOffset = 0ULL;
  Panel->DeferredValid = APPLE_AGX_SCANOUT_FALSE;
  Panel->Ownership = AppleAgxFixedPanelUnregistered;
  return AppleAgxFixedPanelOk;
}
