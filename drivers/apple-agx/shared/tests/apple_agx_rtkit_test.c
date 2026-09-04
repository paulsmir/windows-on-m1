#include "apple_agx_rtkit.h"

#include <assert.h>

static void test_literal_message_vectors(void) {
  assert(AppleAgxRtkitHelloAck(1u, 4u) == 0x0020000000040001ULL);
  assert(AppleAgxRtkitEndpointMapAck(3u, 1u, 0u) ==
         0x0088000300000000ULL);
  assert(AppleAgxRtkitEndpointMapAck(2u, 0u, 1u) ==
         0x0080000200000001ULL);
  assert(AppleAgxRtkitSetIopPower(0x220u) == 0x0060000000000220ULL);
  assert(AppleAgxRtkitSetApPower(0x20u) == 0x00b0000000000020ULL);
  assert(AppleAgxRtkitStartEndpoint(0x20u, 2u) ==
         0x0050002000000002ULL);
  assert(AppleAgxRtkitStartEndpoint(0x21u, 2u) ==
         0x0050002100000002ULL);
  assert(AppleAgxRtkitInitdata(0x00000abcde000ULL) ==
         0x00810000abcde000ULL);
}

static void test_management_decode_is_bounded(void) {
  APPLE_AGX_RTKIT_MANAGEMENT decoded;

  assert(AppleAgxRtkitDecodeManagement(0x0060000000000220ULL, &decoded));
  assert(decoded.Type == AppleAgxRtkitManagementSetIopPower);
  assert(decoded.State == 0x220u);
  assert(decoded.Endpoint == 0u);
  assert(decoded.Flag == 0u);

  assert(AppleAgxRtkitDecodeManagement(0x0050002000000002ULL, &decoded));
  assert(decoded.Type == AppleAgxRtkitManagementStartEndpoint);
  assert(decoded.State == 0u);
  assert(decoded.Endpoint == 0x20u);
  assert(decoded.Flag == 2u);

  assert(AppleAgxRtkitDecodeManagement(0x0010000000040001ULL, &decoded));
  assert(decoded.Type == AppleAgxRtkitManagementHello);
  assert(decoded.MinVersion == 1u);
  assert(decoded.MaxVersion == 4u);

  assert(AppleAgxRtkitDecodeManagement(0x0088000300000005ULL, &decoded));
  assert(decoded.Type == AppleAgxRtkitManagementEndpointMap);
  assert(decoded.Last == 1u);
  assert(decoded.Base == 3u);
  assert(decoded.Bitmap == 5u);

  assert(AppleAgxRtkitDecodeManagement(0x0070000000000020ULL, &decoded));
  assert(decoded.Type == AppleAgxRtkitManagementIopPowerAck);
  assert(decoded.State == 0x20u);

  assert(AppleAgxRtkitDecodeManagement(0x00b0000000000020ULL, &decoded));
  assert(decoded.Type == AppleAgxRtkitManagementSetApPower);
  assert(decoded.State == 0x20u);

  assert(!AppleAgxRtkitDecodeManagement(0x00f0000000000000ULL,
                                        &decoded));
  assert(!AppleAgxRtkitDecodeManagement(0x0060000000010220ULL,
                                        &decoded));
  assert(!AppleAgxRtkitDecodeManagement(0x0051002000000002ULL,
                                        &decoded));
  assert(!AppleAgxRtkitDecodeManagement(0x0060000000000220ULL, 0));
}

static void test_invalid_encode_inputs_are_not_truncated(void) {
  assert(AppleAgxRtkitHelloAck(5u, 4u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitHelloAck(0x10000u, 0x10000u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitEndpointMapAck(8u, 0u, 0u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitEndpointMapAck(0u, 2u, 0u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitEndpointMapAck(0u, 0u, 2u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitSetIopPower(0x10000u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitSetApPower(0x10000u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitStartEndpoint(0x100u, 2u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitStartEndpoint(0x20u, 4u) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
  assert(AppleAgxRtkitInitdata(1ULL << 44) ==
         APPLE_AGX_RTKIT_INVALID_MESSAGE);
}

static void test_endpoint_selector_rejects_reserved_bits(void) {
  APPLE_AGX_RTKIT_U32 endpoint = 0xffffffffu;

  assert(AppleAgxRtkitDecodeEndpoint(0x21ULL, &endpoint));
  assert(endpoint == 0x21u);
  assert(!AppleAgxRtkitDecodeEndpoint(0x100ULL, &endpoint));
  assert(!AppleAgxRtkitDecodeEndpoint(0x20ULL, 0));
}

int main(void) {
  test_literal_message_vectors();
  test_management_decode_is_bounded();
  test_invalid_encode_inputs_are_not_truncated();
  test_endpoint_selector_rejects_reserved_bits();
  return 0;
}
