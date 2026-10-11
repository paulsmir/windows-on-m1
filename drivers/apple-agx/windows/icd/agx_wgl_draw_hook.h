/* EXP1183: GL multi-draw merge hook on the Asahi screen (agx_wgl_draw_hook.c). */
#ifndef AGX_WGL_DRAW_HOOK_H
#define AGX_WGL_DRAW_HOOK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pipe_screen;

/* Receipt counters: draw calls, draws, merged calls, draws in merged calls,
 * POLYGON/QUADS calls converted per draw (indexed/indirect/xfb). */
#define AGX_WGL_MERGE_COUNTERS 5u

/* Advertise GL_POLYGON/GL_QUADS and hook every context the screen creates.
 * Call once, before the first context. */
bool agx_wgl_draw_hook_install(struct pipe_screen *screen);

/* Read and reset the counters. */
void agx_wgl_draw_hook_counters(unsigned out[AGX_WGL_MERGE_COUNTERS]);

#ifdef __cplusplus
}
#endif

#endif
