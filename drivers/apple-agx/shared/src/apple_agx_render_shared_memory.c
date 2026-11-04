#include "apple_agx_render_shared_memory.h"
#include "apple_agx_exp208_adapter.h"
#include "apple_agx_relocation.h"
#include <string.h>

#define RENDER_SHARED_NULL ((void *)0)
#define RENDER_SHARED_VA_ALIGNMENT 0x8000ULL
#define RENDER_SHARED_PAGE_MASK (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)
#define RENDER_SHARED_NATIVE_SHARED_BASE 0xffffffa040000000ULL
#define RENDER_SHARED_NATIVE_SHARED_END 0xffffffa060000000ULL
#define RENDER_SHARED_NATIVE_TIMESTAMP_BASE 0xffffffa071000000ULL
#define RENDER_SHARED_NATIVE_TIMESTAMP_END 0xffffffa075000000ULL
#define RENDER_SHARED_NATIVE_COMMAND_BASE 0xffffffa020000000ULL
#define RENDER_SHARED_NATIVE_COMMAND_END 0xffffffa040000000ULL


static APPLE_AGX_BOOL flights_idle(const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *);

static APPLE_AGX_BOOL queue_object_valid(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *,APPLE_AGX_U32,
    APPLE_AGX_U64,APPLE_AGX_BOOL);

static APPLE_AGX_U64 align_up(APPLE_AGX_U64 Value,
                              APPLE_AGX_U64 Alignment) {
  return (Value + Alignment - 1ULL) & ~(Alignment - 1ULL);
}

/* EXP1099: firmware-shared objects are mapped MmNonCached (Device memory on
 * ARM64): every store is its own transaction and unaligned wide accesses fault.
 * EXP1092 job timing put 340 us of every job between BackendBefore and the 3D
 * kick, consistent with ~12 KiB of per-submission template bytes copied one
 * byte at a time. Copy with naturally aligned 8-byte stores where source and
 * destination share their alignment; the resulting bytes are identical. The
 * volatile accesses keep the compiler from widening them or calling memcpy. */
static void copy_shared_bytes(unsigned char *Destination,
                              const unsigned char *Source, APPLE_AGX_U32 Bytes) {
  APPLE_AGX_U32 i = 0u;
  if ((((APPLE_AGX_U64)Destination ^ (APPLE_AGX_U64)Source) & 7ULL) == 0ULL) {
    for (; i < Bytes && ((APPLE_AGX_U64)(Destination + i) & 7ULL) != 0ULL; ++i)
      Destination[i] = Source[i];
    for (; Bytes - i >= 8u; i += 8u)
      *(volatile APPLE_AGX_U64 *)(Destination + i) =
          *(const volatile APPLE_AGX_U64 *)(Source + i);
  }
  for (; i < Bytes; ++i)
    Destination[i] = Source[i];
}

#if defined(APPLE_AGX_RENDER_SHARED_READ_PROBE)
APPLE_AGX_RENDER_SHARED_READ_PROBE_COUNTS AppleAgxRenderSharedReadProbe;
#define READ_PROBE(field, count) (AppleAgxRenderSharedReadProbe.field += (count))
/* The loads copy_shared_bytes issues from Source (the direct reads). */
static unsigned long copy_shared_loads(const unsigned char *Destination,
                                       const unsigned char *Source,
                                       APPLE_AGX_U32 Bytes) {
  unsigned long loads = 0u;
  APPLE_AGX_U32 i = 0u;
  if ((((APPLE_AGX_U64)Destination ^ (APPLE_AGX_U64)Source) & 7ULL) == 0ULL) {
    for (; i < Bytes && ((APPLE_AGX_U64)(Destination + i) & 7ULL) != 0ULL; ++i)
      ++loads;
    for (; Bytes - i >= 8u; i += 8u) ++loads;
  }
  return loads + (Bytes - i);
}
#else
#define READ_PROBE(field, count) ((void)0)
#endif

/* rc83: reads of firmware-shared (MmNonCached, Device) memory into cached
 * memory with naturally aligned loads of at most 8 bytes, each a single
 * transaction (EXP1099: no misaligned wide access). The bytes are those a
 * byte-wise copy reads; the destination's alignment does not matter. */
