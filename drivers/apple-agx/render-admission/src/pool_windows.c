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

/* M2-f3b: a releasable granule stays committed until it has been idle for
 * this long (EXP1257: VidMm reuses freed ranges at once; releasing them at
 * once cost ~8k commits and releases, 6.5 s, per CS benchmark). A periodic
 * reclaim returns idle granules to Windows when nothing else runs. */
#define ADMISSION_POOL_RELEASE_DELAY_MS 2000ULL
#define ADMISSION_POOL_RECLAIM_PERIOD_MS 500LL

/* M2-f3 demand runtime. One FAST_MUTEX guards the granule state, the MDL
 * per granule and the window range commands. */
typedef struct _ADMISSION_POOL_RUNTIME {
  FAST_MUTEX Lock;
  /* Periodic aged reclaim: timer -> DPC -> work item (PASSIVE). */
  KTIMER Timer;
  KDPC Dpc;
  PIO_WORKITEM Work;
  volatile LONG TimerArmed, WorkQueued, WorkPending, Stopping;
  ADMISSION_CONTEXT *Context;
  ADMISSION_POOL_DEMAND Demand;
  ADMISSION_POOL_GRANULE *Granules;
  ULONGLONG *Candidates;
  /* M2-f3e: pages come in 2 MiB slabs of 32 granule slots. */
  ADMISSION_POOL_SLABS Slabs;
  ADMISSION_POOL_SLAB **SlabOf; /* per granule: its slab, or NULL */
  UCHAR *SlotOf;                /* per granule: its slot */
  ULONGLONG SlabContiguous, SlabChunked;
  /* Scratch for one commit run (under Lock). */
  ADMISSION_POOL_SLAB *RunSlab[1024];
  unsigned int RunSlot[1024];
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

/* Most granules one commit run takes (64 MiB): bounds the scratch arrays and
 * the time one UpdatePageTable spends here. */
#define ADMISSION_POOL_MAX_RUN 1024ULL
#define ADMISSION_POOL_SLAB_BYTES \
  ((ULONGLONG)ADMISSION_POOL_SLAB_SLOTS * ADMISSION_POOL_DEMAND_GRANULE)
/* Empty slabs kept for the next commit. */
#define ADMISSION_POOL_SPARE_SLABS 1ULL
C_ASSERT(ADMISSION_POOL_MAX_RUN <= 1024ULL);

/* A 2 MiB slab of fresh zeroed pages: one contiguous 2 MiB chunk when
 * Windows has one (EXP1259: one contiguous 1 GiB took 374 ms), else 32
 * 64 KiB-aligned 64 KiB chunks (EXP1246). */
static ADMISSION_POOL_SLAB *AdmissionPoolNewSlab(void *Opaque) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  const ULONGLONG pages = ADMISSION_POOL_DEMAND_GRANULE / PAGE_SIZE;
  PHYSICAL_ADDRESS low, high, skip;
  ADMISSION_POOL_SLAB *slab;
  const PFN_NUMBER *frames;
  ULONG attempt;
  slab = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*slab), ADMISSION_POOL_TAG);
  if (slab == NULL) return NULL;
  low.QuadPart = 0;
  high.QuadPart = (LONGLONG)(ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL);
  for (attempt = 0u; attempt < 2u; ++attempt) {
    ULONGLONG i, j;
    PMDL mdl;
    skip.QuadPart = (LONGLONG)(attempt == 0u ? ADMISSION_POOL_SLAB_BYTES
                                             : ADMISSION_POOL_DEMAND_GRANULE);
    mdl = MmAllocatePagesForMdlEx(low, high, skip,
        (SIZE_T)ADMISSION_POOL_SLAB_BYTES, MmWriteCombined,
        MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
    if (mdl == NULL) continue;
    frames = MmGetMdlPfnArray(mdl);
    if (MmGetMdlByteCount(mdl) != ADMISSION_POOL_SLAB_BYTES) goto Next;
    for (i = 0ULL; i < ADMISSION_POOL_SLAB_SLOTS; ++i) {
      ULONGLONG pa = (ULONGLONG)frames[i * pages] << PAGE_SHIFT;
      if ((pa & (ADMISSION_POOL_DEMAND_GRANULE - 1ULL)) ||
          pa > ADMISSION_HVC_PHYSICAL_LIMIT - ADMISSION_POOL_DEMAND_GRANULE)
        goto Next;
      for (j = 1ULL; j < pages; ++j)
        if (frames[i * pages + j] != frames[i * pages] + j) goto Next;
      slab->Pa[i] = pa;
    }
    slab->Handle = mdl;
    if (attempt == 0u) ++pool->SlabContiguous;
    else ++pool->SlabChunked;
    return slab;
Next:
    MmFreePagesFromMdl(mdl);
    ExFreePool(mdl);
  }
  ExFreePoolWithTag(slab, ADMISSION_POOL_TAG);
  return NULL;
}

