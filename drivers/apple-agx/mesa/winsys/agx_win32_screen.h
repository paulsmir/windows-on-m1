#ifndef APPLE_AGX_MESA_WIN32_SCREEN_H
#define APPLE_AGX_MESA_WIN32_SCREEN_H

#include "agx_win32_transport.h"

#define AGX_WIN32_DEVICE_INFO_MAGIC 0x49445741u /* AWDI */
#define AGX_WIN32_DEVICE_INFO_VERSION 1u
#define AGX_WIN32_BUFFER_CLASS_COUNT 3u

typedef enum _AGX_WIN32_GPU_VARIANT {
  AgxWin32GpuG13G = 1u,
} AGX_WIN32_GPU_VARIANT;

typedef enum _AGX_WIN32_BUFFER_CLASS {
  AgxWin32BufferClassGeneral = 1u,
  AgxWin32BufferClassShader = 2u,
  AgxWin32BufferClassEncoder = 3u,
} AGX_WIN32_BUFFER_CLASS;

typedef struct _AGX_WIN32_BUFFER_CLASS_INFO {
  APPLE_AGX_U32 ClassId;
  APPLE_AGX_U32 MinimumAlignment;
  APPLE_AGX_U64 MaximumBytes;
  APPLE_AGX_U32 Flags;
} AGX_WIN32_BUFFER_CLASS_INFO;

typedef struct _AGX_WIN32_DEVICE_INFO {
  APPLE_AGX_U32 Magic, Version, Bytes, Generation;
  APPLE_AGX_U32 GpuGeneration, GpuVariant, PageBytes, ClassCount;
  AGX_WIN32_BUFFER_CLASS_INFO Classes[AGX_WIN32_BUFFER_CLASS_COUNT];
} AGX_WIN32_DEVICE_INFO;

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
    AGX_WIN32_SCREEN *Screen, void *Context,
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