static void read_shared_bytes(unsigned char *Destination,
                              const unsigned char *Source, APPLE_AGX_U32 Bytes) {
  APPLE_AGX_U32 i = 0u, k, n;
  while (i < Bytes) {
    const APPLE_AGX_U64 at = (APPLE_AGX_U64)(Source + i);
    APPLE_AGX_U64 word;
    unsigned char bytes[8];
    if ((at & 7ULL) == 0ULL && Bytes - i >= 8u) {
      word = *(const volatile APPLE_AGX_U64 *)(Source + i); n = 8u;
    } else if ((at & 3ULL) == 0ULL && Bytes - i >= 4u) {
      word = *(const volatile APPLE_AGX_U32 *)(Source + i); n = 4u;
    } else {
      word = *(const volatile unsigned char *)(Source + i); n = 1u;
    }
    READ_PROBE(SharedLoads, 1u);
    /* the loaded value's bytes in memory order (any byte order) */
    if (n == 8u) {
      const APPLE_AGX_U64 value = word;
      const unsigned char *p = (const unsigned char *)&value;
      for (k = 0u; k < 8u; ++k) bytes[k] = p[k];
    } else if (n == 4u) {
      const APPLE_AGX_U32 value = (APPLE_AGX_U32)word;
      const unsigned char *p = (const unsigned char *)&value;
      for (k = 0u; k < 4u; ++k) bytes[k] = p[k];
    } else {
      bytes[0] = (unsigned char)word;
    }
    for (k = 0u; k < n; ++k) Destination[i + k] = bytes[k];
    i += n;
  }
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

static APPLE_AGX_U32 get_u32(const unsigned char *Address) {
  return (APPLE_AGX_U32)Address[0] | ((APPLE_AGX_U32)Address[1] << 8u) |
         ((APPLE_AGX_U32)Address[2] << 16u) |
         ((APPLE_AGX_U32)Address[3] << 24u);
}

/* rc83: a 32-bit field of firmware-shared memory; Direct = the pre-rc83
 * byte-wise read (four transactions). */
static APPLE_AGX_U32 get_shared_u32(const unsigned char *Address,
                                    APPLE_AGX_BOOL Direct) {
  unsigned char value[4];
  if (Direct) {
    READ_PROBE(SharedLoads, 4u);
    return get_u32(Address);
  }
  read_shared_bytes(value, Address, 4u);
  return get_u32(value);
}

static void put_u64(unsigned char *Address, APPLE_AGX_U64 Value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    Address[index] = (unsigned char)((Value >> (index * 8u)) & 0xffu);
}

/* G2: the TA microsequence (template object 17) holds a start (flag 1)
 * and an end Timestamp op at 0x18c/0x1cc: header, command_time,
 * ts_pointers, update_ts. Asahi sets update_ts to &ts_pointers->start_addr
 * and &ts_pointers->end_addr; ts_pointers is already relocated here. */
#define TA_TIMESTAMP_START_OP 0x18cu
#define TA_TIMESTAMP_END_OP 0x1ccu
static APPLE_AGX_U64 plain_u64(const unsigned char *Address) {
  APPLE_AGX_U64 value = 0u;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    value |= (APPLE_AGX_U64)Address[index] << (index * 8u);
  return value;
}

static APPLE_AGX_BOOL patch_ta_timestamp_update(unsigned char *Micro,
                                                APPLE_AGX_U64 Size) {
  APPLE_AGX_U64 start, end;
  if (Micro == RENDER_SHARED_NULL || Size < TA_TIMESTAMP_END_OP + 0x1cu ||
      (APPLE_AGX_U32)plain_u64(Micro + TA_TIMESTAMP_START_OP) != 0x80000019u ||
      (APPLE_AGX_U32)plain_u64(Micro + TA_TIMESTAMP_END_OP) != 0x00000019u)
    return APPLE_AGX_FALSE;
  start = plain_u64(Micro + TA_TIMESTAMP_START_OP + 0xcu);
  end = plain_u64(Micro + TA_TIMESTAMP_END_OP + 0xcu);
  if (!start || start != end) return APPLE_AGX_FALSE;
  put_u64(Micro + TA_TIMESTAMP_START_OP + 0x14u, start);
  put_u64(Micro + TA_TIMESTAMP_END_OP + 0x14u, end + 8u);
  return APPLE_AGX_TRUE;
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
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_TEMPLATE_OBJECT_COUNT;
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
  /* Default path unchanged. An opt-in resident snapshot cannot be lost
   * through teardown or a reset which rebuilds the memory owner. */
  if (!flights_idle(Owner)) return AppleAgxRenderSharedMemoryResultReleaseFailed;
  if (Owner->ManagerSaveOnEvict && !AppleAgxRenderManagerDrain(Owner))
    return AppleAgxRenderSharedMemoryResultReleaseFailed;
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
  {
    APPLE_AGX_U64 next=SharedVa;
    for(index=20u;index<=27u;++index) {
      APPLE_AGX_U64 end=proposed[index]+Owner->Objects[index].Length;
      if(end>next) next=end;
    }
    next=align_up(next,RENDER_SHARED_VA_ALIGNMENT);
    for(index=APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO;
        index<APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;++index) {
      APPLE_AGX_U64 bytes=Owner->Objects[index].Length;
      if(next<SharedVa || next>SharedVa+SharedBytes ||
         bytes>SharedVa+SharedBytes-next)
        return AppleAgxRenderSharedMemoryResultInvalidArgument;
      proposed[index]=next;
      next=align_up(next+bytes,RENDER_SHARED_VA_ALIGNMENT);
    }
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
    if(index<APPLE_AGX_RENDER_SHARED_MEMORY_TEMPLATE_OBJECT_COUNT) {
      if (layouts[index].OriginalIndex != index || layouts[index].ContextId != 0u ||
          layouts[index].Size == 0u)
        return rollback(Owner,AppleAgxRenderSharedMemoryResultInvalidArgument);
      object_offset = layouts[index].OriginalGpuVa & RENDER_SHARED_PAGE_MASK;
      if (object_offset > APPLE_AGX_MEMORY_PAGE_SIZE ||
          layouts[index].Size > APPLE_AGX_MEMORY_PAGE_SIZE - object_offset)
        return rollback(Owner,AppleAgxRenderSharedMemoryResultInvalidArgument);
      allocation_bytes=align_up(object_offset+(APPLE_AGX_U64)layouts[index].Size,
                                APPLE_AGX_MEMORY_PAGE_SIZE);
    } else {
      object_offset=0ULL;
      allocation_bytes=index==APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR ?
          0x10000ULL : APPLE_AGX_MEMORY_PAGE_SIZE;
    }
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

/* q2: no firmware stores at begin/build. Only the commit path publishes.
 * Counts in Owner are speculative until Visible; cancellation restores them. */
static APPLE_AGX_NOTIFIER_FLIGHT *building_flight(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o) {
  return o && o->NotifierFlight && o->BuildingFlight &&
      o->BuildingFlight <= APPLE_AGX_NOTIFIER_FLIGHT_COUNT ?
      &o->Flights[o->BuildingFlight-1u] : RENDER_SHARED_NULL;
}
static APPLE_AGX_BOOL reserve_sidecar(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o,
                                     APPLE_AGX_U32 set) {
  APPLE_AGX_U32 i;
  APPLE_AGX_NOTIFIER_FLIGHT *f=building_flight(o);
  if(!o->NotifierFlight) return APPLE_AGX_TRUE;
  if(!f || f->Published || set>=APPLE_AGX_COMPUTE_JOB_WORD_SET_COUNT ||
      (f->HasSidecar && f->SidecarSet!=set)) return APPLE_AGX_FALSE;
  for(i=0;i<APPLE_AGX_NOTIFIER_FLIGHT_COUNT;++i)
    if(&o->Flights[i]!=f && o->Flights[i].Fence &&
        o->Flights[i].HasSidecar && o->Flights[i].SidecarSet==set)
      return APPLE_AGX_FALSE;
  f->HasSidecar=APPLE_AGX_TRUE; f->SidecarSet=set;
  return APPLE_AGX_TRUE;
}
static unsigned char *notifier_threshold(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o) {
  return (unsigned char *)o->Objects[12].CpuAddress+o->ObjectOffsets[12];
}
static APPLE_AGX_BOOL flights_idle(const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o) {
  APPLE_AGX_U32 i;
  if (!o || !o->NotifierFlight) return APPLE_AGX_TRUE;
  for(i=0;i<APPLE_AGX_NOTIFIER_FLIGHT_COUNT;++i)
    if(o->Flights[i].Fence) return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}
APPLE_AGX_BOOL AppleAgxRenderFlightBegin(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o, APPLE_AGX_U32 fence) {
  APPLE_AGX_U32 i;
  APPLE_AGX_NOTIFIER_FLIGHT *f;
  if (!o) return APPLE_AGX_FALSE;
  if (!o->NotifierFlight) return APPLE_AGX_TRUE;
  if (!fence || o->BuildingFlight || !queue_object_valid(o,12,4,APPLE_AGX_FALSE) ||
      ((APPLE_AGX_U64)notifier_threshold(o) & 3ULL))
    return APPLE_AGX_FALSE;
  for(i=0;i<APPLE_AGX_NOTIFIER_FLIGHT_COUNT;++i)
    if(o->Flights[i].Fence==fence) return APPLE_AGX_FALSE;
  for(i=0;i<APPLE_AGX_NOTIFIER_FLIGHT_COUNT;++i) if(!o->Flights[i].Fence) break;
  if(i==APPLE_AGX_NOTIFIER_FLIGHT_COUNT) return APPLE_AGX_FALSE;
  f=&o->Flights[i]; zero_bytes(f,sizeof(*f));
  f->Fence=fence; f->PreviousThreshold=get_shared_u32(notifier_threshold(o),o->DirectReads);
  f->EventCount=f->PreviousThreshold;
  f->RenderCount=o->NotifierRenderCount; f->ComputeCount=o->NotifierComputeCount;
  f->ComputeStamp=o->NotifierComputeStamp; f->WordSet=o->JobWordSet; f->WordStamp=o->JobWordStamp;
  f->ComputeQueueStamp=o->ComputeQueueStamp;
  o->BuildingFlight=i+1u;
  return APPLE_AGX_TRUE;
}
APPLE_AGX_BOOL AppleAgxRenderFlightPublish(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o, APPLE_AGX_U32 fence) {
  APPLE_AGX_NOTIFIER_FLIGHT *f;
  if (!o) return APPLE_AGX_FALSE;
  if (!o->NotifierFlight) return APPLE_AGX_TRUE;
  f=building_flight(o);
  if(!f || f->Fence!=fence || f->Visible || (!f->EpochReset &&
      f->EventCount<f->RenderCount+f->ComputeCount))
    return APPLE_AGX_FALSE;
  f->Published=APPLE_AGX_TRUE; /* even a failed flush needs restoration */
  /* The live firmware reader must observe one complete count, including
   * at byte carries. Begin checked alignment; Windows is little-endian. */
  *(volatile APPLE_AGX_U32 *)notifier_threshold(o)=f->EventCount;
  return !o->NotifierFlush || o->NotifierFlush(o->NotifierFlushContext,notifier_threshold(o),4u);
}
APPLE_AGX_BOOL AppleAgxRenderFlightCancel(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o, APPLE_AGX_U32 fence) {
  APPLE_AGX_NOTIFIER_FLIGHT *f;
  if (!o) return APPLE_AGX_FALSE;
  if (!o->NotifierFlight) return APPLE_AGX_TRUE;
  f=building_flight(o);
  if(!f || f->Fence!=fence || f->Visible) return APPLE_AGX_FALSE;
  if(f->Published) {
    *(volatile APPLE_AGX_U32 *)notifier_threshold(o)=f->PreviousThreshold;
    if(o->NotifierFlush && !o->NotifierFlush(o->NotifierFlushContext,notifier_threshold(o),4u))
      return APPLE_AGX_FALSE; /* retain on uncertain restoration */
  }
  o->NotifierRenderCount=f->RenderCount; o->NotifierComputeCount=f->ComputeCount;
  o->NotifierComputeStamp=f->ComputeStamp; o->JobWordSet=f->WordSet; o->JobWordStamp=f->WordStamp;
  o->ComputeQueueStamp=f->ComputeQueueStamp;
  zero_bytes(f,sizeof(*f)); o->BuildingFlight=0;
  return APPLE_AGX_TRUE;
}
void AppleAgxRenderFlightVisible(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o, APPLE_AGX_U32 fence) {
  APPLE_AGX_NOTIFIER_FLIGHT *f=building_flight(o);
  if(f && f->Fence==fence && f->Published) { f->Visible=APPLE_AGX_TRUE; o->BuildingFlight=0; }
}
void AppleAgxRenderFlightStopped(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o) {
  if(!o || !o->NotifierFlight) return;
  zero_bytes(o->Flights,sizeof(o->Flights)); o->BuildingFlight=0;
}
void AppleAgxRenderFlightRetire(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o, APPLE_AGX_U32 fence) {
  APPLE_AGX_U32 i;
  if(!o || !o->NotifierFlight) return;
  for(i=0;i<APPLE_AGX_NOTIFIER_FLIGHT_COUNT;++i)
    if(o->Flights[i].Fence==fence && o->Flights[i].Visible)
      zero_bytes(&o->Flights[i],sizeof(o->Flights[i]));
}

/* Objects the CPU rewrites for every job. The TA/3D stamps (9, 10, 26, 27)
 * are not among them: as in Asahi event.rs and m1n1 render.py the CPU seeds
 * them when the queues are (re)initialised and the firmware alone writes
 * them afterwards, so a later job's build cannot signal an earlier job that
 * is still running. event_count (12) stays CPU-owned (m1n1: += 2 per job). */
static APPLE_AGX_BOOL per_submission_object(APPLE_AGX_U32 Index,
                                            APPLE_AGX_BOOL IncludeInitBm) {
  switch (Index) {
    case 12u:
    case 14u:
    case 15u:
    case 17u:
    case 18u:
    case 19u:
      return APPLE_AGX_TRUE;
    case 16u:
      return IncludeInitBm;
    default:
      return APPLE_AGX_FALSE;
  }
}

/* Multi-job phase 2: objects a job owns while it runs (scene 13, barrier,
 * micro-sequences, InitBM, TA/3D work commands, timestamps) have two copies
 * that consecutive jobs alternate between. The macOS layout puts each at the
 * end of its 16 KiB page, so the second copy sits right below the original
 * in the same page and mapping, keeping the original's alignment. */
static APPLE_AGX_BOOL slotted_object(APPLE_AGX_U32 Index) {
  return (Index >= 13u && Index <= 19u) || (Index >= 28u && Index <= 35u);
}

static APPLE_AGX_U64 slot_delta(APPLE_AGX_U64 Offset, APPLE_AGX_U32 Size) {
  APPLE_AGX_U64 alignment = Offset ? (Offset & (~Offset + 1ULL)) : 0x1000ULL;
  if (alignment > 0x1000ULL) alignment = 0x1000ULL;
  return align_up((APPLE_AGX_U64)Size, alignment);
}

/* Slot of a staged job: its stamps step by 0x100, the first job uses 0. */
static APPLE_AGX_U32 job_slot(const APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  return ((Job->TaExpectedStamp >> 8u) ^ 1u) & 1u;
}

/* rc83: one template object from the write-combined arena to its queue
 * object. The arena is read once in bulk into the cached bounce buffer
 * (wide, independent loads; EXP1105's snapshot capture already memcpy's
 * from it), at the destination's alignment, and copied from there with
 * aligned stores; DirectReads keeps the pre-rc83 element-wise copy. The
 * destination bytes are identical. Returns the bytes' cached copy, or NULL
 * for the direct copy. */
static const unsigned char *copy_arena_object(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner, unsigned char *Destination,
    const unsigned char *Source, APPLE_AGX_U32 Bytes) {
  unsigned char *bounce = (unsigned char *)Owner->ArenaBounce +
      ((APPLE_AGX_U64)Destination & 7ULL);
  if (Owner->DirectReads || Bytes > APPLE_AGX_RENDER_SHARED_MEMORY_BOUNCE_BYTES) {
    READ_PROBE(ArenaLoads, copy_shared_loads(Destination, Source, Bytes));
    copy_shared_bytes(Destination, Source, Bytes);
    return RENDER_SHARED_NULL;
  }
  memcpy(bounce, Source, Bytes);
  READ_PROBE(ArenaBulk, 1u);
  READ_PROBE(ArenaBulkBytes, Bytes);
  copy_shared_bytes(Destination, bounce, Bytes);
  return bounce;
}

static APPLE_AGX_BOOL bind_relocation_objects(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity,
    APPLE_AGX_BOOL InitializePersistent,
    APPLE_AGX_BOOL IncludeInitBm, APPLE_AGX_U32 Slot) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U32 index;
  if (Owner == RENDER_SHARED_NULL || !Owner->Initialized || !Owner->Built ||
      Owner->ObjectCount != APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT ||
      TemplateArena == RENDER_SHARED_NULL ||
      TemplateArenaBytes < AppleAgxRenderTemplateBytes() ||
      RelocationObjects == RENDER_SHARED_NULL ||
      RelocationObjectCapacity < APPLE_AGX_RENDER_SHARED_MEMORY_TEMPLATE_OBJECT_COUNT ||
      Slot > 1u)
    return APPLE_AGX_FALSE;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  Owner->StagedEventCountValid = APPLE_AGX_FALSE;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_TEMPLATE_OBJECT_COUNT;
       ++index) {
    const unsigned char *source =
        (const unsigned char *)TemplateArena + layouts[index].ArenaOffset;
    const unsigned char *copied = RENDER_SHARED_NULL;
    APPLE_AGX_U64 delta = 0ULL, other = 0ULL;
    unsigned char *destination;
    if (layouts[index].ArenaOffset > TemplateArenaBytes ||
        layouts[index].Size > TemplateArenaBytes - layouts[index].ArenaOffset)
      return APPLE_AGX_FALSE;
    if (slotted_object(index)) {
      APPLE_AGX_U64 below = slot_delta(Owner->ObjectOffsets[index],
                                       layouts[index].Size);
      if (below > Owner->ObjectOffsets[index]) return APPLE_AGX_FALSE;
      delta = Slot ? below : 0ULL;
      other = Slot ? 0ULL : below;
    }
    destination = (unsigned char *)Owner->Objects[index].CpuAddress +
        Owner->ObjectOffsets[index] - delta;
    if (Owner->NotifierFlight && index == 12u && building_flight(Owner))
      destination = (unsigned char *)&building_flight(Owner)->EventCount;
    if (InitializePersistent || per_submission_object(index, IncludeInitBm)) {
      copied = copy_arena_object(Owner, destination, source,
                                 layouts[index].Size);
      /* rc83: the threshold below starts from the event_count copied here,
       * not from a read back of the firmware-shared copy. */
      if (index == APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER_THRESHOLD &&
          copied != RENDER_SHARED_NULL && layouts[index].Size >= 4u) {
        Owner->StagedEventCount = get_u32(copied);
        Owner->StagedEventCountValid = APPLE_AGX_TRUE;
      }
    }
    /* Queue (re)initialisation seeds both copies alike. */
    if (InitializePersistent && slotted_object(index)) {
      unsigned char *second = (unsigned char *)Owner->Objects[index].CpuAddress +
          Owner->ObjectOffsets[index] - other;
      if (copied != RENDER_SHARED_NULL &&
          (((APPLE_AGX_U64)second ^ (APPLE_AGX_U64)destination) & 7ULL) == 0ULL)
        copy_shared_bytes(second, copied, layouts[index].Size);
      else
        (void)copy_arena_object(Owner, second, source, layouts[index].Size);
    }
    RelocationObjects[index].GpuVa =
        Owner->VirtualAddresses[index] + Owner->ObjectOffsets[index] - delta;
    RelocationObjects[index].PhysicalAddress =
        Owner->Objects[index].DeviceAddress + Owner->ObjectOffsets[index] - delta;
    RelocationObjects[index].Size = layouts[index].Size;
    RelocationObjects[index].Data = destination;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBindRelocationObjects(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity) {
  if (!flights_idle(Owner)) return APPLE_AGX_FALSE;
  if (!bind_relocation_objects(
          Owner, TemplateArena, TemplateArenaBytes, RelocationObjects,
          RelocationObjectCapacity, APPLE_AGX_TRUE, APPLE_AGX_TRUE, 0u))
    return APPLE_AGX_FALSE;
  /* A new queue lifetime has processed no notifying command. The template's
   * event_count is the first render job's value, which that job writes. */
  Owner->NotifierRenderCount = Owner->NotifierComputeCount =
      Owner->NotifierComputeStamp = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryInitializeComputeQueue(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner) {
  unsigned char *info,*pointers;
  const unsigned char *ta;
  if(!flights_idle(Owner)) return APPLE_AGX_FALSE;
  if(!Owner||!Owner->Initialized||!Owner->Built||
     Owner->ObjectCount!=APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT||
     !queue_object_valid(Owner,3u,184u,APPLE_AGX_TRUE)||
     !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO,
                         184u,APPLE_AGX_TRUE)||
     !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS,
                         0x60u,APPLE_AGX_TRUE)||
     !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR,
                         0xec18u,APPLE_AGX_TRUE))
    return APPLE_AGX_FALSE;
  ta=(const unsigned char *)Owner->Objects[3u].CpuAddress+Owner->ObjectOffsets[3u];
  info=(unsigned char *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO].CpuAddress;
  pointers=(unsigned char *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS].CpuAddress;
  for(APPLE_AGX_U32 i=0;i<184u;++i) info[i]=ta[i];
  put_u64(info+0x00u,Owner->VirtualAddresses[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS]);
  put_u64(info+0x08u,Owner->VirtualAddresses[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_RING]);
  put_u64(info+0x10u,Owner->VirtualAddresses[23u]+Owner->ObjectOffsets[23u]);
  put_u64(info+0x18u,Owner->VirtualAddresses[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR]+0xc000u);
  zero_bytes(pointers,0x60u);
  put_u32(pointers+0x50u,0x500u);
  /* rc98: a new queue lifetime starts with set A. */
  Owner->JobWordSet=APPLE_AGX_COMPUTE_JOB_WORD_SET_A;Owner->JobWordStamp=0u;
  Owner->ComputeQueueStamp=0u;
  /* A new work queue starts with empty firmware state. */
  zero_bytes((unsigned char *)Owner->Objects[
                 APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR].CpuAddress+
                 APPLE_AGX_COMPUTE_STATISTICS_OFFSET,
             0x10000u-APPLE_AGX_COMPUTE_STATISTICS_OFFSET);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildCompute(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_COMPUTE_INPUT *Input,
    APPLE_AGX_RENDER_COMPUTE_OUTPUT *Output) {
  APPLE_AGX_G13_COMPUTE_WORK_INPUT work={0};
  APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT micro={0};
  unsigned char *sidecar,*threshold;
  const unsigned char *control;
  APPLE_AGX_U64 gpu,stamp,notifier;
  APPLE_AGX_BOOL counted;
  APPLE_AGX_U32 set,queueStamp;
  if(!Owner||!Input||!Output||!Input->CdmStreamBase||!Input->CdmStreamBytes||
     Input->JobWordSet>=APPLE_AGX_COMPUTE_JOB_WORD_SET_COUNT||
     (Input->JobWordSet!=APPLE_AGX_COMPUTE_JOB_WORD_SET_A&&
      !Input->PreemptionGpuAddress)||
     !Input->Counter||!Input->UscExecutionBase||Input->VmSlot>=0x10u||
     Input->EventNumber>=0x80u||!Input->StampValue||!Input->EventSequence||
     !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR,
                         0x10000u,APPLE_AGX_FALSE)||
     !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER,
                         APPLE_AGX_G13_NOTIFIER_BYTES,APPLE_AGX_FALSE)||
     !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER_THRESHOLD,
                         4u,APPLE_AGX_FALSE)) return APPLE_AGX_FALSE;
  sidecar=(unsigned char *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR].CpuAddress;
  gpu=Owner->VirtualAddresses[APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR];
  stamp=Owner->VirtualAddresses[APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS]+0x80u;
  /* EXP1213: the job joins the Queue's notifier (shared with TA and 3D);
   * the firmware holds its stamp there until the threshold is reached. */
  notifier=Owner->VirtualAddresses[APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER]+
      Owner->ObjectOffsets[APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER];
  control=(const unsigned char *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER].CpuAddress+
      Owner->ObjectOffsets[APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER];
  threshold=(unsigned char *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER_THRESHOLD].CpuAddress+
      Owner->ObjectOffsets[APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER_THRESHOLD];
  if (Owner->NotifierFlight) {
    APPLE_AGX_NOTIFIER_FLIGHT *f=building_flight(Owner);
    if(!f || f->Published) return APPLE_AGX_FALSE;
    threshold=(unsigned char *)&f->EventCount;
  }
  /* Queue initialization invalidates the event even if a raw fence is
   * reused. A duplicate within the same lifetime keeps its event value. */
  counted=Owner->NotifierComputeStamp==Input->StampValue &&
      (!Owner->ComputeQueueStamps || Owner->ComputeQueueStamp!=0u) ?
      APPLE_AGX_TRUE : APPLE_AGX_FALSE;
  queueStamp=Input->StampValue;
  if(Owner->ComputeQueueStamps) {
    queueStamp=Owner->ComputeQueueStamp;
    if(!counted) {
      queueStamp+=APPLE_AGX_COMPUTE_STAMP_INCREMENT;
      /* Zero is reserved by the command/barrier validators. At wrap the
       * next nonzero event is still ahead in signed modular arithmetic. */
      if(!queueStamp) queueStamp=APPLE_AGX_COMPUTE_STAMP_INCREMENT;
    }
  }
  if((notifier&7u)||(!counted&&Owner->NotifierComputeCount>=
      ~(APPLE_AGX_U32)0-Owner->NotifierRenderCount))
    return APPLE_AGX_FALSE;
  set=Input->JobWordSet;
  if(!reserve_sidecar(Owner,set)) return APPLE_AGX_FALSE;
