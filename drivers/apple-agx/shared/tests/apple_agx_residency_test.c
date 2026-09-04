#include "apple_agx_residency.h"

#include <assert.h>
#include <string.h>

#define TEST_PAGE_COUNT 16u
#define TEST_ENTRY_COUNT 2048u
#define TEST_MAPPING_COUNT 8u
#define TEST_UAT_PAGE_SIZE 0x4000ULL
#define TEST_WDDM_PAGE_SIZE 0x10000ULL
#define TEST_CONTEXT_ALLOCATION_COUNT 8u

typedef struct _FAKE_RESIDENCY_ALLOCATOR {
  _Alignas(16384) unsigned long long Entries[TEST_PAGE_COUNT][TEST_ENTRY_COUNT];
  unsigned int Calls;
  unsigned int FailCall;
  unsigned int ReleaseCount;
} FAKE_RESIDENCY_ALLOCATOR;

typedef struct _FAKE_CONTEXT_ALLOCATION {
  _Alignas(16384) unsigned char Storage[0x8000];
  unsigned int Active;
} FAKE_CONTEXT_ALLOCATION;

typedef struct _FAKE_CONTEXT_MEMORY {
  FAKE_CONTEXT_ALLOCATION Allocations[TEST_CONTEXT_ALLOCATION_COUNT];
  unsigned int AllocateCount;
  unsigned int FreeCount;
  unsigned int FailAllocateCall;
} FAKE_CONTEXT_MEMORY;

static unsigned char allocate_context_memory(
    void *context, unsigned long long bytes, void **cpu_base,
    unsigned long long *device_base, void **allocation_handle) {
  FAKE_CONTEXT_MEMORY *fake = (FAKE_CONTEXT_MEMORY *)context;
  FAKE_CONTEXT_ALLOCATION *allocation;
  unsigned int slot = fake->AllocateCount++;

  assert(bytes == 0x8000ULL);
  if (fake->FailAllocateCall != 0u &&
      fake->AllocateCount == fake->FailAllocateCall)
    return 0u;
  assert(slot < TEST_CONTEXT_ALLOCATION_COUNT);
  allocation = &fake->Allocations[slot];
  memset(allocation->Storage, 0xa5, sizeof(allocation->Storage));
  allocation->Active = 1u;
  *cpu_base = allocation->Storage;
  *device_base = 0x30000000ULL + slot * 0x10000ULL;
  *allocation_handle = allocation;
  return 1u;
}

static unsigned char free_context_memory(void *context, void *handle) {
  FAKE_CONTEXT_MEMORY *fake = (FAKE_CONTEXT_MEMORY *)context;
  FAKE_CONTEXT_ALLOCATION *allocation =
      (FAKE_CONTEXT_ALLOCATION *)handle;

  assert(allocation >= &fake->Allocations[0]);
  assert(allocation <
         &fake->Allocations[TEST_CONTEXT_ALLOCATION_COUNT]);
  assert(allocation->Active != 0u);
  allocation->Active = 0u;
  ++fake->FreeCount;
  return 1u;
}

static unsigned char allocate_page(void *context, APPLE_AGX_UAT_PAGE *page) {
  FAKE_RESIDENCY_ALLOCATOR *fake = (FAKE_RESIDENCY_ALLOCATOR *)context;
  unsigned int slot = fake->Calls++;

  if (fake->FailCall != 0u && fake->Calls == fake->FailCall)
    return 0u;
  assert(slot < TEST_PAGE_COUNT);
  memset(fake->Entries[slot], 0, sizeof(fake->Entries[slot]));
  page->PhysicalAddress = 0x10000000ULL + slot * TEST_UAT_PAGE_SIZE;
  page->Entries = fake->Entries[slot];
  return 1u;
}

