/* Count-bounded native fallback for the Windows Asahi draw wrapper. */
#ifndef AGX_WIN32_COUNTED_INDIRECT_H
#define AGX_WIN32_COUNTED_INDIRECT_H

#include "agx_win32_asahi_batch.h"

static void
agx_win32_draw_indirect_count(struct agx_context *ctx,
                              const struct pipe_draw_info *info,
                              unsigned drawid_offset,
                              const struct pipe_draw_indirect_info *indirect,
                              const struct pipe_draw_start_count_bias *draws)
{
   struct pipe_context *pctx = &ctx->base;
   struct pipe_draw_indirect_info native = *indirect;
   struct pipe_transfer *transfer = NULL;
   int (*bindings)(void *) = ctx->windows_draw_bindings;
   unsigned reason = ctx->windows_indirect_reason;
   const unsigned bytes = info->index_size ? 20u : 16u;
   const unsigned stride = indirect->stride ? indirect->stride : bytes;

   if (!indirect->draw_count)
      return;
   if (!indirect->buffer || !indirect->indirect_draw_count ||
       (indirect->offset & 3u) || (indirect->indirect_draw_count_offset & 3u) ||
       (stride & 3u) || stride < bytes ||
       indirect->offset > indirect->buffer->width0 ||
       bytes > indirect->buffer->width0 - indirect->offset ||
       indirect->draw_count - 1u >
          (indirect->buffer->width0 - indirect->offset - bytes) / stride ||
       indirect->indirect_draw_count_offset > indirect->indirect_draw_count->width0 ||
       4u > indirect->indirect_draw_count->width0 - indirect->indirect_draw_count_offset) {
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      return;
   }

   /* A synchronized count map may flush its producer. Do it before graphics
    * batch entry or heap binding. Never generate MaxCount zeroed records:
    * native Asahi would still CPU-encode and query-count every one of them. */
   ctx->windows_draw_bindings = NULL;
   const uint32_t *count = pipe_buffer_map_range(pctx,
      indirect->indirect_draw_count, indirect->indirect_draw_count_offset,
      sizeof(*count), PIPE_MAP_READ, &transfer);
   if (!count || !transfer) {
      if (transfer)
         pipe_buffer_unmap(pctx, transfer);
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      goto done;
   }
   native.draw_count = MIN2(*count, indirect->draw_count);
   pipe_buffer_unmap(pctx, transfer);
   native.stride = stride;
   native.indirect_draw_count = NULL;
   native.indirect_draw_count_offset = 0;
   ctx->windows_draw_bindings = bindings;
   if (!ctx->any_faults && native.draw_count)
      pctx->draw_vbo(pctx, info, drawid_offset, &native, draws, 1);

done:
   ctx->windows_draw_bindings = bindings;
   /* Recursive native draws must not replace the outer call's reason/path. */
   ctx->windows_indirect_path = 2;
   ctx->windows_indirect_reason = reason;
   ctx->windows_indirect_encode_us = 0;
}
#ifdef AGX_WIN32_INDIRECT_STREAM_H
#include "agx_win32_indirect_stream_emit.h"
#endif
#endif
