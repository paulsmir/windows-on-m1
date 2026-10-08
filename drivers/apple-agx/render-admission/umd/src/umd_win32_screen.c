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

static DECLSPEC_ALIGN(8) volatile LONG64 NextOwnerCookie;

static D3DKMT_HANDLE AdmissionUmdScreenCpuAllocation(
    const ADMISSION_UMD_SCREEN_BUFFER *Buffer) {
#ifdef APPLE_AGX_GPUVA_WINSYS
  return Buffer->SystemDirect ? Buffer->KernelAllocation : Buffer->StagingAllocation;
#else
  return Buffer->KernelAllocation;
#endif
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
      !Buffer->Active || !Buffer->Mapped || Buffer->SubmissionHolds || Buffer->LockedBase == NULL ||
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
      Source == NULL || Token == 0ULL || Device->ScreenClosing)
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
  if (Device->ScreenClosing) {
    ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
    return HRESULT_FROM_WIN32(ERROR_BUSY);
  }
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

HRESULT AdmissionUmdScreenAssociateNativeBo(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U64 Token,
    const void *NativeBo, APPLE_AGX_U64 NativeBoSerial) {
  ADMISSION_UMD_SCREEN_BUFFER *buffer;
  HRESULT result = E_INVALIDARG;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Token == 0ULL || NativeBo == NULL || NativeBoSerial == 0ULL)
    return E_INVALIDARG;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(Device, Token);
  if (buffer != NULL && !Device->ScreenClosing && !buffer->Transition &&
      buffer->NativeBo == NULL) {
    buffer->NativeBo = NativeBo;
    buffer->NativeBoSerial = NativeBoSerial;
    result = S_OK;
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  return result;
}

HRESULT AdmissionUmdScreenQueryNativeBo(
    ADMISSION_UMD_DEVICE *Device, const void *NativeBo,
    APPLE_AGX_U64 NativeBoSerial, ADMISSION_UMD_SCREEN_SOURCE *Source) {
  UINT index;
  HRESULT result = E_INVALIDARG;
  if (Source != NULL)
    ZeroMemory(Source, sizeof(*Source));
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      NativeBo == NULL || NativeBoSerial == 0ULL || Source == NULL)
    return E_INVALIDARG;
  AcquireSRWLockShared(&Device->ScreenBufferLock);
  if (!Device->ScreenClosing)
    for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++index) {
      ADMISSION_UMD_SCREEN_BUFFER *buffer = &Device->ScreenBuffers[index];
      if (buffer->Active && !buffer->Transition &&
          buffer->NativeBo == NativeBo &&
          buffer->NativeBoSerial == NativeBoSerial) {
        result = AdmissionUmdScreenSourceFromBuffer(
            Device, buffer, 0ULL, buffer->Bytes, Source);
        break;
      }
    }
  ReleaseSRWLockShared(&Device->ScreenBufferLock);
  return result;
}

HRESULT AdmissionUmdScreenDetachNativeBo(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U64 Token,
    const void *NativeBo, APPLE_AGX_U64 NativeBoSerial) {
  ADMISSION_UMD_SCREEN_BUFFER *buffer;
  HRESULT result = E_INVALIDARG;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Token == 0ULL || NativeBo == NULL || NativeBoSerial == 0ULL)
    return E_INVALIDARG;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(Device, Token);
  if (buffer != NULL && !buffer->Transition && buffer->SourceHolds == 0u &&
      buffer->SubmissionHolds == 0u &&
      buffer->NativeBo == NativeBo &&
      buffer->NativeBoSerial == NativeBoSerial) {
    buffer->NativeBo = NULL;
    buffer->NativeBoSerial = 0ULL;
    result = S_OK;
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  return result;
}

