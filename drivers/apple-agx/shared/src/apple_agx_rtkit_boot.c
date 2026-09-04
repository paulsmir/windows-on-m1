#include "apple_agx_rtkit_boot.h"

#define APPLE_AGX_RTKIT_BOOT_NULL ((void *)0)

static void AppleAgxRtkitBootClearOutput(
    APPLE_AGX_RTKIT_BOOT_OUTPUT *Output) {
  Output->Count = 0u;
  Output->Message[0] = 0u;
  Output->Message[1] = 0u;
}

static APPLE_AGX_RTKIT_BOOT_RESULT
AppleAgxRtkitBootFail(APPLE_AGX_RTKIT_BOOT *Boot) {
  Boot->Phase = AppleAgxRtkitBootFailed;
  return AppleAgxRtkitBootResultProtocolViolation;
}

static void AppleAgxRtkitBootUpdatePhase(APPLE_AGX_RTKIT_BOOT *Boot) {
  if (Boot->HelloSeen == APPLE_AGX_RTKIT_FALSE) {
    Boot->Phase = AppleAgxRtkitBootAwaitingHello;
  } else if (Boot->EndpointMapComplete == APPLE_AGX_RTKIT_FALSE) {
    Boot->Phase = AppleAgxRtkitBootAwaitingEndpointMap;
  } else if (Boot->IopPowerReady == APPLE_AGX_RTKIT_FALSE ||
             Boot->ApPowerReady == APPLE_AGX_RTKIT_FALSE) {
    Boot->Phase = AppleAgxRtkitBootAwaitingPower;
  } else {
    Boot->Phase = AppleAgxRtkitBootReady;
  }
}

void AppleAgxRtkitBootInitialize(APPLE_AGX_RTKIT_BOOT *Boot) {
  APPLE_AGX_RTKIT_U32 index;
  if (Boot == APPLE_AGX_RTKIT_BOOT_NULL)
    return;
  Boot->Phase = AppleAgxRtkitBootAwaitingHello;
  Boot->NegotiatedVersion = 0u;
  for (index = 0u; index < 8u; ++index)
    Boot->EndpointMap[index] = 0u;
  Boot->Begun = APPLE_AGX_RTKIT_FALSE;
  Boot->HelloSeen = APPLE_AGX_RTKIT_FALSE;
  Boot->EndpointMapComplete = APPLE_AGX_RTKIT_FALSE;
  Boot->IopPowerReady = APPLE_AGX_RTKIT_FALSE;
  Boot->ApPowerRequested = APPLE_AGX_RTKIT_FALSE;
  Boot->ApPowerReady = APPLE_AGX_RTKIT_FALSE;
}

APPLE_AGX_RTKIT_BOOT_RESULT
AppleAgxRtkitBootBegin(APPLE_AGX_RTKIT_BOOT *Boot,
                       APPLE_AGX_RTKIT_BOOT_OUTPUT *Output) {
  if (Boot == APPLE_AGX_RTKIT_BOOT_NULL ||
      Output == APPLE_AGX_RTKIT_BOOT_NULL)
    return AppleAgxRtkitBootResultInvalidArgument;
  AppleAgxRtkitBootClearOutput(Output);
  if (Boot->Begun != APPLE_AGX_RTKIT_FALSE ||
      Boot->Phase != AppleAgxRtkitBootAwaitingHello)
    return AppleAgxRtkitBootResultInvalidState;
  Output->Message[0] = AppleAgxRtkitSetIopPower(0x220u);
  Output->Count = 1u;
  Boot->Begun = APPLE_AGX_RTKIT_TRUE;
  return AppleAgxRtkitBootResultOk;
}

