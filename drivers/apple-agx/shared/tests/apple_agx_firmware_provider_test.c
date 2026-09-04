#include "apple_agx_firmware_provider.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_PLATFORM {
  unsigned char Passive;
  unsigned char Powered;
  unsigned char UatCreated;
  unsigned char AscRunning;
  unsigned char FirmwareEndpoint;
  unsigned char DoorbellEndpoint;
  unsigned char RootsPublished;
  unsigned char FailNextReleaseWrite;
  unsigned char PublishHandoffMagicOnPower;
  unsigned int PublishCount;
  unsigned int UnpublishCount;
  unsigned long long NowMs;
  unsigned long long LastPowerOffDeadline;
  unsigned char Handoff[J313_AGX_G2_HANDOFF_SIZE];
} FAKE_PLATFORM;

static unsigned char write64(void *context, unsigned int offset,
                             unsigned long long value);

static unsigned char passive(void *context) {
  return ((FAKE_PLATFORM *)context)->Passive;
}

static unsigned long long now_ms(void *context) {
  return ((FAKE_PLATFORM *)context)->NowMs;
}

static unsigned char power_on(void *context, unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  if (fake->Powered)
    return 0u;
  fake->Powered = 1u;
  if (fake->PublishHandoffMagicOnPower)
    assert(write64(fake, APPLE_AGX_GFX_HANDOFF_MAGIC_FW_OFFSET,
                   APPLE_AGX_GFX_HANDOFF_PPL_MAGIC));
  return 1u;
}

static unsigned char power_off(void *context, unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  fake->LastPowerOffDeadline = deadline;
  if (fake->NowMs >= deadline)
    return 0u;
  if (!fake->Powered)
    return 0u;
  fake->Powered = 0u;
  return 1u;
}

static unsigned char create_uat(void *context, unsigned long long deadline,
                                APPLE_AGX_UAT_TTBR_PAIR *pair,
                                unsigned long long *initdata_address) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  if (!fake->Powered || fake->UatCreated)
    return 0u;
  fake->UatCreated = 1u;
  pair->Ttbr0 = 0x1003ULL;
  pair->Ttbr1 = 0x2003ULL;
  *initdata_address = 0x1500000000ULL;
  return 1u;
}

static unsigned char destroy_uat(void *context, unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  if (!fake->UatCreated || fake->RootsPublished)
    return 0u;
  fake->UatCreated = 0u;
  return 1u;
}

static unsigned char boot_asc(void *context, unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  if (!fake->UatCreated || fake->AscRunning)
    return 0u;
  fake->AscRunning = 1u;
  return 1u;
}

static unsigned char stop_asc(void *context, unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  if (!fake->AscRunning)
    return 0u;
  fake->AscRunning = 0u;
  fake->FirmwareEndpoint = 0u;
  fake->DoorbellEndpoint = 0u;
  return 1u;
}

static unsigned char start_endpoint(void *context, unsigned int endpoint,
                                    unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  if (!fake->AscRunning)
    return 0u;
  if (endpoint == J313_AGX_G2_FIRMWARE_ENDPOINT) {
    if (fake->FirmwareEndpoint)
      return 0u;
    fake->FirmwareEndpoint = 1u;
    return 1u;
  }
  if (endpoint == J313_AGX_G2_DOORBELL_ENDPOINT) {
    if (fake->DoorbellEndpoint)
      return 0u;
    fake->DoorbellEndpoint = 1u;
    return 1u;
  }
  return 0u;
}

static unsigned char stop_endpoint(void *context, unsigned int endpoint,
                                   unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  if (endpoint == J313_AGX_G2_DOORBELL_ENDPOINT && fake->DoorbellEndpoint) {
    fake->DoorbellEndpoint = 0u;
    return 1u;
  }
  if (endpoint == J313_AGX_G2_FIRMWARE_ENDPOINT && fake->FirmwareEndpoint) {
    fake->FirmwareEndpoint = 0u;
    return 1u;
  }
  return 0u;
}

static unsigned char publish_roots(void *context,
                                   const APPLE_AGX_UAT_TTBR_PAIR *pair) {
  FAKE_PLATFORM *fake = context;
  assert(fake->Handoff[APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET] == 1u);
  assert(pair->Ttbr0 == 0x1003ULL && pair->Ttbr1 == 0x2003ULL);
  if (fake->RootsPublished)
    return 0u;
  fake->RootsPublished = 1u;
  ++fake->PublishCount;
  return 1u;
}

