/* EXP1183: install the GL multi-draw merge (agx_wgl_draw_merge.c) on the
 * Asahi screen and its contexts.
 *
 * The screen additionally advertises GL_POLYGON and GL_QUADS, so u_vbuf
 * hands their multi-draws to the driver instead of splitting them through
 * u_primconvert; GL_QUAD_STRIP stays unadvertised, which keeps the mask
 * incomplete and u_vbuf enabled exactly as before. Every POLYGON/QUADS draw
 * therefore reaches the hook and is turned into native primitives here:
 * non-indexed draws are merged (one TRIANGLES list per call), indexed,
 * restarted or indirect ones and any draw while transform feedback is bound
 * go through a u_primconvert context with the driver's original mask (the
 * previous u_vbuf behaviour). Native fans/strips with several draws are
 * merged with primitive restart. */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "indices/u_primconvert.h"
#include "util/u_inlines.h"
#include "agx_wgl_draw_merge.h"
#include "agx_wgl_draw_hook.h"

_Static_assert(offsetof(struct pipe_draw_start_count_bias, start) == 0,
               "merge reads start at offset 0");
_Static_assert(offsetof(struct pipe_draw_start_count_bias, count) == 4,
               "merge reads count at offset 4");

#define AGX_WGL_HOOK_CONTEXTS 16u

typedef struct {
  struct pipe_context *pipe;
  void (*draw_vbo)(struct pipe_context *, const struct pipe_draw_info *, unsigned,
                   const struct pipe_draw_indirect_info *,
                   const struct pipe_draw_start_count_bias *, unsigned);
  void (*bind_rasterizer)(struct pipe_context *, void *);
  void (*set_so_targets)(struct pipe_context *, unsigned,
                         struct pipe_stream_output_target **, const unsigned *,
                         enum mesa_prim);
  void (*destroy)(struct pipe_context *);
  const struct pipe_rasterizer_state *rast;
  unsigned so_targets;
  struct primconvert_context *pc;
  void *scratch;
  size_t scratch_bytes;
} AGX_WGL_HOOK;

static AGX_WGL_HOOK AgxWglHooks[AGX_WGL_HOOK_CONTEXTS];
static unsigned AgxWglNativeMask;
static struct pipe_context *(*AgxWglContextCreate)(struct pipe_screen *, void *,
                                                   unsigned);
/* Receipt counters (game thread; read and reset by the present receipt). */
static volatile long AgxWglMergeCounters[AGX_WGL_MERGE_COUNTERS];

static AGX_WGL_HOOK *hook_of(struct pipe_context *pipe) {
  for (unsigned i = 0; i < AGX_WGL_HOOK_CONTEXTS; ++i)
    if (AgxWglHooks[i].pipe == pipe) return &AgxWglHooks[i];
  return NULL;
}

static bool flatshade_first(const AGX_WGL_HOOK *h) {
  /* struct agx_rasterizer starts with its pipe_rasterizer_state. */
  return h->rast && h->rast->flatshade_first;
}

static void primconvert_draw(AGX_WGL_HOOK *h, const struct pipe_draw_info *info,
                             unsigned drawid_offset,
                             const struct pipe_draw_indirect_info *indirect,
                             const struct pipe_draw_start_count_bias *draws,
                             unsigned num_draws) {
  if (!h->pc) h->pc = util_primconvert_create(h->pipe, AgxWglNativeMask);
  if (!h->pc) return;
  util_primconvert_save_flatshade_first(h->pc, flatshade_first(h));
  util_primconvert_draw_vbo(h->pc, info, drawid_offset, indirect, draws, num_draws);
}

