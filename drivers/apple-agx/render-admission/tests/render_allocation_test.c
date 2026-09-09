#include "render_allocation.h"

#include <assert.h>
#include <string.h>

static void test_surface_and_64k_contract(void) {
  ADMISSION_ALLOCATION_DESCRIPTION description;
  unsigned long long aligned = 0;

  assert(AdmissionAllocationDescribe(2560u, 1600u, 4u, 3u, 21u, 0u,
                                     &description));
  assert(description.Pitch == 10240u);
  assert(description.Size == 0xfa0000ULL);
  assert(AdmissionAllocationDescriptionValid(&description));
  assert(AdmissionAllocationAlign64K(description.Size, &aligned));
  assert(aligned == 0xfa0000ULL);
  description.Pitch++;
  assert(!AdmissionAllocationDescriptionValid(&description));
  assert(!AdmissionAllocationDescribe(0xffffffffu, 0xffffffffu, 4u, 3u,
                                      21u, 0u, &description));
}

static void test_handle_lifetime_blocks_open_destroy(void) {
  ADMISSION_ALLOCATION_DESCRIPTION description;
  ADMISSION_ALLOCATION_OBJECT allocation;

  assert(AdmissionAllocationDescribe(64u, 64u, 4u, 1u, 2u, 1u,
                                     &description));
  assert(AdmissionAllocationCreate(&description, &allocation));
  assert(AdmissionAllocationOpen(&allocation));
  assert(!AdmissionAllocationDestroy(&allocation));
  assert(AdmissionAllocationClose(&allocation));
  assert(!AdmissionAllocationClose(&allocation));
  assert(AdmissionAllocationDestroy(&allocation));
  assert(!AdmissionAllocationDestroy(&allocation));
}

static void test_invalid_create_does_not_mutate(void) {
  ADMISSION_ALLOCATION_DESCRIPTION description;
  ADMISSION_ALLOCATION_OBJECT allocation;
  ADMISSION_ALLOCATION_OBJECT before;

  assert(AdmissionAllocationDescribe(64u, 64u, 4u, 1u, 2u, 0u,
                                     &description));
  description.Reserved = 1u;
  memset(&allocation, 0x5a, sizeof(allocation));
  before = allocation;
  assert(!AdmissionAllocationCreate(&description, &allocation));
  assert(memcmp(&allocation, &before, sizeof(allocation)) == 0);
}

static void test_allocation_contains_bounded_render_view(void) {
  ADMISSION_ALLOCATION_DESCRIPTION description;

  assert(AdmissionAllocationDescribe(16u, 256u, 4u, 3u, 21u, 0u,
                                     &description));
  assert(description.Pitch == 64u && description.Size == 0x4000u);
  assert(AdmissionAllocationContainsView(
      &description, 16u, 16u, 64u, 0x4000u));
  assert(!AdmissionAllocationContainsView(
      &description, 17u, 16u, 64u, 0x4000u));
  assert(!AdmissionAllocationContainsView(
      &description, 16u, 257u, 64u, 0x4000u));
  assert(!AdmissionAllocationContainsView(
      &description, 16u, 16u, 80u, 0x4000u));
  assert(!AdmissionAllocationContainsView(
      &description, 16u, 16u, 64u, 1023u));
}

int main(void) {
  test_surface_and_64k_contract();
  test_handle_lifetime_blocks_open_destroy();
  test_invalid_create_does_not_mutate();
  test_allocation_contains_bounded_render_view();
  return 0;
}
