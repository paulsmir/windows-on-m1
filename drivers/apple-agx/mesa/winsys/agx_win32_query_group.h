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

#endif
