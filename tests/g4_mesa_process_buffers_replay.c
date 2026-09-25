#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct agx_device { int unused; };
typedef struct {
  uint64_t Allocation, Va, Bytes;
  unsigned Bound;
} AGX_WIN32_GPUVA_BO;
struct agx_bo {
  struct agx_device *dev;
  struct { uint64_t addr; } coordinate, *va;
  AGX_WIN32_GPUVA_BO mapping;
  unsigned char *cpu;
  size_t size;
};
typedef struct { struct agx_device *Native; } AGX_WIN32_ASAHI_BACKEND;
typedef struct {
  struct agx_bo *Process[APPLE_AGX_G4_PROCESS_RANGE_COUNT];
} AGX_G4_BATCH;
static unsigned allocations;
static struct agx_bo *agx_bo_create(struct agx_device *dev, size_t bytes,
    unsigned align, unsigned flags, const char *label) {
  struct agx_bo *bo = calloc(1, sizeof(*bo));
  (void)align; (void)flags; (void)label;
  assert(bo && !(bytes & 0xffffu));
  bo->cpu = malloc(bytes);
  assert(bo->cpu);
  memset(bo->cpu, 0xa5, bytes);
  bo->dev = dev;
  bo->size = bytes;
  bo->coordinate.addr = 0x1000000ULL + (uint64_t)allocations * 0x1000000ULL;
  bo->va = &bo->coordinate;
  bo->mapping.Va = bo->coordinate.addr;
  bo->mapping.Bytes = bytes;
  bo->mapping.Bound = 1;
  ++allocations;
  return bo;
}
static const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(
    AGX_WIN32_ASAHI_BACKEND *backend, struct agx_bo *bo) {
  return bo && bo->dev == backend->Native ? &bo->mapping : NULL;
}
static void *agx_bo_map(struct agx_bo *bo) { return bo->cpu; }

#include "g4_mesa_process_buffers_function.inc"

int main(void) {
  struct agx_device native = {0};
  AGX_WIN32_ASAHI_BACKEND backend = {&native};
  AGX_G4_BATCH batch = {0};
  APPLE_AGX_G4_NATIVE_RENDER render = {0};
  APPLE_AGX_G4_PROCESS_RANGE ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT] = {0};
  render.WidthPx = 2560;
  render.HeightPx = 1600;
  render.Layers = 1;
  render.UtileWidthPx = render.UtileHeightPx = 16;
  assert(prepare_process_buffers(&backend, &batch, &render, ranges));
  assert(allocations == APPLE_AGX_G4_PROCESS_RANGE_COUNT);
  assert(ranges[2].Bytes == 0x400000u && ranges[3].Bytes == 0x20000u &&
         ranges[4].Bytes == 0x20000u && ranges[6].Bytes == 0x140000u);
  {
    const uint32_t *pages = (const uint32_t *)batch.Process[0]->cpu;
    const uint32_t *blocks = (const uint32_t *)batch.Process[1]->cpu;
    uint32_t first = (uint32_t)(ranges[2].Va >> 15);
    assert(pages[0] == first && pages[1] == first + 1u &&
           pages[127] == first + 127u);
    assert(blocks[0] == first && blocks[1] == 0u &&
           blocks[62] == first + 124u && blocks[63] == 0u);
  }
  for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    assert(batch.Process[i] && !(ranges[i].Va & 0xffffULL));
    assert(batch.Process[i]->cpu[batch.Process[i]->size - 1u] == 0u);
    free(batch.Process[i]->cpu);
    free(batch.Process[i]);
  }
  puts("g4_mesa_process_buffers_replay: PASS");
  return 0;
}
