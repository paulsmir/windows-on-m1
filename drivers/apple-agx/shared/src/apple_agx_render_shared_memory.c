#include "apple_agx_render_shared_memory.h"
#include "apple_agx_exp208_adapter.h"
#include "apple_agx_relocation.h"

#define RENDER_SHARED_NULL ((void *)0)
#define RENDER_SHARED_VA_ALIGNMENT 0x8000ULL
#define RENDER_SHARED_PAGE_MASK (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)
#define RENDER_SHARED_NATIVE_SHARED_BASE 0xffffffa040000000ULL
#define RENDER_SHARED_NATIVE_SHARED_END 0xffffffa060000000ULL
#define RENDER_SHARED_NATIVE_TIMESTAMP_BASE 0xffffffa071000000ULL
#define RENDER_SHARED_NATIVE_TIMESTAMP_END 0xffffffa075000000ULL
#define RENDER_SHARED_NATIVE_COMMAND_BASE 0xffffffa020000000ULL
#define RENDER_SHARED_NATIVE_COMMAND_END 0xffffffa040000000ULL

static APPLE_AGX_U64 align_up(APPLE_AGX_U64 Value,
                              APPLE_AGX_U64 Alignment) {
  return (Value + Alignment - 1ULL) & ~(Alignment - 1ULL);
}

static void zero_bytes(void *Address, APPLE_AGX_U64 Bytes) {
  APPLE_AGX_U64 index;
  for (index = 0ULL; index < Bytes; ++index)
    ((unsigned char *)Address)[index] = 0u;
}

static void put_u32(unsigned char *Address, APPLE_AGX_U32 Value) {
  Address[0] = (unsigned char)(Value & 0xffu);
  Address[1] = (unsigned char)((Value >> 8u) & 0xffu);
  Address[2] = (unsigned char)((Value >> 16u) & 0xffu);
  Address[3] = (unsigned char)((Value >> 24u) & 0xffu);
}

static void put_u64(unsigned char *Address, APPLE_AGX_U64 Value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    Address[index] = (unsigned char)((Value >> (index * 8u)) & 0xffu);
}

