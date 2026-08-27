#ifndef APPLE_AGX_RTKIT_BOOT_H
#define APPLE_AGX_RTKIT_BOOT_H

#include "apple_agx_rtkit.h"

typedef enum _APPLE_AGX_RTKIT_BOOT_PHASE {
  AppleAgxRtkitBootOff = 0,
  AppleAgxRtkitBootAwaitingHello,
  AppleAgxRtkitBootAwaitingEndpointMap,
  AppleAgxRtkitBootAwaitingPower,
  AppleAgxRtkitBootReady,
  AppleAgxRtkitBootFailed,
} APPLE_AGX_RTKIT_BOOT_PHASE;

typedef enum _APPLE_AGX_RTKIT_BOOT_RESULT {
  AppleAgxRtkitBootResultOk = 0,
  AppleAgxRtkitBootResultInvalidArgument,
  AppleAgxRtkitBootResultInvalidState,
  AppleAgxRtkitBootResultProtocolViolation,
} APPLE_AGX_RTKIT_BOOT_RESULT;

typedef struct _APPLE_AGX_RTKIT_BOOT_OUTPUT {
  APPLE_AGX_RTKIT_U32 Count;
  APPLE_AGX_RTKIT_U64 Message[2];
} APPLE_AGX_RTKIT_BOOT_OUTPUT;

typedef struct _APPLE_AGX_RTKIT_BOOT {
  APPLE_AGX_RTKIT_BOOT_PHASE Phase;
  APPLE_AGX_RTKIT_U32 NegotiatedVersion;
  APPLE_AGX_RTKIT_U32 EndpointMap[8];
  APPLE_AGX_RTKIT_BOOL Begun;
  APPLE_AGX_RTKIT_BOOL HelloSeen;
  APPLE_AGX_RTKIT_BOOL EndpointMapComplete;
  APPLE_AGX_RTKIT_BOOL IopPowerReady;
  APPLE_AGX_RTKIT_BOOL ApPowerRequested;
  APPLE_AGX_RTKIT_BOOL ApPowerReady;
} APPLE_AGX_RTKIT_BOOT;

void AppleAgxRtkitBootInitialize(APPLE_AGX_RTKIT_BOOT *Boot);
APPLE_AGX_RTKIT_BOOT_RESULT
AppleAgxRtkitBootBegin(APPLE_AGX_RTKIT_BOOT *Boot,
                       APPLE_AGX_RTKIT_BOOT_OUTPUT *Output);
APPLE_AGX_RTKIT_BOOT_RESULT AppleAgxRtkitBootHandle(
    APPLE_AGX_RTKIT_BOOT *Boot, APPLE_AGX_RTKIT_U64 Payload,
    APPLE_AGX_RTKIT_U32 Endpoint, APPLE_AGX_RTKIT_BOOT_OUTPUT *Output);
APPLE_AGX_RTKIT_BOOL
AppleAgxRtkitBootIsReady(const APPLE_AGX_RTKIT_BOOT *Boot);

#endif /* APPLE_AGX_RTKIT_BOOT_H */
