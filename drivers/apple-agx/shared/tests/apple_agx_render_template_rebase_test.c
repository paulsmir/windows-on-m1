#include "apple_agx_render_template_rebase.h"

#include <assert.h>
#include <stdlib.h>

static void test_exp208_arena_rebases_inside_proven_16m_mapping(void) {
  unsigned char *arena =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];

  assert(arena != NULL);
  assert(AppleAgxRenderTemplateMaterialize(
      arena, AppleAgxRenderTemplateBytes(), &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(
      arena, AppleAgxRenderTemplateBytes(), 0x9d5000000ULL,
      0x1500800000ULL, 0x1500000000ULL, 0x01000000ULL,
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &roots));
  assert(objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].GpuVa ==
         0x1500800000ULL);
  assert(objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX]
             .PhysicalAddress == 0x9d5000000ULL);
  assert(objects[40].GpuVa == 0x1500ae0000ULL);
  assert(roots.Ta[0] == 0x1500880000ULL);
  assert(roots.Ta[1] == 0x1500898000ULL);
  assert(roots.D3[0] == 0x1500870000ULL);
  assert(roots.D3[1] == 0x1500890000ULL);
  free(arena);
}

static void test_rebase_rejects_unmapped_or_misaligned_arena(void) {
  unsigned char *arena =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];

  assert(arena != NULL);
  assert(AppleAgxRenderTemplateMaterialize(
      arena, AppleAgxRenderTemplateBytes(), &roots));
  assert(!AppleAgxRenderTemplateBuildRelocationObjectsRebased(
      arena, AppleAgxRenderTemplateBytes(), 0x9d5000000ULL,
      0x1500804000ULL, 0x1500000000ULL, 0x01000000ULL,
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &roots));
  assert(!AppleAgxRenderTemplateBuildRelocationObjectsRebased(
      arena, AppleAgxRenderTemplateBytes(), 0x9d5000000ULL,
      0x1500c00000ULL, 0x1500000000ULL, 0x01000000ULL,
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &roots));
  free(arena);
}

int main(void) {
  test_exp208_arena_rebases_inside_proven_16m_mapping();
  test_rebase_rejects_unmapped_or_misaligned_arena();
  return 0;
}
