#include "apple_agx_firmware_provider.h"

#define APPLE_AGX_PROVIDER_NULL ((void *)0)
#define APPLE_AGX_PROVIDER_BLOCKERS 0u
#define APPLE_AGX_PROVIDER_ROLLBACK_TIMEOUT_MS 500ULL

static unsigned char primitives_valid(
    const APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES *ops) {
  return (unsigned char)(
      ops != APPLE_AGX_PROVIDER_NULL &&
      ops->IsPassiveLevel != APPLE_AGX_PROVIDER_NULL &&
      ops->NowMs != APPLE_AGX_PROVIDER_NULL &&
      ops->PowerOn != APPLE_AGX_PROVIDER_NULL &&
      ops->PowerOff != APPLE_AGX_PROVIDER_NULL &&
      ops->CreateFirmwareUat != APPLE_AGX_PROVIDER_NULL &&
      ops->DestroyFirmwareUat != APPLE_AGX_PROVIDER_NULL &&
      ops->BootAsc != APPLE_AGX_PROVIDER_NULL &&
      ops->StopAsc != APPLE_AGX_PROVIDER_NULL &&
      ops->StartEndpoint != APPLE_AGX_PROVIDER_NULL &&
      ops->StopEndpoint != APPLE_AGX_PROVIDER_NULL &&
      ops->PublishUatRoots != APPLE_AGX_PROVIDER_NULL &&
      ops->UnpublishUatRoots != APPLE_AGX_PROVIDER_NULL &&
      ops->SendInitdata != APPLE_AGX_PROVIDER_NULL &&
      ops->SendDeviceControlInit != APPLE_AGX_PROVIDER_NULL &&
      ops->UpdateIdleTimestamp != APPLE_AGX_PROVIDER_NULL);
}

static unsigned char provider_valid(
    const APPLE_AGX_FIRMWARE_PROVIDER *provider) {
  return (unsigned char)(provider != APPLE_AGX_PROVIDER_NULL &&
                         (provider->State &
                          APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED) != 0u &&
                         provider->Handoff != APPLE_AGX_PROVIDER_NULL &&
                         provider->Handoff->Bound != 0u &&
                         primitives_valid(&provider->Primitives));
}

static unsigned char at_passive(APPLE_AGX_FIRMWARE_PROVIDER *provider) {
  return (unsigned char)(provider_valid(provider) &&
                         provider->Primitives.IsPassiveLevel(
                             provider->Primitives.Context));
}

static APPLE_AGX_FW_U64 provider_now_ms(void *context) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider))
    return 0ULL;
  return provider->Primitives.NowMs(provider->Primitives.Context);
}

