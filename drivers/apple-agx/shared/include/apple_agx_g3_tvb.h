#ifndef APPLE_AGX_G3_TVB_H
#define APPLE_AGX_G3_TVB_H
/* rc80: tiler heap (TVB) growth past the 32-block private-pool floor.
 *
 * Asahi drivers/gpu/drm/asahi/buffer.rs: the heap is a list of 128 KiB
 * blocks of four 32 KiB pages. When the firmware runs out of pages it stops
 * the vertex pass and performs a partial render; the kernel then grows the
 * heap from the firmware Stats (auto_grow: when 2 x max_pages exceeds the
 * backed pages, grow to 3 x max_pages) at the next submission, while the
 * buffer manager is idle (queue/render.rs: auto_grow, then
 * ensure_blocks(min_tvb_blocks)). Asahi never shrinks.
 *
 * Windows layout (KMD-owned, never named by the UMD):
 * - blocks [0, 32) are the top 4 MiB of the 32 MiB private window, backed
 *   from the 64 MiB private pool exactly as before (EXP1115);
 * - blocks [32, 2080) are a second per-process reservation of 256 MiB that
 *   starts where the private window ends, so the heap stays VA-contiguous.
 *   It has its own eight leaf tables below the private window's middle
 *   table and is backed on demand, one block at a time, by two 64 KiB slab
 *   leases of Windows memory (render_pool_slab.h private leases, gated by
 *   the LowMemory notification): unified memory, not a fixed pool.
 * Growth is fail-soft: a refused lease, table or broker grant keeps every
 * block mapped so far, and the render runs with partial renders as today.
 *
 * Plain C only: this header is part of the WDK kernel build. */

#define APPLE_AGX_G3_TVB_BLOCK_BYTES 0x20000u
#define APPLE_AGX_G3_TVB_PAGES_PER_BLOCK 4u
/* The private-pool floor (APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS). */
#define APPLE_AGX_G3_TVB_FLOOR_BLOCKS 32u
#define APPLE_AGX_G3_TVB_VA_BYTES 0x10000000ULL
#define APPLE_AGX_G3_TVB_LEAF_BYTES 0x02000000ULL
#define APPLE_AGX_G3_TVB_LEAVES 8u
#define APPLE_AGX_G3_TVB_BLOCKS_PER_LEAF 256u
#define APPLE_AGX_G3_TVB_WINDOW_BLOCKS 2048u
#define APPLE_AGX_G3_TVB_MAX_BLOCKS \
  (APPLE_AGX_G3_TVB_FLOOR_BLOCKS + APPLE_AGX_G3_TVB_WINDOW_BLOCKS)
/* A block is two 64 KiB slab granules; a granule is four 16 KiB GPU pages. */
#define APPLE_AGX_G3_TVB_GRANULE_BYTES 0x10000u
#define APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK 2u
#define APPLE_AGX_G3_TVB_GPU_PAGES_PER_BLOCK 8u
/* One growth step maps at most this many blocks (128 MiB, about 16k broker
 * calls) under the adapter lock; the next idle PREPARE continues. */
#define APPLE_AGX_G3_TVB_STEP_BLOCKS 1024u
/* A refused growth is not retried for this long unless the target rises. */
#define APPLE_AGX_G3_TVB_RETRY_MS 2000ULL
/* Global lease budget: RAM / 8, at most 1 GiB (Asahi max_size_nomemless is
 * 862 MB per heap; two large heaps fit). */
#define APPLE_AGX_G3_TVB_GLOBAL_MAX_BYTES 0x40000000ULL

#if APPLE_AGX_G3_TVB_WINDOW_BLOCKS * APPLE_AGX_G3_TVB_BLOCK_BYTES != 0x10000000 || \
    APPLE_AGX_G3_TVB_BLOCKS_PER_LEAF * APPLE_AGX_G3_TVB_LEAVES != \
        APPLE_AGX_G3_TVB_WINDOW_BLOCKS
#error TVB window geometry
#endif
#if defined(APPLE_AGX_G4_TVB_HEAP_BLOCKS) && \
    APPLE_AGX_G4_TVB_HEAP_BLOCKS != APPLE_AGX_G3_TVB_FLOOR_BLOCKS + APPLE_AGX_G3_TVB_WINDOW_BLOCKS
#error TVB heap range and window disagree
#endif

/* Asahi buffer.rs auto_grow plus queue/render.rs ensure_blocks: the backed
 * block count to grow to, or `current` when nothing must grow. `used_pages`
 * is the firmware Stats max_pages (0 when no valid Stats), `size_blocks` the
 * render's uncapped min_tvb_blocks, `max` the largest count this process can
 * back (the floor when it has no TVB window). Never below `current`. */
static inline unsigned AppleAgxG3TvbTarget(unsigned used_pages,
    unsigned size_blocks, unsigned current, unsigned max) {
  unsigned long long need, want;
  unsigned target = current;
  if (max > APPLE_AGX_G3_TVB_MAX_BLOCKS) max = APPLE_AGX_G3_TVB_MAX_BLOCKS;
  need = ((unsigned long long)used_pages * 2ULL +
          APPLE_AGX_G3_TVB_PAGES_PER_BLOCK - 1ULL) /
         APPLE_AGX_G3_TVB_PAGES_PER_BLOCK;
  want = ((unsigned long long)used_pages * 3ULL +
          APPLE_AGX_G3_TVB_PAGES_PER_BLOCK - 1ULL) /
         APPLE_AGX_G3_TVB_PAGES_PER_BLOCK;
  if (need > max) need = max;
  if (want > max) want = max;
  if (need > current) target = (unsigned)want;
  if (size_blocks > max) size_blocks = max;
  if (size_blocks > target) target = size_blocks;
  return target;
}

