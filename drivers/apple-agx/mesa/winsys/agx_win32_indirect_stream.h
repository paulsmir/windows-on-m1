/* SPDX-License-Identifier: MIT
 * Original Windows/Gallium stream ABI. Shared by the host and libagx kernel.
 */
#ifndef AGX_WIN32_INDIRECT_STREAM_H
#define AGX_WIN32_INDIRECT_STREAM_H

#include "compiler/libcl/libcl.h"
#include "libagx_dgc.h"

struct agx_win32_stream_params {
   uint64_t stream, pipelines, usc_template, arguments, count, index_buffer;
   uint64_t continuation;
   uint32_t max_count, argument_stride, usc_bytes, usc_stride;
   uint32_t params_offset, params_uniform, pipeline_base, index_range;
   uint32_t index_size, restart, topology;
   /* Same low 32-bit query storage used by native libagx IA updates.
    * Zero addresses disable individual counters. PS remains shader-side. */
   uint32_t mode;
   uint64_t ia_vertices, ia_primitives, vs_invocations;
   uint64_t c_primitives, c_invocations;
   /* The template's complete native VS block (agx_encode_state): header,
    * word 0, word 1, outputs, unknown, pad. Each record repeats it with only
    * word 1 (the pipeline) replaced. */
   uint32_t vs_state[6];
} PACKED;

/* RC69 (EXP1987 RC63G): every record must rebind its own USC in the form all
 * native encoders emit (agx_state.c, hk_cmd_draw.c): word 0, word 1, outputs
 * and unknown together. A word-1-only update ran all 60000 GPU records with
 * the template's draw parameters on hardware. */
#define AGX_WIN32_STREAM_VS_STATE_WORDS 6u

/* Native agx_ensure_cmdbuf_has_space/hk_ensure_cs_has_space reserve this
 * mapped tail because VDM fetches beyond the last control word. */
#define AGX_WIN32_STREAM_FETCH_PADDING 0x800u

static inline uint32_t
agx_win32_stream_record_bytes(uint32_t index_size)
{
   return AGX_WIN32_STREAM_VS_STATE_WORDS * 4u +
          AGX_INDEX_LIST_LENGTH + AGX_INDEX_LIST_INDIRECT_BUFFER_LENGTH +
          (index_size ? AGX_INDEX_LIST_BUFFER_LO_LENGTH +
                        AGX_INDEX_LIST_BUFFER_SIZE_LENGTH : 0);
}

#ifndef __OPENCL_VERSION__
/* Lives only during one native draw; never stored in a submitted batch. */
struct agx_win32_indirect_stream {
   const struct pipe_draw_indirect_info *indirect;
   uint64_t usc_template;
   uint32_t usc_bytes, params_offset, params_uniform;
   unsigned mode;
   /* Copied from the CPU VS block emitted for the template in this draw. */
   uint32_t vs_state[AGX_WIN32_STREAM_VS_STATE_WORDS];
   bool vs_state_valid;
};
#endif
#endif