static VOID AdmissionPoolFreeSlab(void *Opaque, ADMISSION_POOL_SLAB *Slab) {
  PMDL mdl = (PMDL)Slab->Handle;
  UNREFERENCED_PARAMETER(Opaque);
  MmFreePagesFromMdl(mdl);
  ExFreePool(mdl);
  ExFreePoolWithTag(Slab, ADMISSION_POOL_TAG);
}

/* Granules [First, First + Count) take slab slots and are mapped (adjacent
 * slots of adjacent granules in one MAP_AT). All or nothing. */
static int AdmissionPoolCommitRun(void *Opaque, unsigned long long First,
                                  unsigned long long Count) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  LARGE_INTEGER start = KeQueryPerformanceCounter(NULL);
  ULONGLONG i, mapped = 0ULL, us;
  if (Count == 0ULL || Count > ADMISSION_POOL_MAX_RUN ||
      !AdmissionPoolSlabTake(&pool->Slabs, Count, pool->RunSlab, pool->RunSlot))
    return 0;
  while (mapped < Count) {
    ULONGLONG run = 1ULL;
    ULONGLONG pa = pool->RunSlab[mapped]->Pa[pool->RunSlot[mapped]];
    while (mapped + run < Count &&
           pool->RunSlab[mapped + run]->Pa[pool->RunSlot[mapped + run]] ==
               pa + run * ADMISSION_POOL_DEMAND_GRANULE)
      ++run;
    pool->LastWindowResult = AdmissionPoolWindowRange(pool->Context,
        AGX_POOL_WINDOW_CMD_MAP_AT, (First + mapped) * ADMISSION_POOL_DEMAND_GRANULE,
        pa, run * ADMISSION_POOL_DEMAND_GRANULE);
    if (pool->LastWindowResult != AgxPoolWindowOk) goto Unmap;
    mapped += run;
  }
  for (i = 0ULL; i < Count; ++i) {
    pool->SlabOf[First + i] = pool->RunSlab[i];
    pool->SlotOf[First + i] = (UCHAR)pool->RunSlot[i];
  }
  us = AdmissionPoolElapsedUs(start);
  pool->CommitUs += us;
  if (us > pool->CommitUsMax) pool->CommitUsMax = us;
  return 1;
Unmap:
  /* Each MAP_AT was all or nothing: undo the spans that were mapped. */
  if (mapped != 0ULL &&
      AdmissionPoolWindowRange(pool->Context, AGX_POOL_WINDOW_CMD_UNMAP_RANGE,
          First * ADMISSION_POOL_DEMAND_GRANULE, 0ULL,
          mapped * ADMISSION_POOL_DEMAND_GRANULE) != AgxPoolWindowOk) {
    /* Still aliased: these slots never return (their slab stays held). */
    InterlockedIncrement(&pool->Context->PoolLeaks);
    for (i = mapped; i < Count; ++i)
      AdmissionPoolSlabGive(&pool->Slabs, pool->RunSlab[i], pool->RunSlot[i]);
    return 0;
  }
  for (i = 0ULL; i < Count; ++i)
    AdmissionPoolSlabGive(&pool->Slabs, pool->RunSlab[i], pool->RunSlot[i]);
  return 0;
}

