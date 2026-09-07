#include "apple_agx_render_shared_memory.h"
#include "apple_agx_exp208_adapter.h"
#include "apple_agx_relocation.h"
#include "apple_agx_render_template_rebase.h"

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
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      source_objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      active_objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_BACKEND_JOB_IMAGE staged_job;
  APPLE_AGX_BACKEND_JOB_IMAGE active_job;
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
  assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(
      arena, APPLE_AGX_EXP208_ARENA_BYTES, 0x9d5000000ULL,
      0x1500800000ULL, 0x1500000000ULL, 0x01000000ULL,
      source_objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      &roots));
  assert(AppleAgxApplyRelocations(
      source_objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),
      AppleAgxRenderTemplateRelocationCount()));

  assert(AppleAgxRenderSharedMemoryBuild(
             &owner, &io, 0xffffffa001000000ULL) ==
         AppleAgxRenderSharedMemoryResultOk);
  assert(owner.ObjectCount == APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT);
  assert((owner.VirtualAddresses[0] & 0x7fffULL) == 0ULL);
  assert((owner.VirtualAddresses[35] & 0x7fffULL) == 0ULL);
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
  assert(queue_config.D3.QueueInfoGpuAddress == owner.VirtualAddresses[3]);
  assert(queue_config.D3.RingCpuAddress ==
         (APPLE_AGX_U64 *)owner.Objects[4].CpuAddress);
  assert(queue_config.D3.GpuDonePointer ==
         (volatile APPLE_AGX_U32 *)owner.Objects[24].CpuAddress);
  assert(queue_config.D3.CpuWritePointer ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[24].CpuAddress + 0x40u));
  assert(queue_config.D3.Stamp ==
         (volatile APPLE_AGX_U32 *)owner.Objects[27].CpuAddress);
  assert(queue_config.Ta.QueueInfoGpuAddress == owner.VirtualAddresses[6]);
  assert(queue_config.Ta.RingCpuAddress ==
         (APPLE_AGX_U64 *)owner.Objects[7].CpuAddress);
  assert(queue_config.Ta.GpuDonePointer ==
         (volatile APPLE_AGX_U32 *)owner.Objects[25].CpuAddress);
  assert(queue_config.Ta.CpuWritePointer ==
         (volatile APPLE_AGX_U32 *)((unsigned char *)
             owner.Objects[25].CpuAddress + 0x40u));
  assert(queue_config.Ta.Stamp ==
         (volatile APPLE_AGX_U32 *)owner.Objects[26].CpuAddress);
  assert(queue_config.TimeoutTicks == 500u);
  layouts = AppleAgxRenderTemplateObjectLayouts();
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    assert(objects[index].GpuVa == owner.VirtualAddresses[index]);
    assert(objects[index].PhysicalAddress == owner.Objects[index].DeviceAddress);
    assert(objects[index].Data == owner.Objects[index].CpuAddress);
    assert(objects[index].Size == layouts[index].Size);
    assert(memcmp(objects[index].Data,
                  arena + layouts[index].ArenaOffset,
                  layouts[index].Size) == 0);
  }

  memset(&staged_job, 0, sizeof(staged_job));
  staged_job.TaWorkAddresses[0] = source_objects[16].GpuVa;
  staged_job.TaWorkAddresses[1] = source_objects[19].GpuVa;
  staged_job.D3WorkAddresses[0] = source_objects[14].GpuVa;
  staged_job.D3WorkAddresses[1] = source_objects[18].GpuVa;
  staged_job.TaWorkAddressCount = 2u;
  staged_job.D3WorkAddressCount = 2u;
  staged_job.TaEvent = 0u;
  staged_job.D3Event = 1u;
  staged_job.TaExpectedStamp = 0x7a000100u;
  staged_job.D3ExpectedStamp = 0x3d000100u;
  staged_job.TaExpectedDonePointer = 2u;
  staged_job.D3ExpectedDonePointer = 2u;
  arena[layouts[0].ArenaOffset] = 0x5au;
  assert(AppleAgxRenderSharedMemoryBuildActiveJob(
      &owner, arena, APPLE_AGX_EXP208_ARENA_BYTES,
      source_objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      0x1500800000ULL, APPLE_AGX_TRUE, &staged_job, active_objects,
      &active_job));
  assert(active_job.TaWorkAddresses[0] == owner.VirtualAddresses[16]);
  assert(active_job.TaWorkAddresses[1] == owner.VirtualAddresses[19]);
  assert(active_job.D3WorkAddresses[0] == owner.VirtualAddresses[14]);
  assert(active_job.D3WorkAddresses[1] == owner.VirtualAddresses[18]);
  assert(active_job.TaWorkAddresses[0] != staged_job.TaWorkAddresses[0]);
  assert(((unsigned char *)owner.Objects[0].CpuAddress)[0] == 0x5au);
  assert(*(unsigned int *)(active_objects[20].Data + 4u) ==
         *(unsigned int *)active_objects[20].Data);
  assert(AppleAgxRenderSharedMemoryDestroy(&owner) ==
         AppleAgxRenderSharedMemoryResultOk);
  assert(fake.Freed == APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT);
  free(arena);
  return 0;
}