static APPLE_AGX_FW_BOOL provider_power_on(void *context,
                                           APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider) ||
      provider->State != APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED ||
      !provider->Primitives.PowerOn(provider->Primitives.Context, deadline))
    return APPLE_AGX_FW_FALSE;
  provider->State |= APPLE_AGX_FIRMWARE_PROVIDER_POWERED;
  return APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL provider_create_uat(void *context,
                                             APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  APPLE_AGX_UAT_TTBR_PAIR pair = {0ULL, 0ULL};
  unsigned long long address = 0ULL;
  if (!at_passive(provider) ||
      provider->State != (APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED |
                          APPLE_AGX_FIRMWARE_PROVIDER_POWERED) ||
      !provider->Primitives.CreateFirmwareUat(
          provider->Primitives.Context, deadline, &pair, &address) ||
      pair.Ttbr0 == 0ULL || pair.Ttbr1 == 0ULL || address == 0ULL)
    return APPLE_AGX_FW_FALSE;
  provider->Pair = pair;
  provider->InitdataAddress = address;
  provider->State |= APPLE_AGX_FIRMWARE_PROVIDER_UAT;
  return APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL provider_boot_asc(void *context,
                                           APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  APPLE_AGX_GFX_HANDOFF_RESULT handoff_result;
  unsigned int required = APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED |
                          APPLE_AGX_FIRMWARE_PROVIDER_POWERED |
                          APPLE_AGX_FIRMWARE_PROVIDER_UAT;
  if (!at_passive(provider) || provider->State != required ||
      !provider->Primitives.BootAsc(provider->Primitives.Context, deadline))
    return APPLE_AGX_FW_FALSE;
  /* Firmware publishes MAGIC_FW only after ASC is running. */
  handoff_result = AppleAgxGfxHandoffInitialize(provider->Handoff, deadline);
  if (handoff_result != AppleAgxGfxHandoffResultOk)
    return APPLE_AGX_FW_FALSE;
  provider->State |= APPLE_AGX_FIRMWARE_PROVIDER_ASC;
  return APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL provider_start_endpoint(
    void *context, APPLE_AGX_FW_U32 endpoint, APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  unsigned int bit;
  unsigned int required;
  if (!at_passive(provider))
    return APPLE_AGX_FW_FALSE;
  if (endpoint == J313_AGX_G2_FIRMWARE_ENDPOINT) {
    bit = APPLE_AGX_FIRMWARE_PROVIDER_FW_ENDPOINT;
    required = APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED |
               APPLE_AGX_FIRMWARE_PROVIDER_POWERED |
               APPLE_AGX_FIRMWARE_PROVIDER_UAT |
               APPLE_AGX_FIRMWARE_PROVIDER_ASC;
  } else if (endpoint == J313_AGX_G2_DOORBELL_ENDPOINT) {
    bit = APPLE_AGX_FIRMWARE_PROVIDER_DOORBELL_ENDPOINT;
    required = APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED |
               APPLE_AGX_FIRMWARE_PROVIDER_POWERED |
               APPLE_AGX_FIRMWARE_PROVIDER_UAT |
               APPLE_AGX_FIRMWARE_PROVIDER_ASC |
               APPLE_AGX_FIRMWARE_PROVIDER_FW_ENDPOINT;
  } else {
    return APPLE_AGX_FW_FALSE;
  }
  if ((provider->State != required &&
       provider->State != (required | APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED)) ||
      !provider->Primitives.StartEndpoint(provider->Primitives.Context,
                                           endpoint, deadline))
    return APPLE_AGX_FW_FALSE;
  provider->State |= bit;
  return APPLE_AGX_FW_TRUE;
}

static unsigned char acquire_handoff(APPLE_AGX_FIRMWARE_PROVIDER *provider,
                                     unsigned long long deadline) {
  unsigned long long now =
      provider->Primitives.NowMs(provider->Primitives.Context);
  if (now > deadline)
    return 0u;
  return (unsigned char)(AppleAgxGfxHandoffAcquire(provider->Handoff,
                                                    deadline - now) ==
                         AppleAgxGfxHandoffResultOk);
}

static unsigned char release_handoff(
    APPLE_AGX_FIRMWARE_PROVIDER *provider) {
  return (unsigned char)(AppleAgxGfxHandoffRelease(provider->Handoff) ==
                         AppleAgxGfxHandoffResultOk);
}

static APPLE_AGX_FW_BOOL provider_publish_initdata(
    void *context, APPLE_AGX_FW_U64 deadline, APPLE_AGX_FW_U64 *address) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  unsigned int required = APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED |
                          APPLE_AGX_FIRMWARE_PROVIDER_POWERED |
                          APPLE_AGX_FIRMWARE_PROVIDER_UAT |
                          APPLE_AGX_FIRMWARE_PROVIDER_ASC |
                          APPLE_AGX_FIRMWARE_PROVIDER_FW_ENDPOINT |
                          APPLE_AGX_FIRMWARE_PROVIDER_DOORBELL_ENDPOINT;
  if (address != APPLE_AGX_PROVIDER_NULL)
    *address = 0ULL;
  if (!at_passive(provider) || address == APPLE_AGX_PROVIDER_NULL)
    return APPLE_AGX_FW_FALSE;
  if (provider->State == (required | APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED)) {
    *address = provider->InitdataAddress;
    return APPLE_AGX_FW_TRUE;
  }
  if (provider->State != required || !acquire_handoff(provider, deadline))
    return APPLE_AGX_FW_FALSE;
  if (!provider->Primitives.PublishUatRoots(provider->Primitives.Context,
                                            &provider->Pair)) {
    (void)release_handoff(provider);
    return APPLE_AGX_FW_FALSE;
  }
  provider->State |= APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED;
  if (!release_handoff(provider)) {
    /* Roots changed while the lock was held; compensate before failing. */
    if (provider->Primitives.UnpublishUatRoots(
            provider->Primitives.Context))
      provider->State &= ~APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED;
    if (provider->Handoff->Locked != 0u)
      (void)release_handoff(provider);
    return APPLE_AGX_FW_FALSE;
  }
  *address = provider->InitdataAddress;
  return APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL provider_send_initdata(
    void *context, APPLE_AGX_FW_U64 address, APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider) ||
      (provider->State & APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED) == 0u ||
      address == 0ULL || address != provider->InitdataAddress)
    return APPLE_AGX_FW_FALSE;
  return provider->Primitives.SendInitdata(provider->Primitives.Context,
                                            address, deadline)
             ? APPLE_AGX_FW_TRUE
             : APPLE_AGX_FW_FALSE;
}

