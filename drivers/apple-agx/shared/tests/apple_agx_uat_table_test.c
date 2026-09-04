#include "apple_agx_uat_table.h"

#include <assert.h>
#include <string.h>

#define TEST_PAGE_COUNT 16u
#define TEST_ENTRY_COUNT 2048u
#define TEST_PAGE_SIZE 0x4000ULL
#define TABLE_ADDRESS_MASK 0x000000ffffffc000ULL

typedef struct _FAKE_ALLOCATOR {
  _Alignas(16384) unsigned long long Entries[TEST_PAGE_COUNT][TEST_ENTRY_COUNT];
  unsigned int Calls;
  unsigned int FailCall;
  unsigned long long Released[TEST_PAGE_COUNT];
  unsigned int ReleaseCount;
} FAKE_ALLOCATOR;

static unsigned char allocate_page(void *context, APPLE_AGX_UAT_PAGE *page) {
  FAKE_ALLOCATOR *fake = (FAKE_ALLOCATOR *)context;
  unsigned int slot = fake->Calls++;

  if (fake->FailCall != 0u && fake->Calls == fake->FailCall) {
    return 0u;
  }
  assert(slot < TEST_PAGE_COUNT);
  memset(fake->Entries[slot], 0, sizeof(fake->Entries[slot]));
  page->PhysicalAddress = 0x10000000ULL + (slot * TEST_PAGE_SIZE);
  page->Entries = fake->Entries[slot];
  return 1u;
}

static void release_page(void *context, const APPLE_AGX_UAT_PAGE *page) {
  FAKE_ALLOCATOR *fake = (FAKE_ALLOCATOR *)context;
  assert(fake->ReleaseCount < TEST_PAGE_COUNT);
  fake->Released[fake->ReleaseCount++] = page->PhysicalAddress;
}

static APPLE_AGX_UAT_PAGE *find_page(APPLE_AGX_UAT_INVENTORY *inventory,
                                    unsigned long long physical) {
  unsigned int index;
  for (index = 0; index < inventory->PageCount; ++index) {
    if (inventory->Pages[index].PhysicalAddress == physical) {
      return &inventory->Pages[index];
    }
  }
  return 0;
}

static void init_fixture(FAKE_ALLOCATOR *fake,
                         APPLE_AGX_UAT_ALLOCATOR *allocator,
                         APPLE_AGX_UAT_INVENTORY *inventory,
                         APPLE_AGX_UAT_PAGE *pages,
                         APPLE_AGX_UAT_MAPPING *mappings) {
  memset(fake, 0, sizeof(*fake));
  memset(pages, 0, sizeof(APPLE_AGX_UAT_PAGE) * TEST_PAGE_COUNT);
  memset(mappings, 0, sizeof(APPLE_AGX_UAT_MAPPING) * 8u);
  allocator->Context = fake;
  allocator->AllocatePage = allocate_page;
  allocator->ReleasePage = release_page;
  inventory->Pages = pages;
  inventory->PageCapacity = TEST_PAGE_COUNT;
  inventory->PageCount = 0u;
  inventory->Mappings = mappings;
  inventory->MappingCapacity = 8u;
  inventory->MappingCount = 0u;
}

