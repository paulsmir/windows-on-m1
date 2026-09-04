#include "apple_agx_rtkit.h"

#define APPLE_AGX_RTKIT_NULL ((void *)0)
#define APPLE_AGX_RTKIT_MANAGEMENT_TYPE_SHIFT 52u
#define APPLE_AGX_RTKIT_MANAGEMENT_TYPE_MASK (0xffULL << 52u)
#define APPLE_AGX_RTKIT_STATE_MASK 0xffffULL
#define APPLE_AGX_RTKIT_ENDPOINT_MASK 0xffULL
#define APPLE_AGX_RTKIT_ENDPOINT_SHIFT 32u
#define APPLE_AGX_RTKIT_FLAG_MASK 0x3ULL
#define APPLE_AGX_RTKIT_HELLO_MASK 0xffffffffULL
#define APPLE_AGX_RTKIT_EPMAP_LAST_MASK (1ULL << 51u)
#define APPLE_AGX_RTKIT_EPMAP_BASE_MASK (0x7ULL << 32u)
#define APPLE_AGX_RTKIT_EPMAP_BITMAP_MASK 0xffffffffULL
#define APPLE_AGX_RTKIT_INITDATA_TYPE (0x81ULL << 48u)
#define APPLE_AGX_RTKIT_INITDATA_ADDRESS_MASK ((1ULL << 44u) - 1ULL)

static APPLE_AGX_RTKIT_U64
AppleAgxRtkitManagementType(APPLE_AGX_RTKIT_U32 Type) {
  return ((APPLE_AGX_RTKIT_U64)Type)
         << APPLE_AGX_RTKIT_MANAGEMENT_TYPE_SHIFT;
}

APPLE_AGX_RTKIT_U64 AppleAgxRtkitHelloAck(APPLE_AGX_RTKIT_U32 MinVersion,
                                          APPLE_AGX_RTKIT_U32 MaxVersion) {
  if (MinVersion > MaxVersion || MaxVersion > 0xffffu)
    return APPLE_AGX_RTKIT_INVALID_MESSAGE;
  return AppleAgxRtkitManagementType(AppleAgxRtkitManagementHelloAck) |
         ((APPLE_AGX_RTKIT_U64)MaxVersion << 16u) | MinVersion;
}

APPLE_AGX_RTKIT_U64 AppleAgxRtkitEndpointMapAck(APPLE_AGX_RTKIT_U32 Base,
                                                APPLE_AGX_RTKIT_U32 Last,
                                                APPLE_AGX_RTKIT_U32 More) {
  if (Base > 7u || Last > 1u || More > 1u)
    return APPLE_AGX_RTKIT_INVALID_MESSAGE;
  return AppleAgxRtkitManagementType(AppleAgxRtkitManagementEndpointMap) |
         ((APPLE_AGX_RTKIT_U64)Last << 51u) |
         ((APPLE_AGX_RTKIT_U64)Base << 32u) | More;
}

static void AppleAgxRtkitClearManagement(
    APPLE_AGX_RTKIT_MANAGEMENT *Decoded) {
  Decoded->Type = (APPLE_AGX_RTKIT_MANAGEMENT_TYPE)0;
  Decoded->State = 0;
  Decoded->Endpoint = 0;
  Decoded->Flag = 0;
  Decoded->MinVersion = 0;
  Decoded->MaxVersion = 0;
  Decoded->Last = 0;
  Decoded->Base = 0;
  Decoded->Bitmap = 0;
}

APPLE_AGX_RTKIT_U64 AppleAgxRtkitSetIopPower(APPLE_AGX_RTKIT_U32 State) {
  if (State > APPLE_AGX_RTKIT_STATE_MASK)
    return APPLE_AGX_RTKIT_INVALID_MESSAGE;
  return AppleAgxRtkitManagementType(AppleAgxRtkitManagementSetIopPower) |
         State;
}

APPLE_AGX_RTKIT_U64 AppleAgxRtkitSetApPower(APPLE_AGX_RTKIT_U32 State) {
  if (State > APPLE_AGX_RTKIT_STATE_MASK)
    return APPLE_AGX_RTKIT_INVALID_MESSAGE;
  return AppleAgxRtkitManagementType(AppleAgxRtkitManagementSetApPower) |
         State;
}

APPLE_AGX_RTKIT_U64
AppleAgxRtkitStartEndpoint(APPLE_AGX_RTKIT_U32 Endpoint,
                           APPLE_AGX_RTKIT_U32 Flag) {
  if (Endpoint > APPLE_AGX_RTKIT_ENDPOINT_MASK ||
      Flag > APPLE_AGX_RTKIT_FLAG_MASK)
    return APPLE_AGX_RTKIT_INVALID_MESSAGE;
  return AppleAgxRtkitManagementType(AppleAgxRtkitManagementStartEndpoint) |
         ((APPLE_AGX_RTKIT_U64)Endpoint << APPLE_AGX_RTKIT_ENDPOINT_SHIFT) |
         Flag;
}

APPLE_AGX_RTKIT_U64 AppleAgxRtkitInitdata(APPLE_AGX_RTKIT_U64 Address) {
  if (Address > APPLE_AGX_RTKIT_INITDATA_ADDRESS_MASK)
    return APPLE_AGX_RTKIT_INVALID_MESSAGE;
  return APPLE_AGX_RTKIT_INITDATA_TYPE | Address;
}