static void release_page(void *context, const APPLE_AGX_UAT_PAGE *page) {
  FAKE_RESIDENCY_ALLOCATOR *fake = (FAKE_RESIDENCY_ALLOCATOR *)context;
  assert(page->PhysicalAddress != 0ULL);
  ++fake->ReleaseCount;
}

static void init_uat(FAKE_RESIDENCY_ALLOCATOR *fake,
                     APPLE_AGX_UAT_ALLOCATOR *allocator,
                     APPLE_AGX_UAT_INVENTORY *inventory,
                     APPLE_AGX_UAT_PAGE *pages,
                     APPLE_AGX_UAT_MAPPING *mappings,
                     APPLE_AGX_UAT_ROOTS *roots) {
  memset(fake, 0, sizeof(*fake));
  memset(pages, 0, sizeof(APPLE_AGX_UAT_PAGE) * TEST_PAGE_COUNT);
  memset(mappings, 0,
         sizeof(APPLE_AGX_UAT_MAPPING) * TEST_MAPPING_COUNT);
  allocator->Context = fake;
  allocator->AllocatePage = allocate_page;
  allocator->ReleasePage = release_page;
  inventory->Pages = pages;
  inventory->PageCapacity = TEST_PAGE_COUNT;
  inventory->PageCount = 0u;
  inventory->Mappings = mappings;
  inventory->MappingCapacity = TEST_MAPPING_COUNT;
  inventory->MappingCount = 0u;
  assert(AppleAgxUatCreateAddressSpace(3u, allocator, inventory, roots) ==
         AppleAgxUatResultOk);
}

static void init_prepared_object(APPLE_AGX_MEMORY_OBJECT *object) {
  memset(object, 0, sizeof(*object));
  object->CpuAddress = (void *)0x10000ULL;
  object->AllocationHandle = object;
  object->DeviceAddress = 0x20000000ULL;
  object->Length = TEST_WDDM_PAGE_SIZE;
  object->State = AppleAgxMemoryPrepared;
}

static void test_maps_one_wddm_page_as_four_apple_leaves_and_unmaps(void) {
  FAKE_RESIDENCY_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[TEST_MAPPING_COUNT];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_MEMORY_OBJECT object;
  APPLE_AGX_RESIDENCY_STATUS status;
  unsigned long long gpu_va = 0x1800000000ULL;

  init_uat(&fake, &allocator, &inventory, pages, mappings, &roots);
  init_prepared_object(&object);
  assert(AppleAgxResidencyMap64K(&object, 3u, &roots, gpu_va, &allocator,
                                 &inventory, &status));
  assert(status.MemoryResult == AppleAgxMemoryResultOk);
  assert(status.UatResult == AppleAgxUatResultOk);
  assert(object.State == AppleAgxMemoryGpuMapped);
  assert(object.Context == 3u);
  assert(object.GpuVirtualAddress == gpu_va);
  assert(inventory.MappingCount == 1u);
  assert(inventory.Mappings[0].Length == TEST_WDDM_PAGE_SIZE);
  assert(inventory.Mappings[0].PhysicalAddress == object.DeviceAddress);

  assert(AppleAgxResidencyUnmap64K(&object, &roots, &allocator, &inventory,
                                   &status));
  assert(object.State == AppleAgxMemoryPrepared);
  assert(object.Context == 0u);
  assert(object.GpuVirtualAddress == 0ULL);
  assert(inventory.MappingCount == 0u);
  assert(inventory.PageCount == 2u);
}

static void test_map_failure_rolls_back_both_owners(void) {
  FAKE_RESIDENCY_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[TEST_MAPPING_COUNT];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_MEMORY_OBJECT object;
  APPLE_AGX_RESIDENCY_STATUS status;

  init_uat(&fake, &allocator, &inventory, pages, mappings, &roots);
  init_prepared_object(&object);
  fake.FailCall = 3u;
  assert(!AppleAgxResidencyMap64K(&object, 3u, &roots, 0x1800000000ULL,
                                  &allocator, &inventory, &status));
  assert(status.UatResult == AppleAgxUatResultAllocationFailed);
  assert(object.State == AppleAgxMemoryPrepared);
  assert(object.Context == 0u);
  assert(object.GpuVirtualAddress == 0ULL);
  assert(inventory.MappingCount == 0u);
  assert(inventory.PageCount == 2u);
}

