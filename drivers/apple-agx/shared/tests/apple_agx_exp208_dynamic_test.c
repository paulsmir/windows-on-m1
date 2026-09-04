#include "apple_agx_exp208_dynamic.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define U32_AT(buffer, offset) (*(uint32_t *)((buffer) + (offset)))

static const uint32_t ExpectedOffsets[] = {
    0x48000u, 0x50000u, 0x60000u, 0x7000cu, 0x70010u, 0x70014u,
    0x78060u, 0x7823cu, 0x8001cu, 0x8824cu, 0x908e4u, 0x908e8u,
    0x98480u, 0x98484u, 0x98580u, 0x98584u, 0xd0000u, 0xd8000u,
};

static int byte_is_expected(uint32_t byte_offset, int include_init_bm) {
  size_t index;
  for (index = 0u; index < sizeof(ExpectedOffsets) / sizeof(ExpectedOffsets[0]);
       ++index) {
    uint32_t bytes = ExpectedOffsets[index] == 0x78060u ? 8u : 4u;
    if (!include_init_bm && ExpectedOffsets[index] == 0x8001cu)
      continue;
    if (byte_offset >= ExpectedOffsets[index] &&
        byte_offset < ExpectedOffsets[index] + bytes)
      return 1;
  }
  return 0;
}

static unsigned char *new_image(void) {
  unsigned char *image = (unsigned char *)calloc(1u, APPLE_AGX_EXP208_IMAGE_BYTES);
  assert(image != NULL);
  return image;
}

static uint64_t read_u64(const unsigned char *buffer, uint32_t offset) {
  uint64_t value;
  memcpy(&value, buffer + offset, sizeof(value));
  return value;
}

static void seed_source_zero_fields(unsigned char *image) {
  U32_AT(image, 0x58008u) = 0u; /* EventControl.submission_id. */
  U32_AT(image, 0x78058u) = 0u; /* Start3D.unk_5c. */
  U32_AT(image, 0x908f4u) = 0u; /* Start3DStruct7.queue_cmd_count. */
  U32_AT(image, 0x98594u) = 0u; /* StartTAStruct3.queue_cmd_count. */
}

static void test_first_job_patches_only_exact_dynamic_fields(void) {
  unsigned char *image = new_image();
  unsigned char *before = new_image();
  APPLE_AGX_EXP208_DYNAMIC_INPUT input = {1u, 7u, 19u, APPLE_AGX_TRUE};
  uint32_t index;

  memset(image, 0xa5, APPLE_AGX_EXP208_IMAGE_BYTES);
  seed_source_zero_fields(image);
  memcpy(before, image, APPLE_AGX_EXP208_IMAGE_BYTES);

  assert(AppleAgxExp208PatchDynamic(image, APPLE_AGX_EXP208_IMAGE_BYTES,
                                    &input));

  assert(U32_AT(image, 0x48000u) == 0x7a000000u);
  assert(U32_AT(image, 0xd0000u) == 0x7a000000u);
  assert(U32_AT(image, 0x50000u) == 0x3d000000u);
  assert(U32_AT(image, 0xd8000u) == 0x3d000000u);
  assert(U32_AT(image, 0x60000u) == 2u);
  assert(U32_AT(image, 0x7000cu) == 0x7a000100u);
  assert(U32_AT(image, 0x70010u) == 7u);
  assert(U32_AT(image, 0x70014u) == 0x3d000100u);
  assert(read_u64(image, 0x78060u) == 0x003d0000ULL);
  assert(U32_AT(image, 0x7823cu) == 0x3d000100u);
  assert(U32_AT(image, 0x8001cu) == 0x7a000100u);
  assert(U32_AT(image, 0x8824cu) == 0x7a000100u);
  assert(U32_AT(image, 0x908e4u) == 0x3d000100u);
  assert(U32_AT(image, 0x908e8u) == 19u);
  assert(U32_AT(image, 0x98480u) == 19u);
  assert(U32_AT(image, 0x98484u) == 0x7a000100u);
  assert(U32_AT(image, 0x98580u) == 0x7a000100u);
  assert(U32_AT(image, 0x98584u) == 7u);
  assert(U32_AT(image, 0x58008u) == 0u);
  assert(U32_AT(image, 0x78058u) == 0u);
  assert(U32_AT(image, 0x908f4u) == 0u);
  assert(U32_AT(image, 0x98594u) == 0u);

  for (index = 0u; index < APPLE_AGX_EXP208_IMAGE_BYTES; ++index) {
    if (!byte_is_expected(index, 1))
      assert(image[index] == before[index]);
  }
  free(before);
  free(image);
}

