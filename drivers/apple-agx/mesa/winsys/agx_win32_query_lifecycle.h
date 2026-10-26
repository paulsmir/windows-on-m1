/* SPDX-License-Identifier: MIT
 * Included in agx_query.c after the native query lifetime helpers.
 * Query generations include resolve readers as well as hardware writers.
 */
#ifndef AGX_WIN32_QUERY_LIFECYCLE_H
#define AGX_WIN32_QUERY_LIFECYCLE_H

static void
agx_win32_query_release_snapshot(struct agx_context *ctx, struct agx_query *q)
{
   for (unsigned i = 0; i < q->windows_timestamp_count; ++i)
      agx_bo_unreference(agx_device(ctx->base.screen), q->windows_timestamp_bos[i]);
   q->windows_timestamp_count = 0;
}

void
AgxWin32AsahiQueryCleanup(struct agx_context *ctx)
{
   struct agx_query **link = &ctx->windows_retired_queries;
   while (*link) {
      struct agx_query *q = *link;
      if (is_query_busy(ctx, q)) {
         link = &q->windows_next;
      } else {
         *link = q->windows_next;
         agx_destroy_query(&ctx->base, (struct pipe_query *)q);
      }
   }
}

static bool
agx_win32_query_reset(struct agx_context *ctx, struct agx_query *q)
{
   AgxWin32AsahiQueryCleanup(ctx);
   if (is_query_busy(ctx, q)) {
      /* Never zero storage still named by an encoder or resolve. Keep the
       * public pipe_query identity, and retire the old allocation by generation.
       * This also preserves occlusion slots until every GPU reader is done. */
      struct agx_query *fresh = (struct agx_query *)
         agx_create_query(&ctx->base, q->type, q->index);
      if (!fresh)
         return false;
      struct agx_query old = *q;
      *q = *fresh;
      *fresh = old;
      fresh->windows_next = ctx->windows_retired_queries;
      ctx->windows_retired_queries = fresh;
   } else {
      agx_win32_query_release_snapshot(ctx, q);
   }
   return true;
}

#endif