static void test_scattered_physical_leaves_map_without_contiguous_claim(void) {
  FAKE_RESIDENCY_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[TEST_MAPPING_COUNT];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_MEMORY_OBJECT object;
  APPLE_AGX_RESIDENCY_STATUS status;
  const unsigned long long physical_pages[4] = {
      0x20000000ULL, 0x28004000ULL, 0x30008000ULL, 0x3800c000ULL};
  unsigned long long gpu_va = 0x1800000000ULL;

  init_uat(&fake, &allocator, &inventory, pages, mappings, &roots);
  init_prepared_object(&object);
  object.DevicePages = physical_pages;
  object.DevicePageCount = 4u;
  assert(AppleAgxResidencyMap64K(&object, 3u, &roots, gpu_va, &allocator,
                                 &inventory, &status));
  assert(inventory.Mappings[0].PhysicalStride ==
         APPLE_AGX_UAT_PHYSICAL_STRIDE_SCATTER);
  assert(AppleAgxResidencyUnmap64K(&object, &roots, &allocator, &inventory,
                                   &status));
}

static void test_alignment_and_inflight_state_are_fail_closed(void) {
  FAKE_RESIDENCY_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[TEST_MAPPING_COUNT];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_MEMORY_OBJECT object;
  APPLE_AGX_RESIDENCY_STATUS status;

  init_uat(&fake, &allocator, &inventory, pages, mappings, &roots);
  init_prepared_object(&object);
  assert(!AppleAgxResidencyMap64K(&object, 3u, &roots, 0x1800004000ULL,
                                  &allocator, &inventory, &status));
  assert(status.MemoryResult == AppleAgxMemoryResultInvalidArgument);
  assert(inventory.MappingCount == 0u);

  assert(AppleAgxResidencyMap64K(&object, 3u, &roots, 0x1800000000ULL,
                                 &allocator, &inventory, &status));
  assert(AppleAgxMemoryMarkSubmitted(&object, 9ULL) ==
         AppleAgxMemoryResultOk);
  assert(!AppleAgxResidencyUnmap64K(&object, &roots, &allocator, &inventory,
                                    &status));
  assert(status.MemoryResult == AppleAgxMemoryResultBusy);
  assert(object.State == AppleAgxMemoryInFlight);
  assert(inventory.MappingCount == 1u);
  assert(AppleAgxMemoryMarkCompleted(&object, 9ULL) ==
         AppleAgxMemoryResultOk);
  assert(AppleAgxResidencyUnmap64K(&object, &roots, &allocator, &inventory,
                                   &status));
}

