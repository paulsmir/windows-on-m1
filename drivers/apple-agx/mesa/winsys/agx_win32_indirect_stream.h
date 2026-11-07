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
   /* Byte offset of each record's private draw-parameter copy in its USC
    * slot; meaningful only when params_offset is not UINT32_MAX. */
   uint32_t params_data;
   /* rc79: 0 = work-item i writes record i (the grid is max_count + 1, as
    * hardware-validated since RC33); otherwise the grid has this many
    * work-items and work-item i writes records i, i + threads, ... up to
    * and including the terminator, so a MaxCount of 60000 with a GPU count
    * of ~150 (RC67 EXP1989) need not launch 60001 work-items. */
   uint32_t threads;
   /* rc89: the outputs hold this many records (1..max_count), sized from
    * the GPU counts the context observed instead of MaxCount. The kernel
    * writes min(count, capacity) records and the terminator, never more. */
   uint32_t capacity, reserved;
   /* rc89: AGX_WIN32_STREAM_FEEDBACK_WORDS words the dispatch reports to
    * (0 = none): see the indices below. */
   uint64_t feedback;
} PACKED;

/* rc89 feedback words, written by work-item 0 of each dispatch with
 * atomics (several dispatches and batches share them): the largest GPU
 * count clamped to MaxCount, the dispatches whose count exceeded their
 * capacity (their records past it were not drawn), and all dispatches. */
#define AGX_WIN32_STREAM_FEEDBACK_MAX_COUNT 0u
#define AGX_WIN32_STREAM_FEEDBACK_OVERFLOWS 1u
#define AGX_WIN32_STREAM_FEEDBACK_DISPATCHES 2u
#define AGX_WIN32_STREAM_FEEDBACK_WORDS 4u

/* RC69 (EXP1987 RC63G): every record must rebind its own USC in the form all
 * native encoders emit (agx_state.c, hk_cmd_draw.c): word 0, word 1, outputs
 * and unknown together. A word-1-only update ran all 60000 GPU records with
 * the template's draw parameters on hardware. */
#define AGX_WIN32_STREAM_VS_STATE_WORDS 6u

/* RC69: the PARAMS load of a record reads {first vertex or base vertex, base
 * instance} from a copy this dispatch writes after the record's USC words,
 * not from the application's (possibly GPU-copied) argument memory. RC44I
 * validated PARAMS loads only from CPU-written arguments. */
#define AGX_WIN32_STREAM_PARAMS_BYTES 8u

static inline uint32_t
agx_win32_stream_params_data(uint32_t usc_bytes)
{
   return (usc_bytes + 7u) & ~7u;
}

/* Callers bound usc_bytes by UINT32_MAX - 79 so neither sum wraps. */
static inline uint32_t
agx_win32_stream_usc_stride(uint32_t usc_bytes, bool params)
{
   uint32_t bytes = params ? agx_win32_stream_params_data(usc_bytes) +
                                AGX_WIN32_STREAM_PARAMS_BYTES : usc_bytes;
   return (bytes + 63u) & ~63u;
}

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