static ADMISSION_POOL_RELEASE AdmissionPoolReleaseGranule(
    void *Opaque, unsigned long long Index) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  LARGE_INTEGER start = KeQueryPerformanceCounter(NULL);
  ADMISSION_POOL_SLAB *slab = pool->SlabOf[Index];
  ULONGLONG us;
  ULONG result;
  if (slab == NULL) return AdmissionPoolReleaseFailed;
  /* Executed records' write-combined stores complete before the unmap. */
  KeMemoryBarrier();
  result = AdmissionPoolWindowRange(pool->Context,
      AGX_POOL_WINDOW_CMD_UNMAP_RANGE, Index * ADMISSION_POOL_DEMAND_GRANULE,
      0ULL, ADMISSION_POOL_DEMAND_GRANULE);
  if (result == AgxPoolWindowBusy) return AdmissionPoolReleaseBusy;
  pool->LastWindowResult = result;
  if (result != AgxPoolWindowOk) return AdmissionPoolReleaseFailed;
  pool->SlabOf[Index] = NULL;
  AdmissionPoolSlabGive(&pool->Slabs, slab, pool->SlotOf[Index]);
  us = AdmissionPoolElapsedUs(start);
  pool->ReleaseUs += us;
  if (us > pool->ReleaseUsMax) pool->ReleaseUsMax = us;
  return AdmissionPoolReleased;
}

static ULONGLONG AdmissionPoolNowMs(VOID) {
  return KeQueryInterruptTime() / 10000ULL;
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
    receipt.ReleaseDelayMs = pool->Demand.ReleaseDelay;
    receipt.HeldGranules = pool->Slabs.Stats.Slabs * ADMISSION_POOL_SLAB_SLOTS;
    receipt.Slabs = pool->Slabs.Stats.Slabs;
    receipt.PeakSlabs = pool->Slabs.Stats.PeakSlabs;
    receipt.NewSlabs = pool->Slabs.Stats.NewSlabs;
    receipt.NewSlabFailures = pool->Slabs.Stats.NewSlabFailures;
    receipt.FreedSlabs = pool->Slabs.Stats.FreedSlabs;
    receipt.SlabContiguous = pool->SlabContiguous;
    receipt.SlabChunked = pool->SlabChunked;
    pool->StagedGranules = stats->CommittedGranules;
    pool->StagedFailures = failures;
  }
  /* Idle granules wait for the periodic reclaim. */
  if (pool->Demand.Candidates != 0u &&
      InterlockedCompareExchange(&pool->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&pool->TimerArmed, 1, 0) == 0) {
    LARGE_INTEGER due;
    due.QuadPart = -ADMISSION_POOL_RECLAIM_PERIOD_MS * 10000LL;
    (void)KeSetTimer(&pool->Timer, due, &pool->Dpc);
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
  (void)AdmissionPoolDemandReclaimAged(demand, AdmissionPoolNowMs());
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
  (void)AdmissionPoolDemandReclaimAged(demand, AdmissionPoolNowMs());
  AdmissionMemoryPoolUnlock(Context);
}

static IO_WORKITEM_ROUTINE AdmissionPoolReclaimWork;
static VOID AdmissionPoolReclaimWork(_In_ PDEVICE_OBJECT Device,
                                     _In_opt_ PVOID Opaque) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  UNREFERENCED_PARAMETER(Device);
  if (pool == NULL) return;
  /* From here the DPC may queue the item again. */
  InterlockedExchange(&pool->WorkQueued, 0);
  if (InterlockedCompareExchange(&pool->Stopping, 0, 0) == 0)
    AdmissionMemoryPoolReclaim(pool->Context);
  InterlockedDecrement(&pool->WorkPending);
}

static KDEFERRED_ROUTINE AdmissionPoolReclaimDpc;
static VOID AdmissionPoolReclaimDpc(_In_ PKDPC Dpc, _In_opt_ PVOID Opaque,
                                    _In_opt_ PVOID Argument1,
                                    _In_opt_ PVOID Argument2) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  UNREFERENCED_PARAMETER(Dpc);
  UNREFERENCED_PARAMETER(Argument1);
  UNREFERENCED_PARAMETER(Argument2);
  if (pool == NULL) return;
  InterlockedExchange(&pool->TimerArmed, 0);
  if (InterlockedCompareExchange(&pool->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&pool->WorkQueued, 1, 0) == 0) {
    InterlockedIncrement(&pool->WorkPending);
    IoQueueWorkItem(pool->Work, AdmissionPoolReclaimWork, DelayedWorkQueue,
                    pool);
  }
}

