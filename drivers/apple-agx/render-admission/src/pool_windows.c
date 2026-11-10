#include "render_admission.h"
#include "apple_agx_g3_tvb.h"

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
  /* M2-f6: the broker table m1n1 sized from RAM; 0 for an older one. */
  Context->PoolBrokerBytes = (status & AGX_POOL_WINDOW_STATUS_BROKER)
      ? AdmissionPoolWindowRead64(Context, AGX_POOL_WINDOW_REG_BROKER_PAGES) *
            AGX_POOL_WINDOW_PAGE
      : 0ULL;
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
  ULONGLONG RamBytes; /* what the segment size was derived from */
  /* Scratch for one commit run (under Lock). */
  ADMISSION_POOL_SLAB *RunSlab[1024];
  unsigned int RunSlot[1024];
  ULONGLONG CommitUs, CommitUsMax, ReleaseUs, ReleaseUsMax;
  ULONG LastWindowResult;
  /* Receipt staging: committed granules and failure total last staged. */
  ULONGLONG StagedGranules, StagedFailures, StagedReserved;
  ULONGLONG NativeStagingBytes, StagedNativeBytes;
  /* M2-f7: \KernelObjects\HighMemoryCondition and LowMemoryCondition
   * (referenced), or NULL. */
  PKEVENT HighMemory, LowMemory;
  ADMISSION_POOL_RESERVE_FAILURE LastReserveFailure, ReserveAttempt;
  ULONGLONG RefusalCounts[ADMISSION_POOL_REFUSAL_COUNT];
  /* rc86 split of the creation cost (receipt v11): Windows page allocation
   * (MmAllocatePagesForMdlEx, including its zeroing), the contiguity check,
   * page return, whole reservations, and TVB lease zeroing. */
  ULONGLONG SlabAllocCalls, SlabAllocUs, SlabAllocUsMax, SlabCheckUs;
  ULONGLONG SlabFreeUs, SlabFreeUsMax;
  ULONGLONG ReserveCalls, ReserveUs, ReserveUsMax;
  ULONGLONG LeaseZeroCount, LeaseZeroUs, LeaseZeroSkips;
  ULONGLONG CacheDrops;
  /* rc86: time of the last reservation; the empty-slab cache ages from it. */
  ULONGLONG LastReserveMs;
  /* rc88: background prefill of the empty-slab cache (AdmissionPoolPrefill*):
   * its work item, queued flag, the boot switch, and its cost. SlabFailUs:
   * time in MmAllocatePagesForMdlEx calls whose slab was not usable
   * (refused, short or not contiguous), all callers. */
  PIO_WORKITEM PrefillWork;
  volatile LONG PrefillQueued;
  BOOLEAN PrefillEnabled;
  /* Time of the last successful reservation of at least
   * ADMISSION_POOL_PREFILL_MIN_BYTES: the prefill follows large creations
   * (an app loading), not DWM's steady small ones. */
  ULONGLONG LastLargeReserveMs;
  ULONGLONG PrefillUs, PrefillDiscards, PrefillFailures, SlabFailUs;
  /* rc93: Wom1PoolConstructLargePages=1 lets the 2 MiB attempt construct
   * large pages (the pre-rc93 call); default FALSE (AdmissionPoolSlabAttemptFlags).
   * SlabFastMisses: 2 MiB attempts refused because Windows' large-page cache
   * was empty; SlabContiguousUs/SlabChunkedUs: time of the attempts that
   * produced a slab, by shape. */
  BOOLEAN ConstructLargePages;
  /* POOL-PROBE: REG_DWORD Wom1PoolAllocProbe=1 (AllocProbe) runs
   * AdmissionPoolAllocProbeLocked once (AllocProbeDone, under Lock) at the
   * first AdmissionPoolNewSlab slab from the 64 KiB rung. */
  BOOLEAN AllocProbe, AllocProbeDone;
  ULONGLONG SlabFastMisses, SlabContiguousUs, SlabChunkedUs;
  /* rc104: the floor wait (AdmissionMemoryPoolReserve; budget from
   * Wom1PoolPressureWaitMs), live allocations holding a segment-3 promise,
   * and receipt staging: the pressure counters last staged, the slabs last
   * staged, and a request from the periodic worker to stage exact values. */
  ADMISSION_POOL_PRESSURE Pressure;
  ULONGLONG ReservedAllocations;
  ULONGLONG StagedPressure, StagedSlabs;
  BOOLEAN StageExact;
  /* rc109: what segment 3 was sized from (receipt v15). */
  ADMISSION_POOL_GRAPHICS_POLICY Policy;
  ULONG PoolPercent, PolicyFlags;
  ULONGLONG GraphicsShareBytes, BaseSegmentBytes;
  /* rc109: granules of the default-share size; growth past them needs
   * HighMemoryCondition (AdmissionPoolMayGrow). */
  ULONGLONG BaseGranules, HeadroomRefusals;
  /* rc115: growth past BaseGranules allowed in neutral memory. */
  ULONGLONG NeutralHeadroomGranules;
} ADMISSION_POOL_RUNTIME;

/* rc88: what one AdmissionPoolAllocSlab call cost, folded into the runtime
 * counters under the pool lock (the prefill allocates without it). */
typedef struct _ADMISSION_POOL_SLAB_COST {
  ULONGLONG Calls, Us, UsMax, CheckUs, FailUs;
  ULONGLONG Contiguous, Chunked;
  ULONGLONG FastMisses, ContiguousUs, ChunkedUs;
} ADMISSION_POOL_SLAB_COST;

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
/* rc86 (EXP2015b: 3116 slabs taken from and 3030 returned to Windows for a
 * peak of 1632; one 128 MiB creation takes 64 slabs, 0.7-2.1 s): while
 * HighMemoryCondition is set and LowMemoryCondition clear, keep up to one
 * 128 MiB creation's worth of empty slabs for the next reservation. The
 * M2-f7c/RC53 early reclaim drops the whole cache as soon as HighMemory
 * clears (AdmissionPoolReclaimLocked), and the periodic worker returns it
 * to ADMISSION_POOL_SPARE_SLABS once no reservation came for
 * ADMISSION_POOL_RELEASE_DELAY_MS. Promised backing is unchanged. */
#define ADMISSION_POOL_CACHE_SLABS 64ULL
/* rc88 prefill (AdmissionPoolPrefillWork): slabs per work item, and the
 * smallest creation that starts it. */
#define ADMISSION_POOL_PREFILL_BATCH 64u
#define ADMISSION_POOL_PREFILL_MIN_BYTES 0x1000000ULL /* 16 MiB */
C_ASSERT(ADMISSION_POOL_MAX_RUN <= 1024ULL);

/* rc93: MmAllocatePagesForMdlEx flags of slab attempt `Attempt` (0: one
 * 2 MiB chunk, 1: 32 x 64 KiB chunks). A 2 MiB-aligned 2 MiB chunk is an
 * ARM64 large page. Microsoft (MmAllocatePagesForMdlEx, Flags): without
 * MM_ALLOCATE_FAST_LARGE_PAGES, once the large page cache is exhausted the
 * call "attempts to construct additional large pages, which may take a long
 * time"; with it the call is served from that cache or fails. The flag must
 * come with MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS and SkipBytes a multiple of
 * the large page size, so only attempt 0 carries it. EXP2028 arm a (first
 * app after boot): 1402 slab calls, 29.2 s, 20.8 ms each, against 0.4 ms at
 * boot and 1.7 ms for the next app reusing returned 2 MiB runs. A refused
 * attempt 0 falls back to attempt 1 as before (EXP2028: 86 chunked slabs).
 * Never MM_DONT_ZERO_ALLOCATION: the pages reach user mode. */
static ULONG AdmissionPoolSlabAttemptFlags(ULONG Attempt,
                                           BOOLEAN ConstructLargePages) {
  ULONG flags = MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED;
  if (Attempt == 0u && !ConstructLargePages) flags |= MM_ALLOCATE_FAST_LARGE_PAGES;
  return flags;
}

/* A 2 MiB slab of fresh zeroed pages: one contiguous 2 MiB chunk when
 * Windows has one (EXP1259: one contiguous 1 GiB took 374 ms), else 32
 * 64 KiB-aligned 64 KiB chunks (EXP1246). */
static ADMISSION_POOL_SLAB *AdmissionPoolAllocSlab(ADMISSION_POOL_SLAB_COST *Cost,
                                                   BOOLEAN ConstructLargePages) {
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
    /* Initialized for MSVC /W4 C4701: Next is reached only after the
     * check started, but by goto. */
    LARGE_INTEGER start = {0};
    ULONGLONG us = 0ULL;
    skip.QuadPart = (LONGLONG)(attempt == 0u ? ADMISSION_POOL_SLAB_BYTES
                                             : ADMISSION_POOL_DEMAND_GRANULE);
    start = KeQueryPerformanceCounter(NULL);
    mdl = MmAllocatePagesForMdlEx(low, high, skip,
        (SIZE_T)ADMISSION_POOL_SLAB_BYTES, MmWriteCombined,
        AdmissionPoolSlabAttemptFlags(attempt, ConstructLargePages));
    us = AdmissionPoolElapsedUs(start);
    ++Cost->Calls;
    Cost->Us += us;
    if (us > Cost->UsMax) Cost->UsMax = us;
    if (mdl == NULL) {
      Cost->FailUs += us;
      if (attempt == 0u && !ConstructLargePages) ++Cost->FastMisses;
      continue;
    }
    start = KeQueryPerformanceCounter(NULL);
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
    Cost->CheckUs += AdmissionPoolElapsedUs(start);
    slab->Handle = mdl;
    if (attempt == 0u) {
      ++Cost->Contiguous;
      Cost->ContiguousUs += us;
    } else {
      ++Cost->Chunked;
      Cost->ChunkedUs += us;
    }
    return slab;
Next:
    Cost->CheckUs += AdmissionPoolElapsedUs(start);
    Cost->FailUs += us;
    MmFreePagesFromMdl(mdl);
    ExFreePool(mdl);
  }
  ExFreePoolWithTag(slab, ADMISSION_POOL_TAG);
  return NULL;
}

