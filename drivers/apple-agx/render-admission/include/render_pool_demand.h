#ifndef APPLE_AGX_RENDER_POOL_DEMAND_H
#define APPLE_AGX_RENDER_POOL_DEMAND_H

/* M2-f3 (investigation/analysis/M2-f3-demand-pool.md): segment 3 is backed
 * on demand, one 64 KiB granule at a time. VidMm places allocations in the
 * segment without telling the driver; the driver learns a placement from the
 * PTEs that reference it. A granule is committed before its first reference
 * is published and released only when no placement owns it, no
 * paging-process PTE maps it, every paging record that named it has
 * executed, and the window agrees to unmap it (no GPU backing). */
#define ADMISSION_POOL_DEMAND_GRANULE 0x10000ULL

/* One allocation's current segment-3 placement; Bytes 0 means none. Lives in
 * the allocation and must be unplaced before the allocation is freed. */
typedef struct _ADMISSION_POOL_PLACEMENT {
  unsigned long long Base;
  unsigned long long Bytes;
} ADMISSION_POOL_PLACEMENT;

typedef struct _ADMISSION_POOL_GRANULE {
  ADMISSION_POOL_PLACEMENT *Owner;
  unsigned long long RecordSeq; /* last paging record naming the granule */
  /* Aged reclaim: 1 + the time it was first seen releasable; 0 not yet. */
  unsigned long long IdleSince;
  /* Ordinary reclaim retries Busy only after a newer executed paging epoch.
   * The periodic Windows worker also clears Busy under the pool lock: broker
   * holds can end during graph teardown with no further paging execution. */
  unsigned long long BusySeq;
  unsigned char Busy;
  unsigned char BusyAnyExecuted;
  unsigned int PagingRefs;      /* paging-process 4 KiB PTEs into it */
  unsigned char Committed;
  unsigned char Pinned;         /* committed until the pool stops */
  unsigned char Recorded;       /* RecordSeq is meaningful */
  unsigned char Candidate;      /* on the reclaim list */
} ADMISSION_POOL_GRANULE;

typedef enum _ADMISSION_POOL_RELEASE {
  AdmissionPoolReleased = 0,
  AdmissionPoolReleaseBusy = 1,   /* a GPU backing still holds a page */
  AdmissionPoolReleaseFailed = 2  /* the granule is kept for good */
} ADMISSION_POOL_RELEASE;

typedef struct _ADMISSION_POOL_DEMAND_OPS {
  void *Context;
  /* Backs granules [First, First + Count) with fresh zeroed pages and maps
   * them, all or nothing; nonzero on success. M2-f3c: one call per run
   * (EXP1258: one page allocation per 64 KiB cost 460-875 us). */
  int (*Commit)(void *Context, unsigned long long First,
                unsigned long long Count);
  ADMISSION_POOL_RELEASE (*Release)(void *Context, unsigned long long Index);
} ADMISSION_POOL_DEMAND_OPS;

typedef struct _ADMISSION_POOL_DEMAND_STATS {
  unsigned long long CommittedGranules, PeakGranules;
  unsigned long long Commits, CommitFailures;
  unsigned long long Releases, BusyRetries, ReleaseFailures;
  unsigned long long Places, Moves, Kills, Unplaces;
  unsigned long long PinnedGranules, Unaligned, RefUnderflows;
  unsigned long long CommitCalls; /* M2-f3c: runs committed */
} ADMISSION_POOL_DEMAND_STATS;

typedef struct _ADMISSION_POOL_DEMAND {
  ADMISSION_POOL_DEMAND_OPS Ops;
  ADMISSION_POOL_GRANULE *Granules;
  unsigned long long *CandidateList; /* Count entries */
  unsigned long long Count, Candidates;
  unsigned long long ExecutedSeq;
  int AnyExecuted;
  /* M2-f3b: AdmissionPoolDemandReclaimAged keeps a releasable granule
   * committed (and mapped) until it has been idle this long, in the caller's
   * time unit: VidMm reuses freed ranges at once (EXP1257: 8k commits and
   * releases in one benchmark), and an idle granule is reused for free. */
  unsigned long long ReleaseDelay;
  /* Most granules one Commit call backs; 0: no limit. */
  unsigned long long MaxRun;
  ADMISSION_POOL_DEMAND_STATS Stats;
} ADMISSION_POOL_DEMAND;

