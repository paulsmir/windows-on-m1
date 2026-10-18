/* EXP1176: the Mesa half of agx_wgl_glthread.c. Mesa's main/ headers refuse
 * <windows.h> in the same translation unit, so the gl_context side of the
 * WGL glthread wrappers lives here and takes the stw context's st_context. */
#include "state_tracker/st_context.h"
#include "main/glthread.h"

/* EXP1176: glthread's worker initialisation calls st_set_background_context,
 * which asserts the frontend screen's set_background_context hook. DRI's
 * hook only adds the queue to its HUD (dri_set_background_context); stw has
 * no HUD queue monitoring and never set the hook, so the smoke aborted. */
static void agx_glthread_background_context(struct st_context *st,
                                            struct util_queue_monitoring *queue) {
  (void)st; (void)queue;
}

void agx_glthread_init_st(struct st_context *st) {
  if (!st || !st->ctx || !st->frontend_screen) return;
  if (!st->frontend_screen->set_background_context)
    st->frontend_screen->set_background_context = agx_glthread_background_context;
  _mesa_glthread_init(st->ctx);  /* no-op if refused */
}

void agx_glthread_finish_st(struct st_context *st) {
  if (st && st->ctx) _mesa_glthread_finish(st->ctx);  /* no-op when disabled */
}

/* EXP1191: the pipe context of an stw context's st_context, for the winsys
 * framebuffer's present (agx_wgl_framebuffer.c). Called from SwapBuffers on
 * the application thread, after stw finished glthread (AgxWglDrvSwapBuffers). */
struct pipe_context *agx_wgl_st_pipe(struct st_context *st) {
  return st ? st->pipe : NULL;
}
