#ifndef APPLE_AGX_FIXED_PANEL_H
#define APPLE_AGX_FIXED_PANEL_H

#include "apple_agx_scanout.h"

#define APPLE_AGX_FIXED_PANEL_SOURCE_ID 0u
#define APPLE_AGX_FIXED_PANEL_TARGET_ID 0u
#define APPLE_AGX_FIXED_PANEL_SEGMENT_ID 2u

/* rc106: a laptop panel keeps its native timing; smaller source modes are
 * scaled into it.  Values match D3DKMDT_VIDPN_PRESENT_PATH_SCALING. */
typedef enum _APPLE_AGX_FIXED_PANEL_SCALING {
  AppleAgxFixedPanelScalingIdentity = 1,
  AppleAgxFixedPanelScalingCentered = 2,
  AppleAgxFixedPanelScalingStretched = 3,
  AppleAgxFixedPanelScalingAspectRatioCenteredMax = 4,
} APPLE_AGX_FIXED_PANEL_SCALING;

#define APPLE_AGX_FIXED_PANEL_SCALING_BIT(Scaling) (1u << (unsigned)(Scaling))

typedef struct _APPLE_AGX_FIXED_PANEL_SOURCE_MODE {
  APPLE_AGX_SCANOUT_U32 Width;
  APPLE_AGX_SCANOUT_U32 Height;
  APPLE_AGX_SCANOUT_U32 Stride;
  APPLE_AGX_SCANOUT_U64 SurfaceBytes;
} APPLE_AGX_FIXED_PANEL_SOURCE_MODE;

typedef struct _APPLE_AGX_FIXED_PANEL_RECT {
  APPLE_AGX_SCANOUT_U32 X;
  APPLE_AGX_SCANOUT_U32 Y;
  APPLE_AGX_SCANOUT_U32 Width;
  APPLE_AGX_SCANOUT_U32 Height;
} APPLE_AGX_FIXED_PANEL_RECT;

/* Source modes in offer order; index 0 is the native 2560x1600 mode and is
 * the only one when Scaled is false (no CAP_SCALED_SOURCE). */
APPLE_AGX_SCANOUT_U32 AppleAgxFixedPanelSourceModeCount(
    APPLE_AGX_SCANOUT_BOOL Scaled);
APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelSourceModeAt(
    APPLE_AGX_SCANOUT_U32 Index, APPLE_AGX_SCANOUT_BOOL Scaled,
    APPLE_AGX_FIXED_PANEL_SOURCE_MODE *Mode);
APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelSourceModeFind(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_SCANOUT_U32 Stride, APPLE_AGX_SCANOUT_BOOL Scaled,
    APPLE_AGX_FIXED_PANEL_SOURCE_MODE *Mode);
/* Bit set of APPLE_AGX_FIXED_PANEL_SCALING_BIT() cofunctional with a source
 * of Width x Height: identity exactly at native size (D3DKMDT_VPPS_IDENTITY
 * requires equal source and target), the three scaled transforms below it.
 * Zero for a size that is not an offered source mode. */
APPLE_AGX_SCANOUT_U32 AppleAgxFixedPanelScalingSupport(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_SCANOUT_BOOL Scaled);
/* The scaling a commit applies.  Requested is a D3DKMDT_VPPS value: 0
 * (uninitialized), 254 (unpinned) and 255 (not specified) let the driver
 * choose (identity at native size, aspect-ratio-centered-max below it, the
 * laptop default).  Below native size 2-4 are applied as requested.  At
 * native size 1-4 all describe the same full-panel transform and resolve to
 * identity: the native commit accepted every one of them before rc106 and a
 * persisted display configuration may still pin one.  Custom (5) and any
 * other value are refused. */
APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelResolveScaling(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_SCANOUT_BOOL Scaled, APPLE_AGX_SCANOUT_U32 Requested,
    APPLE_AGX_FIXED_PANEL_SCALING *Resolved);
/* The panel rectangle a Width x Height source occupies under Scaling. */
APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelDestination(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_FIXED_PANEL_SCALING Scaling,
    APPLE_AGX_FIXED_PANEL_RECT *Rect);

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
  AppleAgxFixedPanelDeferred,
} APPLE_AGX_FIXED_PANEL_RESULT;

typedef enum _APPLE_AGX_FIXED_PANEL_OWNERSHIP {
  AppleAgxFixedPanelUnregistered = 0,
  AppleAgxFixedPanelRegistered,
  AppleAgxFixedPanelOwnershipUnknown,
} APPLE_AGX_FIXED_PANEL_OWNERSHIP;

