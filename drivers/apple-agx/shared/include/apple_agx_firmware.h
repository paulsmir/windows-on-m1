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
  AppleAgxFirmwareHeartbeatObserved,
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
#define APPLE_AGX_FIRMWARE_HEARTBEAT (1u << 9)
#define APPLE_AGX_FIRMWARE_ALL_COMPLETED ((1u << 10) - 1u)

typedef struct _APPLE_AGX_FIRMWARE {
  APPLE_AGX_FIRMWARE_PHASE Phase;
  APPLE_AGX_FW_U32 CompletedMask;
  APPLE_AGX_FW_U64 InitdataAddress;
  APPLE_AGX_FIRMWARE_RESULT LastResult;
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
  APPLE_AGX_FW_BOOL (*ObserveHeartbeat)(void *Context,
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
} APPLE_AGX_FIRMWARE_IO;

void AppleAgxFirmwareInitialize(APPLE_AGX_FIRMWARE *Firmware);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStart(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareRollback(
    APPLE_AGX_FIRMWARE *Firmware, const APPLE_AGX_FIRMWARE_IO *Io);
void AppleAgxFirmwareFail(APPLE_AGX_FIRMWARE *Firmware,
                          APPLE_AGX_FIRMWARE_RESULT Result);

#endif /* APPLE_AGX_FIRMWARE_H */