typedef enum _ADMISSION_POOL_PLACE {
  AdmissionPoolPlaced = 0,
  AdmissionPoolPlaceInvalid = 1,   /* not granule-aligned or past the end */
  AdmissionPoolPlaceNoMemory = 2   /* a commit failed; nothing is owned */
} ADMISSION_POOL_PLACE;

/* Receipt Wom1M2PoolDemand (pool_windows.c), staged when the committed size
 * moves by ADMISSION_POOL_DEMAND_RECEIPT_STEP granules or a failure count
 * changes. Times are microseconds of commit (page allocation + MAP_AT) and
 * release (UNMAP_RANGE + page free). */
/* v9 records the event samples used by MayGrow, not a later resample.
 * Counts in the original prefix are after rollback, before pressure reclaim.
 * Decision granules describe the attempted floor before rollback; arithmetic
 * failure has RequiredGranules=0 (not evaluated). Events: 0 clear, 1 set,
 * 2 unavailable/not sampled. High/Low are sequential samples, not atomic. */
#define ADMISSION_POOL_REFUSAL_NONE 0u
#define ADMISSION_POOL_REFUSAL_LOW_MEMORY 1u
#define ADMISSION_POOL_REFUSAL_NO_SIGNAL 2u
#define ADMISSION_POOL_REFUSAL_BACKING 3u
#define ADMISSION_POOL_REFUSAL_ARITHMETIC 4u
#define ADMISSION_POOL_REFUSAL_COUNT 4u
typedef struct _ADMISSION_POOL_RESERVE_FAILURE {
  unsigned long long RequestBytes, ReservedBytes, HeldBytes, CommittedBytes;
  unsigned long long TimeMs;
  unsigned int HighMemory, LowMemory;
  unsigned int Reason, Reserved;
  unsigned long long RequiredGranules, HeldGranules, ReserveCapGranules;
  unsigned long long PrivateGranules;
} ADMISSION_POOL_RESERVE_FAILURE;