/* The next step toward `target`: the floor first (private pool), then at
 * most APPLE_AGX_G3_TVB_STEP_BLOCKS window blocks. */
static inline unsigned AppleAgxG3TvbStep(unsigned current, unsigned target) {
  if (target <= current) return current;
  if (current < APPLE_AGX_G3_TVB_FLOOR_BLOCKS)
    return target < APPLE_AGX_G3_TVB_FLOOR_BLOCKS ?
        target : APPLE_AGX_G3_TVB_FLOOR_BLOCKS;
  return target - current > APPLE_AGX_G3_TVB_STEP_BLOCKS ?
      current + APPLE_AGX_G3_TVB_STEP_BLOCKS : target;
}

/* Where window block `block` (>= the floor) lives: leaf table and the first
 * of its eight consecutive 16 KiB PTEs. */
static inline int AppleAgxG3TvbBlockSlot(unsigned block, unsigned *leaf,
    unsigned *pte) {
  unsigned index;
  if (!leaf || !pte || block < APPLE_AGX_G3_TVB_FLOOR_BLOCKS ||
      block >= APPLE_AGX_G3_TVB_MAX_BLOCKS) return 0;
  index = block - APPLE_AGX_G3_TVB_FLOOR_BLOCKS;
  *leaf = index / APPLE_AGX_G3_TVB_BLOCKS_PER_LEAF;
  *pte = (index % APPLE_AGX_G3_TVB_BLOCKS_PER_LEAF) *
         APPLE_AGX_G3_TVB_GPU_PAGES_PER_BLOCK;
  return 1;
}

/* Lease budget in 64 KiB granules for a machine with `ram_bytes` of RAM. */
static inline unsigned long long AppleAgxG3TvbGlobalGranules(
    unsigned long long ram_bytes) {
  unsigned long long bytes = ram_bytes / 8ULL;
  if (bytes > APPLE_AGX_G3_TVB_GLOBAL_MAX_BYTES)
    bytes = APPLE_AGX_G3_TVB_GLOBAL_MAX_BYTES;
  return bytes / APPLE_AGX_G3_TVB_GRANULE_BYTES;
}

/* Per-process growth bookkeeping (KMD, under the adapter lock). */
enum {
  APPLE_AGX_G3_TVB_FAIL_NONE = 0,
  APPLE_AGX_G3_TVB_FAIL_FLOOR = 1,   /* private-pool floor refused */
  APPLE_AGX_G3_TVB_FAIL_TABLE = 2,   /* leaf table allocation/registration */
  APPLE_AGX_G3_TVB_FAIL_ATTACH = 3,  /* leaf link below the middle table */
  APPLE_AGX_G3_TVB_FAIL_LEASE = 4,   /* slab lease: budget or LowMemory */
  APPLE_AGX_G3_TVB_FAIL_MAP = 5,     /* broker grant/leaf refused */
  APPLE_AGX_G3_TVB_FAIL_NO_WINDOW = 6, /* no reservation at CreateProcess */
  APPLE_AGX_G3_TVB_FAIL_LIST = 7,    /* lease bookkeeping allocation */
  APPLE_AGX_G3_TVB_FAIL_BUSY = 8     /* no idle point within the wait */
};
typedef struct {
  unsigned LeaseBlocks;      /* window blocks backed (blocks >= floor) */
  unsigned Leaves;           /* bitmask of registered window leaf tables */
  unsigned Grows, Failures;  /* successful steps, refused steps */
  unsigned LastTarget, LastReason, LastUsedPages;
  unsigned FailedTarget;
  unsigned long long FailedAtMs;
} APPLE_AGX_G3_TVB_STATE;

/* A refused target is retried only when it rises or after the hold-off. */
static inline int AppleAgxG3TvbShouldTry(const APPLE_AGX_G3_TVB_STATE *s,
    unsigned target, unsigned current, unsigned long long now_ms) {
  if (!s || target <= current) return 0;
  if (!s->FailedTarget || target > s->FailedTarget) return 1;
  return now_ms - s->FailedAtMs >= APPLE_AGX_G3_TVB_RETRY_MS;
}

/* One growth step from `before` to `after` blocks toward `target`. A
 * partial step keeps what it mapped and holds the target off. */
static inline void AppleAgxG3TvbNoteResult(APPLE_AGX_G3_TVB_STATE *s,
    unsigned target, unsigned before, unsigned after, unsigned reason,
    unsigned long long now_ms) {
  if (!s) return;
  s->LastTarget = target;
  s->LastReason = reason;
  if (after > before) ++s->Grows;
  if (reason == APPLE_AGX_G3_TVB_FAIL_NONE) {
    s->FailedTarget = 0u;
    s->FailedAtMs = 0ULL;
  } else {
    ++s->Failures;
    s->FailedTarget = target;
    s->FailedAtMs = now_ms;
  }
}
#endif
