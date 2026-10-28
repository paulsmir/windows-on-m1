/* SPDX-License-Identifier: MIT
 * RC71: GPU indirect stream outputs come from storage the context retains.
 * Included at the end of agx_state.h (native-asahi-indirect-stream.py).
 *
 * Each stream draw writes MaxCount VDM records and MaxCount USC words
 * (GravityMark: MaxCount 60000, about 3 MiB of VDM and 7 MiB of USC per
 * call, about 7 calls per Execute). Through the batch pools every call made
 * two dedicated BOs (each request is larger than the 256 KiB pool slab) and
 * batch cleanup released them into the 64 MiB winsys BO cache, which then
 * evicted everything else: EXP1990 arm b had 3125 cache evictions and 3585
 * misses in 78 s (arm a, stream off: 0 and 541), 533 BO creations of at least
 * 10 ms, residency trims from Execute 260 on, and the released VAs filled the
 * winsys PendingVa list (fixed separately in agx_win32_asahi_bo.c).
 *
 * Invariants:
 * - a stream output BO serves one draw of one batch; the batch pool holds
 *   the reference that agx_batch_cleanup gives back;
 * - agx_batch_cleanup runs only after the batch's submission retired (or it
 *   never submitted), so a BO given back to the context is idle and is
 *   handed out again without any further wait;
 * - the context keeps at most AGX_WIN32_STREAM_SPARES BOs per kind and,
 *   per kind, the larger of AGX_WIN32_STREAM_SPARE_BYTES and the peak bytes
 *   unretired batches held at once less what the other unretired batches
 *   still hold (rc88), so retained plus in-flight outputs never exceed that
 *   peak; the rest are released as before. New BOs are sized by the request
 *   rounded up to 1 MiB, and the smallest retained BO that fits is chosen;
 * - the context releases what it retains when it is destroyed.
 *
 * rc89 (rc88 budget: MaxCount 240000 at FHD 200k, about 11 MiB of VDM and
 * 30-44 MiB of USC per draw, 7 draws per frame, about 300 MiB per frame in
 * flight; rc67 EXP1989: about 150 executed records per call): outputs are
 * sized by a capacity, not by MaxCount. Invariants:
 * - the kernel writes at most `capacity` records and the terminator
 *   (agx_win32_indirect_stream.cl), so outputs of capacity records are
 *   never overrun, whatever count the GPU produced;
 * - the capacity is MaxCount until a retired batch proves that a dispatch
 *   reported to the context's feedback words, and whenever there are no
 *   feedback words (APPLE_AGX_D3D12_STREAM_CAPACITY=0, or no BO);
 * - afterwards it is min(MaxCount, the power of two at least FLOOR and
 *   MARGIN x the largest GPU count any dispatch of the context reported);
 *   it never shrinks;
 * - when it changes, retained outputs far outside the sizes requested since
 *   are given back (they would otherwise stay retained, unused or oversized),
 *   and a retiring output that finds no room replaces a smaller spare;
 * - a dispatch whose GPU count exceeds its capacity draws only `capacity`
 *   records and counts an overflow, and its count then raises the capacity
 *   of every draw recorded after the batch retires. That one execution is
 *   incomplete: the price of not reserving MaxCount for every draw.
 */
#ifndef AGX_WIN32_STREAM_SCRATCH_H
#define AGX_WIN32_STREAM_SCRATCH_H

#define AGX_WIN32_STREAM_SPARES 16u
#define AGX_WIN32_STREAM_SPARE_BYTES (128ull << 20)
#define AGX_WIN32_STREAM_GRANULE (1u << 20)
/* rc89: a spare larger than this many times the largest request since the
 * last capacity change plus one granule, or too small for the smallest one,
 * is released. Outside a change nothing is: every BO made since then lies
 * inside that range. */
#define AGX_WIN32_STREAM_OVERSIZE 2u
typedef char agx_win32_stream_spares_match[
   sizeof(((struct agx_context *)0)->windows_stream_spare[0]) ==
      AGX_WIN32_STREAM_SPARES * sizeof(struct agx_bo *) ? 1 : -1];

/* Kind 0: VDM records (batch->windows_stream_pool); kind 1: USC words
 * (batch->windows_stream_usc_pool, low VA). GPU-only: cpu is always NULL. */
