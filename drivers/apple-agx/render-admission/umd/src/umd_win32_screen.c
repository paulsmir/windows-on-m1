#include <windows.h>
#include <wingdi.h>

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)

extern "C" {
#include "umd_internal.h"
}

static ADMISSION_UMD_SCREEN_BUFFER *AdmissionUmdScreenFind(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U64 Token) {
  UINT index;
  if (Device == NULL || Token == 0ULL)
    return NULL;
  for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++index) {
    ADMISSION_UMD_SCREEN_BUFFER *buffer = &Device->ScreenBuffers[index];
    if (buffer->Active && buffer->Token == Token)
      return buffer;
  }
  return NULL;
}

static ADMISSION_UMD_SCREEN_BUFFER *AdmissionUmdScreenFreeSlot(
    ADMISSION_UMD_DEVICE *Device) {
  UINT index;
  if (Device == NULL)
    return NULL;
  for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++index)
    if (!Device->ScreenBuffers[index].Active)
      return &Device->ScreenBuffers[index];
  return NULL;
}

static ADMISSION_UMD_SOURCE_HOLD_RECORD *AdmissionUmdScreenFreeHold(
    ADMISSION_UMD_DEVICE *Device) {
  UINT index;
  if (Device == NULL)
    return NULL;
  for (index = 0u; index < ADMISSION_UMD_SOURCE_HOLD_LIMIT; ++index)
    if (!Device->SourceHolds[index].Active)
      return &Device->SourceHolds[index];
  return NULL;
}

static ADMISSION_UMD_SOURCE_HOLD_RECORD *AdmissionUmdScreenFindHold(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U64 HoldId) {
  UINT index;
  if (Device == NULL || HoldId == 0ULL)
    return NULL;
  for (index = 0u; index < ADMISSION_UMD_SOURCE_HOLD_LIMIT; ++index) {
    ADMISSION_UMD_SOURCE_HOLD_RECORD *record = &Device->SourceHolds[index];
    if (record->Active && record->HoldId == HoldId)
      return record;
  }
  return NULL;
}

static HRESULT AdmissionUmdScreenSourceFromBuffer(
    const ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_SCREEN_BUFFER *Buffer, APPLE_AGX_U64 Offset,
    APPLE_AGX_U64 Bytes, ADMISSION_UMD_SCREEN_SOURCE *Source) {
  if (Device == NULL || Buffer == NULL || Source == NULL ||
      !Buffer->Active || !Buffer->Mapped || Buffer->LockedBase == NULL ||
      (Buffer->LockedAccess & AppleAgxWin32BufferCpuRead) == 0u ||
      Buffer->Serial == 0ULL || Buffer->MapEpoch == 0u || Bytes == 0ULL ||
      Offset > Buffer->Bytes || Bytes > Buffer->Bytes - Offset)
    return E_INVALIDARG;
  ZeroMemory(Source, sizeof(*Source));
  Source->Token = Buffer->Token;
  Source->Serial = Buffer->Serial;
  Source->Offset = Offset;
  Source->Bytes = Bytes;
  Source->Generation = Device->Win32Generation;
  Source->MapEpoch = Buffer->MapEpoch;
  Source->HoldId = 0ULL;
  Source->Address = (PVOID)((BYTE *)Buffer->LockedBase + (SIZE_T)Offset);
  return S_OK;
}

HRESULT AdmissionUmdScreenQuerySource(ADMISSION_UMD_DEVICE *Device,
                                      APPLE_AGX_U64 Token,
                                      ADMISSION_UMD_SCREEN_SOURCE *Source) {
  ADMISSION_UMD_SCREEN_BUFFER *buffer;
  HRESULT result;
  if (Source != NULL)
    ZeroMemory(Source, sizeof(*Source));
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Source == NULL || Token == 0ULL)
    return E_INVALIDARG;
  AcquireSRWLockShared(&Device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(Device, Token);
  result = AdmissionUmdScreenSourceFromBuffer(Device, buffer, 0ULL,
                                              buffer == NULL ? 0ULL : buffer->Bytes,
                                              Source);
  ReleaseSRWLockShared(&Device->ScreenBufferLock);
  return result;
}