HRESULT AdmissionUmdScreenPrepareSubmissionMaps(
    ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_DRAW_SUBMISSION *Submission) {
  ADMISSION_UMD_SCREEN_BUFFER *buffers[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  D3DKMT_HANDLE allocations[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  PVOID addresses[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  D3DDDICB_UNLOCK unlock;
  UINT index, slot, count = 0u;
  HRESULT result = E_INVALIDARG;
  if (Device == NULL || Submission == NULL ||
      Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Device->KernelCallbacks == NULL ||
      Device->KernelCallbacks->pfnUnlockCb == NULL)
    return E_INVALIDARG;

  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  if (Device->DrawSubmission != Submission ||
      Submission->Phase != AdmissionDrawSealed ||
      Submission->Count == 0u ||
      Submission->Count > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES)
    goto locked_done;

  /* Validate the whole transition before changing any slot. The captured
   * identities are immutable and Submission->Count is allocation-deduplicated. */
  for (index = 0u; index < Submission->Count; ++index) {
    const AGX_WIN32_RELOC_ALLOCATION *identity =
        &Submission->Identities[index];
    ADMISSION_UMD_SCREEN_BUFFER *buffer = NULL;
    for (slot = 0u; slot < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++slot) {
      ADMISSION_UMD_SCREEN_BUFFER *candidate = &Device->ScreenBuffers[slot];
      if (candidate->Active && candidate->Token == identity->Token) {
        buffer = candidate;
        break;
      }
    }
    if (buffer == NULL || buffer->Serial != identity->Serial ||
        buffer->Bytes != identity->Bytes || buffer->Transition ||
        buffer->KernelAllocation != Submission->Allocations[index].hAllocation)
      goto locked_done;
    if (!buffer->Mapped)
      continue;
    if (buffer->NativeBo == NULL || buffer->NativeMapRelease == NULL ||
        buffer->LockedBase == NULL ||
        !buffer->NativeMapRelease(
            buffer->NativeBo, buffer->LockedBase, FALSE))
      goto locked_done;
    buffers[count] = buffer;
    allocations[count] = AdmissionUmdScreenCpuAllocation(buffer);
    addresses[count] = buffer->LockedBase;
    ++count;
  }
  if (count == 0u) {
    result = S_OK;
    goto locked_done;
  }
  for (index = 0u; index < count; ++index)
    buffers[index]->Transition = TRUE;
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);

  ZeroMemory(&unlock, sizeof(unlock));
  unlock.NumAllocations = count;
  unlock.phAllocations = allocations;
  result = Device->KernelCallbacks->pfnUnlockCb(
      Device->RuntimeDevice.handle, &unlock);

  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  if (FAILED(result)) {
    for (index = 0u; index < count; ++index)
      buffers[index]->Transition = FALSE;
    goto locked_done;
  }
  for (index = 0u; index < count; ++index) {
    ADMISSION_UMD_SCREEN_BUFFER *buffer = buffers[index];
    if (!buffer->Active || !buffer->Transition || !buffer->Mapped ||
        AdmissionUmdScreenCpuAllocation(buffer) != allocations[index] ||
        buffer->LockedBase != addresses[index] ||
        !buffer->NativeMapRelease(
            buffer->NativeBo, addresses[index], TRUE)) {
      Device->DrawTerminal = TRUE;
      result = E_FAIL;
      break;
    }
    buffer->LockedBase = NULL;
    buffer->LockedAccess = 0u;
    buffer->Mapped = FALSE;
    buffer->Transition = FALSE;
#ifdef APPLE_AGX_GPUVA_WINSYS
    AdmissionUmdStagingInvalidate(&buffer->Sync); /* EXP995, see UnmapBuffer */
#endif
  }
  if (FAILED(result)) {
    for (; index < count; ++index)
      buffers[index]->Transition = FALSE;
  } else {
    Device->LastScreenError = S_OK;
  }

locked_done:
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  if (FAILED(result))
    Device->LastScreenError = result;
  return result;
}

BOOL AdmissionUmdScreenHasLiveSources(ADMISSION_UMD_DEVICE *Device) {
  UINT index;
  BOOL live = FALSE;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC)
    return FALSE;
  AcquireSRWLockShared(&Device->ScreenBufferLock);
  if (Device->DrawSubmission != NULL || Device->NativeBackendCount != 0u) {
    ReleaseSRWLockShared(&Device->ScreenBufferLock);
    return TRUE;
  }
  for(index=0u;index<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++index) {
    if(Device->ScreenBuffers[index].NativeBackend != NULL) {
      ReleaseSRWLockShared(&Device->ScreenBufferLock);
      return TRUE;
    }
  }
  for (index = 0u; index < ADMISSION_UMD_SOURCE_HOLD_LIMIT; ++index)
    if (Device->SourceHolds[index].Active) {
      live = TRUE;
      break;
    }
  ReleaseSRWLockShared(&Device->ScreenBufferLock);
  return live;
}

HRESULT AdmissionUmdScreenBeginClose(ADMISSION_UMD_DEVICE *Device) {
  UINT index;
  HRESULT result = S_OK;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC)
    return E_INVALIDARG;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  if (Device->ScreenClosing || Device->DrawSubmission != NULL || Device->NativeBackendCount != 0u) {
    result = HRESULT_FROM_WIN32(ERROR_BUSY);
  } else {
    for (index = 0u; index < ADMISSION_UMD_SOURCE_HOLD_LIMIT; ++index)
      if (Device->SourceHolds[index].Active) {
        result = HRESULT_FROM_WIN32(ERROR_BUSY);
        break;
      }
    if (SUCCEEDED(result))
      for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++index)
        if (Device->ScreenBuffers[index].Transition ||
            Device->ScreenBuffers[index].SubmissionHolds ||
            Device->ScreenBuffers[index].NativeBackend != NULL) {
          result = HRESULT_FROM_WIN32(ERROR_BUSY);
          break;
        }
    if (SUCCEEDED(result))
      Device->ScreenClosing = TRUE;
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  return result;
}

VOID AdmissionUmdScreenCancelClose(ADMISSION_UMD_DEVICE *Device) {
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC)
    return;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  Device->ScreenClosing = FALSE;
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
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
  if (device->ScreenClosing)
    return 0;
  *Info = device->Adapter->DeviceInfo;
  return 1;
}

