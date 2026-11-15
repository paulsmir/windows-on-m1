#ifndef APPLE_AGX_POST_DISPLAY_RELEASE_H
#define APPLE_AGX_POST_DISPLAY_RELEASE_H

/* PD-1c (KMD-D2): pure, host-testable pieces of the post-display hand-back.
 * No DXGK types: the KMD copies DXGK_DISPLAY_INFORMATION into and out of
 * APPLE_AGX_RELEASE_INFO. Microsoft: DxgkDdiStopDeviceAndReleasePostDisplayOwnership
 * returns the display that remains active (TargetId/AcpiId), a 32-bit
 * X8R8G8B8 or A8R8G8B8 linear surface; NOT_SUPPORTED only when no display is
 * connected. */

#include "apple_agx_scanout.h"

#define APPLE_AGX_DDIFMT_A8R8G8B8 21u /* D3DDDIFMT_A8R8G8B8 (d3dukmdt.h) */
#define APPLE_AGX_DDIFMT_X8R8G8B8 22u /* D3DDDIFMT_X8R8G8B8 */
#define APPLE_AGX_RELEASE_PAGE 0x1000ull

typedef struct _APPLE_AGX_RELEASE_INFO {
  APPLE_AGX_SCANOUT_U32 Width, Height, Pitch, ColorFormat;
  APPLE_AGX_SCANOUT_U64 PhysicalAddress;
  APPLE_AGX_SCANOUT_U32 TargetId, AcpiId;
} APPLE_AGX_RELEASE_INFO;

/* Panel limits from one KMD panel-geometry source (not literals here). */
typedef struct _APPLE_AGX_PANEL_LIMITS {
  APPLE_AGX_SCANOUT_U32 MaxWidth, MaxHeight;
} APPLE_AGX_PANEL_LIMITS;

typedef struct _APPLE_AGX_RELEASE_CHILD {
  APPLE_AGX_SCANOUT_U32 ChildUid, AcpiUid;
  APPLE_AGX_SCANOUT_BOOL Internal, Connected;
} APPLE_AGX_RELEASE_CHILD;

typedef enum _APPLE_AGX_RELEASE_RESULT {
  AppleAgxReleaseOk = 0,
  AppleAgxReleaseNoDisplay = 1,     /* -> STATUS_NOT_SUPPORTED (documented case) */
  AppleAgxReleaseNoSurface = 2,     /* -> STATUS_UNSUCCESSFUL (own-scanout route) */
  AppleAgxReleaseInvalid = 3,       /* -> STATUS_INVALID_PARAMETER */
} APPLE_AGX_RELEASE_RESULT;

/* T3: a POST/release surface the basic display driver can use. */
static inline APPLE_AGX_SCANOUT_BOOL AppleAgxPostDisplaySurfaceValid(
    APPLE_AGX_SCANOUT_U32 Width, APPLE_AGX_SCANOUT_U32 Height,
    APPLE_AGX_SCANOUT_U32 Pitch, APPLE_AGX_SCANOUT_U32 Format,
    APPLE_AGX_SCANOUT_U64 PhysicalAddress, const APPLE_AGX_PANEL_LIMITS *Limits) {
  APPLE_AGX_SCANOUT_U64 row;
  if (Limits == 0 || Width == 0u || Height == 0u) return 0;
  if (Width > Limits->MaxWidth || Height > Limits->MaxHeight) return 0;
  if (Format != APPLE_AGX_DDIFMT_X8R8G8B8 && Format != APPLE_AGX_DDIFMT_A8R8G8B8)
    return 0;
  row = (APPLE_AGX_SCANOUT_U64)Width * 4ull;
  if ((APPLE_AGX_SCANOUT_U64)Pitch < row) return 0;
  if ((APPLE_AGX_SCANOUT_U64)Pitch * Height > 0xFFFFFFFFull) return 0;
  if (PhysicalAddress == 0ull || (PhysicalAddress & (APPLE_AGX_RELEASE_PAGE - 1ull)) != 0ull)
    return 0;
  return 1;
}

/* T4: DXGK_DISPLAY_INFORMATION to return from the release DDI. */
static inline APPLE_AGX_RELEASE_RESULT AppleAgxBuildReleaseInfo(
    const APPLE_AGX_RELEASE_INFO *Acquired, const APPLE_AGX_RELEASE_CHILD *Children,
    APPLE_AGX_SCANOUT_U32 ChildCount, APPLE_AGX_SCANOUT_U32 TargetIn,
    const APPLE_AGX_PANEL_LIMITS *Limits, APPLE_AGX_RELEASE_INFO *Out,
    APPLE_AGX_SCANOUT_U32 *MappedTarget) {
  const APPLE_AGX_RELEASE_CHILD *chosen = 0;
  APPLE_AGX_SCANOUT_U32 i;
  if (Acquired == 0 || Out == 0 || MappedTarget == 0) return AppleAgxReleaseInvalid;
  /* The display that remains active: TargetIn when it is connected, else the
   * connected internal panel, else any connected target. */
  for (i = 0u; Children != 0 && i < ChildCount; ++i)
    if (Children[i].Connected && Children[i].ChildUid == TargetIn) chosen = &Children[i];
  for (i = 0u; chosen == 0 && Children != 0 && i < ChildCount; ++i)
    if (Children[i].Connected && Children[i].Internal) chosen = &Children[i];
  for (i = 0u; chosen == 0 && Children != 0 && i < ChildCount; ++i)
    if (Children[i].Connected) chosen = &Children[i];
  if (chosen == 0) return AppleAgxReleaseNoDisplay;
  if (!AppleAgxPostDisplaySurfaceValid(Acquired->Width, Acquired->Height,
          Acquired->Pitch, Acquired->ColorFormat, Acquired->PhysicalAddress, Limits))
    return AppleAgxReleaseNoSurface;
  *Out = *Acquired;
  Out->TargetId = chosen->ChildUid;
  Out->AcpiId = chosen->AcpiUid;
  *MappedTarget = chosen->ChildUid;
  return AppleAgxReleaseOk;
}

#endif