HRESULT AdmissionUmdScreenAcquireSource(
    ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_SCREEN_SOURCE *Expected,
    ADMISSION_UMD_SCREEN_SOURCE *Held) {
  ADMISSION_UMD_SCREEN_BUFFER *buffer;
  ADMISSION_UMD_SOURCE_HOLD_RECORD *record;
  HRESULT result;
  if (Held != NULL)
    ZeroMemory(Held, sizeof(*Held));
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Expected == NULL || Held == NULL || Expected->Token == 0ULL ||
      Expected->Serial == 0ULL || Expected->Generation == 0u ||
      Expected->MapEpoch == 0u || Expected->Bytes == 0ULL)
    return E_INVALIDARG;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(Device, Expected->Token);
  record = AdmissionUmdScreenFreeHold(Device);
  if (buffer == NULL || record == NULL || buffer->Transition ||
      Expected->Generation != Device->Win32Generation ||
      buffer->Serial != Expected->Serial || buffer->MapEpoch != Expected->MapEpoch)
    result = E_FAIL;
  else
    result = AdmissionUmdScreenSourceFromBuffer(Device, buffer,
                                                Expected->Offset,
                                                Expected->Bytes, Held);
  if (SUCCEEDED(result)) {
    APPLE_AGX_U64 holdId = ++Device->NextSourceHoldId;
    if (holdId == 0ULL)
      holdId = ++Device->NextSourceHoldId;
    if (holdId == 0ULL) {
      result = E_OUTOFMEMORY;
      ZeroMemory(Held, sizeof(*Held));
    } else {
      ZeroMemory(record, sizeof(*record));
      record->HoldId = holdId;
      record->Token = Held->Token;
      record->Serial = Held->Serial;
      record->Offset = Held->Offset;
      record->Bytes = Held->Bytes;
      record->Generation = Held->Generation;
      record->MapEpoch = Held->MapEpoch;
      record->Active = TRUE;
      Held->HoldId = holdId;
    }
  }
  if (SUCCEEDED(result))
    ++buffer->SourceHolds;
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  return result;
}

HRESULT AdmissionUmdScreenReleaseSource(
    ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_SCREEN_SOURCE *Held) {
  ADMISSION_UMD_SCREEN_BUFFER *buffer;
  ADMISSION_UMD_SOURCE_HOLD_RECORD *record;
  HRESULT result = E_INVALIDARG;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Held == NULL || Held->Token == 0ULL || Held->Serial == 0ULL ||
      Held->Generation == 0u || Held->MapEpoch == 0u || Held->Bytes == 0ULL ||
      Held->HoldId == 0ULL)
    return E_INVALIDARG;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(Device, Held->Token);
  record = AdmissionUmdScreenFindHold(Device, Held->HoldId);
  if (buffer != NULL && record != NULL && Held->Generation == Device->Win32Generation &&
      buffer->Serial == Held->Serial && buffer->MapEpoch == Held->MapEpoch &&
      buffer->SourceHolds != 0u && record->Token == Held->Token &&
      record->Serial == Held->Serial && record->Generation == Held->Generation &&
      record->MapEpoch == Held->MapEpoch && record->Offset == Held->Offset &&
      record->Bytes == Held->Bytes) {
    --buffer->SourceHolds;
    ZeroMemory(record, sizeof(*record));
    result = S_OK;
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  return result;
}

static ADMISSION_UMD_SCREEN_FENCE *AdmissionUmdScreenFenceFind(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U32 Token) {
  UINT index;
  if (Device == NULL || Token == 0u)
    return NULL;
  for (index = 0u; index < ADMISSION_UMD_SCREEN_FENCE_LIMIT; ++index) {
    ADMISSION_UMD_SCREEN_FENCE *fence = &Device->ScreenFences[index];
    if (fence->Active && fence->Token == Token)
      return fence;
  }
  return NULL;
}

static ADMISSION_UMD_SCREEN_FENCE *AdmissionUmdScreenFenceFreeSlot(
    ADMISSION_UMD_DEVICE *Device) {
  UINT index;
  if (Device == NULL)
    return NULL;
  for (index = 0u; index < ADMISSION_UMD_SCREEN_FENCE_LIMIT; ++index)
    if (!Device->ScreenFences[index].Active)
      return &Device->ScreenFences[index];
  return NULL;
}

