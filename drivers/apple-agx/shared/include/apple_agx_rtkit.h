#ifndef APPLE_AGX_RTKIT_H
#define APPLE_AGX_RTKIT_H

/* Keep this protocol codec usable in freestanding WDK C and host tests. */
typedef unsigned char APPLE_AGX_RTKIT_BOOL;
typedef unsigned int APPLE_AGX_RTKIT_U32;
typedef unsigned long long APPLE_AGX_RTKIT_U64;

#define APPLE_AGX_RTKIT_FALSE ((APPLE_AGX_RTKIT_BOOL)0u)
#define APPLE_AGX_RTKIT_TRUE ((APPLE_AGX_RTKIT_BOOL)1u)
#define APPLE_AGX_RTKIT_INVALID_MESSAGE (~0ULL)

typedef enum _APPLE_AGX_RTKIT_MANAGEMENT_TYPE {
  AppleAgxRtkitManagementHello = 1,
  AppleAgxRtkitManagementHelloAck = 2,
  AppleAgxRtkitManagementPing = 3,
  AppleAgxRtkitManagementPong = 4,
  AppleAgxRtkitManagementStartEndpoint = 5,
  AppleAgxRtkitManagementSetIopPower = 6,
  AppleAgxRtkitManagementIopPowerAck = 7,
  AppleAgxRtkitManagementEndpointMap = 8,
  AppleAgxRtkitManagementSetApPower = 0x0b,
} APPLE_AGX_RTKIT_MANAGEMENT_TYPE;

typedef struct _APPLE_AGX_RTKIT_MANAGEMENT {
  APPLE_AGX_RTKIT_MANAGEMENT_TYPE Type;
  APPLE_AGX_RTKIT_U32 State;
  APPLE_AGX_RTKIT_U32 Endpoint;
  APPLE_AGX_RTKIT_U32 Flag;
  APPLE_AGX_RTKIT_U32 MinVersion;
  APPLE_AGX_RTKIT_U32 MaxVersion;
  APPLE_AGX_RTKIT_U32 Last;
  APPLE_AGX_RTKIT_U32 Base;
  APPLE_AGX_RTKIT_U32 Bitmap;
} APPLE_AGX_RTKIT_MANAGEMENT;

APPLE_AGX_RTKIT_U64 AppleAgxRtkitSetIopPower(APPLE_AGX_RTKIT_U32 State);
APPLE_AGX_RTKIT_U64 AppleAgxRtkitSetApPower(APPLE_AGX_RTKIT_U32 State);
APPLE_AGX_RTKIT_U64
AppleAgxRtkitStartEndpoint(APPLE_AGX_RTKIT_U32 Endpoint,
                           APPLE_AGX_RTKIT_U32 Flag);
APPLE_AGX_RTKIT_U64 AppleAgxRtkitInitdata(APPLE_AGX_RTKIT_U64 Address);
APPLE_AGX_RTKIT_BOOL AppleAgxRtkitDecodeManagement(
    APPLE_AGX_RTKIT_U64 Message, APPLE_AGX_RTKIT_MANAGEMENT *Decoded);
APPLE_AGX_RTKIT_BOOL
AppleAgxRtkitDecodeEndpoint(APPLE_AGX_RTKIT_U64 Selector,
                            APPLE_AGX_RTKIT_U32 *Endpoint);

#endif /* APPLE_AGX_RTKIT_H */
