/* EXP1183: merge GL multi-draws into one draw before the Asahi driver.
 *
 * EXP1182: CS 1.6 issues ~500 draws per frame at ~48 us of emulated CPU per
 * draw. Mesa's vbo hands consecutive glBegin/glEnd primitives to the driver
 * as one multi-draw, but Asahi splits every multi-draw (util_draw_multi) and
 * lacks GL_POLYGON/GL_QUADS, so u_vbuf/u_primconvert turn each polygon into
 * its own driver draw. The merge produces exactly the indices those per-draw
 * paths would produce, concatenated, and submits them as one indexed draw:
 *   - POLYGON/QUADS (not native): u_index_generator per draw with the
 *     driver's native primitive mask and the API provoking vertex on both
 *     sides, as u_primconvert does -> one TRIANGLES list;
 *   - TRIANGLE_FAN/TRIANGLE_STRIP/LINE_STRIP (native): each draw's vertices
 *     in order, separated by an all-ones primitive restart index -> one draw
 *     of the same primitive (Asahi restarts these natively).
 * Plain C so the host suite can test it against per-draw expansion; draws
 * are read as (start, count) at offsets 0 and 4 of each `stride`-byte
 * element (struct pipe_draw_start_count_bias, checked by the caller). */
#ifndef AGX_WGL_DRAW_MERGE_H
#define AGX_WGL_DRAW_MERGE_H

#include <stdbool.h>
#include <stddef.h>
#include "pipe/p_defines.h" /* enum mesa_prim */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  enum mesa_prim prim;      /* primitive of the merged draw */
  unsigned index_size;      /* 2 or 4 */
  unsigned count;           /* indices written */
  bool restart;             /* primitive_restart with the all-ones index */
  unsigned restart_index;
} AGX_WGL_MERGED_DRAW;

/* Whether a non-indexed draw call of `mode` with `num_draws` draws is merged
 * for a driver with native primitive mask `hw_mask`. */
bool agx_wgl_merge_applies(unsigned hw_mask, enum mesa_prim mode,
                           unsigned num_draws);

/* Bytes of index data agx_wgl_merge_build writes for these draws. */
size_t agx_wgl_merge_bytes(unsigned hw_mask, enum mesa_prim mode,
                           bool flatshade_first,
                           const void *draws, size_t stride,
                           unsigned num_draws);

/* Write the merged index list into `out` (agx_wgl_merge_bytes bytes) and
 * describe the merged draw. Returns false when nothing is drawn. */
bool agx_wgl_merge_build(unsigned hw_mask, enum mesa_prim mode,
                         bool flatshade_first,
                         const void *draws, size_t stride,
                         unsigned num_draws, void *out,
                         AGX_WGL_MERGED_DRAW *merged);

#ifdef __cplusplus
}
#endif

#endif
