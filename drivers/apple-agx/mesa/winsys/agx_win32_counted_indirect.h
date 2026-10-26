/* GPU count lowering for the Windows native Asahi draw wrapper.
 * Included after native state helpers, before the public draw wrapper. */
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
   struct agx_device *dev = agx_device(pctx->screen);
   const unsigned stride = (info->index_size ? 5u : 4u) * sizeof(uint32_t);
   const unsigned input_stride = indirect->stride ? indirect->stride : stride;
   const unsigned count = indirect->draw_count;
   struct agx_resource output = {0};
   struct pipe_draw_indirect_info patched = *indirect;
   struct agx_batch *batch;
   struct agx_ptr scratch;
   int (*bindings)(void *) = ctx->windows_draw_bindings;

   if (!count)
      return;

   /* Validate before allocating or emitting. Subtraction bounds avoid both
    * source extent wrap and the pool allocator's 16 KiB size rounding wrap
    * on 32-bit Windows. No CPU read of either input occurs here. */
   if (!indirect->buffer || !indirect->indirect_draw_count ||
       (indirect->offset & 3u) || (indirect->indirect_draw_count_offset & 3u) ||
       (input_stride & 3u) || input_stride < stride ||
       count > (UINT32_MAX - 16383u) / stride ||
       indirect->offset > indirect->buffer->width0 ||
       stride > indirect->buffer->width0 - indirect->offset ||
       count - 1u > (indirect->buffer->width0 - indirect->offset - stride) / input_stride ||
       indirect->indirect_draw_count_offset > indirect->indirect_draw_count->width0 ||
       sizeof(uint32_t) > indirect->indirect_draw_count->width0 - indirect->indirect_draw_count_offset) {
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      return;
   }

   /* Hazard flushes/internal work must not inherit application heap bindings.
    * Recursive native draws restore them and bind after their own hazards. */
   ctx->windows_draw_bindings = NULL;
   batch = agx_get_compute_batch(ctx);
   if (!batch || ctx->any_faults)
      goto done;
   agx_batch_reads(batch, agx_resource(indirect->buffer));
   agx_batch_reads(batch, agx_resource(indirect->indirect_draw_count));
   if (ctx->any_faults)
      goto done;
   agx_batch_init_state(batch);
   if (ctx->any_faults)
      goto done;

   /* Reuse the batch slab allocator. The fake native meta-resource takes no
    * Gallium ownership; pool + explicit pin + consuming batches own its BO.
    * The pin must precede launch: CDM overflow can submit/retire the producer. */
   scratch = agx_pool_alloc_aligned_with_bo(&batch->pool,
                                           (size_t)count * stride, 4,
                                           &output.bo);
   if (!scratch.cpu || !scratch.gpu || !output.bo) {
      output.bo = NULL;
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      goto done;
   }
   agx_bo_reference(output.bo);
   output.base.target = PIPE_BUFFER;
   output.base.width0 = count * stride;
   patched.buffer = &output.base;
   patched.offset = scratch.gpu - output.bo->va->addr;
   patched.stride = stride;
   patched.indirect_draw_count = NULL;
   patched.indirect_draw_count_offset = 0;

   /* Same precompiled kernel/argument ABI as Honeykrisp. Gallium serializes
    * CDM launches with a full barrier. An explicit asynchronous compute flush
    * below supplies the pre-graphics dependency through GPUVA queue ordering;
    * agx_launch_precomp itself does not interpret AGX_PREGFX. */
   libagx_predicate_indirect(batch, agx_1d(count), AGX_BARRIER_ALL | AGX_PREGFX,
      scratch.gpu, agx_map_gpu(agx_resource(indirect->buffer)) + indirect->offset,
      agx_map_gpu(agx_resource(indirect->indirect_draw_count)) +
         indirect->indirect_draw_count_offset,
      input_stride / sizeof(uint32_t), !!info->index_size);
   if (ctx->any_faults)
      goto done;
   if (agx_batch_is_active(batch))
      agx_flush_batch_for_reason(ctx, batch, "Count predicate");
   if (ctx->any_faults)
      goto done;

   /* Producer pool shadows are published only before the kernel. Consumers
    * reference the BO, never republish its CPU shadow over the GPU result. */
   perf_debug_ctx(ctx, "GPU counted indirect");
   ctx->windows_draw_bindings = bindings;
   pctx->draw_vbo(pctx, info, drawid_offset, &patched, draws, 1);

done:
   ctx->windows_draw_bindings = bindings;
   if (output.bo)
      agx_bo_unreference(dev, output.bo);
}
#ifdef AGX_WIN32_INDIRECT_STREAM_H
#include "agx_win32_indirect_stream_emit.h"
#endif
#endif