static void test_owned_render_context_maps_and_releases_exactly(void) {
  FAKE_CONTEXT_MEMORY fake;
  APPLE_AGX_MEMORY_IO memory_io;
  APPLE_AGX_MEMORY_OBJECT page_objects[TEST_CONTEXT_ALLOCATION_COUNT];
  APPLE_AGX_UAT_PAGE pages[TEST_CONTEXT_ALLOCATION_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[TEST_MAPPING_COUNT];
  APPLE_AGX_RESIDENCY_CONTEXT context;
  APPLE_AGX_RESIDENCY_STATUS status;
  APPLE_AGX_MEMORY_OBJECT object;

  memset(&fake, 0, sizeof(fake));
  memset(&memory_io, 0, sizeof(memory_io));
  memset(page_objects, 0, sizeof(page_objects));
  memset(pages, 0, sizeof(pages));
  memset(mappings, 0, sizeof(mappings));
  memset(&context, 0, sizeof(context));
  memory_io.Context = &fake;
  memory_io.AllocateContiguous = allocate_context_memory;
  memory_io.FreeContiguous = free_context_memory;

  assert(AppleAgxResidencyContextCreate(
      &context, 3u, &memory_io, page_objects,
      TEST_CONTEXT_ALLOCATION_COUNT, pages,
      TEST_CONTEXT_ALLOCATION_COUNT, mappings, TEST_MAPPING_COUNT, &status));
  assert(context.Initialized == APPLE_AGX_TRUE);
  assert(context.Context == 3u);
  assert(context.MemoryOwner.ObjectCount == 2u);
  assert(context.Inventory.PageCount == 2u);

  init_prepared_object(&object);
  assert(AppleAgxResidencyMap64K(
      &object, context.Context, &context.Roots,
      APPLE_AGX_RESIDENCY_GEM_VA_BASE, &context.Allocator,
      &context.Inventory, &status));
  assert(context.MemoryOwner.ObjectCount == 4u);
  assert(context.Inventory.MappingCount == 1u);
  assert(!AppleAgxResidencyContextDestroy(&context, &status));
  assert(status.ContextResult == AppleAgxResidencyContextResultBusy);
  assert(context.Initialized == APPLE_AGX_TRUE);
  assert(fake.FreeCount == 0u);

  assert(AppleAgxResidencyUnmap64K(
      &object, &context.Roots, &context.Allocator, &context.Inventory,
      &status));
  assert(context.MemoryOwner.ObjectCount == 2u);
  assert(AppleAgxResidencyContextDestroy(&context, &status));
  assert(context.Initialized == APPLE_AGX_FALSE);
  assert(context.MemoryOwner.ObjectCount == 0u);
  assert(fake.FreeCount == 4u);
}

static void test_context_creation_failure_releases_partial_ttbr_owner(void) {
  FAKE_CONTEXT_MEMORY fake;
  APPLE_AGX_MEMORY_IO memory_io;
  APPLE_AGX_MEMORY_OBJECT page_objects[TEST_CONTEXT_ALLOCATION_COUNT];
  APPLE_AGX_UAT_PAGE pages[TEST_CONTEXT_ALLOCATION_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[TEST_MAPPING_COUNT];
  APPLE_AGX_RESIDENCY_CONTEXT context;
  APPLE_AGX_RESIDENCY_STATUS status;

  memset(&fake, 0, sizeof(fake));
  memset(&memory_io, 0, sizeof(memory_io));
  memset(page_objects, 0, sizeof(page_objects));
  memset(pages, 0, sizeof(pages));
  memset(mappings, 0, sizeof(mappings));
  memset(&context, 0, sizeof(context));
  fake.FailAllocateCall = 2u;
  memory_io.Context = &fake;
  memory_io.AllocateContiguous = allocate_context_memory;
  memory_io.FreeContiguous = free_context_memory;

  assert(!AppleAgxResidencyContextCreate(
      &context, 3u, &memory_io, page_objects,
      TEST_CONTEXT_ALLOCATION_COUNT, pages,
      TEST_CONTEXT_ALLOCATION_COUNT, mappings, TEST_MAPPING_COUNT, &status));
  assert(status.ContextResult == AppleAgxResidencyContextResultUat);
  assert(status.UatResult == AppleAgxUatResultAllocationFailed);
  assert(context.Initialized == APPLE_AGX_FALSE);
  assert(context.MemoryOwner.ObjectCount == 0u);
  assert(fake.FreeCount == 1u);
}

int main(void) {
  test_maps_one_wddm_page_as_four_apple_leaves_and_unmaps();
  test_map_failure_rolls_back_both_owners();
  test_scattered_physical_leaves_map_without_contiguous_claim();
  test_alignment_and_inflight_state_are_fail_closed();
  test_owned_render_context_maps_and_releases_exactly();
  test_context_creation_failure_releases_partial_ttbr_owner();
  return 0;
}
