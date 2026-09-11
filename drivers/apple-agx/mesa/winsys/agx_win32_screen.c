#include "agx_win32_screen.h"

#include <string.h>

#define SCREEN_ALLOWED_FLAGS 0x1fu

static int power_of_two(APPLE_AGX_U64 value) {
  return value != 0ULL && (value & (value - 1ULL)) == 0ULL;
}

static const AGX_WIN32_BUFFER_CLASS_INFO *find_class(
    const AGX_WIN32_DEVICE_INFO *Info, APPLE_AGX_U32 ClassId) {
  APPLE_AGX_U32 index;
  if (Info == NULL)
    return NULL;
  for (index = 0u; index < Info->ClassCount; ++index)
    if (Info->Classes[index].ClassId == ClassId)
      return &Info->Classes[index];
  return NULL;
}

int AgxWin32DeviceInfoValid(const AGX_WIN32_DEVICE_INFO *Info) {
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 seen = 0u;
  if (Info == NULL || Info->Magic != AGX_WIN32_DEVICE_INFO_MAGIC ||
      Info->Version != AGX_WIN32_DEVICE_INFO_VERSION ||
      Info->Bytes != sizeof(*Info) || Info->BootGeneration == 0u ||
      Info->GpuGeneration != 13u || Info->GpuVariant != AgxWin32GpuG13G ||
      Info->PageBytes != 0x4000u ||
      Info->ClassCount != AGX_WIN32_BUFFER_CLASS_COUNT)
    return 0;
  for (index = 0u; index < Info->ClassCount; ++index) {
    const AGX_WIN32_BUFFER_CLASS_INFO *entry = &Info->Classes[index];
    APPLE_AGX_U32 bit;
    if (entry->ClassId < AgxWin32BufferClassGeneral ||
        entry->ClassId > AgxWin32BufferClassEncoder)
      return 0;
    bit = 1u << (entry->ClassId - 1u);
    if ((seen & bit) != 0u || !power_of_two(entry->MinimumAlignment) ||
        entry->MinimumAlignment < Info->PageBytes ||
        entry->MaximumBytes < entry->MinimumAlignment ||
        (entry->MaximumBytes & (Info->PageBytes - 1u)) != 0ULL ||
        entry->Flags == 0u ||
        (entry->Flags & ~SCREEN_ALLOWED_FLAGS) != 0u)
      return 0;
    seen |= bit;
  }
  return seen == (1u << AGX_WIN32_BUFFER_CLASS_COUNT) - 1u;
}