static inline struct agx_ptr
agx_win32_stream_scratch(struct agx_batch *batch, unsigned kind, size_t bytes,
                         struct agx_bo **out)
{
   struct agx_context *ctx = batch->ctx;
   struct agx_pool *pool =
      kind ? &batch->windows_stream_usc_pool : &batch->windows_stream_pool;
   struct agx_bo **spare = ctx->windows_stream_spare[kind];
   unsigned best = AGX_WIN32_STREAM_SPARES;
   struct agx_bo *bo = NULL;
   struct agx_ptr ptr;

   memset(&ptr, 0, sizeof(ptr));
   *out = NULL;
   if (kind > 1 || !bytes || bytes > SIZE_MAX - AGX_WIN32_STREAM_GRANULE)
      return ptr;
   /* rc89: when the capacity policy changes (feedback_retire), outputs of
    * the old size would stay retained forever: the MaxCount-sized warm-up
    * outputs then serve the small draws, or small outputs fill every slot
    * after a growth. Spares are idle; give back those far outside the
    * requests seen since the change. A BO made for any of those requests
    * is inside the range, so a steady workload never releases one. */
   {
      unsigned long long *lo = &ctx->windows_stream_request_min[kind];
      unsigned long long *hi = &ctx->windows_stream_request_max[kind];
      if (!*lo || bytes < *lo)
         *lo = bytes;
      if (bytes > *hi)
         *hi = bytes;
      for (unsigned i = 0; i < AGX_WIN32_STREAM_SPARES; ++i) {
         uint64_t size = spare[i] ? spare[i]->size : 0;
         if (spare[i] &&
             (size > AGX_WIN32_STREAM_OVERSIZE * *hi + AGX_WIN32_STREAM_GRANULE ||
              size < *lo)) {
            agx_bo_unreference(pool->dev, spare[i]);
            spare[i] = NULL;
            AgxWin32PerfNote("GPU stream scratch stale release");
         }
      }
   }
   for (unsigned i = 0; i < AGX_WIN32_STREAM_SPARES; ++i) {
      if (spare[i] && spare[i]->size >= bytes &&
          (best == AGX_WIN32_STREAM_SPARES || spare[i]->size < spare[best]->size))
         best = i;
   }
   if (best < AGX_WIN32_STREAM_SPARES) {
      bo = spare[best];
      spare[best] = NULL;
      AgxWin32PerfNote("GPU stream scratch reuse");
   } else {
      size_t size = (bytes + AGX_WIN32_STREAM_GRANULE - 1) &
                    ~(size_t)(AGX_WIN32_STREAM_GRANULE - 1);
      bo = agx_bo_create(pool->dev, size, 0, pool->create_flags, pool->label);
      if (!bo)
         return ptr;
      AgxWin32PerfNote("GPU stream scratch create");
   }
   util_dynarray_append_typed(&pool->bos, struct agx_bo *, bo);
   ctx->windows_stream_live[kind] += bo->size;
   if (ctx->windows_stream_live[kind] > ctx->windows_stream_peak[kind])
      ctx->windows_stream_peak[kind] = ctx->windows_stream_live[kind];
   *out = bo;
   ptr.gpu = bo->va->addr;
   return ptr;
}

/* rc89: records in each output when no larger GPU count was reported,
 * and the factor over the largest reported count. */
#define AGX_WIN32_STREAM_CAPACITY_FLOOR 16384u
#define AGX_WIN32_STREAM_CAPACITY_MARGIN 4u
#define AGX_WIN32_STREAM_FEEDBACK_BYTES 64u
/* The kernel's AGX_WIN32_STREAM_FEEDBACK_* word indices (the kernel header
 * is not visible in every file that includes agx_state.h; the emitter
 * checks that both agree). */
#define AGX_WIN32_STREAM_FB_MAX_COUNT 0u
#define AGX_WIN32_STREAM_FB_OVERFLOWS 1u
#define AGX_WIN32_STREAM_FB_DISPATCHES 2u
#define AGX_WIN32_STREAM_FB_WORDS 4u
typedef char agx_win32_stream_feedback_fits[
   AGX_WIN32_STREAM_FB_WORDS * 4u <= AGX_WIN32_STREAM_FEEDBACK_BYTES ? 1 : -1];

