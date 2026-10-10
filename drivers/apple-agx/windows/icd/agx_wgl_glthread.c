/* EXP1176: Mesa's GL worker thread (glthread) for the AGX OpenGL ICD.
 *
 * EXP1174 sampled CS 1.6's game thread: ~56% of it ran Mesa's GL front end,
 * the Asahi driver and the UMD under x86 emulation, ~6% the engine. Mesa's
 * glthread marshals GL calls on the application thread and executes them on
 * a worker, so the driver work overlaps the game. The DRI frontend enables it
 * after creating a context and finishes it wherever the frontend itself uses
 * the context (make current, flush/swap, destroy); Mesa's WGL frontend (stw)
 * never enables it. These wrappers, exported in place of stw's Drv* entry
 * points (agx_wgl_icd.def), do the same for WGL when AGX_GLTHREAD=1.
 *
 * Asahi meets glthread's requirements: map_unsynchronized_thread_safe and
 * allow_mapped_buffers_during_execution (the u_screen default). stw paths
 * that use the context from the application thread (present, swap, unbind,
 * destroy) run only after _mesa_glthread_finish; the worker reaches stw only
 * through the st framebuffer interface (validate), as on DRI. */
#include <windows.h>
#include "stw_device.h" /* <GL/gl.h>, then gldrv.h */
#include "stw_context.h"

/* agx_wgl_glthread_mesa.c (Mesa headers, no <windows.h>) */
void agx_glthread_init_st(struct st_context *st);
void agx_glthread_finish_st(struct st_context *st);

/* EXP1177: in CS 1.6 gameplay glthread lowered the frame rate from 37.5 to
 * 29 fps median (operator: 25-27), so it is opt-in (AGX_GLTHREAD=1). */
static BOOL agx_glthread_wanted(void) {
  char value[4];
  DWORD n = GetEnvironmentVariableA("AGX_GLTHREAD", value, sizeof(value));
  return n == 1u && value[0] == '1';
}

static void agx_glthread_finish_context(struct stw_context *ctx) {
  if (ctx) agx_glthread_finish_st(ctx->st);
}

static void agx_glthread_finish_current(void) {
  agx_glthread_finish_context(stw_current_context());
}

static void agx_glthread_finish_handle(DHGLRC dhglrc) {
  if (dhglrc) agx_glthread_finish_context(stw_lookup_context(dhglrc));
}

static DHGLRC agx_glthread_start(DHGLRC dhglrc) {
  struct stw_context *ctx = dhglrc ? stw_lookup_context(dhglrc) : NULL;
  if (ctx && agx_glthread_wanted())
    agx_glthread_init_st(ctx->st);  /* stays single-threaded if refused */
  return dhglrc;
}

DHGLRC APIENTRY AgxWglDrvCreateContext(HDC hdc) {
  return agx_glthread_start(DrvCreateContext(hdc));
}

DHGLRC APIENTRY AgxWglDrvCreateLayerContext(HDC hdc, INT plane) {
  return agx_glthread_start(DrvCreateLayerContext(hdc, plane));
}

BOOL APIENTRY AgxWglDrvDeleteContext(DHGLRC dhglrc) {
  /* stw unbinds a current context and st flushes the current one first. */
  agx_glthread_finish_current();
  agx_glthread_finish_handle(dhglrc);
  return DrvDeleteContext(dhglrc);
}

PGLCLTPROCTABLE APIENTRY AgxWglDrvSetContext(HDC hdc, DHGLRC dhglrc,
                                             PFN_SETPROCTABLE set) {
  agx_glthread_finish_current();
  return DrvSetContext(hdc, dhglrc, set);
}

BOOL APIENTRY AgxWglDrvReleaseContext(DHGLRC dhglrc) {
  agx_glthread_finish_current();
  agx_glthread_finish_handle(dhglrc);
  return DrvReleaseContext(dhglrc);
}

BOOL APIENTRY AgxWglDrvCopyContext(DHGLRC src, DHGLRC dst, UINT mask) {
  agx_glthread_finish_handle(src);
  agx_glthread_finish_handle(dst);
  return DrvCopyContext(src, dst, mask);
}

BOOL APIENTRY AgxWglDrvShareLists(DHGLRC first, DHGLRC second) {
  agx_glthread_finish_handle(first);
  agx_glthread_finish_handle(second);
  return DrvShareLists(first, second);
}

BOOL APIENTRY AgxWglDrvSwapBuffers(HDC hdc) {
  agx_glthread_finish_current();
  return DrvSwapBuffers(hdc);
}

BOOL APIENTRY AgxWglDrvSwapLayerBuffers(HDC hdc, UINT planes) {
  agx_glthread_finish_current();
  return DrvSwapLayerBuffers(hdc, planes);
}

BOOL APIENTRY AgxWglDrvPresentBuffers(HDC hdc, LPPRESENTBUFFERS data) {
  agx_glthread_finish_current();
  return DrvPresentBuffers(hdc, data);
}