static int AdmissionUmdScreenCreateClassBufferImpl(
    void *Context, APPLE_AGX_U32 ClassId, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags,
    APPLE_AGX_U64 *Token, D3DKMT_HANDLE BorrowedStaging) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  ADMISSION_UMD_SCREEN_BUFFER *slot;
  const AGX_WIN32_BUFFER_CLASS_INFO *classInfo;
  ADMISSION_WIN32_ALLOCATION_CREATE description;
  D3DDDI_ALLOCATIONINFO allocationInfo;
  D3DDDICB_ALLOCATE allocate;
  APPLE_AGX_U64 token;
  HRESULT result;
  D3DKMT_HANDLE canonical = 0, staging = BorrowedStaging;
#ifdef APPLE_AGX_GPUVA_WINSYS
  BYTE *privateStaging = NULL;
  BOOL systemDirect = FALSE;
#endif
  ADMISSION_ALLOCATION_DESCRIPTION stagingDescription;
  if (Token) *Token = 0;

  if (device == NULL || device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnAllocateCb == NULL || Token == NULL ||
      Bytes == 0ULL || Bytes > MAXUINT32)
    return 0;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  if (device->ScreenClosing) {
    ReleaseSRWLockShared(&device->ScreenBufferLock);
    return 0;
  }
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  classInfo = AdmissionUmdScreenClass(device, ClassId);
  if (classInfo == NULL || Bytes > classInfo->MaximumBytes ||
      Alignment < classInfo->MinimumAlignment ||
      (Flags & ~classInfo->Flags) != 0u || Flags == 0u)
    return 0;
  ZeroMemory(&description, sizeof(description));
  if (!AdmissionAllocationDescribe(
          (UINT)Bytes, 1u, 1u,
          (UINT)D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE,
          (UINT)D3DDDIFMT_A8, 1u, &description.Allocation))
    return 0;

  description.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  description.Version = ADMISSION_WIN32_ALLOCATION_VERSION;
  stagingDescription = description.Allocation;
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP1025/EXP1027: small unshared buffers are one CPU-visible class
   * allocation the GPU maps directly (the KMD keeps it in the local segment). */
  systemDirect = !BorrowedStaging && Bytes <= ADMISSION_UMD_SYSTEM_DIRECT_BYTES;
  if (!systemDirect) {
    description.Version = ADMISSION_WIN32_ALLOCATION_VERSION_LOCAL;
    description.Allocation.Type = ADMISSION_WIN32_ALLOCATION_GPU_LOCAL;
    description.Allocation.CpuVisible = 0u;
  }
#else
  UNREFERENCED_PARAMETER(stagingDescription);
  UNREFERENCED_PARAMETER(staging);
#endif
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
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  slot=AdmissionUmdScreenFreeSlot(device);
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(BorrowedStaging) for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(device->ScreenBuffers[i].Active &&
       device->ScreenBuffers[i].StagingAllocation==BorrowedStaging) slot=NULL;
#endif
  if(!slot || device->ScreenClosing || device->DrawTerminal || device->NextScreenToken==~0ULL || device->NextScreenSerial==~0ULL) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
  token=++device->NextScreenToken;
  ZeroMemory(slot,sizeof(*slot));
  slot->Token=token; slot->Serial=++device->NextScreenSerial;
  slot->Active=TRUE; slot->Transition=TRUE;
#ifdef APPLE_AGX_GPUVA_WINSYS
  slot->StagingAllocation=BorrowedStaging;
  slot->Borrowed=BorrowedStaging!=0;
#endif
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  result = device->KernelCallbacks->pfnAllocateCb(
      device->RuntimeDevice.handle, &allocate);
  {
    UINT values[4] = {ClassId, (UINT)Bytes, (UINT)Alignment,
                      allocationInfo.hAllocation != 0u};
    AdmissionUmdDiagnostic("g4-native-allocate-cb", result, values,
                           ARRAYSIZE(values));
  }
  canonical = allocationInfo.hAllocation;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (SUCCEEDED(result) && canonical && !staging && !systemDirect) {
    /* EXP1022: unshared staging is read and written only by this process
     * (the KMD copy escape receives its bytes); keep it in ordinary memory. */
    UNREFERENCED_PARAMETER(stagingDescription);
    privateStaging = (BYTE *)VirtualAlloc(NULL,
        (SIZE_T)((Bytes + 0xffffULL) & ~0xffffULL),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!privateStaging) result = E_OUTOFMEMORY;
  }