/* rc89: creates and zeroes the context's feedback words, outside graphics
 * batch entry (agx_win32_stream_flush_inputs). Like the D3D12 query groups,
 * one CPU-mapped writeback BO the GPU updates with atomics. A failure only
 * leaves the context without feedback: every capacity stays MaxCount. */
static inline void
agx_win32_stream_feedback_ensure(struct agx_context *ctx)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   struct agx_bo *bo;
   uint32_t *cpu;
   if (ctx->windows_stream_feedback)
      return;
   bo = agx_bo_create(dev, AGX_WIN32_STREAM_FEEDBACK_BYTES, 0, AGX_BO_WRITEBACK,
                      "GPU indirect feedback");
   cpu = bo ? (uint32_t *)agx_bo_map(bo) : NULL;
   if (!bo || !cpu || !bo->va || !bo->va->addr ||
       bo->size < AGX_WIN32_STREAM_FEEDBACK_BYTES) {
      if (bo)
         agx_bo_unreference(dev, bo);
      return;
   }
   memset(cpu, 0, AGX_WIN32_STREAM_FEEDBACK_BYTES);
   ctx->windows_stream_feedback = bo;
}

/* rc89: the feedback words' GPU address for a stream draw of this batch,
 * which then retains them; 0 when the context has none. */
static inline uint64_t
agx_win32_stream_feedback(struct agx_batch *batch)
{
   struct agx_bo *bo = batch->ctx->windows_stream_feedback;
   if (!bo)
      return 0;
   agx_batch_add_bo(batch, bo);
   return bo->va->addr;
}

/* rc89: the records a stream draw's outputs hold (1..max_count). */
static inline unsigned
agx_win32_stream_capacity(const struct agx_context *ctx, unsigned max_count,
                          uint64_t feedback)
{
   uint64_t want = ctx->windows_stream_want;
   if (!feedback || !ctx->windows_stream_reports || !want)
      return max_count;
   return want < max_count ? (unsigned)want : max_count;
}

/* rc89: the power of two at least FLOOR and MARGIN x the largest reported
 * count; a power of two so that the policy changes only a few times. */
static inline unsigned long long
agx_win32_stream_want(unsigned long long observed)
{
   unsigned long long want = AGX_WIN32_STREAM_CAPACITY_FLOOR;
   while (want < observed * AGX_WIN32_STREAM_CAPACITY_MARGIN)
      want <<= 1;
   return want;
}

/* rc89: agx_batch_cleanup of a batch that ran stream draws: their
 * dispatches have completed, so the words include their reports. */
static inline void
agx_win32_stream_feedback_retire(struct agx_context *ctx)
{
   struct agx_bo *bo = ctx->windows_stream_feedback;
   const volatile uint32_t *words =
      bo ? (const volatile uint32_t *)agx_bo_map(bo) : NULL;
   uint32_t dispatches, observed, overflows;
   if (!words)
      return;
   dispatches = words[AGX_WIN32_STREAM_FB_DISPATCHES];
   observed = words[AGX_WIN32_STREAM_FB_MAX_COUNT];
   overflows = words[AGX_WIN32_STREAM_FB_OVERFLOWS];
   if (!dispatches)
      return;
   ctx->windows_stream_reports = dispatches;
   if (observed > ctx->windows_stream_observed)
      ctx->windows_stream_observed = observed;
   if (agx_win32_stream_want(ctx->windows_stream_observed) != ctx->windows_stream_want) {
      ctx->windows_stream_want = agx_win32_stream_want(ctx->windows_stream_observed);
      for (unsigned kind = 0; kind < 2; ++kind)
         ctx->windows_stream_request_min[kind] = ctx->windows_stream_request_max[kind] = 0;
      AgxWin32PerfNote("GPU stream capacity change");
   }
   if (overflows > ctx->windows_stream_overflows) {
      ctx->windows_stream_overflows = overflows;
      AgxWin32PerfNote("GPU stream capacity overflow");
   }
}

