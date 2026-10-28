#include <assert.h>
#include <string.h>

#include "direct_flip_contract.h"
#include "apple_agx_fixed_panel_modes.h"

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

/* rc106: every offered fixed-panel mode is an exact resource; a pair of
 * different modes, a non-offered size and a padded pitch are not. */
static ADMISSION_UMD_DIRECT_FLIP_RESOURCE mode_resource(unsigned int width,
                                                        unsigned int height) {
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE resource = exact_resource();
  resource.Allocation.Width = width;
  resource.Allocation.Height = height;
  resource.Allocation.Pitch = width * 4u;
  resource.Allocation.Size = (unsigned long long)width * 4u * height;
  return resource;
}

static void test_accepts_every_offered_mode_pair_only(void) {
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE native = exact_resource();
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE current;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE candidate;
  unsigned int index;

  assert(APPLE_AGX_FIXED_PANEL_MODE_COUNT == 11u);
  assert(AppleAgxFixedPanelModeIndex(2560u, 1600u) == 0u);
  for (index = 0u; index < APPLE_AGX_FIXED_PANEL_MODE_COUNT; ++index) {
    current = mode_resource(AppleAgxFixedPanelModeSizes[index][0],
                            AppleAgxFixedPanelModeSizes[index][1]);
    candidate = current;
    assert(current.Allocation.Pitch % 64u == 0u);
    assert(AdmissionUmdDirectFlipCompatible(
        &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
    /* Another offered mode is not direct-flip compatible with it. */
    if (index != 0u) {
      assert(!AdmissionUmdDirectFlipCompatible(
          &native, &candidate, 0u, TEST_FORMAT_BGRA8888));
      assert(!AdmissionUmdDirectFlipCompatible(
          &current, &native, 0u, TEST_FORMAT_BGRA8888));
    }
  }
  current = mode_resource(1920u, 1080u);
  candidate = current;
  assert(current.Allocation.Pitch == 7680u);
  assert(current.Allocation.Size == 0x7e9000ULL);
  assert(AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  candidate.Allocation.Pitch = 7744u;
  candidate.Allocation.Size = 7744ULL * 1080u;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  candidate = current;
  candidate.Allocation.Size = 0xfa0000ULL;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  current = mode_resource(1366u, 768u);
  current.Allocation.Pitch = 5504u;
  current.Allocation.Size = 5504ULL * 768u;
  candidate = current;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
  current = mode_resource(1920u, 1088u);
  candidate = current;
  assert(!AdmissionUmdDirectFlipCompatible(
      &current, &candidate, 0u, TEST_FORMAT_BGRA8888));
}

int main(void) {
  test_accepts_only_the_exact_pair();
  test_rejects_invalid_identity_format_and_flags();
  test_accepts_every_offered_mode_pair_only();
  return 0;
}
