#include "render_backend_image.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_BACKEND_BYTES 0x00800000ULL
#define TEST_BACKEND_GPU 0x1500800000ULL
#define TEST_BACKEND_PHYSICAL 0x9d0800000ULL

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

static void test_materializes_and_relocates_exact_rebased_image(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  const APPLE_AGX_EXP208_RELOCATION *relocations;
  const APPLE_AGX_EXP208_RELOCATION *first;

  assert(storage != NULL);
  memset(storage, 0xa5, TEST_BACKEND_BYTES);
  memset(&image, 0xa5, sizeof(image));
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU;
  view.Bytes = TEST_BACKEND_BYTES;

  assert(AdmissionBackendImagePrepare(&image, &view));
  assert(image.Ready == APPLE_AGX_TRUE);
  assert(image.ArenaCpuAddress == storage);
  assert(image.ArenaPhysicalAddress == TEST_BACKEND_PHYSICAL);
  assert(image.ArenaGpuAddress == TEST_BACKEND_GPU);
  assert(image.ArenaBytes == AppleAgxRenderTemplateBytes());
  assert(image.Roots.Ta[0] == TEST_BACKEND_GPU + 0x80000ULL);
  assert(image.Roots.D3[0] == TEST_BACKEND_GPU + 0x70000ULL);
  assert(image.Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].GpuVa ==
         TEST_BACKEND_GPU);
  assert(image.Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX]
             .PhysicalAddress == TEST_BACKEND_PHYSICAL);

  relocations = AppleAgxRenderTemplateRelocations();
  first = &relocations[0];
  assert(read_u64(image.Objects[first->SourceObject].Data +
                  first->SourceOffset) ==
         image.Objects[first->TargetObject].GpuVa + first->TargetOffset);
  assert(read_u64(image.Objects[first->SourceObject].Data +
                  first->SourceOffset) < 0x1501000000ULL);

  AdmissionBackendImageReset(&image);
  assert(image.Ready == APPLE_AGX_FALSE);
  assert(image.ArenaCpuAddress == NULL);
  free(storage);
}

static void test_rejects_invalid_tail_atomically(void) {
  unsigned char *storage = (unsigned char *)malloc(TEST_BACKEND_BYTES);
  ADMISSION_LOCAL_MEMORY_VIEW view;
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_BACKEND_IMAGE before;

  assert(storage != NULL);
  memset(&image, 0x5a, sizeof(image));
  before = image;
  view.CpuAddress = storage;
  view.HostPhysicalAddress = TEST_BACKEND_PHYSICAL;
  view.GpuVirtualAddress = TEST_BACKEND_GPU + 0x4000ULL;
  view.Bytes = AppleAgxRenderTemplateBytes();
  assert(!AdmissionBackendImagePrepare(&image, &view));
  assert(memcmp(&image, &before, sizeof(image)) == 0);
  free(storage);
}

int main(void) {
  test_materializes_and_relocates_exact_rebased_image();
  test_rejects_invalid_tail_atomically();
  return 0;
}
