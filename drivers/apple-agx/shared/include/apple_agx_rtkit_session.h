#ifndef APPLE_AGX_RTKIT_SESSION_H
#define APPLE_AGX_RTKIT_SESSION_H

#include "apple_agx_asc_transport.h"
#include "apple_agx_gfx_handoff.h"
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
  AppleAgxRtkitSessionResultFirmwareCrashed,
} APPLE_AGX_RTKIT_SESSION_RESULT;

typedef enum _APPLE_AGX_RTKIT_STOP_PHASE {
  AppleAgxRtkitStopIdle = 0,
  AppleAgxRtkitStopApRequested,
  AppleAgxRtkitStopApAcknowledged,
  AppleAgxRtkitStopIopRequested,
  AppleAgxRtkitStopIopAcknowledged,
  AppleAgxRtkitStopComplete,
} APPLE_AGX_RTKIT_STOP_PHASE;

typedef struct _APPLE_AGX_RTKIT_SESSION {
  APPLE_AGX_RTKIT_BOOT Boot;
  APPLE_AGX_RTKIT_BOOL Running;
  APPLE_AGX_RTKIT_BOOL CpuReady;
  APPLE_AGX_RTKIT_BOOL InboxBeforeInitValid;
  APPLE_AGX_RTKIT_BOOL InboxAfterInitValid;
  APPLE_AGX_RTKIT_BOOL InboxAtFailureValid;
  APPLE_AGX_RTKIT_BOOL OutboxAtFailureValid;
  APPLE_AGX_RTKIT_STOP_PHASE StopPhase;
  APPLE_AGX_RTKIT_U32 InboxControlBeforeInit;
  APPLE_AGX_RTKIT_U32 InboxControlAfterInit;
  APPLE_AGX_RTKIT_U32 InboxControlAtFailure;
  APPLE_AGX_RTKIT_U32 OutboxControlAtFailure;
  APPLE_AGX_RTKIT_U32 ReceivedCount;
  APPLE_AGX_RTKIT_U32 LastRxEndpoint;
  APPLE_AGX_RTKIT_U64 LastRxPayload;
  /* 44-bit wire encoding of a mapped kernel-VM owner; never a physical PA. */
  APPLE_AGX_RTKIT_U64 CrashlogGpuAddress;
  APPLE_AGX_RTKIT_U32 CrashlogCapacityBytes;
  APPLE_AGX_RTKIT_U32 CrashlogRequestedBytes;
  APPLE_AGX_RTKIT_BOOL CrashlogReplySent;
  APPLE_AGX_RTKIT_BOOL CrashlogCrashed;
} APPLE_AGX_RTKIT_SESSION;

/* Called only after CPU_READY and handoff initialization, before HELLO. */
typedef APPLE_AGX_RTKIT_BOOL (*APPLE_AGX_RTKIT_PRE_MANAGEMENT)(
    void *Context, APPLE_AGX_ASC_U64 DeadlineMs);

void AppleAgxRtkitSessionInitialize(APPLE_AGX_RTKIT_SESSION *Session);
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionStartCpuAndInitializeHandoff(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_GFX_HANDOFF_STATE *Handoff, APPLE_AGX_ASC_U64 DeadlineMs);
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionCompleteManagementBootstrap(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs);
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionBoot(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_GFX_HANDOFF_STATE *Handoff,
    APPLE_AGX_RTKIT_PRE_MANAGEMENT Prepare, void *PrepareContext,
    APPLE_AGX_ASC_U64 DeadlineMs);
#define APPLE_AGX_RTKIT_RUNTIME_DRAIN_LIMIT 64u
/* Runtime mailbox ownership stays with the platform worker, even without ping. */
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionDrainRuntime(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_RTKIT_U32 MaxMessages, APPLE_AGX_RTKIT_U32 *Drained);

APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionHeartbeat(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs);
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionStop(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs);

/*
 * Firmware restart (TDR-FIRMWARE-RESTART-RECOVERY.md, stage 2).  Nothing on
 * an existing path calls these two.
 *
 * StopForRestart is the orderly stop of a booted session: the same AP 0x10 /
 * IOP 0x10 / RUN-off sequence as AppleAgxRtkitSessionStop, then CPU_CONTROL
 * is read back and RUN must read 0.  It never clears RUN on a firmware that
 * did not acknowledge: an ack timeout is returned with the stop phase left
 * where it stopped (resumable, CPU still running), and a session whose
 * management boot is not Ready is refused with InvalidState.  The caller
 * then either retries or calls AppleAgxRtkitSessionForceStop by name.
 *
 * ForceStop is for a firmware that does not answer.  It writes RUN off,
 * reads CPU_CONTROL back and, when RUN reads 0, moves the session to
 * Running = FALSE / StopComplete from any state.  It sends no RTKit message,
 * reads no clock and does not wait.  The crash latch and mailbox snapshots
 * are kept for the receipt; the next start clears the latch.
 *
 * After either returns Ok, AppleAgxRtkitSessionStartCpuAndInitializeHandoff
 * runs a complete second boot.
 */
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionStopForRestart(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs);
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionForceStop(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io);

#endif /* APPLE_AGX_RTKIT_SESSION_H */
