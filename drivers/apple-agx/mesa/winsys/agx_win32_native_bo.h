#ifndef AGX_WIN32_NATIVE_BO_H
#define AGX_WIN32_NATIVE_BO_H

#include "agx_win32_screen.h"

typedef struct _AGX_WIN32_NATIVE_BO {
  AGX_WIN32_SCREEN_BUFFER Buffer;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 Flags;
  void *CpuAddress;
  /* Construction-only identity. This is not a GPUVA or a physical address. */
  APPLE_AGX_U64 ConstructionSerial;
  APPLE_AGX_U64 ConstructionAddress;
  APPLE_AGX_BOOL Live;
} AGX_WIN32_NATIVE_BO;

typedef enum _AGX_WIN32_NATIVE_BO_RESULT {
  AgxWin32NativeBoSuccess = 0,
  AgxWin32NativeBoArgument,
  AgxWin32NativeBoState,
  AgxWin32NativeBoRange,
  AgxWin32NativeBoAccess,
  AgxWin32NativeBoCallback
} AGX_WIN32_NATIVE_BO_RESULT;

AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoCreate(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 ClassId, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    AGX_WIN32_NATIVE_BO *Bo);
AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoMap(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_NATIVE_BO *Bo,
    APPLE_AGX_U32 Access, void **Address);
AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoUnmap(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_NATIVE_BO *Bo);
AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoDestroy(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_NATIVE_BO *Bo);

#endif
