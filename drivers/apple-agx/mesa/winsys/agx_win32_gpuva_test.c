#include "agx_win32_gpuva.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned reserve, map, free_va, make, wait_page, submit, wait_render, evict;
  unsigned phase;
  uint64_t requested_bytes, mapped_pages, mapped_va, map_fence;
  uint64_t submitted_va, render_fence;
  unsigned command_checksum;
  int bad_order;
} FIXTURE;

static int reserve_va(void *ctx, uint64_t bytes, uint64_t minimum,
                      uint64_t maximum, uint64_t *va) {
  FIXTURE *f = ctx;
  f->reserve++;
  f->requested_bytes = bytes;
  if (minimum != 0x10000 || maximum != (1ULL << 39)) return 0;
  *va = 0x20000;
  return 1;
}
static int map_va(void *ctx, uint64_t allocation, uint64_t va,
                  uint64_t pages, unsigned protection, uint64_t *fence) {
  FIXTURE *f = ctx;
  f->map++;
  if (allocation != 17 || va != 0x20000 ||
      protection != AGX_GPUVA_MAP_WRITE) return 0;
  f->mapped_pages = pages;
  f->mapped_va = va;
  *fence = f->map_fence = 3;
  return 2;
}
static int free_va(void *ctx, uint64_t va, uint64_t bytes) {
  FIXTURE *f = ctx;
  f->free_va++;
  return va == 0x20000 && bytes == 0x10000;
}
static int make_resident(void *ctx, const uint64_t *allocations,
                         unsigned count, uint64_t *fence) {
  FIXTURE *f = ctx;
  f->make++;
  if (f->phase != 1 || count != 2 || allocations[0] != 17 ||
      allocations[1] != 19) f->bad_order = 1;
  f->phase = 2;
  *fence = 5;
  return 2;
}
static int wait_paging(void *ctx, uint64_t fence) {
  FIXTURE *f = ctx;
  f->wait_page++;
  if (fence == 3 && f->phase == 0) f->phase = 1;
  else if (fence == 5 && f->phase == 2) f->phase = 3;
  else f->bad_order = 1;
  return !f->bad_order;
}
static int submit(void *ctx, const uint64_t *written, unsigned written_count,
                  uint64_t va, uint32_t bytes,
                  const void *private_data, uint32_t private_bytes,
                  uint64_t *fence) {
  FIXTURE *f = ctx;
  const unsigned char *command = private_data;
  f->submit++;
  if (f->phase != 3 || written_count != 1 || written[0] != 19 ||
      va != 0x20000 || bytes != 64 ||
      private_bytes != 4 || command[0] != 0xa1 || command[3] != 0xd4)
    f->bad_order = 1;
  f->submitted_va = va;
  f->command_checksum = command[0] + command[1] + command[2] + command[3];
  f->phase = 4;
  *fence = f->render_fence = 7;
  return !f->bad_order;
}
static int wait_render(void *ctx, uint64_t fence) {
  FIXTURE *f = ctx;
  f->wait_render++;
  if (f->phase != 4 || fence != 7) f->bad_order = 1;
  f->phase = 5;
  return !f->bad_order;
}
static int evict(void *ctx, const uint64_t *allocations, unsigned count) {
  FIXTURE *f = ctx;
  f->evict++;
  if (f->phase != 5 || count != 2 || allocations[0] != 17 ||
      allocations[1] != 19) f->bad_order = 1;
  f->phase = 6;
  return !f->bad_order;
}
static AGX_WIN32_GPUVA_OPS ops = {reserve_va, map_va, free_va, make_resident,
                                  wait_paging, submit, wait_render, evict, NULL, NULL};

static int resident_immediate(void *ctx, const uint64_t *handles,
                              unsigned count, uint64_t *fence) {
  FIXTURE *f = ctx;
  f->make++;
  *fence = 0;
  return count == 1 && handles[0] == 17;
}
static int submit_uncertain(void *ctx, const uint64_t *written,
                            unsigned written_count,uint64_t va, uint32_t bytes,
                            const void *data, uint32_t data_bytes,
                            uint64_t *fence) {
  FIXTURE *f = ctx;
  (void)written; (void)written_count; (void)va; (void)bytes;
  (void)data; (void)data_bytes; (void)fence;
  f->submit++;
  return 2;
}
static int resident_uncertain(void *ctx, const uint64_t *handles,
                              unsigned count, uint64_t *fence) {
  FIXTURE *f = ctx;
  (void)handles; (void)count; (void)fence;
  f->make++;
  return 3;
}