#define ADMISSION_POOL_DEMAND_RECEIPT_VERSION 15u /* v15: graphics policy */
#define ADMISSION_POOL_DEMAND_RECEIPT_STEP 256u /* 16 MiB */
typedef struct _ADMISSION_POOL_DEMAND_RECEIPT {
  unsigned int Version, Bytes, GranuleBytes, LastWindowResult;
  unsigned long long SegmentBytes;
  ADMISSION_POOL_DEMAND_STATS Stats;
  unsigned long long CommitUs, CommitUsMax, ReleaseUs, ReleaseUsMax;
  unsigned long long Candidates, ExecutedSeq;
  unsigned long long ReleaseDelayMs;
  /* v3: granules whose pages the driver holds (mapped or awaiting the rest
   * of their commit run). */
  unsigned long long HeldGranules;
  /* v4 (M2-f3e): 2 MiB slabs; contiguous or 32 x 64 KiB chunks. */
  unsigned long long Slabs, PeakSlabs, NewSlabs, NewSlabFailures, FreedSlabs;
  unsigned long long SlabContiguous, SlabChunked;
  /* v5 (M2-f4a): the RAM SegmentBytes was derived from. */
  unsigned long long RamBytes;
  /* v6 (M2-f5): pages promised to live segment-3 allocations at creation,
   * the peak, and creations refused for want of Windows pages. */
  unsigned long long ReservedBytes, PeakReservedBytes, ReserveFailures;
  /* v7 (M2-f6): the broker table the segment size used (0: built-in). */
  unsigned long long BrokerBytes;
  /* v8: retained even after subsequent successful reservations. */
  unsigned long long FloorRefusals;
  ADMISSION_POOL_RESERVE_FAILURE LastReserveFailure;
  /* Index is ADMISSION_POOL_REFUSAL_* minus one. Totals include every refusal,
   * even when registry staging coalesces multiple decisions. */
  unsigned long long RefusalCounts[ADMISSION_POOL_REFUSAL_COUNT];
  unsigned long long NativeStagingBytes, TotalPromisedBytes;
  /* v11 (rc86): where a creation's time goes, in microseconds. SlabAlloc*:
   * MmAllocatePagesForMdlEx calls (contiguous and chunked attempts, failed
   * ones included; Windows zeroes the pages inside the call). SlabCheck: the
   * PFN contiguity/alignment check. SlabFree: MmFreePagesFromMdl. Reserve*:
   * AdmissionMemoryPoolReserve under the pool lock (slab growth plus the
   * pressure reclaim); the rest of pfnAllocateCb is VidMm's. CommitUs above
   * is the window MAP_AT of placements. ReserveNoGrowth: reservations held
   * or cached slabs covered. LeaseZero*: rc80 TVB lease clears, and clears
   * skipped because the slot was still Windows-zeroed. SpareLimit/
   * EmptySlabs: the empty-slab cache; CacheDrops: times it was returned
   * (pressure or idle). */
  unsigned long long SlabAllocCalls, SlabAllocUs, SlabAllocUsMax, SlabCheckUs;
  unsigned long long SlabFreeUs, SlabFreeUsMax;
  unsigned long long ReserveCalls, ReserveUs, ReserveUsMax, ReserveNoGrowth;
  unsigned long long LeaseZeroCount, LeaseZeroUs, LeaseZeroSkips;
  unsigned long long SpareLimit, EmptySlabs, CacheDrops;
  /* v12 (rc88): slabs the background prefill added to the empty-slab cache,
   * its wall time (page allocation outside the pool lock, microseconds),
   * slabs it allocated but could not keep or could not get, and the time of
   * MmAllocatePagesForMdlEx calls whose result was not a usable slab (a
   * refused or non-contiguous 2 MiB attempt before its 64 KiB fallback). */
  unsigned long long PrefillSlabs, PrefillUs, PrefillDiscards, SlabFailUs;
  /* v13 (rc93): 2 MiB attempts refused at once because Windows' large-page
   * cache was empty (MM_ALLOCATE_FAST_LARGE_PAGES), the time of the attempts
   * that produced a slab by shape (2 MiB contiguous, 32 x 64 KiB chunked;
   * microseconds, so per-slab cost = Us / SlabContiguous or SlabChunked),
   * and 1 when Wom1PoolConstructLargePages=1 restored construction. */
  unsigned long long SlabFastMisses, SlabContiguousUs, SlabChunkedUs;
  unsigned long long ConstructLargePages;
  /* v14 (rc104): the floor wait (ADMISSION_POOL_PRESSURE; PressureBudgetMs
   * 0 = Wom1PoolPressureWaitMs kill switch), and the live allocations that
   * hold a segment-3 promise (ReservedBytes is their total). The periodic
   * worker also stages any change, so a receipt read once the pool is idle
   * is exact rather than up to one RECEIPT_STEP stale. */
  unsigned long long PressureBudgetMs, PressureWaits, PressureWaitMs;
  unsigned long long PressureWaitMsMax, PressureRecovered, PressureFailed;
  unsigned long long PressureFastFails, ReservedAllocations;
  /* v15 (rc109): the graphics share segment 3 was sized from
   * (AdmissionPoolDemandShareBytes), the size the default formula alone
   * gives (Wom1PoolPercent=0, after HalfSize/Fraction), the Wom1PoolPercent
   * used, the MemoryManager values read (defaults when absent),
   * ADMISSION_POOL_POLICY_FLAG_*, and growth refused past BaseSegmentBytes
   * for want of HighMemoryCondition (ADMISSION_POOL_REFUSAL_HEADROOM). */
  unsigned long long GraphicsShareBytes, BaseSegmentBytes;
  unsigned int PoolPercent, PolicyPercent, PolicyMaxPercent, PolicyCpuOnlyMiB;
  unsigned int PolicyFlags, PolicyReserved;
  unsigned long long HeadroomRefusals;
} ADMISSION_POOL_DEMAND_RECEIPT;

/* M2-f4a: the declared size of the demand-backed segment 3 for a machine.
 * Backing follows use, so the size only bounds what VidMm may place there.
 * Windows lets PopulatedFromSystemMemory segments total at most
 * TotalSystemMemoryAvailableForGraphics = MIN(80%, MAX(RAM - 16 GB, 50%)) of
 * RAM (Microsoft, "Calculating graphics memory"); the aperture keeps
 * ADMISSION_POOL_DEMAND_APERTURE_RESERVE of shared system memory. Every
 * published segment-3 page is one m1n1 GPU-broker system record
 * (HV_AGX_GPUVA_V5_BACKINGS x 16 KiB = 2 GiB, shared with VidMm's system
 * pages, which keep ADMISSION_POOL_DEMAND_BROKER_RESERVE). Result in 64 MiB
 * steps, at most WindowBytes; 0 below 256 MiB (no pool); RamBytes 0 (unknown)
 * gives ADMISSION_POOL_DEMAND_DEFAULT_BYTES. */
