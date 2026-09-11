#include "apple_agx_state.h"

#include <assert.h>
#include <stddef.h>

static void reach_running(APPLE_AGX_STATE *state) {
  AppleAgxStateInitialize(state);
  assert(AppleAgxStateValidateResources(state));
  assert(AppleAgxStateTakeFirmwareOwnership(state));
  assert(AppleAgxStateMarkQueueReady(state));
  assert(AppleAgxStateStart(state));
  assert(state->Phase == AppleAgxPhaseRunning);
}

static void test_ordered_lifecycle_and_partial_start_rejection(void) {
  APPLE_AGX_STATE state;

  AppleAgxStateInitialize(&state);
  assert(state.Phase == AppleAgxPhaseOff);
  assert(!AppleAgxStateTakeFirmwareOwnership(&state));
  assert(!AppleAgxStateMarkQueueReady(&state));
  assert(!AppleAgxStateStart(&state));
  assert(AppleAgxStateValidateResources(&state));
  assert(!AppleAgxStateValidateResources(&state));
  assert(AppleAgxStateTakeFirmwareOwnership(&state));
  assert(AppleAgxStateMarkQueueReady(&state));
  assert(AppleAgxStateStart(&state));
}

static void test_mapping_inventory_is_bounded_and_wx_safe(void) {
  APPLE_AGX_STATE state;
  size_t index;

  AppleAgxStateInitialize(&state);
  assert(!AppleAgxStateMap(&state, 0x100000, 0x4000, AppleAgxMapRead));
  assert(AppleAgxStateValidateResources(&state));
  assert(AppleAgxStateTakeFirmwareOwnership(&state));
  assert(AppleAgxStateMap(&state, 0x100000, 0x4000,
                          AppleAgxMapRead | AppleAgxMapWrite));
  assert(!AppleAgxStateMap(&state, 0x100000, 0x4000, AppleAgxMapRead));
  assert(!AppleAgxStateMap(&state, 0x102000, 0x4000, AppleAgxMapRead));
  assert(!AppleAgxStateMap(&state, 0x200000, 0x4000,
                           AppleAgxMapWrite | AppleAgxMapExecute));
  assert(!AppleAgxStateMap(&state, 0x200001, 0x4000, AppleAgxMapRead));
  assert(!AppleAgxStateMap(&state, 0x200000, 0x2000, AppleAgxMapRead));
  assert(!AppleAgxStateMap(&state, (1ULL << 40) - 0x2000, 0x4000,
                           AppleAgxMapRead));
  assert(AppleAgxStateUnmap(&state, 0x100000, 0x4000));
  assert(!AppleAgxStateUnmap(&state, 0x100000, 0x4000));

  for (index = 0; index < APPLE_AGX_MAX_MAPPINGS; ++index) {
    assert(AppleAgxStateMap(&state, 0x400000 + index * 0x4000, 0x4000,
                            AppleAgxMapRead));
  }
  assert(!AppleAgxStateMap(&state, 0x800000, 0x4000, AppleAgxMapRead));
}

static void test_single_outstanding_monotonic_fence(void) {
  APPLE_AGX_STATE state;

  reach_running(&state);
  assert(!AppleAgxStateSubmitFence(&state, 0, 10));
  assert(AppleAgxStateSubmitFence(&state, 1, 10));
  assert(!AppleAgxStateSubmitFence(&state, 2, 11));
  assert(!AppleAgxStateCompleteFence(&state, 0));
  assert(!AppleAgxStateCompleteFence(&state, 2));
  assert(AppleAgxStateCompleteFence(&state, 1));
  assert(!AppleAgxStateCompleteFence(&state, 1));
  assert(AppleAgxStateSubmitFence(&state, 2, 20));
  assert(AppleAgxStateCompleteFence(&state, 2));
}

static void test_timeout_forces_reset_and_clean_stop(void) {
  APPLE_AGX_STATE state;

  reach_running(&state);
  assert(AppleAgxStateSubmitFence(&state, 1, 1000));
  assert(!AppleAgxStateCheckTimeout(&state, 1499));
  assert(!AppleAgxStateCheckTimeout(&state, 1500));
  assert(AppleAgxStateCheckTimeout(&state, 1501));
  assert(state.Phase == AppleAgxPhaseResetting);
  assert(!AppleAgxStateCompleteReset(&state));
  assert(AppleAgxStateDiscardOutstandingFence(&state));
  assert(AppleAgxStateCompleteReset(&state));
  assert(state.Phase == AppleAgxPhaseStopped);
  assert(AppleAgxStateValidateResources(&state));
}

static void test_clock_regression_forces_reset(void) {
  APPLE_AGX_STATE state;

  reach_running(&state);
  assert(AppleAgxStateSubmitFence(&state, 1, 1000));
  assert(AppleAgxStateCheckTimeout(&state, 999));
  assert(state.Phase == AppleAgxPhaseResetting);
}

static void test_failed_cleanup_cannot_report_stopped(void) {
  APPLE_AGX_STATE state;

  AppleAgxStateInitialize(&state);
  assert(AppleAgxStateValidateResources(&state));
  assert(AppleAgxStateTakeFirmwareOwnership(&state));
  assert(AppleAgxStateMap(&state, 0x100000, 0x4000, AppleAgxMapRead));
  AppleAgxStateFail(&state);
  assert(state.Phase == AppleAgxPhaseFailed);
  assert(!AppleAgxStateCompleteReset(&state));
  assert(AppleAgxStateBeginReset(&state));
  assert(!AppleAgxStateCompleteReset(&state));
  assert(AppleAgxStateUnmap(&state, 0x100000, 0x4000));
  assert(AppleAgxStateCompleteReset(&state));
  assert(state.Phase == AppleAgxPhaseStopped);
}

int main(void) {
  test_ordered_lifecycle_and_partial_start_rejection();
  test_mapping_inventory_is_bounded_and_wx_safe();
  test_single_outstanding_monotonic_fence();
  test_timeout_forces_reset_and_clean_stop();
  test_clock_regression_forces_reset();
  test_failed_cleanup_cannot_report_stopped();
  return 0;
}
