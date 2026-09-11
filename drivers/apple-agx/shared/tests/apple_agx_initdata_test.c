#include "apple_agx_initdata.h"

#include <assert.h>
#include <string.h>

#define INITDATA_SIZE 0xbcU
#define PAGE_SIZE 0x4000ULL

static unsigned short read_u16(const unsigned char *bytes) {
  return (unsigned short)(bytes[0] | ((unsigned short)bytes[1] << 8));
}

static unsigned int read_u32(const unsigned char *bytes) {
  return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8) |
         ((unsigned int)bytes[2] << 16) | ((unsigned int)bytes[3] << 24);
}

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0; index < 8u; ++index) {
    value |= (unsigned long long)bytes[index] << (index * 8u);
  }
  return value;
}

static APPLE_AGX_INITDATA_INPUT valid_input(void) {
  APPLE_AGX_INITDATA_INPUT input;
  input.TaggedBufferAddress = 0xffffff8000010000ULL;
  input.RuntimePointersAddress = 0xffffff8000014000ULL;
  input.GlobalsAddress = 0xffffff8000018000ULL;
  input.FirmwareStatusAddress = 0xffffff800001c000ULL;
  return input;
}

static void assert_all_zero(const unsigned char *bytes, unsigned int size) {
  unsigned int index;
  for (index = 0; index < size; ++index) {
    assert(bytes[index] == 0u);
  }
}

static void test_golden_envelope(void) {
  static const unsigned int offsets[] = {0x34u, 0x54u, 0x74u};
  static const unsigned int shifts[] = {36u, 25u, 14u};
  static const unsigned int entries[] = {8u, 2048u, 2048u};
  APPLE_AGX_INITDATA_INPUT input = valid_input();
  APPLE_AGX_INITDATA_MANIFEST manifest;
  unsigned char output[INITDATA_SIZE] = {0};
  unsigned char used[INITDATA_SIZE] = {0};
  unsigned int index;
  unsigned int byte;

  memset(&manifest, 0, sizeof(manifest));
  assert(AppleAgxInitdataEncodeG13V13_5(&input, output, sizeof(output),
                                       &manifest) ==
         AppleAgxInitdataResultOk);
  assert(output[0x00] == 0xa0 && output[0x01] == 0x6b);
  assert(output[0x02] == 0x28 && output[0x03] == 0x1f);
  assert(output[0x04] == 0x01 && output[0x05] == 0x06);
  assert(output[0x06] == 0xb0 && output[0x07] == 0x00);
  assert(read_u64(output + 0x08) == input.TaggedBufferAddress);
  assert(read_u32(output + 0x10) == 0u);
  assert(read_u32(output + 0x14) == 0u);
  assert(read_u64(output + 0x18) == input.RuntimePointersAddress);
  assert(read_u64(output + 0x20) == input.GlobalsAddress);
  assert(read_u64(output + 0x28) == input.FirmwareStatusAddress);
  assert(read_u16(output + 0x30) == 0x4000u);
  assert(output[0x32] == 14u && output[0x33] == 3u);
  for (index = 0; index < 3u; ++index) {
    unsigned int offset = offsets[index];
    assert(output[offset + 0u] == 8u);
    assert(output[offset + 1u] == 14u);
    assert(output[offset + 2u] == 14u);
    assert(output[offset + 3u] == shifts[index]);
    assert(read_u16(output + offset + 4u) == entries[index]);
    assert(read_u16(output + offset + 6u) == 0x4000u);
    assert(read_u64(output + offset + 8u) == 1ULL);
    assert(read_u64(output + offset + 16u) == 0x000000ffffffc000ULL);
    assert(read_u64(output + offset + 24u) ==
           ((unsigned long long)(entries[index] - 1u) << shifts[index]));
  }
  assert(read_u32(output + 0xa8) == 1u);
  assert(read_u32(output + 0xac) == 0u);
  assert(read_u32(output + 0xb0) == 0u);
  assert(read_u32(output + 0xb4) == 0u);
  assert(read_u32(output + 0xb8) == 0u);

  for (byte = 0; byte < 0x10u; ++byte) used[byte] = 1u;
  for (byte = 0x18u; byte < 0x94u; ++byte) used[byte] = 1u;
  for (byte = 0xa8u; byte < INITDATA_SIZE; ++byte) used[byte] = 1u;
  for (byte = 0; byte < INITDATA_SIZE; ++byte) {
    if (used[byte] == 0u) assert(output[byte] == 0u);
  }

  assert(manifest.EncodedSize == INITDATA_SIZE);
  assert(manifest.VersionWords[0] == 0x6ba0u);
  assert(manifest.VersionWords[1] == 0x1f28u);
  assert(manifest.VersionWords[2] == 0x0601u);
  assert(manifest.VersionWords[3] == 0x00b0u);
  assert(manifest.ReferencedAddresses[0] == input.TaggedBufferAddress);
  assert(manifest.ReferencedAddresses[1] == input.RuntimePointersAddress);
  assert(manifest.ReferencedAddresses[2] == input.GlobalsAddress);
  assert(manifest.ReferencedAddresses[3] == input.FirmwareStatusAddress);
}

