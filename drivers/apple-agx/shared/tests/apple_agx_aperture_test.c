#include "apple_agx_aperture.h"

#include <assert.h>
#include <string.h>

#define TEST_APERTURE_BASE 0x1600000000ULL
#define TEST_APERTURE_SIZE 0x100000000ULL

static void fill_contiguous_pages(APPLE_AGX_U64 *pages,
                                  APPLE_AGX_U32 count,
                                  APPLE_AGX_U64 base) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < count; ++index)
    pages[index] = base + (APPLE_AGX_U64)index * APPLE_AGX_SYSTEM_PAGE_SIZE;
}

static void test_coalesces_contiguous_64k_mapping(void) {
  APPLE_AGX_U64 pages[16];
  APPLE_AGX_APERTURE_RUN runs[4];
  APPLE_AGX_U32 run_count = 99u;

  fill_contiguous_pages(pages, 16u, 0x20000000ULL);
  memset(runs, 0xa5, sizeof(runs));
  assert(AppleAgxAperturePlan64K(
             TEST_APERTURE_BASE, TEST_APERTURE_SIZE, 0x20000ULL,
             pages, 16u, runs, 4u, &run_count) ==
         AppleAgxApertureResultOk);
  assert(run_count == 1u);
  assert(runs[0].GpuVirtualAddress == TEST_APERTURE_BASE + 0x20000ULL);
  assert(runs[0].PhysicalAddress == 0x20000000ULL);
  assert(runs[0].Length == APPLE_AGX_WDDM_PAGE_SIZE_64K);
}

static void test_splits_only_between_complete_16k_leaves(void) {
  APPLE_AGX_U64 pages[16];
  APPLE_AGX_APERTURE_RUN runs[4];
  APPLE_AGX_U32 run_count = 0u;

  fill_contiguous_pages(pages, 16u, 0x30000000ULL);
  fill_contiguous_pages(&pages[4], 12u, 0x40000000ULL);
  assert(AppleAgxAperturePlan64K(
             TEST_APERTURE_BASE, TEST_APERTURE_SIZE, 0ULL,
             pages, 16u, runs, 4u, &run_count) ==
         AppleAgxApertureResultOk);
  assert(run_count == 2u);
  assert(runs[0].GpuVirtualAddress == TEST_APERTURE_BASE);
  assert(runs[0].PhysicalAddress == 0x30000000ULL);
  assert(runs[0].Length == APPLE_AGX_UAT_PAGE_SIZE_16K);
  assert(runs[1].GpuVirtualAddress ==
         TEST_APERTURE_BASE + APPLE_AGX_UAT_PAGE_SIZE_16K);
  assert(runs[1].PhysicalAddress == 0x40000000ULL);
  assert(runs[1].Length == 3ULL * APPLE_AGX_UAT_PAGE_SIZE_16K);
}

static void test_rejects_noncontiguous_pages_inside_apple_leaf(void) {
  APPLE_AGX_U64 pages[16];
  APPLE_AGX_APERTURE_RUN runs[4];
  APPLE_AGX_U32 run_count = 99u;

  fill_contiguous_pages(pages, 16u, 0x50000000ULL);
  pages[2] += APPLE_AGX_SYSTEM_PAGE_SIZE;
  assert(AppleAgxAperturePlan64K(
             TEST_APERTURE_BASE, TEST_APERTURE_SIZE, 0ULL,
             pages, 16u, runs, 4u, &run_count) ==
         AppleAgxApertureResultPhysicalLayout);
  assert(run_count == 0u);
}

static void test_rejects_partial_64k_and_out_of_range_requests(void) {
  APPLE_AGX_U64 pages[16];
  APPLE_AGX_APERTURE_RUN runs[4];
  APPLE_AGX_U32 run_count = 99u;

  fill_contiguous_pages(pages, 16u, 0x60000000ULL);
  assert(AppleAgxAperturePlan64K(
             TEST_APERTURE_BASE, TEST_APERTURE_SIZE, 0ULL,
             pages, 15u, runs, 4u, &run_count) ==
         AppleAgxApertureResultAlignment);
  assert(run_count == 0u);
  assert(AppleAgxAperturePlan64K(
             TEST_APERTURE_BASE, TEST_APERTURE_SIZE,
             TEST_APERTURE_SIZE, pages, 16u, runs, 4u, &run_count) ==
         AppleAgxApertureResultRange);
  assert(run_count == 0u);
}