typedef struct _APPLE_AGX_FIXED_PANEL {
  APPLE_AGX_SCANOUT_CLIENT Scanout;
  APPLE_AGX_SCANOUT_U64 PoolIpa;
  APPLE_AGX_SCANOUT_U64 PoolBytes;
  APPLE_AGX_SCANOUT_U64 ActiveOffset;
  APPLE_AGX_SCANOUT_U32 LastSwapId;
  APPLE_AGX_SCANOUT_BOOL Started;
  APPLE_AGX_SCANOUT_BOOL Committed;
  APPLE_AGX_SCANOUT_BOOL Visible;
  APPLE_AGX_SCANOUT_BOOL PresentConsumed;
  /* Source address accepted while the source was hidden (WDDM mode set). */
  APPLE_AGX_SCANOUT_U64 DeferredOffset;
  APPLE_AGX_SCANOUT_BOOL DeferredValid;
  APPLE_AGX_FIXED_PANEL_OWNERSHIP Ownership;
  /* S2: set by the owner between Initialize and Start (opt-in switch).
   * Start then negotiates ABI V3 when the broker advertises
   * CAP_TWO_LAYERS; FALSE (Initialize's default) never touches a V3
   * register.  The negotiated state is Scanout.TwoLayers. */
  APPLE_AGX_SCANOUT_BOOL RequestTwoLayers;
} APPLE_AGX_FIXED_PANEL;

APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelInitialize(
    APPLE_AGX_FIXED_PANEL *Panel, const APPLE_AGX_SCANOUT_IO *Io,
    APPLE_AGX_SCANOUT_U64 PoolIpa, APPLE_AGX_SCANOUT_U64 PoolBytes,
    APPLE_AGX_SCANOUT_U64 FirstSequence, APPLE_AGX_SCANOUT_U32 MaxPolls);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelStart(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 DeadlineMs);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelCommit(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_U32 TargetId, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Stride,
    APPLE_AGX_SCANOUT_U32 Format);
/* rc106: commit any offered source mode with its scaling.  A non-native
 * mode requires CAP_SCALED_SOURCE; the native mode requires identity. */
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelCommitMode(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_U32 TargetId, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Stride,
    APPLE_AGX_SCANOUT_U32 Format, APPLE_AGX_FIXED_PANEL_SCALING Scaling);
/* rc110: the surface format of the next PRESENT (BGRA8888, or RGBA8888 with
 * CAP_RGBA8888), independent of the committed source mode's pixel format:
 * dxgkrnl flips an exclusive fullscreen primary in the format the app chose
 * and the broker scans either.  CommitMode resets it to the committed one. */
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelSetPresentFormat(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 Format);
APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelScaledSourceSupported(
    const APPLE_AGX_FIXED_PANEL *Panel);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelSetVisible(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SourceId,
    APPLE_AGX_SCANOUT_BOOL Visible);
APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelTakeDeferred(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 *SegmentOffset);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelPresent(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SegmentId,
    APPLE_AGX_SCANOUT_U64 SegmentOffset,
    APPLE_AGX_SCANOUT_U64 DeadlineMs, APPLE_AGX_SCANOUT_U32 *SwapId);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelQueuePresent(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U32 SegmentId,
    APPLE_AGX_SCANOUT_U64 SegmentOffset,
    APPLE_AGX_SCANOUT_U64 *Sequence);
/* S2: queue one V3 commit (1 or 2 records) on the started, committed and
 * visible panel.  BrokerRejected leaves the refusal code in
 * Scanout.LastBrokerResult. */
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelQueueCommitLayers(
    APPLE_AGX_FIXED_PANEL *Panel, const APPLE_AGX_SCANOUT_LAYER *Layers,
    APPLE_AGX_SCANOUT_U32 Count, APPLE_AGX_SCANOUT_U64 *Sequence);
APPLE_AGX_SCANOUT_BOOL AppleAgxFixedPanelTwoLayers(
    const APPLE_AGX_FIXED_PANEL *Panel);
APPLE_AGX_FIXED_PANEL_RESULT AppleAgxFixedPanelStop(
    APPLE_AGX_FIXED_PANEL *Panel, APPLE_AGX_SCANOUT_U64 DeadlineMs);

#endif /* APPLE_AGX_FIXED_PANEL_H */