static APPLE_AGX_BOOL runtime_bindings_valid(
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *Bindings) {
  if (Bindings == RENDER_SHARED_NULL ||
      Bindings->StatsTaOwnerGpuAddress == 0ULL ||
      Bindings->Stats3dOwnerGpuAddress == 0ULL ||
      (Bindings->StatsTaOwnerGpuAddress &
       (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL ||
      (Bindings->Stats3dOwnerGpuAddress &
       (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL ||
      Bindings->StatsTaOwnerBytes < J313_AGX_G2_REGIONB_STATS_TA_SIZE ||
      Bindings->Stats3dOwnerBytes < J313_AGX_G2_REGIONB_STATS_3D_SIZE ||
      Bindings->StatsTaOwnerGpuAddress >
          ~0ULL - Bindings->StatsTaOwnerBytes ||
      Bindings->Stats3dOwnerGpuAddress >
          ~0ULL - Bindings->Stats3dOwnerBytes ||
      APPLE_AGX_RENDER_STATS_TA_FIELD_OFFSET >=
          Bindings->StatsTaOwnerBytes ||
      APPLE_AGX_RENDER_STATS_3D_FIELD_OFFSET >=
          Bindings->Stats3dOwnerBytes)
    return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
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
    Owner->ObjectOffsets[index] = 0ULL;
    --Owner->ObjectCount;
  }
  Owner->MemoryIo = RENDER_SHARED_NULL;
  Owner->Initialized = APPLE_AGX_FALSE;
  Owner->Built = APPLE_AGX_FALSE;
  Owner->ClassArenasApplied = APPLE_AGX_FALSE;
  Owner->LastResult = AppleAgxRenderSharedMemoryResultOk;
  return Owner->LastResult;
}

static APPLE_AGX_BOOL arena_valid(APPLE_AGX_U64 Va, APPLE_AGX_U64 Bytes,
                                  APPLE_AGX_U64 BandBase,
                                  APPLE_AGX_U64 BandEnd) {
  return Bytes != 0ULL &&
         (Va & (RENDER_SHARED_VA_ALIGNMENT - 1ULL)) == 0ULL &&
         (Bytes & (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) == 0ULL &&
         Va >= BandBase && Va < BandEnd && Bytes <= BandEnd - Va;
}

static APPLE_AGX_BOOL place_class(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *Layouts,
    APPLE_AGX_U64 NativeBase, APPLE_AGX_U64 NativeEnd,
    APPLE_AGX_U64 ArenaVa, APPLE_AGX_U64 ArenaBytes,
    APPLE_AGX_U64 *Proposed) {
  APPLE_AGX_U64 bias;
  APPLE_AGX_U32 index;
  if (ArenaVa <= NativeBase)
    return APPLE_AGX_FALSE;
  bias = ArenaVa - NativeBase;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    APPLE_AGX_U64 original = Layouts[index].OriginalGpuVa;
    APPLE_AGX_U64 gpu_va;
    APPLE_AGX_U64 mapping_va;
    APPLE_AGX_U64 bytes;
    if (original < NativeBase || original >= NativeEnd)
      continue;
    bytes = Owner->Objects[index].Length;
    if (original > ~0ULL - bias)
      return APPLE_AGX_FALSE;
    gpu_va = original + bias;
    mapping_va = gpu_va & ~RENDER_SHARED_PAGE_MASK;
    if (bytes == 0ULL || mapping_va < ArenaVa ||
        mapping_va > ArenaVa + ArenaBytes ||
        bytes > ArenaVa + ArenaBytes - mapping_va)
      return APPLE_AGX_FALSE;
    Proposed[index] = mapping_va;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT
AppleAgxRenderSharedMemoryApplyClassArenas(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 SharedVa, APPLE_AGX_U64 SharedBytes,
    APPLE_AGX_U64 TimestampVa, APPLE_AGX_U64 TimestampBytes) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U64 proposed[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U32 index;
  if (Owner == RENDER_SHARED_NULL || !Owner->Initialized || !Owner->Built ||
      Owner->ClassArenasApplied ||
      Owner->ObjectCount != APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT ||
      !arena_valid(SharedVa, SharedBytes, RENDER_SHARED_NATIVE_SHARED_BASE,
                   RENDER_SHARED_NATIVE_SHARED_END) ||
      !arena_valid(TimestampVa, TimestampBytes,
                   RENDER_SHARED_NATIVE_TIMESTAMP_BASE,
                   RENDER_SHARED_NATIVE_TIMESTAMP_END) ||
      SharedVa > ~0ULL - SharedBytes ||
      TimestampVa > ~0ULL - TimestampBytes ||
      (SharedVa < TimestampVa + TimestampBytes &&
       TimestampVa < SharedVa + SharedBytes))
    return AppleAgxRenderSharedMemoryResultInvalidArgument;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index)
    proposed[index] = Owner->VirtualAddresses[index];
  if (!place_class(Owner, layouts, RENDER_SHARED_NATIVE_SHARED_BASE,
                   RENDER_SHARED_NATIVE_SHARED_END, SharedVa, SharedBytes,
                   proposed) ||
      !place_class(Owner, layouts, RENDER_SHARED_NATIVE_TIMESTAMP_BASE,
                   RENDER_SHARED_NATIVE_TIMESTAMP_END, TimestampVa,
                   TimestampBytes, proposed))
    return AppleAgxRenderSharedMemoryResultInvalidArgument;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    APPLE_AGX_U32 previous;
    for (previous = 0u; previous < index; ++previous) {
      APPLE_AGX_U64 a = proposed[index];
      APPLE_AGX_U64 b = proposed[previous];
      APPLE_AGX_U64 a_bytes = Owner->Objects[index].Length;
      APPLE_AGX_U64 b_bytes = Owner->Objects[previous].Length;
      if (a < b + b_bytes && b < a + a_bytes)
        return AppleAgxRenderSharedMemoryResultInvalidArgument;
    }
  }
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index)
    Owner->VirtualAddresses[index] = proposed[index];
  Owner->ClassArenasApplied = APPLE_AGX_TRUE;
  Owner->LastResult = AppleAgxRenderSharedMemoryResultOk;
  return Owner->LastResult;
}

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT
AppleAgxRenderSharedMemoryApplyCommandArena(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 CommandVa, APPLE_AGX_U64 CommandBytes) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U64 proposed[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U32 index;
  if (Owner == RENDER_SHARED_NULL || !Owner->Initialized || !Owner->Built ||
      Owner->ClassArenasApplied ||
      Owner->ObjectCount != APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT ||
      !arena_valid(CommandVa, CommandBytes, RENDER_SHARED_NATIVE_COMMAND_BASE,
                   RENDER_SHARED_NATIVE_COMMAND_END) ||
      CommandVa > ~0ULL - CommandBytes)
    return AppleAgxRenderSharedMemoryResultInvalidArgument;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index)
    proposed[index] = Owner->VirtualAddresses[index];
  if (!place_class(Owner, layouts, RENDER_SHARED_NATIVE_COMMAND_BASE,
                   RENDER_SHARED_NATIVE_COMMAND_END, CommandVa, CommandBytes,
                   proposed))
    return AppleAgxRenderSharedMemoryResultInvalidArgument;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index)
    Owner->VirtualAddresses[index] = proposed[index];
  Owner->ClassArenasApplied = APPLE_AGX_TRUE;
  Owner->LastResult = AppleAgxRenderSharedMemoryResultOk;
  return Owner->LastResult;
}

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT
AppleAgxRenderSharedMemoryApplyQueueArenas(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 CommandVa, APPLE_AGX_U64 CommandBytes,
    APPLE_AGX_U64 SharedVa, APPLE_AGX_U64 SharedBytes) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U64 proposed[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U64 bias;
  APPLE_AGX_U32 index;
  if (Owner == RENDER_SHARED_NULL || !Owner->Initialized || !Owner->Built ||
      Owner->ClassArenasApplied ||
      Owner->ObjectCount != APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT ||
      !arena_valid(CommandVa, CommandBytes, RENDER_SHARED_NATIVE_COMMAND_BASE,
                   RENDER_SHARED_NATIVE_COMMAND_END) ||
      !arena_valid(SharedVa, SharedBytes, RENDER_SHARED_NATIVE_SHARED_BASE,
                   RENDER_SHARED_NATIVE_SHARED_END) ||
      CommandVa > ~0ULL - CommandBytes || SharedVa > ~0ULL - SharedBytes ||
      SharedVa <= RENDER_SHARED_NATIVE_SHARED_BASE)
    return AppleAgxRenderSharedMemoryResultInvalidArgument;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index)
    proposed[index] = Owner->VirtualAddresses[index];
  if (!place_class(Owner, layouts, RENDER_SHARED_NATIVE_COMMAND_BASE,
                   RENDER_SHARED_NATIVE_COMMAND_END, CommandVa, CommandBytes,
                   proposed))
    return AppleAgxRenderSharedMemoryResultInvalidArgument;
  bias = SharedVa - RENDER_SHARED_NATIVE_SHARED_BASE;
  for (index = 23u; index <= 27u; ++index) {
    APPLE_AGX_U64 gpu_va, mapping_va;
    if (layouts[index].OriginalGpuVa > ~0ULL - bias)
      return AppleAgxRenderSharedMemoryResultInvalidArgument;
    gpu_va = layouts[index].OriginalGpuVa + bias;
    mapping_va = gpu_va & ~RENDER_SHARED_PAGE_MASK;
    if (mapping_va < SharedVa || mapping_va > SharedVa + SharedBytes ||
        Owner->Objects[index].Length > SharedVa + SharedBytes - mapping_va)
      return AppleAgxRenderSharedMemoryResultInvalidArgument;
    proposed[index] = mapping_va;
  }
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index)
    Owner->VirtualAddresses[index] = proposed[index];
  Owner->ClassArenasApplied = APPLE_AGX_TRUE;
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
    APPLE_AGX_U64 object_offset;
    if (layouts[index].OriginalIndex != index || layouts[index].ContextId != 0u ||
        layouts[index].Size == 0u)
      return rollback(Owner,
                      AppleAgxRenderSharedMemoryResultInvalidArgument);
    object_offset = layouts[index].OriginalGpuVa & RENDER_SHARED_PAGE_MASK;
    if (object_offset > APPLE_AGX_MEMORY_PAGE_SIZE ||
        layouts[index].Size > APPLE_AGX_MEMORY_PAGE_SIZE - object_offset)
      return rollback(Owner,
                      AppleAgxRenderSharedMemoryResultInvalidArgument);
    allocation_bytes = align_up(
        object_offset + (APPLE_AGX_U64)layouts[index].Size,
        APPLE_AGX_MEMORY_PAGE_SIZE);
    if (AppleAgxMemoryAllocate(MemoryIo, allocation_bytes,
                               &Owner->Objects[index]) !=
        AppleAgxMemoryResultOk)
      return rollback(Owner,
                      AppleAgxRenderSharedMemoryResultAllocationFailed);
    ++Owner->ObjectCount;
    zero_bytes(Owner->Objects[index].CpuAddress, allocation_bytes);
    Owner->VirtualAddresses[index] = virtual_address;
    Owner->ObjectOffsets[index] = object_offset;
    virtual_address = align_up(
        virtual_address + allocation_bytes + APPLE_AGX_MEMORY_PAGE_SIZE,
        RENDER_SHARED_VA_ALIGNMENT);
  }
  Owner->Built = APPLE_AGX_TRUE;
  Owner->LastResult = AppleAgxRenderSharedMemoryResultOk;
  return Owner->LastResult;
}

