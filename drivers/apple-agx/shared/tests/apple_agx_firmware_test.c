#include "apple_agx_firmware.h"
#include "apple_agx_firmware_start_receipt.h"

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
  TraceUnpublishInitdata,
  TraceStopDoorbellEndpoint,
  TraceStopFirmwareEndpoint,
  TraceStopAsc,
  TraceDestroyUat,
  TracePowerOff,
  TraceForceStopAsc,
  TraceRevalidateUat,
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
  APPLE_AGX_FIRMWARE_START_RECEIPT FirstFailure;
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

static APPLE_AGX_FW_BOOL force_stop_asc(void *context,
                                        APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return cleanup_operation((FAKE_TRANSPORT *)context, TraceForceStopAsc);
}

static APPLE_AGX_FW_BOOL revalidate_uat(void *context,
                                        APPLE_AGX_FW_U64 deadline) {
  (void)deadline;
  return start_operation((FAKE_TRANSPORT *)context, TraceRevalidateUat);
}

static void record_phase(void *context, APPLE_AGX_FIRMWARE_PHASE phase,
                         APPLE_AGX_FIRMWARE_RESULT result,
                         APPLE_AGX_FW_U32 completed_mask) {
  FAKE_TRANSPORT *fake = (FAKE_TRANSPORT *)context;
  AppleAgxFirmwareCaptureStartFailure(&fake->FirstFailure,phase,result,completed_mask);
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
      unpublish_initdata,
      stop_endpoint,
      stop_asc,
      destroy_uat,
      power_off,
      record_phase,
      force_stop_asc,
      revalidate_uat,
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
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  };
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  fake.NowMs = 1000;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(firmware.Phase == AppleAgxFirmwareIdleTimestampUpdated);
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

  for (failure = 1; failure <= 9; ++failure) {
    APPLE_AGX_FIRMWARE firmware;
    FAKE_TRANSPORT fake = {0};
    APPLE_AGX_FIRMWARE_IO io = make_io(&fake);
    unsigned char expected[15];
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
    if (failure >= 3)
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
  assert(late.TraceCount == 6u);
  assert(late.Trace[0] == TracePowerOn);
  assert(late.Trace[1] == TraceCreateUat);
  assert(late.Trace[2] == TraceBootAsc);
  assert(late.Trace[3] == TraceStopAsc);
  assert(late.Trace[4] == TraceDestroyUat);
  assert(late.Trace[5] == TracePowerOff);
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
  firmware.Phase = AppleAgxFirmwareIdleTimestampUpdated;
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
  assert(fake.TraceCount == 15u);
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

static unsigned int count_trace(const FAKE_TRANSPORT *fake,
                                unsigned int from, unsigned char value) {
  unsigned int index;
  unsigned int count = 0;
  for (index = from; index < fake->TraceCount; ++index)
    if (fake->Trace[index] == value)
      ++count;
  return count;
}

static void assert_warm_stopped(const APPLE_AGX_FIRMWARE *firmware) {
  assert(firmware->Phase == AppleAgxFirmwareUatReady);
  assert(firmware->CompletedMask == APPLE_AGX_FIRMWARE_WARM_RETAINED);
  assert(firmware->CleanupMask == 0u);
}

/* Bug caught: a warm restart that skips a boot phase (the restarted firmware
 * never gets its endpoints, initdata or device-control init), or that
 * re-creates the UAT / cycles power (a new retained-root epoch makes every
 * GPUVA record stale; the broker refuses power-off with a live process). */
static void test_warm_restart_replays_boot_and_keeps_uat_and_power(void) {
  static const unsigned char expected_restart[] = {
      TraceUnpublishInitdata,     TraceStopDoorbellEndpoint,
      TraceStopFirmwareEndpoint,  TraceStopAsc,
      TraceRevalidateUat,         TraceBootAsc,
      TraceStartFirmwareEndpoint, TraceStartDoorbellEndpoint,
      TracePublishInitdata,       TraceSendInitdata,
      TraceDeviceControlInit,     TraceUpdateIdleTimestamp,
  };
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);
  unsigned int index;
  unsigned int cycle;

  fake.NowMs = 1000;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) == AppleAgxFirmwareResultOk);
  assert(fake.TraceCount == 9u);
  for (cycle = 0; cycle < 3u; ++cycle) {
    unsigned int base = fake.TraceCount;
    assert(AppleAgxFirmwareRestartWarm(&firmware, &io) ==
           AppleAgxFirmwareResultOk);
    assert(firmware.Phase == AppleAgxFirmwareIdleTimestampUpdated);
    assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_ALL_COMPLETED);
    assert(firmware.InitdataAddress == 0x1500000000ULL);
    assert(fake.TraceCount == base + sizeof(expected_restart));
    for (index = 0; index < sizeof(expected_restart); ++index)
      assert(fake.Trace[base + index] == expected_restart[index]);
    /* The warm boot is the cold boot from BootAsc on, phase for phase. */
    for (index = 0; index < 7u; ++index)
      assert(fake.Trace[base + 5u + index] == fake.Trace[2u + index]);
  }
  assert(count_trace(&fake, 0u, TracePowerOn) == 1u);
  assert(count_trace(&fake, 0u, TraceCreateUat) == 1u);
  assert(count_trace(&fake, 0u, TraceDestroyUat) == 0u);
  assert(count_trace(&fake, 0u, TracePowerOff) == 0u);
  assert(count_trace(&fake, 0u, TraceForceStopAsc) == 0u);
  /* The retained resources still belong to the state machine. */
  assert(AppleAgxFirmwareRollback(&firmware, &io) == AppleAgxFirmwareResultOk);
  assert(firmware.Phase == AppleAgxFirmwareStopped);
  assert(count_trace(&fake, 0u, TraceDestroyUat) == 1u);
  assert(count_trace(&fake, 0u, TracePowerOff) == 1u);
}