static const AGX_WIN32_BUFFER_CLASS_INFO *AdmissionUmdScreenClass(
    const ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U32 ClassId) {
  APPLE_AGX_U32 index;
  if (Device == NULL || Device->Adapter == NULL)
    return NULL;
  for (index = 0u; index < Device->Adapter->DeviceInfo.ClassCount; ++index)
    if (Device->Adapter->DeviceInfo.Classes[index].ClassId == ClassId)
      return &Device->Adapter->DeviceInfo.Classes[index];
  return NULL;
}

static int AdmissionUmdScreenQueryDevice(void *Context,
                                         AGX_WIN32_DEVICE_INFO *Info) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  if (device == NULL || device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      device->Adapter == NULL || Info == NULL ||
      !AgxWin32DeviceInfoValid(&device->Adapter->DeviceInfo))
    return 0;
  *Info = device->Adapter->DeviceInfo;
  return 1;
}

static int AdmissionUmdScreenCreateClassBuffer(
    void *Context, APPLE_AGX_U32 ClassId, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    APPLE_AGX_U64 *Token) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  ADMISSION_UMD_SCREEN_BUFFER *slot;
  const AGX_WIN32_BUFFER_CLASS_INFO *classInfo;
  ADMISSION_WIN32_ALLOCATION_CREATE description;
  D3DDDI_ALLOCATIONINFO allocationInfo;
  D3DDDICB_ALLOCATE allocate;
  APPLE_AGX_U64 token;
  HRESULT result;

  if (device == NULL || device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnAllocateCb == NULL || Token == NULL ||
      Bytes == 0ULL || Bytes > MAXUINT32)
    return 0;
  classInfo = AdmissionUmdScreenClass(device, ClassId);
  if (classInfo == NULL || Bytes > classInfo->MaximumBytes ||
      Alignment < classInfo->MinimumAlignment ||
      (Flags & ~classInfo->Flags) != 0u || Flags == 0u)
    return 0;
  slot = AdmissionUmdScreenFreeSlot(device);
  if (slot == NULL || device->NextScreenToken == ~0ULL)
    return 0;
  ZeroMemory(&description, sizeof(description));
  if (!AdmissionAllocationDescribe(
          (UINT)Bytes, 1u, 1u,
          (UINT)D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE,
          (UINT)D3DDDIFMT_A8, 1u, &description.Allocation))
    return 0;

  description.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  description.Version = ADMISSION_WIN32_ALLOCATION_VERSION;
  description.Bytes = sizeof(description);
  description.ClassId = ClassId;
  description.Flags = Flags;

  ZeroMemory(&allocationInfo, sizeof(allocationInfo));
  allocationInfo.pPrivateDriverData = &description;
  allocationInfo.PrivateDriverDataSize = sizeof(description);
  allocationInfo.VidPnSourceId = 0u;
  ZeroMemory(&allocate, sizeof(allocate));
  allocate.hResource = NULL;
  allocate.NumAllocations = 1u;
  allocate.pAllocationInfo = &allocationInfo;
  result = device->KernelCallbacks->pfnAllocateCb(
      device->RuntimeDevice.handle, &allocate);
  if (FAILED(result) || allocationInfo.hAllocation == 0u) {
    device->LastScreenError = FAILED(result) ? result : E_FAIL;
    return 0;
  }

  token = ++device->NextScreenToken;
  if (token == 0ULL)
    token = ++device->NextScreenToken;
  ZeroMemory(slot, sizeof(*slot));
  slot->Token = token;
  slot->Serial = ++device->NextScreenSerial;
  if (slot->Serial == 0ULL)
    slot->Serial = ++device->NextScreenSerial;
  slot->KernelAllocation = allocationInfo.hAllocation;
  slot->Bytes = Bytes;
  slot->Alignment = Alignment;
  slot->ClassId = ClassId;
  slot->Flags = Flags;
  slot->Active = TRUE;
  device->LastScreenError = S_OK;
  *Token = token;
  return 1;
}

