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
  if (stop_result == AppleAgxAscResultOk) {
    Session->Running = APPLE_AGX_RTKIT_FALSE;
    Session->StopPhase = AppleAgxRtkitStopComplete;
  }
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
  Session->ReceivedCount = 0u;
  Session->LastRxEndpoint = 0u;
  Session->LastRxPayload = 0u;
  Session->CrashlogGpuAddress = 0u;
  Session->CrashlogCapacityBytes = 0u;
  Session->CrashlogRequestedBytes = 0u;
  Session->CrashlogReplySent = APPLE_AGX_RTKIT_FALSE;
  Session->CrashlogCrashed = APPLE_AGX_RTKIT_FALSE;
  Session->StopPhase = AppleAgxRtkitStopIdle;
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

APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionStartCpuAndInitializeHandoff(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_GFX_HANDOFF_STATE *Handoff,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  APPLE_AGX_ASC_U32 cpu_status = 0u;

  if (Session == APPLE_AGX_RTKIT_SESSION_NULL ||
      Io == APPLE_AGX_RTKIT_SESSION_NULL)
    return AppleAgxRtkitSessionResultInvalidArgument;
  if (Session->Running != APPLE_AGX_RTKIT_FALSE)
    return AppleAgxRtkitSessionResultInvalidState;
  if (Session->StopPhase != AppleAgxRtkitStopIdle &&
      Session->StopPhase != AppleAgxRtkitStopComplete)
    return AppleAgxRtkitSessionResultInvalidState;
  Session->StopPhase = AppleAgxRtkitStopIdle;
  Session->CpuReady = APPLE_AGX_RTKIT_FALSE;
  Session->ReceivedCount = 0u;
  Session->LastRxEndpoint = 0u;
  Session->LastRxPayload = 0u;
  Session->CrashlogRequestedBytes = 0u;
  Session->CrashlogReplySent = APPLE_AGX_RTKIT_FALSE;
  Session->CrashlogCrashed = APPLE_AGX_RTKIT_FALSE;
  AppleAgxRtkitBootInitialize(&Session->Boot);

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
  /* Asahi initializes the firmware handoff after CPU_READY and before the
   * management HELLO exchange; firmware otherwise waits for this transition. */
  if (Handoff != APPLE_AGX_RTKIT_SESSION_NULL &&
      AppleAgxGfxHandoffInitialize(Handoff, DeadlineMs) !=
          AppleAgxGfxHandoffResultOk)
    return AppleAgxRtkitSessionForceRunOff(
        Session, Io, AppleAgxRtkitSessionResultTimeout);
  return AppleAgxRtkitSessionResultOk;
}

static APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionCrashlog(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_RTKIT_U64 Payload, APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_RTKIT_U64 reply;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  if ((Payload >> 52) != 1u)
    return AppleAgxRtkitSessionResultProtocolViolation;
  if (Session->CrashlogReplySent) {
    Session->CrashlogCrashed = APPLE_AGX_RTKIT_TRUE;
    return AppleAgxRtkitSessionResultFirmwareCrashed;
  }
  Session->CrashlogRequestedBytes =
      (APPLE_AGX_RTKIT_U32)((Payload >> 44) & 0xffu) << 12;
  /* Never adopt a firmware-supplied address or invent backing for a grant. */
  if ((Payload & ((1ULL << 44) - 1u)) != 0u ||
      Session->CrashlogRequestedBytes == 0u ||
      Session->CrashlogRequestedBytes > Session->CrashlogCapacityBytes ||
      Session->CrashlogGpuAddress <
          (1ULL << 44) - (1ULL << J313_AGX_G2_UAT_INPUT_ADDRESS_BITS) ||
      Session->CrashlogGpuAddress >= (1ULL << 44) ||
      (Session->CrashlogGpuAddress & 0x3fffu) != 0u ||
      Session->CrashlogCapacityBytes == 0u ||
      Session->CrashlogCapacityBytes > 0xff000u ||
      (Session->CrashlogCapacityBytes & 0x3fffu) != 0u ||
      Session->CrashlogCapacityBytes >
          (1ULL << 44) - Session->CrashlogGpuAddress)
    return AppleAgxRtkitSessionResultProtocolViolation;
  reply = (1ULL << 52) |
          ((APPLE_AGX_RTKIT_U64)(Session->CrashlogCapacityBytes >> 12) << 44) |
          Session->CrashlogGpuAddress;
  result = AppleAgxRtkitSessionAscResult(AppleAgxAscSend(Io, reply, 1u, DeadlineMs));
  if (result == AppleAgxRtkitSessionResultOk)
    Session->CrashlogReplySent = APPLE_AGX_RTKIT_TRUE;
  return result;
}

APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionCompleteManagementBootstrap(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_ASC_MESSAGE message;
  APPLE_AGX_RTKIT_BOOT_OUTPUT output;
  APPLE_AGX_RTKIT_BOOT_RESULT boot_result;
  APPLE_AGX_RTKIT_SESSION_RESULT result;
  if (Session == APPLE_AGX_RTKIT_SESSION_NULL ||
      Io == APPLE_AGX_RTKIT_SESSION_NULL)
    return AppleAgxRtkitSessionResultInvalidArgument;
  if (!Session->Running || !Session->CpuReady ||
      Session->StopPhase != AppleAgxRtkitStopIdle ||
      AppleAgxRtkitBootIsReady(&Session->Boot))
    return AppleAgxRtkitSessionResultInvalidState;
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
    ++Session->ReceivedCount;
    Session->LastRxEndpoint = message.Endpoint;
    Session->LastRxPayload = message.Payload;
    if (message.Endpoint == 1u) {
      result = AppleAgxRtkitSessionCrashlog(Session, Io, message.Payload,
                                            DeadlineMs);
      if (result != AppleAgxRtkitSessionResultOk) {
        Session->Boot.Phase = AppleAgxRtkitBootFailed;
        return AppleAgxRtkitSessionForceRunOff(Session, Io, result);
      }
      continue;
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

APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionBoot(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_GFX_HANDOFF_STATE *Handoff,
    APPLE_AGX_RTKIT_PRE_MANAGEMENT Prepare, void *PrepareContext,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_RTKIT_SESSION_RESULT result =
      AppleAgxRtkitSessionStartCpuAndInitializeHandoff(Session, Io, Handoff,
                                                       DeadlineMs);
  if (result != AppleAgxRtkitSessionResultOk)
    return result;
  if (Prepare != APPLE_AGX_RTKIT_SESSION_NULL &&
      !Prepare(PrepareContext, DeadlineMs))
    return AppleAgxRtkitSessionForceRunOff(
        Session, Io, AppleAgxRtkitSessionResultTransportFailed);
  return AppleAgxRtkitSessionCompleteManagementBootstrap(Session, Io, DeadlineMs);
}

APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionHeartbeat(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs) {
  APPLE_AGX_ASC_MESSAGE message;
  APPLE_AGX_RTKIT_MANAGEMENT decoded;
  APPLE_AGX_RTKIT_SESSION_RESULT result;

  if (Session == APPLE_AGX_RTKIT_SESSION_NULL ||
      Io == APPLE_AGX_RTKIT_SESSION_NULL)
    return AppleAgxRtkitSessionResultInvalidArgument;
  if (!Session->Running || !Session->CpuReady ||
      !AppleAgxRtkitBootIsReady(&Session->Boot) ||
      Session->StopPhase != AppleAgxRtkitStopIdle)
    return AppleAgxRtkitSessionResultInvalidState;

  result = AppleAgxRtkitSessionAscResult(
      AppleAgxAscSend(Io, AppleAgxRtkitPing(), 0u, DeadlineMs));
  if (result != AppleAgxRtkitSessionResultOk)
    return result;
  result = AppleAgxRtkitSessionAscResult(
      AppleAgxAscReceive(Io, &message, DeadlineMs));
  if (result != AppleAgxRtkitSessionResultOk) {
    AppleAgxRtkitSessionCaptureFailureMailbox(Session, Io);
    return result;
  }
  ++Session->ReceivedCount;
  Session->LastRxEndpoint = message.Endpoint;
  Session->LastRxPayload = message.Payload;
  if (message.Endpoint != 0u ||
      !AppleAgxRtkitDecodeManagement(message.Payload, &decoded) ||
      decoded.Type != AppleAgxRtkitManagementPong)
    return AppleAgxRtkitSessionResultProtocolViolation;
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

  if (Session == APPLE_AGX_RTKIT_SESSION_NULL ||
      Io == APPLE_AGX_RTKIT_SESSION_NULL)
    return AppleAgxRtkitSessionResultInvalidArgument;
  if (Session->Running == APPLE_AGX_RTKIT_FALSE)
    return AppleAgxRtkitSessionResultOk;
  if (!AppleAgxRtkitBootIsReady(&Session->Boot))
    return AppleAgxRtkitSessionForceRunOff(
        Session, Io, AppleAgxRtkitSessionResultOk);
  if (Session->StopPhase > AppleAgxRtkitStopIopAcknowledged)
    return AppleAgxRtkitSessionResultInvalidState;

  if (Session->StopPhase == AppleAgxRtkitStopIdle) {
    result = AppleAgxRtkitSessionAscResult(
        AppleAgxAscSend(Io, AppleAgxRtkitSetApPower(0x10u), 0u, DeadlineMs));
    if (result != AppleAgxRtkitSessionResultOk)
      return result;
    Session->StopPhase = AppleAgxRtkitStopApRequested;
  }
  if (Session->StopPhase == AppleAgxRtkitStopApRequested) {
    result = AppleAgxRtkitSessionWaitPower(
        Io, AppleAgxRtkitManagementSetApPower, 0x10u, DeadlineMs);
    if (result != AppleAgxRtkitSessionResultOk)
      return result;
    Session->StopPhase = AppleAgxRtkitStopApAcknowledged;
  }
  if (Session->StopPhase == AppleAgxRtkitStopApAcknowledged) {
    result = AppleAgxRtkitSessionAscResult(
        AppleAgxAscSend(Io, AppleAgxRtkitSetIopPower(0x10u), 0u, DeadlineMs));
    if (result != AppleAgxRtkitSessionResultOk)
      return result;
    Session->StopPhase = AppleAgxRtkitStopIopRequested;
  }
  if (Session->StopPhase == AppleAgxRtkitStopIopRequested) {
    result = AppleAgxRtkitSessionWaitPower(
        Io, AppleAgxRtkitManagementIopPowerAck, 0x10u, DeadlineMs);
    if (result != AppleAgxRtkitSessionResultOk)
      return result;
    Session->StopPhase = AppleAgxRtkitStopIopAcknowledged;
  }

  result = AppleAgxRtkitSessionAscResult(
      AppleAgxAscSetRun(Io, APPLE_AGX_ASC_FALSE));
  if (result != AppleAgxRtkitSessionResultOk)
    return AppleAgxRtkitSessionResultCleanupFailed;
  Session->Running = APPLE_AGX_RTKIT_FALSE;
  AppleAgxRtkitBootInitialize(&Session->Boot);
  Session->StopPhase = AppleAgxRtkitStopComplete;
  return AppleAgxRtkitSessionResultOk;
}
