#include "apple_agx_uat.h"

#include <assert.h>

#define BIT64(_bit) (1ULL << (_bit))
#define PAGE_SIZE 0x4000ULL
#define BASE_BITS (BIT64(55) | BIT64(10) | 3ULL)

static void test_table_descriptor(void) {
  unsigned long long descriptor = 0;

  assert(AppleAgxUatEncodeTableDescriptor(0x12340000ULL, &descriptor) ==
         AppleAgxUatResultOk);
  assert(descriptor == 0x12340003ULL);
  assert(AppleAgxUatEncodeTableDescriptor(0x12340001ULL, &descriptor) ==
         AppleAgxUatResultMisaligned);
  assert(AppleAgxUatEncodeTableDescriptor(1ULL << 40, &descriptor) ==
         AppleAgxUatResultOutOfRange);
  assert(AppleAgxUatEncodeTableDescriptor(0x12340000ULL, 0) ==
         AppleAgxUatResultInvalidArgument);
}

static void test_page_descriptor_protections(void) {
  static const struct {
    APPLE_AGX_UAT_PROTECTION Protection;
    unsigned long long Bits;
  } firmware_cases[] = {
      {AppleAgxUatFirmwareDeviceReadWrite,
       BASE_BITS | BIT64(54) | BIT64(6) | BIT64(2)},
      {AppleAgxUatFirmwareSharedReadWrite,
       BASE_BITS | BIT64(54) | BIT64(6) | (2ULL << 2)},
      {AppleAgxUatFirmwarePrivateReadWrite,
       BASE_BITS | BIT64(54) | BIT64(6)},
      {AppleAgxUatFirmwareGpuPrivateReadWrite,
       BASE_BITS | BIT64(54) | BIT64(53)},
      {AppleAgxUatFirmwareReadWriteGpuReadOnly,
       BASE_BITS | BIT64(54) | BIT64(53) | BIT64(6)},
  };
  static const struct {
    APPLE_AGX_UAT_PROTECTION Protection;
    unsigned long long Bits;
  } gpu_cases[] = {
      {AppleAgxUatGpuSharedReadOnly,
       BASE_BITS | BIT64(7) | (2ULL << 2)},
      {AppleAgxUatGpuSharedWriteOnly,
       BASE_BITS | BIT64(53) | BIT64(7) | (2ULL << 2)},
      {AppleAgxUatGpuSharedReadWrite,
       BASE_BITS | BIT64(54) | BIT64(7) | (2ULL << 2)},
  };
  unsigned long long descriptor = 0;
  unsigned int index;

  for (index = 0; index < sizeof(firmware_cases) / sizeof(firmware_cases[0]);
       ++index) {
    assert(AppleAgxUatEncodePageDescriptor(
               0, 0x23400000ULL, firmware_cases[index].Protection,
               &descriptor) == AppleAgxUatResultOk);
    assert(descriptor == (0x23400000ULL | firmware_cases[index].Bits));
  }
  for (index = 0; index < sizeof(gpu_cases) / sizeof(gpu_cases[0]); ++index) {
    assert(AppleAgxUatEncodePageDescriptor(
               63, 0x23400000ULL, gpu_cases[index].Protection, &descriptor) ==
           AppleAgxUatResultOk);
    assert(descriptor ==
           (0x23400000ULL | gpu_cases[index].Bits | BIT64(11)));
  }

  assert(AppleAgxUatEncodePageDescriptor(
             0, 0x23400000ULL, AppleAgxUatFirmwareDeviceReadWrite,
             &descriptor) == AppleAgxUatResultOk);
  assert(descriptor == (0x23400000ULL | BIT64(55) | BIT64(54) | BIT64(10) |
                        BIT64(6) | BIT64(2) | 3ULL));
  assert(AppleAgxUatEncodePageDescriptor(
             63, 0x23400000ULL, AppleAgxUatFirmwareDeviceReadWrite,
             &descriptor) == AppleAgxUatResultUnsupportedProtection);
  assert(AppleAgxUatEncodePageDescriptor(
             0, 0x23400000ULL, AppleAgxUatGpuSharedReadWrite, &descriptor) ==
         AppleAgxUatResultUnsupportedProtection);
  assert(AppleAgxUatEncodePageDescriptor(
             64, 0x23400000ULL, AppleAgxUatGpuSharedReadWrite, &descriptor) ==
         AppleAgxUatResultUnsupportedContext);
  assert(AppleAgxUatEncodePageDescriptor(
             63, 0x23400001ULL, AppleAgxUatGpuSharedReadWrite, &descriptor) ==
         AppleAgxUatResultMisaligned);
  assert(AppleAgxUatEncodePageDescriptor(
             63, 1ULL << 40, AppleAgxUatGpuSharedReadWrite, &descriptor) ==
         AppleAgxUatResultOutOfRange);
  assert(AppleAgxUatEncodePageDescriptor(
             63, 0x23400000ULL, (APPLE_AGX_UAT_PROTECTION)99, &descriptor) ==
         AppleAgxUatResultUnsupportedProtection);
  assert(AppleAgxUatEncodePageDescriptor(
             63, 0x23400000ULL, AppleAgxUatGpuSharedReadWrite, 0) ==
         AppleAgxUatResultInvalidArgument);
}

static void expect_range(unsigned int context, unsigned long long va,
                         unsigned long long pa, unsigned long long length,
                         APPLE_AGX_UAT_PROTECTION protection,
                         APPLE_AGX_UAT_RESULT expected,
                         APPLE_AGX_UAT_HALF expected_half) {
  APPLE_AGX_UAT_HALF half = (APPLE_AGX_UAT_HALF)99;
  APPLE_AGX_UAT_RESULT result = AppleAgxUatValidateRange(
      context, va, pa, length, protection, &half);
  assert(result == expected);
  if (result == AppleAgxUatResultOk) {
    assert(half == expected_half);
  }
}