static int AdmissionUmdScreenCreateBuffer(void *Context,
                                           APPLE_AGX_U64 Bytes,
                                           APPLE_AGX_U32 Flags,
                                           APPLE_AGX_U64 *Token) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  const AGX_WIN32_BUFFER_CLASS_INFO *classInfo =
      AdmissionUmdScreenClass(device, AgxWin32BufferClassGeneral);
  return classInfo != NULL &&
         AdmissionUmdScreenCreateClassBuffer(
             Context, AgxWin32BufferClassGeneral, Bytes,
             classInfo->MinimumAlignment, Flags, Token);
}

static int AdmissionUmdScreenMapBuffer(void *Context, APPLE_AGX_U64 Token,
                                        APPLE_AGX_U64 Offset,
                                        APPLE_AGX_U64 Bytes,
                                        APPLE_AGX_U32 Access,
                                        void **Address) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  ADMISSION_UMD_SCREEN_BUFFER *buffer =
      AdmissionUmdScreenFind(device, Token);
  D3DDDICB_LOCK lock;
  HRESULT result;
  if (buffer == NULL || Address == NULL || buffer->Mapped || Bytes == 0ULL ||
      Offset > buffer->Bytes || Bytes > buffer->Bytes - Offset ||
      (Access & (AppleAgxWin32BufferCpuRead |
                 AppleAgxWin32BufferCpuWrite)) == 0u ||
      (Access & ~(AppleAgxWin32BufferCpuRead |
                  AppleAgxWin32BufferCpuWrite)) != 0u ||
      (Access & buffer->Flags) != Access ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnLockCb == NULL)
    return 0;
  ZeroMemory(&lock, sizeof(lock));
  lock.hAllocation = buffer->KernelAllocation;
  lock.Flags.LockEntire = 1u;
  if ((Access & AppleAgxWin32BufferCpuWrite) == 0u)
    lock.Flags.ReadOnly = 1u;
  else if ((Access & AppleAgxWin32BufferCpuRead) == 0u)
    lock.Flags.WriteOnly = 1u;
  result = device->KernelCallbacks->pfnLockCb(
      device->RuntimeDevice.handle, &lock);
  if (FAILED(result) || lock.pData == NULL) {
    device->LastScreenError = FAILED(result) ? result : E_FAIL;
    return 0;
  }
  buffer->LockedBase = lock.pData;
  buffer->LockedAccess = Access;
  ++buffer->MapEpoch;
  if (buffer->MapEpoch == 0u)
    ++buffer->MapEpoch;
  buffer->Mapped = TRUE;
  device->LastScreenError = S_OK;
  *Address = (PVOID)((BYTE *)lock.pData + (SIZE_T)Offset);
  return 1;
}

static int AdmissionUmdScreenUnmapBuffer(void *Context,
                                          APPLE_AGX_U64 Token) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  ADMISSION_UMD_SCREEN_BUFFER *buffer =
      AdmissionUmdScreenFind(device, Token);
  D3DDDICB_UNLOCK unlock;
  D3DKMT_HANDLE allocation;
  HRESULT result;
  if (device == NULL || device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnUnlockCb == NULL)
    return 0;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(device, Token);
  if (buffer == NULL || !buffer->Mapped || buffer->Transition ||
      buffer->SourceHolds != 0u) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
  allocation = buffer->KernelAllocation;
  buffer->Transition = TRUE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  ZeroMemory(&unlock, sizeof(unlock));
  unlock.NumAllocations = 1u;
  unlock.phAllocations = &allocation;
  result = device->KernelCallbacks->pfnUnlockCb(
      device->RuntimeDevice.handle, &unlock);
  if (FAILED(result)) {
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    buffer = AdmissionUmdScreenFind(device, Token);
    if (buffer != NULL)
      buffer->Transition = FALSE;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = result;
    return 0;
  }
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(device, Token);
  if (buffer == NULL || buffer->KernelAllocation != allocation) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = E_FAIL;
    return 0;
  }
  buffer->LockedBase = NULL;
  buffer->LockedAccess = 0u;
  buffer->Mapped = FALSE;
  buffer->Transition = FALSE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  device->LastScreenError = S_OK;
  return 1;
}