#define JOB_WORD(OffsetA) APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(set,OffsetA)
  /* Only the job's own words are rebuilt: the statistics, fw stamp and
   * gpu_buf belong to the work queue (Asahi workqueue.rs) and persist. */
  if(!Input->PreemptionGpuAddress)
    zero_bytes(sidecar+APPLE_AGX_COMPUTE_PREEMPT_OFFSET,
               APPLE_AGX_COMPUTE_WORK_OFFSET-APPLE_AGX_COMPUTE_PREEMPT_OFFSET);
  /* The set's RunCompute work and plain microsequence (0x600 bytes). */
  zero_bytes(sidecar+JOB_WORD(APPLE_AGX_COMPUTE_WORK_OFFSET),
             APPLE_AGX_COMPUTE_STATISTICS_OFFSET-APPLE_AGX_COMPUTE_WORK_OFFSET);
  micro.WorkGpuAddress=gpu+JOB_WORD(APPLE_AGX_COMPUTE_WORK_OFFSET);
  micro.StatisticsGpuAddress=gpu+APPLE_AGX_COMPUTE_STATISTICS_OFFSET;
  micro.QueueInfoGpuAddress=Owner->VirtualAddresses[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO];
  micro.NotifierBufferGpuAddress=notifier+APPLE_AGX_G13_NOTIFIER_BUFFER_OFFSET;
  micro.FirmwareStampGpuAddress=gpu+APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET;
  micro.Counter=Input->Counter;micro.EventSequence=Input->EventSequence;
  micro.EventGeneration=get_shared_u32(
      control+APPLE_AGX_G13_NOTIFIER_GENERATION_OFFSET,Owner->DirectReads);
  micro.VmSlot=Input->VmSlot;
  micro.StampValue=queueStamp;
  if(Input->Timestamps) {
    zero_bytes(sidecar+JOB_WORD(APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET),
               JOB_WORD(APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET)+APPLE_AGX_G13_TIMESTAMP_BLOCK_BYTES-
               JOB_WORD(APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET));
    if(!AppleAgxG13ComputeMicrosequenceBuildTimestamps(&micro,
        sidecar+JOB_WORD(APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET)))
      return APPLE_AGX_FALSE;
  } else if(!AppleAgxG13ComputeMicrosequenceBuild(&micro,
      sidecar+JOB_WORD(APPLE_AGX_COMPUTE_MICROSEQUENCE_OFFSET))) return APPLE_AGX_FALSE;
  work.Counter=Input->Counter;work.VmSlot=Input->VmSlot;
  work.NotifierGpuAddress=notifier;
  work.PreemptionGpuAddress=Input->PreemptionGpuAddress ?
      Input->PreemptionGpuAddress : gpu+APPLE_AGX_COMPUTE_PREEMPT_OFFSET;
  work.CdmStreamBase=Input->CdmStreamBase;
  work.CdmStreamEnd=Input->CdmStreamBase+Input->CdmStreamBytes;
  work.UscExecutionBase=Input->UscExecutionBase;
  work.MicrosequenceGpuAddress=gpu+(Input->Timestamps ?
      JOB_WORD(APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET) :
      JOB_WORD(APPLE_AGX_COMPUTE_MICROSEQUENCE_OFFSET));
  work.MicrosequenceBytes=Input->Timestamps ?
      APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_TIMESTAMP_BYTES :
      APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES;
  work.TimestampGpuAddress=Input->Timestamps ?
      gpu+JOB_WORD(APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET) : 0u;
  work.StampGpuAddress=stamp;
  work.FirmwareStampGpuAddress=gpu+APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET;
  work.StampValue=queueStamp;work.StampSlot=Input->EventNumber;
  work.EventControlIndex=Input->EventNumber;
  work.EventSequence=Input->EventSequence;
  work.ClientSequence=Input->ClientSequence;
  work.HelperProgram=Input->HelperProgram;
  work.HelperArgument=Input->HelperArgument;
  work.HelperConfig=Input->HelperConfig;
  work.SamplerHeap=Input->SamplerHeap;work.SamplerCount=Input->SamplerCount;
  if(!AppleAgxG13ComputeWorkBuild(&work,
      sidecar+JOB_WORD(APPLE_AGX_COMPUTE_WORK_OFFSET))) return APPLE_AGX_FALSE;
  /* Asahi queue/mod.rs run(): the threshold rises by the job's one
   * notification before the job is kicked; a rebuild is not counted. */
  if(!counted) {
    ++Owner->NotifierComputeCount;
    Owner->NotifierComputeStamp=Input->StampValue;
  }
  put_u32(threshold,Owner->NotifierRenderCount+Owner->NotifierComputeCount);
  Output->NotifierThreshold=
      Owner->NotifierRenderCount+Owner->NotifierComputeCount;
  Output->WorkGpuAddress=gpu+JOB_WORD(APPLE_AGX_COMPUTE_WORK_OFFSET);
  Output->SidecarGpuAddress=gpu;Output->SidecarCpuAddress=sidecar;
  Output->SidecarBytes=0x10000u;
  Output->FirmwareStampGpuAddress=gpu+APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET;
  Output->TimestampCpuAddress=Input->Timestamps ?
      sidecar+JOB_WORD(APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET) : RENDER_SHARED_NULL;
  Output->JobWordSet=set;
  Output->StampValue=queueStamp;
  if(Owner->ComputeQueueStamps) Owner->ComputeQueueStamp=queueStamp;
  Owner->JobWordSet=set;Owner->JobWordStamp=Input->StampValue;