#define ADMISSION_POOL_DEMAND_BROKER_BYTES 0x80000000ULL      /* 2 GiB */
#define ADMISSION_POOL_DEMAND_BROKER_RESERVE 0x10000000ULL    /* 256 MiB */
#define ADMISSION_POOL_DEMAND_APERTURE_RESERVE 0x10000000ULL  /* 256 MiB */
#define ADMISSION_POOL_DEMAND_DEFAULT_BYTES 0x40000000ULL     /* 1 GiB (EXP1262) */
#define ADMISSION_POOL_DEMAND_SIZE_STEP 0x4000000ULL          /* 64 MiB */
#define ADMISSION_POOL_DEMAND_MIN_BYTES 0x10000000ULL         /* 256 MiB */
unsigned long long AdmissionPoolDemandSegmentBytes(unsigned long long RamBytes,
                                                   unsigned long long WindowBytes);
/* M2-f6: BrokerBytes is the GPU broker table m1n1 sized from RAM
 * (AGX_POOL_WINDOW_REG_BROKER_PAGES x 16 KiB); 0 means the built-in
 * ADMISSION_POOL_DEMAND_BROKER_BYTES table of an older hypervisor. */
unsigned long long AdmissionPoolDemandSegmentBytesEx(unsigned long long RamBytes,
                                                     unsigned long long WindowBytes,
                                                     unsigned long long BrokerBytes);
/* rc88: as Ex, with ApertureReserve in place of
 * ADMISSION_POOL_DEMAND_APERTURE_RESERVE. The shared system memory left to
 * the aperture (NON_LOCAL group: TotalSystemMemoryAvailableForGraphics less
 * the PopulatedFromSystemMemory segments) shrinks by what segment 3 gains.
 * ADMISSION_POOL_DEMAND_APERTURE_RESERVE_HALF is the only other reserve
 * pool_windows.c uses (Wom1PoolApertureReserveHalf=1): it keeps the native
 * staging cap (64 MiB, a quarter of the aperture CommitLimit) under half of
 * the NON_LOCAL budget VidMm derives from it (EXP1264: 90% of the group). */
#define ADMISSION_POOL_DEMAND_APERTURE_RESERVE_HALF 0x8000000ULL /* 128 MiB */
unsigned long long AdmissionPoolDemandSegmentBytesReserve(
    unsigned long long RamBytes, unsigned long long WindowBytes,
    unsigned long long BrokerBytes, unsigned long long ApertureReserve);

/* rc109 (EXP2046: FHD 200k outgrew the 3136 MiB segment). The 50% is not
 * this driver's choice: the guest's dxgmms2 (26200, sha256 abeb2c41...,
 * RVA 0xe4438-0xe448c) refuses a PopulatedFromSystemMemory segment larger
 * than VidMm's shared-system figure less 8 MiB (debug text "Driver
 * dedicated too much system memory for video, system policy violated.",
 * STATUS_UNSUCCESSFUL), so the adapter does not start. That figure
 * (RVA 0x74830) is TotalSystemMemoryAvailableForGraphics less the dedicated
 * system memory already counted (none before segment 3: segment 2 is
 * PopulatedByReservedDDRByFirmware). VidMm derives it from machine policy it
 * reads once, at its own initialization (RVA 0xaa3c8, RtlQueryRegistryValues
 * on HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\MemoryManager):
 *   P = SystemPartitionCommitLimitPercentage clamped to [5, 100] (default 50)
 *   M = MAX(P, MIN(SystemPartitionCommitLimitPercentageMax, 100)) (default 80)
 *   C = SystemPartitionCpuOnlyMemory, MiB (default 16384)
 *   Share = MIN(RAM * M%, MAX(RAM - C, RAM * P%))
 * which is Microsoft's documented formula with the defaults ("Calculating
 * graphics memory": dedicated system memory "can't be greater than"
 * TotalSystemMemoryAvailableForGraphics). VidMm's other path (a WIL feature
 * flag off) is RAM * P%, never larger than the above. */
