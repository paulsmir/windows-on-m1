#include "apple_agx_local_segment.h"

#include <assert.h>

#define PAGE_64K 0x10000ULL

static void test_segment_address_translates_to_stable_agx_gpu_va(void) {
  APPLE_AGX_U64 gpuVa = 0ULL;

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             0x1600000000ULL, 0x80120000ULL, 2ULL * PAGE_64K, 0x3450ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressOk);
  assert(gpuVa == 0x1600123450ULL);
}

static void test_segment_identity_and_bounds_fail_closed(void) {
  APPLE_AGX_U64 gpuVa = 0xa5a5a5a5a5a5a5a5ULL;

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 2u, 0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             0x1600000000ULL, 0x80120000ULL, PAGE_64K, 0ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressWrongSegment);
  assert(gpuVa == 0ULL);

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             0x1600000000ULL, 0x7fff0000ULL, PAGE_64K, 0ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressOutsideSegment);
  assert(gpuVa == 0ULL);

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             0x1600000000ULL, 0x83ff0000ULL, 2ULL * PAGE_64K, 0ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressOutsideSegment);
  assert(gpuVa == 0ULL);
}

static void test_alignment_and_allocation_offset_are_validated(void) {
  APPLE_AGX_U64 gpuVa = 0xa5a5a5a5a5a5a5a5ULL;

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             0x1600000000ULL, 0x80121000ULL, PAGE_64K, 0ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressMisaligned);
  assert(gpuVa == 0ULL);

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             0x1600000000ULL, 0x80120000ULL, PAGE_64K - 1ULL, 0ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressMisaligned);
  assert(gpuVa == 0ULL);

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             0x1600000000ULL, 0x80120000ULL, PAGE_64K, PAGE_64K,
             &gpuVa) == AppleAgxLocalSegmentAddressOutsideAllocation);
  assert(gpuVa == 0ULL);
}

static void test_invalid_and_overflowing_contracts_fail_closed(void) {
  APPLE_AGX_U64 gpuVa = 0xa5a5a5a5a5a5a5a5ULL;

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             0u, 0u, 0x80000000ULL, PAGE_64K, 0x1600000000ULL,
             0x80000000ULL, PAGE_64K, 0ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressInvalidArgument);
  assert(gpuVa == 0ULL);

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, PAGE_64K, 0x1600000001ULL,
             0x80000000ULL, PAGE_64K, 0ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressMisaligned);
  assert(gpuVa == 0ULL);

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, 2ULL * PAGE_64K,
             0xffffffffffff0000ULL, 0x80010000ULL, PAGE_64K, 1ULL,
             &gpuVa) == AppleAgxLocalSegmentAddressOverflow);
  assert(gpuVa == 0ULL);

  assert(AppleAgxLocalSegmentAddressToGpuVa(
             1u, 1u, 0x80000000ULL, PAGE_64K, 0x1600000000ULL,
             0x80000000ULL, PAGE_64K, 0ULL,
             0) == AppleAgxLocalSegmentAddressInvalidArgument);
}

int main(void) {
  test_segment_address_translates_to_stable_agx_gpu_va();
  test_segment_identity_and_bounds_fail_closed();
  test_alignment_and_allocation_offset_are_validated();
  test_invalid_and_overflowing_contracts_fail_closed();
  return 0;
}