#endif
  if (FAILED(result) || !canonical
#ifdef APPLE_AGX_GPUVA_WINSYS
      || (!staging && !privateStaging && !systemDirect)
#endif
      ) {
    /* Callback failures may return a handle. Record every owned handle before
     * rollback, and keep the slot if release cannot be proved. */
    HRESULT failure = FAILED(result) ? result : E_FAIL;
    D3DKMT_HANDLE owned[2] = {canonical, staging == BorrowedStaging ? 0 : staging};
#ifdef APPLE_AGX_GPUVA_WINSYS
    if (privateStaging) (void)VirtualFree(privateStaging, 0, MEM_RELEASE);
#endif
    for (UINT i = 0; i < 2; ++i) {
      D3DDDICB_DEALLOCATE rollback = {};
      rollback.NumAllocations = 1; rollback.HandleList = &owned[i];
      if (owned[i] && device->KernelCallbacks->pfnDeallocateCb &&
          SUCCEEDED(device->KernelCallbacks->pfnDeallocateCb(
              device->RuntimeDevice.handle, &rollback))) owned[i] = 0;
    }
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    if (!owned[0] && !owned[1]) ZeroMemory(slot, sizeof(*slot));
    else {
      slot->KernelAllocation = owned[0];
#ifdef APPLE_AGX_GPUVA_WINSYS
      slot->StagingAllocation = owned[1];
#endif
      slot->Transition = FALSE;
      device->DrawTerminal = TRUE;
    }
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = failure;
    return 0;
  }
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  slot->KernelAllocation = canonical;
#ifdef APPLE_AGX_GPUVA_WINSYS
  slot->StagingAllocation = staging;
  slot->PrivateStaging = privateStaging;
  slot->SystemDirect = systemDirect;
  slot->Borrowed = BorrowedStaging != 0;
#endif
  slot->Bytes = Bytes;
  slot->Alignment = Alignment;
  slot->ClassId = ClassId;
  slot->Flags = Flags;
  slot->Active = TRUE;
  slot->Transition = FALSE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  {
    UINT values[8] = {(UINT)token, (UINT)(token >> 32), (UINT)Bytes,
        (UINT)Flags, (UINT)ClassId,
#ifdef APPLE_AGX_GPUVA_WINSYS
        (UINT)(systemDirect != 0) | ((UINT)(privateStaging != NULL) << 1) |
            ((UINT)(BorrowedStaging != 0) << 2),
#else
        0u,
#endif
        (UINT)canonical, 0u};
    AdmissionUmdDiagnostic("ddi-slot-create", S_OK, values, ARRAYSIZE(values));
  }
  device->LastScreenError = S_OK;
  *Token = token;
  return 1;
}

#ifdef APPLE_AGX_GPUVA_WINSYS
/* EXP1011: a second GPU-local canonical allocation with the slot's class,
 * flags and 64 KiB-rounded size (the description CreateClassBufferImpl uses). */
HRESULT AdmissionUmdScreenNewCanonical(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U32 ClassId, APPLE_AGX_U32 Flags, APPLE_AGX_U64 Bytes,
    D3DKMT_HANDLE *Allocation) {
  ADMISSION_WIN32_ALLOCATION_CREATE description;
  D3DDDI_ALLOCATIONINFO allocationInfo;
  D3DDDICB_ALLOCATE allocate;
  HRESULT result;
  if (Allocation) *Allocation = 0;
  if (Device == NULL || Allocation == NULL || Bytes == 0ULL ||
      Bytes > MAXUINT32 - 65535ULL || Device->KernelCallbacks == NULL ||
      Device->KernelCallbacks->pfnAllocateCb == NULL)
    return E_INVALIDARG;
  Bytes = (Bytes + 65535ULL) & ~65535ULL;
  ZeroMemory(&description, sizeof(description));
  if (!AdmissionAllocationDescribe((UINT)Bytes, 1u, 1u,
          (UINT)D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE,
          (UINT)D3DDDIFMT_A8, 1u, &description.Allocation))
    return E_INVALIDARG;
  description.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  description.Version = ADMISSION_WIN32_ALLOCATION_VERSION_LOCAL;
  description.Allocation.Type = ADMISSION_WIN32_ALLOCATION_GPU_LOCAL;
  description.Allocation.CpuVisible = 0u;
  description.Bytes = sizeof(description);
  description.ClassId = ClassId;
  description.Flags = Flags;
  ZeroMemory(&allocationInfo, sizeof(allocationInfo));
  allocationInfo.pPrivateDriverData = &description;
  allocationInfo.PrivateDriverDataSize = sizeof(description);
  ZeroMemory(&allocate, sizeof(allocate));
  allocate.NumAllocations = 1u;
  allocate.pAllocationInfo = &allocationInfo;
  result = Device->KernelCallbacks->pfnAllocateCb(
      Device->RuntimeDevice.handle, &allocate);
  if (SUCCEEDED(result) && allocationInfo.hAllocation == 0u) result = E_FAIL;
  if (SUCCEEDED(result)) *Allocation = allocationInfo.hAllocation;
  return result;
}

HRESULT AdmissionUmdScreenFreeAllocation(ADMISSION_UMD_DEVICE *Device,
    D3DKMT_HANDLE Allocation) {
  D3DDDICB_DEALLOCATE deallocate;
  if (Device == NULL || Allocation == 0u || Device->KernelCallbacks == NULL ||
      Device->KernelCallbacks->pfnDeallocateCb == NULL) return E_INVALIDARG;
  ZeroMemory(&deallocate, sizeof(deallocate));
  deallocate.NumAllocations = 1u;
  deallocate.HandleList = &Allocation;
  return Device->KernelCallbacks->pfnDeallocateCb(
      Device->RuntimeDevice.handle, &deallocate);
}
#endif

