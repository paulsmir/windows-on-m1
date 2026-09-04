#ifndef APPLE_AGX_FIXED_PANEL_H
#define APPLE_AGX_FIXED_PANEL_H

#include "apple_agx_scanout.h"

#define APPLE_AGX_FIXED_PANEL_SOURCE_ID 0u
#define APPLE_AGX_FIXED_PANEL_TARGET_ID 0u
#define APPLE_AGX_FIXED_PANEL_SEGMENT_ID 2u

typedef enum _APPLE_AGX_FIXED_PANEL_RESULT {
  AppleAgxFixedPanelOk = 0,
  AppleAgxFixedPanelInvalidArgument,
  AppleAgxFixedPanelInvalidState,
  AppleAgxFixedPanelInvalidTopology,
  AppleAgxFixedPanelInvalidMode,
  AppleAgxFixedPanelInvalidSegment,
  AppleAgxFixedPanelInvalidSurface,
  AppleAgxFixedPanelNotVisible,
  AppleAgxFixedPanelTransportFailed,
  AppleAgxFixedPanelTimeout,
  AppleAgxFixedPanelBrokerRejected,
  AppleAgxFixedPanelNotQuiesced,
  AppleAgxFixedPanelOwnershipUncertain,
  AppleAgxFixedPanelSinglePresentOnly,
  AppleAgxFixedPanelPresentPending,
} APPLE_AGX_FIXED_PANEL_RESULT;

typedef enum _APPLE_AGX_FIXED_PANEL_OWNERSHIP {
  AppleAgxFixedPanelUnregistered = 0,
  AppleAgxFixedPanelRegistered,
  AppleAgxFixedPanelOwnershipUnknown,
} APPLE_AGX_FIXED_PANEL_OWNERSHIP;

typedef struct _APPLE_AGX_FIXED_PANEL {
  APPLE_AGX_SCANOUT_CLIENT Scanout;
  APPLE_AGX_SCANOUT_U64 PoolIpa;
  APPLE_AGX_SCANOUT_U64 ActiveOffset;
  APPLE_AGX_SCANOUT_U32 LastSwapId;
  APPLE_AGX_SCANOUT_BOOL Started;
  APPLE_AGX_SCANOUT_BOOL Committed;
  APPLE_AGX_SCANOUT_BOOL Visible;
  APPLE_AGX_SCANOUT_BOOL PresentConsumed;
  APPLE_AGX_FIXED_PANEL_OWNERSHIP Ownership;
} APPLE_AGX_FIXED_PANEL;

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelInitialize(
    APPLE_AGX_FIXED_PANEL *Panel, const APPLE_AGX_SCANOUT_IO *Io,
    APPLE_AGX_SCANOUT_U64 PoolIpa,
    APPLE_AGX_SCANOUT_U64 FirstSequence, APPLE_AGX_SCANOUT_U32 MaxPolls);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelStart(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 DeadlineMs);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelCommit(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_U32 TargetId, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Stride,
    APPLE_AGX_SCANOUT_U32 Format);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelSetVisible(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_BOOL Visible);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelPresent(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SegmentId,
    APPLE_AGX_SCANOUT_U64 SegmentOffset,
    APPLE_AGX_SCANOUT_U64 DeadlineMs, APPLE_AGX_SCANOUT_U32 *SwapId);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelQueuePresent(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SegmentId,
    APPLE_AGX_SCANOUT_U64 SegmentOffset,
    APPLE_AGX_SCANOUT_U64 *Sequence);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelStop(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 DeadlineMs);

#endif /* APPLE_AGX_FIXED_PANEL_H */
