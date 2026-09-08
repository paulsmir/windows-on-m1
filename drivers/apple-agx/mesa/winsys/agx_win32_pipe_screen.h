#ifndef APPLE_AGX_MESA_WIN32_PIPE_SCREEN_H
#define APPLE_AGX_MESA_WIN32_PIPE_SCREEN_H

#include "agx_win32_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pipe_screen *AgxWin32PipeScreenCreate(AGX_WIN32_SCREEN *Screen);
AGX_WIN32_SCREEN_BUFFER *AgxWin32PipeResourceBuffer(
    struct pipe_resource *Resource);

#ifdef __cplusplus
}
#endif

#endif
