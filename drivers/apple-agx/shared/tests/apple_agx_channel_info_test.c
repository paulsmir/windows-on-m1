#include "apple_agx_channel_info.h"

#include <assert.h>
#include <string.h>

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

static void assert_all_zero(const unsigned char *bytes, unsigned int size) {
  unsigned int index;
  for (index = 0u; index < size; ++index)
    assert(bytes[index] == 0u);
}

static APPLE_AGX_CHANNEL_INFO_INPUT valid_input(void) {
  APPLE_AGX_CHANNEL_INFO_INPUT input;
  unsigned int index;
  for (index = 0u; index < J313_AGX_G2_CHANNEL_INFO_COUNT; ++index) {
    input.Entries[index].StateAddress =
        J313_AGX_G2_KERNEL_VA_BASE + 0x100000ULL + index * 0x10000ULL;
    input.Entries[index].RingAddress =
        J313_AGX_G2_KERNEL_VA_BASE + 0x300000ULL + index * 0x20000ULL;
  }
  return input;
}

static void test_golden_channel_info_set(void) {
  APPLE_AGX_CHANNEL_INFO_INPUT input = valid_input();
  APPLE_AGX_CHANNEL_INFO_MANIFEST manifest;
  unsigned char output[J313_AGX_G2_CHANNEL_INFO_SET_SIZE] = {0};
  unsigned int index;

  memset(&manifest, 0, sizeof(manifest));
  assert(AppleAgxChannelInfoEncodeG13V13_5(
             &input, output, sizeof(output), &manifest) ==
         AppleAgxChannelInfoResultOk);
  for (index = 0u; index < J313_AGX_G2_CHANNEL_INFO_COUNT; ++index) {
    assert(read_u64(output + index * J313_AGX_G2_CHANNEL_INFO_SIZE) ==
           input.Entries[index].StateAddress);
    assert(read_u64(output + index * J313_AGX_G2_CHANNEL_INFO_SIZE + 8u) ==
           input.Entries[index].RingAddress);
  }
  assert(manifest.EncodedSize == sizeof(output));
  assert(manifest.ChannelCount == J313_AGX_G2_CHANNEL_INFO_COUNT);
}

static void expect_address_failure(APPLE_AGX_CHANNEL_INFO_INPUT input) {
  APPLE_AGX_CHANNEL_INFO_MANIFEST manifest;
  unsigned char output[J313_AGX_G2_CHANNEL_INFO_SET_SIZE] = {0};
  memset(&manifest, 0xa5, sizeof(manifest));
  assert(AppleAgxChannelInfoEncodeG13V13_5(
             &input, output, sizeof(output), &manifest) ==
         AppleAgxChannelInfoResultAddress);
  assert_all_zero(output, sizeof(output));
  assert(manifest.EncodedSize == 0xa5a5a5a5u);
}

static void test_rejections_preserve_output(void) {
  APPLE_AGX_CHANNEL_INFO_INPUT input = valid_input();
  APPLE_AGX_CHANNEL_INFO_MANIFEST manifest;
  unsigned char output[J313_AGX_G2_CHANNEL_INFO_SET_SIZE] = {0};

  assert(AppleAgxChannelInfoEncodeG13V13_5(
             0, output, sizeof(output), &manifest) ==
         AppleAgxChannelInfoResultInvalidArgument);
  assert(AppleAgxChannelInfoEncodeG13V13_5(
             &input, 0, sizeof(output), &manifest) ==
         AppleAgxChannelInfoResultInvalidArgument);
  assert(AppleAgxChannelInfoEncodeG13V13_5(
             &input, output, sizeof(output), 0) ==
         AppleAgxChannelInfoResultInvalidArgument);
  assert(AppleAgxChannelInfoEncodeG13V13_5(
             &input, output, sizeof(output) - 1u, &manifest) ==
         AppleAgxChannelInfoResultDestinationSize);
  assert_all_zero(output, sizeof(output));

  output[3u] = 0x5au;
  memset(&manifest, 0xa5, sizeof(manifest));
  assert(AppleAgxChannelInfoEncodeG13V13_5(
             &input, output, sizeof(output), &manifest) ==
         AppleAgxChannelInfoResultDestinationNotZero);
  assert(output[3u] == 0x5au);
  assert(manifest.EncodedSize == 0xa5a5a5a5u);
  output[3u] = 0u;

  input = valid_input();
  input.Entries[0].StateAddress += 1ULL;
  expect_address_failure(input);
  input = valid_input();
  input.Entries[2].RingAddress = 0x10000ULL;
  expect_address_failure(input);
  input = valid_input();
  input.Entries[4].RingAddress = input.Entries[4].StateAddress;
  expect_address_failure(input);
  input = valid_input();
  input.Entries[8].RingAddress = input.Entries[3].RingAddress;
  expect_address_failure(input);
}

int main(void) {
  test_golden_channel_info_set();
  test_rejections_preserve_output();
  return 0;
}
