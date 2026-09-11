#include "apple_agx_firmware_status.h"

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

static APPLE_AGX_FIRMWARE_STATUS_INPUT valid_input(void) {
  APPLE_AGX_FIRMWARE_STATUS_INPUT input;
  input.StateAddress = 0xffffffa00003c000ULL;
  input.RingAddress = 0xffffffa000044000ULL;
  return input;
}

static void test_golden_firmware_status(void) {
  APPLE_AGX_FIRMWARE_STATUS_INPUT input = valid_input();
  APPLE_AGX_FIRMWARE_STATUS_MANIFEST manifest;
  unsigned char output[J313_AGX_G2_INITDATA_FW_STATUS_SIZE] = {0};

  memset(&manifest, 0, sizeof(manifest));
  assert(AppleAgxFirmwareStatusEncodeG13V13_5(
             &input, output, sizeof(output), &manifest) ==
         AppleAgxFirmwareStatusResultOk);
  assert(read_u64(output + 0x00u) == input.StateAddress);
  assert(read_u64(output + 0x08u) == input.RingAddress);
  assert_all_zero(output + 0x10u, sizeof(output) - 0x10u);
  assert(manifest.EncodedSize == sizeof(output));
  assert(manifest.StateAddress == input.StateAddress);
  assert(manifest.RingAddress == input.RingAddress);
}

static void expect_address_failure(APPLE_AGX_FIRMWARE_STATUS_INPUT input) {
  APPLE_AGX_FIRMWARE_STATUS_MANIFEST manifest;
  unsigned char output[J313_AGX_G2_INITDATA_FW_STATUS_SIZE] = {0};
  memset(&manifest, 0xa5, sizeof(manifest));
  assert(AppleAgxFirmwareStatusEncodeG13V13_5(
             &input, output, sizeof(output), &manifest) ==
         AppleAgxFirmwareStatusResultAddress);
  assert_all_zero(output, sizeof(output));
  assert(manifest.EncodedSize == 0xa5a5a5a5u);
}

static void test_rejections_preserve_output(void) {
  APPLE_AGX_FIRMWARE_STATUS_INPUT input = valid_input();
  APPLE_AGX_FIRMWARE_STATUS_MANIFEST manifest;
  unsigned char output[J313_AGX_G2_INITDATA_FW_STATUS_SIZE] = {0};

  assert(AppleAgxFirmwareStatusEncodeG13V13_5(
             0, output, sizeof(output), &manifest) ==
         AppleAgxFirmwareStatusResultInvalidArgument);
  assert(AppleAgxFirmwareStatusEncodeG13V13_5(
             &input, 0, sizeof(output), &manifest) ==
         AppleAgxFirmwareStatusResultInvalidArgument);
  assert(AppleAgxFirmwareStatusEncodeG13V13_5(
             &input, output, sizeof(output), 0) ==
         AppleAgxFirmwareStatusResultInvalidArgument);
  assert(AppleAgxFirmwareStatusEncodeG13V13_5(
             &input, output, sizeof(output) - 1u, &manifest) ==
         AppleAgxFirmwareStatusResultDestinationSize);
  assert_all_zero(output, sizeof(output));

  output[0x20u] = 0x5au;
  memset(&manifest, 0xa5, sizeof(manifest));
  assert(AppleAgxFirmwareStatusEncodeG13V13_5(
             &input, output, sizeof(output), &manifest) ==
         AppleAgxFirmwareStatusResultDestinationNotZero);
  assert(output[0x20u] == 0x5au);
  assert(manifest.EncodedSize == 0xa5a5a5a5u);
  output[0x20u] = 0u;

  input = valid_input();
  input.StateAddress += 1ULL;
  expect_address_failure(input);
  input = valid_input();
  input.RingAddress = 0x10000ULL;
  expect_address_failure(input);
  input = valid_input();
  input.RingAddress = input.StateAddress;
  expect_address_failure(input);
}

int main(void) {
  test_golden_firmware_status();
  test_rejections_preserve_output();
  return 0;
}