#undef JOB_WORD
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxG13BarrierEncode(
    const APPLE_AGX_RENDER_BARRIER_INPUT *Input,
    unsigned char Barrier[APPLE_AGX_G13_BARRIER_BYTES]) {
  if(!Input||!Barrier||!Input->WaitStampGpuAddress||
     (Input->WaitStampGpuAddress&3u)||!Input->WaitValue||!Input->StampSelf||
     Input->WaitSlot>=0x80u) return APPLE_AGX_FALSE;
  zero_bytes(Barrier,APPLE_AGX_G13_BARRIER_BYTES);
  put_u32(Barrier,APPLE_AGX_G13_BARRIER_TAG);
  put_u64(Barrier+0x04u,Input->WaitStampGpuAddress);
  put_u32(Barrier+0x0cu,Input->WaitValue);
  put_u32(Barrier+0x10u,Input->WaitSlot);
  put_u32(Barrier+0x14u,Input->StampSelf);
  /* Asahi's explicit barriers carry uuid 0xffffbbbb; external is 0. */
  put_u32(Barrier+0x18u,0xffffbbbbu);
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL build_sidecar_barrier(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_BARRIER_INPUT *Input, APPLE_AGX_U32 OffsetA,
    APPLE_AGX_U64 *BarrierGpuAddress) {
  APPLE_AGX_U32 offset;
  if(!Owner||!Input||!BarrierGpuAddress||
     Input->JobWordSet>=APPLE_AGX_COMPUTE_JOB_WORD_SET_COUNT||
     !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR,
                         0x10000u,APPLE_AGX_FALSE)) return APPLE_AGX_FALSE;
  if(!reserve_sidecar(Owner,Input->JobWordSet)) return APPLE_AGX_FALSE;
  offset=APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(Input->JobWordSet,OffsetA);
  if(!AppleAgxG13BarrierEncode(Input,(unsigned char *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR].CpuAddress+offset))
    return APPLE_AGX_FALSE;
  *BarrierGpuAddress=Owner->VirtualAddresses[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR]+offset;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildComputeBarrier(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_BARRIER_INPUT *Input,
    APPLE_AGX_U64 *BarrierGpuAddress) {
  return build_sidecar_barrier(Owner,Input,APPLE_AGX_COMPUTE_TA_BARRIER_OFFSET,
                               BarrierGpuAddress);
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildLeadingBarrier(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_BARRIER_INPUT *Input, APPLE_AGX_U32 Queue,
    APPLE_AGX_U64 *BarrierGpuAddress) {
  if(Queue==APPLE_AGX_RENDER_BARRIER_QUEUE_TA)
    return build_sidecar_barrier(Owner,Input,
        APPLE_AGX_COMPUTE_TA_BARRIER_OFFSET,BarrierGpuAddress);
  if(Queue==APPLE_AGX_RENDER_BARRIER_QUEUE_COMPUTE)
    return build_sidecar_barrier(Owner,Input,
        APPLE_AGX_COMPUTE_QUEUE_BARRIER_OFFSET,BarrierGpuAddress);
  return APPLE_AGX_FALSE;
}

APPLE_AGX_U64 AppleAgxRenderSharedMemoryFirmwareStampAddress(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner, APPLE_AGX_U32 Queue) {
  APPLE_AGX_U32 index;
  if(!Owner) return 0u;
  if(Queue==APPLE_AGX_RENDER_STAMP_QUEUE_COMPUTE)
    return queue_object_valid(Owner,
        APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR,0x10000u,
        APPLE_AGX_FALSE) ? Owner->VirtualAddresses[
        APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR]+
        APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET : 0u;
  if(Queue==APPLE_AGX_RENDER_STAMP_QUEUE_TA)
    index=APPLE_AGX_RENDER_TA_FIRMWARE_STAMP_OBJECT;
  else if(Queue==APPLE_AGX_RENDER_STAMP_QUEUE_3D)
    index=APPLE_AGX_RENDER_3D_FIRMWARE_STAMP_OBJECT;
  else return 0u;
  return queue_object_valid(Owner,index,4u,APPLE_AGX_FALSE) ?
      Owner->VirtualAddresses[index]+Owner->ObjectOffsets[index] : 0u;
}

APPLE_AGX_U32 AppleAgxRenderSharedMemoryNextJobWordSet(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U32 StampValue, APPLE_AGX_BOOL SecondSet) {
  APPLE_AGX_U32 set=APPLE_AGX_COMPUTE_JOB_WORD_SET_A, i;
  if(Owner && SecondSet && Owner->JobWordStamp)
    set=Owner->JobWordStamp==StampValue ? Owner->JobWordSet : Owner->JobWordSet^1u;
  if(Owner && Owner->NotifierFlight && Owner->BuildingFlight &&
      Owner->BuildingFlight<=APPLE_AGX_NOTIFIER_FLIGHT_COUNT) {
    const APPLE_AGX_NOTIFIER_FLIGHT *f=&Owner->Flights[Owner->BuildingFlight-1u];
    if(f->HasSidecar) return f->SidecarSet;
    /* Preserve the legacy depth-one choice, including SECOND_SIDECAR.
     * Only a pinned other flight can force the alternate set. */
    for(i=0;i<APPLE_AGX_NOTIFIER_FLIGHT_COUNT;++i)
      if(&Owner->Flights[i]!=f && Owner->Flights[i].Fence &&
          Owner->Flights[i].HasSidecar && Owner->Flights[i].SidecarSet==set) {
        set^=1u; break;
      }
  }
  return set;
}

/* rc83 receipt: report a build point; timing only. */
static void build_mark(const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
                       APPLE_AGX_U32 Point) {
  if (Owner != RENDER_SHARED_NULL && Owner->BuildMark != RENDER_SHARED_NULL)
    Owner->BuildMark(Owner->BuildMarkContext, Point);
}

static APPLE_AGX_BOOL apply_active_relocations(
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_U32 ObjectCount, APPLE_AGX_BOOL InitializePersistent,
    APPLE_AGX_BOOL IncludeInitBm, APPLE_AGX_BOOL G4Native) {
  const APPLE_AGX_EXP208_RELOCATION *relocations =
      AppleAgxRenderTemplateRelocations();
  APPLE_AGX_U32 relocation_count = AppleAgxRenderTemplateRelocationCount();
  APPLE_AGX_U32 index;
  if (relocations == RENDER_SHARED_NULL || relocation_count == 0u)
    return APPLE_AGX_FALSE;
  if (InitializePersistent && !G4Native)
    return AppleAgxApplyRelocations(
        ActiveObjects, ObjectCount, relocations, relocation_count);
  for (index = 0u; index < relocation_count; ++index) {
    const APPLE_AGX_EXP208_RELOCATION *r = &relocations[index];
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *target;
    APPLE_AGX_EXP208_RELOCATION_OBJECT *source;
    if (!InitializePersistent &&
        !per_submission_object(r->SourceObject, IncludeInitBm)) continue;
    if (r->SourceObject >= ObjectCount || r->TargetObject >= ObjectCount)
      return APPLE_AGX_FALSE;
    target = &ActiveObjects[r->TargetObject];
    source = &ActiveObjects[r->SourceObject];
    if (G4Native && target->GpuVa == 0ULL) {
      if (!(r->TargetObject == 63u ||
            (r->TargetObject >= 67u && r->TargetObject <= 71u)) ||
          r->AddressSpace != AppleAgxExp208RelocationGpuVa ||
          r->Encoding == AppleAgxExp208RelocationGpuVaPage32kU32 ||
          source->Data == RENDER_SHARED_NULL ||
          r->SourceOffset > source->Size ||
          source->Size - r->SourceOffset < 8u)
        return APPLE_AGX_FALSE;
      zero_bytes(source->Data + r->SourceOffset, 8u);
    } else if (!AppleAgxApplyRelocations(
                   ActiveObjects, ObjectCount, r, 1u))
      return APPLE_AGX_FALSE;
  }
  return APPLE_AGX_TRUE;
}

/* rc124: the buffer-manager objects a firmware buffer slot owns, and the
 * WorkCommandInitBM/TA and StartTA/Start3D fields that name the slot
 * (m1n1 cmdqueue.py/microsequence.py V13_5; Asahi fw/buffer.rs InitBuffer,
 * fw/vertex.rs, fw/microseq.rs buffer_slot). */
static const APPLE_AGX_U32 manager_slot_objects[4]={1u,20u,21u,22u};

/* Slot s's copy of a manager object sits s steps below the original in its
 * page; a step keeps the original's alignment modulo 64 bytes. */
static APPLE_AGX_U64 manager_slot_delta(const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
                                        APPLE_AGX_U32 Index, APPLE_AGX_U32 Size,
                                        APPLE_AGX_U32 Slot) {
  APPLE_AGX_U64 delta = (APPLE_AGX_U64)Slot * align_up((APPLE_AGX_U64)Size, 0x40ULL);
  return delta <= Owner->ObjectOffsets[Index] ? delta : ~0ULL;
}

static APPLE_AGX_BOOL select_manager_slot(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
                                          APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
                                          APPLE_AGX_U32 Slot) {
  APPLE_AGX_U32 i;
  if (Slot == 0u) return APPLE_AGX_TRUE;
  if (Slot >= APPLE_AGX_RENDER_MANAGER_SLOTS) return APPLE_AGX_FALSE;
  for (i = 0u; i < 4u; ++i) {
    APPLE_AGX_EXP208_RELOCATION_OBJECT *object = &ActiveObjects[manager_slot_objects[i]];
    APPLE_AGX_U64 delta = manager_slot_delta(Owner, manager_slot_objects[i], object->Size, Slot);
    if (delta == ~0ULL || object->Data == RENDER_SHARED_NULL) return APPLE_AGX_FALSE;
    object->Data -= delta;
    object->GpuVa -= delta;
    object->PhysicalAddress -= delta;
  }
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL write_manager_slot(APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
                                         APPLE_AGX_BOOL IncludeInitBm, APPLE_AGX_U32 Slot) {
  if (ActiveObjects[19u].Size < 0x24u || ActiveObjects[15u].Size < 0x5cu ||
      ActiveObjects[17u].Size < 0x48u || (IncludeInitBm && ActiveObjects[16u].Size < 0x0cu))
    return APPLE_AGX_FALSE;
  if (IncludeInitBm) put_u32(ActiveObjects[16u].Data + 0x08u, Slot); /* InitBM.buffer_mgr_slot */
  put_u64(ActiveObjects[19u].Data + 0x1cu, Slot); /* WorkCommandTA.buffer_mgr_slot */
  put_u32(ActiveObjects[15u].Data + 0x58u, Slot); /* Start3DCmd.buffer_mgr_slot */
  put_u64(ActiveObjects[17u].Data + 0x40u, Slot); /* StartTACmd.buffer_mgr_slot */
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL build_active_job(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob,
    APPLE_AGX_BOOL G4Native, APPLE_AGX_BOOL InitializeQueues,
    APPLE_AGX_U32 ManagerSlot) {
  APPLE_AGX_BACKEND_JOB_IMAGE candidate;
  APPLE_AGX_U32 index;
  APPLE_AGX_BOOL initialize_persistent;
#define BUILD_REFUSE(code) do { if (Owner != RENDER_SHARED_NULL) \
    Owner->LastBuildRefusal = APPLE_AGX_RENDER_BUILD_REFUSAL_##code; \
    return APPLE_AGX_FALSE; } while (0)
  if (Owner != RENDER_SHARED_NULL) Owner->LastBuildRefusal = 0u;
  if (SourceObjects == RENDER_SHARED_NULL ||
      SourceObjectCount != APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      StagedJob == RENDER_SHARED_NULL || ActiveObjects == RENDER_SHARED_NULL ||
      ActiveJob == RENDER_SHARED_NULL || ArenaGpuAddress == 0ULL ||
      (ArenaGpuAddress & (APPLE_AGX_EXP208_ARENA_ALIGNMENT - 1u)) != 0ULL ||
      TemplateArenaBytes != APPLE_AGX_EXP208_ARENA_BYTES)
    BUILD_REFUSE(JOB_ARGUMENTS);
  if (StagedJob->TaWorkAddressCount != APPLE_AGX_EXP208_ROOT_COUNT ||
      StagedJob->D3WorkAddressCount != APPLE_AGX_EXP208_ROOT_COUNT ||
      StagedJob->TaEvent >= 128u || StagedJob->D3Event >= 128u ||
      StagedJob->TaEvent == StagedJob->D3Event ||
      StagedJob->TaExpectedStamp == 0u ||
      StagedJob->D3ExpectedStamp == 0u ||
      StagedJob->TaExpectedDonePointer >= APPLE_AGX_EXP208_QUEUE_CAPACITY ||
      StagedJob->D3ExpectedDonePointer >= APPLE_AGX_EXP208_QUEUE_CAPACITY)
    BUILD_REFUSE(STAGED_JOB);
  if (!runtime_bindings_valid(RuntimeBindings))
    BUILD_REFUSE(RUNTIME_BINDINGS);
  for (index = 0u; index < SourceObjectCount; ++index)
    ActiveObjects[index] = SourceObjects[index];
  initialize_persistent = InitializeQueues;
  if (Owner->NotifierFlight) {
    APPLE_AGX_NOTIFIER_FLIGHT *f=building_flight(Owner);
    if(!f || f->Published) BUILD_REFUSE(BIND);
    for(index=0;index<APPLE_AGX_NOTIFIER_FLIGHT_COUNT;++index) {
      const APPLE_AGX_NOTIFIER_FLIGHT *other=&Owner->Flights[index];
      if(other!=f && other->Fence && (InitializeQueues ||
          (other->HasRender && other->RenderSlot==job_slot(StagedJob))))
        BUILD_REFUSE(BIND);
    }
    f->EpochReset=InitializeQueues;
    f->HasRender=APPLE_AGX_TRUE; f->RenderSlot=job_slot(StagedJob);
  }
  if (!bind_relocation_objects(
          Owner, TemplateArena, TemplateArenaBytes, ActiveObjects,
          SourceObjectCount, initialize_persistent, IncludeInitBm,
          job_slot(StagedJob)))
    BUILD_REFUSE(BIND);
  /* rc124: before any relocation reads the manager objects' addresses */
  if (!select_manager_slot(Owner, ActiveObjects, ManagerSlot))
    BUILD_REFUSE(BIND);
  build_mark(Owner, APPLE_AGX_RENDER_BUILD_MARK_COPIED);
  /* rc124: a managed build into slot s > 0 writes BlockControl itself
   * (restored or template, then the backed block count) */
  if (IncludeInitBm && ManagerSlot == 0u) {
    APPLE_AGX_EXP208_RELOCATION_OBJECT *control = &ActiveObjects[20];
    APPLE_AGX_U32 total;
    if (control->Data == RENDER_SHARED_NULL || control->Size < 8u)
      BUILD_REFUSE(INITBM);
    total = get_shared_u32(control->Data, Owner->DirectReads);
    if (total == 0u)
      BUILD_REFUSE(INITBM);
    put_u32(control->Data + 4u, total);
  }
  if (!apply_active_relocations(
          ActiveObjects, SourceObjectCount, initialize_persistent,
          IncludeInitBm, G4Native) ||
      ActiveObjects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa == 0ULL ||
      ActiveObjects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa == 0ULL ||
      ActiveObjects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa == 0ULL ||
      ActiveObjects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa == 0ULL)
    BUILD_REFUSE(RELOCATIONS);
  if (ActiveObjects[17u].Data == RENDER_SHARED_NULL ||
      ActiveObjects[17u].Size < 548u ||
      ActiveObjects[15u].Data == RENDER_SHARED_NULL ||
      ActiveObjects[15u].Size < 612u)
    BUILD_REFUSE(STATS);
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
  if (Owner != RENDER_SHARED_NULL && Owner->TaStartTimestamps) {
    if (!patch_ta_timestamp_update(ActiveObjects[17u].Data,
                                   ActiveObjects[17u].Size) ||
        ActiveObjects[30u].Data == RENDER_SHARED_NULL ||
        ActiveObjects[30u].Size != sizeof(APPLE_AGX_U64) ||
        ActiveObjects[31u].Data == RENDER_SHARED_NULL ||
        ActiveObjects[31u].Size != sizeof(APPLE_AGX_U64))
      BUILD_REFUSE(STATS);
    /* EXP2125: update_ts alone left TA starts stale. The compute path,
     * whose starts advance, zeroes its timestamp block for every job
     * (build_compute_job); the TA start/end objects 30/31 never were. */
    put_u64(ActiveObjects[30u].Data, 0u);
    put_u64(ActiveObjects[31u].Data, 0u);
  }
  candidate = *StagedJob;
  for (index = 0u; index < 8u; ++index) {
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *timestamp =
        &ActiveObjects[28u + index];
    if (timestamp->Data == RENDER_SHARED_NULL ||
        timestamp->Size != sizeof(APPLE_AGX_U64))
      BUILD_REFUSE(STATS);
    candidate.RenderTimestampPointers[index] = timestamp->Data;
  }
  candidate.TaWorkAddresses[0] =
      ActiveObjects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa;
  candidate.TaWorkAddresses[1] =
      ActiveObjects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa;
  candidate.D3WorkAddresses[0] =
      ActiveObjects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa;
  candidate.D3WorkAddresses[1] =
      ActiveObjects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa;
  {
    /* event_count now holds this job's 2 * sequence from the arena; the
     * queue initialisation resets the firmware count, so ours with it. */
    unsigned char *threshold =
        ActiveObjects[APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER_THRESHOLD].Data;
    APPLE_AGX_U32 render;
    if (threshold == RENDER_SHARED_NULL ||
        ActiveObjects[APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER_THRESHOLD].Size <
            4u)
      BUILD_REFUSE(THRESHOLD);
    if (initialize_persistent)
      Owner->NotifierComputeCount = Owner->NotifierComputeStamp = 0u;
    render = !Owner->DirectReads && Owner->StagedEventCountValid ?
        Owner->StagedEventCount : get_shared_u32(threshold, APPLE_AGX_TRUE);
    if (render > ~(APPLE_AGX_U32)0 - Owner->NotifierComputeCount)
      BUILD_REFUSE(THRESHOLD);
    Owner->NotifierRenderCount = render;
    put_u32(threshold, render + Owner->NotifierComputeCount);
  }
  *ActiveJob = candidate;
  build_mark(Owner, APPLE_AGX_RENDER_BUILD_MARK_RELOCATED);
  return APPLE_AGX_TRUE;
#undef BUILD_REFUSE
}

/* rc124: an unmanaged build uses slot 0's manager objects as its own. */
static APPLE_AGX_BOOL unbind_manager_slot0(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner) {
  Owner->SlotBound[0] = APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveJob(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob) {
  return build_active_job(Owner, TemplateArena, TemplateArenaBytes,
      SourceObjects, SourceObjectCount, ArenaGpuAddress, IncludeInitBm,
      RuntimeBindings, StagedJob, ActiveObjects, ActiveJob, APPLE_AGX_FALSE, IncludeInitBm, 0u) &&
      unbind_manager_slot0(Owner);
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveG4Job(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob) {
  return build_active_job(Owner, TemplateArena, TemplateArenaBytes,
      SourceObjects, SourceObjectCount, ArenaGpuAddress, IncludeInitBm,
      RuntimeBindings, StagedJob, ActiveObjects, ActiveJob, APPLE_AGX_TRUE, IncludeInitBm, 0u) &&
      unbind_manager_slot0(Owner);
}

static void copy_bytes(unsigned char *Destination, const unsigned char *Source,
                       APPLE_AGX_U32 Bytes) {
  copy_shared_bytes(Destination,Source,Bytes);
}

static APPLE_AGX_BOOL manager_key_equal(
    const APPLE_AGX_RENDER_MANAGER_KEY *a,
    const APPLE_AGX_RENDER_MANAGER_KEY *b, APPLE_AGX_BOOL Root) {
  APPLE_AGX_U32 i;
  if (a->Owner != b->Owner || a->Generation != b->Generation ||
      (Root && a->RootIpa != b->RootIpa)) return APPLE_AGX_FALSE;
  for (i=0u;i<3u;++i)
    if (a->Backing[i].Va != b->Backing[i].Va ||
        a->Backing[i].Bytes != b->Backing[i].Bytes ||
        a->Backing[i].Reserved != b->Backing[i].Reserved)
      return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

/* rc124: managed builds use slots 1..count-1; slot 0 stays with the
 * unmanaged builds (template default). Returns the slot bound to Key, or
 * ~0u. */
static APPLE_AGX_U32 manager_slot_count(const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner) {
  return Owner->ManagerSlotCount > APPLE_AGX_RENDER_MANAGER_SLOTS ?
      APPLE_AGX_RENDER_MANAGER_SLOTS : Owner->ManagerSlotCount;
}

static APPLE_AGX_U32 bound_manager_slot(const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
                                        const APPLE_AGX_RENDER_MANAGER_KEY *Key) {
  APPLE_AGX_U32 s, n = manager_slot_count(Owner);
  for (s = 1u; s < n; ++s)
    if (Owner->SlotBound[s] && manager_key_equal(&Owner->SlotKey[s], Key, APPLE_AGX_TRUE))
      return s;
  return ~0u;
}

APPLE_AGX_BOOL AppleAgxRenderManagerNeedsBind(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key) {
  if (!Owner || !Key || !Key->Owner || !Key->Generation) return APPLE_AGX_TRUE;
  if (manager_slot_count(Owner) > 1u)
    return bound_manager_slot(Owner, Key) == ~0u;
  return !manager_key_equal(&Owner->ManagerKey,Key,APPLE_AGX_TRUE);
}

APPLE_AGX_BOOL AppleAgxRenderManagerCanShare(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_MANAGER_STATE *State,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key) {
  return Owner && State && Key && Owner->ManagerDepth2 &&
      Owner->ManagerSaveOnEvict && Owner->ManagerEvict &&
      Owner->ManagerFence && !Owner->ManagerSecondFence &&
      !Owner->ManagerPoisoned && State->PendingFence == Owner->ManagerFence &&
      manager_key_equal(&Owner->ManagerKey,Key,APPLE_AGX_TRUE) &&
      manager_key_equal(&State->Key,Key,APPLE_AGX_TRUE) &&
      Owner->ManagerSlot > 0u &&
      bound_manager_slot(Owner,Key) == Owner->ManagerSlot;
}

/* Exact terminal ownership, independent of observation order. A fault poisons
 * the slot until every user is terminal; no successor may restore its bytes.
 * Called only after joined terminal progress (or proven unpublished cancel). */
static APPLE_AGX_BOOL manager_terminal(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *State, APPLE_AGX_U32 Fence,
    APPLE_AGX_BOOL Abort, const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects) {
  APPLE_AGX_U32 remaining;
  if (!Owner || !State || !Fence ||
      !manager_key_equal(&Owner->ManagerKey,&State->Key,APPLE_AGX_TRUE))
    return APPLE_AGX_FALSE;
  if (Fence != Owner->ManagerFence && Fence != Owner->ManagerSecondFence)
    return Fence == Owner->ManagerTerminalFence[0] ||
           Fence == Owner->ManagerTerminalFence[1];
  if (State->PendingFence != Owner->ManagerFence ||
      Owner->ManagerSlot >= APPLE_AGX_RENDER_MANAGER_SLOTS)
    return APPLE_AGX_FALSE;
  remaining = Fence == Owner->ManagerFence ? Owner->ManagerSecondFence : Owner->ManagerFence;
  if (Abort) Owner->ManagerPoisoned = APPLE_AGX_TRUE;
  if (Owner->ManagerPoisoned) {
    Owner->SlotBound[Owner->ManagerSlot] = APPLE_AGX_FALSE;
    Owner->SlotDirtyFence[Owner->ManagerSlot] = 0u;
    State->Valid = State->Resident = APPLE_AGX_FALSE;
  } else {
    /* Stats is only consumed when the manager is idle. Do not read shared
     * mutable bytes while the other exact render still owns the slot. */
    if (!remaining)
      read_shared_bytes(State->Stats,Objects[22].Data,sizeof(State->Stats));
    State->Valid = State->Resident = APPLE_AGX_TRUE;
    if (!Owner->SlotDirtyFence[Owner->ManagerSlot] ||
        Fence - Owner->SlotDirtyFence[Owner->ManagerSlot] < 0x80000000u)
      Owner->SlotDirtyFence[Owner->ManagerSlot] = Fence;
  }
  if (!State->SavedFence || Fence - State->SavedFence < 0x80000000u)
    State->SavedFence = Fence;
  Owner->ManagerFence = State->PendingFence = remaining;
  Owner->ManagerSecondFence = 0u;
  Owner->ManagerTerminalFence[0] = Owner->ManagerTerminalFence[1];
  Owner->ManagerTerminalFence[1] = Fence;
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL manager_objects_valid(
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects) {
  return Objects && Objects[1].Data && Objects[1].Size==188u &&
      Objects[20].Data && Objects[20].Size==64u &&
      Objects[21].Data && Objects[21].Size==64u &&
      Objects[22].Data && Objects[22].Size==64u;
}

APPLE_AGX_BOOL AppleAgxRenderManagerSnapshot(APPLE_AGX_RENDER_MANAGER_STATE *State,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key, APPLE_AGX_U32 Fence,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects, APPLE_AGX_BOOL DirectReads) {
  if (!State || !Key || !Fence || !manager_objects_valid(Objects)) return APPLE_AGX_FALSE;
  if (DirectReads) {
    READ_PROBE(SharedLoads,
        copy_shared_loads(State->Info,Objects[1].Data,sizeof(State->Info)) +
        copy_shared_loads(State->BlockControl,Objects[20].Data,sizeof(State->BlockControl)) +
        copy_shared_loads(State->Counter,Objects[21].Data,sizeof(State->Counter)) +
        copy_shared_loads(State->Stats,Objects[22].Data,sizeof(State->Stats)));
    copy_bytes(State->Info,Objects[1].Data,sizeof(State->Info));
    copy_bytes(State->BlockControl,Objects[20].Data,sizeof(State->BlockControl));
    copy_bytes(State->Counter,Objects[21].Data,sizeof(State->Counter));
    copy_bytes(State->Stats,Objects[22].Data,sizeof(State->Stats));
  } else {
    /* rc83: State's arrays sit 4 bytes off the objects' 8-byte alignment,
     * so copy_bytes read all 380 bytes one Device load at a time (EXP2012
     * Complete -> JobEnd 77 us); aligned word loads read the same bytes. */
    read_shared_bytes(State->Info,Objects[1].Data,sizeof(State->Info));
    read_shared_bytes(State->BlockControl,Objects[20].Data,sizeof(State->BlockControl));
    read_shared_bytes(State->Counter,Objects[21].Data,sizeof(State->Counter));
    read_shared_bytes(State->Stats,Objects[22].Data,sizeof(State->Stats));
  }
  State->Key=*Key;
  State->Valid=APPLE_AGX_TRUE;
  State->SavedFence=Fence;
  State->PendingFence=0u;
  State->Resident=APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderManagerAcceptSnapshot(APPLE_AGX_RENDER_MANAGER_STATE *State,
    const APPLE_AGX_RENDER_MANAGER_STATE *Snapshot) {
  if (!State || !Snapshot || !Snapshot->Valid || Snapshot->Resident ||
      Snapshot->PendingFence) return APPLE_AGX_FALSE;
  /* A process can grow/recreate its manager while an old slot is cached.
   * An eviction of that old key must not replace the new manager state. */
  if (!manager_key_equal(&State->Key,&Snapshot->Key,APPLE_AGX_TRUE))
    return APPLE_AGX_TRUE;
  if (State->PendingFence) return APPLE_AGX_FALSE;
  if (State->Resident) {
    if (State->SavedFence!=Snapshot->SavedFence) return APPLE_AGX_FALSE;
    *State=*Snapshot;
  }
  return APPLE_AGX_TRUE;
}

static APPLE_AGX_BOOL evict_manager_slot(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
                                        APPLE_AGX_U32 Slot) {
  APPLE_AGX_EXP208_RELOCATION_OBJECT objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT] = {{0}};
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts = AppleAgxRenderTemplateObjectLayouts();
  APPLE_AGX_U32 i;
  if (!Owner->SlotDirtyFence[Slot]) return APPLE_AGX_TRUE;
  if (Owner->ManagerFence || !Owner->ManagerEvict || !Owner->SlotBound[Slot])
    return APPLE_AGX_FALSE;
  for (i=0u;i<4u;++i) {
    APPLE_AGX_U32 n=manager_slot_objects[i];
    if (!Owner->Objects[n].CpuAddress ||
        Owner->ObjectOffsets[n] > Owner->Objects[n].Length ||
        layouts[n].Size > Owner->Objects[n].Length-Owner->ObjectOffsets[n])
      return APPLE_AGX_FALSE;
    objects[n].Data=(unsigned char *)Owner->Objects[n].CpuAddress+Owner->ObjectOffsets[n];
    objects[n].Size=layouts[n].Size;
    objects[n].GpuVa=Owner->VirtualAddresses[n]+Owner->ObjectOffsets[n];
  }
  if (!select_manager_slot(Owner,objects,Slot) ||
      !Owner->ManagerEvict(Owner->ManagerEvictContext,&Owner->SlotKey[Slot],
                          Owner->SlotDirtyFence[Slot],objects))
    return APPLE_AGX_FALSE;
  Owner->SlotDirtyFence[Slot]=0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderManagerDrain(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner) {
  APPLE_AGX_U32 s;
  if (!Owner || Owner->ManagerFence) return APPLE_AGX_FALSE;
  for (s=1u;s<APPLE_AGX_RENDER_MANAGER_SLOTS;++s)
    if (!evict_manager_slot(Owner,s)) return APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderManagerAbort(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *State, APPLE_AGX_U32 Fence) {
  if (!Owner || !State || !Fence) return APPLE_AGX_FALSE;
  if (Owner->ManagerDepth2)
    return manager_terminal(Owner,State,Fence,APPLE_AGX_TRUE,0);
  if (!Owner->ManagerFence && !State->PendingFence && !State->Valid &&
      !State->Resident && State->SavedFence==Fence &&
      manager_key_equal(&Owner->ManagerKey,&State->Key,APPLE_AGX_TRUE))
    return APPLE_AGX_TRUE;
  if (Owner->ManagerFence!=Fence || State->PendingFence!=Fence ||
      Owner->ManagerSlot>=APPLE_AGX_RENDER_MANAGER_SLOTS) return APPLE_AGX_FALSE;
  Owner->SlotBound[Owner->ManagerSlot]=APPLE_AGX_FALSE;
  Owner->SlotDirtyFence[Owner->ManagerSlot]=0u;
  Owner->ManagerFence=0u;
  State->Key=Owner->ManagerKey;
  State->Valid=State->Resident=APPLE_AGX_FALSE;
  /* Invalid terminal record: permits an exact dropped-completion retry,
   * never qualifies for restore (Valid is false). */
  State->SavedFence=Fence;
  State->PendingFence=0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderManagerCancelBuild(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *State, APPLE_AGX_U32 Fence,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects) {
  if (Owner && Owner->ManagerDepth2 && Owner->ManagerSecondFence) {
    if (!State || Fence != Owner->ManagerSecondFence ||
        State->PendingFence != Owner->ManagerFence ||
        !manager_key_equal(&Owner->ManagerKey,&State->Key,APPLE_AGX_TRUE) ||
        !manager_objects_valid(Objects)) return APPLE_AGX_FALSE;
    put_u32(Objects[21].Data,Owner->ManagerBuildCounter);
    Owner->ManagerSecondFence = 0u;
    return APPLE_AGX_TRUE;
  }
  if (!Owner || !State || !Fence || Owner->ManagerFence!=Fence ||
      State->PendingFence!=Fence || Owner->ManagerSlot>=APPLE_AGX_RENDER_MANAGER_SLOTS ||
      !manager_objects_valid(Objects)) return APPLE_AGX_FALSE;
  if (Owner->ManagerSlotLive) {
    /* No GPU access has occurred. Live Info/BlockControl/Stats were not
     * written by this build; undo its sole manager mutation (Counter). */
    put_u32(Objects[21].Data,Owner->ManagerBuildCounter);
  } else {
    /* The victim was snapshotted before replacement; require restore on
     * the next use instead of claiming unsubmitted bytes are live. */
    Owner->SlotBound[Owner->ManagerSlot]=APPLE_AGX_FALSE;
    Owner->SlotDirtyFence[Owner->ManagerSlot]=0u;
  }
  State->PendingFence=0u;
  Owner->ManagerFence=0u;
  Owner->ManagerKey=State->Key;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderManagerSave(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *State, APPLE_AGX_U32 Fence,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects) {
  if (!Owner || !State || !Fence || !manager_objects_valid(Objects))
    return APPLE_AGX_FALSE;
  if (Owner->ManagerDepth2)
    return manager_terminal(Owner,State,Fence,APPLE_AGX_FALSE,Objects);
  if (!Owner->ManagerFence && !State->PendingFence && State->Valid &&
      State->SavedFence==Fence &&
      manager_key_equal(&Owner->ManagerKey,&State->Key,APPLE_AGX_TRUE))
    return APPLE_AGX_TRUE;
  if (Owner->ManagerFence!=Fence || State->PendingFence!=Fence)
    return APPLE_AGX_FALSE;
  if (Owner->ManagerSaveOnEvict && manager_slot_count(Owner) > 1u &&
      Owner->ManagerEvict && Owner->ManagerSlot > 0u &&
      Owner->ManagerSlot < manager_slot_count(Owner)) {
    /* Stats drives rc80 TVB growth and must remain current each completion.
     * All other mutable bytes stay in the slot until an idle eviction. */
    read_shared_bytes(State->Stats,Objects[22].Data,sizeof(State->Stats));
    State->Key=Owner->ManagerKey;
    State->Valid=APPLE_AGX_TRUE;
    State->Resident=APPLE_AGX_TRUE;
    State->SavedFence=Fence;
    State->PendingFence=0u;
    Owner->SlotDirtyFence[Owner->ManagerSlot]=Fence;
  } else {
    if (!AppleAgxRenderManagerSnapshot(State,&Owner->ManagerKey,Fence,
                                      Objects,Owner->DirectReads))
      return APPLE_AGX_FALSE;
  }
  Owner->ManagerFence=0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildManagedG4Job(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *Manager,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key, APPLE_AGX_U32 Fence,
    APPLE_AGX_BOOL InitializeQueues,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob) {
  APPLE_AGX_BOOL restore, live = APPLE_AGX_FALSE;
  APPLE_AGX_U32 i,count,slot = 0u;
  const APPLE_AGX_EXP208_RELOCATION *relocations;
  static const APPLE_AGX_U32 objects[]={1u,20u,21u,22u,13u};
#define MANAGED_REFUSE(code) do { \
    Owner->LastBuildRefusal = APPLE_AGX_RENDER_BUILD_REFUSAL_##code; \
    return APPLE_AGX_FALSE; } while (0)
  if (!Owner) return APPLE_AGX_FALSE;
  Owner->LastBuildRefusal = 0u;
  if (Owner->ManagerFence &&
      (!AppleAgxRenderManagerCanShare(Owner,Manager,Key) ||
       InitializeQueues || IncludeInitBm || Fence == Owner->ManagerFence))
    MANAGED_REFUSE(MANAGER_FENCE);
  if (Manager && Manager->PendingFence &&
      !AppleAgxRenderManagerCanShare(Owner,Manager,Key))
    MANAGED_REFUSE(MANAGER_PENDING);
  if (!Manager || !Key || !Fence || !Key->Owner || !Key->Generation || !Key->RootIpa ||
      SourceObjectCount!=APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      !manager_objects_valid(SourceObjects) || !ActiveObjects ||
      !SourceObjects[13].Data || SourceObjects[13].Size!=72u)
    MANAGED_REFUSE(MANAGER_KEY);
  if (Manager->Resident && !manager_key_equal(&Manager->Key,Key,APPLE_AGX_TRUE)) {
    APPLE_AGX_U32 previous=bound_manager_slot(Owner,&Manager->Key);
    if (previous==~0u || !evict_manager_slot(Owner,previous))
      MANAGED_REFUSE(MANAGER_SNAPSHOT);
  }
  if (manager_slot_count(Owner) > 1u) {
    /* rc124: the key's bound slot keeps its firmware-owned state; otherwise
     * a free slot, else the least recently used one, is (re)bound. A queue
     * (re)initialisation forgets every binding. */
    APPLE_AGX_U32 s, n = manager_slot_count(Owner);
    if (InitializeQueues && !AppleAgxRenderManagerDrain(Owner))
      MANAGED_REFUSE(MANAGER_SNAPSHOT);
    if (InitializeQueues)
      for (s = 0u; s < APPLE_AGX_RENDER_MANAGER_SLOTS; ++s) Owner->SlotBound[s] = APPLE_AGX_FALSE;
    slot = bound_manager_slot(Owner, Key);
    if (slot != ~0u) {
      live = !IncludeInitBm;
    } else {
      slot = 1u;
      for (s = 1u; s < n; ++s) {
        if (!Owner->SlotBound[s]) { slot = s; break; }
        if (Owner->SlotUse[s] < Owner->SlotUse[slot]) slot = s;
      }
    }
    if (!live && !IncludeInitBm) MANAGED_REFUSE(MANAGER_BIND);
  } else if (AppleAgxRenderManagerNeedsBind(Owner,Key) && !IncludeInitBm)
    MANAGED_REFUSE(MANAGER_BIND);
  for (i=0u;i<3u;++i) {
    APPLE_AGX_U32 object=i==0u?41u:(i==1u?42u:43u);
    if (!Key->Backing[i].Bytes || Key->Backing[i].Reserved ||
        Key->Backing[i].Va!=SourceObjects[object].GpuVa)
      MANAGED_REFUSE(MANAGER_BACKING);
  }
  /* Preserve a victim before build_active_job can overwrite any slot bytes.
   * A forced InitBM of the current key needs the same snapshot as eviction. */
  if (!live && !evict_manager_slot(Owner,slot)) MANAGED_REFUSE(MANAGER_SNAPSHOT);
  if (!live && Manager->Resident) MANAGED_REFUSE(MANAGER_SNAPSHOT);
  restore=!live && Manager->Valid && manager_key_equal(&Manager->Key,Key,APPLE_AGX_FALSE);
  Owner->LastBuildRestored=restore;
  /* Once replacement starts, the victim binding must never identify the
   * replacement's bytes, including on a late relocation failure. */
  if (!live) Owner->SlotBound[slot]=APPLE_AGX_FALSE;
  if (!build_active_job(Owner,TemplateArena,TemplateArenaBytes,SourceObjects,
      SourceObjectCount,ArenaGpuAddress,IncludeInitBm,RuntimeBindings,
      StagedJob,ActiveObjects,ActiveJob,APPLE_AGX_TRUE,InitializeQueues,slot))
    return APPLE_AGX_FALSE;
  /* Restore manager-owned mutable state, never queue-owned bytes. Scene13 is
   * a new render pass even when the same buffer manager stays bound. */
  for (i=0u;i<sizeof(objects)/sizeof(objects[0]);++i) {
    APPLE_AGX_U32 n=objects[i];
    const unsigned char *source=SourceObjects[n].Data;
    /* rc124: a live slot's manager objects are the firmware's; only the
     * scene (13) is new */
    if (live && n!=13u) continue;
    if (restore) {
      if(n==1u) source=Manager->Info;
      if(n==20u) source=Manager->BlockControl;
      if(n==21u) source=Manager->Counter;
      if(n==22u) source=Manager->Stats;
    }
    /* rc83: a template (arena) source is read in bulk, a saved one is
     * cached already. */
    if (source==SourceObjects[n].Data)
      (void)copy_arena_object(Owner,ActiveObjects[n].Data,source,ActiveObjects[n].Size);
    else
      copy_bytes(ActiveObjects[n].Data,source,ActiveObjects[n].Size);
  }
  if (!restore && !live) {
    /* The pinned first-job template counter includes its first commit. */
    put_u32(ActiveObjects[21].Data,0u);
    put_u32(ActiveObjects[20].Data+4u,Key->Backing[2].Bytes/0x20000u);
  }
  /* rc83: the counter just written (the saved one, or 0 for a new
   * backing), not a read back of the firmware-shared copy. */
  count=Owner->DirectReads || live ? get_shared_u32(ActiveObjects[21].Data,APPLE_AGX_TRUE) :
      restore ? get_u32(Manager->Counter) : 0u;
  put_u32(ActiveObjects[21].Data,count+1u);
  /* Only refreshed Info/Scene require new relocations. Other work fields were
   * already relocated above; snapshot addresses stay in this context0 slot. */
  relocations=AppleAgxRenderTemplateRelocations();
  for (i=0u;i<AppleAgxRenderTemplateRelocationCount();++i)
    if (((!live && relocations[i].SourceObject==1u) || relocations[i].SourceObject==13u) &&
        !AppleAgxApplyRelocations(ActiveObjects,SourceObjectCount,&relocations[i],1u)) {
      if (live) put_u32(ActiveObjects[21].Data,count);
      MANAGED_REFUSE(MANAGER_RELOCATE);
    }
  if (manager_slot_count(Owner) > 1u) {
    if (!write_manager_slot(ActiveObjects,IncludeInitBm,slot)) {
      if (live) put_u32(ActiveObjects[21].Data,count);
      MANAGED_REFUSE(MANAGER_RELOCATE);
    }
    Owner->SlotKey[slot]=*Key;
    Owner->SlotBound[slot]=APPLE_AGX_TRUE;
    Owner->SlotUse[slot]=++Owner->ManagerSlotClock;
  }
  Owner->ManagerBuildCounter=count;
  Owner->ManagerSlot=slot;
  Owner->ManagerSlotLive=live;
#undef MANAGED_REFUSE
  Owner->ManagerKey=*Key;
  if (Owner->ManagerDepth2) {
    /* Pending ownership needs the current key. Do not relabel a snapshot of
     * different backing as valid: an unpublished rebind may be cancelled and
     * retried before any completion refreshes those saved bytes. */
    if (!manager_key_equal(&Manager->Key,Key,APPLE_AGX_FALSE))
      Manager->Valid=APPLE_AGX_FALSE;
    Manager->Key=*Key;
    if (Owner->ManagerFence) Owner->ManagerSecondFence=Fence;
    else {
      Owner->ManagerFence=Fence;
      Owner->ManagerPoisoned=APPLE_AGX_FALSE;
    }
    Manager->PendingFence=Owner->ManagerFence;
  } else {
    Owner->ManagerFence=Fence;
    Manager->PendingFence=Fence;
  }
  build_mark(Owner,APPLE_AGX_RENDER_BUILD_MARK_MANAGED);
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
      !queue_object_valid(Owner, d3_stamp, 4u, Prepared) ||
      !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO,
                          184u,Prepared) ||
      !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_RING,
                          APPLE_AGX_G13_RING_CAPACITY*8ULL,Prepared) ||
      !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS,
                          0x84u,Prepared) ||
      !queue_object_valid(Owner,APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR,
                          0x10000u,Prepared))
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
  Config->HasCompute=APPLE_AGX_TRUE;
  Config->Compute.QueueType=(APPLE_AGX_U32)AppleAgxG13QueueCompute;
  Config->Compute.QueueInfoGpuAddress=Owner->VirtualAddresses[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO];
  Config->Compute.RingCpuAddress=(APPLE_AGX_U64 *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_RING].CpuAddress;
  Config->Compute.RingCapacity=APPLE_AGX_G13_RING_CAPACITY;
  Config->Compute.GpuDonePointer=(volatile APPLE_AGX_U32 *)Owner->Objects[
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS].CpuAddress;
  Config->Compute.CpuWritePointer=(volatile APPLE_AGX_U32 *)((unsigned char *)
      Owner->Objects[APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS].CpuAddress+0x40u);
  Config->Compute.Stamp=(volatile APPLE_AGX_U32 *)((unsigned char *)
      Owner->Objects[APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS].CpuAddress+0x80u);
  Config->Compute.EventNumber=2u;
  /* S1: the firmware queue stamps (objects 9/10, sidecar 0x8780) */
  Config->Ta.FirmwareStamp = queue_object_valid(Owner,
      APPLE_AGX_RENDER_TA_FIRMWARE_STAMP_OBJECT, 4u, Prepared) ?
      (volatile APPLE_AGX_U32 *)((unsigned char *)Owner->Objects[
          APPLE_AGX_RENDER_TA_FIRMWARE_STAMP_OBJECT].CpuAddress +
          Owner->ObjectOffsets[APPLE_AGX_RENDER_TA_FIRMWARE_STAMP_OBJECT]) : RENDER_SHARED_NULL;
  Config->D3.FirmwareStamp = queue_object_valid(Owner,
      APPLE_AGX_RENDER_3D_FIRMWARE_STAMP_OBJECT, 4u, Prepared) ?
      (volatile APPLE_AGX_U32 *)((unsigned char *)Owner->Objects[
          APPLE_AGX_RENDER_3D_FIRMWARE_STAMP_OBJECT].CpuAddress +
          Owner->ObjectOffsets[APPLE_AGX_RENDER_3D_FIRMWARE_STAMP_OBJECT]) : RENDER_SHARED_NULL;
  Config->Compute.FirmwareStamp = queue_object_valid(Owner,
      APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR, 0x10000u, Prepared) ?
      (volatile APPLE_AGX_U32 *)((unsigned char *)Owner->Objects[
          APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR].CpuAddress +
          APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET) : RENDER_SHARED_NULL;
  Config->StampCompletion = Owner->StampCompletion;
  Config->AbortFaultSuffix = Owner->ManagerDepth2;
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
