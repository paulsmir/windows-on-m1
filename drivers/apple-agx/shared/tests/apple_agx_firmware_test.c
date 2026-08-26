#include "apple_agx_firmware.h"

#include <assert.h>
#include <stddef.h>

enum {
  TracePowerOn = 1,
  TraceCreateUat,
  TraceBootAsc,
  TraceStartFirmwareEndpoint,
  TraceStartDoorbellEndpoint,
  TracePublishInitdata,
  TraceSendInitdata,
  TraceDeviceControlInit,
  TraceUpdateIdleTimestamp,
  TraceHeartbeat,
  TraceUnpublishInitdata,
  TraceStopDoorbellEndpoint,
  TraceStopFirmwareEndpoint,
  TraceStopAsc,
  TraceDestroyUat,
  TracePowerOff,
};

typedef struct _FAKE_TRANSPORT {
  unsigned char Trace[64];
  unsigned int TraceCount;
  unsigned int StartOperation;
  unsigned int CleanupOperation;
  unsigned int FailStartOperation;
  unsigned int FailCleanupOperation;
  unsigned int AdvanceStartOperation;
  unsigned int AdvanceCleanupOperation;
  APPLE_AGX_FW_U64 AdvanceMs;
  unsigned int RegressStartOperation;
  unsigned int RegressCleanupOperation;
  APPLE_AGX_FW_U64 NowMs;
  APPLE_AGX_FW_U64 PublishedAddress;
  unsigned int RecordCount;
} FAKE_TRANSPORT;

static void append_trace(FAKE_TRANSPORT *fake, unsigned char value) {
  assert(fake->TraceCount < sizeof(fake->Trace));
  fake->Trace[fake->TraceCount++] = value;
}

