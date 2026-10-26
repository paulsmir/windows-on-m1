/* SPDX-License-Identifier: MIT
 * Included after native Asahi state helpers. Only pure, ordinary VS/FS draws
 * use this path; unsupported lowering/query paths retain their own draws.
 */
#ifndef AGX_WIN32_INDIRECT_STREAM_EMIT_H
#define AGX_WIN32_INDIRECT_STREAM_EMIT_H

static unsigned
agx_win32_stream_fallback(struct agx_context *ctx,
                          const struct pipe_draw_info *info,
                          unsigned drawid, unsigned num_draws)
{
   if (info->mode == MESA_PRIM_PATCHES || ctx->in_tess ||
       ctx->stage[MESA_SHADER_TESS_CTRL].shader ||
       ctx->stage[MESA_SHADER_TESS_EVAL].shader)
      return 1;
   if (ctx->stage[MESA_SHADER_GEOMETRY].shader || ctx->streamout.num_targets ||
       ctx->active_draw_without_restart)
      return 2;
   if (ctx->occlusion_query || ctx->time_elapsed || ctx->cond_query ||
       ctx->tf_any_overflow)
      return 3;
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->pipeline_statistics); ++i)
      if (ctx->pipeline_statistics[i] &&
          (i > PIPE_STAT_QUERY_CS_INVOCATIONS || info->primitive_restart))
         return 3;
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->prims_generated); ++i)
      if (ctx->prims_generated[i] || ctx->tf_prims_generated[i] ||
          ctx->tf_overflow[i])
         return 3;
   if (drawid || num_draws != 1 || info->has_user_indices ||
       (info->index_size && info->index_size != 2 && info->index_size != 4) ||
       !ctx->stage[MESA_SHADER_VERTEX].shader ||
       ctx->stage[MESA_SHADER_VERTEX].shader->info.windows_uses_draw_id)
      return 4;
   bool xfb_only = false;
   if (agx_needs_passthrough_gs(ctx, info, NULL, &xfb_only))
      return 5;
   if (agx_device(ctx->base.screen)->chip > AGX_CHIP_G13X)
      return 6;
   return 0;
}

/* Register the native query writer and retain its BO for both CDM and
 * render submission. Resolve flushes that writer before copying results. */
static uint64_t
agx_win32_stream_query_address(struct agx_batch *batch, unsigned statistic)
{
   struct agx_context *ctx = batch->ctx;
   return ctx->active_queries && ctx->pipeline_statistics[statistic] ?
      agx_get_query_address(batch, ctx->pipeline_statistics[statistic]) : 0;
}

static unsigned
agx_win32_stream_cdm_bytes(void)
{
   return AGX_CDM_LAUNCH_WORD_0_LENGTH + AGX_CDM_LAUNCH_WORD_1_LENGTH +
          AGX_CDM_UNK_G14X_LENGTH + AGX_CDM_INDIRECT_LENGTH +
          AGX_CDM_GLOBAL_SIZE_LENGTH + AGX_CDM_LOCAL_SIZE_LENGTH +
          AGX_CDM_BARRIER_LENGTH;
}

static bool
agx_win32_stream_inputs_valid(const struct pipe_draw_info *info,
                               const struct pipe_draw_indirect_info *in)
{
   unsigned bytes = info->index_size ? 20u : 16u;
   unsigned stride = in->stride ? in->stride : bytes;
   if (!in->buffer || !in->draw_count || (in->offset & 3) ||
       (stride & 3) || stride < bytes || in->offset > in->buffer->width0 ||
       bytes > in->buffer->width0 - in->offset ||
       in->draw_count - 1u > (in->buffer->width0 - in->offset - bytes) / stride)
      return false;
   if (in->indirect_draw_count &&
       ((in->indirect_draw_count_offset & 3) ||
        in->indirect_draw_count_offset > in->indirect_draw_count->width0 ||
        4u > in->indirect_draw_count->width0 - in->indirect_draw_count_offset))
      return false;
   return true;
}

/* Called with frontend heap callbacks suspended, before graphics batch entry.
 * A previous draw may have produced the count/arguments in this framebuffer.
 * CDM runs before ALL render work in a batch, so that writer must be submitted
 * first. The later native reads retain both BOs through completion. */
static void
agx_win32_stream_flush_inputs(struct agx_context *ctx,
                               const struct pipe_draw_indirect_info *in)
{
   agx_flush_writer(ctx, agx_resource(in->buffer), "Indirect stream arguments");
   if (in->indirect_draw_count && !ctx->any_faults)
      agx_flush_writer(ctx, agx_resource(in->indirect_draw_count),
                       "Indirect stream count");
   struct agx_batch *batch = ctx->batch;
   if (!ctx->any_faults && batch && !agx_batch_is_compute(batch) &&
       batch->cdm.bo &&
       (size_t)(batch->cdm.end - batch->cdm.current) <=
          2u * agx_win32_stream_cdm_bytes())
      agx_flush_batch_for_reason(ctx, batch, "Indirect stream CDM capacity");
}

