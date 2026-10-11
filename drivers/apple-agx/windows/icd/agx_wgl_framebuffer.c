/* EXP1191: the AGX WGL winsys framebuffer (stw_winsys_framebuffer).
 *
 * EXP1189: every SwapBuffers of CS 1.6 waited 1.3 ms in agx_sync_all for the
 * whole frame to finish on the GPU (fence-wait receipt: 0.94 waits and
 * 1.23 ms per frame; 3.8 ms per frame without the Steam overlay UI). Mesa's
 * WGL frontend adds ST_FLUSH_WAIT to the end-of-frame flush of every window
 * framebuffer that has no winsys framebuffer (stw_st_flush), because its GDI
 * present reads the back buffer on the CPU right away. This ICD's present
 * never reads the back buffer on the CPU: it queues a GPU copy into a
 * staging slot and shows the previous frame's slot after syncing that copy
 * (agx_wgl_icd.cpp, present ring). With a winsys framebuffer -- the stw
 * interface the d3d12 winsys implements over a DXGI swapchain -- stw takes
 * the window's colour buffers from the winsys, flushes the frame without a
 * fence and calls present(), so the CPU builds frame N+1 while the GPU
 * renders frame N; the present ring's sync of frame N-1 bounds how far the
 * CPU runs ahead.
 *
 * Like the d3d12 winsys, only double-buffered formats without GDI drawing
 * get one (others keep stw's GDI path and its wait); single-sample only, as
 * the G4 builder (AGX_DBG_NOMSAA). Buffer rotation: agx_wgl_framebuffer_policy.h. */
#include <windows.h>
#include "stw_device.h"
#include "stw_context.h"
#include "stw_pixelformat.h"
#include "stw_winsys.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/os_time.h"
#include "agx_wgl_framebuffer_policy.h"

/* agx_wgl_glthread_mesa.c: the st_context's pipe (Mesa main/ headers). */
struct pipe_context *agx_wgl_st_pipe(struct st_context *st);
/* agx_wgl_icd.cpp: queue the frame's GPU copy and show the previous one. */
BOOL agx_wgl_present_window(struct pipe_screen *screen, struct pipe_context *ctx,
                            struct pipe_resource *res, HWND window);

typedef struct {
  struct stw_winsys_framebuffer base;
  struct pipe_screen *screen;
  HWND window;
  struct pipe_resource *buffers[2];
  unsigned back;  /* index of the buffer stw holds as BACK_LEFT */
  int64_t prev_swap_us;
} AGX_WGL_FRAMEBUFFER;

static AGX_WGL_FRAMEBUFFER *agx_wgl_framebuffer(struct stw_winsys_framebuffer *fb) {
  return (AGX_WGL_FRAMEBUFFER *)fb;
}

static void agx_wgl_framebuffer_release(AGX_WGL_FRAMEBUFFER *fb) {
  for (unsigned i = 0; i < 2u; ++i) pipe_resource_reference(&fb->buffers[i], NULL);
  fb->back = 1u;
}

static void agx_wgl_framebuffer_destroy(struct stw_winsys_framebuffer *base,
                                        struct pipe_context *context) {
  AGX_WGL_FRAMEBUFFER *fb = agx_wgl_framebuffer(base);
  (void)context;
  agx_wgl_framebuffer_release(fb);
  FREE(fb);
}

/* stw calls resize once per size, before taking the buffers; the template
 * carries the visual's colour format and the window size. The buffers are
 * ordinary render targets (EXP1146: no DISPLAY_TARGET in this winsys). */
static void agx_wgl_framebuffer_resize(struct stw_winsys_framebuffer *base,
                                       struct pipe_context *context,
                                       struct pipe_resource *templ) {
  AGX_WGL_FRAMEBUFFER *fb = agx_wgl_framebuffer(base);
  struct pipe_resource local;
  (void)context;
  agx_wgl_framebuffer_release(fb);
  if (!templ) return;
  local = *templ;
  local.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
  for (unsigned i = 0; i < 2u; ++i) {
    fb->buffers[i] = fb->screen->resource_create(fb->screen, &local);
    if (!fb->buffers[i]) { agx_wgl_framebuffer_release(fb); return; }
  }
}

static struct pipe_resource *
agx_wgl_framebuffer_get_resource(struct stw_winsys_framebuffer *base,
                                 enum st_attachment_type statt) {
  AGX_WGL_FRAMEBUFFER *fb = agx_wgl_framebuffer(base);
  struct pipe_resource *resource = NULL;
  int slot = agx_wgl_fb_slot(fb->back, (unsigned)statt);
  if (slot >= 0) pipe_resource_reference(&resource, fb->buffers[slot]);
  return resource;
}

/* WGL_EXT_swap_control: stw paces the GDI path itself (wait_swap_interval)
 * but leaves a winsys framebuffer's interval to present; the same pacing,
 * so a swap interval behaves as before. */
static void agx_wgl_framebuffer_pace(AGX_WGL_FRAMEBUFFER *fb, int interval) {
  int64_t now = os_time_get_nano() / 1000;
  if (interval > 0 && fb->prev_swap_us && stw_dev && stw_dev->refresh_rate > 0) {
    int64_t delta = now - fb->prev_swap_us;
    int64_t period = (int64_t)(1.0e6 / stw_dev->refresh_rate * interval);
    if (delta >= 0 && delta < period) os_time_sleep((int64_t)((period - delta) * 1.75f));
  }
  fb->prev_swap_us = now;
}

/* SwapBuffers: stw has flushed the frame (no fence) and presents the
 * buffer it holds as BACK_LEFT; on success it exchanges its pointers. */
static bool agx_wgl_framebuffer_present(struct stw_winsys_framebuffer *base,
                                        int interval) {
  AGX_WGL_FRAMEBUFFER *fb = agx_wgl_framebuffer(base);
  struct stw_context *ctx = stw_dev ? stw_current_context() : NULL;
  struct pipe_context *pipe = ctx ? agx_wgl_st_pipe(ctx->st) : NULL;
  struct pipe_resource *frame = fb->buffers[agx_wgl_fb_slot(fb->back, ST_ATTACHMENT_BACK_LEFT)];
  agx_wgl_framebuffer_pace(fb, interval);
  if (!pipe || !frame ||
      !agx_wgl_present_window(fb->screen, pipe, frame, fb->window))
    return false;
  fb->back = agx_wgl_fb_after_present(fb->back);
  return true;
}

struct stw_winsys_framebuffer *agx_wgl_create_framebuffer(struct pipe_screen *screen,
                                                          HWND window,
                                                          int pixel_format) {
  const struct stw_pixelformat_info *pfi = stw_pixelformat_get_info(pixel_format);
  if (!screen || !window || !pfi || (pfi->pfd.dwFlags & PFD_SUPPORT_GDI) ||
      !(pfi->pfd.dwFlags & PFD_DOUBLEBUFFER) || pfi->stvis.samples > 1)
    return NULL;
  AGX_WGL_FRAMEBUFFER *fb = CALLOC_STRUCT(AGX_WGL_FRAMEBUFFER);
  if (!fb) return NULL;
  fb->screen = screen;
  fb->window = window;
  fb->back = 1u;
  fb->base.destroy = agx_wgl_framebuffer_destroy;
  fb->base.resize = agx_wgl_framebuffer_resize;
  fb->base.present = agx_wgl_framebuffer_present;
  fb->base.get_resource = agx_wgl_framebuffer_get_resource;
  return &fb->base;
}
