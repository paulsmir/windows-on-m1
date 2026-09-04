#include "apple_agx_uat_memory.h"

#include <assert.h>
#include <string.h>

#define TEST_ALLOCATION_COUNT 12u
#define TEST_PAGE_COUNT 12u

typedef struct _FAKE_ALLOCATION {
  _Alignas(16384) unsigned char Storage[0x8000];
  unsigned long long DeviceBase;
  unsigned int Slot;
  unsigned char Active;
} FAKE_ALLOCATION;

typedef struct _FAKE_MEMORY {
  FAKE_ALLOCATION Allocations[TEST_ALLOCATION_COUNT];
  unsigned int AllocateCount;
  unsigned int FailAllocateCall;
  unsigned int FailFreeSlot;
  unsigned int FreeOrder[TEST_ALLOCATION_COUNT];
  unsigned int FreeCount;
} FAKE_MEMORY;

static unsigned char allocate_contiguous(
    void *context, unsigned long long bytes, void **cpu_base,
    unsigned long long *device_base, void **allocation_handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  FAKE_ALLOCATION *allocation;
  unsigned int slot = fake->AllocateCount++;

  assert(bytes == 0x8000ULL);
  if (fake->FailAllocateCall != 0u &&
      fake->AllocateCount == fake->FailAllocateCall)
    return 0u;
  assert(slot < TEST_ALLOCATION_COUNT);
  allocation = &fake->Allocations[slot];
  memset(allocation->Storage, 0xa5, sizeof(allocation->Storage));
  allocation->DeviceBase = 0x10001000ULL + slot * 0x10000ULL;
  allocation->Slot = slot;
  allocation->Active = 1u;
  *cpu_base = &allocation->Storage[0x1000];
  *device_base = allocation->DeviceBase;
  *allocation_handle = allocation;
  return 1u;
}

static unsigned char free_contiguous(void *context, void *handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  FAKE_ALLOCATION *allocation = (FAKE_ALLOCATION *)handle;

  assert(allocation >= &fake->Allocations[0]);
  assert(allocation < &fake->Allocations[TEST_ALLOCATION_COUNT]);
  assert(allocation->Active != 0u);
  if (fake->FailFreeSlot != 0u &&
      allocation->Slot + 1u == fake->FailFreeSlot)
    return 0u;
  allocation->Active = 0u;
  fake->FreeOrder[fake->FreeCount++] = allocation->Slot;
  return 1u;
}

static void init_fixture(FAKE_MEMORY *fake, APPLE_AGX_MEMORY_IO *memory_io,
                         APPLE_AGX_MEMORY_OBJECT *objects,
                         APPLE_AGX_UAT_MEMORY_OWNER *owner,
                         APPLE_AGX_UAT_ALLOCATOR *allocator,
                         unsigned int capacity) {
  memset(fake, 0, sizeof(*fake));
  memset(objects, 0, sizeof(*objects) * TEST_ALLOCATION_COUNT);
  memset(owner, 0, sizeof(*owner));
  memset(allocator, 0, sizeof(*allocator));
  memory_io->Context = fake;
  memory_io->AllocateContiguous = allocate_contiguous;
  memory_io->FreeContiguous = free_contiguous;
  assert(AppleAgxUatMemoryOwnerInitialize(owner, memory_io, objects, capacity) ==
         AppleAgxUatMemoryResultOk);
  assert(AppleAgxUatMemoryOwnerGetAllocator(owner, allocator) ==
         AppleAgxUatMemoryResultOk);
}

static void init_inventory(APPLE_AGX_UAT_INVENTORY *inventory,
                           APPLE_AGX_UAT_PAGE *pages,
                           APPLE_AGX_UAT_MAPPING *mappings) {
  memset(pages, 0, sizeof(APPLE_AGX_UAT_PAGE) * TEST_PAGE_COUNT);
  memset(mappings, 0, sizeof(APPLE_AGX_UAT_MAPPING) * 4u);
  inventory->Pages = pages;
  inventory->PageCapacity = TEST_PAGE_COUNT;
  inventory->PageCount = 0u;
  inventory->Mappings = mappings;
  inventory->MappingCapacity = 4u;
  inventory->MappingCount = 0u;
}

