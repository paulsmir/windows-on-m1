#include "apple_agx_render_shared_memory.h"
#include "apple_agx_exp208_adapter.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct _FAKE_ALLOCATION {
  void *Storage;
  APPLE_AGX_U64 DeviceAddress;
} FAKE_ALLOCATION;

typedef struct _FAKE_MEMORY {
  FAKE_ALLOCATION Allocations[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U32 Allocated;
  APPLE_AGX_U32 Freed;
} FAKE_MEMORY;

static unsigned char allocate_contiguous(void *Context,
                                         unsigned long long Bytes,
                                         void **CpuBase,
                                         unsigned long long *DeviceBase,
                                         void **Handle) {
  FAKE_MEMORY *fake = Context;
  FAKE_ALLOCATION *allocation = &fake->Allocations[fake->Allocated];
  allocation->Storage = aligned_alloc(APPLE_AGX_MEMORY_PAGE_SIZE, Bytes);
  assert(allocation->Storage != NULL);
  allocation->DeviceAddress = 0x20000000ULL + fake->Allocated * 0x20000ULL;
  *CpuBase = allocation->Storage;
  *DeviceBase = allocation->DeviceAddress;
  *Handle = allocation;
  ++fake->Allocated;
  return 1u;
}

static unsigned char free_contiguous(void *Context, void *Handle) {
  FAKE_MEMORY *fake = Context;
  FAKE_ALLOCATION *allocation = Handle;
  free(allocation->Storage);
  allocation->Storage = NULL;
  ++fake->Freed;
  return 1u;
}

int main(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER owner;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  unsigned char *arena;
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U32 index;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG queue_config;

  memset(&fake, 0, sizeof(fake));
  memset(&owner, 0, sizeof(owner));
  memset(objects, 0, sizeof(objects));
  memset(&io, 0, sizeof(io));
  io.Context = &fake;
  io.AllocateContiguous = allocate_contiguous;
  io.FreeContiguous = free_contiguous;
  arena = aligned_alloc(APPLE_AGX_MEMORY_PAGE_SIZE,
                        APPLE_AGX_EXP208_ARENA_BYTES);
  assert(arena != NULL);
  assert(AppleAgxRenderTemplateMaterialize(
      arena, APPLE_AGX_EXP208_ARENA_BYTES, &roots));

  assert(AppleAgxRenderSharedMemoryBuild(
             &owner, &io, 0xffffffa001000000ULL) ==
         AppleAgxRenderSharedMemoryResultOk);
  assert(owner.ObjectCount == APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT);
  layouts = AppleAgxRenderTemplateObjectLayouts();
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    assert(owner.ObjectAddresses[index] == layouts[index].OriginalGpuVa);
    assert(owner.DataOffsets[index] ==
           (layouts[index].OriginalGpuVa &
            (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)));
    assert(owner.VirtualAddresses[index] ==
           (layouts[index].OriginalGpuVa &
            ~(APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)));
  }
  assert(AppleAgxRenderSharedMemoryBindRelocationObjects(
      &owner, arena, APPLE_AGX_EXP208_ARENA_BYTES, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    assert(AppleAgxMemoryMarkPrepared(&owner.Objects[index]) ==
           AppleAgxMemoryResultOk);
    assert(AppleAgxMemoryMarkGpuMapped(&owner.Objects[index], 0u,
                                       owner.VirtualAddresses[index]) ==
           AppleAgxMemoryResultOk);
  }
  memset(&queue_config, 0, sizeof(queue_config));
  assert(AppleAgxRenderSharedMemoryBuildQueueConfig(
      &owner, 500u, &queue_config));
  assert(queue_config.D3.QueueInfoGpuAddress == owner.ObjectAddresses[3]);
  assert(queue_config.D3.RingCpuAddress ==
         (APPLE_AGX_U64 *)((unsigned char *)owner.Objects[4].CpuAddress +
                           owner.DataOffsets[4]));
  assert(queue_config.D3.GpuDonePointer ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[24].CpuAddress + owner.DataOffsets[24]));
  assert(queue_config.D3.CpuWritePointer ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[24].CpuAddress + owner.DataOffsets[24] + 0x40u));
  assert(queue_config.D3.Stamp ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[27].CpuAddress + owner.DataOffsets[27]));
  assert(queue_config.Ta.QueueInfoGpuAddress == owner.ObjectAddresses[6]);
  assert(queue_config.Ta.RingCpuAddress ==
         (APPLE_AGX_U64 *)((unsigned char *)owner.Objects[7].CpuAddress +
                           owner.DataOffsets[7]));
  assert(queue_config.Ta.GpuDonePointer ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[25].CpuAddress + owner.DataOffsets[25]));
  assert(queue_config.Ta.CpuWritePointer ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[25].CpuAddress + owner.DataOffsets[25] + 0x40u));
  assert(queue_config.Ta.Stamp ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[26].CpuAddress + owner.DataOffsets[26]));
  assert(queue_config.TimeoutTicks == 500u);
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    assert(objects[index].GpuVa == owner.ObjectAddresses[index]);
    assert(objects[index].PhysicalAddress ==
           owner.Objects[index].DeviceAddress + owner.DataOffsets[index]);
    assert(objects[index].Data ==
           (unsigned char *)owner.Objects[index].CpuAddress +
               owner.DataOffsets[index]);
    assert(objects[index].Size == layouts[index].Size);
    assert(memcmp(objects[index].Data,
                  arena + layouts[index].ArenaOffset,
                  layouts[index].Size) == 0);
  }
  assert(AppleAgxRenderSharedMemoryDestroy(&owner) ==
         AppleAgxRenderSharedMemoryResultOk);
  assert(fake.Freed == APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT);
  free(arena);
  return 0;
}
