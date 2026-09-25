#include "agx_win32_gpuva.h"
#include <stdlib.h>
#include <string.h>

#define AGX_GPUVA_PAGE 0x10000ULL
#define AGX_GPUVA_LIMIT (1ULL << 39)
#define AGX_GPUVA_LOW_LIMIT (1ULL << 32)

int AgxWin32GpuvaInit(AGX_WIN32_GPUVA_SPACE *space,
                      const AGX_WIN32_GPUVA_OPS *ops, void *context) {
  if (!space || !ops || !context || !ops->Reserve || !ops->Map ||
      !ops->Free || !ops->MakeResident || !ops->WaitPaging ||
      !ops->Submit || !ops->WaitRender || !ops->Evict) return 0;
  memset(space, 0, sizeof(*space));
  space->Ops = *ops;
  space->Context = context;
  return 1;
}

int AgxWin32GpuvaBind(AGX_WIN32_GPUVA_SPACE *space, AGX_WIN32_GPUVA_BO *bo,
                      uint64_t allocation, uint64_t bytes, int low_va,
                      unsigned protection) {
  uint64_t length, va = 0, fence = 0;
  int mapped;
  const uint64_t limit = low_va ? AGX_GPUVA_LOW_LIMIT : AGX_GPUVA_LIMIT;
  if (!space || !bo || !allocation || bo->Bound || space->Terminal ||
      (protection & ~(AGX_GPUVA_MAP_WRITE | AGX_GPUVA_MAP_EXECUTE)) ||
      !bytes || bytes > limit - AGX_GPUVA_PAGE ||
      bytes > UINT64_MAX - (AGX_GPUVA_PAGE - 1)) return 0;
  length = (bytes + AGX_GPUVA_PAGE - 1) & ~(AGX_GPUVA_PAGE - 1);
  if (!space->Ops.Reserve(space->Context, length, AGX_GPUVA_PAGE, limit, &va))
    return 0;
  if (!va || (va & (AGX_GPUVA_PAGE - 1)) || va >= limit ||
      length > limit - va) {
    if (va && !space->Ops.Free(space->Context, va, length))
      space->Terminal = 1;
    return 0;
  }
  mapped = space->Ops.Map(space->Context, allocation, va, length >> 12,
                          protection, &fence);
  if (mapped == 3) {
    /* Callback accepted a mapping but its returned ownership is uncertain. */
    space->Terminal = 1;
    return 0;
  }
  if (!mapped) {
    if (!space->Ops.Free(space->Context, va, length)) space->Terminal = 1;
    return 0;
  }
  if ((mapped == 2 && !fence) ||
      (fence && !space->Ops.WaitPaging(space->Context, fence))) {
    /* A pending page-table update has uncertain ownership: do not free it. */
    space->Terminal = 1;
    return 0;
  }
  bo->Allocation = allocation;
  bo->Va = va;
  bo->Bytes = length;
  bo->Bound = 1;
  return 1;
}

int AgxWin32GpuvaUnbind(AGX_WIN32_GPUVA_SPACE *space,
                        AGX_WIN32_GPUVA_BO *bo) {
  if (!space || !bo || !bo->Bound || space->Terminal || space->Held ||
      !space->Ops.Free(space->Context, bo->Va, bo->Bytes)) return 0;
  memset(bo, 0, sizeof(*bo));
  return 1;
}

int AgxWin32GpuvaSubmit(AGX_WIN32_GPUVA_SPACE *space,
                        const AGX_WIN32_GPUVA_BO *const *references,
                        unsigned count, const AGX_WIN32_GPUVA_BO *command,
                        uint32_t command_bytes,
                        const AGX_WIN32_GPUVA_BO *const *written,
                        unsigned written_count, const void *private_data,
                        uint32_t private_bytes, uint64_t *completion) {
  uint64_t *handles, *written_handles = NULL, paging_fence = 0, render_fence = 0;
  int resident;
  unsigned found_command = 0;
  if (completion) *completion = 0;
  if (!space || !references || !count ||
      !command || !command->Bound || !command_bytes ||
      command_bytes > command->Bytes || !private_data || !private_bytes ||
      !completion || (written_count && !written) || written_count > count ||
      space->Terminal || space->Held) return 0;
  handles = malloc((size_t)count * sizeof(*handles));
  if (!handles) return 0;
  for (unsigned i = 0; i < count; ++i) {
    const AGX_WIN32_GPUVA_BO *bo = references[i];
    if (!bo || !bo->Bound || !bo->Allocation || !bo->Va ||
        bo->Va >= AGX_GPUVA_LIMIT || bo->Bytes > AGX_GPUVA_LIMIT - bo->Va) {
      free(handles);
      return 0;
    }
    for (unsigned j = 0; j < i; ++j) {
      if (handles[j] == bo->Allocation) { free(handles); return 0; }
    }
    handles[i] = bo->Allocation;
    if (bo == command) found_command = 1;
  }
  if (!found_command) { free(handles); return 0; }
  if (written_count) {
    written_handles = malloc((size_t)written_count * sizeof(*written_handles));
    if (!written_handles) { free(handles); return 0; }
    for (unsigned i = 0; i < written_count; ++i) {
      const AGX_WIN32_GPUVA_BO *bo = written[i];
      unsigned found = 0;
      if (!bo || !bo->Bound) { free(written_handles); free(handles); return 0; }
      for (unsigned j = 0; j < count; ++j)
        if (references[j] == bo) found = 1;
      for (unsigned j = 0; j < i; ++j)
        if (written_handles[j] == bo->Allocation) found = 0;
      if (!found) { free(written_handles); free(handles); return 0; }
      written_handles[i] = bo->Allocation;
    }
  }
  resident = space->Ops.MakeResident(space->Context, handles, count,
                                      &paging_fence);
  /* Submission uses the written tokens only during this call. */
  if (resident == 3) {
    space->Terminal = 1;
    space->Held = handles;
    space->HeldCount = count;
    free(written_handles);
    return 0;
  }
  if (!resident || (resident == 2 && !paging_fence)) {
    free(written_handles);
    free(handles);
    return 0;
  }
  if (paging_fence && !space->Ops.WaitPaging(space->Context, paging_fence)) {
    if (!space->Ops.Evict(space->Context, handles, count)) space->Terminal = 1;
    free(written_handles);
    free(handles);
    return 0;
  }
  int submitted = space->Ops.Submit(space->Context, written_handles,
      written_count, command->Va,
      command_bytes, private_data, private_bytes, &render_fence);
  free(written_handles);
  if (submitted == 2) {
    space->Terminal = 1;
    space->Held = handles;
    space->HeldCount = count;
    return 0;
  }
  if (!submitted || !render_fence) {
    if (!space->Ops.Evict(space->Context, handles, count)) space->Terminal = 1;
    free(handles);
    return 0;
  }
  space->Held = handles;
  space->HeldCount = count;
  space->RenderFence = render_fence;
  *completion = render_fence;
  return 1;
}

int AgxWin32GpuvaRetire(AGX_WIN32_GPUVA_SPACE *space, uint64_t completion) {
  if (!space || !space->Held || !completion ||
      completion != space->RenderFence || space->Terminal) return 0;
  if (!space->Ops.WaitRender(space->Context, completion)) return 0;
  if (!space->Ops.Evict(space->Context, space->Held, space->HeldCount)) {
    space->Terminal = 1;
    return 0;
  }
  free(space->Held);
  space->Held = NULL;
  space->HeldCount = 0;
  space->RenderFence = 0;
  return 1;
}