/* Under the pool lock. */
static VOID AdmissionPoolAddSlabCost(ADMISSION_POOL_RUNTIME *Pool,
                                     const ADMISSION_POOL_SLAB_COST *Cost) {
  Pool->SlabAllocCalls += Cost->Calls;
  Pool->SlabAllocUs += Cost->Us;
  if (Cost->UsMax > Pool->SlabAllocUsMax) Pool->SlabAllocUsMax = Cost->UsMax;
  Pool->SlabCheckUs += Cost->CheckUs;
  Pool->SlabFailUs += Cost->FailUs;
  Pool->SlabContiguous += Cost->Contiguous;
  Pool->SlabChunked += Cost->Chunked;
  Pool->SlabFastMisses += Cost->FastMisses;
  Pool->SlabContiguousUs += Cost->ContiguousUs;
  Pool->SlabChunkedUs += Cost->ChunkedUs;
}

/* POOL-PROBE diagnostic (receipt Wom1PoolAllocProbe; layout, arm order and
 * field meanings: ADMISSION_POOL_ALLOC_PROBE_RECEIPT in render_pool_demand.h).
 * Splits the ~150 us per 64 KiB chunk of the production 64 KiB rung between
 * the chunk search and the write-combined conversion: cached against
 * write-combined 64 KiB arms, larger SkipBytes, and an unchunked
 * PREFER_CONTIGUOUS arm. Probe MDLs are only allocated, inspected through
 * their PFN arrays and freed; they are never mapped or given to the pool. */
#define ADMISSION_POOL_PROBE_CHUNKS \
  (MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED)
#define ADMISSION_POOL_PROBE_GRANULES \
  (ADMISSION_POOL_ALLOC_PROBE_PER_ARM * ADMISSION_POOL_SLAB_SLOTS)
typedef struct _ADMISSION_POOL_PROBE_ARM {
  MEMORY_CACHING_TYPE CacheType;
  ULONG SkipBytes;
  ULONG Flags;
} ADMISSION_POOL_PROBE_ARM;
static const ADMISSION_POOL_PROBE_ARM
    AdmissionPoolProbeArms[ADMISSION_POOL_ALLOC_PROBE_ARMS] = {
  {MmCached, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},         /* 0 B1 */
  {MmCached, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},         /* 1 B2 */
  {MmWriteCombined, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},  /* 2 A1 */
  {MmWriteCombined, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},  /* 3 A2 */
  {MmCached, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},         /* 4 B3 */
  {MmWriteCombined, 0x40000u, ADMISSION_POOL_PROBE_CHUNKS},  /* 5 C1 */
  {MmCached, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},         /* 6 B4 */
  {MmWriteCombined, 0x100000u, ADMISSION_POOL_PROBE_CHUNKS}, /* 7 C2 */
  {MmCached, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},         /* 8 B5 */
  {MmWriteCombined, 0u,
   MM_ALLOCATE_PREFER_CONTIGUOUS | MM_ALLOCATE_FULLY_REQUIRED}, /* 9 D1 */
  {MmCached, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},         /* 10 B6 */
  {MmWriteCombined, 0x10000u, ADMISSION_POOL_PROBE_CHUNKS},  /* 11 A3 */
};
/* Working memory, too large for the kernel stack (2 x 2 KiB of frames plus
 * the 928-byte receipt). */
typedef struct _ADMISSION_POOL_PROBE_WORK {
  ADMISSION_POOL_ALLOC_PROBE_RECEIPT Receipt;
  PMDL Mdl[ADMISSION_POOL_ALLOC_PROBE_PER_ARM];
  PFN_NUMBER Previous[ADMISSION_POOL_PROBE_GRANULES];
  PFN_NUMBER Current[ADMISSION_POOL_PROBE_GRANULES];
} ADMISSION_POOL_PROBE_WORK;

/* Called from AdmissionPoolNewSlab with the pool fast mutex held (APC_LEVEL;
 * MmAllocatePagesForMdlEx and MmFreePagesFromMdl allow DISPATCH_LEVEL).
 * Holding the pool lock for the whole probe (96 allocations of 2 MiB, about
 * one second under load) stalls every pool user once per boot; that is
 * accepted for the Wom1PoolAllocProbe=1 diagnostic boot only. */
static VOID AdmissionPoolAllocProbeLocked(ADMISSION_POOL_RUNTIME *Pool) {
  const ULONGLONG pages = ADMISSION_POOL_DEMAND_GRANULE / PAGE_SIZE;
  const ULONGLONG slabPages = ADMISSION_POOL_SLAB_BYTES / PAGE_SIZE;
  ADMISSION_POOL_PROBE_WORK *work;
  PHYSICAL_ADDRESS low, high, skip;
  LARGE_INTEGER probeStart = {0};
  ULONG previous = 0u;
  ULONG arm;
  work = (ADMISSION_POOL_PROBE_WORK *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
      sizeof(*work), ADMISSION_POOL_TAG);
  if (work == NULL) return;
  probeStart = KeQueryPerformanceCounter(NULL);
  work->Receipt.Version = ADMISSION_POOL_ALLOC_PROBE_VERSION;
  work->Receipt.Arms = ADMISSION_POOL_ALLOC_PROBE_ARMS;
  work->Receipt.PerArm = ADMISSION_POOL_ALLOC_PROBE_PER_ARM;
  work->Receipt.FieldsPerArm = ADMISSION_POOL_ALLOC_PROBE_FIELDS;
  work->Receipt.TriggerSlabAllocCalls = Pool->SlabAllocCalls;
  work->Receipt.TriggerSlabFastMisses = Pool->SlabFastMisses;
  work->Receipt.TriggerSlabChunked = Pool->SlabChunked;
  low.QuadPart = 0;
  high.QuadPart = (LONGLONG)(ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL);
  for (arm = 0u; arm < ADMISSION_POOL_ALLOC_PROBE_ARMS; ++arm) {
    const ADMISSION_POOL_PROBE_ARM *spec = &AdmissionPoolProbeArms[arm];
    unsigned long long *out = work->Receipt.Arm[arm];
    ULONG current = 0u;
    ULONG k, i, j;
    skip.QuadPart = (LONGLONG)spec->SkipBytes;
    out[ADMISSION_POOL_ALLOC_PROBE_MIN_US] = MAXULONGLONG;
    for (k = 0u; k < ADMISSION_POOL_ALLOC_PROBE_PER_ARM; ++k) {
      LARGE_INTEGER start = KeQueryPerformanceCounter(NULL);
      PMDL mdl = MmAllocatePagesForMdlEx(low, high, skip,
          (SIZE_T)ADMISSION_POOL_SLAB_BYTES, spec->CacheType, spec->Flags);
      ULONGLONG us = AdmissionPoolElapsedUs(start);
      const PFN_NUMBER *frames = NULL;
      ULONGLONG g = 0ULL, p = 0ULL;
      BOOLEAN contiguous = FALSE;
      work->Mdl[k] = mdl;
      ++out[ADMISSION_POOL_ALLOC_PROBE_CALLS];
      out[ADMISSION_POOL_ALLOC_PROBE_SUM_US] += us;
      if (us < out[ADMISSION_POOL_ALLOC_PROBE_MIN_US])
        out[ADMISSION_POOL_ALLOC_PROBE_MIN_US] = us;
      if (us > out[ADMISSION_POOL_ALLOC_PROBE_MAX_US])
        out[ADMISSION_POOL_ALLOC_PROBE_MAX_US] = us;
      /* A short MDL stays held and is freed with the others. */
      if (mdl == NULL || MmGetMdlByteCount(mdl) != ADMISSION_POOL_SLAB_BYTES)
        continue;
      ++out[ADMISSION_POOL_ALLOC_PROBE_OK];
      frames = MmGetMdlPfnArray(mdl);
      contiguous = (BOOLEAN)((frames[0] & (slabPages - 1ULL)) == 0ULL);
      for (p = 1ULL; contiguous && p < slabPages; ++p)
        if (frames[p] != frames[0] + p) contiguous = FALSE;
      if (contiguous) ++out[ADMISSION_POOL_ALLOC_PROBE_CONTIG_2M];
      for (g = 0ULL; g < ADMISSION_POOL_SLAB_SLOTS; ++g) {
        const PFN_NUMBER first = frames[g * pages];
        BOOLEAN granule = (BOOLEAN)((first & (pages - 1ULL)) == 0ULL);
        work->Current[current++] = first;
        for (p = 1ULL; granule && p < pages; ++p)
          if (frames[g * pages + p] != first + p) granule = FALSE;
        if (granule) ++out[ADMISSION_POOL_ALLOC_PROBE_GRANULES_OK];
      }
    }
    for (i = 0u; i < current; ++i)
      for (j = 0u; j < previous; ++j)
        if (work->Current[i] == work->Previous[j]) {
          ++out[ADMISSION_POOL_ALLOC_PROBE_REUSED];
          break;
        }
    for (i = 0u; i < current; ++i) work->Previous[i] = work->Current[i];
    previous = current;
    for (k = 0u; k < ADMISSION_POOL_ALLOC_PROBE_PER_ARM; ++k) {
      LARGE_INTEGER start = {0};
      if (work->Mdl[k] == NULL) continue;
      start = KeQueryPerformanceCounter(NULL);
      MmFreePagesFromMdl(work->Mdl[k]);
      out[ADMISSION_POOL_ALLOC_PROBE_FREE_US] += AdmissionPoolElapsedUs(start);
      ExFreePool(work->Mdl[k]);
      work->Mdl[k] = NULL;
    }
  }
  work->Receipt.ProbeUs = AdmissionPoolElapsedUs(probeStart);
  AdmissionRecordM2PoolAllocProbe(Pool->Context, &work->Receipt);
  ExFreePoolWithTag(work, ADMISSION_POOL_TAG);
}

