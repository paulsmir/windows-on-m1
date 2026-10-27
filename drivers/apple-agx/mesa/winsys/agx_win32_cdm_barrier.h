/* SPDX-License-Identifier: MIT
 * RC73: D3D12 compute dispatches keep the CDM barrier only where the
 * application's ordering requires one.
 * Included in agx_state.c (with AGX_WIN32_CDM_BARRIER_ENTRIES, which also
 * defines the C entries the D3D12 owner calls) and in agx_pipe.c (settle at
 * flush only); GPUVA only, projected by native-asahi-cdm-barrier.py.
 *
 * Native Asahi encodes a CDM barrier with every cache flush/invalidate bit
 * after every launch (agx_launch_internal, libagx_dgc.h agx_cdm_barrier: "set
 * these after every launch to be safe"). EXP1991 GravityMark: 174 dispatches
 * per Execute, ~32 us fixed cost per dispatch + barrier.
 *
 * D3D12 orders two Dispatch calls only through an app ResourceBarrier
 * (UAV, transition or aliasing) or an ExecuteCommandLists boundary; UAV
 * accesses without a barrier are unordered. The owner therefore marks only
 * its own grid launches as relaxed (ctx->windows_cdm_relaxed, set around
 * pipe->launch_grid by AgxD3d12OwnerDispatch), and:
 *
 * - a relaxed launch defers its trailing barrier (batch->windows_cdm_owed);
 * - the deferred barrier is emitted, exactly as native encodes it, before
 *   any non-relaxed launch (libagx precomp kernels: statistics, query copies,
 *   blits, IA batches), at every app barrier and command-list boundary
 *   (AgxWin32AsahiCdmBarrier), and before the stream terminates
 *   (agx_flush_compute: CPU maps, other batches and the next Execute see
 *   flushed memory, as natively);
 * - so every non-relaxed launch is still preceded and followed by a native
 *   barrier; only the barrier between two relaxed launches with no app
 *   barrier between them is skipped;
 * - space: a deferred barrier takes the 4 bytes native wrote right after the
 *   launch. The native overflow bound of agx_launch_internal (52 bytes)
 *   counts both the indirect (8) and the global-size (12) words although a
 *   launch encodes one of them, so after any launch that does not flush
 *   there is room for an owed barrier, the largest launch (40 bytes with the
 *   G14X word), its barrier and the stream terminate; the bound is unchanged.
 */
#ifndef AGX_WIN32_CDM_BARRIER_H
#define AGX_WIN32_CDM_BARRIER_H

enum agx_win32_cdm_count {
   AGX_WIN32_CDM_RELAXED_LAUNCHES = 0, /* relaxed launches encoded */
   AGX_WIN32_CDM_NATIVE_LAUNCHES,      /* launches with a native trailing barrier */
   AGX_WIN32_CDM_SETTLED_APP,          /* deferred barriers emitted at app barriers */
   AGX_WIN32_CDM_SETTLED_INTERNAL,     /* ... before a non-relaxed launch */
   AGX_WIN32_CDM_SETTLED_FLUSH,        /* ... before the stream terminates */
   AGX_WIN32_CDM_APP_BARRIERS,         /* AgxWin32AsahiCdmBarrier calls */
   AGX_WIN32_CDM_COUNTS
};
static_assert(sizeof(((struct agx_context *)0)->windows_cdm_counts) ==
                 AGX_WIN32_CDM_COUNTS * sizeof(unsigned),
              "one context counter per agx_win32_cdm_count");

/* Emits the barrier the last relaxed launch of batch deferred, at out. */
static inline uint32_t *
agx_win32_cdm_settle_at(struct agx_batch *batch, uint32_t *out,
                        enum agx_win32_cdm_count reason)
{
   if (!batch->windows_cdm_owed)
      return out;
   struct agx_device *dev = agx_device(batch->ctx->base.screen);
   batch->windows_cdm_owed = false;
   ++batch->ctx->windows_cdm_counts[reason];
   return agx_cdm_barrier(out, dev->chip);
}

