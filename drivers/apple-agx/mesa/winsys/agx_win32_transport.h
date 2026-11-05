#ifndef APPLE_AGX_MESA_WIN32_TRANSPORT_H
#define APPLE_AGX_MESA_WIN32_TRANSPORT_H

#include "apple_agx_win32_abi.h"
#include "apple_agx_win32_device_info.h"

typedef struct _AGX_WIN32_CLEAR_REQUEST {
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 AllocationIndex;
  APPLE_AGX_U64 AllocationOffset;
  APPLE_AGX_U64 AllocationBytes;
  APPLE_AGX_U32 Format;
  APPLE_AGX_U32 Color;
  APPLE_AGX_U32 SurfaceWidth;
  APPLE_AGX_U32 SurfaceHeight;
  APPLE_AGX_U32 SurfacePitch;
  APPLE_AGX_U32 Left;
  APPLE_AGX_U32 Top;
  APPLE_AGX_U32 Right;
  APPLE_AGX_U32 Bottom;
} AGX_WIN32_CLEAR_REQUEST;

typedef struct _AGX_WIN32_DRAW_REQUEST {
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 AllocationCount;
  APPLE_AGX_U32 ReferenceCount;
  APPLE_AGX_U32 RelocationCount;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *References;
  const APPLE_AGX_WIN32_RELOCATION *Relocations;
  APPLE_AGX_WIN32_DRAW_PAYLOAD Draw;
} AGX_WIN32_DRAW_REQUEST;

typedef enum _AGX_WIN32_WINSYS_RESULT {
  AgxWin32WinsysSuccess = 0,
  AgxWin32WinsysArgument,
  AgxWin32WinsysStaleGeneration,
  AgxWin32WinsysRange,
  AgxWin32WinsysAccess,
  AgxWin32WinsysState,
  AgxWin32WinsysCallback,
} AGX_WIN32_WINSYS_RESULT;

typedef struct _AGX_WIN32_BUFFER {
  APPLE_AGX_U64 Token;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_BOOL Mapped;
} AGX_WIN32_BUFFER;

typedef struct _AGX_WIN32_WINSYS_OPERATIONS {
  int (*CreateBuffer)(void *Context, APPLE_AGX_U64 Bytes,
                      APPLE_AGX_U32 Flags, APPLE_AGX_U64 *Token);
  int (*MapBuffer)(void *Context, APPLE_AGX_U64 Token,
                   APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes,
                   APPLE_AGX_U32 Access, void **Address);
  int (*UnmapBuffer)(void *Context, APPLE_AGX_U64 Token);
  int (*DestroyBuffer)(void *Context, APPLE_AGX_U64 Token);
  int (*SubmitClear)(void *Context, const AGX_WIN32_CLEAR_REQUEST *Request,
                     APPLE_AGX_U32 *Fence);
  int (*WaitFence)(void *Context, APPLE_AGX_U32 Fence,
                   APPLE_AGX_U32 TimeoutMs);
  int (*RetireFence)(void *Context, APPLE_AGX_U32 Fence);
} AGX_WIN32_WINSYS_OPERATIONS;

typedef struct _AGX_WIN32_WINSYS {
  void *Context;
  APPLE_AGX_U32 Generation;
  AGX_WIN32_WINSYS_OPERATIONS Operations;
} AGX_WIN32_WINSYS;

APPLE_AGX_WIN32_ABI_RESULT AgxWin32TransportBuildClear(
    const AGX_WIN32_CLEAR_REQUEST *Request, void *CommandBuffer,
    APPLE_AGX_U32 CommandCapacity, APPLE_AGX_U32 *CommandBytes);
APPLE_AGX_WIN32_ABI_RESULT AgxWin32TransportBuildDraw(
    const AGX_WIN32_DRAW_REQUEST *Request, void *CommandBuffer,
    APPLE_AGX_U32 CommandCapacity, APPLE_AGX_U32 *CommandBytes);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysInitialize(
    AGX_WIN32_WINSYS *Winsys, void *Context, APPLE_AGX_U32 Generation,
    const AGX_WIN32_WINSYS_OPERATIONS *Operations);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysCreateBuffer(
    AGX_WIN32_WINSYS *Winsys, APPLE_AGX_U64 Bytes, APPLE_AGX_U32 Flags,
    AGX_WIN32_BUFFER *Buffer);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysMapBuffer(
    AGX_WIN32_WINSYS *Winsys, AGX_WIN32_BUFFER *Buffer,
    APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes, APPLE_AGX_U32 Access,
    void **Address);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysUnmapBuffer(
    AGX_WIN32_WINSYS *Winsys, AGX_WIN32_BUFFER *Buffer);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysDestroyBuffer(
    AGX_WIN32_WINSYS *Winsys, AGX_WIN32_BUFFER *Buffer);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysSubmitClear(
    AGX_WIN32_WINSYS *Winsys, const AGX_WIN32_CLEAR_REQUEST *Request,
    APPLE_AGX_U32 *Fence);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysWaitFence(
    AGX_WIN32_WINSYS *Winsys, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 TimeoutMs);
AGX_WIN32_WINSYS_RESULT AgxWin32WinsysRetireFence(
    AGX_WIN32_WINSYS *Winsys, APPLE_AGX_U32 Fence);

#endif /* APPLE_AGX_MESA_WIN32_TRANSPORT_H */
