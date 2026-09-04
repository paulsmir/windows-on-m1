#include "render_memory.h"

#include <assert.h>
#include <string.h>

#define TEST_APERTURE_BASE 0x1600000000ULL
#define TEST_APERTURE_SIZE 0x10000000ULL
#define TEST_LOCAL_BASE 0x1500000000ULL
#define TEST_LOCAL_SIZE 0x04000000ULL
#define TEST_ALLOCATION_SIZE 0x03800000ULL
#define TEST_BACKEND_SIZE 0x00800000ULL
#define TEST_APERTURE_PAGES (TEST_APERTURE_SIZE / 0x1000ULL)

static APPLE_AGX_SOFTWARE_APERTURE_ENTRY entries[TEST_APERTURE_PAGES];

static void test_exact_two_segment_contract(void) {
  ADMISSION_MEMORY_CONTRACT memory;

  memset(&memory, 0xa5, sizeof(memory));
  memset(entries, 0xa5, sizeof(entries));
  assert(AdmissionMemoryInitialize(&memory, entries, TEST_APERTURE_PAGES,
                                   TEST_APERTURE_BASE, TEST_APERTURE_SIZE,
                                   TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(memory.Topology.SegmentCount == 2u);
  assert(memory.Topology.PagingBufferSegmentId == 1u);
  assert(memory.Topology.Aperture.Id == 1u);
  assert(memory.Topology.Aperture.Aperture == APPLE_AGX_TRUE);
  assert(memory.Topology.Local.Id == 2u);
  assert(memory.Topology.Local.Use64KPages == APPLE_AGX_TRUE);
  assert(memory.Topology.Local.PopulatedFromSystemMemory == APPLE_AGX_TRUE);
  assert(!AdmissionMemoryReady(&memory));
  assert(!AdmissionMemoryMarkPagingReady(&memory));
  assert(!AdmissionMemoryMarkUatReady(&memory, 62u, 0x4000ULL,
                                      TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(!AdmissionMemoryMarkUatReady(&memory, 63u, 0x1000ULL,
                                      TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(AdmissionMemoryMarkUatReady(&memory, 63u, 0x4000ULL,
                                     TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(AdmissionMemoryMarkPagingReady(&memory));
  assert(AdmissionMemoryReady(&memory));
}

static void test_aperture_and_local_address_translation(void) {
  ADMISSION_MEMORY_CONTRACT memory;
  APPLE_AGX_U64 pages[16];
  APPLE_AGX_U64 resolved = 0;
  APPLE_AGX_U64 gpu = 0;
  unsigned int index;

  memset(&memory, 0, sizeof(memory));
  assert(AdmissionMemoryInitialize(&memory, entries, TEST_APERTURE_PAGES,
                                   TEST_APERTURE_BASE, TEST_APERTURE_SIZE,
                                   TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  for (index = 0u; index < 16u; ++index)
    pages[index] = 0x40000000ULL + (APPLE_AGX_U64)index * 0x1000ULL;
  assert(AdmissionMemoryMapAperture64K(&memory, 0x10000ULL, pages, 16u) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureResolve(&memory.Aperture, 0x10000ULL,
                                         &resolved) ==
         AppleAgxSoftwareApertureOk);
  assert(resolved == pages[0]);
  assert(AdmissionMemoryUnmapAperture64K(&memory, 0x10000ULL,
                                         0x50000000ULL) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureResolve(&memory.Aperture, 0x10000ULL,
                                         &resolved) ==
         AppleAgxSoftwareApertureDummy);
  assert(resolved == 0x50000000ULL);
  assert(AdmissionMemoryLocalAddressToGpuVa(
             &memory, 2u, TEST_LOCAL_BASE + 0x20000ULL, 0x10000ULL,
             0x3000ULL, &gpu) == AppleAgxLocalSegmentAddressOk);
  assert(gpu == TEST_LOCAL_BASE + 0x23000ULL);
}

static void test_backend_tail_reservation_preserves_full_uat_mapping(void) {
  ADMISSION_MEMORY_CONTRACT memory;
  APPLE_AGX_U64 backend_gpu = 0u;
  APPLE_AGX_U64 backend_bytes = 0u;
  APPLE_AGX_U64 gpu = 0u;

  memset(&memory, 0, sizeof(memory));
  assert(AdmissionMemoryInitialize(&memory, entries, TEST_APERTURE_PAGES,
                                   TEST_APERTURE_BASE, TEST_APERTURE_SIZE,
                                   TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(memory.LocalAllocationBytes == TEST_LOCAL_SIZE);
  assert(AdmissionMemoryReserveBackendTail(
      &memory, TEST_ALLOCATION_SIZE, TEST_BACKEND_SIZE));
  assert(memory.Topology.Local.Size == TEST_LOCAL_SIZE);
  assert(memory.LocalBytes == TEST_LOCAL_SIZE);
  assert(memory.LocalAllocationBytes == TEST_ALLOCATION_SIZE);
  assert(AdmissionMemoryBackendRange(
      &memory, &backend_gpu, &backend_bytes));
  assert(backend_gpu == TEST_LOCAL_BASE + TEST_ALLOCATION_SIZE);
  assert(backend_bytes == TEST_BACKEND_SIZE);
  assert(AdmissionMemoryLocalAddressToGpuVa(
             &memory, 2u, TEST_LOCAL_BASE + 0x037e0000ULL, 0x10000ULL,
             0u, &gpu) == AppleAgxLocalSegmentAddressOk);
  assert(AdmissionMemoryLocalAddressToGpuVa(
             &memory, 2u, TEST_LOCAL_BASE + TEST_ALLOCATION_SIZE, 0x10000ULL,
             0u, &gpu) == AppleAgxLocalSegmentAddressOutsideSegment);
  {
    APPLE_AGX_PHYSICAL_PAGING_PLAN plan;
    assert(AdmissionMemoryPlanFill(
               &memory, ADMISSION_MEMORY_LOCAL_SEGMENT,
               TEST_LOCAL_BASE + TEST_ALLOCATION_SIZE, 0x10000ULL,
               0x11223344u, &plan) ==
           AppleAgxPhysicalPagingOutOfRange);
  }
  assert(!AdmissionMemoryReserveBackendTail(
      &memory, TEST_ALLOCATION_SIZE, TEST_BACKEND_SIZE));
}

static void test_local_allocation_view_resolves_cpu_host_and_gpu_together(
    void) {
  ADMISSION_MEMORY_CONTRACT memory;
  ADMISSION_LOCAL_MEMORY_VIEW view;
  void *cpu_base = (void *)0x10000000ULL;

  memset(&memory, 0, sizeof(memory));
  assert(AdmissionMemoryInitialize(&memory, entries, TEST_APERTURE_PAGES,
                                   TEST_APERTURE_BASE, TEST_APERTURE_SIZE,
                                   TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(AdmissionMemoryReserveBackendTail(
      &memory, TEST_ALLOCATION_SIZE, TEST_BACKEND_SIZE));
  assert(AdmissionMemoryResolveLocalView(
      &memory, TEST_LOCAL_BASE + 0x20000ULL, 0x10000ULL, 0u,
      cpu_base, 0x9d0000000ULL, &view));
  assert(view.CpuAddress == (void *)0x10020000ULL);
  assert(view.HostPhysicalAddress == 0x9d0020000ULL);
  assert(view.GpuVirtualAddress == TEST_LOCAL_BASE + 0x20000ULL);
  assert(view.Bytes == 0x10000ULL);
  assert(!AdmissionMemoryResolveLocalView(
      &memory, TEST_LOCAL_BASE + TEST_ALLOCATION_SIZE, 0x10000ULL, 0u,
      cpu_base, 0x9d0000000ULL, &view));
}

static void test_invalid_initialization_is_atomic(void) {
  ADMISSION_MEMORY_CONTRACT memory;
  ADMISSION_MEMORY_CONTRACT before;

  memset(&memory, 0x5a, sizeof(memory));
  before = memory;
  assert(!AdmissionMemoryInitialize(&memory, entries, TEST_APERTURE_PAGES,
                                    TEST_APERTURE_BASE + 1u,
                                    TEST_APERTURE_SIZE, TEST_LOCAL_BASE,
                                    TEST_LOCAL_SIZE));
  assert(memcmp(&memory, &before, sizeof(memory)) == 0);
  assert(!AdmissionMemoryInitialize(&memory, entries,
                                    TEST_APERTURE_PAGES - 1u,
                                    TEST_APERTURE_BASE, TEST_APERTURE_SIZE,
                                    TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(memcmp(&memory, &before, sizeof(memory)) == 0);
}

static void test_paging_plans_preserve_segment_and_uat_units(void) {
  ADMISSION_MEMORY_CONTRACT memory;
  APPLE_AGX_PHYSICAL_PAGING_PLAN plan;
  APPLE_AGX_APERTURE_RUN runs[4];
  APPLE_AGX_U64 pages[16];
  APPLE_AGX_U32 runCount = 0u;
  unsigned int index;

  memset(&memory, 0, sizeof(memory));
  assert(AdmissionMemoryInitialize(&memory, entries, TEST_APERTURE_PAGES,
                                   TEST_APERTURE_BASE, TEST_APERTURE_SIZE,
                                   TEST_LOCAL_BASE, TEST_LOCAL_SIZE));
  assert(AdmissionMemoryPlanTransfer(
             &memory, 0u, 0u, ADMISSION_MEMORY_LOCAL_SEGMENT,
             TEST_LOCAL_BASE + 0x20000ULL, 0x1000ULL, 0x3000ULL,
             0x4000ULL, &plan) == AppleAgxPhysicalPagingOk);
  assert(plan.Kind == AppleAgxPhysicalPagingUpload);
  assert(plan.LocalOffset == 0x21000ULL);
  assert(plan.SystemOffset == 0x3000ULL);
  assert(plan.Bytes == 0x4000ULL);
  assert(AdmissionMemoryPlanFill(
             &memory, ADMISSION_MEMORY_LOCAL_SEGMENT,
             TEST_LOCAL_BASE + 0x40000ULL, 0x10000ULL, 0x11223344u,
             &plan) == AppleAgxPhysicalPagingOk);
  assert(plan.Kind == AppleAgxPhysicalPagingFill);
  assert(AdmissionMemoryPlanDiscard(
             &memory, ADMISSION_MEMORY_LOCAL_SEGMENT,
             TEST_LOCAL_BASE + 0x50000ULL,
             &plan) == AppleAgxPhysicalPagingOk);
  assert(plan.Kind == AppleAgxPhysicalPagingDiscard);

  for (index = 0u; index < 16u; ++index)
    pages[index] = 0x60000000ULL + (APPLE_AGX_U64)index * 0x1000ULL;
  assert(AdmissionMemoryPlanAperture64K(
             &memory, 0x10000ULL, pages, 16u, runs, 4u,
             &runCount) == AppleAgxApertureResultOk);
  assert(runCount == 1u);
  assert(runs[0].GpuVirtualAddress == TEST_APERTURE_BASE + 0x10000ULL);
  assert(runs[0].PhysicalAddress == pages[0]);
  assert(runs[0].Length == 0x10000ULL);
}

int main(void) {
  test_exact_two_segment_contract();
  test_aperture_and_local_address_translation();
  test_backend_tail_reservation_preserves_full_uat_mapping();
  test_local_allocation_view_resolves_cpu_host_and_gpu_together();
  test_invalid_initialization_is_atomic();
  test_paging_plans_preserve_segment_and_uat_units();
  return 0;
}