static int AdmissionUmdScreenDestroyBuffer(void *Context,
                                            APPLE_AGX_U64 Token) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  ADMISSION_UMD_SCREEN_BUFFER *buffer =
      AdmissionUmdScreenFind(device, Token);
  D3DDDICB_DEALLOCATE deallocate;
  D3DKMT_HANDLE allocation;
  HRESULT result;
  if (device == NULL || device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnDeallocateCb == NULL)
    return 0;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(device, Token);
  if (buffer == NULL || buffer->Mapped || buffer->Transition ||
      buffer->SourceHolds != 0u) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
  allocation = buffer->KernelAllocation;
  buffer->Transition = TRUE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  ZeroMemory(&deallocate, sizeof(deallocate));
  deallocate.NumAllocations = 1u;
  deallocate.HandleList = &allocation;
  result = device->KernelCallbacks->pfnDeallocateCb(
      device->RuntimeDevice.handle, &deallocate);
  if (FAILED(result)) {
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    buffer = AdmissionUmdScreenFind(device, Token);
    if (buffer != NULL)
      buffer->Transition = FALSE;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = result;
    return 0;
  }
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(device, Token);
  if (buffer == NULL || buffer->KernelAllocation != allocation) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = E_FAIL;
    return 0;
  }
  ZeroMemory(buffer, sizeof(*buffer));
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  device->LastScreenError = S_OK;
  return 1;
}

static int AdmissionUmdScreenSubmitClear(
    void *Context, const AGX_WIN32_CLEAR_REQUEST *Request,
    APPLE_AGX_U32 *Fence) {
  UNREFERENCED_PARAMETER(Context);
  UNREFERENCED_PARAMETER(Request);
  UNREFERENCED_PARAMETER(Fence);
  return 0;
}

static int AdmissionUmdScreenWaitFence(void *Context, APPLE_AGX_U32 Fence,
                                        APPLE_AGX_U32 TimeoutMs) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  ADMISSION_UMD_SCREEN_FENCE *fence =
      AdmissionUmdScreenFenceFind(device, Fence);
  DWORD waitResult;
  if (fence == NULL || fence->Event == NULL)
    return 0;
  if (fence->Completed)
    return 1;
  waitResult = WaitForSingleObject(fence->Event, TimeoutMs);
  if (waitResult == WAIT_OBJECT_0) {
    fence->Completed = TRUE;
    device->LastScreenError = S_OK;
    return 1;
  }
  device->LastScreenError =
      waitResult == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                                 : HRESULT_FROM_WIN32(GetLastError());
  return 0;
}

static int AdmissionUmdScreenRetireFence(void *Context,
                                          APPLE_AGX_U32 Fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  ADMISSION_UMD_SCREEN_FENCE *fence =
      AdmissionUmdScreenFenceFind(device, Fence);
  if (fence == NULL || fence->Event == NULL)
    return 0;
  if (!CloseHandle(fence->Event)) {
    device->LastScreenError = HRESULT_FROM_WIN32(GetLastError());
    return 0;
  }
  ZeroMemory(fence, sizeof(*fence));
  device->LastScreenError = S_OK;
  return 1;
}

HRESULT AdmissionUmdScreenInitialize(ADMISSION_UMD_DEVICE *Device) {
  AGX_WIN32_WINSYS_OPERATIONS transport;
  AGX_WIN32_SCREEN_OPERATIONS screen;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC)
    return E_INVALIDARG;
  ZeroMemory(&transport, sizeof(transport));
  transport.CreateBuffer = AdmissionUmdScreenCreateBuffer;
  transport.MapBuffer = AdmissionUmdScreenMapBuffer;
  transport.UnmapBuffer = AdmissionUmdScreenUnmapBuffer;
  transport.DestroyBuffer = AdmissionUmdScreenDestroyBuffer;
  transport.SubmitClear = AdmissionUmdScreenSubmitClear;
  transport.WaitFence = AdmissionUmdScreenWaitFence;
  transport.RetireFence = AdmissionUmdScreenRetireFence;
  ZeroMemory(&screen, sizeof(screen));
  screen.QueryDevice = AdmissionUmdScreenQueryDevice;
  screen.CreateClassBuffer = AdmissionUmdScreenCreateClassBuffer;
  Device->NextScreenToken = 0ULL;
  Device->NextScreenSerial = 0ULL;
  InitializeSRWLock(&Device->ScreenBufferLock);
  Device->NextScreenFence = 0u;
  Device->LastScreenError = S_OK;
  return AgxWin32ScreenInitialize(
             &Device->Screen, Device, Device->Win32Generation,
             &transport, &screen) == AgxWin32ScreenSuccess
             ? S_OK
             : E_FAIL;
}