#define ADMISSION_POOL_POLICY_PERCENT 50u
#define ADMISSION_POOL_POLICY_MAX_PERCENT 80u
#define ADMISSION_POOL_POLICY_CPU_ONLY_MIB 16384u
#define ADMISSION_POOL_POLICY_MIN_PERCENT 5u
#define ADMISSION_POOL_POLICY_SEGMENT_MARGIN 0x800000ULL /* dxgmms2: 8 MiB */
typedef struct _ADMISSION_POOL_GRAPHICS_POLICY {
  /* Registry values as read (defaults when absent); clamped when used. */
  unsigned int Percent, MaxPercent, CpuOnlyMiB;
  /* Nonzero: the MemoryManager key was written after this boot began, so
   * VidMm may still use older values; the share then never exceeds the
   * default one. */
  unsigned int WrittenAfterBoot;
} ADMISSION_POOL_GRAPHICS_POLICY;
void AdmissionPoolGraphicsPolicyDefault(ADMISSION_POOL_GRAPHICS_POLICY *Policy);
/* VidMm's TotalSystemMemoryAvailableForGraphics for Policy (0: defaults). */
unsigned long long AdmissionPoolGraphicsShareBytes(
    unsigned long long RamBytes, const ADMISSION_POOL_GRAPHICS_POLICY *Policy);

/* rc109: REG_DWORD Wom1PoolPercent (device key) asks for segment 3 plus the
 * aperture reserve to cover this percentage of RAM, like macOS letting the
 * GPU working set reach about 2/3-3/4 of RAM (recommendedMaxWorkingSetSize).
 * The result never exceeds the share VidMm enforces (above), never falls
 * below the default share, and 0 is the kill switch: the pre-rc109 size from
 * the default formula alone. Absent: ADMISSION_POOL_PERCENT_DEFAULT; above
 * 100: 100. With Windows' default policy (50%) every value gives the
 * pre-rc109 size; the segment grows only where the machine policy
 * (SystemPartitionCommitLimitPercentage) allows it. */
#define ADMISSION_POOL_PERCENT_DEFAULT 75u
#define ADMISSION_POOL_PERCENT_KILL 0u
/* Receipt v15 PolicyFlags: the MemoryManager key exists, it holds
 * SystemPartitionCommitLimitPercentage, it was written after boot (or its
 * time is unreadable), the device key holds Wom1PoolPercent. */
#define ADMISSION_POOL_POLICY_FLAG_KEY 1u
#define ADMISSION_POOL_POLICY_FLAG_PERCENT 2u
#define ADMISSION_POOL_POLICY_FLAG_AFTER_BOOT 4u
#define ADMISSION_POOL_POLICY_FLAG_POOL_PERCENT 8u
unsigned long long AdmissionPoolDemandShareBytes(
    unsigned long long RamBytes, const ADMISSION_POOL_GRAPHICS_POLICY *Policy,
    unsigned int PoolPercent);
/* As SegmentBytesReserve, from a graphics share (AdmissionPoolDemandShareBytes)
 * instead of the default formula; the result is at most ShareBytes less
 * ADMISSION_POOL_POLICY_SEGMENT_MARGIN. RamBytes 0 (unknown) keeps
 * ADMISSION_POOL_DEMAND_DEFAULT_BYTES. */
unsigned long long AdmissionPoolDemandSegmentBytesShare(
    unsigned long long RamBytes, unsigned long long WindowBytes,
    unsigned long long BrokerBytes, unsigned long long ApertureReserve,
    unsigned long long ShareBytes);

/* Granules and CandidateList hold Count zeroed entries each. */
int AdmissionPoolDemandInit(ADMISSION_POOL_DEMAND *Pool,
                            ADMISSION_POOL_GRANULE *Granules,
                            unsigned long long *CandidateList,
                            unsigned long long Count,
                            const ADMISSION_POOL_DEMAND_OPS *Ops);

/* Owner now occupies [Base, Base + Bytes): an older placement of Owner and
 * every other placement overlapping the range lose their granules. */
