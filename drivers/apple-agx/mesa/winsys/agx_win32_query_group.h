/* SPDX-License-Identifier: MIT
 * RC67: the queries of one D3D12 query index share one BO.
 * Included in agx_query.c after agx_win32_query_lifecycle.h (GPUVA only,
 * native-asahi-query-group.py).
 *
 * The D3D12 owner keeps 11 PIPELINE_STATISTICS_SINGLE queries per index
 * (agx_d3d12_windows.cpp) and always begins, ends and resolves the 11
 * together. Native agx_create_query gives each its own 16-byte BO, a 64 KiB
 * Windows allocation; every Begin CPU-zeroes all 11 and each dirty BO pays
 * its own copy escapes at the next submission (EXP1988: 28 QUERY escapes on
 * 64 KiB slots per HD Execute, 12.4 ms).
 *
 * Group invariants:
 * - one BO holds exactly the queries of one group, query i at 16 * i;
 *   no other query, index or heap shares it, so a whole-page upload or
 *   download of that BO only ever carries this group's slots;
 * - the group is reset as a unit before its Begin: if any member is busy
 *   (GPU writer or resolve reader), every member swaps in storage from one
 *   fresh group BO and its old storage retires by generation, exactly as
 *   agx_win32_query_reset does for a single query; the per-query reset in
 *   agx_begin_query then finds no busy member and allocates nothing;
 * - each query holds its own BO reference, so the BO is freed after the
 *   last retired member is destroyed.
 */
#ifndef AGX_WIN32_QUERY_GROUP_H
#define AGX_WIN32_QUERY_GROUP_H

#define AGX_WIN32_QUERY_GROUP_MAX 16u

/* Idle queries of a new group. Null on failure, nothing left allocated. */
static bool
agx_win32_query_group_create(struct agx_context *ctx, unsigned type,
                             unsigned count, struct agx_query **queries)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   struct agx_bo *bo = agx_bo_create(dev, sizeof(uint64_t) * 2 * count, 0,
                                     AGX_BO_WRITEBACK, "Query");
   uint8_t *cpu = bo ? (uint8_t *)agx_bo_map(bo) : NULL;
   if (!bo || !cpu || !bo->va || !bo->va->addr) {
      if (bo)
         agx_bo_unreference(dev, bo);
      return false;
   }
   for (unsigned i = 0; i < count; ++i) {
      struct agx_query *q = (struct agx_query *)calloc(1, sizeof(struct agx_query));
      if (!q) {
         while (i--) {
            agx_bo_unreference(dev, queries[i]->bo);
            free(queries[i]);
            queries[i] = NULL;
         }
         agx_bo_unreference(dev, bo);
         return false;
      }
      q->type = type;
      q->index = i;
      for (unsigned g = 0; g < ARRAY_SIZE(q->writer_generation); ++g)
         q->writer_generation[g] = UINT64_MAX;
      agx_bo_reference(bo);
      q->bo = bo;
      q->ptr = (struct agx_ptr){
         .gpu = bo->va->addr + sizeof(uint64_t) * 2 * i,
         .cpu = cpu + sizeof(uint64_t) * 2 * i,
      };
      queries[i] = q;
   }
   agx_bo_unreference(dev, bo); /* the members hold the BO */
   return true;
}

/* Called before Begin of every member. Creates the group's queries when
 * all are absent, and moves the whole group to fresh storage when any
 * member is busy or the members do not share one BO. Query i is
 * PIPE type `type` with index i. False (no memory, invalid arguments)
 * leaves every query unchanged; the caller's per-query path still works. */
/* Declared for C++ callers in agx_win32_asahi_scene.h. */
int AgxWin32AsahiQueryGroupBegin(struct pipe_context *, unsigned,
                                 struct pipe_query **, unsigned);

int
AgxWin32AsahiQueryGroupBegin(struct pipe_context *pctx, unsigned type,
                             struct pipe_query **group, unsigned count)
{
   struct agx_context *ctx = agx_context(pctx);
   if (!ctx || !group || !count || count > AGX_WIN32_QUERY_GROUP_MAX ||
       type != PIPE_QUERY_PIPELINE_STATISTICS_SINGLE)
      return 0;
   AgxWin32AsahiQueryCleanup(ctx);

   struct agx_query **members = (struct agx_query **)group;
   bool fresh = false;
   for (unsigned i = 0; i < count; ++i) {
      if (!members[i] || members[i]->type != type || members[i]->index != i ||
          !members[i]->bo || members[i]->bo != members[0]->bo ||
          members[i]->ptr.gpu != members[0]->ptr.gpu + sizeof(uint64_t) * 2 * i ||
          is_query_busy(ctx, members[i]))
         fresh = true;
   }
   if (!fresh)
      return 1;

