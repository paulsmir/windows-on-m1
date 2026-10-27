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
   /* EXP1993 validated the stream in GravityMark (rc69 per-record params,
    * rc71 VA release and scratch reuse), so it is the default.
    * APPLE_AGX_D3D12_GPU_INDIRECT_STREAM=0 restores the CPU count path. */
   const char *stream = getenv("APPLE_AGX_D3D12_GPU_INDIRECT_STREAM");
   if (stream && stream[0] == '0')
      return 7;
   return 0;
}

/* rc79: the stream dispatch launches MaxCount + 1 work-items although only
 * count + 1 of them write (GravityMark: MaxCount 60000, ~150 executed records
 * per call, RC67 EXP1989). APPLE_AGX_D3D12_STREAM_THREADS=N (32..65536)
 * bounds the grid to N work-items that stride over the records; unset,
 * invalid or not smaller than MaxCount + 1 keeps the hardware-validated
 * one-record-per-work-item grid (params.threads 0). Hardware A/B pending. */
#define AGX_WIN32_STREAM_THREADS_MIN 32u
#define AGX_WIN32_STREAM_THREADS_MAX 65536u
static unsigned
agx_win32_stream_threads(unsigned max_count)
{
   const char *value = getenv("APPLE_AGX_D3D12_STREAM_THREADS");
   unsigned threads = 0;
   if (!value || !*value)
      return 0;
   for (; *value; ++value) {
      if (*value < '0' || *value > '9' || threads > AGX_WIN32_STREAM_THREADS_MAX)
         return 0;
      threads = threads * 10u + (unsigned)(*value - '0');
   }
   if (threads < AGX_WIN32_STREAM_THREADS_MIN ||
       threads > AGX_WIN32_STREAM_THREADS_MAX || threads >= max_count + 1u)
      return 0;
   return threads;
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

/* The generated stream is not CPU-readable by the KMD validator. Prove the
 * complete writable output, including VDM fetch padding, before launching its
 * producer. Windows process mappings use 39 bits even though VDM links encode
 * 40. Check both backing and VA extents without overflowing either sum. */
static bool
agx_win32_stream_output_valid(const struct agx_bo *bo, uint64_t va,
                              uint64_t bytes, unsigned alignment)
{
   const uint64_t limit = 1ull << 39;
   if (!bo || !bo->va || !bytes || va < 0x10000 || va >= limit ||
       bytes > limit - va || (va & (alignment - 1u)) || va < bo->va->addr)
      return false;
   uint64_t offset = va - bo->va->addr;
   return offset <= bo->size && bytes <= bo->size - offset &&
          offset <= bo->va->size_B && bytes <= bo->va->size_B - offset;
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

/* The captured block must be the complete native VS update that
 * agx_encode_state emits (header, word 0, word 1, outputs, unknown, zero
 * pad); anything else is refused rather than repeated per record. */
static bool
agx_win32_stream_vs_state_valid(const struct agx_win32_indirect_stream *stream)
{
   uint32_t header = 0, *h = &header;
   agx_push(h, VDM_STATE, cfg) {
      cfg.vertex_shader_word_0_present = true;
      cfg.vertex_shader_word_1_present = true;
      cfg.vertex_outputs_present = true;
      cfg.vertex_unknown_present = true;
   }
   return stream->vs_state_valid && stream->vs_state[0] == header &&
          !stream->vs_state[AGX_WIN32_STREAM_VS_STATE_WORDS - 1];
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
   const unsigned tail = AGX_VDM_STREAM_LINK_LENGTH + AGX_WIN32_STREAM_FETCH_PADDING;
   const struct agx_ptr chunk = batch->vdm.windows_chunk;
   const uintptr_t cpu = (uintptr_t)out, start = (uintptr_t)chunk.cpu;
   const uintptr_t end = (uintptr_t)batch->vdm.end;
   /* The active encoder can be a later pool slab, not the original vdm.bo.
    * Keep the construction coordinate from allocation/rollover. */
   if (!start || chunk.gpu < 0x10000 || cpu < start || cpu > end ||
       tail > end - cpu || (cpu & 3u) || (chunk.gpu & 3u))
      goto fail;
   uint64_t continuation = chunk.gpu + (cpu - start) + AGX_VDM_STREAM_LINK_LENGTH;
   if (continuation < chunk.gpu || continuation >= (1ull << 39) ||
       AGX_WIN32_STREAM_FETCH_PADDING > (1ull << 39) - continuation)
      goto fail;
   /* Keep rounding and pool growth valid on WOW64 as well as 64-bit hosts. */
   if (!agx_win32_stream_vs_state_valid(stream) ||
       !stream->usc_template || !stream->usc_bytes ||
       stream->usc_bytes > UINT32_MAX - 79u ||
       (stream->params_offset != UINT32_MAX &&
        (stream->usc_bytes < 8 || stream->params_offset > stream->usc_bytes - 8)))
      goto fail;
   const bool has_params = stream->params_offset != UINT32_MAX;
   unsigned usc_stride = agx_win32_stream_usc_stride(stream->usc_bytes, has_params);
   if (count > (UINT32_MAX - 16383u - tail) / record ||
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
    * them to the submission/residency set; pools retain them until cleanup.
    * RC71: each output is a whole BO the context retains across batches
    * (agx_win32_stream_scratch.h), at offset 0 of a 64 KiB aligned VA. */
   struct agx_bo *vdm_bo = NULL, *usc_bo = NULL;
   struct agx_ptr vdm = agx_win32_stream_scratch(batch, 0,
      (size_t)count * record + tail, &vdm_bo);
   struct agx_ptr usc = agx_win32_stream_scratch(batch, 1,
      (size_t)count * usc_stride, &usc_bo);
   if (!agx_win32_stream_output_valid(vdm_bo, vdm.gpu,
          (uint64_t)count * record + tail, 256) ||
       !agx_win32_stream_output_valid(usc_bo, usc.gpu,
          (uint64_t)count * usc_stride, 64) ||
       usc.gpu < dev->shader_base ||
       usc.gpu - dev->shader_base > UINT32_MAX - (uint64_t)count * usc_stride)
      goto fail;
   agx_batch_add_bo(batch, vdm_bo);
   agx_batch_add_bo(batch, usc_bo);
   if (ctx->any_faults)
      return NULL;

   struct agx_win32_stream_params params = {
      .stream = vdm.gpu, .pipelines = usc.gpu,
      .continuation = continuation,
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
      .params_data = has_params ? agx_win32_stream_params_data(stream->usc_bytes) : 0,
      .pipeline_base = agx_usc_addr(dev, usc.gpu),
      .index_range = draw.index_buffer_range_B,
      .index_size = draw.indexed ? agx_index_size_to_B(draw.index_size) : 0,
      .restart = draw.restart, .topology = topology, .mode = stream->mode,
      .ia_vertices = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_IA_VERTICES),
      .ia_primitives = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_IA_PRIMITIVES),
      .vs_invocations = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_VS_INVOCATIONS),
      .c_primitives = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_C_PRIMITIVES),
      .c_invocations = agx_win32_stream_query_address(batch, PIPE_STAT_QUERY_C_INVOCATIONS),
      .threads = agx_win32_stream_threads(count),
   };
   memcpy(params.vs_state, stream->vs_state, sizeof(params.vs_state));
   if (ctx->any_faults)
      return NULL;
   uint64_t args = agx_pool_upload_aligned(&batch->pool, &params, sizeof(params), 8);
   if (!args || !AgxWin32AsahiBatchComputeEnter(batch))
      goto fail;
   libagx_win32_indirect_stream(batch,
                                agx_1d(params.threads ? params.threads : count + 1u),
                                AGX_BARRIER_ALL | AGX_PREGFX, args);
   if (!AgxWin32AsahiBatchComputeLeave(batch))
      goto fail;
   if (ctx->any_faults)
      return NULL;

   ctx->windows_indirect_path = 3;
   return (uint8_t *)agx_vdm_jump((uint32_t *)out, vdm.gpu);
fail:
   AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
   return NULL;
}
#endif