static void minimal_draw_uses_stable_va_and_waits_for_residency(void) {
  FIXTURE f = {0};
  AGX_WIN32_GPUVA_SPACE space = {0};
  AGX_WIN32_GPUVA_BO command = {0};
  AGX_WIN32_GPUVA_BO color = {0};
  const AGX_WIN32_GPUVA_BO *references[2] = {&command, &color};
  const AGX_WIN32_GPUVA_BO *written[1] = {&color};
  const unsigned char native_asahi[4] = {0xa1, 0xb2, 0xc3, 0xd4};
  uint64_t completion = 0;
  assert(AgxWin32GpuvaInit(&space, &ops, &f));
  assert(AgxWin32GpuvaBind(&space, &command, 17, 0x4000, 0,
                           AGX_GPUVA_MAP_WRITE));
  assert(command.Va == 0x20000 && command.Bytes == 0x10000);
  assert(f.requested_bytes == 0x10000 && f.mapped_pages == 16);
  /* The second reference already has a valid mapping from the same process. */
  color.Allocation = 19;
  color.Va = 0x40000;
  color.Bytes = 0x10000;
  color.Bound = 1;
  assert(AgxWin32GpuvaSubmit(&space, references, 2, &command, 64,
                             written, 1,
                             native_asahi, sizeof(native_asahi), &completion));
  assert(completion == 7 && f.submitted_va == command.Va);
  assert(f.command_checksum == 0xa1 + 0xb2 + 0xc3 + 0xd4);
  assert(!f.bad_order && f.make == 1 && f.wait_page == 2 && f.submit == 1);
  assert(AgxWin32GpuvaRetire(&space, completion));
  assert(f.wait_render == 1 && f.evict == 1);
  assert(AgxWin32GpuvaUnbind(&space, &command));
  assert(f.free_va == 1);
}

static void invalid_mapping_and_missing_residency_never_submit(void) {
  FIXTURE f = {0};
  AGX_WIN32_GPUVA_SPACE space = {0};
  AGX_WIN32_GPUVA_BO command = {0};
  const AGX_WIN32_GPUVA_BO *references[1] = {&command};
  uint64_t fence = 0;
  assert(AgxWin32GpuvaInit(&space, &ops, &f));
  assert(!AgxWin32GpuvaBind(&space, &command, 17, UINT64_MAX, 0,
                            AGX_GPUVA_MAP_WRITE));
  assert(f.reserve == 0 && f.map == 0);
  assert(!AgxWin32GpuvaSubmit(&space, references, 1, &command, 16,
                              NULL, 0,
                              "draw", 4, &fence));
  assert(f.submit == 0 && f.make == 0);
}

static void written_bo_must_be_a_resident_reference(void) {
  FIXTURE f = {0};
  AGX_WIN32_GPUVA_SPACE space = {0};
  AGX_WIN32_GPUVA_BO command = {.Allocation = 17, .Va = 0x20000,
                                 .Bytes = 0x10000, .Bound = 1};
  AGX_WIN32_GPUVA_BO color = {.Allocation = 19, .Va = 0x40000,
                               .Bytes = 0x10000, .Bound = 1};
  const AGX_WIN32_GPUVA_BO *references[1] = {&command};
  const AGX_WIN32_GPUVA_BO *written[1] = {&color};
  uint64_t fence = 0;
  assert(AgxWin32GpuvaInit(&space, &ops, &f));
  assert(!AgxWin32GpuvaSubmit(&space, references, 1, &command, 64,
                              written, 1, "draw", 4, &fence));
  assert(f.make == 0 && f.submit == 0);
}

static void uncertain_completion_preserves_residency(void) {
  FIXTURE f = {0};
  AGX_WIN32_GPUVA_OPS uncertain_ops = ops;
  AGX_WIN32_GPUVA_SPACE space = {0};
  AGX_WIN32_GPUVA_BO command = {.Allocation = 17, .Va = 0x20000,
                                 .Bytes = 0x10000, .Bound = 1};
  const AGX_WIN32_GPUVA_BO *references[1] = {&command};
  uint64_t fence = 0;
  uncertain_ops.MakeResident = resident_immediate;
  uncertain_ops.Submit = submit_uncertain;
  assert(AgxWin32GpuvaInit(&space, &uncertain_ops, &f));
  assert(!AgxWin32GpuvaSubmit(&space, references, 1, &command, 64,
                              NULL, 0,
                              "draw", 4, &fence));
  assert(space.Terminal && space.Held && space.HeldCount == 1);
  assert(f.submit == 1 && f.evict == 0);
  /* The real process keeps the allocation until reset; release test memory. */
  free(space.Held);
}

static void uncertain_residency_never_submits_or_frees(void) {
  FIXTURE f = {0};
  AGX_WIN32_GPUVA_OPS uncertain_ops = ops;
  AGX_WIN32_GPUVA_SPACE space = {0};
  AGX_WIN32_GPUVA_BO command = {.Allocation = 17, .Va = 0x20000,
                                 .Bytes = 0x10000, .Bound = 1};
  const AGX_WIN32_GPUVA_BO *references[1] = {&command};
  uint64_t fence = 0;
  uncertain_ops.MakeResident = resident_uncertain;
  assert(AgxWin32GpuvaInit(&space, &uncertain_ops, &f));
  assert(!AgxWin32GpuvaSubmit(&space, references, 1, &command, 64,
                              NULL, 0,
                              "draw", 4, &fence));
  assert(space.Terminal && space.Held && f.submit == 0 && f.evict == 0);
  assert(!AgxWin32GpuvaUnbind(&space, &command));
  free(space.Held);
}

