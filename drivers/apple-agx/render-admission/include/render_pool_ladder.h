#ifndef APPLE_AGX_RENDER_POOL_LADDER_H
#define APPLE_AGX_RENDER_POOL_LADDER_H

/* POOL-LADDER (opt-in, REG_DWORD Wom1PoolLadder=1): the order of the
 * MmAllocatePagesForMdlEx attempts one 2 MiB slab makes
 * (AdmissionPoolAllocSlab, pool_windows.c), and the back-off of the optional
 * 256 KiB rung. EXP2333: the 64 KiB rung costs per chunk, so a 2 MiB slab
 * built from 8 x 256 KiB chunks may cost less than 32 x 64 KiB chunks.
 *
 * Rungs (attempt indices; AdmissionPoolSlabAttemptFlags keys on them):
 *   0  one 2 MiB-aligned 2 MiB chunk (SkipBytes 2 MiB, fast large pages);
 *   2  8 x 256 KiB chunks (SkipBytes 256 KiB), only with the switch on;
 *   1  32 x 64 KiB chunks (SkipBytes 64 KiB).
 * Switch off: exactly 0 then 1, as before POOL-LADDER.
 *
 * Back-off: a failed 256 KiB attempt (no MDL, short MDL or a failed
 * per-granule check) makes the next ADMISSION_POOL_LADDER_BACKOFF_SLABS slab
 * allocations go from rung 0 straight to rung 1. A successful 256 KiB
 * attempt, or an allocation that never reached the rung, leaves the back-off
 * unchanged. Begin and End run under the pool lock; the allocation between
 * them may run without it (the opt-in prefill).
 * Host-compiled by tests/test_apple_agx_pool_ladder.py: no kernel types. */

#define ADMISSION_POOL_RUNG_LARGE 0u
#define ADMISSION_POOL_RUNG_64K 1u
#define ADMISSION_POOL_RUNG_256K 2u
#define ADMISSION_POOL_RUNG_END 3u

#define ADMISSION_POOL_LADDER_BACKOFF_SLABS 16u

/* What one slab allocation may do with the 256 KiB rung. */
#define ADMISSION_POOL_LADDER_OFF 0u       /* switch off: rungs 0, 1 */
#define ADMISSION_POOL_LADDER_TRY 1u       /* rungs 0, 2, 1 */
#define ADMISSION_POOL_LADDER_BACKED_OFF 2u /* rungs 0, 1; counted as skipped
                                             * if rung 1 is reached */

typedef struct _ADMISSION_POOL_LADDER {
  unsigned int Backoff; /* slab allocations still to skip the 256 KiB rung */
} ADMISSION_POOL_LADDER;

/* Under the pool lock, once per slab allocation, before it. A back-off slot
 * is consumed by every allocation that starts while it is active. */
static __inline unsigned int AdmissionPoolLadderBegin(
    ADMISSION_POOL_LADDER *Ladder, int Enabled) {
  if (!Enabled) return ADMISSION_POOL_LADDER_OFF;
  if (Ladder->Backoff != 0u) {
    --Ladder->Backoff;
    return ADMISSION_POOL_LADDER_BACKED_OFF;
  }
  return ADMISSION_POOL_LADDER_TRY;
}

/* Under the pool lock, after the allocation: Tried = the 256 KiB attempt
 * ran, Ok = it produced the slab. */
static __inline void AdmissionPoolLadderEnd(ADMISSION_POOL_LADDER *Ladder,
                                            int Tried, int Ok) {
  if (Tried && !Ok) Ladder->Backoff = ADMISSION_POOL_LADDER_BACKOFF_SLABS;
}

/* The rung after a failed attempt at Rung; ADMISSION_POOL_RUNG_END ends the
 * slab allocation. */
static __inline unsigned int AdmissionPoolLadderNextRung(unsigned int Rung,
                                                         unsigned int Plan) {
  if (Rung == ADMISSION_POOL_RUNG_LARGE)
    return Plan == ADMISSION_POOL_LADDER_TRY ? ADMISSION_POOL_RUNG_256K
                                             : ADMISSION_POOL_RUNG_64K;
  if (Rung == ADMISSION_POOL_RUNG_256K) return ADMISSION_POOL_RUNG_64K;
  return ADMISSION_POOL_RUNG_END;
}

#endif
