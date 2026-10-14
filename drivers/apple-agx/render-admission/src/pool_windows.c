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
 * adapter runs on two segments.
 *
 * M2-f3 (investigation/analysis/M2-f3-demand-pool.md): when the window maps
 * ranges (AGX_POOL_WINDOW_STATUS_RANGES) segment 3 starts empty and is backed
 * one 64 KiB granule at a time (render_pool_demand.c): committed before the
 * first PTE that references it is published, released when nothing can
 * reach it any more. The CPU view then maps the window IPA itself. */
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

static ULONG AdmissionPoolWindowRange(ADMISSION_CONTEXT *Context,
                                      ULONG Command, ULONGLONG WindowOffset,
                                      ULONGLONG SourceIpa, ULONGLONG Bytes) {
  WRITE_REGISTER_ULONG64(
      (volatile ULONG64 *)AdmissionPoolWindowRegister(
          Context, AGX_POOL_WINDOW_REG_WINDOW_OFFSET), WindowOffset);
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
                                       ADMISSION_POOL_ATTEMPT *Window,
                                       BOOLEAN *Ranges) {
  ULONGLONG base, capacity;
  ULONG status;
  Window->Status = ADMISSION_POOL_WINDOW_ABSENT;
  *Ranges = FALSE;
  if (Context->BrokerBase == NULL ||
      AdmissionPoolWindowRead32(Context, AGX_POOL_WINDOW_REG_MAGIC) !=
          AGX_POOL_WINDOW_MAGIC ||
      AdmissionPoolWindowRead32(Context, AGX_POOL_WINDOW_REG_VERSION) !=
          AGX_POOL_WINDOW_VERSION)
    return FALSE;
  status = AdmissionPoolWindowRead32(Context, AGX_POOL_WINDOW_REG_STATUS);
  if (!(status & AGX_POOL_WINDOW_STATUS_AVAILABLE)) return FALSE;
  *Ranges = (status & AGX_POOL_WINDOW_STATUS_RANGES) != 0u;
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

/* M2-f3 demand runtime. One FAST_MUTEX guards the granule state, the MDL
 * per granule and the window range commands. */
typedef struct _ADMISSION_POOL_RUNTIME {
  FAST_MUTEX Lock;
  ADMISSION_CONTEXT *Context;
  ADMISSION_POOL_DEMAND Demand;
  ADMISSION_POOL_GRANULE *Granules;
  ULONGLONG *Candidates;
  PMDL *Mdls;
  ULONGLONG CommitUs, CommitUsMax, ReleaseUs, ReleaseUsMax;
  ULONG LastWindowResult;
  /* Receipt staging: committed granules and failure total last staged. */
  ULONGLONG StagedGranules, StagedFailures;
} ADMISSION_POOL_RUNTIME;

static ULONGLONG AdmissionPoolElapsedUs(LARGE_INTEGER Start) {
  LARGE_INTEGER frequency, end = KeQueryPerformanceCounter(&frequency);
  if (frequency.QuadPart <= 0) return 0ULL;
  return (ULONGLONG)(end.QuadPart - Start.QuadPart) * 1000000ULL /
         (ULONGLONG)frequency.QuadPart;
}

/* Fresh zeroed Windows pages, one 64 KiB-aligned physically contiguous
 * block (EXP1246), mapped at the granule's window offset. */
static int AdmissionPoolCommitGranule(void *Opaque, unsigned long long Index) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  PHYSICAL_ADDRESS low, high, skip;
  LARGE_INTEGER start = KeQueryPerformanceCounter(NULL);
  const PFN_NUMBER *frames;
  ULONGLONG pa, us;
  ULONG i;
  PMDL mdl;
  low.QuadPart = 0;
  high.QuadPart = (LONGLONG)(ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL);
  skip.QuadPart = (LONGLONG)ADMISSION_POOL_DEMAND_GRANULE;
  mdl = MmAllocatePagesForMdlEx(low, high, skip,
      (SIZE_T)ADMISSION_POOL_DEMAND_GRANULE, MmWriteCombined,
      MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
  if (mdl == NULL) return 0;
  frames = MmGetMdlPfnArray(mdl);
  pa = (ULONGLONG)frames[0] << PAGE_SHIFT;
  if (MmGetMdlByteCount(mdl) != ADMISSION_POOL_DEMAND_GRANULE ||
      (pa & (ADMISSION_POOL_DEMAND_GRANULE - 1ULL)) ||
      pa > ADMISSION_HVC_PHYSICAL_LIMIT - ADMISSION_POOL_DEMAND_GRANULE)
    goto Free;
  for (i = 1u; i < ADMISSION_POOL_DEMAND_GRANULE / PAGE_SIZE; ++i)
    if (frames[i] != frames[0] + i) goto Free;
  pool->LastWindowResult = AdmissionPoolWindowRange(pool->Context,
      AGX_POOL_WINDOW_CMD_MAP_AT, Index * ADMISSION_POOL_DEMAND_GRANULE, pa,
      ADMISSION_POOL_DEMAND_GRANULE);
  if (pool->LastWindowResult != AgxPoolWindowOk) goto Free;
  pool->Mdls[Index] = mdl;
  us = AdmissionPoolElapsedUs(start);
  pool->CommitUs += us;
  if (us > pool->CommitUsMax) pool->CommitUsMax = us;
  return 1;
Free:
  /* MAP_AT is all or nothing: a refused page was never aliased. */
  MmFreePagesFromMdl(mdl);
  ExFreePool(mdl);
  return 0;
}

static ADMISSION_POOL_RELEASE AdmissionPoolReleaseGranule(
    void *Opaque, unsigned long long Index) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  LARGE_INTEGER start = KeQueryPerformanceCounter(NULL);
  ULONGLONG us;
  ULONG result;
  if (pool->Mdls[Index] == NULL) return AdmissionPoolReleaseFailed;
  /* Executed records' write-combined stores complete before the unmap. */
  KeMemoryBarrier();
  result = AdmissionPoolWindowRange(pool->Context,
      AGX_POOL_WINDOW_CMD_UNMAP_RANGE, Index * ADMISSION_POOL_DEMAND_GRANULE,
      0ULL, ADMISSION_POOL_DEMAND_GRANULE);
  if (result == AgxPoolWindowBusy) return AdmissionPoolReleaseBusy;
  pool->LastWindowResult = result;
  if (result != AgxPoolWindowOk) return AdmissionPoolReleaseFailed;
  MmFreePagesFromMdl(pool->Mdls[Index]);
  ExFreePool(pool->Mdls[Index]);
  pool->Mdls[Index] = NULL;
  us = AdmissionPoolElapsedUs(start);
  pool->ReleaseUs += us;
  if (us > pool->ReleaseUsMax) pool->ReleaseUsMax = us;
  return AdmissionPoolReleased;
}

