#ifndef APPLE_AGX_POST_DISPLAY_ROUTE_H
#define APPLE_AGX_POST_DISPLAY_ROUTE_H

#include "apple_agx_scanout.h"

typedef enum _APPLE_AGX_POST_DISPLAY_ROUTE {
  AppleAgxPostDisplayAcquireFailed = 0,
  AppleAgxPostDisplayOwnScanout = 1,
  AppleAgxPostDisplayAdoptPost = 2,
  AppleAgxPostDisplayGeometryRejected = 3,
} APPLE_AGX_POST_DISPLAY_ROUTE;

static inline APPLE_AGX_POST_DISPLAY_ROUTE AppleAgxPostDisplayRoute(
    APPLE_AGX_SCANOUT_BOOL AcquireSucceeded, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, APPLE_AGX_SCANOUT_U32 Pitch,
    APPLE_AGX_SCANOUT_U64 PhysicalAddress) {
  if (!AcquireSucceeded)
    return AppleAgxPostDisplayAcquireFailed;
  /* Dxgkrnl may successfully return Width=0 when no POST mode is available. */
  if (Width == 0u)
    return AppleAgxPostDisplayOwnScanout;
  if (PhysicalAddress == 0ULL || Width != APPLE_AGX_SCANOUT_J313_WIDTH ||
      Height != APPLE_AGX_SCANOUT_J313_HEIGHT ||
      Pitch != APPLE_AGX_SCANOUT_J313_STRIDE)
    return AppleAgxPostDisplayGeometryRejected;
  return AppleAgxPostDisplayAdoptPost;
}

#endif
