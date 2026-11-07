/* SPDX-License-Identifier: MIT
 * IE1a (APPLE_AGX_D3D12_EXPAND_AHEAD=1; default off, read once): the GPU
 * indirect stream expansion of a counted ExecuteIndirect runs in its own
 * compute batch E ahead of its render batch R, instead of R's own CDM (the
 * v5 RENDER_PRELUDE, agx_win32_indirect_stream_emit.h). Included at the end
 * of agx_state.h after agx_win32_stream_scratch.h
 * (native-asahi-indirect-stream.py).
 *
 * Batch model (Asahi agx_batch.c, as projected):
 * - E is an ordinary compute batch (key width AGX_COMPUTE_BATCH_WIDTH) whose
 *   key height AGX_WIN32_EXPAND_KEY_HEIGHT + R's slot never equals the
 *   context's compute key (height 0), so agx_get_compute_batch never returns
 *   it, and no framebuffer is 0xFFFF wide. E is never ctx->batch: the
 *   dispatch names its batch explicitly (agx_launch_precomp).
 * - E is opened by agx_win32_stream_flush_inputs, before R's draw is entered:
 *   opening a batch may retire, evict or flush other batches. Inside the
 *   entered draw nothing is created or flushed; without a usable E the draw
 *   keeps the prelude in R's CDM.
 * - All expansions of R go to the same E while its CDM has room for two
 *   launches (the prelude's own bound, checked before entry); otherwise E is
 *   flushed (still ahead of R) and a new one is opened.
 * - agx_flush_batch(R) first flushes its E (rc105 block,
 *   native-asahi-submit-order.py), so E is submitted strictly before R. If E
 *   fails or is aborted the context is faulted and R is aborted, never
 *   submitted. An E without dispatches is reset, never submitted.
 * - R keeps the stream outputs (its windows_stream pools, RC71 retire) and
 *   reads them; E only lists them for residency (agx_batch_add_bo, never
 *   agx_batch_writes), with the feedback words, the argument/count BOs and
 *   its own pool (parameters and a copy of the USC template: R's pipeline
 *   pool is a CPU shadow published only when R submits).
 * - R reset with a linked E aborts E (agx_win32_expand_cleanup).
 * KMD: E is a plain compute envelope (AgxWin32AsahiBatchFinishCompute), R a
 * plain v3 render; the render waits on the preceding compute job (barrier
 * TA_ON_COMPUTE), exactly the job pair of an application compute pass.
 */
#ifndef AGX_WIN32_EXPAND_AHEAD_H
#define AGX_WIN32_EXPAND_AHEAD_H

#include <stdbool.h>
#include <stdlib.h>

/* agx_win32_asahi_batch.h, which not every agx_state.h user includes */
void AgxWin32AsahiMarkContextFault(struct agx_context *, unsigned);

#define AGX_WIN32_EXPAND_KEY_HEIGHT 0xE000u
typedef char agx_win32_expand_key_fits[
   AGX_WIN32_EXPAND_KEY_HEIGHT + AGX_MAX_BATCHES <= 0xFFFFu ? 1 : -1];

static inline bool
agx_win32_expand_ahead_parse(const char *value)
{
   return value && value[0] == '1' && !value[1];
}

/* A benign race: every thread computes the same value. */
static inline bool
agx_win32_expand_ahead_enabled(void)
{
   static int mode = -1;
   if (mode < 0)
      mode = agx_win32_expand_ahead_parse(getenv("APPLE_AGX_D3D12_EXPAND_AHEAD")) ? 1 : 0;
   return mode == 1;
}

/* The prelude stays whenever the expansion writes query results: the
 * statistics addresses register the query writer on the dispatching batch
 * (agx_win32_stream_query_address). Predication never reaches the stream
 * (agx_win32_stream_fallback reason 3). */
static inline bool
agx_win32_expand_ahead_allowed(const struct agx_context *ctx)
{
   if (!agx_win32_expand_ahead_enabled() || ctx->cond_query)
      return false;
   if (ctx->active_queries) {
      for (unsigned i = 0; i < ARRAY_SIZE(ctx->pipeline_statistics); ++i)
         if (ctx->pipeline_statistics[i])
            return false;
   }
   return true;
}

/* R's open expansion batch, or NULL. A link is valid only while E is active
 * and names R back; E's slot may have been reused since. */
static inline struct agx_batch *
agx_win32_expand_linked(struct agx_batch *render)
{
   struct agx_batch *expand = render->windows_expand;
   if (!expand)
      return NULL;
   if (expand->windows_expand_owner != render || !agx_batch_is_active(expand) ||
       agx_batch_is_submitted(expand)) {
      render->windows_expand = NULL;
      return NULL;
   }
   return expand;
}

/* Top of agx_flush_batch. Returns true when `batch` was an empty expansion
 * batch and has been reset instead of submitted. */
static inline bool
agx_win32_expand_flush_first(struct agx_context *ctx, struct agx_batch *batch)
{
   if (batch->windows_expand_owner && !batch->windows_expand_dispatches) {
      if (batch->windows_expand_owner->windows_expand == batch)
         batch->windows_expand_owner->windows_expand = NULL;
      batch->windows_expand_owner = NULL;
      agx_batch_reset(ctx, batch);
      return true;
   }
   struct agx_batch *expand = agx_win32_expand_linked(batch);
   if (!expand)
      return false;
   batch->windows_expand = NULL;
   AgxWin32PerfNote("Expand ahead: expansion submitted first");
   agx_flush_batch(ctx, expand);
   /* Every failed or refused E submission faults the context, which aborts
    * R in agx_flush_batch; an E left open would follow R. */
   if (!ctx->any_faults && agx_batch_is_active(expand))
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
   return false;
}

/* agx_batch_cleanup: unlink E from R; a render cleaned up with an open E
 * (reset, never submitted) aborts it, so E never writes outputs R gave back. */
static inline void
agx_win32_expand_cleanup(struct agx_context *ctx, struct agx_batch *batch)
{
   struct agx_batch *owner = batch->windows_expand_owner;
   if (owner && owner->windows_expand == batch)
      owner->windows_expand = NULL;
   batch->windows_expand_owner = NULL;
   struct agx_batch *expand = agx_win32_expand_linked(batch);
   batch->windows_expand = NULL;
   if (expand) {
      expand->windows_expand_owner = NULL;
      AgxWin32PerfNote("Expand ahead: expansion aborted with its render");
      agx_batch_reset(ctx, expand);
   }
}

/* agx_batch.c: opens and links R's expansion batch (outside draw entry). */
struct agx_batch *agx_win32_expand_open(struct agx_context *ctx,
                                        struct agx_batch *render);
/* agx_batch.c: the CPU shadow bytes [va, va + bytes) of R's pipeline pool. */
const void *agx_win32_expand_template_cpu(struct agx_batch *render,
                                          uint64_t va, size_t bytes);
#endif
