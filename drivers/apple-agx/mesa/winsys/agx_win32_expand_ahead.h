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
 *
 * PF1 "expand fold" (default on since EXP2335; APPLE_AGX_D3D12_EXPAND_FOLD=0
 * turns it off; read once; EXPAND_AHEAD=1 wins and turns it off): no batch
 * of its own. The kernel becomes the last dispatches of the context's open application compute
 * batch Wb when Wb wrote the arguments/count, or writes the render target,
 * of R's counted draw (handoff PF1-EXPAND-FOLD.md, r16 design a'):
 * - agx_win32_stream_flush_inputs does not flush Wb for those inputs; it
 *   SEALS Wb and links it as R's expansion batch (the IE1a link, E := Wb).
 * - Sealed: a hazard flush of Wb that R itself requests (agx_batch_reads or
 *   agx_batch_writes of R) is deferred (agx_win32_fold_defers): the link
 *   submits Wb before R, which is the order the hazard asks for. Every other
 *   flush of Wb (a new dispatch -- agx_get_compute_batch flushes Wb first,
 *   so no application work joins it after R --, a CPU map, an eviction,
 *   flush_all, another batch's hazard) goes ahead and passes the one
 *   seal-drop point, agx_win32_fold_drop at the top of agx_flush_batch;
 *   after a seal drop R's later counted draws keep the in-render prelude.
 * - PF1c: while the pair stays sealed, R's later counted draws append their
 *   kernels to Wb without repeating the first seal's order check
 *   (agx_win32_fold_render_writes): R became a writer of Wb's BOs after the
 *   seal only through deferred hazards, which run after Wb, and any other
 *   writer of a kernel's arguments/count is flushed before the draw
 *   (agx_win32_stream_flush_inputs; flushing R drops the seal first).
 * - agx_flush_batch(R) flushes Wb first (agx_win32_expand_flush_first);
 *   Wb's own rc105 scan skips R, which depends on it (agx_win32_fold_follows).
 *   A Wb without kernels is application work: submitted, never reset.
 * - R reset unlinks Wb and never aborts it (application work); R's stream
 *   outputs move to Wb's pools so they retire with the batch that writes
 *   them (agx_win32_fold_adopt, RC71).
 * - R (AGX_WIN32_FOLD_MARK_RENDER) and Wb (AGX_WIN32_FOLD_MARK_COMPUTE)
 *   carry a mark until they retire: the D3D12 owner never lets either lead
 *   or overlap across the pair (agx_d3d12_job_barriers.h
 *   AGX_D3D12_JOB_FOLDED, batch-kind flag AGX_WIN32_FOLD_KIND_FLAG).
 * - PF1b, pipeline statistics: the kernel adds IA_VERTICES, IA_PRIMITIVES,
 *   VS_INVOCATIONS, C_PRIMITIVES and C_INVOCATIONS. Like every native
 *   statistic writer (Asahi agx_get_query_address -> agx_add_query_to_batch)
 *   the batch that launches it, Wb, retains the query BO and is registered
 *   as a writer of the query (agx_win32_stream_query_address); R keeps its
 *   own registration (its draw uniforms register R for every active
 *   statistic, and it writes PS_INVOCATIONS), so a resolve, a GetData or a
 *   Begin's busy check flushes or waits for both, Wb first (flushing R
 *   flushes its linked Wb first; flushing Wb drops the seal). Predication
 *   still refuses (agx_win32_fold_queries_allow).
 * - A render reset discards no counted draw except on a context fault: an
 *   agx_flush_batch reset needs a batch without draws, the other resets are
 *   fault aborts and context teardown. Only then does a kept Wb still add
 *   the IA/VS counts of a dropped draw, on a device that is already lost.
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

/* PF1 opt-out: APPLE_AGX_D3D12_EXPAND_FOLD exactly "0". */
static inline bool
agx_win32_expand_fold_parse_off(const char *value)
{
   return value && value[0] == '0' && !value[1];
}

/* PF1: on by default (hardware experiment EXP2335 is the evidence);
 * APPLE_AGX_D3D12_EXPAND_FOLD=0 turns it off, any other value or none
 * leaves it on. EXPAND_AHEAD=1 wins and turns it off. */