/* agx_batch_cleanup, in place of agx_pool_cleanup: the batch has retired.
 * rc87 (EXP2017 GravityMark FHD 200k: stream outputs of about 30 MiB, more
 * than 128 MiB per batch): the fixed 128 MiB cap released part of every
 * retired batch's outputs, so each later batch created them again -- 1005
 * "GPU stream scratch create" (14 at 100k), 396 creations of 30 MiB taking
 * 30 s, 848 steady-state residency trims of 30 MiB, and the released
 * outputs flooded the 64 MiB winsys BO cache (7722 evictions; 1617 small
 * creations of at least 10 ms, 31 s). The next batch needs what this one
 * used, so the cap is at least this batch's own outputs.
 * rc88 (EXP2019b, MaxCount 240000: 1595 creates in 1602 presents, 467
 * creations of 30 MiB, 11118 BO cache evictions, 1884 steady trims): a frame
 * spreads its stream draws over several batches that retire back to back,
 * so spares kept from the first retire left no room under the second
 * batch's own (smaller) cap and one output per frame was released and
 * created again. The cap is now the in-flight peak less the outputs other
 * unretired batches still hold: retained plus in-flight bytes stay within a
 * peak the window already reached, and the next window finds them. */
static inline void
agx_win32_stream_scratch_retire(struct agx_context *ctx, struct agx_pool *pool,
                                unsigned kind)
{
   struct agx_bo **spare = ctx->windows_stream_spare[kind & 1u];
   unsigned long long *live = &ctx->windows_stream_live[kind & 1u];
   uint64_t cap = AGX_WIN32_STREAM_SPARE_BYTES, own = 0;
   util_dynarray_foreach(&pool->bos, struct agx_bo *, entry) {
      if (*entry)
         own += (*entry)->size;
   }
   if (own && !(kind & 1u))
      agx_win32_stream_feedback_retire(ctx);
   *live = *live > own ? *live - own : 0;
   if (own > cap)
      cap = own;
   if (ctx->windows_stream_peak[kind & 1u] > *live &&
       ctx->windows_stream_peak[kind & 1u] - *live > cap)
      cap = ctx->windows_stream_peak[kind & 1u] - *live;
   util_dynarray_foreach(&pool->bos, struct agx_bo *, entry) {
      struct agx_bo *bo = *entry;
      while (bo) {
         uint64_t kept = 0;
         unsigned slot = AGX_WIN32_STREAM_SPARES, least = AGX_WIN32_STREAM_SPARES;
         for (unsigned i = 0; i < AGX_WIN32_STREAM_SPARES; ++i) {
            if (spare[i]) {
               kept += spare[i]->size;
               if (least == AGX_WIN32_STREAM_SPARES ||
                   spare[i]->size < spare[least]->size)
                  least = i;
            } else if (slot == AGX_WIN32_STREAM_SPARES) {
               slot = i;
            }
         }
         if (slot < AGX_WIN32_STREAM_SPARES && kept <= cap &&
             bo->size <= cap - kept) {
            spare[slot] = bo;
            bo = NULL;
         } else if (least < AGX_WIN32_STREAM_SPARES &&
                    spare[least]->size < bo->size) {
            /* rc89: no room. Keep the larger output, which costs more to
             * create again: after a capacity growth the slots and the cap
             * are otherwise held by small outputs of the old capacity. In
             * a steady window retained plus in-flight outputs fit the peak,
             * so this never runs there. */
            agx_bo_unreference(pool->dev, spare[least]);
            spare[least] = NULL;
         } else {
            agx_bo_unreference(pool->dev, bo);
            bo = NULL;
         }
      }
   }
   util_dynarray_fini(&pool->bos);
   pool->transient_bo = NULL;
   pool->transient_offset = 0;
}

/* agx_destroy_context, after every batch retired. */
static inline void
agx_win32_stream_scratch_fini(struct agx_context *ctx)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   for (unsigned kind = 0; kind < 2; ++kind) {
      for (unsigned i = 0; i < AGX_WIN32_STREAM_SPARES; ++i) {
         if (ctx->windows_stream_spare[kind][i])
            agx_bo_unreference(dev, ctx->windows_stream_spare[kind][i]);
         ctx->windows_stream_spare[kind][i] = NULL;
      }
   }
   if (ctx->windows_stream_feedback)
      agx_bo_unreference(dev, ctx->windows_stream_feedback);
   ctx->windows_stream_feedback = NULL;
}
#endif