static void test_builds_and_releases_context_zero_in_reverse_order(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO memory_io;
  APPLE_AGX_MEMORY_OBJECT objects[TEST_ALLOCATION_COUNT];
  APPLE_AGX_UAT_MEMORY_OWNER owner;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[4];
  APPLE_AGX_UAT_ROOTS roots;
  unsigned int index;

  init_fixture(&fake, &memory_io, objects, &owner, &allocator,
               TEST_ALLOCATION_COUNT);
  init_inventory(&inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(0u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  assert(AppleAgxUatMap(0u, &roots, 0xffffff8000010000ULL, 0x20000000ULL,
                       0x4000ULL, AppleAgxUatFirmwarePrivateReadWrite,
                       &allocator, &inventory) == AppleAgxUatResultOk);
  assert(owner.ObjectCount == 4u);
  assert(roots.Ttbr0PhysicalAddress == 0x10004000ULL);
  assert(roots.Ttbr1PhysicalAddress == 0x10014000ULL);
  for (index = 0u; index < owner.ObjectCount; ++index) {
    assert((objects[index].DeviceAddress & 0x3fffULL) == 0ULL);
    assert(((unsigned long long)(void *)objects[index].CpuAddress & 0x3fffULL) ==
           0ULL);
    assert(objects[index].State == AppleAgxMemoryCpuOwned);
  }

  AppleAgxUatDestroy(&allocator, &inventory);
  assert(owner.ObjectCount == 0u);
  assert(owner.LastResult == AppleAgxUatMemoryResultOk);
  assert(fake.FreeCount == 4u);
  assert(fake.FreeOrder[0] == 3u);
  assert(fake.FreeOrder[1] == 2u);
  assert(fake.FreeOrder[2] == 1u);
  assert(fake.FreeOrder[3] == 0u);
  assert(AppleAgxUatMemoryOwnerDestroy(&owner) ==
         AppleAgxUatMemoryResultOk);
  assert(fake.FreeCount == 4u);
}

static void test_capacity_and_allocation_failure_roll_back(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO memory_io;
  APPLE_AGX_MEMORY_OBJECT objects[TEST_ALLOCATION_COUNT];
  APPLE_AGX_UAT_MEMORY_OWNER owner;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[4];
  APPLE_AGX_UAT_ROOTS roots;

  init_fixture(&fake, &memory_io, objects, &owner, &allocator, 1u);
  init_inventory(&inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(0u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultAllocationFailed);
  assert(owner.ObjectCount == 0u);
  assert(owner.LastResult == AppleAgxUatMemoryResultCapacity);
  assert(fake.FreeCount == 1u);

  init_fixture(&fake, &memory_io, objects, &owner, &allocator,
               TEST_ALLOCATION_COUNT);
  init_inventory(&inventory, pages, mappings);
  fake.FailAllocateCall = 2u;
  assert(AppleAgxUatCreateAddressSpace(0u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultAllocationFailed);
  assert(owner.ObjectCount == 0u);
  assert(owner.LastResult == AppleAgxUatMemoryResultAllocationFailed);
  assert(fake.FreeCount == 1u);
}

static void test_rejects_unowned_release_without_freeing(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO memory_io;
  APPLE_AGX_MEMORY_OBJECT objects[TEST_ALLOCATION_COUNT];
  APPLE_AGX_UAT_MEMORY_OWNER owner;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_PAGE page;

  init_fixture(&fake, &memory_io, objects, &owner, &allocator,
               TEST_ALLOCATION_COUNT);
  memset(&page, 0, sizeof(page));
  page.PhysicalAddress = 0x44440000ULL;
  page.Entries = (unsigned long long *)(void *)&fake.Allocations[0].Storage[0];
  allocator.ReleasePage(allocator.Context, &page);
  assert(owner.LastResult == AppleAgxUatMemoryResultNotOwned);
  assert(owner.ObjectCount == 0u);
  assert(fake.FreeCount == 0u);
}

static void test_failed_release_preserves_owner_for_retry(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO memory_io;
  APPLE_AGX_MEMORY_OBJECT objects[TEST_ALLOCATION_COUNT];
  APPLE_AGX_UAT_MEMORY_OWNER owner;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[4];
  APPLE_AGX_UAT_ROOTS roots;

  init_fixture(&fake, &memory_io, objects, &owner, &allocator,
               TEST_ALLOCATION_COUNT);
  init_inventory(&inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(0u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  fake.FailFreeSlot = 2u;
  AppleAgxUatDestroy(&allocator, &inventory);
  assert(owner.LastResult == AppleAgxUatMemoryResultReleaseFailed);
  assert(owner.ObjectCount == 1u);
  assert(objects[0].DeviceAddress == 0x10014000ULL);
  fake.FailFreeSlot = 0u;
  assert(AppleAgxUatMemoryOwnerDestroy(&owner) ==
         AppleAgxUatMemoryResultOk);
  assert(owner.ObjectCount == 0u);
  assert(fake.FreeCount == 2u);
  assert(AppleAgxUatMemoryOwnerDestroy(&owner) ==
         AppleAgxUatMemoryResultOk);
}

static void test_invalid_owner_arguments_fail_closed(void) {
  APPLE_AGX_UAT_MEMORY_OWNER owner;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_MEMORY_OBJECT object;

  memset(&owner, 0, sizeof(owner));
  memset(&allocator, 0, sizeof(allocator));
  memset(&io, 0, sizeof(io));
  memset(&object, 0, sizeof(object));
  assert(AppleAgxUatMemoryOwnerInitialize(0, &io, &object, 1u) ==
         AppleAgxUatMemoryResultInvalidArgument);
  assert(AppleAgxUatMemoryOwnerInitialize(&owner, &io, &object, 1u) ==
         AppleAgxUatMemoryResultInvalidArgument);
  assert(AppleAgxUatMemoryOwnerGetAllocator(&owner, &allocator) ==
         AppleAgxUatMemoryResultInvalidArgument);
  assert(AppleAgxUatMemoryOwnerDestroy(0) ==
         AppleAgxUatMemoryResultInvalidArgument);
}

int main(void) {
  test_builds_and_releases_context_zero_in_reverse_order();
  test_capacity_and_allocation_failure_roll_back();
  test_rejects_unowned_release_without_freeing();
  test_failed_release_preserves_owner_for_retry();
  test_invalid_owner_arguments_fail_closed();
  return 0;
}