static int AdmissionUmdScreenCreateClassBuffer(
    void *Context, APPLE_AGX_U32 ClassId, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U64 Alignment, APPLE_AGX_U32 Flags, APPLE_AGX_U64 *Token) {
  return AdmissionUmdScreenCreateClassBufferImpl(
      Context, ClassId, Bytes, Alignment, Flags, Token, 0);
}

HRESULT AdmissionUmdScreenAdoptAllocation(
    ADMISSION_UMD_DEVICE *Device, D3DKMT_HANDLE KernelAllocation,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U64 Alignment,
    APPLE_AGX_U32 ClassId, APPLE_AGX_U32 Flags,
#ifdef APPLE_AGX_GPUVA_WINSYS
    BOOL WrittenPrimary, BOOL Direct,
#endif
    AGX_WIN32_SCREEN_BUFFER *Buffer) {
  const AGX_WIN32_BUFFER_CLASS_INFO *classInfo;
  ADMISSION_UMD_SCREEN_BUFFER *slot;
  APPLE_AGX_U64 token;
  if(Buffer) ZeroMemory(Buffer,sizeof(*Buffer));
  if(!Device || Device->Magic!=ADMISSION_UMD_DEVICE_MAGIC || !Buffer ||
     !KernelAllocation || !Bytes || !Alignment ||
     (Alignment&(Alignment-1ULL))!=0ULL)
    return E_INVALIDARG;
  /* Borrowed logical size need not be page-aligned. KMD owns the rounded
   * physical allocation; expose only the private-data extent to native BOs. */
  classInfo=AdmissionUmdScreenClass(Device,ClassId);
  if(!classInfo || Bytes>classInfo->MaximumBytes ||
     Alignment<classInfo->MinimumAlignment || !Flags ||
     (Flags&~classInfo->Flags)!=0u)
    return E_INVALIDARG;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (Direct) {
    /* R158: a presentation allocation is GPU-local (CpuVisible=0, local
     * segment, 64 KiB aligned) and therefore UAT-representable. Render into it
     * directly, as WDDM expects of a presentation surface. Pairing it as the
     * CPU staging of a separate canonical BO failed: LockCb returns
     * E_INVALIDARG on a non-CPU-visible allocation (EXP871), so no frame ever
     * reached the surface. One registration per handle; aliases share it. */
    AcquireSRWLockExclusive(&Device->ScreenBufferLock);
    slot=AdmissionUmdScreenFreeSlot(Device);
    for (UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
      if (Device->ScreenBuffers[i].Active &&
          (Device->ScreenBuffers[i].StagingAllocation==KernelAllocation ||
           Device->ScreenBuffers[i].KernelAllocation==KernelAllocation))
        slot=NULL;
    if (!slot || Device->ScreenClosing || Device->DrawTerminal ||
        Device->NextScreenToken==~0ULL || Device->NextScreenSerial==~0ULL) {
      ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
      return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    token=++Device->NextScreenToken;
    ZeroMemory(slot,sizeof(*slot));
    slot->Token=token;slot->Serial=++Device->NextScreenSerial;
    slot->KernelAllocation=KernelAllocation;slot->Bytes=Bytes;
    slot->Alignment=Alignment;slot->ClassId=ClassId;slot->Flags=Flags;
    slot->Active=TRUE;slot->Borrowed=TRUE;slot->Direct=TRUE;
    slot->WrittenPrimary=WrittenPrimary;
  } else {
  /* CPU-visible imports live in the aperture (system memory, not UAT
   * representable): keep the canonical local BO paired with CPU staging. */
  /* One canonical owner per borrowed handle; aliases must share that owner. */
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  for (UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if (Device->ScreenBuffers[i].Active &&
        (Device->ScreenBuffers[i].StagingAllocation==KernelAllocation ||
         Device->ScreenBuffers[i].KernelAllocation==KernelAllocation)) {
      ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
      return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  if (Bytes > MAXUINT32 - 65535ULL ||
      !AdmissionUmdScreenCreateClassBufferImpl(Device, ClassId,
          (Bytes + 65535ULL) & ~65535ULL, Alignment, Flags, &token,
          KernelAllocation)) return E_FAIL;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  slot=AdmissionUmdScreenFind(Device,token);
  slot->Bytes=Bytes;
  slot->WrittenPrimary=WrittenPrimary;
  }
#else
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  slot=AdmissionUmdScreenFreeSlot(Device);
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(Device->ScreenBuffers[i].Active &&
       Device->ScreenBuffers[i].KernelAllocation==KernelAllocation)
      slot=NULL;
  if(!slot || Device->ScreenClosing || Device->NextScreenToken==~0ULL ||
     Device->NextScreenSerial==~0ULL) {
    ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
    return HRESULT_FROM_WIN32(ERROR_BUSY);
  }
  token=++Device->NextScreenToken;
  ZeroMemory(slot,sizeof(*slot));
  slot->Token=token;slot->Serial=++Device->NextScreenSerial;
  slot->KernelAllocation=KernelAllocation;slot->Bytes=Bytes;
  slot->Alignment=Alignment;slot->ClassId=ClassId;slot->Flags=Flags;
  slot->Active=TRUE;slot->Borrowed=TRUE;
#ifdef APPLE_AGX_GPUVA_WINSYS
  slot->WrittenPrimary=WrittenPrimary;
#endif
#endif
  Buffer->Transport.Token=token;Buffer->Transport.Bytes=Bytes;
  Buffer->Transport.Generation=Device->Win32Generation;
  Buffer->Transport.Flags=Flags;Buffer->ClassId=ClassId;
  Buffer->Alignment=Alignment;
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  Device->LastScreenError=S_OK;
  return S_OK;
}

BOOL AdmissionUmdScreenAllocationRegistered(
    ADMISSION_UMD_DEVICE *Device, D3DKMT_HANDLE KernelAllocation) {
  BOOL found=FALSE;
  if(!Device || !KernelAllocation) return FALSE;
  AcquireSRWLockShared(&Device->ScreenBufferLock);
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(Device->ScreenBuffers[i].Active &&
       (
#ifdef APPLE_AGX_GPUVA_WINSYS
       Device->ScreenBuffers[i].Direct ? Device->ScreenBuffers[i].KernelAllocation :
       Device->ScreenBuffers[i].StagingAllocation
#else
       Device->ScreenBuffers[i].KernelAllocation
#endif
       )==KernelAllocation) {
      found=TRUE;break;
    }
  ReleaseSRWLockShared(&Device->ScreenBufferLock);
  return found;
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
  if (device == NULL || Address == NULL ||
      (Access & (AppleAgxWin32BufferCpuRead |
                 AppleAgxWin32BufferCpuWrite)) == 0u ||
      (Access & ~(AppleAgxWin32BufferCpuRead |
                  AppleAgxWin32BufferCpuWrite)) != 0u ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnLockCb == NULL)
    return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP995: a private slot defers its post-submission download until the
   * first CPU map; complete it before the staging becomes CPU-visible. */
  if (!AdmissionUmdGpuvaPrepareCpuMap(device, Token)) {
    device->LastScreenError = E_FAIL;
    return 0;
  }
#endif
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  if (device->ScreenClosing || device->DrawTerminal) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
  buffer = AdmissionUmdScreenFind(device, Token);
  if (buffer == NULL || !buffer->KernelAllocation || buffer->Mapped || buffer->Transition ||
      buffer->SubmissionHolds || Bytes == 0ULL ||
      Offset > buffer->Bytes || Bytes > buffer->Bytes - Offset ||
      (Access & buffer->Flags) != Access) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
  buffer->Transition = TRUE;
  {
    D3DKMT_HANDLE allocation = AdmissionUmdScreenCpuAllocation(buffer);
#ifdef APPLE_AGX_GPUVA_WINSYS
    BYTE *privateStaging = buffer->PrivateStaging;
#endif
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    ZeroMemory(&lock, sizeof(lock));
    lock.hAllocation = allocation;
    lock.Flags.LockEntire = 1u;
#ifndef APPLE_AGX_GPUVA_WINSYS
    if ((Access & AppleAgxWin32BufferCpuWrite) == 0u)
      lock.Flags.ReadOnly = 1u;
    else if ((Access & AppleAgxWin32BufferCpuRead) == 0u)
      lock.Flags.WriteOnly = 1u;
#endif
#ifdef APPLE_AGX_GPUVA_WINSYS
    if (privateStaging) {
      lock.pData = privateStaging; /* EXP1022: no VidMm lock */
      result = S_OK;
    } else
#endif
    result = device->KernelCallbacks->pfnLockCb(
        device->RuntimeDevice.handle, &lock);
    {
      UINT values[2] = {allocation != 0u, lock.pData != NULL};
      AdmissionUmdDiagnostic("g4-native-lock-cb", result, values,
                             ARRAYSIZE(values));
    }
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    buffer = AdmissionUmdScreenFind(device, Token);
    if (buffer == NULL || AdmissionUmdScreenCpuAllocation(buffer) != allocation) {
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
      device->LastScreenError = E_FAIL;
      return 0;
    }
  }
  if (FAILED(result) || lock.pData == NULL) {
    buffer->Transition = FALSE;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = FAILED(result) ? result : E_FAIL;
    return 0;
  }
  buffer->LockedBase = lock.pData;
  buffer->LockedAccess = Access;
  ++buffer->MapEpoch;
  if (buffer->MapEpoch == 0u)
    ++buffer->MapEpoch;
  buffer->Mapped = TRUE;
  buffer->Transition = FALSE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
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
      buffer->SourceHolds != 0u || buffer->SubmissionHolds != 0u) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
  allocation = AdmissionUmdScreenCpuAllocation(buffer);
  buffer->Transition = TRUE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  ZeroMemory(&unlock, sizeof(unlock));
  unlock.NumAllocations = 1u;
  unlock.phAllocations = &allocation;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (buffer->PrivateStaging)
    result = S_OK; /* EXP1022: ordinary memory, nothing to unlock */
  else
#endif
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
  if (buffer == NULL || AdmissionUmdScreenCpuAllocation(buffer) != allocation) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = E_FAIL;
    return 0;
  }
  buffer->LockedBase = NULL;
  buffer->LockedAccess = 0u;
  buffer->Mapped = FALSE;
  buffer->Transition = FALSE;
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP995: CPU writes made through the map must reach the canonical copy:
   * unmapped private slots are otherwise not re-inspected for upload. */
  AdmissionUmdStagingInvalidate(&buffer->Sync);
#endif
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
  if (device == NULL)
    return 0;
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(device, Token);
  if (buffer == NULL || buffer->Mapped || buffer->Transition || buffer->NativeBo != NULL ||
      buffer->SourceHolds != 0u || buffer->SubmissionHolds != 0u) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
  allocation = buffer->KernelAllocation;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (buffer->CopyHeld || device->DrawTerminal || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnDeallocateCb) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock); return 0;
  }
  buffer->Transition = TRUE;
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  /* EXP978: a borrowed Direct allocation outlives this slot, so its
   * persistent residency reference must be dropped explicitly. */
  if (buffer->Direct && buffer->Resident && buffer->KernelAllocation &&
      device->KernelCallbacks->pfnEvictCb) {
    D3DKMT_HANDLE resident = buffer->KernelAllocation;
    D3DDDICB_EVICT evict = {};
    evict.NumAllocations = 1; evict.AllocationList = &resident;
    result = device->KernelCallbacks->pfnEvictCb(device->RuntimeDevice.handle, &evict);
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    if (FAILED(result)) {
      buffer->Transition = FALSE;
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
      device->LastScreenError = result; return 0;
    }
    buffer->Resident = FALSE;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  }
  for (UINT i=0; i<2; ++i) {
    allocation = i == 0 ? (buffer->Direct ? 0 : buffer->KernelAllocation) :
        (buffer->Borrowed ? 0 : buffer->StagingAllocation);
    if (!allocation) continue;
    ZeroMemory(&deallocate,sizeof(deallocate));
    deallocate.NumAllocations=1; deallocate.HandleList=&allocation;
    result=device->KernelCallbacks->pfnDeallocateCb(device->RuntimeDevice.handle,&deallocate);
    AdmissionUmdVaRecordDeallocate(device,buffer->Token,allocation,
                                   buffer->CanonicalGpuVa,result);
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    if (FAILED(result)) {
      buffer->Transition=FALSE;
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
      device->LastScreenError=result; return 0;
    }
    if (i == 0) buffer->KernelAllocation=0;
    else buffer->StagingAllocation=0;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  }
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  if (buffer->PrivateStaging)
    (void)VirtualFree(buffer->PrivateStaging, 0, MEM_RELEASE);
  ZeroMemory(buffer,sizeof(*buffer));
  ReleaseSRWLockExclusive(&device->ScreenBufferLock);
  device->LastScreenError=S_OK; return 1;
#else
  if(buffer->Borrowed) {
    ZeroMemory(buffer,sizeof(*buffer));
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError=S_OK;
    return 1;
  }
  if(device->KernelCallbacks==NULL ||
     device->KernelCallbacks->pfnDeallocateCb==NULL) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    return 0;
  }
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
#endif
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
  if (fence == NULL || fence->Event == NULL) {
    if (device != NULL) device->LastScreenError = E_INVALIDARG;
    return 0;
  }
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
  if (fence == NULL || fence->Event == NULL) {
    if (device != NULL) device->LastScreenError = E_INVALIDARG;
    return 0;
  }
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
  if (Device->OwnerCookie != 0ULL || Device->DrawSubmission != NULL)
    return HRESULT_FROM_WIN32(ERROR_BUSY);
  for (;;) {
    LONG64 old = InterlockedCompareExchange64(&NextOwnerCookie, 0, 0);
    if (old == MAXLONGLONG) return E_OUTOFMEMORY;
    if (InterlockedCompareExchange64(&NextOwnerCookie, old + 1, old) == old) {
      Device->OwnerCookie = (APPLE_AGX_U64)(old + 1);
      break;
    }
  }
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
#ifdef APPLE_AGX_GPUVA_WINSYS
  screen.GpuvaOps = AdmissionUmdGpuvaOperations();
