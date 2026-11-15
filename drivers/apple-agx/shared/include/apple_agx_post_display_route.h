#ifndef APPLE_AGX_POST_DISPLAY_ROUTE_H
#define APPLE_AGX_POST_DISPLAY_ROUTE_H

#include "apple_agx_scanout.h"
#include "apple_agx_post_display_release.h"

typedef enum _APPLE_AGX_POST_DISPLAY_ROUTE {
  AppleAgxPostDisplayAcquireFailed = 0,
  AppleAgxPostDisplayOwnScanout = 1,
  AppleAgxPostDisplayAdoptPost = 2,
  AppleAgxPostDisplayGeometryRejected = 3,
} APPLE_AGX_POST_DISPLAY_ROUTE;

/* PD-1c (KMD-D2): the POST surface is checked against the panel limits the KMD
 * passes in (one panel-geometry source), not against literals here. */
static inline APPLE_AGX_POST_DISPLAY_ROUTE AppleAgxPostDisplayRoute(
    APPLE_AGX_SCANOUT_BOOL AcquireSucceeded, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Pitch,
    APPLE_AGX_SCANOUT_U32 ColorFormat, APPLE_AGX_SCANOUT_U64 PhysicalAddress,
    const APPLE_AGX_PANEL_LIMITS *Limits) {
  if (!AcquireSucceeded)
    return AppleAgxPostDisplayAcquireFailed;
  /* Dxgkrnl may successfully return Width=0 when no POST mode is available. */
  if (Width == 0u)
    return AppleAgxPostDisplayOwnScanout;
  if (!AppleAgxPostDisplaySurfaceValid(Width, Height, Pitch, ColorFormat,
                                       PhysicalAddress, Limits))
    return AppleAgxPostDisplayGeometryRejected;
  return AppleAgxPostDisplayAdoptPost;
}

#endif