static void test_range_validation(void) {
  expect_range(63, 0, 0, PAGE_SIZE, AppleAgxUatGpuSharedReadWrite,
               AppleAgxUatResultOk, AppleAgxUatTtbr0);
  expect_range(63, 0x0000007fffffc000ULL, 0xffffffc000ULL, PAGE_SIZE,
               AppleAgxUatGpuSharedReadOnly, AppleAgxUatResultOk,
               AppleAgxUatTtbr0);
  expect_range(0, 0xffffff8000000000ULL, 0, PAGE_SIZE,
               AppleAgxUatFirmwarePrivateReadWrite, AppleAgxUatResultOk,
               AppleAgxUatTtbr1);
  expect_range(0, 0xffffffffffffc000ULL, 0xffffffc000ULL, PAGE_SIZE,
               AppleAgxUatFirmwarePrivateReadWrite, AppleAgxUatResultOk,
               AppleAgxUatTtbr1);

  expect_range(63, 0x0000008000000000ULL, 0, PAGE_SIZE,
               AppleAgxUatGpuSharedReadWrite, AppleAgxUatResultOutOfRange,
               AppleAgxUatTtbr0);
  expect_range(63, 0xffffff7ffffff000ULL, 0, PAGE_SIZE,
               AppleAgxUatGpuSharedReadWrite, AppleAgxUatResultMisaligned,
               AppleAgxUatTtbr0);
  expect_range(63, 0x0000007fffffc000ULL, 0, 2 * PAGE_SIZE,
               AppleAgxUatGpuSharedReadWrite, AppleAgxUatResultOutOfRange,
               AppleAgxUatTtbr0);
  expect_range(63, PAGE_SIZE, 0, 0, AppleAgxUatGpuSharedReadWrite,
               AppleAgxUatResultInvalidArgument, AppleAgxUatTtbr0);
  expect_range(63, PAGE_SIZE + 1, 0, PAGE_SIZE,
               AppleAgxUatGpuSharedReadWrite, AppleAgxUatResultMisaligned,
               AppleAgxUatTtbr0);
  expect_range(63, PAGE_SIZE, 1, PAGE_SIZE,
               AppleAgxUatGpuSharedReadWrite, AppleAgxUatResultMisaligned,
               AppleAgxUatTtbr0);
  expect_range(63, PAGE_SIZE, 0, PAGE_SIZE + 1,
               AppleAgxUatGpuSharedReadWrite, AppleAgxUatResultMisaligned,
               AppleAgxUatTtbr0);
  expect_range(63, PAGE_SIZE, 0xffffffc000ULL, 2 * PAGE_SIZE,
               AppleAgxUatGpuSharedReadWrite, AppleAgxUatResultOutOfRange,
               AppleAgxUatTtbr0);
  expect_range(0, 0xffffffffffffc000ULL, 0, 2 * PAGE_SIZE,
               AppleAgxUatFirmwarePrivateReadWrite,
               AppleAgxUatResultOverflow, AppleAgxUatTtbr1);
  expect_range(64, PAGE_SIZE, 0, PAGE_SIZE,
               AppleAgxUatGpuSharedReadWrite,
               AppleAgxUatResultUnsupportedContext, AppleAgxUatTtbr0);
  assert(AppleAgxUatValidateRange(63, PAGE_SIZE, 0, PAGE_SIZE,
                                 AppleAgxUatGpuSharedReadWrite, 0) ==
         AppleAgxUatResultInvalidArgument);
}

int main(void) {
  APPLE_AGX_UAT_TTBR_PAIR pair = {0xaaaaaaaaaaaaaaaaULL,
                                  0xbbbbbbbbbbbbbbbbULL};
  APPLE_AGX_UAT_ROOTS roots = {0x10004000ULL, 0x10008000ULL};

  assert(sizeof(pair) == 16u);
  assert(AppleAgxUatEncodeTtbrPair(64u, &roots, &pair) ==
         AppleAgxUatResultUnsupportedContext);
  assert(pair.Ttbr0 == 0xaaaaaaaaaaaaaaaaULL);
  assert(pair.Ttbr1 == 0xbbbbbbbbbbbbbbbbULL);
  assert(AppleAgxUatEncodeTtbrPair(0u, 0, &pair) ==
         AppleAgxUatResultInvalidArgument);
  assert(AppleAgxUatEncodeTtbrPair(0u, &roots, 0) ==
         AppleAgxUatResultInvalidArgument);
  assert(AppleAgxUatEncodeTtbrPair(0u, &roots, &pair) ==
         AppleAgxUatResultOk);
  assert(pair.Ttbr0 == 0x10004001ULL);
  assert(pair.Ttbr1 == 0x10008001ULL);
  assert(AppleAgxUatEncodeTtbrPair(63u, &roots, &pair) ==
         AppleAgxUatResultOk);
  assert(pair.Ttbr0 == 0x003f000010004001ULL);
  assert(pair.Ttbr1 == 0x003f000010008001ULL);
  roots.Ttbr1PhysicalAddress = 0x10008001ULL;
  assert(AppleAgxUatEncodeTtbrPair(0u, &roots, &pair) ==
         AppleAgxUatResultMisaligned);
  assert(pair.Ttbr0 == 0x003f000010004001ULL);
  assert(pair.Ttbr1 == 0x003f000010008001ULL);
  AppleAgxUatClearTtbrPair(&pair);
  assert(pair.Ttbr0 == 0ULL && pair.Ttbr1 == 0ULL);
  AppleAgxUatClearTtbrPair(0);

  test_table_descriptor();
  test_page_descriptor_protections();
  test_range_validation();
  return 0;
}