static APPLE_AGX_BOOL per_submission_object(APPLE_AGX_U32 Index,
                                            APPLE_AGX_BOOL IncludeInitBm) {
  switch (Index) {
    case 9u:
    case 10u:
    case 12u:
    case 14u:
    case 15u:
    case 17u:
    case 18u:
    case 19u:
    case 26u:
    case 27u:
      return APPLE_AGX_TRUE;
    case 16u:
      return IncludeInitBm;
    default:
      return APPLE_AGX_FALSE;
  }
}

static APPLE_AGX_BOOL bind_relocation_objects(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity,
    APPLE_AGX_BOOL InitializePersistent,
    APPLE_AGX_BOOL IncludeInitBm) {
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
        Owner->ObjectOffsets[index];
    if (layouts[index].ArenaOffset > TemplateArenaBytes ||
        layouts[index].Size > TemplateArenaBytes - layouts[index].ArenaOffset)
      return APPLE_AGX_FALSE;
    if (InitializePersistent || per_submission_object(index, IncludeInitBm)) {
      for (byte = 0u; byte < layouts[index].Size; ++byte)
        destination[byte] = source[byte];
    }
    RelocationObjects[index].GpuVa =
        Owner->VirtualAddresses[index] + Owner->ObjectOffsets[index];
    RelocationObjects[index].PhysicalAddress =
        Owner->Objects[index].DeviceAddress + Owner->ObjectOffsets[index];
    RelocationObjects[index].Size = layouts[index].Size;
    RelocationObjects[index].Data = destination;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBindRelocationObjects(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity) {
  return bind_relocation_objects(
      Owner, TemplateArena, TemplateArenaBytes, RelocationObjects,
      RelocationObjectCapacity, APPLE_AGX_TRUE, APPLE_AGX_TRUE);
}

static APPLE_AGX_BOOL apply_active_relocations(
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_U32 ObjectCount, APPLE_AGX_BOOL InitializePersistent,
    APPLE_AGX_BOOL IncludeInitBm) {
  const APPLE_AGX_EXP208_RELOCATION *relocations =
      AppleAgxRenderTemplateRelocations();
  APPLE_AGX_U32 relocation_count = AppleAgxRenderTemplateRelocationCount();
  APPLE_AGX_U32 index;
  if (relocations == RENDER_SHARED_NULL || relocation_count == 0u)
    return APPLE_AGX_FALSE;
  if (InitializePersistent)
    return AppleAgxApplyRelocations(
        ActiveObjects, ObjectCount, relocations, relocation_count);
  for (index = 0u; index < relocation_count; ++index) {
    if (per_submission_object(relocations[index].SourceObject,
                              IncludeInitBm) &&
        !AppleAgxApplyRelocations(
            ActiveObjects, ObjectCount, &relocations[index], 1u))
      return APPLE_AGX_FALSE;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveJob(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob) {
  APPLE_AGX_BACKEND_JOB_IMAGE candidate;
  APPLE_AGX_U32 index;
  APPLE_AGX_BOOL initialize_persistent;
  if (SourceObjects == RENDER_SHARED_NULL ||
      SourceObjectCount != APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      StagedJob == RENDER_SHARED_NULL || ActiveObjects == RENDER_SHARED_NULL ||
      ActiveJob == RENDER_SHARED_NULL || ArenaGpuAddress == 0ULL ||
      (ArenaGpuAddress & (APPLE_AGX_EXP208_ARENA_ALIGNMENT - 1u)) != 0ULL ||
      TemplateArenaBytes != APPLE_AGX_EXP208_ARENA_BYTES ||
      StagedJob->TaWorkAddressCount != APPLE_AGX_EXP208_ROOT_COUNT ||
      StagedJob->D3WorkAddressCount != APPLE_AGX_EXP208_ROOT_COUNT ||
      StagedJob->TaEvent >= 128u || StagedJob->D3Event >= 128u ||
      StagedJob->TaEvent == StagedJob->D3Event ||
      StagedJob->TaExpectedStamp == 0u ||
      StagedJob->D3ExpectedStamp == 0u ||
      StagedJob->TaExpectedDonePointer >= APPLE_AGX_EXP208_QUEUE_CAPACITY ||
      StagedJob->D3ExpectedDonePointer >= APPLE_AGX_EXP208_QUEUE_CAPACITY ||
      !runtime_bindings_valid(RuntimeBindings))
    return APPLE_AGX_FALSE;
  for (index = 0u; index < SourceObjectCount; ++index)
    ActiveObjects[index] = SourceObjects[index];
  initialize_persistent = IncludeInitBm;
  if (!bind_relocation_objects(
          Owner, TemplateArena, TemplateArenaBytes, ActiveObjects,
          SourceObjectCount, initialize_persistent, IncludeInitBm))
    return APPLE_AGX_FALSE;
  if (IncludeInitBm) {
    APPLE_AGX_EXP208_RELOCATION_OBJECT *control = &ActiveObjects[20];
    APPLE_AGX_U32 total;
    if (control->Data == RENDER_SHARED_NULL || control->Size < 8u)
      return APPLE_AGX_FALSE;
    total = (APPLE_AGX_U32)control->Data[0] |
            ((APPLE_AGX_U32)control->Data[1] << 8u) |
            ((APPLE_AGX_U32)control->Data[2] << 16u) |
            ((APPLE_AGX_U32)control->Data[3] << 24u);
    if (total == 0u)
      return APPLE_AGX_FALSE;
    put_u32(control->Data + 4u, total);
  }
  if (!apply_active_relocations(
          ActiveObjects, SourceObjectCount, initialize_persistent,
          IncludeInitBm) ||
      ActiveObjects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa == 0ULL ||
      ActiveObjects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa == 0ULL ||
      ActiveObjects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa == 0ULL ||
      ActiveObjects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa == 0ULL)
    return APPLE_AGX_FALSE;
  if (ActiveObjects[17u].Data == RENDER_SHARED_NULL ||
      ActiveObjects[17u].Size < 548u ||
      ActiveObjects[15u].Data == RENDER_SHARED_NULL ||
      ActiveObjects[15u].Size < 612u)
    return APPLE_AGX_FALSE;
  put_u64(ActiveObjects[17u].Data + 36u,
          RuntimeBindings->StatsTaOwnerGpuAddress +
              APPLE_AGX_RENDER_STATS_TA_FIELD_OFFSET);
  put_u64(ActiveObjects[17u].Data + 540u,
          RuntimeBindings->StatsTaOwnerGpuAddress +
              APPLE_AGX_RENDER_STATS_TA_FIELD_OFFSET);
  put_u64(ActiveObjects[15u].Data + 28u,
          RuntimeBindings->Stats3dOwnerGpuAddress +
              APPLE_AGX_RENDER_STATS_3D_FIELD_OFFSET);
  put_u64(ActiveObjects[15u].Data + 604u,
          RuntimeBindings->Stats3dOwnerGpuAddress +
              APPLE_AGX_RENDER_STATS_3D_FIELD_OFFSET);
  candidate = *StagedJob;
  candidate.TaWorkAddresses[0] =
      ActiveObjects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa;
  candidate.TaWorkAddresses[1] =
      ActiveObjects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa;
  candidate.D3WorkAddresses[0] =
      ActiveObjects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa;
  candidate.D3WorkAddresses[1] =
      ActiveObjects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa;
  *ActiveJob = candidate;
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL queue_object_valid(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U32 Index, APPLE_AGX_U64 MinimumBytes, APPLE_AGX_BOOL Prepared) {
  return Index < Owner->ObjectCount && Owner->VirtualAddresses[Index] != 0ULL &&
                 Owner->Objects[Index].CpuAddress != RENDER_SHARED_NULL &&
                 Owner->ObjectOffsets[Index] <= Owner->Objects[Index].Length &&
                 MinimumBytes <=
                     Owner->Objects[Index].Length - Owner->ObjectOffsets[Index] &&
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
  Config->Ta.QueueInfoGpuAddress = Owner->VirtualAddresses[ta_queue_info] +
                                   Owner->ObjectOffsets[ta_queue_info];
  Config->Ta.RingCpuAddress =
      (APPLE_AGX_U64 *)((unsigned char *)Owner->Objects[ta_ring].CpuAddress +
                        Owner->ObjectOffsets[ta_ring]);
  Config->Ta.RingCapacity = APPLE_AGX_G13_RING_CAPACITY;
  Config->Ta.GpuDonePointer =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[ta_pointers].CpuAddress +
          Owner->ObjectOffsets[ta_pointers]);
  Config->Ta.CpuWritePointer = (volatile APPLE_AGX_U32 *)(
      (unsigned char *)Owner->Objects[ta_pointers].CpuAddress +
      Owner->ObjectOffsets[ta_pointers] + 0x40u);
  Config->Ta.Stamp =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[ta_stamp].CpuAddress + Owner->ObjectOffsets[ta_stamp]);
  Config->Ta.EventNumber = 0u;

  Config->D3.QueueType = (APPLE_AGX_U32)AppleAgxG13Queue3d;
  Config->D3.QueueInfoGpuAddress = Owner->VirtualAddresses[d3_queue_info] +
                                   Owner->ObjectOffsets[d3_queue_info];
  Config->D3.RingCpuAddress =
      (APPLE_AGX_U64 *)((unsigned char *)Owner->Objects[d3_ring].CpuAddress +
                        Owner->ObjectOffsets[d3_ring]);
  Config->D3.RingCapacity = APPLE_AGX_G13_RING_CAPACITY;
  Config->D3.GpuDonePointer =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[d3_pointers].CpuAddress +
          Owner->ObjectOffsets[d3_pointers]);
  Config->D3.CpuWritePointer = (volatile APPLE_AGX_U32 *)(
      (unsigned char *)Owner->Objects[d3_pointers].CpuAddress +
      Owner->ObjectOffsets[d3_pointers] + 0x40u);
  Config->D3.Stamp =
      (volatile APPLE_AGX_U32 *)((unsigned char *)
          Owner->Objects[d3_stamp].CpuAddress + Owner->ObjectOffsets[d3_stamp]);
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