static void hook_draw_vbo(struct pipe_context *pipe, const struct pipe_draw_info *info,
                          unsigned drawid_offset,
                          const struct pipe_draw_indirect_info *indirect,
                          const struct pipe_draw_start_count_bias *draws,
                          unsigned num_draws) {
  AGX_WGL_HOOK *h = hook_of(pipe);
  enum mesa_prim mode = (enum mesa_prim)info->mode;
  bool unsupported = mode == MESA_PRIM_POLYGON || mode == MESA_PRIM_QUADS;
  if (!h) return;
  ++AgxWglMergeCounters[0];
  AgxWglMergeCounters[1] += num_draws;
  if (!indirect && !info->index_size && !h->so_targets &&
      agx_wgl_merge_applies(AgxWglNativeMask, mode, num_draws)) {
    bool first = flatshade_first(h);
    size_t bytes = agx_wgl_merge_bytes(AgxWglNativeMask, mode, first, draws,
                                       sizeof(*draws), num_draws);
    if (bytes > h->scratch_bytes) {
      void *grown = realloc(h->scratch, bytes);
      if (grown) { h->scratch = grown; h->scratch_bytes = bytes; }
    }
    if (bytes <= h->scratch_bytes) {
      AGX_WGL_MERGED_DRAW merged;
      if (agx_wgl_merge_build(AgxWglNativeMask, mode, first, draws, sizeof(*draws),
                              num_draws, h->scratch, &merged)) {
        struct pipe_draw_info one = *info;
        struct pipe_draw_start_count_bias range = {0u, merged.count, 0};
        one.mode = merged.prim;
        one.index_size = (uint16_t)merged.index_size;
        one.has_user_indices = true;
        one.index.user = h->scratch;
        one.primitive_restart = merged.restart;
        one.restart_index = merged.restart_index;
        one.index_bounds_valid = false;
        one.min_index = 0;
        one.max_index = ~0u;
        one.increment_draw_id = false;
        ++AgxWglMergeCounters[2];
        AgxWglMergeCounters[3] += num_draws;
        h->draw_vbo(pipe, &one, drawid_offset, NULL, &range, 1);
      }
      return;  /* nothing to draw when every draw was degenerate */
    }
  }
  if (unsupported) {
    ++AgxWglMergeCounters[4];
    primconvert_draw(h, info, drawid_offset, indirect, draws, num_draws);
    return;
  }
  h->draw_vbo(pipe, info, drawid_offset, indirect, draws, num_draws);
}

static void hook_bind_rasterizer(struct pipe_context *pipe, void *cso) {
  AGX_WGL_HOOK *h = hook_of(pipe);
  if (!h) return;
  h->rast = (const struct pipe_rasterizer_state *)cso;
  h->bind_rasterizer(pipe, cso);
}

static void hook_set_so_targets(struct pipe_context *pipe, unsigned num,
                                struct pipe_stream_output_target **targets,
                                const unsigned *offsets, enum mesa_prim prim) {
  AGX_WGL_HOOK *h = hook_of(pipe);
  if (!h) return;
  h->so_targets = num;
  h->set_so_targets(pipe, num, targets, offsets, prim);
}

static void hook_destroy(struct pipe_context *pipe) {
  AGX_WGL_HOOK *h = hook_of(pipe);
  void (*destroy)(struct pipe_context *) = h ? h->destroy : NULL;
  if (!h) return;
  if (h->pc) util_primconvert_destroy(h->pc);
  free(h->scratch);
  memset(h, 0, sizeof(*h));
  destroy(pipe);
}

static struct pipe_context *hook_context_create(struct pipe_screen *screen,
                                                void *priv, unsigned flags) {
  struct pipe_context *pipe = AgxWglContextCreate(screen, priv, flags);
  AGX_WGL_HOOK *h;
  if (!pipe) return NULL;
  h = hook_of(NULL);
  if (!h || !pipe->draw_vbo || !pipe->bind_rasterizer_state || !pipe->destroy ||
      !pipe->set_stream_output_targets) {
    /* No free slot: this context would see POLYGON/QUADS it cannot draw. */
    pipe->destroy(pipe);
    return NULL;
  }
  h->draw_vbo = pipe->draw_vbo;
  h->bind_rasterizer = pipe->bind_rasterizer_state;
  h->set_so_targets = pipe->set_stream_output_targets;
  h->destroy = pipe->destroy;
  h->pipe = pipe;
  pipe->draw_vbo = hook_draw_vbo;
  pipe->bind_rasterizer_state = hook_bind_rasterizer;
  pipe->set_stream_output_targets = hook_set_so_targets;
  pipe->destroy = hook_destroy;
  return pipe;
}

bool agx_wgl_draw_hook_install(struct pipe_screen *screen) {
  if (!screen || !screen->context_create || AgxWglContextCreate) return false;
  AgxWglNativeMask = screen->caps.supported_prim_modes;
  AgxWglContextCreate = screen->context_create;
  screen->context_create = hook_context_create;
  /* pipe_screen::caps is const once the screen is built; like the drivers'
   * own caps setup, adjust it before any context reads it. */
  ((struct pipe_caps *)&screen->caps)->supported_prim_modes |=
      (1u << MESA_PRIM_POLYGON) | (1u << MESA_PRIM_QUADS);
  return true;
}

void agx_wgl_draw_hook_counters(unsigned out[AGX_WGL_MERGE_COUNTERS]) {
  for (unsigned i = 0; i < AGX_WGL_MERGE_COUNTERS; ++i) {
    out[i] = (unsigned)AgxWglMergeCounters[i];
    AgxWglMergeCounters[i] = 0;
  }
}
