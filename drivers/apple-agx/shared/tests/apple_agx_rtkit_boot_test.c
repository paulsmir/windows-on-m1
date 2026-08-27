#include "apple_agx_rtkit_boot.h"

#include <assert.h>

static APPLE_AGX_RTKIT_BOOT_OUTPUT step(
    APPLE_AGX_RTKIT_BOOT *boot, APPLE_AGX_RTKIT_U64 payload) {
  APPLE_AGX_RTKIT_BOOT_OUTPUT output;
  assert(AppleAgxRtkitBootHandle(boot, payload, 0u, &output) ==
         AppleAgxRtkitBootResultOk);
  return output;
}

static void test_exact_management_handshake(void) {
  APPLE_AGX_RTKIT_BOOT boot;
  APPLE_AGX_RTKIT_BOOT_OUTPUT output;

  AppleAgxRtkitBootInitialize(&boot);
  assert(boot.Phase == AppleAgxRtkitBootAwaitingHello);
  assert(AppleAgxRtkitBootBegin(&boot, &output) == AppleAgxRtkitBootResultOk);
  assert(output.Count == 1u);
  assert(output.Message[0] == 0x0060000000000220ULL);

  output = step(&boot, 0x0010000000040001ULL);
  assert(output.Count == 1u);
  assert(output.Message[0] == 0x0020000000040004ULL);
  assert(boot.NegotiatedVersion == 4u);

  output = step(&boot, 0x0080000000000003ULL);
  assert(output.Count == 1u);
  assert(output.Message[0] == 0x0080000000000001ULL);

  output = step(&boot, 0x0088000100000001ULL);
  assert(output.Count == 2u);
  assert(output.Message[0] == 0x0088000100000000ULL);
  assert(output.Message[1] == 0x00b0000000000020ULL);

  output = step(&boot, 0x0070000000000020ULL);
  assert(output.Count == 0u);
  assert(boot.Phase == AppleAgxRtkitBootAwaitingPower);

  output = step(&boot, 0x00b0000000000020ULL);
  assert(output.Count == 0u);
  assert(boot.Phase == AppleAgxRtkitBootReady);
  assert(AppleAgxRtkitBootIsReady(&boot));
}

static void test_power_ack_order_is_not_assumed(void) {
  APPLE_AGX_RTKIT_BOOT boot;
  APPLE_AGX_RTKIT_BOOT_OUTPUT output;

  AppleAgxRtkitBootInitialize(&boot);
  assert(AppleAgxRtkitBootBegin(&boot, &output) == AppleAgxRtkitBootResultOk);
  (void)step(&boot, 0x0010000000040001ULL);
  (void)step(&boot, 0x0088000000000000ULL);
  (void)step(&boot, 0x00b0000000000020ULL);
  assert(!AppleAgxRtkitBootIsReady(&boot));
  (void)step(&boot, 0x0070000000000020ULL);
  assert(AppleAgxRtkitBootIsReady(&boot));
}

static void test_protocol_violation_fails_closed(void) {
  APPLE_AGX_RTKIT_BOOT boot;
  APPLE_AGX_RTKIT_BOOT_OUTPUT output;

  AppleAgxRtkitBootInitialize(&boot);
  assert(AppleAgxRtkitBootBegin(&boot, &output) == AppleAgxRtkitBootResultOk);
  assert(AppleAgxRtkitBootHandle(&boot, 0x0010000000040001ULL, 1u,
                                &output) ==
         AppleAgxRtkitBootResultProtocolViolation);
  assert(boot.Phase == AppleAgxRtkitBootFailed);
  assert(AppleAgxRtkitBootHandle(&boot, 0x0010000000040001ULL, 0u,
                                &output) ==
         AppleAgxRtkitBootResultInvalidState);
  assert(AppleAgxRtkitBootBegin(&boot, &output) ==
         AppleAgxRtkitBootResultInvalidState);
}

static void test_rejects_bad_versions_and_power_states(void) {
  APPLE_AGX_RTKIT_BOOT boot;
  APPLE_AGX_RTKIT_BOOT_OUTPUT output;

  AppleAgxRtkitBootInitialize(&boot);
  assert(AppleAgxRtkitBootBegin(&boot, &output) == AppleAgxRtkitBootResultOk);
  assert(AppleAgxRtkitBootHandle(&boot, 0x0010000000010004ULL, 0u,
                                &output) ==
         AppleAgxRtkitBootResultProtocolViolation);

  AppleAgxRtkitBootInitialize(&boot);
  assert(AppleAgxRtkitBootBegin(&boot, &output) == AppleAgxRtkitBootResultOk);
  (void)step(&boot, 0x0010000000040001ULL);
  assert(AppleAgxRtkitBootHandle(&boot, 0x0070000000000010ULL, 0u,
                                &output) ==
         AppleAgxRtkitBootResultProtocolViolation);
}

int main(void) {
  test_exact_management_handshake();
  test_power_ack_order_is_not_assumed();
  test_protocol_violation_fails_closed();
  test_rejects_bad_versions_and_power_states();
  return 0;
}