static void test_reused_job_derives_sequence_and_does_not_touch_init_bm(void) {
  unsigned char *image = new_image();
  APPLE_AGX_EXP208_DYNAMIC_INPUT input = {3u, 1u, 2u, APPLE_AGX_FALSE};
  APPLE_AGX_EXP208_DYNAMIC_RESULT result;

  assert(AppleAgxExp208DeriveDynamic(&input, &result));
  assert(result.TaPreviousStamp == 0x7a000200u);
  assert(result.D3PreviousStamp == 0x3d000200u);
  assert(result.TaCurrentStamp == 0x7a000300u);
  assert(result.D3CurrentStamp == 0x3d000300u);
  assert(result.EventCount == 6u);
  assert(result.Start3dQueueCommandCount == 0x003d0002u);
  U32_AT(image, 0x8001cu) = 0x11223344u;
  assert(AppleAgxExp208PatchDynamic(image, APPLE_AGX_EXP208_IMAGE_BYTES,
                                    &input));
  assert(U32_AT(image, 0x48000u) == 0x7a000200u);
  assert(U32_AT(image, 0x50000u) == 0x3d000200u);
  assert(U32_AT(image, 0x60000u) == 6u);
  assert(U32_AT(image, 0x7000cu) == 0x7a000300u);
  assert(U32_AT(image, 0x70014u) == 0x3d000300u);
  assert(read_u64(image, 0x78060u) == 0x003d0002ULL);
  assert(U32_AT(image, 0x8001cu) == 0x11223344u);
  free(image);
}

static void assert_rejected_without_mutation(
    const APPLE_AGX_EXP208_DYNAMIC_INPUT *input, uint32_t capacity) {
  unsigned char *image = new_image();
  unsigned char *before = new_image();
  memset(image, 0x5a, APPLE_AGX_EXP208_IMAGE_BYTES);
  memcpy(before, image, APPLE_AGX_EXP208_IMAGE_BYTES);
  assert(!AppleAgxExp208PatchDynamic(image, capacity, input));
  assert(memcmp(image, before, APPLE_AGX_EXP208_IMAGE_BYTES) == 0);
  free(before);
  free(image);
}

static void test_invalid_inputs_are_rejected_atomically(void) {
  APPLE_AGX_EXP208_DYNAMIC_INPUT zero_sequence = {0u, 0u, 1u, APPLE_AGX_FALSE};
  APPLE_AGX_EXP208_DYNAMIC_INPUT same_event = {1u, 4u, 4u, APPLE_AGX_TRUE};
  APPLE_AGX_EXP208_DYNAMIC_INPUT event_out_of_range = {1u, 127u, 128u,
                                                       APPLE_AGX_TRUE};
  APPLE_AGX_EXP208_DYNAMIC_INPUT init_after_first = {2u, 0u, 1u,
                                                     APPLE_AGX_TRUE};
  APPLE_AGX_EXP208_DYNAMIC_INPUT event_count_overflow = {
      0x80000000u, 0u, 1u, APPLE_AGX_FALSE};
  APPLE_AGX_EXP208_DYNAMIC_INPUT stamp_overflow = {
      0x00860000u, 0u, 1u, APPLE_AGX_FALSE};
  APPLE_AGX_EXP208_DYNAMIC_INPUT valid = {1u, 0u, 1u, APPLE_AGX_TRUE};

  assert_rejected_without_mutation(&zero_sequence, APPLE_AGX_EXP208_IMAGE_BYTES);
  assert_rejected_without_mutation(&same_event, APPLE_AGX_EXP208_IMAGE_BYTES);
  assert_rejected_without_mutation(&event_out_of_range,
                                   APPLE_AGX_EXP208_IMAGE_BYTES);
  assert_rejected_without_mutation(&init_after_first,
                                   APPLE_AGX_EXP208_IMAGE_BYTES);
  assert_rejected_without_mutation(&event_count_overflow,
                                   APPLE_AGX_EXP208_IMAGE_BYTES);
  assert_rejected_without_mutation(&stamp_overflow,
                                   APPLE_AGX_EXP208_IMAGE_BYTES);
  assert_rejected_without_mutation(&valid,
                                   APPLE_AGX_EXP208_IMAGE_BYTES - 1u);
  assert(!AppleAgxExp208PatchDynamic(NULL, APPLE_AGX_EXP208_IMAGE_BYTES, &valid));
  assert(!AppleAgxExp208PatchDynamic((void *)1, APPLE_AGX_EXP208_IMAGE_BYTES,
                                    NULL));
}

int main(void) {
  test_first_job_patches_only_exact_dynamic_fields();
  test_reused_job_derives_sequence_and_does_not_touch_init_bm();
  test_invalid_inputs_are_rejected_atomically();
  return 0;
}
