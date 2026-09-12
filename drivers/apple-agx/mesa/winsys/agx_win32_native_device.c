#include "agx_win32_native_device.h"

#include <string.h>

static AGX_WIN32_NATIVE_DEVICE_RESULT screen_result(
    AGX_WIN32_NATIVE_BO_RESULT result) {
  switch (result) {
  case AgxWin32NativeBoSuccess: return AgxWin32NativeDeviceSuccess;
  case AgxWin32NativeBoRange: return AgxWin32NativeDeviceRange;
  case AgxWin32NativeBoCallback: return AgxWin32NativeDeviceCallback;
  default: return AgxWin32NativeDeviceState;
  }
}

static AGX_WIN32_NATIVE_DEVICE_RESULT construction_result(
    AGX_WIN32_CONSTRUCTION_RESULT result) {
  switch (result) {
  case AgxWin32ConstructionSuccess: return AgxWin32NativeDeviceSuccess;
  case AgxWin32ConstructionRange: return AgxWin32NativeDeviceRange;
  case AgxWin32ConstructionStale: return AgxWin32NativeDeviceStale;
  default: return AgxWin32NativeDeviceState;
  }
}

AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceInitialize(
    AGX_WIN32_NATIVE_DEVICE *Device, AGX_WIN32_SCREEN *Screen,
    APPLE_AGX_U64 ConstructionBase, APPLE_AGX_U32 Generation) {
  if (Device == NULL || Screen == NULL || !Screen->Active || Generation == 0u ||
      Generation != Screen->Generation)
    return AgxWin32NativeDeviceArgument;
  memset(Device, 0, sizeof(*Device));
  if (AgxWin32ConstructionInitialize(&Device->Construction, ConstructionBase,
                                     Generation) != AgxWin32ConstructionSuccess)
    return AgxWin32NativeDeviceArgument;
  Device->Screen = Screen;
  Device->Generation = Generation;
  Device->Active = APPLE_AGX_TRUE;
  return AgxWin32NativeDeviceSuccess;
}

AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceCreateBo(
    AGX_WIN32_NATIVE_DEVICE *Device, APPLE_AGX_U32 ClassId,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    AGX_WIN32_NATIVE_BO *Bo) {
  APPLE_AGX_U64 serial;
  APPLE_AGX_U64 address = 0ULL;
  AGX_WIN32_NATIVE_BO_RESULT created;
  AGX_WIN32_CONSTRUCTION_RESULT reserved;
  if (Device == NULL || Bo == NULL || !Device->Active ||
      Device->Screen == NULL || !Device->Screen->Active ||
      Device->Generation != Device->Screen->Generation || Bo->Live ||
      Device->NextSerial == ~0ULL)
    return AgxWin32NativeDeviceState;
  serial = Device->NextSerial + 1ULL;
  created = AgxWin32NativeBoCreate(Device->Screen, ClassId, Bytes, Alignment,
                                   Flags, Bo);
  if (created != AgxWin32NativeBoSuccess)
    return screen_result(created);
  reserved = AgxWin32ConstructionReserve(&Device->Construction,
                                         Bo->Buffer.Transport.Token, serial,
                                         Bo->Bytes, &address);
  if (reserved != AgxWin32ConstructionSuccess) {
    (void)AgxWin32NativeBoDestroy(Device->Screen, Bo);
    return construction_result(reserved);
  }
  Device->NextSerial = serial;
  Bo->ConstructionSerial = serial;
  Bo->ConstructionAddress = address;
  return AgxWin32NativeDeviceSuccess;
}

AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceResolveBo(
    const AGX_WIN32_NATIVE_DEVICE *Device, const AGX_WIN32_NATIVE_BO *Bo,
    APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes, APPLE_AGX_U64 *Address) {
  if (Address != NULL)
    *Address = 0ULL;
  if (Device == NULL || Bo == NULL || Address == NULL || !Device->Active ||
      !Bo->Live || Bo->Generation != Device->Generation ||
      Bo->ConstructionSerial == 0ULL || Bo->ConstructionAddress == 0ULL)
    return AgxWin32NativeDeviceStale;
  return construction_result(AgxWin32ConstructionResolve(
      &Device->Construction, Bo->Buffer.Transport.Token, Bo->ConstructionSerial,
      Offset, Bytes, Address));
}

AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceDestroyBo(
    AGX_WIN32_NATIVE_DEVICE *Device, AGX_WIN32_NATIVE_BO *Bo) {
  AGX_WIN32_NATIVE_BO_RESULT destroyed;
  AGX_WIN32_CONSTRUCTION_RESULT released;
  APPLE_AGX_U64 ignored = 0ULL;
  APPLE_AGX_U64 token;
  APPLE_AGX_U64 serial;
  if (Device == NULL || Bo == NULL || !Device->Active || !Bo->Live ||
      Bo->Generation != Device->Generation || Bo->ConstructionSerial == 0ULL ||
      AgxWin32ConstructionResolve(&Device->Construction,
                                  Bo->Buffer.Transport.Token,
                                  Bo->ConstructionSerial, 0ULL, Bo->Bytes,
                                  &ignored) != AgxWin32ConstructionSuccess)
    return AgxWin32NativeDeviceStale;
  token = Bo->Buffer.Transport.Token;
  serial = Bo->ConstructionSerial;
  destroyed = AgxWin32NativeBoDestroy(Device->Screen, Bo);
  if (destroyed != AgxWin32NativeBoSuccess)
    return screen_result(destroyed);
  released = AgxWin32ConstructionRelease(&Device->Construction, token, serial);
  return construction_result(released);
}