/* ADMISSION_POOL_SLABS.NewSlab: called with the pool lock held. */
static ADMISSION_POOL_SLAB *AdmissionPoolNewSlab(void *Opaque) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  ADMISSION_POOL_SLAB_COST cost = {0};
  ADMISSION_POOL_SLAB *slab = AdmissionPoolAllocSlab(&cost, pool->ConstructLargePages);
  AdmissionPoolAddSlabCost(pool, &cost);
  /* POOL-PROBE: once per boot, still under the pool lock, when this slab
   * came from the 64 KiB rung. Switch off: one boolean test. The unlocked
   * prefill (AdmissionPoolPrefillWork) never triggers it. */
  if (pool->AllocProbe && !pool->AllocProbeDone && cost.Chunked != 0ULL) {
    pool->AllocProbeDone = TRUE;
    AdmissionPoolAllocProbeLocked(pool);
  }
  return slab;
}

static VOID AdmissionPoolFreeSlab(void *Opaque, ADMISSION_POOL_SLAB *Slab) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  LARGE_INTEGER start = KeQueryPerformanceCounter(NULL);
  PMDL mdl = (PMDL)Slab->Handle;
  ULONGLONG us;
  MmFreePagesFromMdl(mdl);
  ExFreePool(mdl);
  ExFreePoolWithTag(Slab, ADMISSION_POOL_TAG);
  us = AdmissionPoolElapsedUs(start);
  pool->SlabFreeUs += us;
  if (us > pool->SlabFreeUsMax) pool->SlabFreeUsMax = us;
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

/* RC53: neutral memory (both events clear) is not low memory. Only the
 * LowMemory notification vetoes new reservation slabs. Missing telemetry
 * fails closed, separately from observed pressure. Never gate admitted paging. */
static int AdmissionPoolMayGrow(void *Opaque) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  ADMISSION_POOL_RESERVE_FAILURE *decision = &pool->ReserveAttempt;
  /* Called under the pool mutex before growth (also by private leases).
   * Reserve includes its attempt in Reserved and checks overflow first;
   * only that wrapper publishes this scratch decision as a failure receipt. */
  decision->HighMemory = pool->HighMemory == NULL ? 2u :
      (KeReadStateEvent(pool->HighMemory) != 0 ? 1u : 0u);
  decision->LowMemory = pool->LowMemory == NULL ? 2u :
      (KeReadStateEvent(pool->LowMemory) != 0 ? 1u : 0u);
  decision->TimeMs = AdmissionPoolNowMs();
  decision->HeldGranules = pool->Slabs.Stats.Slabs * ADMISSION_POOL_SLAB_SLOTS;
  decision->RequiredGranules = (pool->Slabs.Reserved < pool->Slabs.ReserveCap ?
      pool->Slabs.Reserved : pool->Slabs.ReserveCap) + pool->Slabs.PrivateCount;
  decision->Reason = decision->LowMemory == 0u ? ADMISSION_POOL_REFUSAL_NONE :
      (decision->LowMemory == 1u ? ADMISSION_POOL_REFUSAL_LOW_MEMORY :
                                 ADMISSION_POOL_REFUSAL_NO_SIGNAL);
  /* rc109: the floor scales with the pool. Up to the default share
   * (BaseGranules, the pre-rc109 segment) the RC53 floor above is the whole
   * policy. A segment that the machine policy let grow past it may take the
   * extra pages only while Windows reports plenty of memory
   * (HighMemoryCondition set): the larger the GPU's part of RAM, the more
   * headroom DWM keeps (EXP1911 c00001ad under the low floor alone). */
  /* rc115 (EXP2061b: GravityMark D3D12 FHD 100k after a D3D11 run was
   * removed with E_OUTOFMEMORY at 3015 MiB reserved; Windows reported
   * neither High nor Low memory, available physical 877 MiB): in that
   * neutral state the segment may still grow ADMISSION_POOL_NEUTRAL_HEADROOM
   * past the default share; only beyond it does growth wait for
   * HighMemoryCondition. LowMemoryCondition still refuses every slab. */
  if (decision->Reason == ADMISSION_POOL_REFUSAL_NONE &&
      pool->BaseGranules < pool->Slabs.ReserveCap &&
      decision->HeldGranules + ADMISSION_POOL_SLAB_SLOTS >
          pool->BaseGranules + (decision->HighMemory == 0u
              ? pool->NeutralHeadroomGranules : 0ULL) &&
      decision->HighMemory != 1u)
    decision->Reason = ADMISSION_POOL_REFUSAL_HEADROOM;
  return decision->Reason == ADMISSION_POOL_REFUSAL_NONE;
}

/* Preserve M2-f7c early reclaim even in the neutral interval: release safe
 * surplus before LowMemory, but do not treat absent HighMemory as an OOM. */
static int AdmissionPoolShouldReclaim(ADMISSION_POOL_RUNTIME *Pool) {
  return Pool->HighMemory == NULL || KeReadStateEvent(Pool->HighMemory) == 0 ||
         Pool->LowMemory == NULL || KeReadStateEvent(Pool->LowMemory) != 0;
}

/* Called with the pool mutex held. Pressure bypasses only the idle delay
 * and empty-slab cache: Reclaim still enforces owner/pin/paging/sequence and
 * broker-unmap safety, and slab trimming still preserves promised backing. */
static VOID AdmissionPoolReclaimLocked(ADMISSION_POOL_RUNTIME *Pool) {
  if (!AdmissionPoolShouldReclaim(Pool)) {
    (void)AdmissionPoolDemandReclaimAged(&Pool->Demand, AdmissionPoolNowMs());
  } else {
    ULONGLONG spare = Pool->Slabs.SpareLimit;
    if (AdmissionPoolSlabHasSurplus(&Pool->Slabs, ADMISSION_POOL_SPARE_SLABS))
      ++Pool->CacheDrops;
    Pool->Slabs.SpareLimit = 0ULL;
    (void)AdmissionPoolDemandReclaim(&Pool->Demand);
    /* Also trim already-empty slabs, including when there were no candidates. */
    AdmissionPoolSlabUnreserve(&Pool->Slabs, 0ULL);
    Pool->Slabs.SpareLimit = spare;
  }
}

_Use_decl_annotations_ ADMISSION_POOL_DEMAND *AdmissionMemoryPoolLock(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_RUNTIME *pool;
  if (Context == NULL || Context->PoolDemand == NULL) return NULL;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  ExAcquireFastMutex(&pool->Lock);
  return &pool->Demand;
}

static VOID AdmissionPoolPrefillQueueLocked(ADMISSION_POOL_RUNTIME *Pool);

/* Stages Wom1M2PoolDemand when the committed size moved by a receipt step
 * or a failure count changed. */
