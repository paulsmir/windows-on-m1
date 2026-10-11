/* EXP1183: index merge for GL multi-draws (see agx_wgl_draw_merge.h). */
#include "agx_wgl_draw_merge.h"

#include <string.h>
#include "indices/u_indices.h"
#include "util/u_prim.h"

static unsigned draw_start(const void *draws, size_t stride, unsigned i) {
  return *(const unsigned *)((const unsigned char *)draws + stride * i);
}

static unsigned draw_count(const void *draws, size_t stride, unsigned i) {
  return *(const unsigned *)((const unsigned char *)draws + stride * i + 4u);
}

static bool generated_prim(unsigned hw_mask, enum mesa_prim mode) {
  return (mode == MESA_PRIM_POLYGON || mode == MESA_PRIM_QUADS) &&
         !(hw_mask & (1u << mode));
}

static bool restart_prim(unsigned hw_mask, enum mesa_prim mode) {
  return (mode == MESA_PRIM_TRIANGLE_FAN || mode == MESA_PRIM_TRIANGLE_STRIP ||
          mode == MESA_PRIM_LINE_STRIP) &&
         (hw_mask & (1u << mode));
}

bool agx_wgl_merge_applies(unsigned hw_mask, enum mesa_prim mode,
                           unsigned num_draws) {
  /* A single native draw gains nothing; a non-native one still replaces
   * u_primconvert's per-draw upload. */
  if (generated_prim(hw_mask, mode)) return num_draws >= 1;
  return restart_prim(hw_mask, mode) && num_draws > 1;
}

static unsigned max_end(const void *draws, size_t stride, unsigned num_draws) {
  unsigned end = 0;
  for (unsigned i = 0; i < num_draws; ++i) {
    unsigned start = draw_start(draws, stride, i), count = draw_count(draws, stride, i);
    if (count && start + count > end) end = start + count;
  }
  return end;
}

/* u_index_generator picks the index size from start + nr; pass a start that
 * selects the size chosen for the whole merged draw. */
static unsigned size_start(unsigned index_size) {
  return index_size == 4 ? 0xffffu : 0u;
}

static unsigned generated_index_size(const void *draws, size_t stride,
                                     unsigned num_draws) {
  return max_end(draws, stride, num_draws) > 0xfffeu ? 4u : 2u;
}

/* The restart index must not name a vertex. */
static unsigned restart_index_size(const void *draws, size_t stride,
                                   unsigned num_draws) {
  return max_end(draws, stride, num_draws) > 0xffffu ? 4u : 2u;
}

size_t agx_wgl_merge_bytes(unsigned hw_mask, enum mesa_prim mode,
                           bool flatshade_first,
                           const void *draws, size_t stride,
                           unsigned num_draws) {
  unsigned pv = flatshade_first ? PV_FIRST : PV_LAST;
  size_t indices = 0;
  if (generated_prim(hw_mask, mode)) {
    unsigned size = generated_index_size(draws, stride, num_draws);
    for (unsigned i = 0; i < num_draws; ++i) {
      /* As u_primconvert: degenerate primitives are dropped and the count is
       * trimmed to whole primitives before generating (a 1-vertex polygon
       * would otherwise underflow (nr - 2) * 3). */
      unsigned count = draw_count(draws, stride, i);
      if (u_trim_pipe_prim(mode, &count))
        indices += u_index_count_converted_indices(hw_mask, true, mode, count);
    }
    (void)pv;
    return indices * size;
  }
  if (restart_prim(hw_mask, mode)) {
    unsigned size = restart_index_size(draws, stride, num_draws);
    for (unsigned i = 0; i < num_draws; ++i)
      if (draw_count(draws, stride, i)) indices += draw_count(draws, stride, i) + 1u;
    return indices * size;
  }
  return 0;
}

static void put_index(void *out, unsigned size, unsigned at, unsigned value) {
  if (size == 2) ((unsigned short *)out)[at] = (unsigned short)value;
  else ((unsigned *)out)[at] = value;
}

bool agx_wgl_merge_build(unsigned hw_mask, enum mesa_prim mode,
                         bool flatshade_first,
                         const void *draws, size_t stride,
                         unsigned num_draws, void *out,
                         AGX_WGL_MERGED_DRAW *merged) {
  unsigned pv = flatshade_first ? PV_FIRST : PV_LAST;
  memset(merged, 0, sizeof(*merged));
  if (generated_prim(hw_mask, mode)) {
    unsigned size = generated_index_size(draws, stride, num_draws);
    unsigned char *dst = (unsigned char *)out;
    merged->index_size = size;
    merged->prim = MESA_PRIM_TRIANGLES;
    for (unsigned i = 0; i < num_draws; ++i) {
      enum mesa_prim prim;
      unsigned out_size, out_nr, count = draw_count(draws, stride, i);
      u_generate_func gen;
      if (!u_trim_pipe_prim(mode, &count)) continue;
      (void)u_index_generator(hw_mask, mode, size_start(size), count,
                              pv, pv, &prim, &out_size, &out_nr, &gen);
      if (out_size != size || !out_nr) continue;
      merged->prim = prim;
      gen(draw_start(draws, stride, i), out_nr, dst);
      dst += (size_t)out_nr * size;
      merged->count += out_nr;
    }
    return merged->count != 0;
  }
  if (restart_prim(hw_mask, mode)) {
    unsigned size = restart_index_size(draws, stride, num_draws), at = 0;
    merged->index_size = size;
    merged->prim = mode;
    merged->restart = true;
    merged->restart_index = size == 2 ? 0xffffu : 0xffffffffu;
    for (unsigned i = 0; i < num_draws; ++i) {
      unsigned start = draw_start(draws, stride, i), count = draw_count(draws, stride, i);
      if (!count) continue;
      if (at) put_index(out, size, at++, merged->restart_index);
      for (unsigned v = 0; v < count; ++v) put_index(out, size, at++, start + v);
    }
    merged->count = at;
    return at != 0;
  }
  return false;
}
