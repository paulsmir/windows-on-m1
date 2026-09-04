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

int main(void) {
  test_surface_and_64k_contract();
  test_handle_lifetime_blocks_open_destroy();
  test_invalid_create_does_not_mutate();
  return 0;
}