static APPLE_AGX_FW_BOOL provider_device_control_init(
    void *context, APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider) ||
      (provider->State & APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED) == 0u)
    return APPLE_AGX_FW_FALSE;
  return provider->Primitives.SendDeviceControlInit(
             provider->Primitives.Context, deadline)
             ? APPLE_AGX_FW_TRUE
             : APPLE_AGX_FW_FALSE;
}

static APPLE_AGX_FW_BOOL provider_update_idle_timestamp(
    void *context, APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider) ||
      (provider->State & APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED) == 0u)
    return APPLE_AGX_FW_FALSE;
  return provider->Primitives.UpdateIdleTimestamp(
             provider->Primitives.Context, deadline)
             ? APPLE_AGX_FW_TRUE
             : APPLE_AGX_FW_FALSE;
}

static APPLE_AGX_FW_BOOL provider_unpublish_initdata(
    void *context, APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider))
    return APPLE_AGX_FW_FALSE;
  if ((provider->State & APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED) == 0u) {
    if (provider->Handoff->Locked != 0u)
      return release_handoff(provider) ? APPLE_AGX_FW_TRUE
                                       : APPLE_AGX_FW_FALSE;
    return APPLE_AGX_FW_TRUE;
  }
  if (provider->Handoff->Locked == 0u &&
      !acquire_handoff(provider, deadline))
    return APPLE_AGX_FW_FALSE;
  if (!provider->Primitives.UnpublishUatRoots(
          provider->Primitives.Context)) {
    (void)release_handoff(provider);
    return APPLE_AGX_FW_FALSE;
  }
  provider->State &= ~APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED;
  return release_handoff(provider) ? APPLE_AGX_FW_TRUE
                                   : APPLE_AGX_FW_FALSE;
}

