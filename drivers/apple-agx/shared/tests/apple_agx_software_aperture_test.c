#include "apple_agx_software_aperture.h"

#include <assert.h>
#include <string.h>

#define PAGE_4K 0x1000ULL

static void test_map_copies_pages_and_resolves_byte_offsets(void) {
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY entries[8];
  APPLE_AGX_SOFTWARE_APERTURE table;
  APPLE_AGX_U64 pages[3] = {0x20000000ULL, 0x30000000ULL, 0x40000000ULL};
  APPLE_AGX_U64 physical = 0ULL;

  memset(entries, 0xa5, sizeof(entries));
  assert(AppleAgxSoftwareApertureInitialize(&table, entries, 8u) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureMap(&table, 2u, pages, 3u) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureResolve(&table, 2ULL * PAGE_4K + 0x345ULL,
                                         &physical) ==
         AppleAgxSoftwareApertureOk);
  assert(physical == 0x20000345ULL);
  assert(AppleAgxSoftwareApertureResolve(&table, 4ULL * PAGE_4K + 0xfffULL,
                                         &physical) ==
         AppleAgxSoftwareApertureOk);
  assert(physical == 0x40000fffULL);
}

static void test_unmap_replaces_every_slot_with_exact_dummy_page(void) {
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY entries[4];
  APPLE_AGX_SOFTWARE_APERTURE table;
  APPLE_AGX_U64 pages[2] = {0x50000000ULL, 0x60000000ULL};
  APPLE_AGX_U64 physical = 0ULL;

  assert(AppleAgxSoftwareApertureInitialize(&table, entries, 4u) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureMap(&table, 1u, pages, 2u) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureUnmap(&table, 1u, 2u, 0x70000000ULL) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureResolve(&table, PAGE_4K + 0x24ULL,
                                         &physical) ==
         AppleAgxSoftwareApertureDummy);
  assert(physical == 0x70000024ULL);
  assert(AppleAgxSoftwareApertureResolve(&table, 2ULL * PAGE_4K + 0x48ULL,
                                         &physical) ==
         AppleAgxSoftwareApertureDummy);
  assert(physical == 0x70000048ULL);
}

static void test_invalid_map_is_atomic(void) {
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY entries[4];
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY before[4];
  APPLE_AGX_SOFTWARE_APERTURE table;
  APPLE_AGX_U64 pages[2] = {0x80000000ULL, 0x80001001ULL};

  assert(AppleAgxSoftwareApertureInitialize(&table, entries, 4u) ==
         AppleAgxSoftwareApertureOk);
  memcpy(before, entries, sizeof(entries));
  assert(AppleAgxSoftwareApertureMap(&table, 1u, pages, 2u) ==
         AppleAgxSoftwareApertureMisaligned);
  assert(memcmp(entries, before, sizeof(entries)) == 0);
  assert(AppleAgxSoftwareApertureMap(&table, 3u, pages, 2u) ==
         AppleAgxSoftwareApertureOutOfRange);
  assert(memcmp(entries, before, sizeof(entries)) == 0);
}

static void test_unmapped_and_invalid_requests_fail_closed(void) {
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY entries[2];
  APPLE_AGX_SOFTWARE_APERTURE table;
  APPLE_AGX_U64 physical = 0xa5a5a5a5a5a5a5a5ULL;

  assert(AppleAgxSoftwareApertureInitialize(&table, entries, 2u) ==
         AppleAgxSoftwareApertureOk);
  assert(AppleAgxSoftwareApertureResolve(&table, 0ULL, &physical) ==
         AppleAgxSoftwareApertureNotMapped);
  assert(physical == 0ULL);
  assert(AppleAgxSoftwareApertureResolve(&table, 2ULL * PAGE_4K,
                                         &physical) ==
         AppleAgxSoftwareApertureOutOfRange);
  assert(physical == 0ULL);
  assert(AppleAgxSoftwareApertureUnmap(&table, 0u, 1u, 0ULL) ==
         AppleAgxSoftwareApertureInvalidArgument);
  assert(AppleAgxSoftwareApertureUnmap(&table, 0u, 1u, 0x90000001ULL) ==
         AppleAgxSoftwareApertureMisaligned);
}

int main(void) {
  test_map_copies_pages_and_resolves_byte_offsets();
  test_unmap_replaces_every_slot_with_exact_dummy_page();
  test_invalid_map_is_atomic();
  test_unmapped_and_invalid_requests_fail_closed();
  return 0;
}
