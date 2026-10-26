#include <windows.h>
#include <wingdi.h>
#ifdef APPLE_AGX_GPUVA_WINSYS
#include <psapi.h> /* K32GetProcessMemoryInfo, kernel32 on Windows 7+ */
#endif

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)

extern "C" {
#include "umd_internal.h"
#include "../../../mesa/winsys/agx_win32_wait.h"
}

static DECLSPEC_ALIGN(8) volatile LONG64 NextOwnerCookie;

static D3DKMT_HANDLE AdmissionUmdScreenCpuAllocation(
    const ADMISSION_UMD_SCREEN_BUFFER *Buffer) {
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (Buffer->FailedCpuLock) return Buffer->FailedCpuLock;
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
#ifdef ADMISSION_UMD_SLOT_CACHE_SIZE
  volatile LONG *hint = &Device->TokenSlotHint[ADMISSION_UMD_SLOT_HASH(Token)];
  index = (UINT)*hint;
  if (index && index <= ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device) &&
      Device->ScreenBuffers[index - 1u].Active &&
      Device->ScreenBuffers[index - 1u].Token == Token)
    return &Device->ScreenBuffers[index - 1u];
#endif
  for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++index) {
    ADMISSION_UMD_SCREEN_BUFFER *buffer = &Device->ScreenBuffers[index];
    if (buffer->Active && buffer->Token == Token) {
#ifdef ADMISSION_UMD_SLOT_CACHE_SIZE
      *hint = (LONG)(index + 1u);
#endif
      return buffer;
    }
  }
  return NULL;
}

/* One fixed-size record per large slot. OwnerCookie + Token is a lifetime
 * key, including across cached BO reuse; slot zeroing starts fresh counters.
 * Canonical VA is the latest range, not a claim about every sparse alias. */
static VOID AdmissionUmdScreenCpuAccessReceipt(ADMISSION_UMD_DEVICE *Device,
    const ADMISSION_UMD_SCREEN_BUFFER *Buffer, HRESULT Status, UINT Reason) {
  if (!Buffer->Active || Buffer->Bytes <= (16ULL << 20)) return;
  const ADMISSION_UMD_CPU_ACCESS *facts = &Buffer->CpuAccess;
  APPLE_AGX_U64 va = 0;
  UINT flags = (Buffer->Mapped ? 1u : 0u) |
      (Buffer->LockedBase ? 2u : 0u) | (facts->OwnerPointers ? 4u : 0u) |
      (facts->ActiveLocks ? 8u : 0u) | (Buffer->Borrowed ? 64u : 0u) |
      (Buffer->Transition ? 256u : 0u) | (Buffer->NativeBo ? 512u : 0u);
#ifdef APPLE_AGX_GPUVA_WINSYS
  va = Buffer->CanonicalGpuVa;
  flags |= (Buffer->Direct ? 16u : 0u) | (Buffer->SystemDirect ? 32u : 0u) |
      (Buffer->PrivateStaging ? 128u : 0u);
#endif
  if (facts->Maps == ~0u || facts->Locks == ~0u || facts->ActiveLocks == ~0u ||
      facts->OwnerPointers == ~0u || facts->HeapMaps == ~0u || facts->HeapDepth == ~0u)
    flags |= 1024u;
  const UINT values[16] = {1u | (Reason << 8),
      (UINT)Device->OwnerCookie, (UINT)(Device->OwnerCookie >> 32),
      (UINT)Buffer->Token, (UINT)(Buffer->Token >> 32), Buffer->KernelAllocation,
      (UINT)va, (UINT)(va >> 32), (UINT)Buffer->Bytes, (UINT)(Buffer->Bytes >> 32),
      facts->Maps, facts->Locks, facts->OwnerPointers, facts->HeapMaps,
      facts->HeapDepth, flags};
  AdmissionUmdDiagnostic("measure-bo-cpu-access", Status, values, ARRAYSIZE(values));
}

