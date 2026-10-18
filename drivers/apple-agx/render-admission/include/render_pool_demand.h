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
#define ADMISSION_POOL_DEMAND_RECEIPT_VERSION 7u /* 2: ReleaseDelayMs; 3: Stats.CommitCalls; 4: slabs; 5: RamBytes; 6: reservations; 7: BrokerBytes */
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

#endif