/* Bug caught: an orderly stop whose ASC step fails (IOP ack timeout) leaves
 * the state machine where no start is accepted and nothing leads out; or the
 * orderly path silently clears RUN on a firmware that never acknowledged.
 * Required: the half-stopped state refuses a start without touching anything,
 * resumes, and has exactly one explicit way to a full second boot. */
static void test_warm_stop_failure_has_an_explicit_way_to_a_second_boot(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);
  unsigned int base;

  fake.NowMs = 1000;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) == AppleAgxFirmwareResultOk);
  fake.FailCleanupOperation = 4; /* StopAsc */
  assert(AppleAgxFirmwareStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultCleanupFailed);
  assert(firmware.Phase == AppleAgxFirmwareFailed);
  assert(firmware.CompletedMask ==
         (APPLE_AGX_FIRMWARE_WARM_RETAINED | APPLE_AGX_FIRMWARE_ASC_RUNNING));
  assert(count_trace(&fake, 0u, TraceForceStopAsc) == 0u);
  assert(count_trace(&fake, 0u, TraceDestroyUat) == 0u);
  assert(count_trace(&fake, 0u, TracePowerOff) == 0u);

  base = fake.TraceCount;
  assert(AppleAgxFirmwareStartWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(fake.TraceCount == base);
  assert(firmware.Phase == AppleAgxFirmwareFailed);

  /* Resume: only the unfinished step runs again, and it still fails. */
  fake.FailCleanupOperation = 5;
  assert(AppleAgxFirmwareStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultCleanupFailed);
  assert(fake.TraceCount == base + 1u);
  assert(fake.Trace[base] == TraceStopAsc);

  base = fake.TraceCount;
  assert(AppleAgxFirmwareForceStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert_warm_stopped(&firmware);
  assert(fake.TraceCount == base + 1u);
  assert(fake.Trace[base] == TraceForceStopAsc);

  base = fake.TraceCount;
  assert(AppleAgxFirmwareStartWarm(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(firmware.Phase == AppleAgxFirmwareIdleTimestampUpdated);
  assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_ALL_COMPLETED);
  assert(fake.TraceCount == base + 8u);
  assert(fake.Trace[base] == TraceRevalidateUat);
  assert(fake.Trace[base + 1u] == TraceBootAsc);
  assert(fake.Trace[base + 7u] == TraceUpdateIdleTimestamp);
}

/* Bug caught: a forced stop that still runs the orderly callbacks, i.e.
 * sends StopEndpoint / power-state messages to a firmware that is dead and
 * waits for their acks inside ResetFromTimeout. */
static void test_forced_warm_stop_runs_only_the_force_callback(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);
  APPLE_AGX_FIRMWARE before;

  fake.NowMs = 1000;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStart(&firmware, &io) == AppleAgxFirmwareResultOk);

  io.ForceStopAsc = NULL;
  before = firmware;
  assert(AppleAgxFirmwareForceStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(firmware.Phase == before.Phase &&
         firmware.CompletedMask == before.CompletedMask);
  assert(fake.TraceCount == 9u);
  io.ForceStopAsc = force_stop_asc;

  fake.FailCleanupOperation = 1;
  assert(AppleAgxFirmwareForceStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultCleanupFailed);
  assert(firmware.Phase == AppleAgxFirmwareFailed);
  /* A force that is not proven changes no ownership bit. */
  assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_ALL_COMPLETED);

  assert(AppleAgxFirmwareForceStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert_warm_stopped(&firmware);
  assert(firmware.InitdataAddress == 0u);
  assert(fake.TraceCount == 11u);
  assert(fake.Trace[9] == TraceForceStopAsc);
  assert(fake.Trace[10] == TraceForceStopAsc);
  /* Asked again on a stopped firmware it proves the stop again. */
  assert(AppleAgxFirmwareForceStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  assert(fake.TraceCount == 12u && fake.Trace[11] == TraceForceStopAsc);
  assert_warm_stopped(&firmware);
}

/* Bug caught: a warm boot that fails half-way falls into the cold rollback
 * and destroys the UAT / powers off under live GPU processes, or leaves the
 * state machine unable to try again. */
static void test_failed_warm_start_stays_warm_and_can_retry(void) {
  unsigned int step;

  for (step = 1; step <= 8u; ++step) {
    APPLE_AGX_FIRMWARE firmware;
    FAKE_TRANSPORT fake = {0};
    APPLE_AGX_FIRMWARE_IO io = make_io(&fake);
    unsigned int base;

    fake.NowMs = 1000;
    AppleAgxFirmwareInitialize(&firmware);
    assert(AppleAgxFirmwareStart(&firmware, &io) == AppleAgxFirmwareResultOk);
    assert(AppleAgxFirmwareStopWarm(&firmware, &io) ==
           AppleAgxFirmwareResultOk);
    assert_warm_stopped(&firmware);
    /* Stopping twice is not a second stop. */
    base = fake.TraceCount;
    assert(AppleAgxFirmwareStopWarm(&firmware, &io) ==
           AppleAgxFirmwareResultOk);
    assert(fake.TraceCount == base);

    fake.FailStartOperation = 9u + step;
    assert(AppleAgxFirmwareStartWarm(&firmware, &io) ==
           AppleAgxFirmwareResultTransportFailed);
    assert_warm_stopped(&firmware);
    assert(firmware.LastResult == AppleAgxFirmwareResultTransportFailed);
    assert(count_trace(&fake, 0u, TraceDestroyUat) == 0u);
    assert(count_trace(&fake, 0u, TracePowerOff) == 0u);
    assert(count_trace(&fake, 0u, TraceForceStopAsc) == 0u);
    /* A boot attempt that reached BootAsc owes exactly one StopAsc. */
    assert(count_trace(&fake, base, TraceStopAsc) == (step >= 2u ? 1u : 0u));

    base = fake.TraceCount;
    assert(AppleAgxFirmwareStartWarm(&firmware, &io) ==
           AppleAgxFirmwareResultOk);
    assert(firmware.CompletedMask == APPLE_AGX_FIRMWARE_ALL_COMPLETED);
    assert(fake.TraceCount == base + 8u);
  }
}

/* Bug caught: a warm entry point accepted on a firmware that holds no UAT or
 * power (boots an ASC against no page tables), or a warm start that runs
 * without the revalidation step and publishes a stale initdata address. */
static void test_warm_entry_points_refuse_states_they_do_not_own(void) {
  APPLE_AGX_FIRMWARE firmware;
  FAKE_TRANSPORT fake = {0};
  APPLE_AGX_FIRMWARE_IO io = make_io(&fake);

  fake.NowMs = 1000;
  AppleAgxFirmwareInitialize(&firmware);
  assert(AppleAgxFirmwareStartWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(AppleAgxFirmwareStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(AppleAgxFirmwareForceStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(AppleAgxFirmwareRestartWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(firmware.Phase == AppleAgxFirmwareOff && fake.TraceCount == 0u);

  assert(AppleAgxFirmwareStart(&firmware, &io) == AppleAgxFirmwareResultOk);
  /* Running: a warm start is not a restart. */
  assert(AppleAgxFirmwareStartWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(fake.TraceCount == 9u);
  assert(AppleAgxFirmwareStopWarm(&firmware, &io) ==
         AppleAgxFirmwareResultOk);
  io.RevalidateFirmwareUat = NULL;
  assert(AppleAgxFirmwareStartWarm(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert_warm_stopped(&firmware);
  assert(fake.TraceCount == 13u);
  /* The cold start must not adopt a warm-stopped firmware either. */
  assert(AppleAgxFirmwareStart(&firmware, &io) ==
         AppleAgxFirmwareResultInvalid);
  assert(fake.TraceCount == 13u);
  assert_warm_stopped(&firmware);
}

int main(void) {
  {
    FAKE_TRANSPORT fake={0};
    APPLE_AGX_FIRMWARE_IO io=make_io(&fake);
    APPLE_AGX_FIRMWARE firmware;
    fake.FailStartOperation=7;fake.FailCleanupOperation=2;
    AppleAgxFirmwareInitialize(&firmware);
    assert(AppleAgxFirmwareStart(&firmware,&io)==AppleAgxFirmwareResultCleanupFailed);
    assert(fake.FirstFailure.Captured);
    assert(fake.FirstFailure.Result==AppleAgxFirmwareResultTransportFailed);
    assert(fake.FirstFailure.CompletedMask==0x3fu);
    fake.FailCleanupOperation=0;
    assert(AppleAgxFirmwareRollback(&firmware,&io)==AppleAgxFirmwareResultOk);
    assert(fake.FirstFailure.Result==AppleAgxFirmwareResultTransportFailed);
    assert(fake.FirstFailure.CompletedMask==0x3fu);
  }
  test_ordered_start_and_idempotent_rollback();
  test_failure_after_every_start_operation_rolls_back_exactly();
  test_deadline_equality_passes_and_one_tick_late_fails();
  test_clock_regression_and_deadline_overflow_fail_closed();
  test_invalid_contracts_and_unknown_bits_are_rejected();
  test_invalid_initdata_address_rolls_back();
  test_cleanup_failure_keeps_exact_resource_bit();
  test_cleanup_timeout_is_failure();
  test_cleanup_clock_regression_is_failure();
  test_warm_restart_replays_boot_and_keeps_uat_and_power();
  test_warm_stop_failure_has_an_explicit_way_to_a_second_boot();
  test_forced_warm_stop_runs_only_the_force_callback();
  test_failed_warm_start_stays_warm_and_can_retry();
  test_warm_entry_points_refuse_states_they_do_not_own();
  return 0;
}
