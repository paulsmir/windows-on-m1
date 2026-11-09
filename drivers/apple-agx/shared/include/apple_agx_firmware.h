#ifndef APPLE_AGX_FIRMWARE_H
#define APPLE_AGX_FIRMWARE_H

#include "j313_agx_g2.generated.h"

/* Keep this coordinator usable in freestanding WDK C and host tests. */
typedef unsigned char APPLE_AGX_FW_BOOL;
typedef unsigned int APPLE_AGX_FW_U32;
typedef unsigned long long APPLE_AGX_FW_U64;

#define APPLE_AGX_FW_FALSE ((APPLE_AGX_FW_BOOL)0u)
#define APPLE_AGX_FW_TRUE ((APPLE_AGX_FW_BOOL)1u)

typedef enum _APPLE_AGX_FIRMWARE_PHASE {
  AppleAgxFirmwareOff = 0,
  AppleAgxFirmwarePowered,
  AppleAgxFirmwareUatReady,
  AppleAgxFirmwareAscRunning,
  AppleAgxFirmwareEndpointStarted,
  AppleAgxDoorbellEndpointStarted,
  AppleAgxFirmwareInitdataPublished,
  AppleAgxFirmwareInitdataSent,
  AppleAgxFirmwareDeviceControlInitialized,
  AppleAgxFirmwareIdleTimestampUpdated,
  AppleAgxFirmwareRollingBack,
  AppleAgxFirmwareStopped,
  AppleAgxFirmwareFailed,
} APPLE_AGX_FIRMWARE_PHASE;

typedef enum _APPLE_AGX_FIRMWARE_RESULT {
  AppleAgxFirmwareResultOk = 0,
  AppleAgxFirmwareResultInvalid,
  AppleAgxFirmwareResultDeadlineOverflow,
  AppleAgxFirmwareResultTimeout,
  AppleAgxFirmwareResultClockRegression,
  AppleAgxFirmwareResultTransportFailed,
  AppleAgxFirmwareResultCleanupFailed,
} APPLE_AGX_FIRMWARE_RESULT;

#define APPLE_AGX_FIRMWARE_POWERED (1u << 0)
#define APPLE_AGX_FIRMWARE_UAT_READY (1u << 1)
#define APPLE_AGX_FIRMWARE_ASC_RUNNING (1u << 2)
#define APPLE_AGX_FIRMWARE_ENDPOINT (1u << 3)
#define APPLE_AGX_FIRMWARE_DOORBELL_ENDPOINT (1u << 4)
#define APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED (1u << 5)
#define APPLE_AGX_FIRMWARE_INITDATA_SENT (1u << 6)
#define APPLE_AGX_FIRMWARE_DEVICE_CONTROL (1u << 7)
#define APPLE_AGX_FIRMWARE_IDLE_TIMESTAMP (1u << 8)
#define APPLE_AGX_FIRMWARE_ALL_COMPLETED ((1u << 9) - 1u)

typedef struct _APPLE_AGX_FIRMWARE {
  APPLE_AGX_FIRMWARE_PHASE Phase;
  APPLE_AGX_FW_U32 CompletedMask;
  APPLE_AGX_FW_U64 InitdataAddress;
  APPLE_AGX_FIRMWARE_RESULT LastResult;
  /* Cleanup obligations are not completed hardware phases. */
  APPLE_AGX_FW_U32 CleanupMask;
} APPLE_AGX_FIRMWARE;