_Use_decl_annotations_ VOID AdmissionMemoryPoolUnlock(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  const ADMISSION_POOL_DEMAND_STATS *stats = &pool->Demand.Stats;
  ADMISSION_POOL_DEMAND_RECEIPT receipt = {0};
  ULONGLONG failures = stats->CommitFailures + stats->ReleaseFailures +
                       stats->Unaligned + stats->RefUnderflows +
                       pool->Slabs.Stats.ReserveFailures;
  ULONGLONG moved = stats->CommittedGranules > pool->StagedGranules ?
      stats->CommittedGranules - pool->StagedGranules :
      pool->StagedGranules - stats->CommittedGranules;
  ULONGLONG reserved = pool->Slabs.Reserved > pool->StagedReserved ?
      pool->Slabs.Reserved - pool->StagedReserved :
      pool->StagedReserved - pool->Slabs.Reserved;
  ULONGLONG pressure = pool->Pressure.Waits + pool->Pressure.FastFails;
  /* rc104: the periodic worker stages any change, so the receipt read after
   * an app has exited holds exact values (EXP2033 compared post-app values
   * that could each be up to one step stale). */
  BOOLEAN exact = pool->StageExact &&
      (moved != 0ULL || reserved != 0ULL ||
       pool->Slabs.Stats.Slabs != pool->StagedSlabs);
  BOOLEAN stage = moved >= ADMISSION_POOL_DEMAND_RECEIPT_STEP ||
                  reserved >= ADMISSION_POOL_DEMAND_RECEIPT_STEP ||
                  pool->NativeStagingBytes != pool->StagedNativeBytes ||
                  failures != pool->StagedFailures ||
                  pressure != pool->StagedPressure || exact;
  pool->StageExact = FALSE;
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
    receipt.RamBytes = pool->RamBytes;
    receipt.ReservedBytes =
        pool->Slabs.Reserved * ADMISSION_POOL_DEMAND_GRANULE;
    receipt.PeakReservedBytes =
        pool->Slabs.Stats.PeakReserved * ADMISSION_POOL_DEMAND_GRANULE;
    receipt.ReserveFailures = pool->Slabs.Stats.ReserveFailures;
    receipt.BrokerBytes = Context->PoolBrokerBytes;
    receipt.FloorRefusals = pool->Slabs.Stats.FloorRefusals;
    receipt.NativeStagingBytes = pool->NativeStagingBytes;
    receipt.TotalPromisedBytes = ((pool->Slabs.Reserved < pool->Slabs.ReserveCap ?
        pool->Slabs.Reserved : pool->Slabs.ReserveCap) + pool->Slabs.PrivateCount) *
        ADMISSION_POOL_DEMAND_GRANULE + pool->NativeStagingBytes;
    receipt.LastReserveFailure = pool->LastReserveFailure;
    for (ULONG i = 0u; i < ADMISSION_POOL_REFUSAL_COUNT; ++i)
      receipt.RefusalCounts[i] = pool->RefusalCounts[i];
    receipt.SlabAllocCalls = pool->SlabAllocCalls;
    receipt.SlabAllocUs = pool->SlabAllocUs;
    receipt.SlabAllocUsMax = pool->SlabAllocUsMax;
    receipt.SlabCheckUs = pool->SlabCheckUs;
    receipt.SlabFreeUs = pool->SlabFreeUs;
    receipt.SlabFreeUsMax = pool->SlabFreeUsMax;
    receipt.ReserveCalls = pool->ReserveCalls;
    receipt.ReserveUs = pool->ReserveUs;
    receipt.ReserveUsMax = pool->ReserveUsMax;
    receipt.ReserveNoGrowth = pool->Slabs.Stats.ReserveNoGrowth;
    receipt.LeaseZeroCount = pool->LeaseZeroCount;
    receipt.LeaseZeroUs = pool->LeaseZeroUs;
    receipt.LeaseZeroSkips = pool->LeaseZeroSkips;
    receipt.SpareLimit = pool->Slabs.SpareLimit;
    receipt.EmptySlabs = pool->Slabs.Empty;
    receipt.CacheDrops = pool->CacheDrops;
    receipt.PrefillSlabs = pool->Slabs.Stats.AdoptedSlabs;
    receipt.PrefillUs = pool->PrefillUs;
    receipt.PrefillDiscards = pool->PrefillDiscards + pool->PrefillFailures;
    receipt.SlabFailUs = pool->SlabFailUs;
    receipt.SlabFastMisses = pool->SlabFastMisses;
    receipt.SlabContiguousUs = pool->SlabContiguousUs;
    receipt.SlabChunkedUs = pool->SlabChunkedUs;
    receipt.ConstructLargePages = pool->ConstructLargePages ? 1ULL : 0ULL;
    receipt.PressureBudgetMs = pool->Pressure.BudgetMs;
    receipt.PressureWaits = pool->Pressure.Waits;
    receipt.PressureWaitMs = pool->Pressure.WaitMs;
    receipt.PressureWaitMsMax = pool->Pressure.WaitMsMax;
    receipt.PressureRecovered = pool->Pressure.Recovered;
    receipt.PressureFailed = pool->Pressure.TimedOut;
    receipt.PressureFastFails = pool->Pressure.FastFails;
    receipt.ReservedAllocations = pool->ReservedAllocations;
    receipt.GraphicsShareBytes = pool->GraphicsShareBytes;
    receipt.BaseSegmentBytes = pool->BaseSegmentBytes;
    receipt.PoolPercent = pool->PoolPercent;
    receipt.PolicyPercent = pool->Policy.Percent;
    receipt.PolicyMaxPercent = pool->Policy.MaxPercent;
    receipt.PolicyCpuOnlyMiB = pool->Policy.CpuOnlyMiB;
    receipt.PolicyFlags = pool->PolicyFlags;
    receipt.HeadroomRefusals = pool->HeadroomRefusals;
    pool->StagedPressure = pressure;
    pool->StagedSlabs = pool->Slabs.Stats.Slabs;
    pool->StagedGranules = stats->CommittedGranules;
    pool->StagedFailures = failures;
    pool->StagedReserved = pool->Slabs.Reserved;
    pool->StagedNativeBytes = pool->NativeStagingBytes;
  }
  /* Idle granules and cached empty slabs wait for the periodic reclaim. */
  if ((pool->Demand.Candidates != 0u ||
       AdmissionPoolSlabHasSurplus(&pool->Slabs, ADMISSION_POOL_SPARE_SLABS)) &&
      InterlockedCompareExchange(&pool->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&pool->TimerArmed, 1, 0) == 0) {
    LARGE_INTEGER due;
    due.QuadPart = -ADMISSION_POOL_RECLAIM_PERIOD_MS * 10000LL;
    (void)KeSetTimer(&pool->Timer, due, &pool->Dpc);
  }
  AdmissionPoolPrefillQueueLocked(pool);
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
  AdmissionPoolReclaimLocked((ADMISSION_POOL_RUNTIME *)Context->PoolDemand);
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

/* One reservation attempt (the whole pre-rc104 AdmissionMemoryPoolReserve).
 * *Reason: the refusal reason, ADMISSION_POOL_REFUSAL_NONE when reserved. */
static BOOLEAN AdmissionPoolReserveOnce(ADMISSION_CONTEXT *Context,
                                        ULONGLONG Bytes, ULONG *Reason) {
  ADMISSION_POOL_RUNTIME *pool;
  BOOLEAN reserved;
  ADMISSION_POOL_RESERVE_FAILURE attempt = {0};
  ULONGLONG slabFailures, us;
  LARGE_INTEGER start;
  *Reason = ADMISSION_POOL_REFUSAL_NONE;
  if (Bytes == 0ULL || AdmissionMemoryPoolLock(Context) == NULL) return TRUE;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  start = KeQueryPerformanceCounter(NULL);
  /* No policy call for a fully backed promise. If the allocator rejects
   * before MayGrow, its only refusal is checked arithmetic overflow. */
  attempt.HighMemory = attempt.LowMemory = 2u;
  attempt.Reason = ADMISSION_POOL_REFUSAL_ARITHMETIC;
  attempt.TimeMs = AdmissionPoolNowMs();
  attempt.HeldGranules = pool->Slabs.Stats.Slabs * ADMISSION_POOL_SLAB_SLOTS;
  attempt.ReserveCapGranules = pool->Slabs.ReserveCap;
  attempt.PrivateGranules = pool->Slabs.PrivateCount;
  pool->ReserveAttempt = attempt;
  slabFailures = pool->Slabs.Stats.NewSlabFailures;
  reserved = AdmissionPoolSlabReserve(&pool->Slabs,
      (Bytes + ADMISSION_POOL_DEMAND_GRANULE - 1ULL) /
          ADMISSION_POOL_DEMAND_GRANULE) ? TRUE : FALSE;
  if (!reserved) {
    /* The last policy sample belongs to the slab that failed, including
     * partial growth. Preserve it across rollback and later reclaim calls. */
    if (pool->Slabs.Stats.NewSlabFailures != slabFailures)
      pool->ReserveAttempt.Reason = ADMISSION_POOL_REFUSAL_BACKING;
    pool->LastReserveFailure = pool->ReserveAttempt;
    if (pool->LastReserveFailure.Reason == ADMISSION_POOL_REFUSAL_HEADROOM)
      ++pool->HeadroomRefusals;
    else
      ++pool->RefusalCounts[pool->LastReserveFailure.Reason - 1u];
    pool->LastReserveFailure.RequestBytes = Bytes;
    pool->LastReserveFailure.ReservedBytes =
        pool->Slabs.Reserved * ADMISSION_POOL_DEMAND_GRANULE;
    pool->LastReserveFailure.HeldBytes = pool->Slabs.Stats.Slabs *
        ADMISSION_POOL_SLAB_SLOTS * ADMISSION_POOL_DEMAND_GRANULE;
    pool->LastReserveFailure.CommittedBytes =
        pool->Demand.Stats.CommittedGranules * ADMISSION_POOL_DEMAND_GRANULE;
    *Reason = pool->LastReserveFailure.Reason;
  } else {
    ++pool->ReservedAllocations;
  }
  /* First let this reservation reuse held pages; then return only surplus. */
  if (AdmissionPoolShouldReclaim(pool)) AdmissionPoolReclaimLocked(pool);
  us = AdmissionPoolElapsedUs(start);
  ++pool->ReserveCalls;
  pool->ReserveUs += us;
  if (us > pool->ReserveUsMax) pool->ReserveUsMax = us;
  pool->LastReserveMs = AdmissionPoolNowMs();
  if (reserved && Bytes >= ADMISSION_POOL_PREFILL_MIN_BYTES)
    pool->LastLargeReserveMs = pool->LastReserveMs;
  AdmissionMemoryPoolUnlock(Context);
  return reserved;
}

/* rc104: the refusals that wait for the memory manager (rc109: also the
 * headroom floor of an enlarged segment, which waits for HighMemory). */
static BOOLEAN AdmissionPoolPressureWaits(ULONG Reason) {
  return Reason == ADMISSION_POOL_REFUSAL_LOW_MEMORY ||
         Reason == ADMISSION_POOL_REFUSAL_HEADROOM;
}

/* rc104: sampled without the pool lock on a referenced event. */
static BOOLEAN AdmissionPoolPressureEventSet(PKEVENT Event) {
  return Event != NULL && KeReadStateEvent(Event) != 0;
}

/* rc104: sleeps up to Ms at PASSIVE_LEVEL with no driver lock held, waking
 * early when HighMemoryCondition is set (Standard Event Objects: drivers may
 * wait on it); returns the wall milliseconds slept. */
static ULONGLONG AdmissionPoolPressureSleep(PKEVENT HighMemory, ULONGLONG Ms) {
  LARGE_INTEGER timeout;
  ULONGLONG start = AdmissionPoolNowMs();
  timeout.QuadPart = -(LONGLONG)(Ms * 10000ULL);
  if (HighMemory != NULL)
    (void)KeWaitForSingleObject(HighMemory, Executive, KernelMode, FALSE, &timeout);
  else
    (void)KeDelayExecutionThread(KernelMode, FALSE, &timeout);
  return AdmissionPoolNowMs() - start;
}

/* rc104 (EXP2033 arm e: four creations refused on one LowMemoryCondition
 * sample each, GravityMark E_OUTOFMEMORY then device removed): a creation
 * refused at the floor waits, without the pool lock and in
 * ADMISSION_POOL_PRESSURE_STEP_MS steps, for the memory manager to clear
 * LowMemoryCondition, then retries; it fails as before once its
 * Wom1PoolPressureWaitMs budget is spent (AdmissionPoolPressureNextWait).
 * The floor is unchanged: every attempt runs AdmissionPoolReserveOnce, so no
 * slab is taken while LowMemory is set, and a still-set LowMemory is not
 * retried (one FloorRefusal per dip, not per step). rc109: a headroom
 * refusal (growth past the default share without HighMemoryCondition) waits
 * the same way and is retried once HighMemory is set. Other refusals
 * (backing, no signal, arithmetic) never wait. Microsoft (DxgkDdiCreateAllocation):
 * STATUS_NO_MEMORY only when memory the call needs cannot be allocated. */