_Use_decl_annotations_ ADMISSION_POOL_DEMAND *AdmissionMemoryPoolLock(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_RUNTIME *pool;
  if (Context == NULL || Context->PoolDemand == NULL) return NULL;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  ExAcquireFastMutex(&pool->Lock);
  return &pool->Demand;
}

/* Stages Wom1M2PoolDemand when the committed size moved by a receipt step
 * or a failure count changed. */
_Use_decl_annotations_ VOID AdmissionMemoryPoolUnlock(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  const ADMISSION_POOL_DEMAND_STATS *stats = &pool->Demand.Stats;
  ADMISSION_POOL_DEMAND_RECEIPT receipt = {0};
  ULONGLONG failures = stats->CommitFailures + stats->ReleaseFailures +
                       stats->Unaligned + stats->RefUnderflows;
  ULONGLONG moved = stats->CommittedGranules > pool->StagedGranules ?
      stats->CommittedGranules - pool->StagedGranules :
      pool->StagedGranules - stats->CommittedGranules;
  BOOLEAN stage = moved >= ADMISSION_POOL_DEMAND_RECEIPT_STEP ||
                  failures != pool->StagedFailures;
  if (stage) {
    receipt.Version = ADMISSION_POOL_DEMAND_RECEIPT_VERSION;
    receipt.Bytes = sizeof(receipt);
    receipt.GranuleBytes = (unsigned int)ADMISSION_POOL_DEMAND_GRANULE;
    receipt.LastWindowResult = pool->LastWindowResult;
    receipt.SegmentBytes = Context->PoolBytes;
    receipt.Stats = *stats;
    receipt.CommitUs = pool->CommitUs;
    receipt.CommitUsMax = pool->CommitUsMax;
    receipt.ReleaseUs = pool->ReleaseUs;
    receipt.ReleaseUsMax = pool->ReleaseUsMax;
    receipt.Candidates = pool->Demand.Candidates;
    receipt.ExecutedSeq = pool->Demand.ExecutedSeq;
    pool->StagedGranules = stats->CommittedGranules;
    pool->StagedFailures = failures;
  }
  ExReleaseFastMutex(&pool->Lock);
  if (stage) AdmissionRecordM2PoolDemand(Context, &receipt);
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolNoteRecord(
    ADMISSION_CONTEXT *Context, ADMISSION_PAGING_RECORD *Record) {
  ADMISSION_POOL_DEMAND *demand;
  if (Context == NULL || Record == NULL) return;
  Record->Seq = (ULONGLONG)InterlockedIncrement64(&Context->PagingRecordSeq);
  demand = AdmissionMemoryPoolLock(Context);
  if (demand == NULL) return;
  if (Record->DestinationSegment == ADMISSION_MEMORY_POOL_SEGMENT &&
      Record->DestinationIpa >= Context->PoolIpa)
    AdmissionPoolDemandRecord(demand, Record->DestinationIpa - Context->PoolIpa,
                              Record->Bytes, Record->Seq);
  if (Record->Kind == AdmissionPagingVirtualTransfer &&
      Record->SourceSegment == ADMISSION_MEMORY_POOL_SEGMENT &&
      Record->SourceIpa >= Context->PoolIpa)
    AdmissionPoolDemandRecord(demand, Record->SourceIpa - Context->PoolIpa,
                              Record->Bytes, Record->Seq);
  AdmissionMemoryPoolUnlock(Context);
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolExecuted(
    ADMISSION_CONTEXT *Context, ULONGLONG Seq) {
  ADMISSION_POOL_DEMAND *demand = AdmissionMemoryPoolLock(Context);
  if (demand == NULL) return;
  AdmissionPoolDemandExecuted(demand, Seq);
  (void)AdmissionPoolDemandReclaim(demand);
  AdmissionMemoryPoolUnlock(Context);
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolForget(
    ADMISSION_CONTEXT *Context, ADMISSION_POOL_PLACEMENT *Placement) {
  ADMISSION_POOL_DEMAND *demand;
  if (Placement == NULL || Placement->Bytes == 0ULL) return;
  demand = AdmissionMemoryPoolLock(Context);
  if (demand == NULL) return;
  AdmissionPoolDemandUnplace(demand, Placement);
  AdmissionMemoryPoolUnlock(Context);
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolReclaim(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_DEMAND *demand = AdmissionMemoryPoolLock(Context);
  if (demand == NULL) return;
  (void)AdmissionPoolDemandReclaim(demand);
  AdmissionMemoryPoolUnlock(Context);
}

static VOID AdmissionPoolDemandFree(ADMISSION_POOL_RUNTIME *Pool) {
  if (Pool->Mdls) ExFreePoolWithTag(Pool->Mdls, ADMISSION_POOL_TAG);
  if (Pool->Candidates) ExFreePoolWithTag(Pool->Candidates, ADMISSION_POOL_TAG);
  if (Pool->Granules) ExFreePoolWithTag(Pool->Granules, ADMISSION_POOL_TAG);
  ExFreePoolWithTag(Pool, ADMISSION_POOL_TAG);
}

/* Window pages leave before their pages return to Windows; a refused
 * UNMAP_ALL keeps every page allocated (PoolLeaks). */
static VOID AdmissionPoolDemandStop(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  ULONGLONG i;
  BOOLEAN unmapped;
  if (pool == NULL) return;
  Context->PoolDemand = NULL;
  if (Context->PoolCpu != NULL)
    MmUnmapIoSpace(Context->PoolCpu, (SIZE_T)Context->PoolBytes);
  unmapped = Context->BrokerBase != NULL &&
      AdmissionPoolWindowCommand(Context, AGX_POOL_WINDOW_CMD_UNMAP_ALL, 0ULL,
                                 0ULL) == AgxPoolWindowOk;
  for (i = 0ULL; i < pool->Demand.Count; ++i) {
    if (pool->Mdls[i] == NULL) continue;
    if (!unmapped) {
      InterlockedIncrement(&Context->PoolLeaks);
      continue;
    }
    MmFreePagesFromMdl(pool->Mdls[i]);
    ExFreePool(pool->Mdls[i]);
  }
  AdmissionPoolDemandFree(pool);
  Context->PoolCpu = NULL;
  Context->PoolIpa = 0ULL;
  Context->PoolBytes = 0ULL;
}

/* An empty segment over the window; FALSE leaves no state behind. */
static BOOLEAN AdmissionPoolDemandStart(ADMISSION_CONTEXT *Context,
                                        ADMISSION_POOL_ATTEMPT *Window) {
  ADMISSION_POOL_DEMAND_OPS ops;
  ADMISSION_POOL_RUNTIME *pool;
  ULONGLONG count = ADMISSION_MEMORY_POOL_BYTES / ADMISSION_POOL_DEMAND_GRANULE;
  PHYSICAL_ADDRESS base;
  pool = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*pool), ADMISSION_POOL_TAG);
  if (pool == NULL) return FALSE;
  pool->Granules = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      (SIZE_T)(count * sizeof(*pool->Granules)), ADMISSION_POOL_TAG);
  pool->Candidates = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      (SIZE_T)(count * sizeof(*pool->Candidates)), ADMISSION_POOL_TAG);
  pool->Mdls = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      (SIZE_T)(count * sizeof(*pool->Mdls)), ADMISSION_POOL_TAG);
  ops.Context = pool;
  ops.Commit = AdmissionPoolCommitGranule;
  ops.Release = AdmissionPoolReleaseGranule;
  if (pool->Granules == NULL || pool->Candidates == NULL || pool->Mdls == NULL ||
      !AdmissionPoolDemandInit(&pool->Demand, pool->Granules, pool->Candidates,
                               count, &ops)) {
    AdmissionPoolDemandFree(pool);
    return FALSE;
  }
  ExInitializeFastMutex(&pool->Lock);
  pool->Context = Context;
  base.QuadPart = (LONGLONG)Window->SkipBytes;
  Context->PoolCpu = MmMapIoSpaceEx(base, (SIZE_T)ADMISSION_MEMORY_POOL_BYTES,
                                    PAGE_READWRITE | PAGE_WRITECOMBINE);
  if (Context->PoolCpu == NULL) {
    Window->Status = 5u;
    AdmissionPoolDemandFree(pool);
    return FALSE;
  }
  Context->PoolIpa = Window->SkipBytes;
  Context->PoolBytes = ADMISSION_MEMORY_POOL_BYTES;
  Context->PoolDemand = pool;
  Window->Status = ADMISSION_POOL_WINDOW_DEMAND;
  Window->RequestedBytes = ADMISSION_MEMORY_POOL_BYTES;
  Window->ObtainedBytes = 0ULL;
  return TRUE;
}