VOID AdmissionUmdScreenCpuAccessEvent(ADMISSION_UMD_DEVICE *Device,
                                     APPLE_AGX_U64 Token, UINT Event) {
  if (!Device) return;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  ADMISSION_UMD_SCREEN_BUFFER *buffer = AdmissionUmdScreenFind(Device, Token);
  if (buffer && buffer->Bytes > (16ULL << 20)) {
    AdmissionUmdCpuAccessEvent(&buffer->CpuAccess, Event);
    /* EXP1918U3: GravityMark is killed at its timeout after the removal, so
     * the snapshot (reason 2/3) and destruction rows never reach the trace.
     * Reason 4: one live row per CPU-access event of a >64 MiB slot, bounded
     * per process; the last row per token is the state at the failure. */
    static unsigned live; /* updated under ScreenBufferLock (exclusive) */
    if (buffer->Bytes > (64ULL << 20) && live < 512u && ++live)
      AdmissionUmdScreenCpuAccessReceipt(Device, buffer, 0, 4u);
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
}

VOID AdmissionUmdScreenCpuAccessSnapshot(ADMISSION_UMD_DEVICE *Device,
                                        HRESULT Status, UINT Reason) {
  if (!Device || (Reason != 2u && Reason != 3u)) return;
  UINT bit = Reason == 2u ? 1u : 2u;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  if (!(Device->CpuAccessSnapshots & bit)) {
    Device->CpuAccessSnapshots |= bit;
    /* All >16 MiB slots includes ALL >64 MiB candidates, without a top-N
     * cutoff. Work and output are bounded by the existing 4096-slot table. */
    for (UINT i = 0; i < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++i)
      AdmissionUmdScreenCpuAccessReceipt(Device, &Device->ScreenBuffers[i], Status, Reason);
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  AdmissionUmdDiagnosticFlush();
}

static ADMISSION_UMD_SCREEN_BUFFER *AdmissionUmdScreenFreeSlot(
    ADMISSION_UMD_DEVICE *Device) {
  UINT index;
  if (Device == NULL)
    return NULL;
  for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++index)
    if (!Device->ScreenBuffers[index].Active) {
      if (Device->ScreenBufferHighWater <= index)
        Device->ScreenBufferHighWater = index + 1u;
      return &Device->ScreenBuffers[index];
    }
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

/* EXP1131: the runtime deallocated the resource that owns Allocation. That
 * destroys the handle and drops every residency reference the device held
 * on it, so a Direct slot still naming it must neither evict nor reuse it:
 * an Evict of the dead handle made dxgkrnl mark the device as removed
 * (EXP1130 ETW: DxgkEvictInternal -> VidSchMarkDeviceAsError) and DWM
 * rebuilt its device, blanking the screen, on every Settings close. */
VOID AdmissionUmdScreenForgetAllocation(ADMISSION_UMD_DEVICE *Device,
                                        D3DKMT_HANDLE Allocation) {
#ifdef APPLE_AGX_GPUVA_WINSYS
  UINT index;
  if (Device == NULL || Allocation == 0u)
    return;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++index) {
    ADMISSION_UMD_SCREEN_BUFFER *buffer = &Device->ScreenBuffers[index];
    if (buffer->Active && buffer->Direct &&
        buffer->KernelAllocation == Allocation) {
      buffer->Resident = FALSE;
      buffer->KernelAllocation = 0;
    }
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
#else
  UNREFERENCED_PARAMETER(Device);
  UNREFERENCED_PARAMETER(Allocation);
#endif
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
    for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++index) {
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
    for (slot = 0u; slot < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++slot) {
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
  /* UnlockCb succeeded for the entire list, including entries after a
   * possible native-cache release failure below. */
  for (index = 0u; index < count; ++index)
    if (buffers[index]->Bytes > (16ULL << 20))
      AdmissionUmdCpuAccessEvent(&buffers[index]->CpuAccess, AdmissionCpuUnlock);
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
  for(index=0u;index<ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device);++index) {
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
      for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++index)
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

#ifdef APPLE_AGX_GPUVA_WINSYS
/* EXP1929: pool v8 identifies a headroom refusal, but cumulative transfer
 * bytes cannot identify retained CPU copies. Sample ownership BEFORE rollback
 * and the winsys cache retry. Logical spans only, not physical residency or
 * working set; borrowed/aliased canonical spans may overlap. Skip transitions,
 * copy-held slots and unknown sizes explicitly. Copy pointers are tested,
 * never dereferenced. CopyHeld protects transfers which publish merge shadows
 * outside ScreenBufferLock; do not sample those pointers during a transfer.
 * Caller holds no ScreenBufferLock. Emit only after releasing it. */
static VOID AdmissionUmdScreenMemoryRefusal(ADMISSION_UMD_DEVICE *Device,
    HRESULT Status, APPLE_AGX_U64 Token, UINT ClassId, UINT Flags,
    APPLE_AGX_U64 Bytes, UINT Stage, UINT Pending) {
  UINT header[16] = {3u, (UINT)Device->OwnerCookie,
      (UINT)(Device->OwnerCookie >> 32), (UINT)Token, (UINT)(Token >> 32),
      0u, ClassId, Flags, (UINT)Bytes, (UINT)(Bytes >> 32), Stage, Pending};
  UINT rows[3][16] = {};
  APPLE_AGX_U64 totals[3][3] = {}; /* all, mapped, unmapped logical bytes */
  UINT state[16] = {}, ownership[2][16] = {};
  APPLE_AGX_U64 canonicalBytes = 0, submissionHolds = 0, sourceHolds = 0;
  APPLE_AGX_U64 ownershipBytes[6] = {};
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  if (Device->MemoryRefusalSnapshots >= 4u) {
    ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
    return;
  }
  header[5] = ++Device->MemoryRefusalSnapshots;
  state[6] = (Device->DrawTerminal ? 1u : 0u) |
      (Device->ScreenClosing ? 2u : 0u);
  for (UINT i = 0u; i < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++i) {
    const ADMISSION_UMD_SCREEN_BUFFER *slot = &Device->ScreenBuffers[i];
    if (!slot->Active) continue;
    ++header[12];
    /* EXP1940: CopyHeld clears at queueing, SubmissionHolds at retirement.
     * Count scalar ownership even during transitions/transfers; unlike the
     * CPU-copy census below this never reads a staging pointer. Categories
     * overlap, bytes count each canonical span once per category, and zero
     * size remains explicitly unknown. These are not physical page counts. */
    if (slot->KernelAllocation) {
      ++state[7];
      canonicalBytes += slot->Bytes;
      submissionHolds += slot->SubmissionHolds;
      sourceHolds += slot->SourceHolds;
      if (slot->Transition) ++state[14];
      if (!slot->Bytes) ++state[15];
      const BOOL held[6] = {slot->Resident, slot->SubmissionHolds != 0u,
          slot->CopyHeld, slot->SourceHolds != 0u, slot->KmtResidencyHeld,
          slot->Resident && !slot->Transition && !slot->SubmissionHolds &&
              !slot->CopyHeld && !slot->SourceHolds && !slot->KmtResidencyHeld};
      for (UINT kind = 0u; kind < ARRAYSIZE(held); ++kind) {
        if (!held[kind]) continue;
        ++ownership[kind / 3u][7u + (kind % 3u) * 3u];
        ownershipBytes[kind] += slot->Bytes;
      }
    }
    if (slot->Transition) { ++header[13]; continue; }
    if (slot->CopyHeld) { ++header[15]; continue; }
    if (!slot->Bytes) { ++header[14]; continue; }
    const BOOL present[3] = {slot->KernelAllocation != 0,
        slot->PrivateStaging != NULL, slot->HeapStagingShadow != NULL};
    for (UINT kind = 0u; kind < 3u; ++kind) {
      if (!present[kind]) continue;
      ++rows[kind][7];
      totals[kind][0] += slot->Bytes;
      const UINT subset = slot->Mapped ? 1u : 2u;
      ++rows[kind][slot->Mapped ? 10u : 13u];
      totals[kind][subset] += slot->Bytes;
    }
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  const APPLE_AGX_U64 stateValues[3] = {canonicalBytes, submissionHolds, sourceHolds};
  for (UINT i = 0u; i < 6u; ++i) state[i] = header[i];
  for (UINT i = 0u; i < ARRAYSIZE(stateValues); ++i) {
    state[8u + i * 2u] = (UINT)stateValues[i];
    state[9u + i * 2u] = (UINT)(stateValues[i] >> 32);
  }
  /* EXP1930: logical copy spans alone cannot distinguish process commit
   * from physical pressure outside those copies. Sample the OS before any
   * diagnostic I/O/rollback/cache retry, without holding the slot lock.
   * These are adjacent, best-effort observations, not an atomic residency
   * snapshot. Failed queries must never be interpreted as zero headroom. */
  const DWORD savedError = GetLastError();
  MEMORYSTATUSEX memory = {};
  PROCESS_MEMORY_COUNTERS_EX process = {};
  UINT osRows[2][16] = {};
  APPLE_AGX_U64 osValues[2][4] = {};
  memory.dwLength = sizeof(memory);
  process.cb = sizeof(process);
  osRows[0][6] = GlobalMemoryStatusEx(&memory) != FALSE;
  if (osRows[0][6]) {
    osValues[0][0] = memory.ullAvailPhys;
    osValues[0][1] = memory.ullTotalPhys;
    osValues[0][2] = memory.ullAvailPageFile;
    osValues[0][3] = memory.ullTotalPageFile;
  } else osRows[0][7] = GetLastError();
  osRows[1][6] = K32GetProcessMemoryInfo(GetCurrentProcess(),
      (PROCESS_MEMORY_COUNTERS *)&process, sizeof(process)) != FALSE;
  if (osRows[1][6]) {
    osValues[1][0] = process.PrivateUsage;
    osValues[1][1] = process.WorkingSetSize;
    osValues[1][2] = process.PeakWorkingSetSize;
    osValues[1][3] = process.PeakPagefileUsage;
  } else osRows[1][7] = GetLastError();
  AdmissionUmdDiagnostic("measure-memory-refusal", Status, header, ARRAYSIZE(header));
  for (UINT kind = 0u; kind < 3u; ++kind) {
    for (UINT i = 0u; i < 6u; ++i) rows[kind][i] = header[i];
    rows[kind][6] = kind + 1u; /* canonical, private staging, merge baseline */
    for (UINT subset = 0u; subset < 3u; ++subset) {
      const UINT at = 8u + subset * 3u;
      rows[kind][at] = (UINT)totals[kind][subset];
      rows[kind][at + 1u] = (UINT)(totals[kind][subset] >> 32);
    }
    AdmissionUmdDiagnostic("measure-memory-storage", Status, rows[kind], 16u);
  }
  for (UINT kind = 0u; kind < 2u; ++kind) {
    for (UINT i = 0u; i < 6u; ++i) osRows[kind][i] = header[i];
    for (UINT i = 0u; i < 4u; ++i) {
      osRows[kind][8u + i * 2u] = (UINT)osValues[kind][i];
      osRows[kind][9u + i * 2u] = (UINT)(osValues[kind][i] >> 32);
    }
    AdmissionUmdDiagnostic(kind ? "measure-memory-process" : "measure-memory-system",
        Status, osRows[kind], 16u);
  }
  AdmissionUmdDiagnostic("measure-memory-state", Status, state, ARRAYSIZE(state));
  for (UINT group = 0u; group < ARRAYSIZE(ownership); ++group) {
    for (UINT i = 0u; i < 6u; ++i) ownership[group][i] = header[i];
    ownership[group][6] = group + 1u;
    for (UINT i = 0u; i < 3u; ++i) {
      const APPLE_AGX_U64 bytes = ownershipBytes[group * 3u + i];
      ownership[group][8u + i * 3u] = (UINT)bytes;
      ownership[group][9u + i * 3u] = (UINT)(bytes >> 32);
    }
    AdmissionUmdDiagnostic("measure-memory-residency", Status, ownership[group], 16u);
  }
  SetLastError(savedError);
}
#endif

#ifdef APPLE_AGX_NATIVE_STAGING
static VOID AdmissionUmdNativeStagingFallback(ADMISSION_UMD_DEVICE *Device,
    UINT Reason, ULONGLONG Bytes, HRESULT Status) {
  UINT values[5] = {Reason, (UINT)Bytes, (UINT)(Bytes >> 32), 0u, 0u};
  AcquireSRWLockShared(&Device->ScreenBufferLock);
  for (UINT i=0; i<ADMISSION_UMD_NATIVE_ARENAS; ++i)
    if (Device->NativeStaging[i].Ring) {
      values[3] += Device->NativeStaging[i].Ring->Used;
      values[4] += Device->NativeStaging[i].Ring->Capacity;
    }
  ReleaseSRWLockShared(&Device->ScreenBufferLock);
  AdmissionUmdDiagnostic("native-ring-fallback", Status, values, ARRAYSIZE(values));
}

static BOOL AdmissionUmdNativeStagingReserve(ADMISSION_UMD_DEVICE *Device,
    ULONGLONG Bytes, ULONGLONG Alignment, ADMISSION_UMD_RING_SLICE *Slice,
    BYTE **Address, ULONGLONG *Token) {
  BOOL reserved = FALSE;
  if (!Bytes || Bytes > ADMISSION_NATIVE_STAGING_MAX_BYTES ||
      Alignment > ADMISSION_NATIVE_STAGING_MAX_BYTES ||
      Device->NativeStagingDisabled || !Device->KernelCallbacks ||
      !Device->KernelCallbacks->pfnEscapeCb ||
      !Device->Adapter || Device->Adapter->Magic != ADMISSION_UMD_ADAPTER_MAGIC ||
      !Device->Adapter->RuntimeAdapter.handle || !Device->RuntimeDevice.handle ||
      !Device->KernelContext)
    return FALSE;
  for (UINT index=0; index<ADMISSION_UMD_NATIVE_ARENAS; ++index) {
    ADMISSION_UMD_NATIVE_ARENA *arena = &Device->NativeStaging[index];
    BOOL initialize = FALSE;
    AcquireSRWLockExclusive(&Device->ScreenBufferLock);
    if (!arena->Attempted) { arena->Attempted = TRUE; initialize = TRUE; }
    ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
    if (initialize) {
      auto *ring = (ADMISSION_UMD_STAGING_RING *)HeapAlloc(
          GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ADMISSION_UMD_STAGING_RING));
      if (ring) {
        ADMISSION_NATIVE_STAGING_REQUEST q = {};
        D3DDDICB_ESCAPE request = {};
        q.Magic = ADMISSION_NATIVE_STAGING_MAGIC;
        q.Version = 1u; q.Bytes = sizeof(q);
        q.Capacity = ADMISSION_NATIVE_STAGING_MAX_BYTES;
        request.hDevice = Device->RuntimeDevice.handle;
        request.hContext = Device->KernelContext;
        request.pPrivateDriverData = &q;
        request.PrivateDriverDataSize = sizeof(q);
        HRESULT hr = Device->KernelCallbacks->pfnEscapeCb(
            Device->Adapter->RuntimeAdapter.handle, &request);
        BOOL valid = SUCCEEDED(hr) && q.Magic == ADMISSION_NATIVE_STAGING_MAGIC &&
            q.Version == 1u && q.Bytes == sizeof(q) &&
            q.Capacity == ADMISSION_NATIVE_STAGING_MAX_BYTES && q.Token && q.UserVa &&
            !(q.UserVa & 65535ULL) && q.UserVa <= (ULONGLONG)(ULONG_PTR)-1 &&
            q.Capacity - 1ULL <= (ULONGLONG)(ULONG_PTR)-1 - q.UserVa &&
            AdmissionUmdRingInit(ring, q.Capacity);
        UINT values[4] = {q.Capacity, (UINT)q.Token, (UINT)(q.Token >> 32), (UINT)valid};
        AdmissionUmdDiagnostic("native-ring-map", hr, values, ARRAYSIZE(values));
        AcquireSRWLockExclusive(&Device->ScreenBufferLock);
        if (valid && !Device->ScreenClosing && !Device->DrawTerminal) {
          arena->Ring = ring;
          arena->Token = q.Token;
          arena->Address = (BYTE *)(ULONG_PTR)q.UserVa;
          ring = NULL;
        }
        ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
        if (ring) {
          /* A never-bound mapping can be canceled promptly. A failed cancel
           * remains the KMD process owner's responsibility, never VirtualFree. */
          if (SUCCEEDED(hr) && q.Token) {
            q.Magic = ADMISSION_NATIVE_STAGING_MAGIC; q.Version = 1u;
            q.Bytes = sizeof(q); q.Capacity = 0u; q.UserVa = 0ULL;
            (void)Device->KernelCallbacks->pfnEscapeCb(
                Device->Adapter->RuntimeAdapter.handle, &request);
          }
          HeapFree(GetProcessHeap(), 0, ring);
        }
      }
    }
    AcquireSRWLockExclusive(&Device->ScreenBufferLock);
    if (!Device->ScreenClosing && !Device->DrawTerminal && arena->Ring) {
      reserved = AdmissionUmdRingReserveOwned(arena->Ring,
          (UINT)Bytes, (UINT)Alignment, Slice);
      if (reserved) {
        *Address = arena->Address + Slice->Offset;
        *Token = arena->Token;
      }
    }
    BOOL available = arena->Ring != NULL;
    ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
    if (reserved) return TRUE;
    if (initialize && !available) break;
  }
  return FALSE;
}

/* Caller has completed the last-use fence, ended CPU ownership and destroyed
 * its WDDM allocation. LastUse=0 is allowed for clean creation rollback. */
static BOOL AdmissionUmdNativeStagingRelease(ADMISSION_UMD_DEVICE *Device,
    ULONGLONG Token, ULONGLONG Ticket, ULONGLONG LastUse) {
  BOOL released = FALSE;
  AcquireSRWLockExclusive(&Device->ScreenBufferLock);
  for (UINT i=0; i<ADMISSION_UMD_NATIVE_ARENAS; ++i) {
    ADMISSION_UMD_NATIVE_ARENA *arena = &Device->NativeStaging[i];
    if (arena->Ring && arena->Token == Token) {
      released = AdmissionUmdRingReleaseOwned(arena->Ring, Ticket, LastUse,
          LastUse > arena->Ring->Completed ? LastUse : arena->Ring->Completed);
      break;
    }
  }
  ReleaseSRWLockExclusive(&Device->ScreenBufferLock);
  return released;
}
#endif

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
  BOOL systemDirect = FALSE, writeWatch = FALSE;
  UINT failureStage = 1u; /* AllocateCb / missing canonical handle */
#ifdef APPLE_AGX_NATIVE_STAGING
  ADMISSION_NATIVE_STAGING_ALLOCATION nativeDescription = {};
  ADMISSION_UMD_RING_SLICE nativeSlice = {};
  BYTE *nativeAddress = NULL;
  ULONGLONG nativeToken = 0ULL;
#endif
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
  /* C75/T6b: one CPU/GPU allocation up to the reported class limit.
   * Idle SystemDirect slots can now release their residency references;
   * CPU maps restore residency and finish paging before taking the lock.
   * Persistent maps remain pinned until their matching Unlock succeeds. */
  systemDirect = !BorrowedStaging && Bytes <= classInfo->MaximumBytes;
  /* T13: keep C75c for every other owner. The D3D12 owner's CPU-facing
   * heaps AND internal CPU-written BOs use native canonical storage, with
   * ordinary process memory for the CPU; no aperture-resident copy BO.
   * RC25 instead keeps one canonical allocation; MapBuffer proves its full
   * pool placement after Lock and retains the lock while the pointer lives. */
  if (device->HeapStaging && !device->DirectCpuPool && !BorrowedStaging &&
      (Flags & (AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite)) &&
      !(Flags & AppleAgxWin32BufferShareable)) systemDirect = FALSE;
  if (Flags & AppleAgxWin32BufferGpuOnly) {
    if (ClassId != AgxWin32BufferClassGeneral || BorrowedStaging ||
        (Flags & (AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite |
                  AppleAgxWin32BufferShareable)) != 0u ||
        !(Flags & (AppleAgxWin32BufferGpuRead | AppleAgxWin32BufferGpuWrite)))
      return 0;
  }
  if (!systemDirect || (Flags & AppleAgxWin32BufferGpuOnly)) {
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
  if(BorrowedStaging) for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(device);++i)
    if(device->ScreenBuffers[i].Active &&
       device->ScreenBuffers[i].StagingAllocation==BorrowedStaging) slot=NULL;
#endif
  if(!slot || device->ScreenClosing || device->DrawTerminal || device->NextScreenToken==~0ULL || device->NextScreenSerial==~0ULL) {
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
#ifdef APPLE_AGX_NATIVE_STAGING
    if (nativeSlice.Ticket)
      (void)AdmissionUmdNativeStagingRelease(device, nativeToken, nativeSlice.Ticket, 0ULL);
#endif
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
  /* Transition pins the device/context while mapping drops the slot lock. */
#ifdef APPLE_AGX_NATIVE_STAGING
  if (device->HeapStaging && !BorrowedStaging &&
      (Flags & (AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite)) &&
      !(Flags & (AppleAgxWin32BufferShareable | AppleAgxWin32BufferGpuOnly))) {
    if (AdmissionUmdNativeStagingReserve(device, Bytes, Alignment,
          &nativeSlice, &nativeAddress, &nativeToken)) {
      nativeDescription.Create = description;
      nativeDescription.Create.Version = ADMISSION_WIN32_ALLOCATION_VERSION_NATIVE;
      nativeDescription.Create.Bytes = sizeof(nativeDescription);
      nativeDescription.Create.Allocation = stagingDescription;
      nativeDescription.Token = nativeToken;
      nativeDescription.Offset = nativeSlice.Offset;
    } else AdmissionUmdNativeStagingFallback(device, 1u, Bytes, S_OK);
  }
#endif

#ifdef APPLE_AGX_NATIVE_STAGING
  if (nativeAddress) {
    /* The slice is exclusively reserved and all former BO/fence owners have
     * retired. ExistingSysMem preserves its input bytes, so zero on reuse. */
    ZeroMemory(nativeAddress, (SIZE_T)Bytes);
    MemoryBarrier();
    allocationInfo.pPrivateDriverData = &nativeDescription;
    allocationInfo.PrivateDriverDataSize = sizeof(nativeDescription);
    allocationInfo.pSystemMem = nativeAddress;
  }
#endif
  unsigned wait_pfnAllocateCb_1=AdmissionUmdWaitBegin(AgxWaitAllocate);
  result = device->KernelCallbacks->pfnAllocateCb(
      device->RuntimeDevice.handle, &allocate);
  AdmissionUmdWaitEnd(wait_pfnAllocateCb_1);
  {
    UINT values[4] = {ClassId, (UINT)Bytes, (UINT)Alignment,
                      allocationInfo.hAllocation != 0u};
    AdmissionUmdDiagnostic("g4-native-allocate-cb", result, values,
                           ARRAYSIZE(values));
  }
  canonical = allocationInfo.hAllocation;
#ifdef APPLE_AGX_NATIVE_STAGING
  if (nativeAddress && (FAILED(result) || !canonical)) {
    /* An old/refusing kernel may have returned a handle. Only a proved
     * rollback permits fallback or slice reuse. Otherwise normal cleanup
     * below retains the ticket alongside the uncertain allocation. */
    D3DDDICB_DEALLOCATE undo = {};
    undo.NumAllocations = 1u; undo.HandleList = &canonical;
    if (!canonical || (device->KernelCallbacks->pfnDeallocateCb &&
        SUCCEEDED(device->KernelCallbacks->pfnDeallocateCb(
            device->RuntimeDevice.handle, &undo)))) {
      AdmissionUmdNativeStagingFallback(device, 2u, Bytes, result);
      (void)AdmissionUmdNativeStagingRelease(device, nativeToken, nativeSlice.Ticket, 0ULL);
      nativeSlice = {}; nativeAddress = NULL; canonical = 0;
      allocationInfo.hAllocation = 0;
      allocationInfo.pSystemMem = NULL;
      allocationInfo.pPrivateDriverData = &description;
      allocationInfo.PrivateDriverDataSize = sizeof(description);
      unsigned waited = AdmissionUmdWaitBegin(AgxWaitAllocate);
      result = device->KernelCallbacks->pfnAllocateCb(device->RuntimeDevice.handle, &allocate);
      AdmissionUmdWaitEnd(waited);
      canonical = allocationInfo.hAllocation;
    }
  }
  if (nativeAddress && SUCCEEDED(result) && canonical) systemDirect = TRUE;
#endif
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (SUCCEEDED(result) && canonical && !staging && !systemDirect) {
    failureStage = 2u; /* CPU private staging after canonical allocation */
    /* EXP1022: unshared staging is read and written only by this process
     * (the KMD copy escape receives its bytes); keep it in ordinary memory. */
    UNREFERENCED_PARAMETER(stagingDescription);
    /* EXP1083: watch CPU writes, so an unwritten staging copy needs no hash. */
    privateStaging = (BYTE *)VirtualAlloc(NULL,
        (SIZE_T)((Bytes + 0xffffULL) & ~0xffffULL),
        MEM_COMMIT | MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
    writeWatch = privateStaging != NULL;
    if (!privateStaging)
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
    AdmissionUmdScreenMemoryRefusal(device, failure, token, ClassId, Flags,
        Bytes, failureStage, (canonical ? 1u : 0u) |
        (privateStaging ? 2u : 0u) | (BorrowedStaging ? 4u : 0u));
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
    if (!owned[0] && !owned[1]) {
#ifdef APPLE_AGX_NATIVE_STAGING
      if (nativeSlice.Ticket) {
        /* Keep Active+Transition until the out-of-lock ring release is done:
         * they prevent concurrent finalization from freeing this device. */
        ReleaseSRWLockExclusive(&device->ScreenBufferLock);
        (void)AdmissionUmdNativeStagingRelease(device, nativeToken, nativeSlice.Ticket, 0ULL);
        AcquireSRWLockExclusive(&device->ScreenBufferLock);
      }
#endif
      ZeroMemory(slot, sizeof(*slot));
    }
    else {
      slot->KernelAllocation = owned[0];
#ifdef APPLE_AGX_GPUVA_WINSYS
      slot->StagingAllocation = owned[1];
#endif
      slot->Transition = FALSE;
      device->DrawTerminal = TRUE;
#ifdef APPLE_AGX_NATIVE_STAGING
      slot->NativeStagingAddress = nativeAddress;
      slot->NativeStagingTicket = nativeSlice.Ticket;
      slot->NativeStagingToken = nativeToken;
#endif
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
  slot->WriteWatch = writeWatch;
  slot->HeapStaging = device->HeapStaging && privateStaging != NULL && !BorrowedStaging;
  slot->SystemDirect = systemDirect;
#ifdef APPLE_AGX_NATIVE_STAGING
  slot->NativeStagingAddress = nativeAddress;
  slot->NativeStagingTicket = nativeSlice.Ticket;
  slot->NativeStagingToken = nativeToken;
#endif
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
#ifdef APPLE_AGX_NATIVE_STAGING
  if (nativeAddress) {
    UINT values[6] = {(UINT)nativeToken, (UINT)(nativeToken >> 32),
        (UINT)nativeSlice.Ticket, (UINT)(nativeSlice.Ticket >> 32),
        nativeSlice.Offset, (UINT)Bytes};
    AdmissionUmdDiagnostic("native-ring-allocation", S_OK, values, ARRAYSIZE(values));
  }
#endif
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
  unsigned wait_pfnAllocateCb_2=AdmissionUmdWaitBegin(AgxWaitAllocateCanonical);
  result = Device->KernelCallbacks->pfnAllocateCb(
      Device->RuntimeDevice.handle, &allocate);
  AdmissionUmdWaitEnd(wait_pfnAllocateCb_2);
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
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  int created = AdmissionUmdScreenCreateClassBufferImpl(
      Context, ClassId, Bytes, Alignment, Flags, Token, 0);
  /* EXP1057 receipt-only: an application's native backend fails for good
   * when a buffer cannot be created (EXP1056 Notepad, asahi_bo.c site 184).
   * Name the slot occupancy and device state of the refusal. */
  if (!created && device != NULL) {
    UINT active = 0u, values[7];
    AcquireSRWLockShared(&device->ScreenBufferLock);
    for (UINT i = 0u; i < ADMISSION_UMD_SCREEN_BUFFER_SCAN(device); ++i)
      if (device->ScreenBuffers[i].Active) ++active;
    ReleaseSRWLockShared(&device->ScreenBufferLock);
    values[0] = active; values[1] = ClassId;
    values[2] = (UINT)Bytes; values[3] = (UINT)(Bytes >> 32);
    values[4] = (UINT)device->LastScreenError;
    values[5] = (UINT)(device->DrawTerminal != FALSE) |
        ((UINT)(device->ScreenClosing != FALSE) << 1);
    values[6] = Flags;
    AdmissionUmdDiagnostic("reject-screen-create", E_FAIL, values,
                           ARRAYSIZE(values));
  }
  return created;
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
    for (UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device);++i)
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
  for (UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device);++i)
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
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device);++i)
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
  for(UINT i=0;i<ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device);++i)
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
  if (Address) *Address = NULL;
  if (device == NULL || Address == NULL ||
      (Access & (AppleAgxWin32BufferCpuRead |
                 AppleAgxWin32BufferCpuWrite)) == 0u ||
      (Access & ~(AppleAgxWin32BufferCpuRead |
                  AppleAgxWin32BufferCpuWrite)) != 0u ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnLockCb == NULL)
    return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (device->DirectCpuPool && !device->KernelCallbacks->pfnUnlockCb) return 0;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  buffer = AdmissionUmdScreenFind(device, Token);
  BOOL gpuOnly = buffer && (buffer->Flags & AppleAgxWin32BufferGpuOnly);
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  if (gpuOnly) {
    UINT values[3] = {(UINT)Token, (UINT)(Token >> 32), Access};
    *Address = NULL;
    device->LastScreenError = E_INVALIDARG;
    AdmissionUmdDiagnostic("measure-gpu-only-map", E_INVALIDARG, values, ARRAYSIZE(values));
    return 0;
  }
  /* EXP995: a private slot defers its post-submission download until the
   * first CPU map; complete it before the staging becomes CPU-visible. */
  if (!AdmissionUmdGpuvaPrepareCpuMap(device, Token) ||
      !AdmissionUmdGpuvaPrepareDirectMap(device, Token)) {
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
      buffer->SubmissionHolds || buffer->SourceHolds ||
#ifdef APPLE_AGX_GPUVA_WINSYS
      buffer->CopyHeld ||
#endif
      Bytes == 0ULL ||
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
    BOOL systemDirect = buffer->SystemDirect;
#ifdef APPLE_AGX_NATIVE_STAGING
    BYTE *nativeAddress = buffer->NativeStagingAddress;
    ULONGLONG nativeFence = buffer->LastUseFence;
#endif
#endif
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
#ifdef APPLE_AGX_GPUVA_WINSYS
    if (systemDirect && !AdmissionUmdGpuvaRestoreCpuMapResidency(device, Token)) {
      AcquireSRWLockExclusive(&device->ScreenBufferLock);
      buffer = AdmissionUmdScreenFind(device, Token);
      if (buffer != NULL) buffer->Transition = FALSE;
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
      device->LastScreenError = !device->DrawTerminal && FAILED(device->GpuvaFailure)
          ? device->GpuvaFailure : DXGI_ERROR_DEVICE_HUNG;
      return 0;
    }
#endif
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
#ifdef APPLE_AGX_NATIVE_STAGING
    if (nativeAddress) {
      result = AdmissionUmdGpuvaWaitNativeStaging(device, nativeFence) ? S_OK : E_FAIL;
      lock.pData = SUCCEEDED(result) ? nativeAddress : NULL;
    } else
#endif
    if (privateStaging) {
      lock.pData = privateStaging; /* EXP1022: no VidMm lock */
      result = S_OK;
    } else
#endif
    {
      unsigned wait_pfnLockCb_1=AdmissionUmdWaitBegin(AgxWaitLock);
      result = device->KernelCallbacks->pfnLockCb(
          device->RuntimeDevice.handle, &lock);
      AdmissionUmdWaitEnd(wait_pfnLockCb_1);
    }
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
  if (SUCCEEDED(result)
#ifdef APPLE_AGX_GPUVA_WINSYS
      && !buffer->PrivateStaging
#endif
#ifdef APPLE_AGX_NATIVE_STAGING
      && !buffer->NativeStagingAddress
#endif
      && buffer->Bytes > (16ULL << 20))
    AdmissionUmdCpuAccessEvent(&buffer->CpuAccess, AdmissionCpuLock);
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (SUCCEEDED(result) && device->DirectCpuPool && buffer->SystemDirect &&
#ifdef APPLE_AGX_NATIVE_STAGING
      !buffer->NativeStagingAddress &&
#endif
      !buffer->Borrowed && !(buffer->Flags & AppleAgxWin32BufferShareable)) {
    /* Lock preserves the CPU pointer, not a permanent segment-3 placement.
     * Transition excludes our trim/submit until the current full pool proof
     * succeeds and Mapped takes over. Later submissions still require native
     * proof: VidMm can republish this allocation outside the pool. */
    BOOL sameAllocation = lock.hAllocation == buffer->KernelAllocation;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    BOOL ready = sameAllocation && lock.pData &&
        AdmissionUmdGpuvaValidateCpuPoolMap(device, Token);
    if (!ready) {
      HRESULT failure = sameAllocation && lock.pData && FAILED(device->GpuvaFailure)
          ? device->GpuvaFailure : E_FAIL;
      D3DDDICB_UNLOCK undo = {};
      undo.NumAllocations = 1u;
      undo.phAllocations = &lock.hAllocation;
      HRESULT undone = device->KernelCallbacks->pfnUnlockCb(
          device->RuntimeDevice.handle, &undo);
      AcquireSRWLockExclusive(&device->ScreenBufferLock);
      /* Transition has retained this slot and its residency throughout. A
       * failed cleanup must keep the real lock/owner even though no pointer
       * was exposed. Terminal teardown may not reclaim uncertain ownership. */
      if (FAILED(undone)) {
        buffer->FailedCpuLock = lock.hAllocation;
        buffer->LockedBase = lock.pData;
        buffer->LockedAccess = 0u;
        buffer->Mapped = TRUE;
        device->DrawTerminal = TRUE;
      } else if (buffer->Bytes > (16ULL << 20)) {
        AdmissionUmdCpuAccessEvent(&buffer->CpuAccess, AdmissionCpuUnlock);
      }
      if (!sameAllocation) device->DrawTerminal = TRUE;
      buffer->Transition = FALSE;
      ReleaseSRWLockExclusive(&device->ScreenBufferLock);
      device->LastScreenError = failure;
      return 0;
    }
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
  }
#endif
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
  if (buffer->Bytes > (16ULL << 20))
    AdmissionUmdCpuAccessEvent(&buffer->CpuAccess, AdmissionCpuMap);
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
  if (buffer->PrivateStaging
#ifdef APPLE_AGX_NATIVE_STAGING
      || buffer->NativeStagingAddress
#endif
      )
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
  if (buffer->Bytes > (16ULL << 20)
#ifdef APPLE_AGX_GPUVA_WINSYS
      && !buffer->PrivateStaging
#endif
#ifdef APPLE_AGX_NATIVE_STAGING
      && !buffer->NativeStagingAddress
#endif
      ) AdmissionUmdCpuAccessEvent(&buffer->CpuAccess, AdmissionCpuUnlock);
#ifdef APPLE_AGX_GPUVA_WINSYS
  buffer->FailedCpuLock = 0;
#endif
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
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP1059: a Direct slot's shadow returns its changed chunks now. The map
   * itself is gone either way; a lost upload must not fail BO teardown. */
  if (!AdmissionUmdGpuvaFinishDirectMap(device, Token)) {
    UINT values[2] = {(UINT)Token, (UINT)(Token >> 32)};
    AdmissionUmdDiagnostic("reject-direct-unmap", E_FAIL, values,
                           ARRAYSIZE(values));
  }
#endif
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
  if (!buffer->CpuAccess.DestructionReported) {
    AdmissionUmdScreenCpuAccessReceipt(device, buffer, S_OK, 1u);
    buffer->CpuAccess.DestructionReported = 1u;
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
   * persistent residency reference must be dropped explicitly.
   * EXP1124: without EvictOnlyIfNecessary VidMm paged every released
   * application surface out to system memory just before its owner freed
   * it; only drop the reference and let VidMm evict under pressure. */
#ifdef APPLE_AGX_NATIVE_STAGING
  if (buffer->NativeStagingTicket &&
      !AdmissionUmdGpuvaWaitNativeStaging(device, buffer->LastUseFence)) {
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    buffer->Transition = FALSE;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = E_FAIL;
    return 0;
  }
#endif
  if (buffer->Direct && buffer->Resident && buffer->KernelAllocation &&
      device->KernelCallbacks->pfnEvictCb) {
    D3DKMT_HANDLE resident = buffer->KernelAllocation;
    D3DDDICB_EVICT evict = {};
    evict.Flags.EvictOnlyIfNecessary = 1u;
    evict.NumAllocations = 1; evict.AllocationList = &resident;
    unsigned wait_pfnEvictCb=AdmissionUmdWaitBegin(AgxWaitEvict);
    result = device->KernelCallbacks->pfnEvictCb(device->RuntimeDevice.handle, &evict);
    AdmissionUmdWaitEnd(wait_pfnEvictCb);
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
#ifdef APPLE_AGX_NATIVE_STAGING
  if (buffer->NativeStagingTicket && !AdmissionUmdNativeStagingRelease(device,
        buffer->NativeStagingToken, buffer->NativeStagingTicket, buffer->LastUseFence)) {
    AcquireSRWLockExclusive(&device->ScreenBufferLock);
    buffer->Transition = FALSE;
    device->DrawTerminal = TRUE;
    ReleaseSRWLockExclusive(&device->ScreenBufferLock);
    device->LastScreenError = E_FAIL;
    return 0;
  }
#endif
  AcquireSRWLockExclusive(&device->ScreenBufferLock);
  if (buffer->HeapStagingShadow)
    (void)VirtualFree(buffer->HeapStagingShadow, 0, MEM_RELEASE);
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
  unsigned event_wait=TimeoutMs ? AdmissionUmdWaitBegin(AgxWaitEvent) : 0;
  waitResult = WaitForSingleObject(fence->Event, TimeoutMs);
  AdmissionUmdWaitEnd(event_wait);
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
  for (index = 0u; index < ADMISSION_UMD_SCREEN_BUFFER_SCAN(Device); ++index) {
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