#endif
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

static HRESULT AdmissionUmdScreenSignalFenceTagged(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U32 Kind,
    APPLE_AGX_U64 QueryOwner, APPLE_AGX_U32 QueryGeneration,
    APPLE_AGX_U32 QueryIssue, APPLE_AGX_U32 *Fence) {
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
  slot->QueryOwner = QueryOwner;
  slot->QueryGeneration = QueryGeneration;
  slot->QueryIssue = QueryIssue;
  slot->Token = token;
  slot->Kind = Kind;
  slot->Active = TRUE;
  Device->LastScreenError = S_OK;
  *Fence = token;
  return S_OK;
}

HRESULT AdmissionUmdScreenSignalFence(ADMISSION_UMD_DEVICE *Device,
                                      APPLE_AGX_U32 *Fence) {
  return AdmissionUmdScreenSignalFenceTagged(Device, AdmissionUmdFenceDraw,
                                              0u, 0u, 0u, Fence);
}

HRESULT AdmissionUmdScreenSignalQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 *Fence) {
  if(Device == NULL || Owner == 0u || Owner != Device->OwnerCookie ||
     Generation == 0u || Generation != Device->Win32Generation || Issue == 0u)
    return E_INVALIDARG;
  return AdmissionUmdScreenSignalFenceTagged(Device,
      AdmissionUmdFenceQueryAttached, Owner, Generation, Issue, Fence);
}