static void expect_input_failure(APPLE_AGX_INITDATA_INPUT input,
                                 APPLE_AGX_INITDATA_RESULT expected) {
  APPLE_AGX_INITDATA_MANIFEST manifest;
  unsigned char output[INITDATA_SIZE] = {0};
  memset(&manifest, 0xa5, sizeof(manifest));
  assert(AppleAgxInitdataEncodeG13V13_5(&input, output, sizeof(output),
                                       &manifest) == expected);
  assert_all_zero(output, sizeof(output));
  assert(manifest.EncodedSize == 0xa5a5a5a5u);
}

static void test_rejections_preserve_output(void) {
  APPLE_AGX_INITDATA_INPUT input = valid_input();
  APPLE_AGX_INITDATA_MANIFEST manifest;
  unsigned char output[INITDATA_SIZE] = {0};

  assert(AppleAgxInitdataEncodeG13V13_5(0, output, sizeof(output), &manifest) ==
         AppleAgxInitdataResultInvalidArgument);
  assert(AppleAgxInitdataEncodeG13V13_5(&input, 0, sizeof(output), &manifest) ==
         AppleAgxInitdataResultInvalidArgument);
  assert(AppleAgxInitdataEncodeG13V13_5(&input, output, sizeof(output), 0) ==
         AppleAgxInitdataResultInvalidArgument);
  assert(AppleAgxInitdataEncodeG13V13_5(&input, output, sizeof(output) - 1u,
                                       &manifest) ==
         AppleAgxInitdataResultDestinationSize);
  assert(AppleAgxInitdataEncodeG13V13_5(&input, output, sizeof(output) + 1u,
                                       &manifest) ==
         AppleAgxInitdataResultDestinationSize);
  assert_all_zero(output, sizeof(output));

  output[0x40] = 0x5au;
  memset(&manifest, 0xa5, sizeof(manifest));
  assert(AppleAgxInitdataEncodeG13V13_5(&input, output, sizeof(output),
                                       &manifest) ==
         AppleAgxInitdataResultDestinationNotZero);
  assert(output[0x40] == 0x5au);
  output[0x40] = 0u;

  input = valid_input();
  input.TaggedBufferAddress += 1ULL;
  expect_input_failure(input, AppleAgxInitdataResultAddress);
  input = valid_input();
  input.RuntimePointersAddress = 0x10000ULL;
  expect_input_failure(input, AppleAgxInitdataResultAddress);
  input = valid_input();
  input.GlobalsAddress = 0x0000008000000000ULL;
  expect_input_failure(input, AppleAgxInitdataResultAddress);
  input = valid_input();
  input.FirmwareStatusAddress = input.TaggedBufferAddress;
  expect_input_failure(input, AppleAgxInitdataResultOverlap);
}

int main(void) {
  test_golden_envelope();
  test_rejections_preserve_output();
  return 0;
}
