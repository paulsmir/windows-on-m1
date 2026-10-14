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
  /* Backs granule Index with fresh zeroed pages and maps it; nonzero on
   * success. */
  int (*Commit)(void *Context, unsigned long long Index);
  ADMISSION_POOL_RELEASE (*Release)(void *Context, unsigned long long Index);
} ADMISSION_POOL_DEMAND_OPS;

typedef struct _ADMISSION_POOL_DEMAND_STATS {
  unsigned long long CommittedGranules, PeakGranules;
  unsigned long long Commits, CommitFailures;
  unsigned long long Releases, BusyRetries, ReleaseFailures;
  unsigned long long Places, Moves, Kills, Unplaces;
  unsigned long long PinnedGranules, Unaligned, RefUnderflows;
} ADMISSION_POOL_DEMAND_STATS;

typedef struct _ADMISSION_POOL_DEMAND {
  ADMISSION_POOL_DEMAND_OPS Ops;
  ADMISSION_POOL_GRANULE *Granules;
  unsigned long long *CandidateList; /* Count entries */
  unsigned long long Count, Candidates;
  unsigned long long ExecutedSeq;
  int AnyExecuted;
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
#define ADMISSION_POOL_DEMAND_RECEIPT_VERSION 1u
#define ADMISSION_POOL_DEMAND_RECEIPT_STEP 256u /* 16 MiB */
typedef struct _ADMISSION_POOL_DEMAND_RECEIPT {
  unsigned int Version, Bytes, GranuleBytes, LastWindowResult;
  unsigned long long SegmentBytes;
  ADMISSION_POOL_DEMAND_STATS Stats;
  unsigned long long CommitUs, CommitUsMax, ReleaseUs, ReleaseUsMax;
  unsigned long long Candidates, ExecutedSeq;
} ADMISSION_POOL_DEMAND_RECEIPT;

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

#endif
