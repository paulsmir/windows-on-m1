#include "apple_agx_rtkit_session.h"

#define APPLE_AGX_RTKIT_SESSION_NULL ((void *)0)

static APPLE_AGX_RTKIT_SESSION_RESULT
AppleAgxRtkitSessionAscResult(APPLE_AGX_ASC_RESULT Result) {
  switch (Result) {
  case AppleAgxAscResultOk:
    return AppleAgxRtkitSessionResultOk;
  case AppleAgxAscResultInvalidArgument:
    return AppleAgxRtkitSessionResultInvalidArgument;
  case AppleAgxAscResultTimeout:
    return AppleAgxRtkitSessionResultTimeout;
  case AppleAgxAscResultClockRegression:
    return AppleAgxRtkitSessionResultClockRegression;
  default:
    return AppleAgxRtkitSessionResultTransportFailed;
  }
}

static APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionSendOutput(
    const APPLE_AGX_ASC_IO *Io, const APPLE_AGX_RTKIT_BOOT_OUTPUT *Output,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_RTKIT_U32 index;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  for (index = 0u; index < Output->Count; ++index) {
    result = AppleAgxRtkitSessionAscResult(
        AppleAgxAscSend(Io, Output->Message[index], 0u, DeadlineMs));
    if (result != AppleAgxRtkitSessionResultOk)
      return result;
  }
  return AppleAgxRtkitSessionResultOk;
}

static APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionForceRunOff(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_RTKIT_SESSION_RESULT OriginalResult) {
  APPLE_AGX_ASC_RESULT stop_result =
      AppleAgxAscSetRun(Io, APPLE_AGX_ASC_FALSE);
  Session->Running = APPLE_AGX_RTKIT_FALSE;
  return stop_result == AppleAgxAscResultOk
             ? OriginalResult
             : AppleAgxRtkitSessionResultCleanupFailed;
}

void AppleAgxRtkitSessionInitialize(APPLE_AGX_RTKIT_SESSION *Session) {
  if (Session == APPLE_AGX_RTKIT_SESSION_NULL)
    return;
  AppleAgxRtkitBootInitialize(&Session->Boot);
  Session->Running = APPLE_AGX_RTKIT_FALSE;
  Session->CpuReady = APPLE_AGX_RTKIT_FALSE;
  Session->InboxBeforeInitValid = APPLE_AGX_RTKIT_FALSE;
  Session->InboxAfterInitValid = APPLE_AGX_RTKIT_FALSE;
  Session->InboxAtFailureValid = APPLE_AGX_RTKIT_FALSE;
  Session->OutboxAtFailureValid = APPLE_AGX_RTKIT_FALSE;
  Session->InboxControlBeforeInit = 0u;
  Session->InboxControlAfterInit = 0u;
  Session->InboxControlAtFailure = 0u;
  Session->OutboxControlAtFailure = 0u;
}

static void AppleAgxRtkitSessionCaptureFailureMailbox(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io) {
  if (AppleAgxAscReadInboxControl(Io, &Session->InboxControlAtFailure) ==
      AppleAgxAscResultOk)
    Session->InboxAtFailureValid = APPLE_AGX_RTKIT_TRUE;
  if (AppleAgxAscReadOutboxControl(Io, &Session->OutboxControlAtFailure) ==
      AppleAgxAscResultOk)
    Session->OutboxAtFailureValid = APPLE_AGX_RTKIT_TRUE;
}

APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionBoot(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_ASC_MESSAGE message;
  APPLE_AGX_RTKIT_BOOT_OUTPUT output;
  APPLE_AGX_RTKIT_BOOT_RESULT boot_result;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  APPLE_AGX_ASC_U32 cpu_status = 0u;

  if (Session == APPLE_AGX_RTKIT_SESSION_NULL ||
      Io == APPLE_AGX_RTKIT_SESSION_NULL)
    return AppleAgxRtkitSessionResultInvalidArgument;
  if (Session->Running != APPLE_AGX_RTKIT_FALSE)
    return AppleAgxRtkitSessionResultInvalidState;

  result = AppleAgxRtkitSessionAscResult(
      AppleAgxAscSetRun(Io, APPLE_AGX_ASC_TRUE));
  if (result != AppleAgxRtkitSessionResultOk)
    return result;
  Session->Running = APPLE_AGX_RTKIT_TRUE;
  result = AppleAgxRtkitSessionAscResult(
      AppleAgxAscWaitRunning(Io, DeadlineMs, &cpu_status));
  if (result != AppleAgxRtkitSessionResultOk)
    return AppleAgxRtkitSessionForceRunOff(Session, Io, result);
  Session->CpuReady = APPLE_AGX_RTKIT_TRUE;
  AppleAgxRtkitBootInitialize(&Session->Boot);
  boot_result = AppleAgxRtkitBootBegin(&Session->Boot, &output);
  if (boot_result != AppleAgxRtkitBootResultOk)
    return AppleAgxRtkitSessionForceRunOff(
        Session, Io, AppleAgxRtkitSessionResultInvalidState);
  if (AppleAgxAscReadInboxControl(Io, &Session->InboxControlBeforeInit) ==
      AppleAgxAscResultOk)
    Session->InboxBeforeInitValid = APPLE_AGX_RTKIT_TRUE;
  result = AppleAgxRtkitSessionSendOutput(Io, &output, DeadlineMs);
  if (result != AppleAgxRtkitSessionResultOk)
    return AppleAgxRtkitSessionForceRunOff(Session, Io, result);
  if (AppleAgxAscReadInboxControl(Io, &Session->InboxControlAfterInit) ==
      AppleAgxAscResultOk)
    Session->InboxAfterInitValid = APPLE_AGX_RTKIT_TRUE;

  while (AppleAgxRtkitBootIsReady(&Session->Boot) ==
         APPLE_AGX_RTKIT_FALSE) {
    result = AppleAgxRtkitSessionAscResult(
        AppleAgxAscReceive(Io, &message, DeadlineMs));
    if (result != AppleAgxRtkitSessionResultOk) {
      AppleAgxRtkitSessionCaptureFailureMailbox(Session, Io);
      return AppleAgxRtkitSessionForceRunOff(Session, Io, result);
    }
    boot_result = AppleAgxRtkitBootHandle(
        &Session->Boot, message.Payload, message.Endpoint, &output);
    if (boot_result != AppleAgxRtkitBootResultOk)
      return AppleAgxRtkitSessionForceRunOff(
          Session, Io, AppleAgxRtkitSessionResultProtocolViolation);
    result = AppleAgxRtkitSessionSendOutput(Io, &output, DeadlineMs);
    if (result != AppleAgxRtkitSessionResultOk)
      return AppleAgxRtkitSessionForceRunOff(Session, Io, result);
  }
  return AppleAgxRtkitSessionResultOk;
}

static APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionWaitPower(
    const APPLE_AGX_ASC_IO *Io, APPLE_AGX_RTKIT_MANAGEMENT_TYPE Type,
    APPLE_AGX_RTKIT_U32 State, APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_ASC_MESSAGE message;
  APPLE_AGX_RTKIT_MANAGEMENT decoded;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  for (;;) {
    result = AppleAgxRtkitSessionAscResult(
        AppleAgxAscReceive(Io, &message, DeadlineMs));
    if (result != AppleAgxRtkitSessionResultOk)
      return result;
    if (message.Endpoint != 0u ||
        AppleAgxRtkitDecodeManagement(message.Payload, &decoded) ==
            APPLE_AGX_RTKIT_FALSE)
      return AppleAgxRtkitSessionResultProtocolViolation;
    if (decoded.Type == Type && decoded.State == State)
      return AppleAgxRtkitSessionResultOk;
    return AppleAgxRtkitSessionResultProtocolViolation;
  }
}

APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionStop(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  APPLE_AGX_RTKIT_SESSION_RESULT cleanup_result;

  if (Session == APPLE_AGX_RTKIT_SESSION_NULL ||
      Io == APPLE_AGX_RTKIT_SESSION_NULL)
    return AppleAgxRtkitSessionResultInvalidArgument;
  if (Session->Running == APPLE_AGX_RTKIT_FALSE ||
      !AppleAgxRtkitBootIsReady(&Session->Boot))
    return AppleAgxRtkitSessionResultInvalidState;

  result = AppleAgxRtkitSessionAscResult(
      AppleAgxAscSend(Io, AppleAgxRtkitSetApPower(0x10u), 0u, DeadlineMs));
  if (result == AppleAgxRtkitSessionResultOk)
    result = AppleAgxRtkitSessionWaitPower(
        Io, AppleAgxRtkitManagementSetApPower, 0x10u, DeadlineMs);
  if (result == AppleAgxRtkitSessionResultOk)
    result = AppleAgxRtkitSessionAscResult(
        AppleAgxAscSend(Io, AppleAgxRtkitSetIopPower(0x10u), 0u, DeadlineMs));
  if (result == AppleAgxRtkitSessionResultOk)
    result = AppleAgxRtkitSessionWaitPower(
        Io, AppleAgxRtkitManagementIopPowerAck, 0x10u, DeadlineMs);

  cleanup_result = AppleAgxRtkitSessionAscResult(
      AppleAgxAscSetRun(Io, APPLE_AGX_ASC_FALSE));
  Session->Running = APPLE_AGX_RTKIT_FALSE;
  AppleAgxRtkitBootInitialize(&Session->Boot);
  if (cleanup_result != AppleAgxRtkitSessionResultOk)
    return AppleAgxRtkitSessionResultCleanupFailed;
  return result;
}
