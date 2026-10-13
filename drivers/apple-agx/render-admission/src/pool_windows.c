#include "render_admission.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* M2-f2: segment 3 is a CPU-visible memory segment populated from Windows
 * memory this driver allocates at start (DXGK_SEGMENTFLAGS.
 * PopulatedFromSystemMemory). VidMm-managed system memory cannot back
 * 16 KiB UAT pages (EXP1244). Windows grants one physically contiguous 1 GiB
 * block only sometimes (EXP1246/EXP1247 yes, EXP1248 no), 64 KiB-aligned
 * 64 KiB blocks always (EXP1246; MmAllocatePagesForMdlEx SkipBytes 0x10000
 * with MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS: every block exactly SkipBytes
 * long and SkipBytes-aligned). The hypervisor presents the pages, in MDL
 * order, as one contiguous guest-IPA window (apple_agx_pool_window_abi.h):
 * the segment's CpuTranslatedAddress and the IPA the GPU broker resolves.
 * Window offset k and byte k of the MDL are the same page, so the kernel
 * write-combined MDL mapping is the CPU view. Pages are zeroed (they reach
 * user mode through CPU-visible allocations). Without a window or pages the
 * adapter runs on two segments. */
#define ADMISSION_MEMORY_POOL_BYTES 0x40000000ULL
#define ADMISSION_MEMORY_POOL_GPU_VA 0x1700000000ULL
#define ADMISSION_MEMORY_POOL_CHUNK 0x10000ULL
/* One MAP_APPEND covers at most 16 MiB: the hypervisor validates every page
 * under its broker lock while this vCPU waits. */
#define ADMISSION_MEMORY_POOL_MAP_STEP 0x1000000ULL

C_ASSERT(sizeof(PFN_NUMBER) == sizeof(unsigned long long));
C_ASSERT(ADMISSION_MEMORY_POOL_CHUNK % AGX_POOL_WINDOW_PAGE == 0ULL);

static volatile UCHAR *AdmissionPoolWindowRegister(ADMISSION_CONTEXT *Context,
                                                   ULONG Offset) {
  return Context->BrokerBase + AGX_POOL_WINDOW_OFFSET + Offset;
}

static ULONG AdmissionPoolWindowRead32(ADMISSION_CONTEXT *Context, ULONG Offset) {
  return READ_REGISTER_ULONG(
      (volatile ULONG *)AdmissionPoolWindowRegister(Context, Offset));
}

static ULONGLONG AdmissionPoolWindowRead64(ADMISSION_CONTEXT *Context,
                                           ULONG Offset) {
  return READ_REGISTER_ULONG64(
      (volatile ULONG64 *)AdmissionPoolWindowRegister(Context, Offset));
}

static ULONG AdmissionPoolWindowCommand(ADMISSION_CONTEXT *Context,
                                        ULONG Command, ULONGLONG SourceIpa,
                                        ULONGLONG Bytes) {
  WRITE_REGISTER_ULONG64(
      (volatile ULONG64 *)AdmissionPoolWindowRegister(
          Context, AGX_POOL_WINDOW_REG_SOURCE_IPA), SourceIpa);
  WRITE_REGISTER_ULONG64(
      (volatile ULONG64 *)AdmissionPoolWindowRegister(
          Context, AGX_POOL_WINDOW_REG_SOURCE_BYTES), Bytes);
  WRITE_REGISTER_ULONG(
      (volatile ULONG *)AdmissionPoolWindowRegister(
          Context, AGX_POOL_WINDOW_REG_COMMAND), Command);
  return AdmissionPoolWindowRead32(Context, AGX_POOL_WINDOW_REG_RESULT);
}

/* The window's base and capacity, empty: an earlier instance that did not
 * stop (or a stale map) is unmapped first. */
static BOOLEAN AdmissionPoolWindowOpen(ADMISSION_CONTEXT *Context,
                                       ADMISSION_POOL_ATTEMPT *Window) {
  ULONGLONG base, capacity;
  Window->Status = ADMISSION_POOL_WINDOW_ABSENT;
  if (Context->BrokerBase == NULL ||
      AdmissionPoolWindowRead32(Context, AGX_POOL_WINDOW_REG_MAGIC) !=
          AGX_POOL_WINDOW_MAGIC ||
      AdmissionPoolWindowRead32(Context, AGX_POOL_WINDOW_REG_VERSION) !=
          AGX_POOL_WINDOW_VERSION ||
      !(AdmissionPoolWindowRead32(Context, AGX_POOL_WINDOW_REG_STATUS) &
        AGX_POOL_WINDOW_STATUS_AVAILABLE))
    return FALSE;
  base = AdmissionPoolWindowRead64(Context, AGX_POOL_WINDOW_REG_IPA);
  capacity = AdmissionPoolWindowRead64(Context, AGX_POOL_WINDOW_REG_CAPACITY);
  Window->SkipBytes = base;
  Window->RequestedBytes = capacity;
  if (base == 0ULL || (base & (ADMISSION_MEMORY_POOL_CHUNK - 1ULL)) ||
      capacity < ADMISSION_MEMORY_POOL_BYTES ||
      base > ADMISSION_HVC_PHYSICAL_LIMIT - capacity)
    return FALSE;
  if (AdmissionPoolWindowRead64(Context, AGX_POOL_WINDOW_REG_MAPPED) != 0ULL) {
    Window->Misaligned = AdmissionPoolWindowCommand(
        Context, AGX_POOL_WINDOW_CMD_UNMAP_ALL, 0ULL, 0ULL);
    if (Window->Misaligned != AgxPoolWindowOk) {
      Window->Status = ADMISSION_POOL_WINDOW_STALE;
      return FALSE;
    }
  }
  return TRUE;
}

