#include <assert.h>
#include <string.h>

#include "direct_flip_contract.h"

#define TEST_FORMAT_BGRA8888 21u

static ADMISSION_UMD_DIRECT_FLIP_RESOURCE exact_resource(void) {
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE resource;
  memset(&resource, 0, sizeof(resource));
  resource.Magic = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC;
  resource.Version = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION;
  resource.Allocation.Magic = ADMISSION_ALLOCATION_MAGIC;
  resource.Allocation.Version = ADMISSION_ALLOCATION_VERSION;
  resource.Allocation.Type = 1u;
  resource.Allocation.Format = TEST_FORMAT_BGRA8888;
  resource.Allocation.Width = 2560u;
  resource.Allocation.Height = 1600u;
  resource.Allocation.Pitch = 10240u;
  resource.Allocation.BytesPerPixel = 4u;
  resource.Allocation.Size = 0xfa0000ULL;
  resource.SegmentId = 2u;
  resource.Linear = 1u;
  resource.Displayable = 1u;
  return resource;
}

static void test_accepts_only_the_exact_pair(void) {
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE current = exact_resource();
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE candidate = exact_resource();

  assert(AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, ADMISSION_UMD_DIRECT_FLIP_IMMEDIATE,
      TEST_FORMAT_BGRA8888));

  candidate.Allocation.Width = 1920u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  candidate = exact_resource();
  candidate.Allocation.Pitch = 9984u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  candidate = exact_resource();
  candidate.SegmentId = 1u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  candidate = exact_resource();
  candidate.Linear = 0u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  candidate = exact_resource();
  candidate.Displayable = 0u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
}

static void test_rejects_invalid_identity_format_and_flags(void) {
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE current = exact_resource();
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE candidate = exact_resource();

  assert(!AdmissionUmdDirectFlipCompatible(
      0, &candidate, 0u, TEST_FORMAT_BGRA8888));
  current.Magic = 0u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  current = exact_resource();
  candidate.Allocation.Format = 22u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  candidate = exact_resource();
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 2u, TEST_FORMAT_BGRA8888));
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, 0u));
}

int main(void) {
  test_accepts_only_the_exact_pair();
  test_rejects_invalid_identity_format_and_flags();
  return 0;
}