_Use_decl_annotations_ BOOLEAN AdmissionMemoryPoolReserve(
    ADMISSION_CONTEXT *Context, ULONGLONG Bytes) {
  ADMISSION_POOL_RUNTIME *pool;
  ULONG reason = ADMISSION_POOL_REFUSAL_NONE;
  ULONGLONG charged = 0ULL, wall = 0ULL, step;
  PKEVENT high = NULL, low = NULL;
  BOOLEAN reserved = AdmissionPoolReserveOnce(Context, Bytes, &reason);
  if (reserved || !AdmissionPoolPressureWaits(reason)) return reserved;
  for (;;) {
    if (AdmissionMemoryPoolLock(Context) == NULL) break;
    pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
    step = InterlockedCompareExchange(&pool->Stopping, 0, 0) != 0 ? 0ULL :
        AdmissionPoolPressureNextWait(&pool->Pressure, AdmissionPoolNowMs(),
                                      charged);
    if (step != 0ULL && low == NULL && pool->LowMemory != NULL) {
      /* Hold the events across the unlocked wait. */
      low = pool->LowMemory;
      ObReferenceObject(low);
      high = pool->HighMemory;
      if (high != NULL) ObReferenceObject(high);
    }
    AdmissionMemoryPoolUnlock(Context);
    if (step == 0ULL || low == NULL) break;
    wall += AdmissionPoolPressureSleep(high, step);
    charged += step;
    /* Retry once the event that refused it has changed. */
    if (AdmissionPoolPressureEventSet(low) ||
        (reason == ADMISSION_POOL_REFUSAL_HEADROOM &&
         !AdmissionPoolPressureEventSet(high)))
      continue;
    reserved = AdmissionPoolReserveOnce(Context, Bytes, &reason);
    if (reserved || !AdmissionPoolPressureWaits(reason)) break;
  }
  if (AdmissionMemoryPoolLock(Context) != NULL) {
    pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
    AdmissionPoolPressureDone(&pool->Pressure, AdmissionPoolNowMs(), charged,
                              wall, reserved ? 1 : 0);
    AdmissionMemoryPoolUnlock(Context);
  }
  if (high != NULL) ObDereferenceObject(high);
  if (low != NULL) ObDereferenceObject(low);
  return reserved;
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolUnreserve(
    ADMISSION_CONTEXT *Context, ULONGLONG Bytes) {
  ADMISSION_POOL_RUNTIME *pool;
  if (Bytes == 0ULL || AdmissionMemoryPoolLock(Context) == NULL) return;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  if (pool->ReservedAllocations != 0ULL) --pool->ReservedAllocations;
  AdmissionPoolSlabUnreserve(&pool->Slabs,
      (Bytes + ADMISSION_POOL_DEMAND_GRANULE - 1ULL) /
          ADMISSION_POOL_DEMAND_GRANULE);
  /* rc86: the cache exists only in high memory; under the M2-f7c early
   * reclaim condition the freed surplus goes back to Windows now. */
  if (AdmissionPoolShouldReclaim(pool)) AdmissionPoolReclaimLocked(pool);
  AdmissionMemoryPoolUnlock(Context);
}

#ifdef APPLE_AGX_NATIVE_STAGING
/* Dedicated original MDLs are charged separately: they cannot satisfy a
 * segment-3 placement and must not make the slab allocator take duplicate
 * backing. The physical manager charges before allocation and keeps the
 * charge until CPU, VidMm and graph ownership have all ended.
 * RC62 (EXP1984): every native BO is aperture-only, so the charge is also
 * bounded by the aperture segment's CommitLimit. Three 128 MiB arenas left
 * persistently mapped native BOs over the 256 MiB NON_LOCAL budget: a later
 * MakeResident failed with CantTrimFurther after trimming 1446 MiB of local
 * residency that could not help. Keep three quarters for other placements. */
static ULONGLONG AdmissionNativeStagingCap(const ADMISSION_CONTEXT *Context) {
  ULONGLONG cap = (Context->Memory.Topology.Aperture.CommitLimit /
                   ADMISSION_NATIVE_STAGING_APERTURE_SHARE) & ~65535ULL;
  return cap < ADMISSION_NATIVE_STAGING_BUDGET_BYTES ? cap :
                                                       ADMISSION_NATIVE_STAGING_BUDGET_BYTES;
}

BOOLEAN AdmissionMemoryPoolChargeNative(ADMISSION_CONTEXT *Context,
    ULONGLONG Bytes) {
  ADMISSION_POOL_RUNTIME *pool;
  BOOLEAN allowed = FALSE;
  ULONGLONG cap;
  if (!Context || !Bytes || (Bytes & 65535ULL)) return FALSE;
  cap = AdmissionNativeStagingCap(Context);
  if (Bytes > cap || !AdmissionMemoryPoolLock(Context)) return FALSE;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  if (!pool->Stopping && pool->NativeStagingBytes <= cap - Bytes &&
      AdmissionPoolMayGrow(pool)) {
    pool->NativeStagingBytes += Bytes;
    allowed = TRUE;
  }
  AdmissionMemoryPoolUnlock(Context);
  return allowed;
}

VOID AdmissionMemoryPoolUnchargeNative(ADMISSION_CONTEXT *Context,
    ULONGLONG Bytes) {
  ADMISSION_POOL_RUNTIME *pool;
  if (!Context || !Bytes || !AdmissionMemoryPoolLock(Context)) return;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  /* An invalid release cannot erase somebody else's backing promise. */
  if (Bytes <= pool->NativeStagingBytes) pool->NativeStagingBytes -= Bytes;
  AdmissionMemoryPoolUnlock(Context);
}
#endif

/* rc80: a lease granule is slot `Slot` of its slab, bytes [Slot * 64 KiB,
 * +64 KiB) of the slab's page MDL in both slab shapes (AdmissionPoolNewSlab:
 * Pa[i] is frame i * 16). Map exactly those pages write-combined, as they
 * were allocated, and clear them. */
static BOOLEAN AdmissionPoolZeroLease(const ADMISSION_POOL_PRIVATE *Lease) {
  PMDL source, part;
  PUCHAR base;
  PVOID cpu;
  if (Lease == NULL || Lease->Slab == NULL || Lease->Slab->Handle == NULL ||
      Lease->Slot >= ADMISSION_POOL_SLAB_SLOTS) return FALSE;
  source = (PMDL)Lease->Slab->Handle;
  base = (PUCHAR)MmGetMdlVirtualAddress(source) +
         (SIZE_T)Lease->Slot * (SIZE_T)ADMISSION_POOL_DEMAND_GRANULE;
  part = IoAllocateMdl(base, (ULONG)ADMISSION_POOL_DEMAND_GRANULE, FALSE,
                       FALSE, NULL);
  if (part == NULL) return FALSE;
  IoBuildPartialMdl(source, part, base, (ULONG)ADMISSION_POOL_DEMAND_GRANULE);
  if (((ULONGLONG)MmGetMdlPfnArray(part)[0] << PAGE_SHIFT) !=
      Lease->Slab->Pa[Lease->Slot]) {
    IoFreeMdl(part);
    return FALSE;
  }
  cpu = MmMapLockedPagesSpecifyCache(part, KernelMode, MmWriteCombined, NULL,
      FALSE, NormalPagePriority | MdlMappingNoExecute);
  if (cpu == NULL) {
    IoFreeMdl(part);
    return FALSE;
  }
  RtlZeroMemory(cpu, (SIZE_T)ADMISSION_POOL_DEMAND_GRANULE);
  KeMemoryBarrier();
  MmUnmapLockedPages(cpu, part);
  IoFreeMdl(part);
  return TRUE;
}

/* Counters are approximate: lease zeroing runs outside the pool lock. */
static BOOLEAN AdmissionPoolZeroLeaseTimed(ADMISSION_POOL_RUNTIME *Pool,
                                           const ADMISSION_POOL_PRIVATE *Lease) {
  LARGE_INTEGER start = KeQueryPerformanceCounter(NULL);
  BOOLEAN zeroed = AdmissionPoolZeroLease(Lease);
  Pool->LeaseZeroUs += AdmissionPoolElapsedUs(start);
  ++Pool->LeaseZeroCount;
  return zeroed;
}

_Use_decl_annotations_ BOOLEAN AdmissionMemoryPoolTvbTake(
    ADMISSION_CONTEXT *Context, ADMISSION_POOL_PRIVATE *Lease,
    ULONGLONG *GuestIpa) {
  ADMISSION_POOL_RUNTIME *pool;
  BOOLEAN taken = FALSE;
  if (GuestIpa != NULL) *GuestIpa = 0ULL;
  if (Lease == NULL || GuestIpa == NULL || Lease->Slab != NULL ||
      AdmissionMemoryPoolLock(Context) == NULL) return FALSE;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  if (!pool->Stopping && AdmissionPoolSlabTakePrivate(&pool->Slabs, Lease))
    taken = TRUE;
  AdmissionMemoryPoolUnlock(Context);
  if (!taken) return FALSE;
  /* The lease is exclusive: its slab cannot be freed while it is live.
   * rc86: a clean slot still holds the zeroed pages Windows supplied
   * (MmAllocatePagesForMdlEx without MM_DONT_ZERO_ALLOCATION) and was never
   * mapped since; clearing it again only costs a map and 64 KiB of stores. */
  if (Lease->Zero) {
    ++pool->LeaseZeroSkips;
  } else if (!AdmissionPoolZeroLeaseTimed(pool, Lease)) {
    /* Never GPU-mapped, so it may return unzeroed; a stopped runtime keeps
     * its private slots (AdmissionPoolDemandStop leak path). */
    if (AdmissionMemoryPoolLock(Context) != NULL) {
      (void)AdmissionPoolSlabGivePrivate(&pool->Slabs, Lease);
      AdmissionMemoryPoolUnlock(Context);
    }
    return FALSE;
  }
  /* From here the owner exposes the slot; it is no longer known zero. */
  Lease->Zero = 0u;
  *GuestIpa = Lease->Slab->Pa[Lease->Slot];
  return TRUE;
}

_Use_decl_annotations_ BOOLEAN AdmissionMemoryPoolTvbGive(
    ADMISSION_CONTEXT *Context, ADMISSION_POOL_PRIVATE *Lease) {
  ADMISSION_POOL_RUNTIME *pool;
  BOOLEAN given;
  if (Lease == NULL || Lease->Slab == NULL) return Lease != NULL;
  /* Process data never returns to the pool: an unzeroed granule stays. */
  if (Context == NULL || Context->PoolDemand == NULL ||
      !AdmissionPoolZeroLeaseTimed((ADMISSION_POOL_RUNTIME *)Context->PoolDemand,
                                   Lease)) {
    if (Context != NULL) InterlockedIncrement(&Context->PoolLeaks);
    return FALSE;
  }
  if (AdmissionMemoryPoolLock(Context) == NULL) return FALSE;
  pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  /* Zeroed just now: the slot goes back clean. */
  Lease->Zero = 1u;
  given = AdmissionPoolSlabGivePrivate(&pool->Slabs, Lease) != 0;
  AdmissionMemoryPoolUnlock(Context);
  return given;
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolReclaim(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_DEMAND *demand = AdmissionMemoryPoolLock(Context);
  if (demand == NULL) return;
  AdmissionPoolReclaimLocked((ADMISSION_POOL_RUNTIME *)Context->PoolDemand);
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
  if (InterlockedCompareExchange(&pool->Stopping, 0, 0) == 0) {
    ADMISSION_POOL_DEMAND *demand = AdmissionMemoryPoolLock(pool->Context);
    if (demand != NULL) {
      /* A process graph can drop the last broker hold without executing a
       * newer paging record. Give Busy candidates one retry per timer wake
       * (500 ms), otherwise they can keep dead allocations' backing forever.
       * Ordinary reclaim calls still suppress same-epoch retries. Only that
       * suppression is cleared: owner/pin/paging/record checks and the broker
       * acknowledgement remain mandatory before any slot returns. */
      for (ULONGLONG i = 0ULL; i < demand->Candidates; ++i)
        demand->Granules[demand->CandidateList[i]].Busy = 0u;
      AdmissionPoolReclaimLocked(pool);
      /* rc86: an idle cache goes back to Windows even in high memory. */
      if (AdmissionPoolNowMs() - pool->LastReserveMs >=
              ADMISSION_POOL_RELEASE_DELAY_MS &&
          AdmissionPoolSlabHasSurplus(&pool->Slabs, ADMISSION_POOL_SPARE_SLABS)) {
        ULONGLONG spare = pool->Slabs.SpareLimit;
        ++pool->CacheDrops;
        pool->Slabs.SpareLimit = ADMISSION_POOL_SPARE_SLABS;
        AdmissionPoolSlabUnreserve(&pool->Slabs, 0ULL);
        pool->Slabs.SpareLimit = spare;
      }
      pool->StageExact = TRUE;
      AdmissionMemoryPoolUnlock(pool->Context);
    }
  }
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

/* rc88 (EXP2019a, FHD 100k: 1849 MmAllocatePagesForMdlEx calls took 37.2 s
 * of the run, 20 ms each, and the pool lock was held through all of them;
 * Wom1M2PoolDemand v11 ReserveUs 35.5 s against SlabAllocUs 37.2 s, so a
 * 128 MiB pfnAllocateCb is Windows page allocation, not VidMm; the first
 * frame's 128 MiB creations took 0.3-1.7 s each): keep the rc86 empty-slab
 * cache full from a work item instead of filling it inside the next
 * creation. Pages come from Windows without the pool lock, while the app's
 * thread does other work, and join the cache as empty spares; they never
 * exceed the cache the rc86 policy already keeps (SpareLimit empty slabs),
 * Trim and the pressure reclaim treat them as any cached slab, and the
 * periodic worker still returns the cache after RELEASE_DELAY idle.
 * Allowed only while the switch is on (Wom1PoolPrefill=1; rc91: off by
 * default, see AdmissionPoolPrefillEnabled), the
 * pool runs, HighMemoryCondition is set and LowMemoryCondition clear (the
 * cache's own condition), a creation of at least
 * ADMISSION_POOL_PREFILL_MIN_BYTES reserved within the cache's idle delay,
 * and the cache is short. One work item adds at most
 * ADMISSION_POOL_PREFILL_BATCH slabs, rechecking before each. */
static BOOLEAN AdmissionPoolPrefillAllowed(BOOLEAN Enabled, BOOLEAN Stopping,
                                           ULONG HighMemory, ULONG LowMemory,
                                           ULONGLONG NowMs, ULONGLONG LastReserveMs,
                                           ULONGLONG Wanted) {
  return Enabled && !Stopping && HighMemory == 1u && LowMemory == 0u &&
         LastReserveMs != 0ULL && NowMs >= LastReserveMs &&
         NowMs - LastReserveMs < ADMISSION_POOL_RELEASE_DELAY_MS &&
         Wanted != 0ULL ? TRUE : FALSE;
}

/* Under the pool lock. */
static BOOLEAN AdmissionPoolPrefillWanted(ADMISSION_POOL_RUNTIME *Pool) {
  ULONG high = Pool->HighMemory == NULL ? 2u :
      (KeReadStateEvent(Pool->HighMemory) != 0 ? 1u : 0u);
  ULONG low = Pool->LowMemory == NULL ? 2u :
      (KeReadStateEvent(Pool->LowMemory) != 0 ? 1u : 0u);
  return AdmissionPoolPrefillAllowed(Pool->PrefillEnabled,
      InterlockedCompareExchange(&Pool->Stopping, 0, 0) != 0 ? TRUE : FALSE,
      high, low,
      AdmissionPoolNowMs(), Pool->LastLargeReserveMs,
      AdmissionPoolSlabPrefillWants(&Pool->Slabs));
}

static IO_WORKITEM_ROUTINE AdmissionPoolPrefillWork;
static VOID AdmissionPoolPrefillWork(_In_ PDEVICE_OBJECT Device,
                                     _In_opt_ PVOID Opaque) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Opaque;
  ULONG i;
  UNREFERENCED_PARAMETER(Device);
  if (pool == NULL) return;
  /* From here a pool unlock may queue the item again. */
  InterlockedExchange(&pool->PrefillQueued, 0);
  for (i = 0u; i < ADMISSION_POOL_PREFILL_BATCH; ++i) {
    ADMISSION_POOL_SLAB_COST cost = {0};
    ADMISSION_POOL_SLAB *slab;
    BOOLEAN wanted, adopted = FALSE;
    LARGE_INTEGER start;
    /* Stop drains WorkPending before it frees the runtime or clears
     * Context->PoolDemand, so the raw lock stays valid here. Release it
     * without AdmissionMemoryPoolUnlock: no receipt and no re-queue. */
    ExAcquireFastMutex(&pool->Lock);
    wanted = AdmissionPoolPrefillWanted(pool);
    ExReleaseFastMutex(&pool->Lock);
    if (!wanted) break;
    start = KeQueryPerformanceCounter(NULL);
    slab = AdmissionPoolAllocSlab(&cost, pool->ConstructLargePages);
    ExAcquireFastMutex(&pool->Lock);
    AdmissionPoolAddSlabCost(pool, &cost);
    pool->PrefillUs += AdmissionPoolElapsedUs(start);
    if (slab != NULL) {
      /* The cache may have filled or the condition cleared meanwhile. */
      if (AdmissionPoolPrefillWanted(pool) &&
          AdmissionPoolSlabAdopt(&pool->Slabs, slab))
        adopted = TRUE;
      else {
        ++pool->PrefillDiscards;
        AdmissionPoolFreeSlab(pool, slab);
      }
    } else {
      ++pool->PrefillFailures;
    }
    ExReleaseFastMutex(&pool->Lock);
    if (!adopted) break;
  }
  InterlockedDecrement(&pool->WorkPending);
}

/* Under the pool lock (AdmissionMemoryPoolUnlock). IoQueueWorkItem is
 * callable at APC_LEVEL; Stop's WorkPending drain covers this item. */
static VOID AdmissionPoolPrefillQueueLocked(ADMISSION_POOL_RUNTIME *Pool) {
  if (Pool->PrefillWork == NULL || !AdmissionPoolPrefillWanted(Pool) ||
      InterlockedCompareExchange(&Pool->PrefillQueued, 1, 0) != 0)
    return;
  InterlockedIncrement(&Pool->WorkPending);
  IoQueueWorkItem(Pool->PrefillWork, AdmissionPoolPrefillWork,
                  DelayedWorkQueue, Pool);
}

static VOID AdmissionPoolDemandFree(ADMISSION_POOL_RUNTIME *Pool) {
  if (Pool->HighMemory) ObDereferenceObject(Pool->HighMemory);
  if (Pool->LowMemory) ObDereferenceObject(Pool->LowMemory);
  if (Pool->Work) IoFreeWorkItem(Pool->Work);
  if (Pool->PrefillWork) IoFreeWorkItem(Pool->PrefillWork);
  if (Pool->SlotOf) ExFreePoolWithTag(Pool->SlotOf, ADMISSION_POOL_TAG);
  if (Pool->SlabOf) ExFreePoolWithTag(Pool->SlabOf, ADMISSION_POOL_TAG);
  if (Pool->Candidates) ExFreePoolWithTag(Pool->Candidates, ADMISSION_POOL_TAG);
  if (Pool->Granules) ExFreePoolWithTag(Pool->Granules, ADMISSION_POOL_TAG);
  ExFreePoolWithTag(Pool, ADMISSION_POOL_TAG);
}

/* Window pages leave before their pages return to Windows. Outstanding private
 * leases or a refused UNMAP_ALL retain the entire runtime, not just the pages:
 * slab ownership and callbacks still belong to this object (PoolLeaks). */
static VOID AdmissionPoolDemandStop(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_RUNTIME *pool = (ADMISSION_POOL_RUNTIME *)Context->PoolDemand;
  ULONGLONG i;
  BOOLEAN unmapped;
  ULONG waited;
  if (pool == NULL) return;
  /* No reclaim runs past this point: no new arm or queue, the armed timer
   * cancelled, running DPCs drained, queued work items finished. */
  InterlockedExchange(&pool->Stopping, 1);
  /* rc88: an unlock that saw Stopping clear queues its prefill item, and
   * counts it in WorkPending, before it releases the pool lock: after this
   * barrier no new item can appear and the drain below covers the rest. */
  ExAcquireFastMutex(&pool->Lock);
  ExReleaseFastMutex(&pool->Lock);
  KeCancelTimer(&pool->Timer);
  KeFlushQueuedDpcs();
  for (waited = 0u; InterlockedCompareExchange(&pool->WorkPending, 0, 0) != 0 &&
                    waited < 10000u; ++waited) {
    LARGE_INTEGER delay;
    delay.QuadPart = -10000LL; /* 1 ms */
    (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);
  }
  if (InterlockedCompareExchange(&pool->WorkPending, 0, 0) != 0 ||
      pool->Slabs.PrivateCount != 0ULL) {
    /* A worker or private lease still owns the runtime. Private grants/copies
     * are not retired by UNMAP_ALL, which owns only the advertised window.
     * Preserve mappings, slabs and their owner together on this leak path. */
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
    AdmissionPoolDemandFree(pool);
  } else {
    /* Still aliased: keep the slab list and its containing owner for good. */
    InterlockedIncrement(&Context->PoolLeaks);
  }
  Context->PoolCpu = NULL;
  Context->PoolIpa = 0ULL;
  Context->PoolBytes = 0ULL;
}

/* The RAM Windows manages (MmGetPhysicalMemoryRangesEx2, PASSIVE: base/length
 * pairs, a zero pair ends the block, the caller frees it); 0 when unknown. */
static ULONGLONG AdmissionPoolRamBytes(VOID) {
  PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRangesEx2(NULL, 0u);
  ULONGLONG total = 0ULL;
  ULONG i;
  if (ranges == NULL) return 0ULL;
  for (i = 0u; ranges[i].BaseAddress.QuadPart != 0 ||
               ranges[i].NumberOfBytes.QuadPart != 0; ++i)
    total += (ULONGLONG)ranges[i].NumberOfBytes.QuadPart;
  ExFreePool(ranges);
  return total;
}

static PKEVENT AdmissionPoolOpenEvent(PCWSTR Name) {
  UNICODE_STRING name;
  HANDLE handle = NULL;
  PKEVENT event;
  RtlInitUnicodeString(&name, Name);
  event = IoCreateNotificationEvent(&name, &handle);
  if (event == NULL) return NULL;
  ObReferenceObject(event);
  ZwClose(handle);
  return event;
}

/* EXP1926: fixed-at-start pressure candidate. A lower resident bound lets
 * VidMm page before lifetime reservations consume the entire normal pool.
 * Do not lower ReserveCap alone: paging may use every advertised byte.
 * Default and the T8 growth gate are unchanged. */
static ULONGLONG AdmissionPoolSegmentBytesShare(ULONGLONG RamBytes,
                                               ULONGLONG WindowBytes,
                                               ULONGLONG BrokerBytes,
                                               BOOLEAN HalfSize,
                                               BOOLEAN ThreeQuarter,
                                               ULONGLONG ApertureReserve,
                                               ULONGLONG ShareBytes) {
  ULONGLONG bytes = AdmissionPoolDemandSegmentBytesShare(
      RamBytes, WindowBytes, BrokerBytes, ApertureReserve, ShareBytes);
  if (HalfSize && bytes != 0ULL) {
    bytes = (bytes / 2ULL) & ~(ADMISSION_POOL_DEMAND_SIZE_STEP - 1ULL);
    if (bytes < ADMISSION_POOL_DEMAND_MIN_BYTES)
      bytes = ADMISSION_POOL_DEMAND_MIN_BYTES;
  } else if (ThreeQuarter && bytes != 0ULL) {
    /* RC15: retain 3/4 of the ordinary bound, including the backing promise.
     * Divide first to avoid overflow. The normal bound is 64 MiB-aligned,
     * hence this result is 16 MiB-aligned (whole 2 MiB slabs). On J313 this
     * is 2016 MiB: 672 MiB more residency than HalfSize, 672 MiB less locked
     * backing than full size. The validated High&&!Low floor stays intact. */
    bytes = (bytes / 4ULL) * 3ULL;
    if (bytes < ADMISSION_POOL_DEMAND_MIN_BYTES)
      bytes = ADMISSION_POOL_DEMAND_MIN_BYTES;
  }
  return bytes;
}

/* The pre-rc109 size: the default graphics formula alone. */
static ULONGLONG AdmissionPoolSegmentBytes(ULONGLONG RamBytes,
                                          ULONGLONG WindowBytes,
                                          ULONGLONG BrokerBytes,
                                          BOOLEAN HalfSize,
                                          BOOLEAN ThreeQuarter,
                                          ULONGLONG ApertureReserve) {
  return AdmissionPoolSegmentBytesShare(RamBytes, WindowBytes, BrokerBytes,
      HalfSize, ThreeQuarter, ApertureReserve,
      AdmissionPoolGraphicsShareBytes(RamBytes, NULL));
}

/* Device key, once at PASSIVE_LEVEL before the segment is published.
 * Only REG_DWORD=1 opts in; absent/malformed values keep the normal size. */
static BOOLEAN AdmissionPoolHalfSizeEnabled(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1PoolHalfSize");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* RC15: only fraction 3 opts in; other values preserve the default.
 * HalfSize wins if both are set, so a stale switch cannot enlarge its cap. */
static BOOLEAN AdmissionPoolThreeQuarterEnabled(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1PoolFraction");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 3u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* rc88: the LOCAL budget VidMm reports is 85% of segments 2 + 3 (EXP1264:
 * 1788 of 2104 MiB; EXP1274: 2550 of 3000 MiB), about 2931 MiB on J313
 * (312 + 3136 MiB), below the GravityMark FHD 200k working set. Only
 * REG_DWORD Wom1PoolApertureReserveHalf=1 moves 128 MiB of the 50%-of-RAM
 * graphics share from the aperture's shared system memory to segment 3
 * (3136 -> 3264 MiB on 6884 MiB of RAM: LOCAL budget about +109 MiB,
 * NON_LOCAL budget about 275 -> 160 MiB). Default and invalid values keep
 * the 256 MiB reserve. */
static BOOLEAN AdmissionPoolApertureReserveHalfEnabled(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1PoolApertureReserveHalf");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* rc88 background slab prefill; rc91: opt-in, only REG_DWORD
 * Wom1PoolPrefill=1 turns it on, absent or other values keep it off.
 * Its work item shares the system DelayedWorkQueue with the paging worker
 * (AdmissionPagingQueueActive). Wom1G3PagingProfile DispatchToWorker max
 * was 7.8-20 ms in every receipt EXP2015-EXP2021 and 1.06-1.55 s in every
 * one since the prefill (EXP2022-EXP2024, about the prefill's longest
 * MmAllocatePagesForMdlEx call); VidMm residency waits on those packets,
 * and GravityMark D3D11 creates its large resources inside the scored
 * window (EXP2024: first frames 1.8 s -> 11.7 s, 2529 -> 2279). */
static BOOLEAN AdmissionPoolPrefillEnabled(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1PoolPrefill");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* rc93 kill switch: only REG_DWORD Wom1PoolConstructLargePages=1 lets the
 * 2 MiB slab attempt construct large pages (the pre-rc93 flags); absent or
 * other values keep MM_ALLOCATE_FAST_LARGE_PAGES. */
static BOOLEAN AdmissionPoolConstructLargePagesEnabled(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1PoolConstructLargePages");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* POOL-PROBE: only REG_DWORD Wom1PoolAllocProbe=1 arms the one-shot
 * allocation probe (AdmissionPoolAllocProbeLocked); absent or other values
 * leave it off. */
static BOOLEAN AdmissionPoolAllocProbeEnabled(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1PoolAllocProbe");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* rc104: REG_DWORD Wom1PoolPressureWaitMs is the floor-wait budget of one
 * creation in milliseconds; 0 is the kill switch (refuse on the first
 * LowMemoryCondition sample, the pre-rc104 behaviour); absent or not a
 * REG_DWORD keeps ADMISSION_POOL_PRESSURE_DEFAULT_BUDGET_MS; larger values
 * are clamped to ADMISSION_POOL_PRESSURE_MAX_BUDGET_MS. */
static ULONGLONG AdmissionPoolPressureBudgetMs(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  ULONGLONG budget = ADMISSION_POOL_PRESSURE_DEFAULT_BUDGET_MS;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return budget;
  RtlInitUnicodeString(&name, L"Wom1PoolPressureWaitMs");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG))
    budget = AdmissionPoolPressureBudget((ULONGLONG)*(const ULONG *)value->Data);
  ZwClose(key);
  return budget;
}

/* rc109: one REG_DWORD of an open key; FALSE when absent or another type. */
static BOOLEAN AdmissionPoolReadDword(HANDLE Key, PCWSTR Name, ULONG *Value) {
  UNICODE_STRING name;
  ULONG bytes = 0u;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  RtlInitUnicodeString(&name, Name);
  if (!NT_SUCCESS(ZwQueryValueKey(Key, &name, KeyValuePartialInformation,
                                  value, sizeof(data.Buffer), &bytes)) ||
      value->Type != REG_DWORD || value->DataLength != sizeof(ULONG))
    return FALSE;
  *Value = *(const ULONG *)value->Data;
  return TRUE;
}

/* rc109: the machine policy VidMm sized TotalSystemMemoryAvailableForGraphics
 * from (render_pool_demand.h; dxgmms2 RVA 0xaa3c8 reads the same values from
 * the same key, once, when it initializes). VidMm keeps what it read at boot,
 * so a key written since this boot began (LastWriteTime after
 * now - interrupt time, which counts sleep) or whose time cannot be read is
 * marked WrittenAfterBoot: the share then never grows past the default one.
 * Flags: ADMISSION_POOL_POLICY_FLAG_*. PASSIVE_LEVEL. */
static VOID AdmissionPoolGraphicsPolicyRead(ADMISSION_POOL_GRAPHICS_POLICY *Policy,
                                            ULONG *Flags) {
  UNICODE_STRING path;
  OBJECT_ATTRIBUTES attributes;
  HANDLE key = NULL;
  ULONG value = 0u, bytes = 0u;
  LARGE_INTEGER now;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 64u * sizeof(WCHAR)];
  } info;
  PKEY_BASIC_INFORMATION basic = (PKEY_BASIC_INFORMATION)info.Buffer;
  NTSTATUS status;
  AdmissionPoolGraphicsPolicyDefault(Policy);
  *Flags = 0u;
  RtlInitUnicodeString(&path,
      L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\"
      L"GraphicsDrivers\\MemoryManager");
  InitializeObjectAttributes(&attributes, &path,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
  if (!NT_SUCCESS(ZwOpenKey(&key, KEY_QUERY_VALUE, &attributes))) return;
  *Flags |= ADMISSION_POOL_POLICY_FLAG_KEY;
  if (AdmissionPoolReadDword(key, L"SystemPartitionCommitLimitPercentage", &value)) {
    Policy->Percent = value;
    *Flags |= ADMISSION_POOL_POLICY_FLAG_PERCENT;
  }
  if (AdmissionPoolReadDword(key, L"SystemPartitionCommitLimitPercentageMax", &value))
    Policy->MaxPercent = value;
  if (AdmissionPoolReadDword(key, L"SystemPartitionCpuOnlyMemory", &value))
    Policy->CpuOnlyMiB = value;
  RtlZeroMemory(&info, sizeof(info));
  status = ZwQueryKey(key, KeyBasicInformation, basic, sizeof(info.Buffer), &bytes);
  KeQuerySystemTime(&now);
  if ((!NT_SUCCESS(status) && status != STATUS_BUFFER_OVERFLOW) ||
      basic->LastWriteTime.QuadPart >=
          now.QuadPart - (LONGLONG)KeQueryInterruptTime()) {
    Policy->WrittenAfterBoot = 1u;
    *Flags |= ADMISSION_POOL_POLICY_FLAG_AFTER_BOOT;
  }
  ZwClose(key);
}

/* rc109: REG_DWORD Wom1PoolPercent (device key, render_pool_demand.h);
 * absent or not a REG_DWORD: ADMISSION_POOL_PERCENT_DEFAULT; 0: kill switch. */
static ULONG AdmissionPoolPercent(ADMISSION_CONTEXT *Context, ULONG *Flags) {
  HANDLE key = NULL;
  ULONG percent = ADMISSION_POOL_PERCENT_DEFAULT, value = 0u;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return percent;
  if (AdmissionPoolReadDword(key, L"Wom1PoolPercent", &value)) {
    percent = value > 100u ? 100u : value;
    *Flags |= ADMISSION_POOL_POLICY_FLAG_POOL_PERCENT;
  }
  ZwClose(key);
  return percent;
}

/* RC13: freeze the experimental placement policy before allocation creation. */
static BOOLEAN AdmissionSysmemSpillEnabled(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1SysmemSpill");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* RC23: recognize the request without claiming shadow backing is supported.
 * CreateAllocation rejects eligible requests before taking any resources. */
static BOOLEAN AdmissionSysmemShadowRequested(ADMISSION_CONTEXT *Context) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, L"Wom1SysmemShadow");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* An empty segment over the window, sized for this machine (M2-f4a), with
 * an optional smaller residency bound. FALSE leaves no state behind. */
static BOOLEAN AdmissionPoolDemandStart(ADMISSION_CONTEXT *Context,
                                        ADMISSION_POOL_ATTEMPT *Window) {
  ADMISSION_POOL_DEMAND_OPS ops;
  ADMISSION_POOL_RUNTIME *pool;
  ULONGLONG ram = AdmissionPoolRamBytes();
  BOOLEAN halfSize = AdmissionPoolHalfSizeEnabled(Context);
  BOOLEAN threeQuarter = AdmissionPoolThreeQuarterEnabled(Context);
  ULONGLONG reserve = AdmissionPoolApertureReserveHalfEnabled(Context) ?
      ADMISSION_POOL_DEMAND_APERTURE_RESERVE_HALF :
      ADMISSION_POOL_DEMAND_APERTURE_RESERVE;
  ADMISSION_POOL_GRAPHICS_POLICY policy;
  ULONG policyFlags = 0u, percent;
  ULONGLONG share, baseBytes, bytes, count;
  PHYSICAL_ADDRESS base;
  /* rc109: the machine policy VidMm enforces bounds Wom1PoolPercent; the
   * default formula alone (the kill switch) gives the base size. */
  AdmissionPoolGraphicsPolicyRead(&policy, &policyFlags);
  percent = AdmissionPoolPercent(Context, &policyFlags);
  share = AdmissionPoolDemandShareBytes(ram, &policy, percent);
  baseBytes = AdmissionPoolSegmentBytes(ram, Window->RequestedBytes,
      Context->PoolBrokerBytes, halfSize, threeQuarter, reserve);
  bytes = AdmissionPoolSegmentBytesShare(ram, Window->RequestedBytes,
      Context->PoolBrokerBytes, halfSize, threeQuarter, reserve, share);
  if (bytes < baseBytes) baseBytes = bytes; /* a lowered machine policy */
  count = bytes / ADMISSION_POOL_DEMAND_GRANULE;
  if (bytes == 0ULL) {
    Window->Status = ADMISSION_POOL_WINDOW_DEMAND_TOO_SMALL;
    return FALSE;
  }
  pool = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*pool), ADMISSION_POOL_TAG);
  if (pool == NULL) return FALSE;
  pool->RamBytes = ram;
  pool->Policy = policy;
  pool->PolicyFlags = policyFlags;
  pool->PoolPercent = percent;
  pool->GraphicsShareBytes = share;
  pool->BaseSegmentBytes = baseBytes;
  pool->BaseGranules = baseBytes / ADMISSION_POOL_DEMAND_GRANULE;
  pool->NeutralHeadroomGranules =
      ADMISSION_POOL_NEUTRAL_HEADROOM / ADMISSION_POOL_DEMAND_GRANULE;
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
  /* rc88: without its work item the pool runs as before, unprefilled. */
  pool->PrefillEnabled = AdmissionPoolPrefillEnabled(Context);
  pool->ConstructLargePages = AdmissionPoolConstructLargePagesEnabled(Context);
  pool->AllocProbe = AdmissionPoolAllocProbeEnabled(Context);
  pool->Pressure.BudgetMs = AdmissionPoolPressureBudgetMs(Context);
  if (pool->PrefillEnabled)
    pool->PrefillWork = IoAllocateWorkItem(Context->PhysicalDeviceObject);
  pool->Demand.ReleaseDelay = ADMISSION_POOL_RELEASE_DELAY_MS;
  pool->Demand.MaxRun = ADMISSION_POOL_MAX_RUN;
  pool->Slabs.Context = pool;
  pool->Slabs.NewSlab = AdmissionPoolNewSlab;
  pool->Slabs.FreeSlab = AdmissionPoolFreeSlab;
  pool->HighMemory = AdmissionPoolOpenEvent(L"\\KernelObjects\\HighMemoryCondition");
  pool->LowMemory = AdmissionPoolOpenEvent(L"\\KernelObjects\\LowMemoryCondition");
  pool->Slabs.MayGrow = AdmissionPoolMayGrow;
  pool->Slabs.SpareLimit = ADMISSION_POOL_CACHE_SLABS;
  /* M2-f5: VidMm never places more than the segment. */
  pool->Slabs.ReserveCap = count;
  /* rc80: TVB window blocks lease raw granules beyond that promise. */
  pool->Slabs.PrivateCap = AppleAgxG3TvbGlobalGranules(ram);
  ExInitializeFastMutex(&pool->Lock);
  KeInitializeTimer(&pool->Timer);
  KeInitializeDpc(&pool->Dpc, AdmissionPoolReclaimDpc, pool);
  pool->Context = Context;
  base.QuadPart = (LONGLONG)Window->SkipBytes;
  Context->PoolCpu = MmMapIoSpaceEx(base, (SIZE_T)bytes,
                                    PAGE_READWRITE | PAGE_WRITECOMBINE);
  if (Context->PoolCpu == NULL) {
    Window->Status = 5u;
    AdmissionPoolDemandFree(pool);
    return FALSE;
  }
  Context->PoolIpa = Window->SkipBytes;
  Context->PoolBytes = bytes;
  Context->PoolDemand = pool;
  Window->Status = ADMISSION_POOL_WINDOW_DEMAND;
  Window->RequestedBytes = bytes;
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
  Context->SysmemSpill = AdmissionSysmemSpillEnabled(Context);
  Context->SysmemShadow = AdmissionSysmemShadowRequested(Context);
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