static VOID AdmissionMemoryPoolRelease(ADMISSION_CONTEXT *Context) {
  BOOLEAN aliased = Context->PoolIpa != 0ULL;
  if (Context->PoolDemand != NULL) {
    AdmissionPoolDemandStop(Context);
    return;
  }
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
  BOOLEAN ranges;
  if (Context == NULL || Context->PoolMdl != NULL ||
      Context->PoolDemand != NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !AdmissionMemoryReady(&Context->Memory))
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = ADMISSION_POOL_PROBE_VERSION;
  receipt.Bytes = sizeof(receipt);
  receipt.Count = ADMISSION_POOL_ATTEMPT_WINDOW + 1u;
  if (!AdmissionPoolWindowOpen(Context, window, &ranges)) {
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  if (ranges) {
    if (AdmissionPoolDemandStart(Context, window) &&
        AppleAgxPhysicalTopologyAddPool(&Context->Memory.Topology,
            ADMISSION_MEMORY_POOL_GPU_VA, Context->PoolBytes) !=
            AppleAgxPhysicalTopologyOk) {
      window->Status = 6u;
      AdmissionMemoryPoolRelease(Context);
    }
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  /* EXP1251 diagnostic: ADMISSION_POOL_DIAG_CHUNKS_ONLY skips the block so
   * the 64 KiB-block fallback runs on a boot where Windows would grant one
   * block. Production builds never define it. */
#if !defined(ADMISSION_POOL_DIAG_CHUNKS_ONLY)
  mdl = AdmissionPoolAllocate(0ULL,
                              &receipt.Attempts[ADMISSION_POOL_ATTEMPT_BLOCK]);
#else
  mdl = NULL;
#endif
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
