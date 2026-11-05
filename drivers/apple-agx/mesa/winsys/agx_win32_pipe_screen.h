#ifndef APPLE_AGX_MESA_WIN32_PIPE_SCREEN_H
#define APPLE_AGX_MESA_WIN32_PIPE_SCREEN_H

#include "agx_win32_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pipe_screen *AgxWin32PipeScreenCreate(AGX_WIN32_SCREEN *Screen);
typedef struct _AGX_WIN32_PIPE_DEVICE {
  AGX_WIN32_SCREEN *Runtime;
  struct pipe_screen *Screen;
  struct pipe_context *Context;
  APPLE_AGX_U32 Generation;
} AGX_WIN32_PIPE_DEVICE;

/* Zero-initialized caller storage. Runtime must remain alive until Close
 * succeeds. These functions create no DDI tables and advertise no pipeline. */
int AgxWin32PipeDeviceInitialize(AGX_WIN32_PIPE_DEVICE *Device,
                                AGX_WIN32_SCREEN *Runtime);
int AgxWin32PipeDeviceClose(AGX_WIN32_PIPE_DEVICE *Device);
/* Serialized device teardown. Failure preserves both objects; success releases
 * only this screen and its sole context, not the borrowed Windows runtime. */
int AgxWin32PipeScreenReleaseDevice(struct pipe_screen *Screen,
                                   struct pipe_context *OwnedContext);
AGX_WIN32_SCREEN_BUFFER *AgxWin32PipeResourceBuffer(
    struct pipe_resource *Resource);

#ifdef __cplusplus
}
#endif

#endif