static void test_high_and_low_walks(void) {
  FAKE_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[8];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_UAT_PAGE *root;
  APPLE_AGX_UAT_PAGE *level1;
  APPLE_AGX_UAT_PAGE *level2;
  unsigned long long va = 0xffffff8000010000ULL;
  unsigned int l0 = (unsigned int)((va >> 36) & 7ULL);
  unsigned int l1 = (unsigned int)((va >> 25) & 2047ULL);
  unsigned int l2 = (unsigned int)((va >> 14) & 2047ULL);

  init_fixture(&fake, &allocator, &inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(0, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  assert(inventory.PageCount == 2u);
  assert(roots.Ttbr0PhysicalAddress == pages[0].PhysicalAddress);
  assert(roots.Ttbr1PhysicalAddress == pages[1].PhysicalAddress);
  assert(pages[0].Level == 0u && pages[1].Level == 0u);

  assert(AppleAgxUatMap(0, &roots, va, 0x20000000ULL, 0x8000ULL,
                       AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultOk);
  assert(inventory.MappingCount == 1u);
  assert(inventory.Mappings[0].Length == 0x8000ULL);
  root = find_page(&inventory, roots.Ttbr1PhysicalAddress);
  assert(root != 0);
  level1 = find_page(&inventory, root->Entries[l0] & TABLE_ADDRESS_MASK);
  assert(level1 != 0 && level1->Level == 1u);
  level2 = find_page(&inventory, level1->Entries[l1] & TABLE_ADDRESS_MASK);
  assert(level2 != 0 && level2->Level == 2u);
  assert((level2->Entries[l2] & TABLE_ADDRESS_MASK) == 0x20000000ULL);
  assert((level2->Entries[l2 + 1u] & TABLE_ADDRESS_MASK) == 0x20004000ULL);

  va = 0x0000000000010000ULL;
  l0 = (unsigned int)((va >> 36) & 7ULL);
  l1 = (unsigned int)((va >> 25) & 2047ULL);
  l2 = (unsigned int)((va >> 14) & 2047ULL);
  assert(AppleAgxUatMap(0, &roots, va, 0x30000000ULL, TEST_PAGE_SIZE,
                       AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultOk);
  root = find_page(&inventory, roots.Ttbr0PhysicalAddress);
  level1 = find_page(&inventory, root->Entries[l0] & TABLE_ADDRESS_MASK);
  level2 = find_page(&inventory, level1->Entries[l1] & TABLE_ADDRESS_MASK);
  assert((level2->Entries[l2] & TABLE_ADDRESS_MASK) == 0x30000000ULL);
  assert(inventory.MappingCount == 2u);

  assert(AppleAgxUatMap(0, &roots, va, 0x40000000ULL, TEST_PAGE_SIZE,
                       AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultAlreadyMapped);
  assert(AppleAgxUatMap(0, &roots, va, 0x40000000ULL, 2 * TEST_PAGE_SIZE,
                       AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultAlreadyMapped);
  assert(AppleAgxUatMap(0, &roots, 0x0000007fffffc000ULL, 0x40000000ULL,
                       2 * TEST_PAGE_SIZE,
                       AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultOutOfRange);

  AppleAgxUatDestroy(&allocator, &inventory);
  assert(fake.ReleaseCount == 6u);
  assert(fake.Released[0] == 0x10014000ULL);
  assert(fake.Released[5] == 0x10000000ULL);
  AppleAgxUatDestroy(&allocator, &inventory);
  assert(fake.ReleaseCount == 6u);
}

static void test_create_rollback(void) {
  unsigned int fail_call;
  for (fail_call = 1u; fail_call <= 2u; ++fail_call) {
    FAKE_ALLOCATOR fake;
    APPLE_AGX_UAT_ALLOCATOR allocator;
    APPLE_AGX_UAT_INVENTORY inventory;
    APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
    APPLE_AGX_UAT_MAPPING mappings[8];
    APPLE_AGX_UAT_ROOTS roots = {1ULL, 1ULL};
    init_fixture(&fake, &allocator, &inventory, pages, mappings);
    fake.FailCall = fail_call;
    assert(AppleAgxUatCreateAddressSpace(0, &allocator, &inventory, &roots) ==
           AppleAgxUatResultAllocationFailed);
    assert(inventory.PageCount == 0u);
    assert(inventory.MappingCount == 0u);
    assert(roots.Ttbr0PhysicalAddress == 0ULL);
    assert(roots.Ttbr1PhysicalAddress == 0ULL);
    assert(fake.ReleaseCount == fail_call - 1u);
    if (fake.ReleaseCount == 1u) {
      assert(fake.Released[0] == 0x10000000ULL);
    }
  }
}

static void test_mapping_rollback_and_capacity(void) {
  unsigned int fail_call;
  for (fail_call = 3u; fail_call <= 4u; ++fail_call) {
    FAKE_ALLOCATOR fake;
    APPLE_AGX_UAT_ALLOCATOR allocator;
    APPLE_AGX_UAT_INVENTORY inventory;
    APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
    APPLE_AGX_UAT_MAPPING mappings[8];
    APPLE_AGX_UAT_ROOTS roots;
    APPLE_AGX_UAT_PAGE *root;
    init_fixture(&fake, &allocator, &inventory, pages, mappings);
    assert(AppleAgxUatCreateAddressSpace(0, &allocator, &inventory, &roots) ==
           AppleAgxUatResultOk);
    fake.FailCall = fail_call;
    assert(AppleAgxUatMap(0, &roots, 0xffffff8000010000ULL, 0x20000000ULL,
                         TEST_PAGE_SIZE,
                         AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                         &inventory) == AppleAgxUatResultAllocationFailed);
    assert(inventory.PageCount == 2u);
    assert(inventory.MappingCount == 0u);
    root = find_page(&inventory, roots.Ttbr1PhysicalAddress);
    assert(root != 0 && root->Entries[0] == 0ULL);
    assert(fake.ReleaseCount == fail_call - 3u);
  }

  {
    FAKE_ALLOCATOR fake;
    APPLE_AGX_UAT_ALLOCATOR allocator;
    APPLE_AGX_UAT_INVENTORY inventory;
    APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
    APPLE_AGX_UAT_MAPPING mappings[8];
    APPLE_AGX_UAT_ROOTS roots;
    init_fixture(&fake, &allocator, &inventory, pages, mappings);
    inventory.PageCapacity = 2u;
    inventory.MappingCapacity = 1u;
    assert(AppleAgxUatCreateAddressSpace(0, &allocator, &inventory, &roots) ==
           AppleAgxUatResultOk);
    assert(AppleAgxUatMap(0, &roots, 0xffffff8000010000ULL, 0x20000000ULL,
                         TEST_PAGE_SIZE,
                         AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                         &inventory) == AppleAgxUatResultCapacity);
    assert(inventory.PageCount == 2u && inventory.MappingCount == 0u);
    inventory.MappingCapacity = 0u;
    assert(AppleAgxUatMap(0, &roots, 0xffffff8000010000ULL, 0x20000000ULL,
                         TEST_PAGE_SIZE,
                         AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                         &inventory) == AppleAgxUatResultCapacity);
  }
}

static void test_exact_unmap_is_atomic_reclaims_tables_and_can_remap(void) {
  FAKE_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[8];
  APPLE_AGX_UAT_ROOTS roots;
  unsigned long long va = 0xffffff8000010000ULL;

  init_fixture(&fake, &allocator, &inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(3u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  assert(AppleAgxUatMap(3u, &roots, va, 0x20000000ULL, 0x10000ULL,
                       AppleAgxUatGpuSharedReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultOk);
  assert(inventory.PageCount == 4u);
  assert(inventory.MappingCount == 1u);

  assert(AppleAgxUatUnmap(3u, &roots, va, 0x8000ULL, &allocator,
                         &inventory) == AppleAgxUatResultNotMapped);
  assert(inventory.PageCount == 4u);
  assert(inventory.MappingCount == 1u);
  assert(fake.ReleaseCount == 0u);

  assert(AppleAgxUatUnmap(3u, &roots, va, 0x10000ULL, &allocator,
                         &inventory) == AppleAgxUatResultOk);
  assert(inventory.PageCount == 2u);
  assert(inventory.MappingCount == 0u);
  assert(fake.ReleaseCount == 2u);

  assert(AppleAgxUatMap(3u, &roots, va, 0x30000000ULL, 0x10000ULL,
                       AppleAgxUatGpuSharedReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultOk);
  assert(inventory.PageCount == 4u);
  assert(inventory.MappingCount == 1u);
}

static void test_batch_map_rolls_back_when_later_run_cannot_allocate(void) {
  FAKE_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[8];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_UAT_RANGE ranges[2];

  init_fixture(&fake, &allocator, &inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(3u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  ranges[0].VirtualAddress = 0x16ffffc000ULL;
  ranges[0].PhysicalAddress = 0x40000000ULL;
  ranges[0].Length = TEST_PAGE_SIZE;
  ranges[0].Protection = AppleAgxUatGpuSharedReadWrite;
  ranges[1].VirtualAddress = 0x1700000000ULL;
  ranges[1].PhysicalAddress = 0x50000000ULL;
  ranges[1].Length = TEST_PAGE_SIZE;
  ranges[1].Protection = AppleAgxUatGpuSharedReadWrite;
  fake.FailCall = 5u;

  assert(AppleAgxUatMapBatch(3u, &roots, ranges, 2u, &allocator,
                             &inventory) ==
         AppleAgxUatResultAllocationFailed);
  assert(inventory.MappingCount == 0u);
  assert(inventory.PageCount == 2u);
}

static void test_batch_unmap_prevalidates_every_exact_run(void) {
  FAKE_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[8];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_UAT_RANGE mapped[2];
  APPLE_AGX_UAT_RANGE invalid[2];

  init_fixture(&fake, &allocator, &inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(3u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  mapped[0].VirtualAddress = 0x1600010000ULL;
  mapped[0].PhysicalAddress = 0x60000000ULL;
  mapped[0].Length = TEST_PAGE_SIZE;
  mapped[0].Protection = AppleAgxUatGpuSharedReadWrite;
  mapped[1].VirtualAddress = 0x1600014000ULL;
  mapped[1].PhysicalAddress = 0x70000000ULL;
  mapped[1].Length = TEST_PAGE_SIZE;
  mapped[1].Protection = AppleAgxUatGpuSharedReadWrite;
  assert(AppleAgxUatMapBatch(3u, &roots, mapped, 2u, &allocator,
                             &inventory) == AppleAgxUatResultOk);
  assert(inventory.MappingCount == 2u);

  invalid[0] = mapped[0];
  invalid[1] = mapped[1];
  invalid[1].PhysicalAddress += TEST_PAGE_SIZE;
  assert(AppleAgxUatUnmapBatch(3u, &roots, invalid, 2u, &allocator,
                               &inventory) == AppleAgxUatResultNotMapped);
  assert(inventory.MappingCount == 2u);

  assert(AppleAgxUatUnmapBatch(3u, &roots, mapped, 2u, &allocator,
                               &inventory) == AppleAgxUatResultOk);
  assert(inventory.MappingCount == 0u);
  assert(inventory.PageCount == 2u);
}

static void test_batch_replace_with_dummy_page_is_atomic(void) {
  FAKE_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[8];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_UAT_RANGE mapped[2];
  APPLE_AGX_UAT_RANGE invalid[2];
  APPLE_AGX_UAT_PAGE *root;
  APPLE_AGX_UAT_PAGE *level1;
  APPLE_AGX_UAT_PAGE *level2;
  unsigned long long dummy = 0x72000000ULL;
  unsigned int index;

  init_fixture(&fake, &allocator, &inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(3u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  mapped[0].VirtualAddress = 0x1600020000ULL;
  mapped[0].PhysicalAddress = 0x60000000ULL;
  mapped[0].Length = 2u * TEST_PAGE_SIZE;
  mapped[0].Protection = AppleAgxUatGpuSharedReadWrite;
  mapped[1].VirtualAddress = 0x1600028000ULL;
  mapped[1].PhysicalAddress = 0x70000000ULL;
  mapped[1].Length = TEST_PAGE_SIZE;
  mapped[1].Protection = AppleAgxUatGpuSharedReadWrite;
  assert(AppleAgxUatMapBatch(3u, &roots, mapped, 2u, &allocator,
                             &inventory) == AppleAgxUatResultOk);

  invalid[0] = mapped[0];
  invalid[1] = mapped[1];
  invalid[1].PhysicalAddress += TEST_PAGE_SIZE;
  assert(AppleAgxUatReplaceBatchWithPage(
             3u, &roots, invalid, 2u, dummy,
             AppleAgxUatGpuSharedReadWrite, &allocator, &inventory) ==
         AppleAgxUatResultNotMapped);
  assert(inventory.Mappings[0].PhysicalAddress == mapped[0].PhysicalAddress);
  assert(inventory.Mappings[0].PhysicalStride == TEST_PAGE_SIZE);

  assert(AppleAgxUatReplaceBatchWithPage(
             3u, &roots, mapped, 2u, dummy,
             AppleAgxUatGpuSharedReadWrite, &allocator, &inventory) ==
         AppleAgxUatResultOk);
  assert(inventory.MappingCount == 2u);
  for (index = 0u; index < 2u; ++index) {
    assert(inventory.Mappings[index].PhysicalAddress == dummy);
    assert(inventory.Mappings[index].PhysicalStride == 0ULL);
  }

  root = find_page(&inventory, roots.Ttbr0PhysicalAddress);
  assert(root != 0);
  level1 = find_page(&inventory,
                     root->Entries[(mapped[0].VirtualAddress >> 36) & 7ULL] &
                         TABLE_ADDRESS_MASK);
  assert(level1 != 0);
  level2 = find_page(&inventory,
                     level1->Entries[(mapped[0].VirtualAddress >> 25) & 2047ULL] &
                         TABLE_ADDRESS_MASK);
  assert(level2 != 0);
  for (index = 0u; index < 3u; ++index) {
    unsigned int leaf =
        (unsigned int)((mapped[0].VirtualAddress >> 14) & 2047ULL) + index;
    assert((level2->Entries[leaf] & TABLE_ADDRESS_MASK) == dummy);
  }

  {
    APPLE_AGX_UAT_RANGE dummy_ranges[2];
    APPLE_AGX_UAT_RANGE remapped[2];
    for (index = 0u; index < 2u; ++index) {
      dummy_ranges[index] = mapped[index];
      dummy_ranges[index].PhysicalAddress = dummy;
      remapped[index] = mapped[index];
      remapped[index].PhysicalAddress += 0x20000000ULL;
    }
    assert(AppleAgxUatReplaceBatch(3u, &roots, dummy_ranges, remapped, 2u,
                                   &allocator, &inventory) ==
           AppleAgxUatResultOk);
    assert(inventory.Mappings[0].PhysicalAddress ==
           remapped[0].PhysicalAddress);
    assert(inventory.Mappings[0].PhysicalStride == TEST_PAGE_SIZE);
    for (index = 0u; index < 2u; ++index) {
      unsigned int leaf =
          (unsigned int)((mapped[0].VirtualAddress >> 14) & 2047ULL) + index;
      assert((level2->Entries[leaf] & TABLE_ADDRESS_MASK) ==
             remapped[0].PhysicalAddress + index * TEST_PAGE_SIZE);
    }
  }
}

static void test_page_list_maps_noncontiguous_pages_atomically(void) {
  FAKE_ALLOCATOR fake;
  APPLE_AGX_UAT_ALLOCATOR allocator;
  APPLE_AGX_UAT_INVENTORY inventory;
  APPLE_AGX_UAT_PAGE pages[TEST_PAGE_COUNT];
  APPLE_AGX_UAT_MAPPING mappings[8];
  APPLE_AGX_UAT_ROOTS roots;
  APPLE_AGX_UAT_PAGE *root;
  APPLE_AGX_UAT_PAGE *level1;
  APPLE_AGX_UAT_PAGE *level2;
  const unsigned long long physical_pages[3] = {
      0x60000000ULL, 0x64004000ULL, 0x68008000ULL};
  const unsigned long long va = 0x1600200000ULL;
  unsigned int leaf;

  init_fixture(&fake, &allocator, &inventory, pages, mappings);
  assert(AppleAgxUatCreateAddressSpace(3u, &allocator, &inventory, &roots) ==
         AppleAgxUatResultOk);
  assert(AppleAgxUatMapPageList(
             3u, &roots, va, physical_pages, 3u,
             AppleAgxUatGpuSharedReadWrite, &allocator, &inventory) ==
         AppleAgxUatResultOk);
  assert(inventory.MappingCount == 1u);
  assert(inventory.Mappings[0].PhysicalAddress == physical_pages[0]);
  assert(inventory.Mappings[0].PhysicalStride ==
         APPLE_AGX_UAT_PHYSICAL_STRIDE_SCATTER);

  root = find_page(&inventory, roots.Ttbr0PhysicalAddress);
  assert(root != 0);
  level1 = find_page(&inventory,
                     root->Entries[(va >> 36) & 7ULL] & TABLE_ADDRESS_MASK);
  assert(level1 != 0);
  level2 = find_page(&inventory,
                     level1->Entries[(va >> 25) & 2047ULL] &
                         TABLE_ADDRESS_MASK);
  assert(level2 != 0);
  leaf = (unsigned int)((va >> 14) & 2047ULL);
  assert((level2->Entries[leaf] & TABLE_ADDRESS_MASK) == physical_pages[0]);
  assert((level2->Entries[leaf + 1u] & TABLE_ADDRESS_MASK) ==
         physical_pages[1]);
  assert((level2->Entries[leaf + 2u] & TABLE_ADDRESS_MASK) ==
         physical_pages[2]);
  assert(AppleAgxUatUnmap(3u, &roots, va, 3u * TEST_PAGE_SIZE, &allocator,
                           &inventory) == AppleAgxUatResultOk);
  assert(inventory.MappingCount == 0u);
}


static void test_invalid_arguments(void) {
  APPLE_AGX_UAT_ROOTS roots = {0, 0};
  APPLE_AGX_UAT_INVENTORY inventory = {0};
  APPLE_AGX_UAT_ALLOCATOR allocator = {0};
  assert(AppleAgxUatCreateAddressSpace(0, 0, &inventory, &roots) ==
         AppleAgxUatResultInvalidArgument);
  assert(AppleAgxUatCreateAddressSpace(64, &allocator, &inventory, &roots) ==
         AppleAgxUatResultUnsupportedContext);
  assert(AppleAgxUatMap(0, &roots, 0, 0, TEST_PAGE_SIZE,
                       AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                       &inventory) == AppleAgxUatResultInvalidArgument);
}

int main(void) {
  test_high_and_low_walks();
  test_create_rollback();
  test_mapping_rollback_and_capacity();
  test_exact_unmap_is_atomic_reclaims_tables_and_can_remap();
  test_batch_map_rolls_back_when_later_run_cannot_allocate();
  test_batch_unmap_prevalidates_every_exact_run();
  test_batch_replace_with_dummy_page_is_atomic();
  test_page_list_maps_noncontiguous_pages_atomically();
  test_invalid_arguments();
  return 0;
}
