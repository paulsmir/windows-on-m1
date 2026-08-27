#ifndef APPLE_AGX_RTKIT_SESSION_H
#define APPLE_AGX_RTKIT_SESSION_H

#include "apple_agx_asc_transport.h"
#include "apple_agx_rtkit_boot.h"

typedef enum _APPLE_AGX_RTKIT_SESSION_RESULT {
  AppleAgxRtkitSessionResultOk = 0,
  AppleAgxRtkitSessionResultInvalidArgument,
  AppleAgxRtkitSessionResultInvalidState,
  AppleAgxRtkitSessionResultTimeout,
  AppleAgxRtkitSessionResultClockRegression,
  AppleAgxRtkitSessionResultTransportFailed,
  AppleAgxRtkitSessionResultProtocolViolation,
  AppleAgxRtkitSessionResultCleanupFailed,
} APPLE_AGX_RTKIT_SESSION_RESULT;

typedef struct _APPLE_AGX_RTKIT_SESSION {
  APPLE_AGX_RTKIT_BOOT Boot;
  APPLE_AGX_RTKIT_BOOL Running;
} APPLE_AGX_RTKIT_SESSION;

void AppleAgxRtkitSessionInitialize(APPLE_AGX_RTKIT_SESSION *Session);
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionBoot(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs);
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionStop(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs);

#endif /* APPLE_AGX_RTKIT_SESSION_H */
