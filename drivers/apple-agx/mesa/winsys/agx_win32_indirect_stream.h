/* SPDX-License-Identifier: MIT
 * Original Windows/Gallium stream ABI. Shared by the host and libagx kernel.
 */
#ifndef AGX_WIN32_INDIRECT_STREAM_H
#define AGX_WIN32_INDIRECT_STREAM_H

#include "compiler/libcl/libcl.h"
#include "libagx_dgc.h"

struct agx_win32_stream_params {
   uint64_t stream, pipelines, usc_template, arguments, count, index_buffer;
   uint32_t max_count, argument_stride, usc_bytes, usc_stride;
   uint32_t params_offset, params_uniform, pipeline_base, index_range;
   uint32_t index_size, restart, topology;
} PACKED;

static inline uint32_t
agx_win32_stream_record_bytes(uint32_t index_size)
{
   return AGX_VDM_STATE_LENGTH + AGX_VDM_STATE_VERTEX_SHADER_WORD_1_LENGTH +
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
};
#endif
#endif