static unsigned char unpublish_roots(void *context) {
  FAKE_PLATFORM *fake = context;
  assert(fake->Handoff[APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET] == 1u);
  if (!fake->RootsPublished)
    return 0u;
  fake->RootsPublished = 0u;
  ++fake->UnpublishCount;
  return 1u;
}

static unsigned char send_initdata(void *context,
                                   unsigned long long address,
                                   unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  assert(fake->NowMs <= deadline);
  return (unsigned char)(fake->AscRunning && fake->RootsPublished &&
                         address == 0x1500000000ULL);
}

static unsigned char send_device_control_init(
    void *context, unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  return (unsigned char)(fake->NowMs <= deadline && fake->RootsPublished);
}

static unsigned char update_idle_timestamp(
    void *context, unsigned long long deadline) {
  FAKE_PLATFORM *fake = context;
  return (unsigned char)(fake->NowMs <= deadline && fake->RootsPublished);
}

static unsigned char read8(void *context, unsigned int offset,
                           unsigned char *value) {
  FAKE_PLATFORM *fake = context;
  if (offset >= sizeof(fake->Handoff))
    return 0u;
  *value = fake->Handoff[offset];
  return 1u;
}

static unsigned char read32(void *context, unsigned int offset,
                            unsigned int *value) {
  FAKE_PLATFORM *fake = context;
  unsigned int index;
  if (offset > sizeof(fake->Handoff) - 4u)
    return 0u;
  *value = 0u;
  for (index = 0u; index < 4u; ++index)
    *value |= (unsigned int)fake->Handoff[offset + index] << (index * 8u);
  return 1u;
}

static unsigned char write8(void *context, unsigned int offset,
                            unsigned char value) {
  FAKE_PLATFORM *fake = context;
  if (offset >= sizeof(fake->Handoff))
    return 0u;
  if (offset == APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET && value == 0u &&
      fake->FailNextReleaseWrite) {
    fake->FailNextReleaseWrite = 0u;
    return 0u;
  }
  fake->Handoff[offset] = value;
  return 1u;
}

static unsigned char write32(void *context, unsigned int offset,
                             unsigned int value) {
  FAKE_PLATFORM *fake = context;
  unsigned int index;
  if (offset > sizeof(fake->Handoff) - 4u)
    return 0u;
  for (index = 0u; index < 4u; ++index)
    fake->Handoff[offset + index] =
        (unsigned char)(value >> (index * 8u));
  return 1u;
}

static unsigned char read64(void *context, unsigned int offset,
                            unsigned long long *value) {
  FAKE_PLATFORM *fake = context;
  unsigned int index;
  if (offset > sizeof(fake->Handoff) - 8u)
    return 0u;
  *value = 0ULL;
  for (index = 0u; index < 8u; ++index)
    *value |= (unsigned long long)fake->Handoff[offset + index]
              << (index * 8u);
  return 1u;
}

static unsigned char write64(void *context, unsigned int offset,
                             unsigned long long value) {
  FAKE_PLATFORM *fake = context;
  unsigned int index;
  if (offset > sizeof(fake->Handoff) - 8u)
    return 0u;
  for (index = 0u; index < 8u; ++index)
    fake->Handoff[offset + index] =
        (unsigned char)(value >> (index * 8u));
  return 1u;
}

static void barrier(void *context) { (void)context; }
static void relax(void *context) { ++((FAKE_PLATFORM *)context)->NowMs; }

static APPLE_AGX_GFX_HANDOFF_STATE bind_uninitialized_handoff(
    FAKE_PLATFORM *fake) {
  APPLE_AGX_GFX_HANDOFF_STATE state;
  APPLE_AGX_GFX_HANDOFF_REGION region = {
      J313_AGX_G2_HANDOFF_BASE, (unsigned int)J313_AGX_G2_HANDOFF_SIZE};
  APPLE_AGX_GFX_HANDOFF_IO io = {fake, read8, read32, read64, write8,
                                 write32, write64, barrier, relax, now_ms};
  memset(&state, 0, sizeof(state));
  assert(AppleAgxGfxHandoffBindJ313(&state, &region, &io) ==
         AppleAgxGfxHandoffResultOk);
  return state;
}

static APPLE_AGX_GFX_HANDOFF_STATE bind_handoff(FAKE_PLATFORM *fake) {
  APPLE_AGX_GFX_HANDOFF_STATE state = bind_uninitialized_handoff(fake);
  assert(write64(fake, APPLE_AGX_GFX_HANDOFF_MAGIC_FW_OFFSET,
                 APPLE_AGX_GFX_HANDOFF_PPL_MAGIC));
  assert(AppleAgxGfxHandoffInitialize(&state, 100u) ==
         AppleAgxGfxHandoffResultOk);
  return state;
}

static APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES primitives(
    FAKE_PLATFORM *fake) {
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES result = {
      .Context = fake,
      .IsPassiveLevel = passive,
      .NowMs = now_ms,
      .PowerOn = power_on,
      .PowerOff = power_off,
      .CreateFirmwareUat = create_uat,
      .DestroyFirmwareUat = destroy_uat,
      .BootAsc = boot_asc,
      .StopAsc = stop_asc,
      .StartEndpoint = start_endpoint,
      .StopEndpoint = stop_endpoint,
      .PublishUatRoots = publish_roots,
      .UnpublishUatRoots = unpublish_roots,
      .SendInitdata = send_initdata,
      .SendDeviceControlInit = send_device_control_init,
      .UpdateIdleTimestamp = update_idle_timestamp,
  };
  return result;
}

static void start_through_endpoints(APPLE_AGX_FIRMWARE_IO *io) {
  assert(io->PowerOn(io->Context, 100u));
  assert(io->CreateFirmwareUat(io->Context, 100u));
  assert(io->BootAsc(io->Context, 100u));
  assert(io->StartEndpoint(io->Context, J313_AGX_G2_FIRMWARE_ENDPOINT,
                           100u));
  assert(io->StartEndpoint(io->Context, J313_AGX_G2_DOORBELL_ENDPOINT,
                           100u));
}

