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
  /* Borrowed from an already context0-mapped owner; never a physical address. */
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
APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionStop(
    APPLE_AGX_RTKIT_SESSION *Session, const APPLE_AGX_ASC_IO *Io,
    APPLE_AGX_ASC_U64 DeadlineMs);

#endif /* APPLE_AGX_RTKIT_SESSION_H */
