/* SPDX-License-Identifier: MIT
 * Original Windows pipeline-statistics kernel (RC67). One dispatch counts the
 * input-assembly statistics of every record of an unrolled multi-draw
 * indirect. Native Asahi counts each record with its own one-invocation
 * libagx_increment_ia dispatch followed by a CDM barrier (agx_ia_update,
 * agx_launch_internal). The arithmetic is native poly_increment_ia for a
 * non-patch, non-restart draw: vertices = count * instances and
 * primitives = u_decomposed_prims_for_vertices(mode, count) * instances,
 * accumulated in the low 32 bits of the query slots.
 */
#include "compiler/libcl/libcl.h"
#include "compiler/shader_enums.h"

/* Records run in parallel here, so additions must be atomic (native's
 * one-record helper is serialized by its own CDM barrier). Zero means no
 * query, never the Linux scratch sentinel. */
static inline void
agx_win32_query_ia_add(uint64_t address, uint32_t delta)
{
   if (address)
      atomic_add((global uint32_t *)(uintptr_t)address, delta);
}

/* Invocation i reads record i at records + i * stride_B: the record the
 * unrolled native draw i reads (offset + i * stride). The caller knows the
 * record count on the CPU and dispatches at least that many invocations. */
KERNEL(32)
libagx_win32_query_ia_records(uint64_t ia_vertices, uint64_t ia_primitives,
                              uint64_t vs_invocations, uint64_t c_primitives,
                              uint64_t c_invocations, uint64_t records,
                              uint32_t count, uint32_t stride_B, uint32_t mode)
{
   uint32_t i = cl_global_id.x;
   if (i >= count)
      return;

   constant uint32_t *record = (constant uint32_t *)(uintptr_t)
      (records + (uint64_t)i * stride_B);
   uint32_t vertices = record[0] * record[1];
   uint32_t primitives =
      u_decomposed_prims_for_vertices((enum mesa_prim)mode, record[0]) *
      record[1];
   agx_win32_query_ia_add(ia_vertices, vertices);
   agx_win32_query_ia_add(vs_invocations, vertices);
   agx_win32_query_ia_add(ia_primitives, primitives);
   agx_win32_query_ia_add(c_primitives, primitives);
   agx_win32_query_ia_add(c_invocations, primitives);
}