static VOID AdmissionPoolDemandFree(ADMISSION_POOL_RUNTIME *Pool) {
  if (Pool->Work) IoFreeWorkItem(Pool->Work);
  if (Pool->SlotOf) ExFreePoolWithTag(Pool->SlotOf, ADMISSION_POOL_TAG);
  if (Pool->SlabOf) ExFreePoolWithTag(Pool->SlabOf, ADMISSION_POOL_TAG);
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
  ULONG waited;
  if (pool == NULL) return;
  /* No reclaim runs past this point: no new arm or queue, the armed timer
   * cancelled, running DPCs drained, queued work items finished. */
  InterlockedExchange(&pool->Stopping, 1);
  KeCancelTimer(&pool->Timer);
  KeFlushQueuedDpcs();
  for (waited = 0u; InterlockedCompareExchange(&pool->WorkPending, 0, 0) != 0 &&
                    waited < 10000u; ++waited) {
    LARGE_INTEGER delay;
    delay.QuadPart = -10000LL; /* 1 ms */
    (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);
  }
  if (InterlockedCompareExchange(&pool->WorkPending, 0, 0) != 0) {
    /* A work item still references the runtime: keep it (and its pages). */
    InterlockedIncrement(&Context->PoolLeaks);
    Context->PoolDemand = NULL;
    return;
  }
  Context->PoolDemand = NULL;
  if (Context->PoolCpu != NULL)
    MmUnmapIoSpace(Context->PoolCpu, (SIZE_T)Context->PoolBytes);
  unmapped = Context->BrokerBase != NULL &&
      AdmissionPoolWindowCommand(Context, AGX_POOL_WINDOW_CMD_UNMAP_ALL, 0ULL,
                                 0ULL) == AgxPoolWindowOk;
  if (unmapped) {
    /* Every window page is gone: all slabs return to Windows. */
    for (i = 0ULL; i < pool->Demand.Count; ++i) pool->SlabOf[i] = NULL;
    AdmissionPoolSlabDrain(&pool->Slabs);
  } else {
    /* Still aliased: keep every slab (and its pages) for good. */
    InterlockedIncrement(&Context->PoolLeaks);
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
  pool->SlabOf = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      (SIZE_T)(count * sizeof(*pool->SlabOf)), ADMISSION_POOL_TAG);
  pool->SlotOf = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      (SIZE_T)(count * sizeof(*pool->SlotOf)), ADMISSION_POOL_TAG);
  ops.Context = pool;
  ops.Commit = AdmissionPoolCommitRun;
  ops.Release = AdmissionPoolReleaseGranule;
  if (pool->Granules == NULL || pool->Candidates == NULL || pool->SlabOf == NULL ||
      pool->SlotOf == NULL ||
      !AdmissionPoolDemandInit(&pool->Demand, pool->Granules, pool->Candidates,
                               count, &ops)) {
    AdmissionPoolDemandFree(pool);
    return FALSE;
  }
  pool->Work = IoAllocateWorkItem(Context->PhysicalDeviceObject);
  if (pool->Work == NULL) {
    AdmissionPoolDemandFree(pool);
    return FALSE;
  }
  pool->Demand.ReleaseDelay = ADMISSION_POOL_RELEASE_DELAY_MS;
  pool->Demand.MaxRun = ADMISSION_POOL_MAX_RUN;
  pool->Slabs.Context = pool;
  pool->Slabs.NewSlab = AdmissionPoolNewSlab;
  pool->Slabs.FreeSlab = AdmissionPoolFreeSlab;
  pool->Slabs.SpareLimit = ADMISSION_POOL_SPARE_SLABS;
  ExInitializeFastMutex(&pool->Lock);
  KeInitializeTimer(&pool->Timer);
  KeInitializeDpc(&pool->Dpc, AdmissionPoolReclaimDpc, pool);
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
