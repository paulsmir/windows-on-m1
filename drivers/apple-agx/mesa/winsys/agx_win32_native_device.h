#ifndef AGX_WIN32_NATIVE_DEVICE_H
#define AGX_WIN32_NATIVE_DEVICE_H

#include "agx_win32_construction_address.h"
#include "agx_win32_native_bo.h"

/* Owns construction identities for Windows-backed native BOs.  Addresses here
 * are private materialization inputs, never Windows-facing GPUVAs. */
typedef struct _AGX_WIN32_NATIVE_DEVICE {
  AGX_WIN32_SCREEN *Screen;
  AGX_WIN32_CONSTRUCTION_SPACE Construction;
  APPLE_AGX_U64 NextSerial;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_BOOL Active;
} AGX_WIN32_NATIVE_DEVICE;

typedef enum _AGX_WIN32_NATIVE_DEVICE_RESULT {
  AgxWin32NativeDeviceSuccess = 0,
  AgxWin32NativeDeviceArgument,
  AgxWin32NativeDeviceState,
  AgxWin32NativeDeviceRange,
  AgxWin32NativeDeviceStale,
  AgxWin32NativeDeviceCallback
} AGX_WIN32_NATIVE_DEVICE_RESULT;

AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceInitialize(
    AGX_WIN32_NATIVE_DEVICE *Device, AGX_WIN32_SCREEN *Screen,
    APPLE_AGX_U64 ConstructionBase, APPLE_AGX_U32 Generation);
AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceCreateBo(
    AGX_WIN32_NATIVE_DEVICE *Device, APPLE_AGX_U32 ClassId,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    AGX_WIN32_NATIVE_BO *Bo);
AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceImportBo(
    AGX_WIN32_NATIVE_DEVICE *Device,
    const AGX_WIN32_SCREEN_BUFFER *Buffer,
    AGX_WIN32_NATIVE_BO *Bo);
AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceResolveBo(
    const AGX_WIN32_NATIVE_DEVICE *Device, const AGX_WIN32_NATIVE_BO *Bo,
    APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes, APPLE_AGX_U64 *Address);
AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceDestroyBo(
    AGX_WIN32_NATIVE_DEVICE *Device, AGX_WIN32_NATIVE_BO *Bo);

#endif
