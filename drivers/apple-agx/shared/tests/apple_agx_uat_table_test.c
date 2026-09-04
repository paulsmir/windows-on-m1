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
  test_invalid_arguments();
  return 0;
}