static AGX_WIN32_SCREEN_RESULT translate(AGX_WIN32_WINSYS_RESULT result) {
  switch (result) {
    case AgxWin32WinsysSuccess: return AgxWin32ScreenSuccess;
    case AgxWin32WinsysStaleGeneration:
      return AgxWin32ScreenStaleGeneration;
    case AgxWin32WinsysRange: return AgxWin32ScreenRange;
    case AgxWin32WinsysAccess: return AgxWin32ScreenAccess;
    case AgxWin32WinsysState: return AgxWin32ScreenState;
    case AgxWin32WinsysCallback: return AgxWin32ScreenCallback;
    default: return AgxWin32ScreenArgument;
  }
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenInitialize(
    AGX_WIN32_SCREEN *Screen, void *Context, APPLE_AGX_U32 Generation,
    const AGX_WIN32_WINSYS_OPERATIONS *TransportOperations,
    const AGX_WIN32_SCREEN_OPERATIONS *ScreenOperations) {
  AGX_WIN32_SCREEN initialized;
  if (Screen == NULL || Context == NULL || Generation == 0u ||
      TransportOperations == NULL ||
      ScreenOperations == NULL || ScreenOperations->QueryDevice == NULL ||
      ScreenOperations->CreateClassBuffer == NULL)
    return AgxWin32ScreenArgument;
  memset(&initialized, 0, sizeof(initialized));
  if (!ScreenOperations->QueryDevice(Context, &initialized.Info) ||
      !AgxWin32DeviceInfoValid(&initialized.Info))
    return AgxWin32ScreenDeviceInfo;
  if (AgxWin32WinsysInitialize(&initialized.Transport, Context,
                               Generation,
                               TransportOperations) !=
      AgxWin32WinsysSuccess)
    return AgxWin32ScreenArgument;
  initialized.Context = Context;
  initialized.Operations = *ScreenOperations;
  initialized.Generation = Generation;
  initialized.Active = APPLE_AGX_TRUE;
  *Screen = initialized;
  return AgxWin32ScreenSuccess;
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenCreateBuffer(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 ClassId,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    AGX_WIN32_SCREEN_BUFFER *Buffer) {
  AGX_WIN32_SCREEN_BUFFER created;
  const AGX_WIN32_BUFFER_CLASS_INFO *info;
  APPLE_AGX_U64 token = 0ULL;
  if (Screen == NULL || Buffer == NULL || !Screen->Active)
    return AgxWin32ScreenArgument;
  info = find_class(&Screen->Info, ClassId);
  if (info == NULL)
    return AgxWin32ScreenClass;
  if (Bytes == 0ULL || Bytes > info->MaximumBytes ||
      (Bytes & (Screen->Info.PageBytes - 1u)) != 0ULL ||
      !power_of_two(Alignment) || Alignment < info->MinimumAlignment)
    return AgxWin32ScreenRange;
  if (Flags == 0u || (Flags & ~info->Flags) != 0u)
    return AgxWin32ScreenAccess;
  if (!Screen->Operations.CreateClassBuffer(
          Screen->Context, ClassId, Bytes, Alignment, Flags, &token) ||
      token == 0ULL)
    return AgxWin32ScreenCallback;
  memset(&created, 0, sizeof(created));
  created.Transport.Token = token;
  created.Transport.Bytes = Bytes;
  created.Transport.Generation = Screen->Generation;
  created.Transport.Flags = Flags;
  created.ClassId = ClassId;
  created.Alignment = Alignment;
  *Buffer = created;
  return AgxWin32ScreenSuccess;
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenMapBuffer(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_SCREEN_BUFFER *Buffer,
    APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes, APPLE_AGX_U32 Access,
    void **Address) {
  if (Screen == NULL || Buffer == NULL)
    return AgxWin32ScreenArgument;
  if (Buffer->Transport.Generation != Screen->Generation)
    return AgxWin32ScreenStaleGeneration;
  if (!Screen->Active)
    return AgxWin32ScreenState;
  return translate(AgxWin32WinsysMapBuffer(
      &Screen->Transport, &Buffer->Transport, Offset, Bytes, Access, Address));
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenUnmapBuffer(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_SCREEN_BUFFER *Buffer) {
  if (Screen == NULL || Buffer == NULL)
    return AgxWin32ScreenArgument;
  if (Buffer->Transport.Generation != Screen->Generation)
    return AgxWin32ScreenStaleGeneration;
  if (!Screen->Active)
    return AgxWin32ScreenState;
  return translate(AgxWin32WinsysUnmapBuffer(
      &Screen->Transport, &Buffer->Transport));
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenDestroyBuffer(
    AGX_WIN32_SCREEN *Screen, AGX_WIN32_SCREEN_BUFFER *Buffer) {
  AGX_WIN32_SCREEN_RESULT result;
  if (Screen == NULL || Buffer == NULL)
    return AgxWin32ScreenArgument;
  if (Buffer->Transport.Generation != Screen->Generation)
    return AgxWin32ScreenStaleGeneration;
  if (!Screen->Active)
    return AgxWin32ScreenState;
  result = translate(AgxWin32WinsysDestroyBuffer(
      &Screen->Transport, &Buffer->Transport));
  if (result == AgxWin32ScreenSuccess)
    memset(Buffer, 0, sizeof(*Buffer));
  return result;
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenWaitFence(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 TimeoutMs) {
  if (Screen == NULL || !Screen->Active)
    return AgxWin32ScreenState;
  return translate(AgxWin32WinsysWaitFence(
      &Screen->Transport, Fence, TimeoutMs));
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenRetireFence(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 Fence) {
  if (Screen == NULL || !Screen->Active)
    return AgxWin32ScreenState;
  return translate(AgxWin32WinsysRetireFence(&Screen->Transport, Fence));
}

AGX_WIN32_SCREEN_RESULT AgxWin32ScreenInvalidate(
    AGX_WIN32_SCREEN *Screen, APPLE_AGX_U32 NewGeneration) {
  if (Screen == NULL || !Screen->Active || NewGeneration == 0u ||
      NewGeneration == Screen->Generation)
    return AgxWin32ScreenArgument;
  Screen->Generation = NewGeneration;
  Screen->Transport.Generation = NewGeneration;
  Screen->Active = APPLE_AGX_FALSE;
  return AgxWin32ScreenSuccess;
}
