#include "apple_agx_firmware.h"

#include "apple_agx_rtkit.h"

#define APPLE_AGX_FW_NULL ((void *)0)
#define APPLE_AGX_FW_U64_MAX (~0ULL)
#define APPLE_AGX_FIRMWARE_OBSERVATION_BITS                                \
  (APPLE_AGX_FIRMWARE_INITDATA_SENT | APPLE_AGX_FIRMWARE_DEVICE_CONTROL |  \
   APPLE_AGX_FIRMWARE_IDLE_TIMESTAMP)

static void AppleAgxFirmwareRecord(const APPLE_AGX_FIRMWARE *Firmware,
                                   const APPLE_AGX_FIRMWARE_IO *Io) {
  if (Firmware != APPLE_AGX_FW_NULL && Io != APPLE_AGX_FW_NULL &&
      Io->RecordPhase != APPLE_AGX_FW_NULL)
    Io->RecordPhase(Io->Context, Firmware->Phase, Firmware->LastResult,
                    Firmware->CompletedMask);
}

static APPLE_AGX_FW_BOOL
AppleAgxFirmwareIoValid(const APPLE_AGX_FIRMWARE_IO *Io) {
  return Io != APPLE_AGX_FW_NULL && Io->NowMs != APPLE_AGX_FW_NULL &&
         Io->PowerOn != APPLE_AGX_FW_NULL &&
         Io->CreateFirmwareUat != APPLE_AGX_FW_NULL &&
         Io->BootAsc != APPLE_AGX_FW_NULL &&
         Io->StartEndpoint != APPLE_AGX_FW_NULL &&
         Io->PublishInitdata != APPLE_AGX_FW_NULL &&
         Io->SendInitdata != APPLE_AGX_FW_NULL &&
         Io->SendDeviceControlInit != APPLE_AGX_FW_NULL &&
         Io->UpdateIdleTimestamp != APPLE_AGX_FW_NULL &&
         Io->UnpublishInitdata != APPLE_AGX_FW_NULL &&
         Io->StopEndpoint != APPLE_AGX_FW_NULL &&
         Io->StopAsc != APPLE_AGX_FW_NULL &&
         Io->DestroyFirmwareUat != APPLE_AGX_FW_NULL &&
         Io->PowerOff != APPLE_AGX_FW_NULL;
}

static APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareBeginDeadline(
    const APPLE_AGX_FIRMWARE_IO *Io, APPLE_AGX_FW_U32 TimeoutMs,
    APPLE_AGX_FW_U64 *StartMs, APPLE_AGX_FW_U64 *DeadlineMs) {
  *StartMs = Io->NowMs(Io->Context);
  if (*StartMs > APPLE_AGX_FW_U64_MAX - TimeoutMs)
    return AppleAgxFirmwareResultDeadlineOverflow;
  *DeadlineMs = *StartMs + TimeoutMs;
  return AppleAgxFirmwareResultOk;
}

static APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareFinishOperation(
    const APPLE_AGX_FIRMWARE_IO *Io, APPLE_AGX_FW_U64 StartMs,
    APPLE_AGX_FW_U64 DeadlineMs, APPLE_AGX_FW_BOOL TransportResult) {
  APPLE_AGX_FW_U64 end_ms = Io->NowMs(Io->Context);

  if (end_ms < StartMs)
    return AppleAgxFirmwareResultClockRegression;
  if (end_ms > DeadlineMs)
    return AppleAgxFirmwareResultTimeout;
  if (!TransportResult)
    return AppleAgxFirmwareResultTransportFailed;
  return AppleAgxFirmwareResultOk;
}

static void AppleAgxFirmwareComplete(APPLE_AGX_FIRMWARE *Firmware,
                                     const APPLE_AGX_FIRMWARE_IO *Io,
                                     APPLE_AGX_FIRMWARE_PHASE Phase,
                                     APPLE_AGX_FW_U32 Bit) {
  Firmware->CompletedMask |= Bit;
  Firmware->Phase = Phase;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
  AppleAgxFirmwareRecord(Firmware, Io);
}

/* Warm: a failed warm start goes back to the warm-stopped state and keeps
 * the UAT and the power domain; the cold start rolls everything back. */
static APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareAbortStart(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io,
    APPLE_AGX_FIRMWARE_RESULT Failure, APPLE_AGX_FW_BOOL Warm) {
  APPLE_AGX_FIRMWARE_RESULT rollback;

  AppleAgxFirmwareFail(Firmware, Failure);
  AppleAgxFirmwareRecord(Firmware, Io);
  rollback = Warm ? AppleAgxFirmwareStopWarm(Firmware, Io)
                  : AppleAgxFirmwareRollback(Firmware, Io);
  if (rollback != AppleAgxFirmwareResultOk)
    return rollback;
  Firmware->LastResult = Failure;
  AppleAgxFirmwareRecord(Firmware, Io);
  return Failure;
}

static APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareValidateStart(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  if (Firmware == APPLE_AGX_FW_NULL || !AppleAgxFirmwareIoValid(Io)) {
    if (Firmware != APPLE_AGX_FW_NULL) {
      AppleAgxFirmwareFail(Firmware, AppleAgxFirmwareResultInvalid);
      AppleAgxFirmwareRecord(Firmware, Io);
    }
    return AppleAgxFirmwareResultInvalid;
  }
  if ((Firmware->Phase != AppleAgxFirmwareOff &&
       Firmware->Phase != AppleAgxFirmwareStopped) ||
      Firmware->CompletedMask != 0 || Firmware->CleanupMask != 0)
    return AppleAgxFirmwareResultInvalid;
  Firmware->Phase = AppleAgxFirmwareOff;
  Firmware->InitdataAddress = 0;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
  return AppleAgxFirmwareResultOk;
}

void AppleAgxFirmwareInitialize(APPLE_AGX_FIRMWARE *Firmware) {
  if (Firmware == APPLE_AGX_FW_NULL)
    return;
  Firmware->Phase = AppleAgxFirmwareOff;
  Firmware->CompletedMask = 0;
  Firmware->CleanupMask = 0;
  Firmware->InitdataAddress = 0;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
}

void AppleAgxFirmwareFail(APPLE_AGX_FIRMWARE *Firmware,
                          APPLE_AGX_FIRMWARE_RESULT Result) {
  if (Firmware == APPLE_AGX_FW_NULL)
    return;
  Firmware->Phase = AppleAgxFirmwareFailed;
  Firmware->LastResult = Result == AppleAgxFirmwareResultOk
                             ? AppleAgxFirmwareResultInvalid
                             : Result;
}

#define APPLE_AGX_START_SIMPLE(Callback, Timeout, Phase, Bit)               \
  do {                                                                      \
    result = AppleAgxFirmwareBeginDeadline(Io, (Timeout), &start_ms,         \
                                           &deadline_ms);                    \
    if (result == AppleAgxFirmwareResultOk)                                 \
      result = AppleAgxFirmwareFinishOperation(                             \
          Io, start_ms, deadline_ms, (Callback)(Io->Context, deadline_ms));  \
    if (result != AppleAgxFirmwareResultOk)                                 \
      return AppleAgxFirmwareAbortStart(Firmware, Io, result, Warm);        \
    AppleAgxFirmwareComplete(Firmware, Io, (Phase), (Bit));                 \
  } while (0)

/* Everything a start does once the domain is powered and the UAT exists.
 * Shared by the cold start and the warm restart so that both replay the
 * same phases in the same order with the same deadlines. */
static APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareBootFromUatReady(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io,
    APPLE_AGX_FW_BOOL Warm) {
  APPLE_AGX_FIRMWARE_RESULT result;
  APPLE_AGX_FW_U64 start_ms;
  APPLE_AGX_FW_U64 deadline_ms;
  APPLE_AGX_FW_BOOL transport_result;

  /* Boot may acquire CPU/root ownership before a later primitive fails. */
  Firmware->CleanupMask |= APPLE_AGX_FIRMWARE_ASC_RUNNING;
  APPLE_AGX_START_SIMPLE(Io->BootAsc, J313_AGX_G2_ASC_BOOT_TIMEOUT_MS,
                         AppleAgxFirmwareAscRunning,
                         APPLE_AGX_FIRMWARE_ASC_RUNNING);

  result = AppleAgxFirmwareBeginDeadline(
      Io, J313_AGX_G2_ENDPOINT_TIMEOUT_MS, &start_ms, &deadline_ms);
  if (result == AppleAgxFirmwareResultOk)
    result = AppleAgxFirmwareFinishOperation(
        Io, start_ms, deadline_ms,
        Io->StartEndpoint(Io->Context, J313_AGX_G2_FIRMWARE_ENDPOINT,
                          deadline_ms));
  if (result != AppleAgxFirmwareResultOk)
    return AppleAgxFirmwareAbortStart(Firmware, Io, result, Warm);
  AppleAgxFirmwareComplete(Firmware, Io, AppleAgxFirmwareEndpointStarted,
                           APPLE_AGX_FIRMWARE_ENDPOINT);

  result = AppleAgxFirmwareBeginDeadline(
      Io, J313_AGX_G2_ENDPOINT_TIMEOUT_MS, &start_ms, &deadline_ms);
  if (result == AppleAgxFirmwareResultOk)
    result = AppleAgxFirmwareFinishOperation(
        Io, start_ms, deadline_ms,
        Io->StartEndpoint(Io->Context, J313_AGX_G2_DOORBELL_ENDPOINT,
                          deadline_ms));
  if (result != AppleAgxFirmwareResultOk)
    return AppleAgxFirmwareAbortStart(Firmware, Io, result, Warm);
  AppleAgxFirmwareComplete(Firmware, Io,
                           AppleAgxDoorbellEndpointStarted,
                           APPLE_AGX_FIRMWARE_DOORBELL_ENDPOINT);

  result = AppleAgxFirmwareBeginDeadline(
      Io, J313_AGX_G2_INITDATA_TIMEOUT_MS, &start_ms, &deadline_ms);
  if (result == AppleAgxFirmwareResultOk) {
    Firmware->InitdataAddress = 0;
    transport_result = Io->PublishInitdata(
        Io->Context, deadline_ms, &Firmware->InitdataAddress);
    result = AppleAgxFirmwareFinishOperation(Io, start_ms, deadline_ms,
                                             transport_result);
    if (result == AppleAgxFirmwareResultOk &&
        (Firmware->InitdataAddress == 0 ||
         AppleAgxRtkitInitdata(Firmware->InitdataAddress) ==
             APPLE_AGX_RTKIT_INVALID_MESSAGE))
      result = AppleAgxFirmwareResultInvalid;
  }
  if (result != AppleAgxFirmwareResultOk)
    return AppleAgxFirmwareAbortStart(Firmware, Io, result, Warm);
  AppleAgxFirmwareComplete(Firmware, Io,
                           AppleAgxFirmwareInitdataPublished,
                           APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED);

  result = AppleAgxFirmwareBeginDeadline(
      Io, J313_AGX_G2_INITDATA_TIMEOUT_MS, &start_ms, &deadline_ms);
  if (result == AppleAgxFirmwareResultOk)
    result = AppleAgxFirmwareFinishOperation(
        Io, start_ms, deadline_ms,
        Io->SendInitdata(Io->Context, Firmware->InitdataAddress,
                         deadline_ms));
  if (result != AppleAgxFirmwareResultOk)
    return AppleAgxFirmwareAbortStart(Firmware, Io, result, Warm);
  AppleAgxFirmwareComplete(Firmware, Io, AppleAgxFirmwareInitdataSent,
                           APPLE_AGX_FIRMWARE_INITDATA_SENT);

  APPLE_AGX_START_SIMPLE(Io->SendDeviceControlInit,
                         J313_AGX_G2_INITDATA_TIMEOUT_MS,
                         AppleAgxFirmwareDeviceControlInitialized,
                         APPLE_AGX_FIRMWARE_DEVICE_CONTROL);
  APPLE_AGX_START_SIMPLE(Io->UpdateIdleTimestamp,
                         J313_AGX_G2_INITDATA_TIMEOUT_MS,
                         AppleAgxFirmwareIdleTimestampUpdated,
                         APPLE_AGX_FIRMWARE_IDLE_TIMESTAMP);
  return AppleAgxFirmwareResultOk;
}

APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStart(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  const APPLE_AGX_FW_BOOL Warm = APPLE_AGX_FW_FALSE;
  APPLE_AGX_FIRMWARE_RESULT result;
  APPLE_AGX_FW_U64 start_ms;
  APPLE_AGX_FW_U64 deadline_ms;

  result = AppleAgxFirmwareValidateStart(Firmware, Io);
  if (result != AppleAgxFirmwareResultOk)
    return result;

  APPLE_AGX_START_SIMPLE(Io->PowerOn, J313_AGX_G2_INITDATA_TIMEOUT_MS,
                         AppleAgxFirmwarePowered,
                         APPLE_AGX_FIRMWARE_POWERED);
  APPLE_AGX_START_SIMPLE(Io->CreateFirmwareUat,
                         J313_AGX_G2_INITDATA_TIMEOUT_MS,
                         AppleAgxFirmwareUatReady,
                         APPLE_AGX_FIRMWARE_UAT_READY);
  return AppleAgxFirmwareBootFromUatReady(Firmware, Io, Warm);
}

APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStartWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  APPLE_AGX_FIRMWARE_RESULT result;
  APPLE_AGX_FW_U64 start_ms;
  APPLE_AGX_FW_U64 deadline_ms;

  /* Refuse without touching state or hardware: the caller still holds a
   * cold or a half-stopped firmware and decides what to do with it. */
  if (Firmware == APPLE_AGX_FW_NULL || !AppleAgxFirmwareIoValid(Io) ||
      Io->RevalidateFirmwareUat == APPLE_AGX_FW_NULL ||
      Firmware->Phase != AppleAgxFirmwareUatReady ||
      Firmware->CompletedMask != APPLE_AGX_FIRMWARE_WARM_RETAINED ||
      Firmware->CleanupMask != 0)
    return AppleAgxFirmwareResultInvalid;
  Firmware->InitdataAddress = 0;
  Firmware->LastResult = AppleAgxFirmwareResultOk;

  result = AppleAgxFirmwareBeginDeadline(
      Io, J313_AGX_G2_INITDATA_TIMEOUT_MS, &start_ms, &deadline_ms);
  if (result == AppleAgxFirmwareResultOk)
    result = AppleAgxFirmwareFinishOperation(
        Io, start_ms, deadline_ms,
        Io->RevalidateFirmwareUat(Io->Context, deadline_ms));
  if (result != AppleAgxFirmwareResultOk) {
    /* Nothing was started: stay warm-stopped and report the failure. */
    Firmware->LastResult = result;
    AppleAgxFirmwareRecord(Firmware, Io);
    return result;
  }
  AppleAgxFirmwareRecord(Firmware, Io);
  return AppleAgxFirmwareBootFromUatReady(Firmware, Io, APPLE_AGX_FW_TRUE);
}

#undef APPLE_AGX_START_SIMPLE

static APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareCleanupFinish(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io,
    APPLE_AGX_FW_U32 Bit, APPLE_AGX_FW_U64 StartMs,
    APPLE_AGX_FW_U64 DeadlineMs, APPLE_AGX_FW_BOOL TransportResult) {
  APPLE_AGX_FIRMWARE_RESULT result = AppleAgxFirmwareFinishOperation(
      Io, StartMs, DeadlineMs, TransportResult);
  if (result == AppleAgxFirmwareResultOk) {
    Firmware->CompletedMask &= ~Bit;
    Firmware->CleanupMask &= ~Bit;
  } else
    Firmware->LastResult = AppleAgxFirmwareResultCleanupFailed;
  AppleAgxFirmwareRecord(Firmware, Io);
  return result;
}

#define APPLE_AGX_CLEANUP_SIMPLE(Bit, CallbackExpression)                  \
  do {                                                                     \
    if (((Firmware->CompletedMask | Firmware->CleanupMask) & (Bit)) != 0) { \
      result = AppleAgxFirmwareBeginDeadline(                              \
          Io, J313_AGX_G2_STOP_TIMEOUT_MS, &start_ms, &deadline_ms);       \
      if (result == AppleAgxFirmwareResultOk)                              \
        result = AppleAgxFirmwareCleanupFinish(                            \
            Firmware, Io, (Bit), start_ms, deadline_ms,                   \
            (CallbackExpression));                                        \
      if (result != AppleAgxFirmwareResultOk)                              \
        cleanup_failed = APPLE_AGX_FW_TRUE;                               \
    }                                                                      \
  } while (0)

APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareRollback(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  APPLE_AGX_FW_U64 start_ms;
  APPLE_AGX_FW_U64 deadline_ms;
  APPLE_AGX_FIRMWARE_RESULT result;
  APPLE_AGX_FW_BOOL cleanup_failed = APPLE_AGX_FW_FALSE;

  if (Firmware == APPLE_AGX_FW_NULL || !AppleAgxFirmwareIoValid(Io)) {
    if (Firmware != APPLE_AGX_FW_NULL)
      AppleAgxFirmwareFail(Firmware, AppleAgxFirmwareResultInvalid);
    return AppleAgxFirmwareResultInvalid;
  }
  if ((Firmware->CompletedMask & ~APPLE_AGX_FIRMWARE_ALL_COMPLETED) != 0 ||
      (Firmware->CleanupMask & ~APPLE_AGX_FIRMWARE_ASC_RUNNING) != 0) {
    AppleAgxFirmwareFail(Firmware, AppleAgxFirmwareResultInvalid);
    AppleAgxFirmwareRecord(Firmware, Io);
    return AppleAgxFirmwareResultInvalid;
  }
  if (Firmware->CompletedMask == 0 && Firmware->CleanupMask == 0) {
    Firmware->Phase = AppleAgxFirmwareStopped;
    Firmware->LastResult = AppleAgxFirmwareResultOk;
    AppleAgxFirmwareRecord(Firmware, Io);
    return AppleAgxFirmwareResultOk;
  }

  Firmware->Phase = AppleAgxFirmwareRollingBack;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
  Firmware->CompletedMask &= ~APPLE_AGX_FIRMWARE_OBSERVATION_BITS;
  AppleAgxFirmwareRecord(Firmware, Io);

  APPLE_AGX_CLEANUP_SIMPLE(
      APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED,
      Io->UnpublishInitdata(Io->Context, deadline_ms));
  if ((Firmware->CompletedMask & APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED) ==
      0)
    Firmware->InitdataAddress = 0;
  APPLE_AGX_CLEANUP_SIMPLE(
      APPLE_AGX_FIRMWARE_DOORBELL_ENDPOINT,
      Io->StopEndpoint(Io->Context, J313_AGX_G2_DOORBELL_ENDPOINT,
                       deadline_ms));
  APPLE_AGX_CLEANUP_SIMPLE(
      APPLE_AGX_FIRMWARE_ENDPOINT,
      Io->StopEndpoint(Io->Context, J313_AGX_G2_FIRMWARE_ENDPOINT,
                       deadline_ms));
  APPLE_AGX_CLEANUP_SIMPLE(APPLE_AGX_FIRMWARE_ASC_RUNNING,
                           Io->StopAsc(Io->Context, deadline_ms));
  APPLE_AGX_CLEANUP_SIMPLE(
      APPLE_AGX_FIRMWARE_UAT_READY,
      Io->DestroyFirmwareUat(Io->Context, deadline_ms));
  APPLE_AGX_CLEANUP_SIMPLE(APPLE_AGX_FIRMWARE_POWERED,
                           Io->PowerOff(Io->Context, deadline_ms));

  if (cleanup_failed || Firmware->CompletedMask != 0 ||
      Firmware->CleanupMask != 0) {
    Firmware->Phase = AppleAgxFirmwareFailed;
    Firmware->LastResult = AppleAgxFirmwareResultCleanupFailed;
    AppleAgxFirmwareRecord(Firmware, Io);
    return AppleAgxFirmwareResultCleanupFailed;
  }
  Firmware->Phase = AppleAgxFirmwareStopped;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
  AppleAgxFirmwareRecord(Firmware, Io);
  return AppleAgxFirmwareResultOk;
}

static APPLE_AGX_FW_BOOL AppleAgxFirmwareWarmStopValid(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  /* Warm needs both retained resources and no unknown bits.  Refusing here
   * changes nothing: a cold AppleAgxFirmwareRollback stays possible. */
  return Firmware != APPLE_AGX_FW_NULL && AppleAgxFirmwareIoValid(Io) &&
         (Firmware->CompletedMask & ~APPLE_AGX_FIRMWARE_ALL_COMPLETED) == 0 &&
         (Firmware->CleanupMask & ~APPLE_AGX_FIRMWARE_ASC_RUNNING) == 0 &&
         (Firmware->CompletedMask & APPLE_AGX_FIRMWARE_WARM_RETAINED) ==
             APPLE_AGX_FIRMWARE_WARM_RETAINED;
}

static APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareWarmStopFinish(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io,
    APPLE_AGX_FW_BOOL CleanupFailed) {
  if (CleanupFailed ||
      Firmware->CompletedMask != APPLE_AGX_FIRMWARE_WARM_RETAINED ||
      Firmware->CleanupMask != 0) {
    Firmware->Phase = AppleAgxFirmwareFailed;
    Firmware->LastResult = AppleAgxFirmwareResultCleanupFailed;
    AppleAgxFirmwareRecord(Firmware, Io);
    return AppleAgxFirmwareResultCleanupFailed;
  }
  Firmware->Phase = AppleAgxFirmwareUatReady;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
  AppleAgxFirmwareRecord(Firmware, Io);
  return AppleAgxFirmwareResultOk;
}

APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStopWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  APPLE_AGX_FW_U64 start_ms;
  APPLE_AGX_FW_U64 deadline_ms;
  APPLE_AGX_FIRMWARE_RESULT result;
  APPLE_AGX_FW_BOOL cleanup_failed = APPLE_AGX_FW_FALSE;

  if (!AppleAgxFirmwareWarmStopValid(Firmware, Io))
    return AppleAgxFirmwareResultInvalid;
  if (Firmware->CompletedMask == APPLE_AGX_FIRMWARE_WARM_RETAINED &&
      Firmware->CleanupMask == 0)
    return AppleAgxFirmwareWarmStopFinish(Firmware, Io, APPLE_AGX_FW_FALSE);

  Firmware->Phase = AppleAgxFirmwareRollingBack;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
  Firmware->CompletedMask &= ~APPLE_AGX_FIRMWARE_OBSERVATION_BITS;
  AppleAgxFirmwareRecord(Firmware, Io);

  /* Same order as AppleAgxFirmwareRollback, ending after StopAsc. */
  APPLE_AGX_CLEANUP_SIMPLE(
      APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED,
      Io->UnpublishInitdata(Io->Context, deadline_ms));
  if ((Firmware->CompletedMask & APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED) ==
      0)
    Firmware->InitdataAddress = 0;
  APPLE_AGX_CLEANUP_SIMPLE(
      APPLE_AGX_FIRMWARE_DOORBELL_ENDPOINT,
      Io->StopEndpoint(Io->Context, J313_AGX_G2_DOORBELL_ENDPOINT,
                       deadline_ms));
  APPLE_AGX_CLEANUP_SIMPLE(
      APPLE_AGX_FIRMWARE_ENDPOINT,
      Io->StopEndpoint(Io->Context, J313_AGX_G2_FIRMWARE_ENDPOINT,
                       deadline_ms));
  APPLE_AGX_CLEANUP_SIMPLE(APPLE_AGX_FIRMWARE_ASC_RUNNING,
                           Io->StopAsc(Io->Context, deadline_ms));
  return AppleAgxFirmwareWarmStopFinish(Firmware, Io, cleanup_failed);
}

APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareForceStopWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  const APPLE_AGX_FW_U32 session_bits =
      APPLE_AGX_FIRMWARE_ASC_RUNNING | APPLE_AGX_FIRMWARE_ENDPOINT |
      APPLE_AGX_FIRMWARE_DOORBELL_ENDPOINT |
      APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED |
      APPLE_AGX_FIRMWARE_OBSERVATION_BITS;
  APPLE_AGX_FW_U64 start_ms;
  APPLE_AGX_FW_U64 deadline_ms;
  APPLE_AGX_FIRMWARE_RESULT result;

  if (!AppleAgxFirmwareWarmStopValid(Firmware, Io) ||
      Io->ForceStopAsc == APPLE_AGX_FW_NULL)
    return AppleAgxFirmwareResultInvalid;

  Firmware->Phase = AppleAgxFirmwareRollingBack;
  Firmware->LastResult = AppleAgxFirmwareResultOk;
  AppleAgxFirmwareRecord(Firmware, Io);

  /* Called even when no session bit is set: the caller asks for proof that
   * the CPU is stopped, not for bookkeeping. */
  result = AppleAgxFirmwareBeginDeadline(
      Io, J313_AGX_G2_STOP_TIMEOUT_MS, &start_ms, &deadline_ms);
  if (result == AppleAgxFirmwareResultOk)
    result = AppleAgxFirmwareFinishOperation(
        Io, start_ms, deadline_ms,
        Io->ForceStopAsc(Io->Context, deadline_ms));
  if (result != AppleAgxFirmwareResultOk)
    return AppleAgxFirmwareWarmStopFinish(Firmware, Io, APPLE_AGX_FW_TRUE);
  Firmware->CompletedMask &= ~session_bits;
  Firmware->CleanupMask &= ~session_bits;
  Firmware->InitdataAddress = 0;
  return AppleAgxFirmwareWarmStopFinish(Firmware, Io, APPLE_AGX_FW_FALSE);
}

#undef APPLE_AGX_CLEANUP_SIMPLE

APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareRestartWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io) {
  APPLE_AGX_FIRMWARE_RESULT result = AppleAgxFirmwareStopWarm(Firmware, Io);
  if (result != AppleAgxFirmwareResultOk)
    return result;
  return AppleAgxFirmwareStartWarm(Firmware, Io);
}