HRESULT AdmissionUmdScreenSignalFence(ADMISSION_UMD_DEVICE *Device,
                                      APPLE_AGX_U32 *Fence) {
  ADMISSION_UMD_SCREEN_FENCE *slot;
  D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 signal;
  APPLE_AGX_U32 token;
  HANDLE eventHandle;
  HRESULT result;
  if (Fence == NULL)
    return E_INVALIDARG;
  *Fence = 0u;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Device->KernelContext == NULL ||
      Device->KernelCallbacks == NULL ||
      Device->KernelCallbacks->pfnSignalSynchronizationObject2Cb == NULL)
    return E_INVALIDARG;
  slot = AdmissionUmdScreenFenceFreeSlot(Device);
  if (slot == NULL || Device->NextScreenFence == MAXUINT32)
    return E_OUTOFMEMORY;
  token = ++Device->NextScreenFence;
  eventHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (eventHandle == NULL)
    return HRESULT_FROM_WIN32(GetLastError());
  ZeroMemory(&signal, sizeof(signal));
  signal.hContext = Device->KernelContext;
  signal.ObjectCount = 0u;
  signal.Flags.EnqueueCpuEvent = 1u;
  signal.CpuEventHandle = eventHandle;
  result = Device->KernelCallbacks->pfnSignalSynchronizationObject2Cb(
      Device->RuntimeDevice.handle, &signal);
  if (FAILED(result)) {
    (void)CloseHandle(eventHandle);
    Device->LastScreenError = result;
    return result;
  }
  ZeroMemory(slot, sizeof(*slot));
  slot->Event = eventHandle;
  slot->Token = token;
  slot->Active = TRUE;
  Device->LastScreenError = S_OK;
  *Fence = token;
  return S_OK;
}

HRESULT AdmissionUmdScreenFinalize(ADMISSION_UMD_DEVICE *Device,
                                   ULONG *Undeallocated) {
  HRESULT firstError = S_OK;
  ULONG undeallocated = 0u;
  UINT index;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Undeallocated == NULL)
    return E_INVALIDARG;
  for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++index) {
    ADMISSION_UMD_SCREEN_BUFFER *buffer = &Device->ScreenBuffers[index];
    if (!buffer->Active)
      continue;
    if (buffer->Mapped &&
        !AdmissionUmdScreenUnmapBuffer(Device, buffer->Token)) {
      if (SUCCEEDED(firstError))
        firstError = FAILED(Device->LastScreenError)
                         ? Device->LastScreenError : E_FAIL;
      ++undeallocated;
      continue;
    }
    if (!AdmissionUmdScreenDestroyBuffer(Device, buffer->Token)) {
      if (SUCCEEDED(firstError))
        firstError = FAILED(Device->LastScreenError)
                         ? Device->LastScreenError : E_FAIL;
      ++undeallocated;
    }
  }
  for (index = 0u; index < ADMISSION_UMD_SCREEN_FENCE_LIMIT; ++index) {
    ADMISSION_UMD_SCREEN_FENCE *fence = &Device->ScreenFences[index];
    if (!fence->Active)
      continue;
    if (!AdmissionUmdScreenRetireFence(Device, fence->Token)) {
      if (SUCCEEDED(firstError))
        firstError = FAILED(Device->LastScreenError)
                         ? Device->LastScreenError : E_FAIL;
      ++undeallocated;
    }
  }
  if (Device->Screen.Active)
    (void)AgxWin32ScreenInvalidate(
        &Device->Screen, Device->Win32Generation + 1u != 0u
                             ? Device->Win32Generation + 1u : 1u);
  *Undeallocated = undeallocated;
  return firstError;
}