static inline void
agx_win32_cdm_settle(struct agx_batch *batch, enum agx_win32_cdm_count reason)
{
   if (!batch->windows_cdm_owed || !batch->cdm.current) {
      batch->windows_cdm_owed = false;
      return;
   }
   batch->cdm.current =
      (uint8_t *)agx_win32_cdm_settle_at(batch, (uint32_t *)batch->cdm.current, reason);
}

/* Replaces native "launch; barrier" in agx_launch_internal. Consumes the
 * one-shot user-launch mark set by agx_launch_grid. */
static inline uint32_t *
agx_win32_cdm_launch(struct agx_batch *batch, uint32_t *out,
                     struct agx_grid grid, struct agx_workgroup wg,
                     struct agx_cdm_launch_word_0_packed launch, uint32_t usc)
{
   struct agx_context *ctx = batch->ctx;
   struct agx_device *dev = agx_device(ctx->base.screen);
   const bool relaxed = ctx->windows_cdm_user_launch && agx_batch_is_compute(batch);
   ctx->windows_cdm_user_launch = false;
   if (!relaxed)
      out = agx_win32_cdm_settle_at(batch, out, AGX_WIN32_CDM_SETTLED_INTERNAL);
   out = agx_cdm_launch(out, dev->chip, grid, wg, launch, usc);
   if (relaxed) {
      batch->windows_cdm_owed = true;
      ++ctx->windows_cdm_counts[AGX_WIN32_CDM_RELAXED_LAUNCHES];
   } else {
      out = agx_cdm_barrier(out, dev->chip);
      ++ctx->windows_cdm_counts[AGX_WIN32_CDM_NATIVE_LAUNCHES];
   }
   return out;
}

#ifdef AGX_WIN32_CDM_BARRIER_ENTRIES
/* Declared for C++ callers in agx_win32_asahi_scene.h. */
void AgxWin32AsahiCdmRelaxed(struct pipe_context *, int);
void AgxWin32AsahiCdmBarrier(struct pipe_context *);
void AgxWin32AsahiCdmReceipt(struct pipe_context *, unsigned *, unsigned);

/* Nonzero: the next owner grid launches may run without a barrier between
 * them. The owner sets it around its own launch_grid calls only. */
void
AgxWin32AsahiCdmRelaxed(struct pipe_context *pctx, int relaxed)
{
   struct agx_context *ctx = agx_context(pctx);
   if (!ctx)
      return;
   ctx->windows_cdm_relaxed = relaxed != 0;
   ctx->windows_cdm_user_launch = false;
}

/* An app ResourceBarrier or command-list boundary: every launch encoded so
 * far completes, with native cache flush/invalidate, before any later one. */
void
AgxWin32AsahiCdmBarrier(struct pipe_context *pctx)
{
   struct agx_context *ctx = agx_context(pctx);
   if (!ctx)
      return;
   ++ctx->windows_cdm_counts[AGX_WIN32_CDM_APP_BARRIERS];
   for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i) {
      if (BITSET_TEST(ctx->batches.active, i))
         agx_win32_cdm_settle(&ctx->batches.slots[i], AGX_WIN32_CDM_SETTLED_APP);
   }
}

/* Copies up to count counters (enum agx_win32_cdm_count) and resets them. */
void
AgxWin32AsahiCdmReceipt(struct pipe_context *pctx, unsigned *out, unsigned count)
{
   struct agx_context *ctx = agx_context(pctx);
   for (unsigned i = 0; i < count; ++i)
      out[i] = ctx && i < AGX_WIN32_CDM_COUNTS ? ctx->windows_cdm_counts[i] : 0u;
   if (ctx)
      memset(ctx->windows_cdm_counts, 0, sizeof(ctx->windows_cdm_counts));
}
#endif

#endif
