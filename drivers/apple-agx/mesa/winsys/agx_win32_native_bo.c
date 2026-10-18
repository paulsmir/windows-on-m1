#include "agx_win32_native_bo.h"

#include <string.h>

static AGX_WIN32_NATIVE_BO_RESULT result(AGX_WIN32_SCREEN_RESULT value) {
  switch (value) {
  case AgxWin32ScreenSuccess: return AgxWin32NativeBoSuccess;
  case AgxWin32ScreenRange: return AgxWin32NativeBoRange;
  case AgxWin32ScreenAccess: return AgxWin32NativeBoAccess;
  case AgxWin32ScreenCallback: return AgxWin32NativeBoCallback;
  default: return AgxWin32NativeBoState;
  }
}

AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoCreate(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 ClassId, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    AGX_WIN32_NATIVE_BO *Bo) {
  AGX_WIN32_SCREEN_BUFFER buffer;
  AGX_WIN32_SCREEN_RESULT status;
  if (Bo == NULL || Bo->Live || Bytes == 0ULL)
    return AgxWin32NativeBoArgument;
  memset(&buffer, 0, sizeof(buffer));
  status = AgxWin32ScreenCreateBuffer(Screen, ClassId, Bytes, Alignment,
                                      Flags, &buffer);
  if (status != AgxWin32ScreenSuccess)
    return result(status);
  memset(Bo, 0, sizeof(*Bo));
  Bo->Buffer = buffer;
  Bo->Bytes = Bytes;
  Bo->Generation = buffer.Transport.Generation;
  Bo->Flags = Flags;
  Bo->Live = APPLE_AGX_TRUE;
  return AgxWin32NativeBoSuccess;
}

AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoMap(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_NATIVE_BO *Bo,
    APPLE_AGX_U32 Access, void **Address) {
  AGX_WIN32_SCREEN_RESULT status;
  void *address = NULL;
  if (Address != NULL)
    *Address = NULL;
  if (Bo == NULL || Address == NULL || !Bo->Live || Bo->CpuAddress != NULL ||
      Bo->Generation != Bo->Buffer.Transport.Generation)
    return AgxWin32NativeBoState;
  status = AgxWin32ScreenMapBuffer(Screen, &Bo->Buffer, 0ULL, Bo->Bytes,
                                   Access, &address);
  if (status != AgxWin32ScreenSuccess)
    return result(status);
  Bo->CpuAddress = address;
  *Address = address;
  return AgxWin32NativeBoSuccess;
}

AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoUnmap(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_NATIVE_BO *Bo) {
  AGX_WIN32_SCREEN_RESULT status;
  if (Bo == NULL || !Bo->Live || Bo->CpuAddress == NULL)
    return AgxWin32NativeBoState;
  status = AgxWin32ScreenUnmapBuffer(Screen, &Bo->Buffer);
  if (status != AgxWin32ScreenSuccess)
    return result(status);
  Bo->CpuAddress = NULL;
  return AgxWin32NativeBoSuccess;
}

AGX_WIN32_NATIVE_BO_RESULT AgxWin32NativeBoDestroy(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_NATIVE_BO *Bo) {
  AGX_WIN32_SCREEN_RESULT status;
  if (Bo == NULL || !Bo->Live || Bo->CpuAddress != NULL)
    return AgxWin32NativeBoState;
  status = AgxWin32ScreenDestroyBuffer(Screen, &Bo->Buffer);
  if (status != AgxWin32ScreenSuccess)
    return result(status);
  memset(Bo, 0, sizeof(*Bo));
  return AgxWin32NativeBoSuccess;
}
