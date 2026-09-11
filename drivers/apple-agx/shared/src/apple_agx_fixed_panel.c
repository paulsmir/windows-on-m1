#include "apple_agx_fixed_panel.h"

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
    APPLE_AGX_SCANOUT_U64 PoolIpa,
    APPLE_AGX_SCANOUT_U64 FirstSequence, APPLE_AGX_SCANOUT_U32 MaxPolls) {
  APPLE_AGX_SCANOUT_RESULT result;

  if (Panel == APPLE_AGX_FIXED_PANEL_NULL ||
      Io == APPLE_AGX_FIXED_PANEL_NULL || PoolIpa == 0ULL ||
      (PoolIpa & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL)
    return AppleAgxFixedPanelInvalidArgument;

  Panel->PoolIpa = PoolIpa;
  Panel->ActiveOffset = 0ULL;
  Panel->LastSwapId = 0u;
  Panel->Started = APPLE_AGX_SCANOUT_FALSE;
  Panel->Committed = APPLE_AGX_SCANOUT_FALSE;
  Panel->Visible = APPLE_AGX_SCANOUT_TRUE;
  Panel->PresentConsumed = APPLE_AGX_SCANOUT_FALSE;
  Panel->Ownership = AppleAgxFixedPanelUnregistered;
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
  result = AppleAgxScanoutRegisterPool(&Panel->Scanout, Panel->PoolIpa,
                                       DeadlineMs);
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

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelCommit(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_U32 TargetId, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Stride,
    APPLE_AGX_SCANOUT_U32 Format) {
  if (Panel == APPLE_AGX_FIXED_PANEL_NULL || !Panel->Started)
    return AppleAgxFixedPanelInvalidState;
  if (SourceId != APPLE_AGX_FIXED_PANEL_SOURCE_ID ||
      TargetId != APPLE_AGX_FIXED_PANEL_TARGET_ID)
    return AppleAgxFixedPanelInvalidTopology;
  if (Width != APPLE_AGX_SCANOUT_J313_WIDTH ||
      Height != APPLE_AGX_SCANOUT_J313_HEIGHT ||
      Stride != APPLE_AGX_SCANOUT_J313_STRIDE ||
      Format != APPLE_AGX_SCANOUT_FORMAT_BGRA8888)
    return AppleAgxFixedPanelInvalidMode;
  Panel->Committed = APPLE_AGX_SCANOUT_TRUE;
  return AppleAgxFixedPanelOk;
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
      offset > APPLE_AGX_SCANOUT_J313_POOL_SIZE -
                   APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
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
  if (!Panel->Visible)
    return AppleAgxFixedPanelNotVisible;
  if (SegmentId != APPLE_AGX_FIXED_PANEL_SEGMENT_ID)
    return AppleAgxFixedPanelInvalidSegment;
  if ((SegmentOffset & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      SegmentOffset > APPLE_AGX_SCANOUT_J313_POOL_SIZE -
                          APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
    return AppleAgxFixedPanelInvalidSurface;

  result = AppleAgxScanoutQueuePresent(&Panel->Scanout, SegmentOffset,
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
  Panel->Ownership = AppleAgxFixedPanelUnregistered;
  return AppleAgxFixedPanelOk;
}
