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
 * - the context keeps at most AGX_WIN32_STREAM_SPARES BOs and
 *   AGX_WIN32_STREAM_SPARE_BYTES per kind; the rest are released as before.
 *   New BOs are sized by the request rounded up to 1 MiB, and the smallest
 *   retained BO that fits is chosen;
 * - the context releases what it retains when it is destroyed.
 */
#ifndef AGX_WIN32_STREAM_SCRATCH_H
#define AGX_WIN32_STREAM_SCRATCH_H

#define AGX_WIN32_STREAM_SPARES 16u
#define AGX_WIN32_STREAM_SPARE_BYTES (128ull << 20)
#define AGX_WIN32_STREAM_GRANULE (1u << 20)
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
   *out = bo;
   ptr.gpu = bo->va->addr;
   return ptr;
}

/* agx_batch_cleanup, in place of agx_pool_cleanup: the batch has retired. */
static inline void
agx_win32_stream_scratch_retire(struct agx_context *ctx, struct agx_pool *pool,
                                unsigned kind)
{
   struct agx_bo **spare = ctx->windows_stream_spare[kind & 1u];
   util_dynarray_foreach(&pool->bos, struct agx_bo *, entry) {
      struct agx_bo *bo = *entry;
      uint64_t kept = 0;
      unsigned slot = AGX_WIN32_STREAM_SPARES;
      for (unsigned i = 0; i < AGX_WIN32_STREAM_SPARES; ++i) {
         if (spare[i])
            kept += spare[i]->size;
         else if (slot == AGX_WIN32_STREAM_SPARES)
            slot = i;
      }
      if (bo && slot < AGX_WIN32_STREAM_SPARES &&
          bo->size <= AGX_WIN32_STREAM_SPARE_BYTES - kept)
         spare[slot] = bo;
      else
         agx_bo_unreference(pool->dev, bo);
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
}
#endif
