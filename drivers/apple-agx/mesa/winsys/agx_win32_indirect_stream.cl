/* SPDX-License-Identifier: MIT
 * GPU-generated pure draw-argument streams. No application state is decoded.
 * Mesa's MIT libagx encoders define the hardware words, on both host and GPU.
 */
#include "agx_win32_indirect_stream.h"

/* Native IA queries use the low 32 bits of 64-bit slots. Records run in
 * parallel here, so additions must be atomic (native's one-record helper
 * is serialized). Null means no query, never the Linux scratch sentinel. */
static inline void
agx_win32_stream_add(uint64_t address, uint32_t delta)
{
   if (address)
      atomic_add((global uint32_t *)(uintptr_t)address, delta);
}

KERNEL(32)
libagx_win32_indirect_stream(constant struct agx_win32_stream_params *p)
{
   uint32_t i = cl_global_id.x;
   uint32_t count = p->count ?
      *((constant uint32_t *)(uintptr_t)p->count) : p->max_count;
   count = count < p->max_count ? count : p->max_count;
   if (i > count)
      return;

   uint32_t slot = agx_win32_stream_record_bytes(p->index_size);
   global uint32_t *out = (global uint32_t *)(uintptr_t)
      (p->stream + (uint64_t)i * slot);
   if (i == count) {
      agx_vdm_return(out);
      return;
   }

   uint64_t arg = p->arguments + (uint64_t)i * p->argument_stride;
   if (p->ia_vertices || p->ia_primitives || p->vs_invocations ||
       p->c_primitives || p->c_invocations) {
      constant uint32_t *record = (constant uint32_t *)(uintptr_t)arg;
      uint32_t vertices = record[0] * record[1];
      uint32_t primitives =
         u_decomposed_prims_for_vertices(p->mode, record[0]) * record[1];
      agx_win32_stream_add(p->ia_vertices, vertices);
      agx_win32_stream_add(p->vs_invocations, vertices);
      agx_win32_stream_add(p->ia_primitives, primitives);
      agx_win32_stream_add(p->c_primitives, primitives);
      agx_win32_stream_add(p->c_invocations, primitives);
   }
   global uint8_t *usc = (global uint8_t *)(uintptr_t)
      (p->pipelines + (uint64_t)i * p->usc_stride);
   constant uint8_t *src = (constant uint8_t *)(uintptr_t)p->usc_template;
   for (uint32_t j = 0; j < p->usc_bytes; ++j)
      usc[j] = src[j];

   /* The native VS pipeline reserves four halfwords for first vertex and
    * base instance. Repoint that load, never modify a shared uniform word:
    * several draws can be in flight simultaneously. No shader ABI changes. */
   if (p->params_offset != UINT32_MAX) {
      struct agx_usc_builder b = agx_usc_builder(usc + p->params_offset, 8);
      agx_usc_uniform(&b, p->params_uniform, 4,
                      arg + (p->index_size ? 12 : 8));
   }

   agx_push(out, VDM_STATE, cfg)
      cfg.vertex_shader_word_1_present = true;
   agx_push(out, VDM_STATE_VERTEX_SHADER_WORD_1, cfg)
      cfg.pipeline = p->pipeline_base + i * p->usc_stride;

   struct agx_draw draw = p->index_size ?
      agx_draw_indexed_indirect(arg, p->index_buffer, p->index_range,
                                agx_translate_index_size(p->index_size),
                                p->restart) : agx_draw_indirect(arg);
   agx_vdm_draw(out, 0, draw, p->topology);
}