typedef struct _APPLE_AGX_FIRMWARE_IO {
  void *Context;
  APPLE_AGX_FW_U64 (*NowMs)(void *Context);
  APPLE_AGX_FW_BOOL (*PowerOn)(void *Context,
                               APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*CreateFirmwareUat)(void *Context,
                                         APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*BootAsc)(void *Context,
                               APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*StartEndpoint)(void *Context,
                                     APPLE_AGX_FW_U32 Endpoint,
                                     APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*PublishInitdata)(void *Context,
                                      APPLE_AGX_FW_U64 DeadlineMs,
                                      APPLE_AGX_FW_U64 *Address);
  APPLE_AGX_FW_BOOL (*SendInitdata)(void *Context,
                                    APPLE_AGX_FW_U64 Address,
                                    APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*SendDeviceControlInit)(void *Context,
                                             APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*UpdateIdleTimestamp)(void *Context,
                                           APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*UnpublishInitdata)(void *Context,
                                        APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*StopEndpoint)(void *Context,
                                    APPLE_AGX_FW_U32 Endpoint,
                                    APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*StopAsc)(void *Context,
                               APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*DestroyFirmwareUat)(void *Context,
                                          APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*PowerOff)(void *Context,
                                APPLE_AGX_FW_U64 DeadlineMs);
  void (*RecordPhase)(void *Context, APPLE_AGX_FIRMWARE_PHASE Phase,
                      APPLE_AGX_FIRMWARE_RESULT Result,
                      APPLE_AGX_FW_U32 CompletedMask);
  /*
   * Optional, warm restart only (NULL: that entry point returns Invalid).
   *
   * ForceStopAsc: stop an ASC CPU whose firmware does not answer and prove
   * it stopped.  No RTKit message may be sent.  On success the callee has
   * also retired its initdata publication and both endpoints' host state;
   * UnpublishInitdata and StopEndpoint are not called for a forced stop.
   *
   * RevalidateFirmwareUat: the warm replacement for CreateFirmwareUat.  The
   * retained UAT roots must be unchanged; the callee re-arms whatever a
   * second ASC boot needs (handoff) and refreshes the initdata address that
   * PublishInitdata will return.  It must not create, destroy or re-root
   * page tables and must not touch the power domain.
   */
  APPLE_AGX_FW_BOOL (*ForceStopAsc)(void *Context,
                                    APPLE_AGX_FW_U64 DeadlineMs);
  APPLE_AGX_FW_BOOL (*RevalidateFirmwareUat)(void *Context,
                                             APPLE_AGX_FW_U64 DeadlineMs);
} APPLE_AGX_FIRMWARE_IO;

void AppleAgxFirmwareInitialize(APPLE_AGX_FIRMWARE *Firmware);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStart(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareRollback(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
void AppleAgxFirmwareFail(APPLE_AGX_FIRMWARE *Firmware,
                          APPLE_AGX_FIRMWARE_RESULT Result);

/*
 * Warm restart (TDR-FIRMWARE-RESTART-RECOVERY.md, stage 2; untested on
 * hardware until H1).  "Warm" = the ASC CPU and the RTKit session are
 * stopped and booted again while the power domain stays on and the UAT
 * stays as it is: PowerOn, CreateFirmwareUat, DestroyFirmwareUat and
 * PowerOff are never called by these functions.  The warm-stopped state is
 * Phase == AppleAgxFirmwareUatReady with CompletedMask ==
 * APPLE_AGX_FIRMWARE_WARM_RETAINED and CleanupMask == 0.
 *
 * StopWarm: the orderly stop.  Same cleanup order and deadlines as
 * AppleAgxFirmwareRollback up to and including StopAsc, then it stops.  A
 * failed step leaves Phase == AppleAgxFirmwareFailed with the unfinished
 * bits still set; from there exactly three calls are meaningful: StopWarm
 * again (resume), ForceStopWarm, or AppleAgxFirmwareRollback (cold).  It
 * never falls back to a forced stop by itself.
 *
 * ForceStopWarm: for a firmware that does not answer.  Calls only
 * Io->ForceStopAsc; no endpoint stop and no unpublish callback runs.
 *
 * StartWarm: from the warm-stopped state only.  RevalidateFirmwareUat, then
 * the same sequence AppleAgxFirmwareStart runs from BootAsc on, with the
 * same deadlines.  A failed step warm-stops again (orderly) and returns the
 * failure with Phase == AppleAgxFirmwareUatReady, so the caller may retry,
 * force, or roll back cold.
 *
 * RestartWarm: StopWarm then StartWarm, for a caller with nothing to
 * rebuild in between.  A caller that rebuilds firmware-visible memory
 * between the two halves calls them separately.
 *
 * A warm-stopped firmware still owns the UAT and the power domain:
 * AppleAgxFirmwareRollback from that state destroys the UAT and powers off.
 */
#define APPLE_AGX_FIRMWARE_WARM_RETAINED \
  (APPLE_AGX_FIRMWARE_POWERED | APPLE_AGX_FIRMWARE_UAT_READY)
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStopWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareForceStopWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStartWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareRestartWarm(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);

#endif /* APPLE_AGX_FIRMWARE_H */