static PMDL AdmissionPoolAllocate(ULONGLONG SkipBytes,
                                  ADMISSION_POOL_ATTEMPT *Attempt) {
  PHYSICAL_ADDRESS low, high, skip;
  LARGE_INTEGER frequency, start, end;
  PMDL mdl;
  Attempt->SkipBytes = SkipBytes;
  Attempt->RequestedBytes = ADMISSION_MEMORY_POOL_BYTES;
  low.QuadPart = 0;
  high.QuadPart = (LONGLONG)(ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL);
  skip.QuadPart = (LONGLONG)SkipBytes;
  start = KeQueryPerformanceCounter(&frequency);
  mdl = MmAllocatePagesForMdlEx(low, high, skip,
      (SIZE_T)ADMISSION_MEMORY_POOL_BYTES, MmWriteCombined,
      MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
  end = KeQueryPerformanceCounter(NULL);
  if (frequency.QuadPart > 0) {
    ULONGLONG us = (ULONGLONG)(end.QuadPart - start.QuadPart) * 1000000ULL /
                   (ULONGLONG)frequency.QuadPart;
    Attempt->ElapsedUs = us > 0xffffffffULL ? 0xffffffffu : (ULONG)us;
  }
  if (mdl == NULL) {
    Attempt->Status = 2u;
    return NULL;
  }
  AdmissionPoolAnalyzeFrames(
      (const unsigned long long *)MmGetMdlPfnArray(mdl),
      MmGetMdlByteCount(mdl) / PAGE_SIZE, Attempt);
  Attempt->Status =
      MmGetMdlByteCount(mdl) >= ADMISSION_MEMORY_POOL_BYTES ? 1u : 3u;
  /* Every run must start on a 64 KiB boundary (64 KB PTEs, window pages)
   * and lie below the HVC limit; one block is not aligned by contract. */
  if (Attempt->Status != 1u ||
      (Attempt->LowestPhysical & (ADMISSION_MEMORY_POOL_CHUNK - 1ULL)) ||
      Attempt->HighestPhysical > ADMISSION_HVC_PHYSICAL_LIMIT ||
      Attempt->Misaligned != 0u) {
    if (Attempt->Status == 1u) Attempt->Status = 4u;
    MmFreePagesFromMdl(mdl);
    ExFreePool(mdl);
    return NULL;
  }
  return mdl;
}

/* Maps the MDL's physical runs, in order, from the window base. On failure
 * the caller's release unmaps whatever was mapped. */
static BOOLEAN AdmissionPoolWindowMap(ADMISSION_CONTEXT *Context, PMDL Mdl,
                                      ADMISSION_POOL_ATTEMPT *Window) {
  const PFN_NUMBER *frames = MmGetMdlPfnArray(Mdl);
  ULONGLONG count = MmGetMdlByteCount(Mdl) / PAGE_SIZE, i = 0ULL;
  LARGE_INTEGER frequency, start, end;
  start = KeQueryPerformanceCounter(&frequency);
  while (i < count && Window->Misaligned == AgxPoolWindowOk) {
    ULONGLONG first = i, limit = ADMISSION_MEMORY_POOL_MAP_STEP / PAGE_SIZE;
    while (i + 1ULL < count && i + 1ULL - first < limit &&
           frames[i + 1ULL] == frames[i] + 1ULL)
      ++i;
    ++i;
    Window->Misaligned = AdmissionPoolWindowCommand(
        Context, AGX_POOL_WINDOW_CMD_MAP_APPEND,
        (ULONGLONG)frames[first] << PAGE_SHIFT, (i - first) * PAGE_SIZE);
    ++Window->Runs;
  }
  end = KeQueryPerformanceCounter(NULL);
  if (frequency.QuadPart > 0) {
    ULONGLONG us = (ULONGLONG)(end.QuadPart - start.QuadPart) * 1000000ULL /
                   (ULONGLONG)frequency.QuadPart;
    Window->ElapsedUs = us > 0xffffffffULL ? 0xffffffffu : (ULONG)us;
  }
  Window->ObtainedBytes =
      AdmissionPoolWindowRead64(Context, AGX_POOL_WINDOW_REG_MAPPED);
  if (Window->Misaligned == AgxPoolWindowOk &&
      Window->ObtainedBytes == ADMISSION_MEMORY_POOL_BYTES) {
    Window->Status = ADMISSION_POOL_WINDOW_MAPPED;
    return TRUE;
  }
  Window->Status = ADMISSION_POOL_WINDOW_REFUSED;
  return FALSE;
}

static VOID AdmissionMemoryPoolRelease(ADMISSION_CONTEXT *Context) {
  BOOLEAN aliased = Context->PoolIpa != 0ULL;
  if (Context->PoolCpu != NULL && Context->PoolMdl != NULL)
    MmUnmapLockedPages(Context->PoolCpu, Context->PoolMdl);
  /* Pages leave the window before they return to Windows; a refused
   * unmap (the GPU broker still holds a page) keeps them allocated. */
  if (aliased &&
      (Context->BrokerBase == NULL ||
       AdmissionPoolWindowCommand(Context, AGX_POOL_WINDOW_CMD_UNMAP_ALL,
                                  0ULL, 0ULL) != AgxPoolWindowOk))
    InterlockedIncrement(&Context->PoolLeaks);
  else if (Context->PoolMdl != NULL) {
    MmFreePagesFromMdl(Context->PoolMdl);
    ExFreePool(Context->PoolMdl);
  }
  Context->PoolMdl = NULL;
  Context->PoolCpu = NULL;
  Context->PoolIpa = 0ULL;
  Context->PoolBytes = 0ULL;
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolStart(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_PROBE receipt;
  ADMISSION_POOL_ATTEMPT *window =
      &receipt.Attempts[ADMISSION_POOL_ATTEMPT_WINDOW];
  PMDL mdl;
  if (Context == NULL || Context->PoolMdl != NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !AdmissionMemoryReady(&Context->Memory))
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = ADMISSION_POOL_PROBE_VERSION;
  receipt.Bytes = sizeof(receipt);
  receipt.Count = ADMISSION_POOL_ATTEMPT_WINDOW + 1u;
  if (!AdmissionPoolWindowOpen(Context, window)) {
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  mdl = AdmissionPoolAllocate(0ULL,
                              &receipt.Attempts[ADMISSION_POOL_ATTEMPT_BLOCK]);
  if (mdl == NULL)
    mdl = AdmissionPoolAllocate(
        ADMISSION_MEMORY_POOL_CHUNK,
        &receipt.Attempts[ADMISSION_POOL_ATTEMPT_CHUNKS]);
  if (mdl == NULL) {
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  Context->PoolMdl = mdl;
  /* From here the pages may be aliased: only the release path frees them. */
  Context->PoolIpa = window->SkipBytes;
  if (!AdmissionPoolWindowMap(Context, mdl, window)) {
    AdmissionMemoryPoolRelease(Context);
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  Context->PoolCpu = MmMapLockedPagesSpecifyCache(mdl, KernelMode,
      MmWriteCombined, NULL, FALSE, NormalPagePriority | MdlMappingNoExecute);
  if (Context->PoolCpu == NULL) {
    window->Status = 5u;
    AdmissionMemoryPoolRelease(Context);
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  Context->PoolBytes = ADMISSION_MEMORY_POOL_BYTES;
  if (AppleAgxPhysicalTopologyAddPool(&Context->Memory.Topology,
          ADMISSION_MEMORY_POOL_GPU_VA, Context->PoolBytes) !=
      AppleAgxPhysicalTopologyOk) {
    window->Status = 6u;
    AdmissionMemoryPoolRelease(Context);
  }
  AdmissionRecordM2PoolProbe(Context, &receipt);
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolStop(ADMISSION_CONTEXT *Context) {
  if (Context == NULL) return;
  AdmissionMemoryPoolRelease(Context);
  RtlZeroMemory(&Context->Memory.Topology.Pool,
                sizeof(Context->Memory.Topology.Pool));
  if (Context->Memory.Topology.SegmentCount == 3u)
    Context->Memory.Topology.SegmentCount = 2u;
}

_Use_decl_annotations_ BOOLEAN AdmissionMemoryPoolView(
    const ADMISSION_CONTEXT *Context, ADMISSION_SCANOUT_MEMORY_VIEW *View) {
  if (Context == NULL || View == NULL) return FALSE;
  RtlZeroMemory(View, sizeof(*View));
  if (Context->PoolCpu == NULL || Context->PoolBytes == 0ULL ||
      Context->PoolIpa == 0ULL || Context->Memory.Topology.Pool.Id != 3u)
    return FALSE;
  View->CpuAddress = Context->PoolCpu;
  View->GuestIpaAddress = Context->PoolIpa;
  View->GpuVirtualAddress = Context->Memory.Topology.Pool.Base;
  View->Bytes = Context->PoolBytes;
  return TRUE;
}
#endif