APPLE_AGX_RTKIT_BOOL AppleAgxRtkitDecodeManagement(
    APPLE_AGX_RTKIT_U64 Message, APPLE_AGX_RTKIT_MANAGEMENT *Decoded) {
  APPLE_AGX_RTKIT_U32 type;
  APPLE_AGX_RTKIT_U64 allowed;

  if (Decoded == APPLE_AGX_RTKIT_NULL)
    return APPLE_AGX_RTKIT_FALSE;
  AppleAgxRtkitClearManagement(Decoded);
  type = (APPLE_AGX_RTKIT_U32)(
      (Message & APPLE_AGX_RTKIT_MANAGEMENT_TYPE_MASK) >>
      APPLE_AGX_RTKIT_MANAGEMENT_TYPE_SHIFT);
  Decoded->Type = (APPLE_AGX_RTKIT_MANAGEMENT_TYPE)type;

  switch (type) {
  case AppleAgxRtkitManagementHello:
  case AppleAgxRtkitManagementHelloAck:
    allowed = APPLE_AGX_RTKIT_MANAGEMENT_TYPE_MASK |
              APPLE_AGX_RTKIT_HELLO_MASK;
    if ((Message & ~allowed) != 0)
      return APPLE_AGX_RTKIT_FALSE;
    Decoded->MinVersion = (APPLE_AGX_RTKIT_U32)(Message & 0xffffULL);
    Decoded->MaxVersion =
        (APPLE_AGX_RTKIT_U32)((Message >> 16u) & 0xffffULL);
    return APPLE_AGX_RTKIT_TRUE;
  case AppleAgxRtkitManagementPing:
  case AppleAgxRtkitManagementPong:
    return Message == AppleAgxRtkitManagementType(type)
               ? APPLE_AGX_RTKIT_TRUE
               : APPLE_AGX_RTKIT_FALSE;
  case AppleAgxRtkitManagementStartEndpoint:
    allowed = APPLE_AGX_RTKIT_MANAGEMENT_TYPE_MASK |
              (APPLE_AGX_RTKIT_ENDPOINT_MASK <<
               APPLE_AGX_RTKIT_ENDPOINT_SHIFT) |
              APPLE_AGX_RTKIT_FLAG_MASK;
    if ((Message & ~allowed) != 0)
      return APPLE_AGX_RTKIT_FALSE;
    Decoded->Endpoint = (APPLE_AGX_RTKIT_U32)(
        (Message >> APPLE_AGX_RTKIT_ENDPOINT_SHIFT) &
        APPLE_AGX_RTKIT_ENDPOINT_MASK);
    Decoded->Flag =
        (APPLE_AGX_RTKIT_U32)(Message & APPLE_AGX_RTKIT_FLAG_MASK);
    return APPLE_AGX_RTKIT_TRUE;
  case AppleAgxRtkitManagementSetIopPower:
  case AppleAgxRtkitManagementIopPowerAck:
  case AppleAgxRtkitManagementSetApPower:
    allowed = APPLE_AGX_RTKIT_MANAGEMENT_TYPE_MASK |
              APPLE_AGX_RTKIT_STATE_MASK;
    if ((Message & ~allowed) != 0)
      return APPLE_AGX_RTKIT_FALSE;
    Decoded->State =
        (APPLE_AGX_RTKIT_U32)(Message & APPLE_AGX_RTKIT_STATE_MASK);
    return APPLE_AGX_RTKIT_TRUE;
  case AppleAgxRtkitManagementEndpointMap:
    allowed = APPLE_AGX_RTKIT_MANAGEMENT_TYPE_MASK |
              APPLE_AGX_RTKIT_EPMAP_LAST_MASK |
              APPLE_AGX_RTKIT_EPMAP_BASE_MASK |
              APPLE_AGX_RTKIT_EPMAP_BITMAP_MASK;
    if ((Message & ~allowed) != 0)
      return APPLE_AGX_RTKIT_FALSE;
    Decoded->Last =
        (Message & APPLE_AGX_RTKIT_EPMAP_LAST_MASK) != 0 ? 1u : 0u;
    Decoded->Base = (APPLE_AGX_RTKIT_U32)(
        (Message & APPLE_AGX_RTKIT_EPMAP_BASE_MASK) >> 32u);
    Decoded->Bitmap =
        (APPLE_AGX_RTKIT_U32)(Message & APPLE_AGX_RTKIT_EPMAP_BITMAP_MASK);
    return APPLE_AGX_RTKIT_TRUE;
  default:
    AppleAgxRtkitClearManagement(Decoded);
    return APPLE_AGX_RTKIT_FALSE;
  }
}

APPLE_AGX_RTKIT_BOOL
AppleAgxRtkitDecodeEndpoint(APPLE_AGX_RTKIT_U64 Selector,
                            APPLE_AGX_RTKIT_U32 *Endpoint) {
  if (Endpoint == APPLE_AGX_RTKIT_NULL ||
      (Selector & ~APPLE_AGX_RTKIT_ENDPOINT_MASK) != 0)
    return APPLE_AGX_RTKIT_FALSE;
  *Endpoint = (APPLE_AGX_RTKIT_U32)Selector;
  return APPLE_AGX_RTKIT_TRUE;
}