static void test_exact_semantics_are_ready(void) {
  FAKE_PLATFORM fake = {.Passive = 1u};
  APPLE_AGX_GFX_HANDOFF_STATE handoff = bind_handoff(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES ops = primitives(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER provider;
  APPLE_AGX_FIRMWARE_IO io;
  unsigned long long address = 0u;
  memset(&provider, 0, sizeof(provider));
  memset(&io, 0, sizeof(io));
  assert(AppleAgxFirmwareProviderInitialize(&provider, &ops, &handoff, &io) ==
         AppleAgxFirmwareProviderResultOk);
  assert(AppleAgxFirmwareProviderBlockers(&provider) == 0u);
  assert(AppleAgxFirmwareProviderIsReady(&provider));
  start_through_endpoints(&io);
  assert(io.PublishInitdata(io.Context, 100u, &address));
  assert(io.SendInitdata(io.Context, address, 100u));
  assert(io.SendDeviceControlInit(io.Context, 100u));
  assert(io.UpdateIdleTimestamp(io.Context, 100u));
  assert(io.UnpublishInitdata(io.Context, 100u));
  assert(io.StopEndpoint(io.Context, J313_AGX_G2_DOORBELL_ENDPOINT, 100u));
  assert(io.StopEndpoint(io.Context, J313_AGX_G2_FIRMWARE_ENDPOINT, 100u));
}

static void test_publication_holds_exact_handoff_lock(void) {
  FAKE_PLATFORM fake = {.Passive = 1u};
  APPLE_AGX_GFX_HANDOFF_STATE handoff = bind_handoff(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES ops = primitives(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER provider;
  APPLE_AGX_FIRMWARE_IO io;
  unsigned long long address = 0u;

  memset(&provider, 0, sizeof(provider));
  assert(AppleAgxFirmwareProviderInitialize(&provider, &ops, &handoff, &io) ==
         AppleAgxFirmwareProviderResultOk);
  start_through_endpoints(&io);
  assert(io.PublishInitdata(io.Context, 100u, &address));
  assert(address == 0x1500000000ULL);
  assert(fake.PublishCount == 1u && fake.RootsPublished);
  assert(!handoff.Locked);
  assert(io.SendInitdata(io.Context, address, 100u));
  assert(io.UnpublishInitdata(io.Context, 100u));
  assert(fake.UnpublishCount == 1u && !fake.RootsPublished);
  assert(!handoff.Locked);
}

static void test_release_failure_compensates_publication(void) {
  FAKE_PLATFORM fake = {.Passive = 1u};
  APPLE_AGX_GFX_HANDOFF_STATE handoff = bind_handoff(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES ops = primitives(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER provider;
  APPLE_AGX_FIRMWARE_IO io;
  unsigned long long address = 0u;

  memset(&provider, 0, sizeof(provider));
  assert(AppleAgxFirmwareProviderInitialize(&provider, &ops, &handoff, &io) ==
         AppleAgxFirmwareProviderResultOk);
  start_through_endpoints(&io);
  fake.FailNextReleaseWrite = 1u;
  assert(!io.PublishInitdata(io.Context, 100u, &address));
  assert(address == 0u);
  assert(fake.PublishCount == 1u);
  assert(fake.UnpublishCount == 1u);
  assert(!fake.RootsPublished);
  assert(!handoff.Locked);
}

static void test_coordinator_rolls_back_every_available_primitive(void) {
  FAKE_PLATFORM fake = {.Passive = 1u};
  APPLE_AGX_GFX_HANDOFF_STATE handoff = bind_handoff(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES ops = primitives(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER provider;
  APPLE_AGX_FIRMWARE firmware;
  APPLE_AGX_FIRMWARE_IO io;

  memset(&provider, 0, sizeof(provider));
  assert(AppleAgxFirmwareProviderInitialize(&provider, &ops, &handoff, &io) ==
         AppleAgxFirmwareProviderResultOk);
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(firmware.Phase == AppleAgxFirmwareStopped);
  assert(firmware.CompletedMask == 0u);
  assert(!fake.Powered);
  assert(!fake.UatCreated);
  assert(!fake.AscRunning);
  assert(!fake.RootsPublished);
  assert(fake.PublishCount == 1u && fake.UnpublishCount == 1u);
}

static void test_passive_lifetime_is_enforced(void) {
  FAKE_PLATFORM fake = {.Passive = 1u};
  APPLE_AGX_GFX_HANDOFF_STATE handoff = bind_handoff(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES ops = primitives(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER provider;
  APPLE_AGX_FIRMWARE_IO io;

  memset(&provider, 0, sizeof(provider));
  assert(AppleAgxFirmwareProviderInitialize(&provider, &ops, &handoff, &io) ==
         AppleAgxFirmwareProviderResultOk);
  fake.Passive = 0u;
  assert(!io.PowerOn(io.Context, 100u));
  assert(!fake.Powered);
  assert(AppleAgxFirmwareProviderDestroy(&provider) ==
         AppleAgxFirmwareProviderResultInvalidState);
  fake.Passive = 1u;
  assert(AppleAgxFirmwareProviderDestroy(&provider) ==
         AppleAgxFirmwareProviderResultOk);
}

static void test_handoff_initializes_only_after_power(void) {
  FAKE_PLATFORM fake = {
      .Passive = 1u,
      .PublishHandoffMagicOnPower = 1u,
  };
  APPLE_AGX_GFX_HANDOFF_STATE handoff =
      bind_uninitialized_handoff(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES ops = primitives(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER provider;
  APPLE_AGX_FIRMWARE_IO io;

  memset(&provider, 0, sizeof(provider));
  memset(&io, 0, sizeof(io));
  assert(AppleAgxFirmwareProviderInitialize(&provider, &ops, &handoff, &io) ==
         AppleAgxFirmwareProviderResultOk);
  assert(!handoff.Initialized);
  assert(io.PowerOn(io.Context, 100u));
  assert(fake.Powered);
  assert(handoff.Initialized);
  assert(io.PowerOff(io.Context, 100u));
  assert(AppleAgxFirmwareProviderDestroy(&provider) ==
         AppleAgxFirmwareProviderResultOk);
}

static void test_handoff_timeout_compensates_power(void) {
  FAKE_PLATFORM fake = {.Passive = 1u};
  APPLE_AGX_GFX_HANDOFF_STATE handoff =
      bind_uninitialized_handoff(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER_PRIMITIVES ops = primitives(&fake);
  APPLE_AGX_FIRMWARE_PROVIDER provider;
  APPLE_AGX_FIRMWARE_IO io;

  memset(&provider, 0, sizeof(provider));
  memset(&io, 0, sizeof(io));
  assert(AppleAgxFirmwareProviderInitialize(&provider, &ops, &handoff, &io) ==
         AppleAgxFirmwareProviderResultOk);
  assert(!io.PowerOn(io.Context, 3u));
  assert(fake.NowMs >= 3u);
  assert(fake.LastPowerOffDeadline > fake.NowMs);
  assert(fake.LastPowerOffDeadline - fake.NowMs == 500u);
  assert(!fake.Powered);
  assert(!handoff.Initialized);
  assert(!handoff.Locked);
  assert(provider.State == APPLE_AGX_FIRMWARE_PROVIDER_INITIALIZED);
  assert(AppleAgxFirmwareProviderDestroy(&provider) ==
         AppleAgxFirmwareProviderResultOk);
}

int main(void) {
  test_exact_semantics_are_ready();
  test_publication_holds_exact_handoff_lock();
  test_release_failure_compensates_publication();
  test_coordinator_rolls_back_every_available_primitive();
  test_passive_lifetime_is_enforced();
  test_handoff_initializes_only_after_power();
  test_handoff_timeout_compensates_power();
  return 0;
}
