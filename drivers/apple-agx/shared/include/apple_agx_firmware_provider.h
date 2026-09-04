#ifndef APPLE_AGX_FIRMWARE_PROVIDER_H
#define APPLE_AGX_FIRMWARE_PROVIDER_H

#include "apple_agx_firmware.h"
#include "apple_agx_gfx_handoff.h"
#include "apple_agx_uat.h"

/*
 * Firmware/power/UAT half of a platform provider.  This adapter deliberately
 * has no queue or render-context surface and cannot make a complete
 * APPLE_AGX_BACKEND_IO truthful by itself.
 */
typedef enum _APPLE_AGX_FIRMWARE_PROVIDER_RESULT {
  AppleAgxFirmwareProviderResultOk = 0,
  AppleAgxFirmwareProviderResultInvalidArgument,
  AppleAgxFirmwareProviderResultInvalidState,
} APPLE_AGX_FIRMWARE_PROVIDER_RESULT;

#define APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED (1u << 0)
#define APPLE_AGX_FIRMWARE_PROVIDER_POWERED (1u << 1)
#define APPLE_AGX_FIRMWARE_PROVIDER_UAT (1u << 2)
#define APPLE_AGX_FIRMWARE_PROVIDER_ASC (1u << 3)
#define APPLE_AGX_FIRMWARE_PROVIDER_FW_ENDPOINT (1u << 4)
#define APPLE_AGX_FIRMWARE_PROVIDER_DOORBELL_ENDPOINT (1u << 5)
#define APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED (1u << 6)

/*
 * These are adapters over existing platform primitives, not substitute
 * firmware semantics.  Each operation must either complete its exact effect
 * or roll back its own partial work before returning false.
 */
typedef struct _APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES {
  void *Context;
  unsigned char (*IsPassiveLevel)(void *Context);
  unsigned long long (*NowMs)(void *Context);
  unsigned char (*PowerOn)(void *Context, unsigned long long DeadlineMs);
  unsigned char (*PowerOff)(void *Context, unsigned long long DeadlineMs);
  unsigned char (*CreateFirmwareUat)(
      void *Context, unsigned long long DeadlineMs,
      APPLE_AGX_UAT_TTBR_PAIR *Pair,
      unsigned long long *InitdataAddress);
  unsigned char (*DestroyFirmwareUat)(void *Context,
                                      unsigned long long DeadlineMs);
  unsigned char (*BootAsc)(void *Context, unsigned long long DeadlineMs);
  unsigned char (*StopAsc)(void *Context, unsigned long long DeadlineMs);
  unsigned char (*StartEndpoint)(void *Context, unsigned int Endpoint,
                                 unsigned long long DeadlineMs);
  unsigned char (*StopEndpoint)(void *Context, unsigned int Endpoint,
                                unsigned long long DeadlineMs);
  unsigned char (*PublishUatRoots)(
      void *Context, const APPLE_AGX_UAT_TTBR_PAIR *Pair);
  unsigned char (*UnpublishUatRoots)(void *Context);
  unsigned char (*SendInitdata)(void *Context,
                                unsigned long long InitdataAddress,
                                unsigned long long DeadlineMs);
  unsigned char (*SendDeviceControlInit)(void *Context,
                                         unsigned long long DeadlineMs);
  unsigned char (*UpdateIdleTimestamp)(void *Context,
                                       unsigned long long DeadlineMs);
} APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES;

typedef struct _APPLE_AGX_FIRMWARE_PROVIDER {
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES Primitives;
  APPLE_AGX_GFX_HANDOFF_STATE *Handoff;
  APPLE_AGX_UAT_TTBR_PAIR Pair;
  unsigned long long InitdataAddress;
  unsigned int State;
  unsigned int Blockers;
} APPLE_AGX_FIRMWARE_PROVIDER;

/* Initialize and Destroy are PASSIVE_LEVEL-only lifetime operations. */
APPLE_AGX_FIRMWARE_PROVIDER_RESULT AppleAgxFirmwareProviderInitialize(
    APPLE_AGX_FIRMWARE_PROVIDER *Provider,
    const APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES *Primitives,
    APPLE_AGX_GFX_HANDOFF_STATE *Handoff,
    APPLE_AGX_FIRMWARE_IO *FirmwareIo);
APPLE_AGX_FIRMWARE_PROVIDER_RESULT AppleAgxFirmwareProviderDestroy(
    APPLE_AGX_FIRMWARE_PROVIDER *Provider);
unsigned int AppleAgxFirmwareProviderBlockers(
    const APPLE_AGX_FIRMWARE_PROVIDER *Provider);
unsigned char AppleAgxFirmwareProviderIsReady(
    const APPLE_AGX_FIRMWARE_PROVIDER *Provider);

#endif /* APPLE_AGX_FIRMWARE_PROVIDER_H */