APPLE_AGX_RTKIT_BOOT_RESULT AppleAgxRtkitBootHandle(
    APPLE_AGX_RTKIT_BOOT *Boot, APPLE_AGX_RTKIT_U64 Payload,
    APPLE_AGX_RTKIT_U32 Endpoint, APPLE_AGX_RTKIT_BOOT_OUTPUT *Output) {
  APPLE_AGX_RTKIT_MANAGEMENT message;
  APPLE_AGX_RTKIT_U32 version;

  if (Boot == APPLE_AGX_RTKIT_BOOT_NULL ||
      Output == APPLE_AGX_RTKIT_BOOT_NULL)
    return AppleAgxRtkitBootResultInvalidArgument;
  AppleAgxRtkitBootClearOutput(Output);
  if (Boot->Begun == APPLE_AGX_RTKIT_FALSE ||
      Boot->Phase == AppleAgxRtkitBootReady ||
      Boot->Phase == AppleAgxRtkitBootFailed)
    return AppleAgxRtkitBootResultInvalidState;
  if (Endpoint != 0u ||
      AppleAgxRtkitDecodeManagement(Payload, &message) ==
          APPLE_AGX_RTKIT_FALSE)
    return AppleAgxRtkitBootFail(Boot);

  switch (message.Type) {
  case AppleAgxRtkitManagementHello:
    if (Boot->HelloSeen != APPLE_AGX_RTKIT_FALSE ||
        message.MinVersion > message.MaxVersion)
      return AppleAgxRtkitBootFail(Boot);
    version = message.MaxVersion;
    Output->Message[0] = AppleAgxRtkitHelloAck(version, version);
    if (Output->Message[0] == APPLE_AGX_RTKIT_INVALID_MESSAGE)
      return AppleAgxRtkitBootFail(Boot);
    Output->Count = 1u;
    Boot->NegotiatedVersion = version;
    Boot->HelloSeen = APPLE_AGX_RTKIT_TRUE;
    break;
  case AppleAgxRtkitManagementEndpointMap:
    if (Boot->HelloSeen == APPLE_AGX_RTKIT_FALSE ||
        Boot->EndpointMapComplete != APPLE_AGX_RTKIT_FALSE)
      return AppleAgxRtkitBootFail(Boot);
    Boot->EndpointMap[message.Base] = message.Bitmap;
    Output->Message[0] = AppleAgxRtkitEndpointMapAck(
        message.Base, message.Last, message.Last != 0u ? 0u : 1u);
    Output->Count = 1u;
    if (message.Last != 0u) {
      Boot->EndpointMapComplete = APPLE_AGX_RTKIT_TRUE;
      Boot->ApPowerRequested = APPLE_AGX_RTKIT_TRUE;
      Output->Message[1] = AppleAgxRtkitSetApPower(0x20u);
      Output->Count = 2u;
    }
    break;
  case AppleAgxRtkitManagementIopPowerAck:
    if (Boot->IopPowerReady != APPLE_AGX_RTKIT_FALSE ||
        message.State != 0x20u)
      return AppleAgxRtkitBootFail(Boot);
    Boot->IopPowerReady = APPLE_AGX_RTKIT_TRUE;
    break;
  case AppleAgxRtkitManagementSetApPower:
    if (Boot->ApPowerRequested == APPLE_AGX_RTKIT_FALSE ||
        Boot->ApPowerReady != APPLE_AGX_RTKIT_FALSE ||
        message.State != 0x20u)
      return AppleAgxRtkitBootFail(Boot);
    Boot->ApPowerReady = APPLE_AGX_RTKIT_TRUE;
    break;
  default:
    return AppleAgxRtkitBootFail(Boot);
  }

  AppleAgxRtkitBootUpdatePhase(Boot);
  return AppleAgxRtkitBootResultOk;
}

APPLE_AGX_RTKIT_BOOL
AppleAgxRtkitBootIsReady(const APPLE_AGX_RTKIT_BOOT *Boot) {
  return (APPLE_AGX_RTKIT_BOOL)(
      Boot != APPLE_AGX_RTKIT_BOOT_NULL &&
      Boot->Phase == AppleAgxRtkitBootReady);
}
