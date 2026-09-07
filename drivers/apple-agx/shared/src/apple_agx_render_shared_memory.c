#include "apple_agx_render_shared_memory.h"

#define RENDER_SHARED_NULL ((void *)0)
#define RENDER_SHARED_VA_ALIGNMENT 0x8000ULL
#define RENDER_SHARED_PAGE_MASK (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)

static APPLE_AGX_U64 align_up(APPLE_AGX_U64 Value,
                              APPLE_AGX_U64 Alignment) {
  return (Value + Alignment - 1ULL) & ~(Alignment - 1ULL);
}

static void zero_bytes(void *Address, APPLE_AGX_U64 Bytes) {
  APPLE_AGX_U64 index;
  for (index = 0ULL; index < Bytes; ++index)
    ((unsigned char *)Address)[index] = 0u;
}

static APPLE_AGX_BOOL storage_empty(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    if (Owner->Objects[index].State != AppleAgxMemoryEmpty)
      return APPLE_AGX_FALSE;
  }
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL exact_queue_identity(APPLE_AGX_U32 Index) {
  return Index <= 13u || (Index >= 23u && Index <= 27u)
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT AppleAgxRenderSharedMemoryDestroy(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner) {
  if (Owner == RENDER_SHARED_NULL)
    return AppleAgxRenderSharedMemoryResultInvalidArgument;
  if (!Owner->Initialized)
    return AppleAgxRenderSharedMemoryResultOk;
  while (Owner->ObjectCount != 0u) {
    APPLE_AGX_U32 index = Owner->ObjectCount - 1u;
    APPLE_AGX_MEMORY_OBJECT *object = &Owner->Objects[index];
    if ((object->State == AppleAgxMemoryGpuMapped ||
         object->State == AppleAgxMemoryCompleted) &&
        AppleAgxMemoryMarkGpuUnmapped(object) != AppleAgxMemoryResultOk) {
      Owner->LastResult = AppleAgxRenderSharedMemoryResultReleaseFailed;
      return Owner->LastResult;
    }
    if (AppleAgxMemoryRelease(Owner->MemoryIo, object) !=
        AppleAgxMemoryResultOk) {
      Owner->LastResult = AppleAgxRenderSharedMemoryResultReleaseFailed;
      return Owner->LastResult;
    }
    Owner->VirtualAddresses[index] = 0ULL;
    Owner->ObjectAddresses[index] = 0ULL;
    Owner->DataOffsets[index] = 0u;
    --Owner->ObjectCount;
  }
  Owner->MemoryIo = RENDER_SHARED_NULL;
  Owner->Initialized = APPLE_AGX_FALSE;
  Owner->Built = APPLE_AGX_FALSE;
  Owner->LastResult = AppleAgxRenderSharedMemoryResultOk;
  return Owner->LastResult;
}

static APPLE_AGX_RENDER_SHARED_MEMORY_RESULT rollback(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_SHARED_MEMORY_RESULT Failure) {
  if (AppleAgxRenderSharedMemoryDestroy(Owner) !=
      AppleAgxRenderSharedMemoryResultOk)
    return AppleAgxRenderSharedMemoryResultReleaseFailed;
  Owner->LastResult = Failure;
  return Failure;
}

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT AppleAgxRenderSharedMemoryBuild(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    APPLE_AGX_U64 FirstVirtualAddress) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U64 virtual_address;
  APPLE_AGX_U32 index;
  if (Owner == RENDER_SHARED_NULL || MemoryIo == RENDER_SHARED_NULL ||
      MemoryIo->AllocateContiguous == RENDER_SHARED_NULL ||
      MemoryIo->FreeContiguous == RENDER_SHARED_NULL || Owner->Initialized ||
      Owner->Built || Owner->ObjectCount != 0u || !storage_empty(Owner) ||
      (FirstVirtualAddress & (RENDER_SHARED_VA_ALIGNMENT - 1ULL)) != 0ULL)
    return AppleAgxRenderSharedMemoryResultInvalidArgument;

  layouts = AppleAgxRenderTemplateObjectLayouts();
  Owner->MemoryIo = MemoryIo;
  Owner->Initialized = APPLE_AGX_TRUE;
  virtual_address = FirstVirtualAddress;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    APPLE_AGX_U64 allocation_bytes;
    APPLE_AGX_U64 data_offset;
    APPLE_AGX_U64 mapping_address;
    if (layouts[index].OriginalIndex != index || layouts[index].ContextId != 0u ||
        layouts[index].Size == 0u)
      return rollback(Owner,
                      AppleAgxRenderSharedMemoryResultInvalidArgument);
    if (exact_queue_identity(index)) {
      data_offset = layouts[index].OriginalGpuVa & RENDER_SHARED_PAGE_MASK;
      mapping_address =
          layouts[index].OriginalGpuVa & ~RENDER_SHARED_PAGE_MASK;
    } else {
      data_offset = 0ULL;
      mapping_address = virtual_address;
    }
    allocation_bytes = align_up(
        data_offset + (APPLE_AGX_U64)layouts[index].Size,
        APPLE_AGX_MEMORY_PAGE_SIZE);
    if (allocation_bytes == 0ULL ||
        data_offset > allocation_bytes ||
        layouts[index].Size > allocation_bytes - data_offset)
      return rollback(Owner,
                      AppleAgxRenderSharedMemoryResultInvalidArgument);
    if (AppleAgxMemoryAllocate(MemoryIo, allocation_bytes,
                               &Owner->Objects[index]) !=
        AppleAgxMemoryResultOk)
      return rollback(Owner,
                      AppleAgxRenderSharedMemoryResultAllocationFailed);
    ++Owner->ObjectCount;
    zero_bytes(Owner->Objects[index].CpuAddress, allocation_bytes);
    Owner->VirtualAddresses[index] = mapping_address;
    Owner->ObjectAddresses[index] = exact_queue_identity(index)
                                         ? layouts[index].OriginalGpuVa
                                         : mapping_address;
    Owner->DataOffsets[index] = (APPLE_AGX_U32)data_offset;
    if (!exact_queue_identity(index))
      virtual_address = align_up(
          mapping_address + allocation_bytes + APPLE_AGX_MEMORY_PAGE_SIZE,
          RENDER_SHARED_VA_ALIGNMENT);
  }
  Owner->Built = APPLE_AGX_TRUE;
  Owner->LastResult = AppleAgxRenderSharedMemoryResultOk;
  return Owner->LastResult;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBindRelocationObjects(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U32 index;
  if (Owner == RENDER_SHARED_NULL || !Owner->Initialized || !Owner->Built ||
      Owner->ObjectCount != APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT ||
      TemplateArena == RENDER_SHARED_NULL ||
      TemplateArenaBytes < AppleAgxRenderTemplateBytes() ||
      RelocationObjects == RENDER_SHARED_NULL ||
      RelocationObjectCapacity < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT)
    return APPLE_AGX_FALSE;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    APPLE_AGX_U32 byte;
    const unsigned char *source =
        (const unsigned char *)TemplateArena + layouts[index].ArenaOffset;
    unsigned char *destination =
        (unsigned char *)Owner->Objects[index].CpuAddress +
        Owner->DataOffsets[index];
    if (layouts[index].ArenaOffset > TemplateArenaBytes ||
        layouts[index].Size > TemplateArenaBytes - layouts[index].ArenaOffset)
      return APPLE_AGX_FALSE;
    for (byte = 0u; byte < layouts[index].Size; ++byte)
      destination[byte] = source[byte];
    RelocationObjects[index].GpuVa = Owner->ObjectAddresses[index];
    RelocationObjects[index].PhysicalAddress =
        Owner->Objects[index].DeviceAddress + Owner->DataOffsets[index];
    RelocationObjects[index].Size = layouts[index].Size;
    RelocationObjects[index].Data = destination;
  }
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL queue_object_valid(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U32 Index, APPLE_AGX_U64 MinimumBytes, APPLE_AGX_BOOL Prepared) {
  return Index < Owner->ObjectCount && Owner->VirtualAddresses[Index] != 0ULL &&
                 Owner->ObjectAddresses[Index] != 0ULL &&
                 Owner->Objects[Index].CpuAddress != RENDER_SHARED_NULL &&
                 Owner->DataOffsets[Index] <= Owner->Objects[Index].Length &&
                 MinimumBytes <=
                     Owner->Objects[Index].Length - Owner->DataOffsets[Index] &&
                 ((Prepared && Owner->Objects[Index].State == AppleAgxMemoryPrepared &&
                   Owner->Objects[Index].GpuVirtualAddress == 0ULL) ||
                  (!Prepared && Owner->Objects[Index].State == AppleAgxMemoryGpuMapped &&
                   Owner->Objects[Index].Context == 0u &&
                   Owner->Objects[Index].GpuVirtualAddress == Owner->VirtualAddresses[Index]))
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL queue_config(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 TimeoutTicks,
    APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config, APPLE_AGX_BOOL Prepared) {
  const APPLE_AGX_U32 d3_queue_info = 3u;
  const APPLE_AGX_U32 d3_ring = 4u;
  const APPLE_AGX_U32 ta_queue_info = 6u;
  const APPLE_AGX_U32 ta_ring = 7u;
  const APPLE_AGX_U32 d3_pointers = 24u;
  const APPLE_AGX_U32 ta_pointers = 25u;
  const APPLE_AGX_U32 ta_stamp = 26u;
  const APPLE_AGX_U32 d3_stamp = 27u;
  if (Owner == RENDER_SHARED_NULL || !Owner->Initialized || !Owner->Built ||
      Owner->ObjectCount != APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT ||
      Config == RENDER_SHARED_NULL || TimeoutTicks == 0ULL ||
      !queue_object_valid(Owner, d3_queue_info, 184u, Prepared) ||
      !queue_object_valid(Owner, d3_ring,
                          APPLE_AGX_G13_RING_CAPACITY * 8ULL, Prepared) ||
      !queue_object_valid(Owner, ta_queue_info, 184u, Prepared) ||
      !queue_object_valid(Owner, ta_ring,
                          APPLE_AGX_G13_RING_CAPACITY * 8ULL, Prepared) ||
      !queue_object_valid(Owner, d3_pointers, 0x44u, Prepared) ||
      !queue_object_valid(Owner, ta_pointers, 0x44u, Prepared) ||
      !queue_object_valid(Owner, ta_stamp, 4u, Prepared) ||
      !queue_object_valid(Owner, d3_stamp, 4u, Prepared))
    return APPLE_AGX_FALSE;

  zero_bytes(Config, (APPLE_AGX_U64)sizeof(*Config));
  Config->Ta.QueueType = (APPLE_AGX_U32)AppleAgxG13QueueTa;
  Config->Ta.QueueInfoGpuAddress = Owner->ObjectAddresses[ta_queue_info];
  Config->Ta.RingCpuAddress =
      (APPLE_AGX_U64 *)((unsigned char *)Owner->Objects[ta_ring].CpuAddress +
                        Owner->DataOffsets[ta_ring]);
  Config->Ta.RingCapacity = APPLE_AGX_G13_RING_CAPACITY;
  Config->Ta.GpuDonePointer =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[ta_pointers].CpuAddress +
          Owner->DataOffsets[ta_pointers]);
  Config->Ta.CpuWritePointer = (volatile APPLE_AGX_U32 *)(
      (unsigned char *)Owner->Objects[ta_pointers].CpuAddress +
      Owner->DataOffsets[ta_pointers] + 0x40u);
  Config->Ta.Stamp =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[ta_stamp].CpuAddress + Owner->DataOffsets[ta_stamp]);
  Config->Ta.EventNumber = 0u;

  Config->D3.QueueType = (APPLE_AGX_U32)AppleAgxG13Queue3d;
  Config->D3.QueueInfoGpuAddress = Owner->ObjectAddresses[d3_queue_info];
  Config->D3.RingCpuAddress =
      (APPLE_AGX_U64 *)((unsigned char *)Owner->Objects[d3_ring].CpuAddress +
                        Owner->DataOffsets[d3_ring]);
  Config->D3.RingCapacity = APPLE_AGX_G13_RING_CAPACITY;
  Config->D3.GpuDonePointer =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[d3_pointers].CpuAddress +
          Owner->DataOffsets[d3_pointers]);
  Config->D3.CpuWritePointer = (volatile APPLE_AGX_U32 *)(
      (unsigned char *)Owner->Objects[d3_pointers].CpuAddress +
      Owner->DataOffsets[d3_pointers] + 0x40u);
  Config->D3.Stamp =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[d3_stamp].CpuAddress + Owner->DataOffsets[d3_stamp]);
  Config->D3.EventNumber = 1u;
  Config->TimeoutTicks = TimeoutTicks;
  return APPLE_AGX_TRUE;
}


APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildQueueConfig(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner, APPLE_AGX_U64 TimeoutTicks,
    APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config) {
  return queue_config(Owner, TimeoutTicks, Config, APPLE_AGX_FALSE);
}
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryPrepareQueueConfig(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner, APPLE_AGX_U64 TimeoutTicks,
    APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config) {
  return queue_config(Owner, TimeoutTicks, Config, APPLE_AGX_TRUE);
}

#undef RENDER_SHARED_PAGE_MASK
#undef RENDER_SHARED_VA_ALIGNMENT
#undef RENDER_SHARED_NULL