   struct agx_query *storage[AGX_WIN32_QUERY_GROUP_MAX];
   if (!agx_win32_query_group_create(ctx, type, count, storage))
      return 0;
   for (unsigned i = 0; i < count; ++i) {
      if (!members[i]) {
         members[i] = storage[i];
         continue;
      }
      /* Keep the public pipe_query identity; retire the old storage. */
      struct agx_query old = *members[i];
      *members[i] = *storage[i];
      *storage[i] = old;
      storage[i]->windows_next = ctx->windows_retired_queries;
      ctx->windows_retired_queries = storage[i];
   }
   /* Idle retired storage is released now, busy storage by generation. */
   AgxWin32AsahiQueryCleanup(ctx);
   return 1;
}

/* OV3 needs the projected agx_query.c (agx_win32_asahi_batch.h is included
 * there before this header; the RC67 host model includes this one alone). */
#ifdef AGX_WIN32_ASAHI_BATCH_H
/* OV3: a D3D12 pipeline-statistics resolve whose GPU copy runs in the CDM
 * prelude of a later render batch instead of the compute batch after the
 * render that wrote the query (agx_get_query_result_resource_gpu), so that
 * compute batch stays independent of that render (OV2).
 *
 * Step 1, at ResolveQueryData: submit the query's writers now, as the native
 * GPU resolve does. Step 2, after a later draw: append the copy to the CDM of
 * the current render batch, which has at least one draw and is not itself a
 * writer or reader of the query. The prelude runs before that render's TA and
 * after every render and compute job submitted before it (Asahi compute
 * barriers 0/0; the KMD window's COMPUTE_ON_3D / COMPUTE_ON_COMPUTE lead
 * barrier), so the copy reads the finished statistics. The batch becomes a
 * reader of the query like the native resolve (Begin then moves the group to
 * fresh storage) and a writer of the destination range.
 * FlushWriters: 0 = not a GPU-resolved statistic; resolve the native way.
 * IntoRender: 1 = copied; 0 = no render batch with a draw now (keep it);
 * -1 = this batch cannot take it (resolve the native way).
 * OV3b (ExistingPrelude != 0, EXP2197: OV3's own preludes added ~1.3 prelude
 * renders per frame, each starting behind the overlapping compute): copy only
 * into a render batch whose CDM prelude already exists (RC67 IA statistics or
 * an ExecuteIndirect expansion allocated it); otherwise 0, so no render gains
 * a prelude or its lead barrier. */
int AgxWin32AsahiQueryResolveFlushWriters(struct pipe_context *, struct pipe_query *);
int AgxWin32AsahiQueryResolveIntoRender(struct pipe_context *, struct pipe_query *,
                                        struct pipe_resource *, unsigned, int);

int
AgxWin32AsahiQueryResolveFlushWriters(struct pipe_context *pctx, struct pipe_query *pq)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_query *query = (struct agx_query *)pq;
   if (!ctx || !query || query->type != PIPE_QUERY_PIPELINE_STATISTICS_SINGLE ||
       !query->bo || !query->ptr.gpu)
      return 0;
   flush_query_writers(ctx, query, "Deferred query resolve");
   return 1;
}

int
AgxWin32AsahiQueryResolveIntoRender(struct pipe_context *pctx, struct pipe_query *pq,
                                    struct pipe_resource *prsrc, unsigned offset,
                                    int existing_prelude)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_query *query = (struct agx_query *)pq;
   struct agx_batch *batch = ctx ? ctx->batch : NULL;
   if (!ctx || ctx->any_faults || !batch || agx_batch_is_compute(batch) || !batch->draws)
      return 0;
   /* OV3b: the render batch CDM is allocated lazily by its first prelude
    * dispatch (agx_state.c, agx_win32_query_ia.h, indirect stream emit) */
   if (existing_prelude && !batch->cdm.bo)
      return 0;
   if (!query || !prsrc || query->type != PIPE_QUERY_PIPELINE_STATISTICS_SINGLE ||
       !query->bo || !query->ptr.gpu ||
       offset > prsrc->width0 || prsrc->width0 - offset < sizeof(uint64_t))
      return -1;
   /* a batch that writes (or already reads) the query would copy before the
    * statistics it accumulates are final */
   unsigned idx = agx_batch_idx(batch);
   if (query->writer_generation[idx] == ctx->batches.generation[idx])
      return -1;
   if (!batch->cdm.bo) {
      batch->cdm = agx_encoder_allocate(batch, agx_device(ctx->base.screen));
      if (!batch->cdm.bo)
         return -1;
   }
   struct agx_resource *rsrc = agx_resource(prsrc);
   agx_add_query_to_batch(batch, query);
   agx_batch_writes_range(batch, rsrc, offset, sizeof(uint64_t));
   /* the writes may have flushed batches that use the destination, never
    * this one (it neither reads nor writes it yet) */
   if (ctx->batch != batch)
      return -1;
   libagx_copy_query_gl(batch, agx_1d(1), AGX_BARRIER_ALL | AGX_PREGFX,
                        query->ptr.gpu, agx_map_gpu(rsrc) + offset,
                        PIPE_QUERY_TYPE_U64, 0);
   return 1;
}

#endif /* AGX_WIN32_ASAHI_BATCH_H */
#endif