ADMISSION_POOL_PLACE AdmissionPoolDemandPlace(ADMISSION_POOL_DEMAND *Pool,
                                              ADMISSION_POOL_PLACEMENT *Owner,
                                              unsigned long long Base,
                                              unsigned long long Bytes);
void AdmissionPoolDemandUnplace(ADMISSION_POOL_DEMAND *Pool,
                                ADMISSION_POOL_PLACEMENT *Owner);
/* Commits the granules under [Offset, Offset + Bytes) for good (a reference
 * outside the placement contract); zero when a commit failed. */
int AdmissionPoolDemandPin(ADMISSION_POOL_DEMAND *Pool,
                           unsigned long long Offset, unsigned long long Bytes);
/* A paging-process PTE to the page at Offset was set (+1) or replaced (-1). */
void AdmissionPoolDemandPagingRef(ADMISSION_POOL_DEMAND *Pool,
                                  unsigned long long Offset, int Delta);
/* Paging record Seq (increasing) reads or writes [Offset, Offset + Bytes). */
void AdmissionPoolDemandRecord(ADMISSION_POOL_DEMAND *Pool,
                               unsigned long long Offset,
                               unsigned long long Bytes,
                               unsigned long long Seq);
/* Every record up to Seq has executed. */
void AdmissionPoolDemandExecuted(ADMISSION_POOL_DEMAND *Pool,
                                 unsigned long long Seq);
/* Releases every releasable granule; returns how many were released. */
unsigned long long AdmissionPoolDemandReclaim(ADMISSION_POOL_DEMAND *Pool);
/* As Reclaim, for granules releasable for ReleaseDelay by Now (increasing). */
unsigned long long AdmissionPoolDemandReclaimAged(ADMISSION_POOL_DEMAND *Pool,
                                                  unsigned long long Now);

/* rc104 (EXP2033 arm e): a creation refused at the Windows memory floor
 * (LowMemoryCondition set, ADMISSION_POOL_REFUSAL_LOW_MEMORY) waits for the
 * memory manager to rebalance, then retries, instead of failing on one
 * sample. The floor itself is unchanged: no slab is taken while LowMemory is
 * set, the wait runs without the pool lock, and only that refusal reason
 * waits (backing, missing signal and arithmetic refusals fail at once).
 * BudgetMs bounds the wait of one creation (0: never wait, the pre-rc104
 * behaviour). A creation that waited its whole budget and still failed opens
 * a FastFail window in which later floor refusals do not wait (a doomed
 * workload fails in about one budget, not one budget per creation); the
 * floor is still sampled fresh by every creation, so memory that recovers
 * inside the window is used at once. All calls under the pool lock. */
#define ADMISSION_POOL_PRESSURE_STEP_MS 50ULL
#define ADMISSION_POOL_PRESSURE_DEFAULT_BUDGET_MS 2000ULL
#define ADMISSION_POOL_PRESSURE_MAX_BUDGET_MS 10000ULL
#define ADMISSION_POOL_PRESSURE_FAST_FAIL_MS 5000ULL
typedef struct _ADMISSION_POOL_PRESSURE {
  unsigned long long BudgetMs;
  unsigned long long FastFailUntilMs;
  /* Receipt: creations that waited, their total and longest wait (ms, wall),
   * those that then reserved, those that still failed (budget spent, or a
   * later non-floor refusal), and floor refusals that did not wait because
   * of the FastFail window. */
  unsigned long long Waits, WaitMs, WaitMsMax, Recovered, TimedOut, FastFails;
} ADMISSION_POOL_PRESSURE;
/* Registry value -> budget: 0 stays 0, larger values are clamped. */
unsigned long long AdmissionPoolPressureBudget(unsigned long long RequestedMs);
/* The next wait (ms) of a creation that the floor refused after it was
 * charged ChargedMs of waiting; 0: stop waiting and fail. */
unsigned long long AdmissionPoolPressureNextWait(ADMISSION_POOL_PRESSURE *Pressure,
                                                 unsigned long long NowMs,
                                                 unsigned long long ChargedMs);
/* End of a creation the floor refused at least once: ChargedMs as above,
 * WallMs the time it actually slept, Reserved its outcome. */
void AdmissionPoolPressureDone(ADMISSION_POOL_PRESSURE *Pressure,
                               unsigned long long NowMs,
                               unsigned long long ChargedMs,
                               unsigned long long WallMs, int Reserved);

#endif