static uint8_t *
agx_win32_emit_indirect_stream(struct agx_batch *batch,
                                struct agx_win32_indirect_stream *stream,
                                struct agx_draw draw,
                                enum agx_primitive topology, uint8_t *out)
{
   struct agx_context *ctx = batch->ctx;
   struct agx_device *dev = agx_device(ctx->base.screen);
   const struct pipe_draw_indirect_info *in = stream->indirect;
   const unsigned count = in->draw_count;
   unsigned record = agx_win32_stream_record_bytes(draw.indexed);
   /* Keep rounding and pool growth valid on WOW64 as well as 64-bit hosts. */
   if (!stream->usc_template || !stream->usc_bytes ||
       stream->usc_bytes > UINT32_MAX - 63u ||
       (stream->params_offset != UINT32_MAX &&
        (stream->usc_bytes < 8 || stream->params_offset > stream->usc_bytes - 8)))
      goto fail;
   unsigned usc_stride = ALIGN_POT(stream->usc_bytes, 64);
   if (count > (UINT32_MAX - 16383u - AGX_VDM_BARRIER_LENGTH) / record ||
       count > (UINT32_MAX - 16383u) / usc_stride)
      goto fail;

   /* A graphics batch's CDM is pre-raster work, exactly as for native GS.
    * Never allow agx_launch_internal to auto-submit while this draw is entered. */
   if (!batch->cdm.bo)
      batch->cdm = agx_encoder_allocate(batch, dev);
   if (!batch->cdm.bo || !batch->cdm.current || !batch->cdm.end ||
       (size_t)(batch->cdm.end - batch->cdm.current) <=
          2u * agx_win32_stream_cdm_bytes())
      goto fail;

   agx_batch_reads(batch, agx_resource(in->buffer));
   if (in->indirect_draw_count)
      agx_batch_reads(batch, agx_resource(in->indirect_draw_count));
   if (ctx->any_faults)
      return NULL;

   /* Output-only batch pools use writable General BOs, including USC low VA.
    * They are never CPU-mapped or published. Explicit batch references add
    * them to the submission/residency set; pools retain them until cleanup. */
   struct agx_bo *vdm_bo = NULL, *usc_bo = NULL;
   struct agx_ptr vdm = agx_pool_alloc_aligned_with_bo(&batch->windows_stream_pool,
      (size_t)count * record + AGX_VDM_BARRIER_LENGTH, 64, &vdm_bo);
   struct agx_ptr usc = agx_pool_alloc_aligned_with_bo(&batch->windows_stream_usc_pool,
      (size_t)count * usc_stride, 64, &usc_bo);
   if (!vdm.gpu || !vdm_bo || !usc.gpu || !usc_bo ||
       usc.gpu < dev->shader_base ||
       usc.gpu - dev->shader_base > UINT32_MAX - (uint64_t)count * usc_stride)
      goto fail;
   agx_batch_add_bo(batch, vdm_bo);
   agx_batch_add_bo(batch, usc_bo);
   if (ctx->any_faults)
      return NULL;

   struct agx_win32_stream_params params = {
      .stream = vdm.gpu, .pipelines = usc.gpu,
      .usc_template = stream->usc_template,
      .arguments = draw.b.ptr,
      .count = in->indirect_draw_count ?
         agx_map_gpu(agx_resource(in->indirect_draw_count)) +
            in->indirect_draw_count_offset : 0,
      .index_buffer = draw.index_buffer,
      .max_count = count,
      .argument_stride = in->stride ? in->stride : (draw.indexed ? 20 : 16),
      .usc_bytes = stream->usc_bytes, .usc_stride = usc_stride,
      .params_offset = stream->params_offset,
      .params_uniform = stream->params_uniform,
      .pipeline_base = agx_usc_addr(dev, usc.gpu),
      .index_range = draw.index_buffer_range_B,
      .index_size = draw.indexed ? agx_index_size_to_B(draw.index_size) : 0,
      .restart = draw.restart, .topology = topology, .mode = stream->mode,
      .ia_vertices = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_IA_VERTICES),
      .ia_primitives = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_IA_PRIMITIVES),
      .vs_invocations = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_VS_INVOCATIONS),
      .c_primitives = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_C_PRIMITIVES),
      .c_invocations = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_C_INVOCATIONS),
   };
   if (ctx->any_faults)
      return NULL;
   uint64_t args = agx_pool_upload_aligned(&batch->pool, &params, sizeof(params), 8);
   if (!args || !AgxWin32AsahiBatchComputeEnter(batch))
      goto fail;
   libagx_win32_indirect_stream(batch, agx_1d(count + 1u),
                                AGX_BARRIER_ALL | AGX_PREGFX, args);
   if (!AgxWin32AsahiBatchComputeLeave(batch))
      goto fail;
   if (ctx->any_faults)
      return NULL;

   ctx->windows_indirect_path = 3;
   return (uint8_t *)agx_vdm_call((uint32_t *)out, vdm.gpu);
fail:
   AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
   return NULL;
}
#endif
