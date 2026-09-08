#ifndef APPLE_AGX_MESA_WIN32_SCREEN_H
#define APPLE_AGX_MESA_WIN32_SCREEN_H

#include "agx_win32_transport.h"

typedef struct _AGX_WIN32_SCREEN_OPERATIONS {
  int (*QueryDevice)(void *Context, AGX_WIN32_DEVICE_INFO *Info);
  int (*CreateClassBuffer)(
      void *Context, APPLE_AGX_U32 ClassId, APPLE_AGX_U64 Bytes,
      APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
      APPLE_AGX_U64 *Token);
} AGX_WIN32_SCREEN_OPERATIONS;

typedef enum _AGX_WIN32_SCREEN_RESULT {
  AgxWin32ScreenSuccess = 0,
  AgxWin32ScreenArgument,
  AgxWin32ScreenDeviceInfo,
  AgxWin32ScreenStaleGeneration,
  AgxWin32ScreenClass,
  AgxWin32ScreenRange,
  AgxWin32ScreenAccess,
  AgxWin32ScreenState,
  AgxWin32ScreenCallback,
} AGX_WIN32_SCREEN_RESULT;

typedef struct _AGX_WIN32_SCREEN {
  void *Context;
  AGX_WIN32_DEVICE_INFO Info;
  AGX_WIN32_WINSYS Transport;
  AGX_WIN32_SCREEN_OPERATIONS Operations;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_BOOL Active;
} AGX_WIN32_SCREEN;

typedef struct _AGX_WIN32_SCREEN_BUFFER {
  AGX_WIN32_BUFFER Transport;
  APPLE_AGX_U32 ClassId;
  APPLE_AGX_U32 Reserved;
  APPLE_AGX_U64 Alignment;
} AGX_WIN32_SCREEN_BUFFER;

int AgxWin32DeviceInfoValid(const AGX_WIN32_DEVICE_INFO *Info);
AGX_WIN32_SCREEN_RESULT AgxWin32ScreenInitialize(
    AGX_WIN32_SCREEN *Screen, void *Context, APPLE_AGX_U32 Generation,
    const AGX_WIN32_WINSYS_OPERATIONS *TransportOperations,
    const AGX_WIN32_SCREEN_OPERATIONS *ScreenOperations);
AGX_WIN32_SCREEN_RESULT AgxWin32ScreenCreateBuffer(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 ClassId,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    AGX_WIN32_SCREEN_BUFFER *Buffer);
AGX_WIN32_SCREEN_RESULT AgxWin32ScreenMapBuffer(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_SCREEN_BUFFER *Buffer,
    APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes, APPLE_AGX_U32 Access,
    void **Address);
AGX_WIN32_SCREEN_RESULT AgxWin32ScreenUnmapBuffer(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_SCREEN_BUFFER *Buffer);
AGX_WIN32_SCREEN_RESULT AgxWin32ScreenDestroyBuffer(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_SCREEN_BUFFER *Buffer);
AGX_WIN32_SCREEN_RESULT AgxWin32ScreenWaitFence(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 TimeoutMs);
AGX_WIN32_SCREEN_RESULT AgxWin32ScreenInvalidate(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 NewGeneration);

#endif