static void test_capacity_failure_is_atomic(void) {
  APPLE_AGX_U64 pages[16];
  APPLE_AGX_APERTURE_RUN runs[1];
  APPLE_AGX_APERTURE_RUN before;
  APPLE_AGX_U32 run_count = 99u;

  fill_contiguous_pages(pages, 16u, 0x70000000ULL);
  fill_contiguous_pages(&pages[4], 12u, 0x80000000ULL);
  runs[0].GpuVirtualAddress = 1ULL;
  runs[0].PhysicalAddress = 2ULL;
  runs[0].Length = 3ULL;
  before = runs[0];
  assert(AppleAgxAperturePlan64K(
             TEST_APERTURE_BASE, TEST_APERTURE_SIZE, 0ULL,
             pages, 16u, runs, 1u, &run_count) ==
         AppleAgxApertureResultCapacity);
  assert(run_count == 0u);
  assert(memcmp(&runs[0], &before, sizeof(before)) == 0);
}

static void test_gdi_mapping_requires_one_coherent_cpu_visible_aperture(void) {
  APPLE_AGX_GDI_APERTURE_CANDIDATE candidate;
  APPLE_AGX_GDI_APERTURE_RECEIPT receipt;

  memset(&candidate, 0, sizeof(candidate));
  memset(&receipt, 0xa5, sizeof(receipt));
  candidate.ApertureBase = TEST_APERTURE_BASE;
  candidate.ApertureSize = TEST_APERTURE_SIZE;
  candidate.MappingAddress = TEST_APERTURE_BASE + 0x20000ULL;
  candidate.MappingLength = APPLE_AGX_WDDM_PAGE_SIZE_64K;
  candidate.PageSize = APPLE_AGX_WDDM_PAGE_SIZE_64K;
  candidate.IsAperture = APPLE_AGX_TRUE;
  candidate.CpuVisible = APPLE_AGX_TRUE;
  candidate.CacheCoherent = APPLE_AGX_TRUE;
  assert(AppleAgxApertureValidateGdiMapping(&candidate, &receipt));
  assert(receipt.Address == candidate.MappingAddress);
  assert(receipt.Length == candidate.MappingLength);
  assert(receipt.PageSize == APPLE_AGX_WDDM_PAGE_SIZE_64K);

#define EXPECT_REJECTED(Member, Value)                                      \
  do {                                                                      \
    APPLE_AGX_GDI_APERTURE_CANDIDATE rejected = candidate;                  \
    APPLE_AGX_GDI_APERTURE_RECEIPT untouched;                              \
    memset(&untouched, 0xa5, sizeof(untouched));                            \
    rejected.Member = (Value);                                              \
    assert(!AppleAgxApertureValidateGdiMapping(&rejected, &untouched));      \
    assert(untouched.Address == 0ULL && untouched.Length == 0ULL &&          \
           untouched.PageSize == 0ULL);                                     \
  } while (0)

  EXPECT_REJECTED(IsAperture, APPLE_AGX_FALSE);
  EXPECT_REJECTED(CpuVisible, APPLE_AGX_FALSE);
  EXPECT_REJECTED(CacheCoherent, APPLE_AGX_FALSE);
  EXPECT_REJECTED(PageSize, APPLE_AGX_UAT_PAGE_SIZE_16K);
  EXPECT_REJECTED(MappingAddress, TEST_APERTURE_BASE + 1ULL);
  EXPECT_REJECTED(MappingLength, APPLE_AGX_WDDM_PAGE_SIZE_64K - 1ULL);
  EXPECT_REJECTED(MappingAddress,
                  TEST_APERTURE_BASE + TEST_APERTURE_SIZE);
#undef EXPECT_REJECTED
}

int main(void) {
  test_coalesces_contiguous_64k_mapping();
  test_splits_only_between_complete_16k_leaves();
  test_rejects_noncontiguous_pages_inside_apple_leaf();
  test_rejects_partial_64k_and_out_of_range_requests();
  test_capacity_failure_is_atomic();
  test_gdi_mapping_requires_one_coherent_cpu_visible_aperture();
  return 0;
}