static inline bool
agx_win32_expand_fold_enabled(void)
{
   static int mode = -1;
   if (mode < 0)
      mode = !agx_win32_expand_fold_parse_off(getenv("APPLE_AGX_D3D12_EXPAND_FOLD")) &&
             !agx_win32_expand_ahead_enabled() ? 1 : 0;
   return mode == 1;
}

/* The prelude stays whenever the expansion writes query results: the
 * statistics addresses register the query writer on the dispatching batch
 * (agx_win32_stream_query_address). Predication never reaches the stream
 * (agx_win32_stream_fallback reason 3). */
static inline bool
agx_win32_expand_queries_allow(const struct agx_context *ctx)
{
   if (ctx->cond_query)
      return false;
   if (ctx->active_queries) {
      for (unsigned i = 0; i < ARRAY_SIZE(ctx->pipeline_statistics); ++i)
         if (ctx->pipeline_statistics[i])
            return false;
   }
   return true;
}

/* PF1b: the fold registers the statistics on the batch that launches the
 * kernel (Wb), so only predication refuses it. */
static inline bool
agx_win32_fold_queries_allow(const struct agx_context *ctx)
{
   return !ctx->cond_query;
}

static inline bool
agx_win32_expand_ahead_allowed(const struct agx_context *ctx)
{
   return agx_win32_expand_ahead_enabled() && agx_win32_expand_queries_allow(ctx);
}

/* PF1 counters (ctx->windows_fold_counts, one measure-d3d12-counted-fold
 * line per present, AgxWin32AsahiFoldCounters): seals, folded kernels,
 * deferred hazard flushes, refusals 1..9 at REFUSED + reason, flushes of a
 * sealed batch by cause 0..7 at CAUSED + cause (cause 1 = submitted first
 * by its own render, the expected end; every other cause drops the seal). */
#define AGX_WIN32_FOLD_SEALS 0u
#define AGX_WIN32_FOLD_KERNELS 1u
#define AGX_WIN32_FOLD_DEFERRED 2u
#define AGX_WIN32_FOLD_REFUSED 2u
#define AGX_WIN32_FOLD_CAUSED 12u
#define AGX_WIN32_FOLD_COUNTS 20u
/* refusal reasons */
#define AGX_WIN32_FOLD_REFUSE_QUERY 1u        /* predication */
#define AGX_WIN32_FOLD_REFUSE_NO_COMPUTE 2u   /* no open compute batch */
#define AGX_WIN32_FOLD_REFUSE_NOT_WRITER 3u   /* it writes no input nor target */
#define AGX_WIN32_FOLD_REFUSE_PRELUDE 4u      /* the render already has a CDM */
#define AGX_WIN32_FOLD_REFUSE_CAPACITY 5u     /* no CDM room for two launches */
#define AGX_WIN32_FOLD_REFUSE_OTHER_RENDER 6u /* sealed for another render */
#define AGX_WIN32_FOLD_REFUSE_ORDER 7u        /* the render writes what it uses */
#define AGX_WIN32_FOLD_REFUSE_LOST 8u         /* fault, or flushed meanwhile */
#define AGX_WIN32_FOLD_REFUSE_DRAW 9u         /* no sealed link at the draw */
/* causes of a flush of a sealed batch */
#define AGX_WIN32_FOLD_CAUSE_OTHER 0u     /* agx_flush_batch_for_reason etc. */
#define AGX_WIN32_FOLD_CAUSE_RENDER 1u    /* its render submits it first */
#define AGX_WIN32_FOLD_CAUSE_DISPATCH 2u  /* new work for the compute batch */
#define AGX_WIN32_FOLD_CAUSE_EVICT 3u     /* batch slot eviction */
#define AGX_WIN32_FOLD_CAUSE_FLUSH_ALL 4u /* agx_flush_all / agx_sync_all */
#define AGX_WIN32_FOLD_CAUSE_RESOURCE 5u  /* CPU map, explicit resource flush */
#define AGX_WIN32_FOLD_CAUSE_HAZARD 6u    /* another batch's hazard */
#define AGX_WIN32_FOLD_CAUSE_RESET 7u     /* its render was reset: unlinked */
/* marks, kept until the batch is reused */
#define AGX_WIN32_FOLD_MARK_RENDER 1u
#define AGX_WIN32_FOLD_MARK_COMPUTE 2u
/* AgxWin32BatchKindHook flag: (AGX_D3D12_JOB_FOLDED = 2) << 8 */
#define AGX_WIN32_FOLD_KIND_FLAG 0x200u
typedef char agx_win32_fold_counts_fit[
   AGX_WIN32_FOLD_REFUSED + AGX_WIN32_FOLD_REFUSE_DRAW < AGX_WIN32_FOLD_CAUSED &&
   AGX_WIN32_FOLD_CAUSED + AGX_WIN32_FOLD_CAUSE_RESET < AGX_WIN32_FOLD_COUNTS &&
   sizeof(((struct agx_context *)0)->windows_fold_counts) ==
      AGX_WIN32_FOLD_COUNTS * sizeof(unsigned) ? 1 : -1];

