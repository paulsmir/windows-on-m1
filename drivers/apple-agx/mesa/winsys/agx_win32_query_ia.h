/* SPDX-License-Identifier: MIT
 * RC67: input-assembly statistics of an unrolled multi-draw indirect in one
 * dispatch. Included into the projected agx_state.c after the native IA
 * helpers and before agx_draw_vbo_windows_body (native-asahi-query-ia.py).
 *
 * Native Asahi unrolls a multi-draw indirect into one draw per record
 * (util_draw_multi_unroll_indirect). With a pipeline-statistics query active,
 * every record then runs agx_ia_update: one libagx_increment_ia dispatch and
 * a CDM barrier (agx_launch_internal) in the batch's compute prelude, which
 * the render waits for. EXP1986 GravityMark HD: ~1020 per Execute, almost
 * all from its counted ExecuteIndirect calls (CPU count fallback, path 2).
 *
 * Here one libagx_win32_query_ia_records dispatch reads the same records the
 * unrolled draws read (offset + i * stride), from the same CDM position the
 * first native update would have used, and adds the same native sums
 * (poly_increment_ia, non-patch and non-restart) to the same query slots.
 * The unrolled draws then skip their native update. Every case whose native
 * count differs or reads other data stays native per draw.
 */
#ifndef AGX_WIN32_QUERY_IA_H
#define AGX_WIN32_QUERY_IA_H

/* Mirrors the native per-draw IA gate in agx_draw_vbo_windows_body as each
 * unrolled draw would evaluate it, plus the cases that are native only. */
static bool
agx_win32_query_ia_records_eligible(struct agx_context *ctx,
                                    const struct pipe_draw_info *info,
                                    const struct pipe_draw_indirect_info *indirect)
{
   if (!indirect || !indirect->buffer || indirect->draw_count < 2 ||
       indirect->indirect_draw_count || indirect->count_from_stream_output)
      return false;
   if (ctx->windows_indirect_stream || ctx->windows_query_ia_records ||
       !ctx->active_queries || ctx->in_tess || ctx->active_draw_without_restart)
      return false;
   const bool clipper = !ctx->stage[MESA_SHADER_TESS_EVAL].shader &&
                        !ctx->stage[MESA_SHADER_GEOMETRY].shader;
   if (!ctx->pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES] &&
       !ctx->pipeline_statistics[PIPE_STAT_QUERY_IA_PRIMITIVES] &&
       !ctx->pipeline_statistics[PIPE_STAT_QUERY_VS_INVOCATIONS] &&
       !((ctx->pipeline_statistics[PIPE_STAT_QUERY_C_PRIMITIVES] ||
          ctx->pipeline_statistics[PIPE_STAT_QUERY_C_INVOCATIONS]) && clipper))
      return false;
   /* Native only: restart counts by scanning the indices, patches count
    * with the tessellation vertex count, a render condition is evaluated per
    * unrolled draw, and a passthrough GS draws (and counts) recursively with
    * the GS bound, which moves the clipper counters to the GS prefix sum. */
   if (info->primitive_restart || info->mode == MESA_PRIM_PATCHES ||
       ctx->cond_query || !ctx->stage[MESA_SHADER_VERTEX].shader)
      return false;
   bool xfb_only = false;
   if (agx_needs_passthrough_gs(ctx, info, indirect, &xfb_only))
      return false;
   /* The kernel reads the first two words of every record. */
   const unsigned stride = indirect->stride, offset = indirect->offset;
   const unsigned width = indirect->buffer->width0;
   if (stride < 8u || (stride & 3u) || (offset & 3u) || offset > width ||
       8u > width - offset ||
       indirect->draw_count - 1u > (width - offset - 8u) / stride)
      return false;
   return true;
}

/* Zero for an absent query: the kernel skips it (native passes the scratch
 * page). Present queries register this batch as their writer, as native. */
static uint64_t
agx_win32_query_ia_address(struct agx_batch *batch, unsigned statistic)
{
   struct agx_query *query = batch->ctx->pipeline_statistics[statistic];
   return query ? agx_get_query_address(batch, query) : 0;
}

/* True when the records of this unrolled call are counted; the caller then
 * suppresses the native update of each unrolled draw. False keeps native
 * counting, also after a fault (the caller checks ctx->any_faults). */
static bool
agx_win32_query_ia_records(struct agx_context *ctx,
                           const struct pipe_draw_info *info,
                           const struct pipe_draw_indirect_info *indirect)
{
   if (!agx_win32_query_ia_records_eligible(ctx, info, indirect))
      return false;
   struct agx_batch *batch = agx_get_batch(ctx);
   if (!batch)
      return false;
   struct agx_device *dev = agx_device(ctx->base.screen);
   if (!batch->cdm.bo) {
      batch->cdm = agx_encoder_allocate(batch, dev);
      if (!batch->cdm.bo || !batch->cdm.current) {
         AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
         return false;
      }
   }

   const bool clipper = !ctx->stage[MESA_SHADER_TESS_EVAL].shader &&
                        !ctx->stage[MESA_SHADER_GEOMETRY].shader;
   const uint64_t records = agx_indirect_buffer_ptr(batch, indirect);
   const uint64_t ia_vertices =
      agx_win32_query_ia_address(batch, PIPE_STAT_QUERY_IA_VERTICES);
   const uint64_t ia_primitives =
      agx_win32_query_ia_address(batch, PIPE_STAT_QUERY_IA_PRIMITIVES);
   const uint64_t vs_invocations =
      agx_win32_query_ia_address(batch, PIPE_STAT_QUERY_VS_INVOCATIONS);
   const uint64_t c_primitives = clipper ?
      agx_win32_query_ia_address(batch, PIPE_STAT_QUERY_C_PRIMITIVES) : 0;
   const uint64_t c_invocations = clipper ?
      agx_win32_query_ia_address(batch, PIPE_STAT_QUERY_C_INVOCATIONS) : 0;
   if (ctx->any_faults)
      return false;

   perf_debug(dev, "Input assembly counters, records batched");
   libagx_win32_query_ia_records(batch, agx_1d(indirect->draw_count),
                                 AGX_BARRIER_ALL, ia_vertices, ia_primitives,
                                 vs_invocations, c_primitives, c_invocations,
                                 records, (uint32_t)indirect->draw_count,
                                 (uint32_t)indirect->stride,
                                 (uint32_t)info->mode);
   return !ctx->any_faults;
}

#endif
