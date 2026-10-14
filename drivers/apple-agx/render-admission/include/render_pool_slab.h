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

typedef struct _ADMISSION_POOL_SLAB {
  void *Handle;                                /* the owner's page list */
  unsigned long long Pa[ADMISSION_POOL_SLAB_SLOTS]; /* 64 KiB-aligned */
  unsigned int FreeMask;                       /* bit i: slot i free */
  unsigned int Live;                           /* slots handed out */
  struct _ADMISSION_POOL_SLAB *Next;
} ADMISSION_POOL_SLAB;

typedef struct _ADMISSION_POOL_SLAB_STATS {
  unsigned long long Slabs, PeakSlabs, NewSlabs, NewSlabFailures, FreedSlabs;
} ADMISSION_POOL_SLAB_STATS;

typedef struct _ADMISSION_POOL_SLABS {
  void *Context;
  /* A slab with Handle and every Pa filled (FreeMask/Live/Next are set
   * here), or 0 when Windows has no pages. */
  ADMISSION_POOL_SLAB *(*NewSlab)(void *Context);
  void (*FreeSlab)(void *Context, ADMISSION_POOL_SLAB *Slab);
  ADMISSION_POOL_SLAB *List;
  unsigned long long SpareLimit, Empty;
  ADMISSION_POOL_SLAB_STATS Stats;
} ADMISSION_POOL_SLABS;

/* Hands out Count slots (all or nothing): Slab[i]/Slot[i] for i < Count. */
int AdmissionPoolSlabTake(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count,
                          ADMISSION_POOL_SLAB **Slab, unsigned int *Slot);
void AdmissionPoolSlabGive(ADMISSION_POOL_SLABS *Slabs, ADMISSION_POOL_SLAB *Slab,
                           unsigned int Slot);
/* Frees every slab (stop); slots still handed out are the caller's leak. */
void AdmissionPoolSlabDrain(ADMISSION_POOL_SLABS *Slabs);

#endif