static inline void
agx_win32_fold_count(struct agx_context *ctx, unsigned slot)
{
   if (slot < AGX_WIN32_FOLD_COUNTS)
      ++ctx->windows_fold_counts[slot];
}

/* the batch-kind flag of a batch that took part in a fold */
static inline unsigned
agx_win32_fold_kind(const struct agx_batch *batch)
{
   return batch->windows_fold_marks ? AGX_WIN32_FOLD_KIND_FLAG : 0u;
}

/* Wb's render while Wb is sealed for it, else NULL. */
static inline struct agx_batch *
agx_win32_fold_owner(struct agx_batch *wb)
{
   struct agx_batch *render = wb->windows_expand_fold ? wb->windows_expand_owner : NULL;
   if (!render || render->windows_expand != wb || !agx_batch_is_active(render) ||
       agx_batch_is_submitted(render))
      return NULL;
   return render;
}

/* Labels the next flush of a sealed batch (the first label wins until that
 * flush consumes it). */
static inline void
agx_win32_fold_cause(struct agx_batch *batch, unsigned cause)
{
   if (batch->windows_expand_fold && !batch->windows_fold_cause)
      batch->windows_fold_cause = cause;
}

/* The hazard flush of `batch` that `requester` asks for (the except batch
 * of agx_flush_writer_except / agx_flush_readers_except, the writing batch
 * of agx_batch_writes_internal) is deferred when batch is sealed for
 * requester: requester is the render the link submits after batch. Else
 * the caller flushes, labelled `cause`. Switch off: never sealed, false. */
static inline bool
agx_win32_fold_defers(struct agx_context *ctx, struct agx_batch *batch,
                      struct agx_batch *requester, unsigned cause)
{
   if (!batch->windows_expand_fold)
      return false;
   if (requester && agx_win32_fold_owner(batch) == requester) {
      agx_win32_fold_count(ctx, AGX_WIN32_FOLD_DEFERRED);
      return true;
   }
   agx_win32_fold_cause(batch, cause);
   return false;
}

/* The one seal-drop point (top of agx_flush_batch, through
 * agx_win32_expand_flush_first): a sealed batch being flushed stops taking
 * kernels and R loses its link; R's later counted draws keep the prelude.
 * windows_expand_owner stays until the batch is reused, so that its rc105
 * scan still skips R (agx_win32_fold_follows). Never resets the batch. */
static inline void
agx_win32_fold_drop(struct agx_context *ctx, struct agx_batch *wb)
{
   struct agx_batch *render = wb->windows_expand_owner;
   if (!wb->windows_expand_fold)
      return;
   agx_win32_fold_count(ctx, AGX_WIN32_FOLD_CAUSED + (wb->windows_fold_cause & 7u));
   if (wb->windows_fold_cause != AGX_WIN32_FOLD_CAUSE_RENDER)
      AgxWin32PerfNote("Expand fold: seal dropped");
   wb->windows_expand_fold = false;
   wb->windows_fold_cause = 0;
   if (render && render->windows_expand == wb)
      render->windows_expand = NULL;
}

/* rc105 (native-asahi-submit-order.py): `writer`, an open writer of a BO
 * `batch` uses, is the render that depends on the folded batch being
 * flushed: it took that BO's writes (or reads) after the seal through a
 * deferred hazard, so it runs after batch, never before. */
