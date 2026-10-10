/* EXP1176: the Mesa half of agx_wgl_glthread.c. Mesa's main/ headers refuse
 * <windows.h> in the same translation unit, so the gl_context side of the
 * WGL glthread wrappers lives here and takes the stw context's st_context. */
#include "state_tracker/st_context.h"
#include "main/glthread.h"

void agx_glthread_init_st(struct st_context *st) {
  if (st && st->ctx) _mesa_glthread_init(st->ctx);  /* no-op if refused */
}

void agx_glthread_finish_st(struct st_context *st) {
  if (st && st->ctx) _mesa_glthread_finish(st->ctx);  /* no-op when disabled */
}