/* C53 B0: reserved ranges and tile maps at allocation offsets */
typedef struct {
  uint64_t reserve_va, freed_va, freed_bytes, map_va, map_allocation;
  uint64_t map_offset_pages, map_pages, fence;
  unsigned reserves, maps, frees, waits, result;
  unsigned protection;
} RANGE;
static int range_reserve(void *ctx, uint64_t bytes, uint64_t minimum,
                         uint64_t maximum, uint64_t *va) {
  RANGE *r = ctx;
  r->reserves++;
  (void)bytes; (void)minimum; (void)maximum;
  *va = r->reserve_va;
  return 1;
}
static int range_free(void *ctx, uint64_t va, uint64_t bytes) {
  RANGE *r = ctx;
  r->frees++;
  r->freed_va = va;
  r->freed_bytes = bytes;
  return 1;
}
static int range_wait(void *ctx, uint64_t fence) {
  RANGE *r = ctx;
  r->waits++;
  return fence == r->fence;
}
static int range_map_at(void *ctx, uint64_t allocation, uint64_t va,
                        uint64_t offset_pages, uint64_t pages,
                        unsigned protection, uint64_t *fence) {
  RANGE *r = ctx;
  r->maps++;
  r->map_allocation = allocation;
  r->map_va = va;
  r->map_offset_pages = offset_pages;
  r->map_pages = pages;
  r->protection = protection;
  *fence = r->fence;
  return (int)r->result;
}
static void reserved_ranges_map_tiles_at_offsets(void) {
  RANGE r = {0};
  AGX_WIN32_GPUVA_OPS range_ops = {range_reserve, map_va, range_free,
                                   make_resident, range_wait, submit,
                                   wait_render, evict, NULL, NULL, NULL, NULL};
  AGX_WIN32_GPUVA_SPACE space;
  uint64_t va = 0;
  assert(AgxWin32GpuvaInit(&space, &range_ops, &r));
  /* sizes are whole 64 KiB tiles */
  assert(!AgxWin32GpuvaReserveRange(&space, 0x18000, &va) && !r.reserves);
  /* a reservation the callback misaligns is given back */
  r.reserve_va = 0x48000;
  assert(!AgxWin32GpuvaReserveRange(&space, 0x40000, &va));
  assert(r.frees == 1 && r.freed_va == 0x48000 && !space.Terminal);
  r.reserve_va = 0x400000;
  assert(AgxWin32GpuvaReserveRange(&space, 0x40000, &va) && va == 0x400000);
  /* without MapAt nothing is mapped */
  assert(!AgxWin32GpuvaMapRange(&space, va + 0x10000, 17, 0x20000, 0x10000,
                                AGX_GPUVA_MAP_WRITE) && !r.maps);
  space.Ops.MapAt = range_map_at;
  /* tile 1 of the range onto tile 2 of allocation 17: offset in 4 KiB pages */
  r.result = 2;
  r.fence = 9;
  assert(AgxWin32GpuvaMapRange(&space, va + 0x10000, 17, 0x20000, 0x10000,
                               AGX_GPUVA_MAP_WRITE));
  assert(r.map_va == 0x410000 && r.map_allocation == 17 &&
         r.map_offset_pages == 0x20 && r.map_pages == 0x10 &&
         r.protection == AGX_GPUVA_MAP_WRITE && r.waits == 1);
  /* unaligned offsets never reach the callback */
  assert(!AgxWin32GpuvaMapRange(&space, va, 17, 0x1000, 0x10000, 0) && r.maps == 1);
  /* refused map: not terminal */
  r.result = 0;
  assert(!AgxWin32GpuvaMapRange(&space, va, 17, 0, 0x10000, 0) && !space.Terminal);
  /* free refuses while a submission is in flight */
  space.OlderCount = 1;
  assert(!AgxWin32GpuvaFreeRange(&space, va, 0x40000) && r.frees == 1);
  space.OlderCount = 0;
  assert(AgxWin32GpuvaFreeRange(&space, va, 0x40000) && r.freed_va == va &&
         r.freed_bytes == 0x40000);
  /* uncertain ownership or a pending map without a fence is terminal */
  r.result = 3;
  assert(!AgxWin32GpuvaMapRange(&space, va, 17, 0, 0x10000, 0) && space.Terminal);
  space.Terminal = 0;
  r.result = 2;
  r.fence = 0;
  assert(!AgxWin32GpuvaMapRange(&space, va, 17, 0, 0x10000, 0) && space.Terminal);
}

int main(void) {
  reserved_ranges_map_tiles_at_offsets();
  minimal_draw_uses_stable_va_and_waits_for_residency();
  invalid_mapping_and_missing_residency_never_submit();
  written_bo_must_be_a_resident_reference();
  uncertain_completion_preserves_residency();
  uncertain_residency_never_submits_or_frees();
  puts("agx_win32_gpuva_test: PASS");
  return 0;
}