static inline bool
agx_win32_fold_follows(const struct agx_batch *batch, const struct agx_batch *writer)
{
   return (batch->windows_fold_marks & AGX_WIN32_FOLD_MARK_COMPUTE) &&
          batch->windows_expand_owner == writer;
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
   /* PF1: a folded compute batch is application work: never reset here */
   if (batch->windows_fold_marks & AGX_WIN32_FOLD_MARK_COMPUTE) {
      agx_win32_fold_drop(ctx, batch);
   } else if (batch->windows_expand_owner && !batch->windows_expand_dispatches) {
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
   if (expand->windows_expand_fold)
      expand->windows_fold_cause = AGX_WIN32_FOLD_CAUSE_RENDER;
   else
      AgxWin32PerfNote("Expand ahead: expansion submitted first");
   agx_flush_batch(ctx, expand);
   /* Every failed or refused E submission faults the context, which aborts
    * R in agx_flush_batch; an E left open would follow R. */
   if (!ctx->any_faults && agx_batch_is_active(expand))
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
   return false;
}

/* agx_win32_expand_cleanup: R's stream outputs join Wb's pools (agx_batch.c). */
void agx_win32_fold_adopt(struct agx_batch *wb, struct agx_batch *render);

/* agx_batch_cleanup: unlink E from R; a render cleaned up with an open E
 * (reset, never submitted) aborts it, so E never writes outputs R gave back.
 * PF1: a sealed Wb is application work: it is only unlinked, and takes R's
 * stream outputs, which its kernels may still write, until it retires. */
static inline void
agx_win32_expand_cleanup(struct agx_context *ctx, struct agx_batch *batch)
{
   struct agx_batch *owner = batch->windows_expand_owner;
   if (owner && owner->windows_expand == batch)
      owner->windows_expand = NULL;
   batch->windows_expand_owner = NULL;
   batch->windows_expand_fold = false;
   struct agx_batch *expand = agx_win32_expand_linked(batch);
   batch->windows_expand = NULL;
   if (expand && expand->windows_expand_fold) {
      agx_win32_fold_count(ctx, AGX_WIN32_FOLD_CAUSED + AGX_WIN32_FOLD_CAUSE_RESET);
      AgxWin32PerfNote("Expand fold: render reset, compute batch kept");
      expand->windows_expand_fold = false;
      expand->windows_fold_cause = 0;
      expand->windows_expand_owner = NULL;
      agx_win32_fold_adopt(expand, batch);
   } else if (expand) {
      expand->windows_expand_owner = NULL;
      AgxWin32PerfNote("Expand ahead: expansion aborted with its render");
      agx_batch_reset(ctx, expand);
   }
}

/* agx_batch.c (PF1): the context's open application compute batch (the
 * agx_get_compute_batch key), never created here; whether `wb` is the
 * recorded writer of the resource; of a target of the bound framebuffer;
 * whether `render` is the recorded writer of a BO wb uses (rc105 would then
 * submit render first). */
struct agx_batch *agx_win32_fold_compute(struct agx_context *ctx);
bool agx_win32_fold_writer(struct agx_context *ctx, struct agx_batch *wb,
                           struct pipe_resource *resource);
bool agx_win32_fold_target(struct agx_context *ctx, struct agx_batch *wb);
bool agx_win32_fold_render_writes(struct agx_context *ctx, struct agx_batch *wb,
                                  struct agx_batch *render);

/* PF1, top of agx_get_compute_batch: a sealed compute batch accepts no
 * further work; it is submitted before the new work is recorded. */
static inline void
agx_win32_fold_dispatch(struct agx_context *ctx)
{
   struct agx_batch *wb = agx_win32_fold_compute(ctx);
   if (!wb || !wb->windows_expand_fold)
      return;
   agx_win32_fold_cause(wb, AGX_WIN32_FOLD_CAUSE_DISPATCH);
   agx_flush_batch_for_reason(ctx, wb, "Expand fold: new compute work");
}

/* agx_batch.c: opens and links R's expansion batch (outside draw entry). */
struct agx_batch *agx_win32_expand_open(struct agx_context *ctx,
                                        struct agx_batch *render);
/* agx_batch.c: the CPU shadow bytes [va, va + bytes) of R's pipeline pool. */
const void *agx_win32_expand_template_cpu(struct agx_batch *render,
                                          uint64_t va, size_t bytes);
#endif