static APPLE_AGX_FW_BOOL start_operation(FAKE_TRANSPORT *fake,
                                         unsigned char trace) {
  ++fake->StartOperation;
  append_trace(fake, trace);
  if (fake->StartOperation == fake->AdvanceStartOperation)
    fake->NowMs += fake->AdvanceMs;
  if (fake->StartOperation == fake->RegressStartOperation)
    --fake->NowMs;
  return fake->StartOperation == fake->FailStartOperation
             ? APPLE_AGX_FW_FALSE
             : APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_BOOL cleanup_operation(FAKE_TRANSPORT *fake,
                                           unsigned char trace) {
  ++fake->CleanupOperation;
  append_trace(fake, trace);
  if (fake->CleanupOperation == fake->AdvanceCleanupOperation)
    fake->NowMs += fake->AdvanceMs;
  if (fake->CleanupOperation == fake->RegressCleanupOperation)
    --fake->NowMs;
  return fake->CleanupOperation == fake->FailCleanupOperation
             ? APPLE_AGX_FW_FALSE
             : APPLE_AGX_FW_TRUE;
}

static APPLE_AGX_FW_U64 now_ms(void *context) {
  return ((FAKE_TRANSPORT *)context)->NowMs;
}

static APPLE_AGX_FW_BOOL power_on(void *context,
                                  APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return start_operation((FAKE_TRANSPORT *)context, TracePowerOn);
}

static APPLE_AGX_FW_BOOL create_uat(void *context,
                                    APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return start_operation((FAKE_TRANSPORT *)context, TraceCreateUat);
}

static APPLE_AGX_FW_BOOL boot_asc(void *context,
                                  APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return start_operation((FAKE_TRANSPORT *)context, TraceBootAsc);
}

static APPLE_AGX_FW_BOOL start_endpoint(void *context,
                                        APPLE_AGX_FW_U32 endpoint,
                                        APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  if (endpoint == J313_AGX_G2_FIRMWARE_ENDPOINT)
    return start_operation((FAKE_TRANSPORT *)context,
                           TraceStartFirmwareEndpoint);
  assert(endpoint == J313_AGX_G2_DOORBELL_ENDPOINT);
  return start_operation((FAKE_TRANSPORT *)context,
                         TraceStartDoorbellEndpoint);
}

static APPLE_AGX_FW_BOOL publish_initdata(void *context,
                                          APPLE_AGX_FW_U64 deadline,
                                          APPLE_AGX_FW_U64 *address) {
  (void)deadline;
  FAKE_TRANSPORT *fake = (FAKE_TRANSPORT *)context;
  *address = fake->PublishedAddress == 0 ? 0x1500000000ULL
                                         : fake->PublishedAddress;
  return start_operation(fake, TracePublishInitdata);
}

static APPLE_AGX_FW_BOOL send_initdata(void *context,
                                       APPLE_AGX_FW_U64 address,
                                       APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  assert(address == 0x1500000000ULL);
  return start_operation((FAKE_TRANSPORT *)context, TraceSendInitdata);
}

static APPLE_AGX_FW_BOOL device_control_init(void *context,
                                             APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return start_operation((FAKE_TRANSPORT *)context, TraceDeviceControlInit);
}

static APPLE_AGX_FW_BOOL update_idle_timestamp(
    void *context, APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return start_operation((FAKE_TRANSPORT *)context,
                         TraceUpdateIdleTimestamp);
}

static APPLE_AGX_FW_BOOL observe_heartbeat(void *context,
                                           APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return start_operation((FAKE_TRANSPORT *)context, TraceHeartbeat);
}

static APPLE_AGX_FW_BOOL unpublish_initdata(void *context,
                                            APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return cleanup_operation((FAKE_TRANSPORT *)context,
                           TraceUnpublishInitdata);
}

static APPLE_AGX_FW_BOOL stop_endpoint(void *context,
                                       APPLE_AGX_FW_U32 endpoint,
                                       APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  if (endpoint == J313_AGX_G2_DOORBELL_ENDPOINT)
    return cleanup_operation((FAKE_TRANSPORT *)context,
                             TraceStopDoorbellEndpoint);
  assert(endpoint == J313_AGX_G2_FIRMWARE_ENDPOINT);
  return cleanup_operation((FAKE_TRANSPORT *)context,
                           TraceStopFirmwareEndpoint);
}

static APPLE_AGX_FW_BOOL stop_asc(void *context,
                                  APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return cleanup_operation((FAKE_TRANSPORT *)context, TraceStopAsc);
}

static APPLE_AGX_FW_BOOL destroy_uat(void *context,
                                     APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return cleanup_operation((FAKE_TRANSPORT *)context, TraceDestroyUat);
}

static APPLE_AGX_FW_BOOL power_off(void *context,
                                   APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return cleanup_operation((FAKE_TRANSPORT *)context, TracePowerOff);
}

static void record_phase(void *context, APPLE_AGX_FIRMWARE_PHASE phase,
                         APPLE_AGX_FIRMWARE_RESULT result,
                         APPLE_AGX_FW_U32 completed_mask) {
  FAKE_TRANSPORT *fake = (FAKE_TRANSPORT *)context;
  (void)phase;
  (void)result;
  (void)completed_mask;
  ++fake->RecordCount;
}

static APPLE_AGX_FIRMWARE_IO make_io(FAKE_TRANSPORT *fake) {
  APPLE_AGX_FIRMWARE_IO io = {
      fake,
      now_ms,
      power_on,
      create_uat,
      boot_asc,
      start_endpoint,
      publish_initdata,
      send_initdata,
      device_control_init,
      update_idle_timestamp,
      observe_heartbeat,
      unpublish_initdata,
      stop_endpoint,
      stop_asc,
      destroy_uat,
      power_off,
      record_phase,
  };
  return io;
}

static void assert_trace(const FAKE_TRANSPORT *fake,
                         const unsigned char *expected,
                         unsigned int expected_count) {
  unsigned int index;
  assert(fake->TraceCount == expected_count);
  for (index = 0; index < expected_count; ++index)
    assert(fake->Trace[index] == expected[index]);
}

static void test_ordered_start_and_idempotent_rollback(void) {
  static const unsigned char expected[] = {
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
  };
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  fake.NowMs = 1000;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(firmware.Phase == AppleAgxFirmwareHeartbeatObserved);
  assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_ALL_COMPLETED);
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(firmware.Phase == AppleAgxFirmwareStopped);
  assert(firmware.CompletedMask == 0u);
  assert_trace(&fake, expected, sizeof(expected));
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert_trace(&fake, expected, sizeof(expected));
  assert(fake.RecordCount != 0u);

  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(fake.TraceCount == 2u * sizeof(expected));
  {
    unsigned int index;
    for (index = 0; index < sizeof(expected); ++index)
      assert(fake.Trace[sizeof(expected) + index] == expected[index]);
  }
}

static void test_failure_after_every_start_operation_rolls_back_exactly(void) {
  unsigned int failure;

  for (failure = 1; failure <= 10; ++failure) {
    APPLE_AGX_FIRMWARE firmware;
    FAKE_TRANSPORT fake = {0};
    APPLE_AGX_FIRMWARE_IO io = make_io(&fake);
    unsigned char expected[16];
    unsigned int count = 0;
    unsigned int index;

    fake.FailStartOperation = failure;
    for (index = 1; index <= failure; ++index)
      expected[count++] = (unsigned char)index;
    if (failure > 6)
      expected[count++] = TraceUnpublishInitdata;
    if (failure > 5)
      expected[count++] = TraceStopDoorbellEndpoint;
    if (failure > 4)
      expected[count++] = TraceStopFirmwareEndpoint;
    if (failure > 3)
      expected[count++] = TraceStopAsc;
    if (failure > 2)
      expected[count++] = TraceDestroyUat;
    if (failure > 1)
      expected[count++] = TracePowerOff;

    AppleAgxFirmwareInitialize(&firmware);
    assert(AppleAgxFirmwareStart(&firmware, &io) ==
           AppleAgxFirmwareResultTransportFailed);
    assert(firmware.Phase == AppleAgxFirmwareStopped);
    assert(firmware.CompletedMask == 0u);
    assert_trace(&fake, expected, count);
  }
}