static ADMISSION_UMD_SCREEN_FENCE *AdmissionUmdScreenQueryFence(
    ADMISSION_UMD_DEVICE *Device, APPLE_AGX_U64 Owner,
    APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 Kind) {
  ADMISSION_UMD_SCREEN_FENCE *slot=AdmissionUmdScreenFenceFind(Device,Fence);
  return slot && slot->Kind==Kind && slot->QueryOwner==Owner &&
      slot->QueryGeneration==Generation && slot->QueryIssue==Issue ? slot : NULL;
}

HRESULT AdmissionUmdScreenPollQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 Fence, BOOL *Completed) {
  DWORD waitResult;
  if(Completed) *Completed=FALSE;
  if(!Device || !Completed || Device->Magic!=ADMISSION_UMD_DEVICE_MAGIC)
    return E_INVALIDARG;
  ADMISSION_UMD_SCREEN_FENCE *slot=AdmissionUmdScreenQueryFence(Device,Owner,
      Generation,Issue,Fence,AdmissionUmdFenceQueryAttached);
  if(!slot || !slot->Event) return E_INVALIDARG;
  if(slot->Completed) { *Completed=TRUE;return S_OK; }
  waitResult=WaitForSingleObject(slot->Event,0u);
  if(waitResult==WAIT_TIMEOUT) { Device->LastScreenError=S_OK;return S_OK; }
  if(waitResult!=WAIT_OBJECT_0) {
    Device->LastScreenError=HRESULT_FROM_WIN32(GetLastError());
    return Device->LastScreenError;
  }
  slot->Completed=TRUE;Device->LastScreenError=S_OK;*Completed=TRUE;
  return S_OK;
}

HRESULT AdmissionUmdScreenConsumeQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 Fence) {
  ADMISSION_UMD_SCREEN_FENCE *slot=AdmissionUmdScreenQueryFence(Device,Owner,
      Generation,Issue,Fence,AdmissionUmdFenceQueryAttached);
  if(!slot || !slot->Completed) return E_INVALIDARG;
  return AdmissionUmdScreenRetireFence(Device,Fence) ? S_OK :
      (FAILED(Device->LastScreenError)?Device->LastScreenError:E_FAIL);
}

HRESULT AdmissionUmdScreenDetachQueryFence(ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U32 Issue,
    APPLE_AGX_U32 Fence) {
  ADMISSION_UMD_SCREEN_FENCE *slot=AdmissionUmdScreenQueryFence(Device,Owner,
      Generation,Issue,Fence,AdmissionUmdFenceQueryAttached);
  if(!slot) return E_INVALIDARG;
  slot->Kind=AdmissionUmdFenceQueryDetached;
  return S_OK;
}

HRESULT AdmissionUmdScreenCollectDetachedQueryFences(
    ADMISSION_UMD_DEVICE *Device) {
  HRESULT first=S_OK;
  if(!Device || Device->Magic!=ADMISSION_UMD_DEVICE_MAGIC) return E_INVALIDARG;
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_FENCE_LIMIT;++i) {
    ADMISSION_UMD_SCREEN_FENCE *slot=&Device->ScreenFences[i];
    if(!slot->Active || slot->Kind!=AdmissionUmdFenceQueryDetached) continue;
    DWORD waitResult=slot->Completed?WAIT_OBJECT_0:WaitForSingleObject(slot->Event,0u);
    if(waitResult==WAIT_TIMEOUT) continue;
    if(waitResult!=WAIT_OBJECT_0) {
      HRESULT error=HRESULT_FROM_WIN32(GetLastError());
      if(SUCCEEDED(first)) first=error;
      continue;
    }
    slot->Completed=TRUE;
    if(!AdmissionUmdScreenRetireFence(Device,slot->Token) && SUCCEEDED(first))
      first=FAILED(Device->LastScreenError)?Device->LastScreenError:E_FAIL;
  }
  return first;
}

HRESULT AdmissionUmdScreenFinalize(ADMISSION_UMD_DEVICE *Device,
                                   ULONG *Undeallocated) {
  HRESULT firstError = S_OK;
  ULONG undeallocated = 0u;
  UINT index;
  if (Device == NULL || Device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      Undeallocated == NULL)
    return E_INVALIDARG;
  if (AdmissionUmdScreenHasLiveSources(Device)) {
    *Undeallocated = 0u;
    Device->LastScreenError = HRESULT_FROM_WIN32(ERROR_BUSY);
    return Device->LastScreenError;
  }
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