static APPLE_AGX_FW_BOOL provider_stop_endpoint(
    void *context, APPLE_AGX_FW_U32 endpoint, APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  unsigned int bit;
  if (!at_passive(provider))
    return APPLE_AGX_FW_FALSE;
  if (endpoint == J313_AGX_G2_DOORBELL_ENDPOINT)
    bit = APPLE_AGX_FIRMWARE_PROVIDER_DOORBELL_ENDPOINT;
  else if (endpoint == J313_AGX_G2_FIRMWARE_ENDPOINT)
    bit = APPLE_AGX_FIRMWARE_PROVIDER_FW_ENDPOINT;
  else
    return APPLE_AGX_FW_FALSE;
  if ((provider->State & bit) == 0u ||
      !provider->Primitives.StopEndpoint(provider->Primitives.Context,
                                          endpoint, deadline))
    return APPLE_AGX_FW_FALSE;
  provider->State &= ~bit;
  return APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL provider_stop_asc(void *context,
                                           APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider) ||
      (provider->State & APPLE_AGX_FIRMWARE_PROVIDER_ASC) == 0u ||
      (provider->State & APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED) != 0u ||
      !provider->Primitives.StopAsc(provider->Primitives.Context, deadline))
    return APPLE_AGX_FW_FALSE;
  provider->State &= ~(APPLE_AGX_FIRMWARE_PROVIDER_ASC |
                       APPLE_AGX_FIRMWARE_PROVIDER_FW_ENDPOINT |
                       APPLE_AGX_FIRMWARE_PROVIDER_DOORBELL_ENDPOINT);
  return APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL provider_destroy_uat(void *context,
                                              APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  if (!at_passive(provider) ||
      (provider->State & APPLE_AGX_FIRMWARE_PROVIDER_UAT) == 0u ||
      (provider->State & (APPLE_AGX_FIRMWARE_PROVIDER_ASC |
                          APPLE_AGX_FIRMWARE_PROVIDER_PUBLISHED)) != 0u ||
      !provider->Primitives.DestroyFirmwareUat(
          provider->Primitives.Context, deadline))
    return APPLE_AGX_FW_FALSE;
  provider->State &= ~APPLE_AGX_FIRMWARE_PROVIDER_UAT;
  provider->Pair.Ttbr0 = 0ULL;
  provider->Pair.Ttbr1 = 0ULL;
  provider->InitdataAddress = 0ULL;
  return APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL provider_power_off(void *context,
                                            APPLE_AGX_FW_U64 deadline) {
  APPLE_AGX_FIRMWARE_PROVIDER *provider = context;
  unsigned int allowed = APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED |
                         APPLE_AGX_FIRMWARE_PROVIDER_POWERED;
  if (!at_passive(provider) || provider->State != allowed ||
      !provider->Primitives.PowerOff(provider->Primitives.Context, deadline))
    return APPLE_AGX_FW_FALSE;
  provider->State = APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED;
  return APPLE_AGX_FW_TRUE;
}

APPLE_AGX_FIRMWARE_PROVIDER_RESULT AppleAgxFirmwareProviderInitialize(
    APPLE_AGX_FIRMWARE_PROVIDER *provider,
    const APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES *primitives,
    APPLE_AGX_GFX_HANDOFF_STATE *handoff,
    APPLE_AGX_FIRMWARE_IO *firmware_io) {
  if (provider == APPLE_AGX_PROVIDER_NULL ||
      firmware_io == APPLE_AGX_PROVIDER_NULL || !primitives_valid(primitives) ||
      handoff == APPLE_AGX_PROVIDER_NULL || handoff->Bound == 0u ||
      handoff->Locked != 0u || provider->State != 0u ||
      !primitives->IsPassiveLevel(primitives->Context))
    return AppleAgxFirmwareProviderResultInvalidArgument;

  provider->Primitives = *primitives;
  provider->Handoff = handoff;
  provider->Pair.Ttbr0 = 0ULL;
  provider->Pair.Ttbr1 = 0ULL;
  provider->InitdataAddress = 0ULL;
  provider->State = APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED;
  provider->Blockers = APPLE_AGX_PROVIDER_BLOCKERS;

  firmware_io->Context = provider;
  firmware_io->NowMs = provider_now_ms;
  firmware_io->PowerOn = provider_power_on;
  firmware_io->CreateFirmwareUat = provider_create_uat;
  firmware_io->BootAsc = provider_boot_asc;
  firmware_io->StartEndpoint = provider_start_endpoint;
  firmware_io->PublishInitdata = provider_publish_initdata;
  firmware_io->SendInitdata = provider_send_initdata;
  firmware_io->SendDeviceControlInit = provider_device_control_init;
  firmware_io->UpdateIdleTimestamp = provider_update_idle_timestamp;
  firmware_io->UnpublishInitdata = provider_unpublish_initdata;
  firmware_io->StopEndpoint = provider_stop_endpoint;
  firmware_io->StopAsc = provider_stop_asc;
  firmware_io->DestroyFirmwareUat = provider_destroy_uat;
  firmware_io->PowerOff = provider_power_off;
  firmware_io->RecordPhase = APPLE_AGX_PROVIDER_NULL;
  return AppleAgxFirmwareProviderResultOk;
}

APPLE_AGX_FIRMWARE_PROVIDER_RESULT AppleAgxFirmwareProviderDestroy(
    APPLE_AGX_FIRMWARE_PROVIDER *provider) {
  if (!provider_valid(provider))
    return AppleAgxFirmwareProviderResultInvalidArgument;
  if (!provider->Primitives.IsPassiveLevel(provider->Primitives.Context) ||
      provider->State != APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED ||
      provider->Handoff->Locked != 0u)
    return AppleAgxFirmwareProviderResultInvalidState;
  provider->State = 0u;
  provider->Blockers = 0u;
  provider->Handoff = APPLE_AGX_PROVIDER_NULL;
  return AppleAgxFirmwareProviderResultOk;
}

unsigned int AppleAgxFirmwareProviderBlockers(
    const APPLE_AGX_FIRMWARE_PROVIDER *provider) {
  return provider_valid(provider) ? provider->Blockers
                                  : APPLE_AGX_PROVIDER_BLOCKERS;
}

unsigned char AppleAgxFirmwareProviderIsReady(
    const APPLE_AGX_FIRMWARE_PROVIDER *provider) {
  return (unsigned char)(provider_valid(provider) &&
                         provider->Blockers == 0u);
}