static void test_deadline_equality_passes_and_one_tick_late_fails(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT equal = {0};
  APPLE_AGX_FIRMWARE_IO equal_io = make_io(&equal);
  FAKE_TRANSPORT late = {0};
  APPLE_AGX_FIRMWARE_IO late_io = make_io(&late);

  equal.NowMs = 1000;
  equal.AdvanceStartOperation = 3;
  equal.AdvanceMs = J313_AGX_G2_ASC_BOOT_TIMEOUT_MS;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &equal_io) ==
         AppleAgxFirmwareResultOk);
  assert(AppleAgxFirmwareRollback(&firmware, &equal_io) ==
         AppleAgxFirmwareResultOk);

  late.NowMs = 2000;
  late.AdvanceStartOperation = 3;
  late.AdvanceMs = J313_AGX_G2_ASC_BOOT_TIMEOUT_MS + 1u;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &late_io) ==
         AppleAgxFirmwareResultTimeout);
  assert(firmware.Phase == AppleAgxFirmwareStopped);
  assert(late.TraceCount == 5u);
  assert(late.Trace[0] == TracePowerOn);
  assert(late.Trace[1] == TraceCreateUat);
  assert(late.Trace[2] == TraceBootAsc);
  assert(late.Trace[3] == TraceDestroyUat);
  assert(late.Trace[4] == TracePowerOff);
}

static void test_clock_regression_and_deadline_overflow_fail_closed(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT regression = {0};
  APPLE_AGX_FIRMWARE_IO regression_io = make_io(&regression);
  FAKE_TRANSPORT overflow = {0};
  APPLE_AGX_FIRMWARE_IO overflow_io = make_io(&overflow);

  regression.NowMs = 1000;
  regression.RegressStartOperation = 4;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &regression_io) ==
         AppleAgxFirmwareResultClockRegression);
  assert(firmware.Phase == AppleAgxFirmwareStopped);

  overflow.NowMs = ~0ULL - 2ULL;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &overflow_io) ==
         AppleAgxFirmwareResultDeadlineOverflow);
  assert(firmware.Phase == AppleAgxFirmwareStopped);
  assert(overflow.TraceCount == 0u);
}

static void test_invalid_contracts_and_unknown_bits_are_rejected(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  io.BootAsc = 0;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(fake.TraceCount == 0u);

  io = make_io(&fake);
  firmware.Phase = AppleAgxFirmwareHeartbeatObserved;
  firmware.CompletedMask = 1u << 31;
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(firmware.Phase == AppleAgxFirmwareFailed);
  assert(fake.TraceCount == 0u);

  io = make_io(&fake);
  io.RecordPhase = 0;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
}

static void test_invalid_initdata_address_rolls_back(void) {
  static const unsigned char expected[] = {
      TracePowerOn,
      TraceCreateUat,
      TraceBootAsc,
      TraceStartFirmwareEndpoint,
      TraceStartDoorbellEndpoint,
      TracePublishInitdata,
      TraceStopDoorbellEndpoint,
      TraceStopFirmwareEndpoint,
      TraceStopAsc,
      TraceDestroyUat,
      TracePowerOff,
  };
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  fake.PublishedAddress = 1ULL << 44;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(firmware.Phase == AppleAgxFirmwareStopped);
  assert(firmware.CompletedMask == 0u);
  assert_trace(&fake, expected, sizeof(expected));
}

static void test_cleanup_failure_keeps_exact_resource_bit(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  fake.FailCleanupOperation = 2;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultCleanupFailed);
  assert(firmware.Phase == AppleAgxFirmwareFailed);
  assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_DOORBELL_ENDPOINT);
  assert(fake.TraceCount == 16u);
}

static void test_cleanup_timeout_is_failure(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  fake.AdvanceCleanupOperation = 1;
  fake.AdvanceMs = J313_AGX_G2_STOP_TIMEOUT_MS + 1u;
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultCleanupFailed);
  assert(firmware.Phase == AppleAgxFirmwareFailed);
  assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED);
}

static void test_cleanup_clock_regression_is_failure(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  fake.NowMs = 1000;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  fake.RegressCleanupOperation = 1;
  assert(AppleAgxFirmwareRollback(&firmware, &io) ==
         AppleAgxFirmwareResultCleanupFailed);
  assert(firmware.Phase == AppleAgxFirmwareFailed);
  assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_INITDATA_PUBLISHED);
}

int main(void) {
  test_ordered_start_and_idempotent_rollback();
  test_failure_after_every_start_operation_rolls_back_exactly();
  test_deadline_equality_passes_and_one_tick_late_fails();
  test_clock_regression_and_deadline_overflow_fail_closed();
  test_invalid_contracts_and_unknown_bits_are_rejected();
  test_invalid_initdata_address_rolls_back();
  test_cleanup_failure_keeps_exact_resource_bit();
  test_cleanup_timeout_is_failure();
  test_cleanup_clock_regression_is_failure();
  return 0;
}
