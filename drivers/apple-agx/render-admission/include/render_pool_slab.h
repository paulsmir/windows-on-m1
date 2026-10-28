#ifndef APPLE_AGX_RENDER_POOL_SLAB_H
#define APPLE_AGX_RENDER_POOL_SLAB_H

/* M2-f3e: the demand pool's pages come from Windows in 2 MiB slabs of 32
 * granule slots (64 KiB each), not one allocation per granule. EXP1259-1261:
 * Windows' cost is per contiguous chunk (~258 us per 64 KiB chunk, worse per
 * 16 KiB chunk) while one contiguous 1 GiB took 374 ms, so a slab amortizes
 * one search over 32 granules. Slots of partly used slabs are handed out
 * first (packing); a slab goes back to Windows when its last slot does,
 * beyond SpareLimit empty slabs kept for the next commit. */
#define ADMISSION_POOL_SLAB_SLOTS 32u

struct _ADMISSION_POOL_PRIVATE;

typedef struct _ADMISSION_POOL_SLAB {
  void *Handle;                                /* the owner's page list */
  unsigned long long Pa[ADMISSION_POOL_SLAB_SLOTS]; /* 64 KiB-aligned */
  unsigned int FreeMask;                       /* bit i: slot i free */
  unsigned int Live;                           /* slots handed out */
  /* rc86: bit i: slot i still holds the zeroed pages NewSlab returned and
   * has never been handed out since (no window map, no lease use). */
  unsigned int CleanMask;
  struct _ADMISSION_POOL_PRIVATE *PrivateOwner[ADMISSION_POOL_SLAB_SLOTS];
  struct _ADMISSION_POOL_SLAB *Next;
} ADMISSION_POOL_SLAB;

typedef struct _ADMISSION_POOL_SLAB_STATS {
  unsigned long long Slabs, PeakSlabs, NewSlabs, NewSlabFailures, FreedSlabs;
  /* M2-f5: reservations Windows could not back, and the largest total. */
  unsigned long long ReserveFailures, PeakReserved;
  /* M2-f7: reservations refused at the Windows memory floor (MayGrow 0). */
  unsigned long long FloorRefusals;
  /* rc86: successful reservations that took no slab from Windows (held or
   * cached empty slots covered them). */
  unsigned long long ReserveNoGrowth;
} ADMISSION_POOL_SLAB_STATS;

typedef struct _ADMISSION_POOL_SLABS {
  void *Context;
  /* A slab with Handle and every Pa filled (FreeMask/Live/Next are set
   * here), or 0 when Windows has no pages. */
  ADMISSION_POOL_SLAB *(*NewSlab)(void *Context);
  void (*FreeSlab)(void *Context, ADMISSION_POOL_SLAB *Slab);
  /* M2-f7: 0 while Windows has too little free memory (pool_windows.c:
   * HighMemoryCondition not set): a reservation then takes no slab. Optional; the paging path
   * (Take) is not gated, its failure poisons a process (M2-f5). */
  int (*MayGrow)(void *Context);
  ADMISSION_POOL_SLAB *List;
  unsigned long long SpareLimit, Empty;
  /* M2-f5: slots promised to live allocations. The slabs held never fall
   * below MIN(Reserved, ReserveCap) slots (ReserveCap: the segment, beyond
   * which VidMm cannot place more), so a reserved placement never waits for
   * Windows. */
  unsigned long long Reserved, ReserveCap;
  /* Raw private slots are additional to the advertised-segment promise.
   * PrivateCap is fixed before use; zero disables acquisition. */
  unsigned long long PrivateCap, PrivateCount;
  ADMISSION_POOL_SLAB_STATS Stats;
} ADMISSION_POOL_SLABS;

/* Exclusive raw 64-KiB slab slot, not a pool-window offset or GPU grant.
 * Initialize to zero; keep its address stable and never copy a live lease.
 * All slab APIs require the caller's pool lock. Acquire only at a fallible
 * boundary. Release only after copies, aliases and GPU references are retired.
 * The allocator cannot prove those external lifetime conditions. */
typedef struct _ADMISSION_POOL_PRIVATE {
  ADMISSION_POOL_SLAB *Slab;
  unsigned int Slot;
  /* rc86: the slot's bytes are known zero. TakePrivate sets it from the
   * slab's CleanMask; the owner clears it once it exposes the slot and sets
   * it again only after zeroing, so GivePrivate can keep the slot clean. */
  unsigned int Zero;
} ADMISSION_POOL_PRIVATE;
int AdmissionPoolSlabTakePrivate(ADMISSION_POOL_SLABS *Slabs,
                                 ADMISSION_POOL_PRIVATE *Lease);
/* Whole-allocation acquisition. Leases has Count zero-initialized,
 * address-stable entries. Reject zero Count or any active entry; on failure
 * release the acquired prefix, preserving existing leases and reservations.
 * The cap bounds Count before inspecting the array. Same lock/lifetime rules
 * as TakePrivate; each 64-KiB chunk is contiguous, not the whole range. */
int AdmissionPoolSlabTakePrivateRange(ADMISSION_POOL_SLABS *Slabs,
    unsigned long long Count, ADMISSION_POOL_PRIVATE *Leases);
int AdmissionPoolSlabGivePrivate(ADMISSION_POOL_SLABS *Slabs,
                                 ADMISSION_POOL_PRIVATE *Lease);

/* Hands out Count slots (all or nothing): Slab[i]/Slot[i] for i < Count. */
int AdmissionPoolSlabTake(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count,
                          ADMISSION_POOL_SLAB **Slab, unsigned int *Slot);
void AdmissionPoolSlabGive(ADMISSION_POOL_SLABS *Slabs, ADMISSION_POOL_SLAB *Slab,
                           unsigned int Slot);
/* M2-f5: promise Count more slots, taking slabs from Windows until the held
 * slots cover MIN(Reserved, ReserveCap). All or nothing: on a refusal the
 * promise and the slabs taken for it are undone and 0 is returned. M2-f7:
 * a slab is taken only while MayGrow allows it. */
int AdmissionPoolSlabReserve(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count);
/* Ends a promise (clamped at zero) and returns empty slabs it no longer needs. */
void AdmissionPoolSlabUnreserve(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count);
/* rc86: nonzero when more than Keep empty slabs could go back to Windows
 * now (beyond Keep and still covering every promise). */
int AdmissionPoolSlabHasSurplus(const ADMISSION_POOL_SLABS *Slabs,
                                unsigned long long Keep);
/* Frees every slab (stop); normal slots still handed out are the caller's
 * leak. With private leases outstanding retain the entire pool: a caller must
 * retire/release them first, or retain the pool and MDLs on uncertain revoke. */
void AdmissionPoolSlabDrain(ADMISSION_POOL_SLABS *Slabs);

#endif
