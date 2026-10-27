#include <ntifs.h>
#include "render_admission.h"
#include "render_native_staging.h"
#include "render_native_staging_receipt.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* MmAllocatePagesForMdlEx supplies zeroed WC pages in native 64KiB chunks.
 * A separate ordinary ExistingSysMem allocation supplies VidMm provenance.
 * This owner retains the original MDL until CPU views, WDDM bindings and the
 * caller's acknowledged GPU graph have all retired.
 *
 * Process-notify runs before address-space deletion in the last exiting
 * thread. Its mapping-only mutex serializes that boundary with foreign
 * attach/unmap. Never acquire this mutex under a GPU/pool lock, and never do
 * GPU, pool, registry, final object-release or callback-removal work under it.
 * https://learn.microsoft.com/windows-hardware/drivers/ddi/ntddk/nc-ntddk-pcreate_process_notify_routine
 * https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/nf-wdm-mmunmaplockedpages
 */
#define NATIVE_RING_LIMIT 4u
#define NATIVE_RING_MAX_BYTES (128u << 20)
#define NATIVE_RING_CHUNK 0x10000u
#define NATIVE_BINDING_MAGIC 0x4e53424eu
C_ASSERT(PAGE_SIZE == 0x1000u);

typedef struct _NATIVE_RING {
  ADMISSION_CONTEXT *Adapter;
  PVOID GpuProcess;
  PEPROCESS Owner;
  PMDL Mdl;
  PVOID UserVa;
  ULONGLONG Token;
  ULONG Bytes, Bindings;
  volatile LONG Closing;
  BOOLEAN Preparing, Releasing, Charged, EverBound;
} NATIVE_RING;

typedef struct _NATIVE_BINDING {
  NATIVE_RING *Ring;
  ULONGLONG Offset, Bytes;
  ULONG Magic;
} NATIVE_BINDING;

static FAST_MUTEX NativeLock;
static NATIVE_RING *NativeRings[NATIVE_RING_LIMIT];
/* 0 absent, 1 initialization, 2 accepting, 3 final unload, 4 abandoned backing. */
static volatile LONG NativeState;
static ULONGLONG NativeNextToken;

static VOID NativeUnmapLocked(NATIVE_RING *Ring) {
  KAPC_STATE attached;
  BOOLEAN foreign;
  InterlockedExchange(&Ring->Closing, 1);
  if (Ring->UserVa == NULL) return;
  foreign = PsGetCurrentProcess() != Ring->Owner;
  if (foreign) KeStackAttachProcess((PRKPROCESS)Ring->Owner, &attached);
  /* No failure is converted into UserGone. Keep this attach region limited
   * to the documented original-process unmap operation. */
  MmUnmapLockedPages(Ring->UserVa, Ring->Mdl);
  if (foreign) KeUnstackDetachProcess(&attached);
  Ring->UserVa = NULL;
}

static VOID NativeProcessNotify(HANDLE Parent, HANDLE ProcessId,
                                BOOLEAN Create) {
  ULONG i;
  UNREFERENCED_PARAMETER(Parent);
  UNREFERENCED_PARAMETER(ProcessId);
  if (Create) return;
  ExAcquireFastMutex(&NativeLock);
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i)
    if (NativeRings[i] != NULL &&
        NativeRings[i]->Owner == PsGetCurrentProcess())
      NativeUnmapLocked(NativeRings[i]);
  ExReleaseFastMutex(&NativeLock);
}

NTSTATUS AdmissionNativeStagingInitialize(void) {
  NTSTATUS status;
  LONG prior = InterlockedCompareExchange(&NativeState, 1, 0);
  if (prior == 2) return STATUS_SUCCESS;
  if (prior != 0) return STATUS_DEVICE_BUSY;
  ExInitializeFastMutex(&NativeLock);
  status = PsSetCreateProcessNotifyRoutine(NativeProcessNotify, FALSE);
  InterlockedExchange(&NativeState, NT_SUCCESS(status) ? 2 : 0);
  return status;
}

VOID AdmissionNativeStagingUninitialize(void) {
  PEPROCESS owners[NATIVE_RING_LIMIT];
  ULONG i, count = 0u;
  NTSTATUS status;
  LONG state = InterlockedCompareExchange(&NativeState, 3, 2);
  if (state != 2 && state != 3) return;
  ExAcquireFastMutex(&NativeLock);
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i)
    if (NativeRings[i] != NULL) NativeUnmapLocked(NativeRings[i]);
  ExReleaseFastMutex(&NativeLock);
  /* Final VOID unload cannot retain a registered callback, even if normal
   * Stop's empty-registry invariant failed. Removing our exact registered
   * callback is required to succeed; no lock may be held across removal. */
  status = PsSetCreateProcessNotifyRoutine(NativeProcessNotify, TRUE);
  NT_ASSERT(NT_SUCCESS(status));
  UNREFERENCED_PARAMETER(status);
  ExAcquireFastMutex(&NativeLock);
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i) {
    if (NativeRings[i] != NULL) {
      owners[count++] = NativeRings[i]->Owner;
      NativeRings[i]->Owner = NULL;
    }
  }
  /* A live owner has no GPU-retirement proof. Keep its MDL, binding metadata
   * and charged quota; state 4 permits no future pool/backing callout. This
   * bounded final-unload containment is not normal process reclamation. */
  InterlockedExchange(&NativeState, count != 0u ? 4 : 0);
  ExReleaseFastMutex(&NativeLock);
  for (i = 0u; i < count; ++i) ObDereferenceObject(owners[i]);
}

static BOOLEAN NativeUsable(void) {
  LONG state = InterlockedCompareExchange(&NativeState, 0, 0);
  return state == 2 || state == 3;
}

static VOID NativeFreeBacking(NATIVE_RING *Ring) {
  if (Ring->Mdl != NULL) {
    MmFreePagesFromMdl(Ring->Mdl);
    ExFreePool(Ring->Mdl);
  }
  if (Ring->Charged)
    AdmissionMemoryPoolUnchargeNative(Ring->Adapter, Ring->Bytes);
}

static VOID NativeFreeOwner(NATIVE_RING *Ring) {
  ObDereferenceObject(Ring->Owner);
  ExFreePoolWithTag(Ring, ADMISSION_POOL_TAG);
}

static VOID NativeReleaseRegistered(NATIVE_RING *Ring, ULONG Slot) {
  /* AdapterIdle must remain FALSE until the last out-of-lock pool access.
   * Preparing/Releasing prevents a second destroy from taking this record. */
  NativeFreeBacking(Ring);
  ExAcquireFastMutex(&NativeLock);
  NT_ASSERT(NativeRings[Slot] == Ring);
  NativeRings[Slot] = NULL;
  ExReleaseFastMutex(&NativeLock);
  NativeFreeOwner(Ring);
}

static BOOLEAN NativeGeometry(PMDL Mdl, ULONG Bytes) {
  const PFN_NUMBER *pages;
  ULONG first, part;
  if (MmGetMdlByteCount(Mdl) != Bytes) return FALSE;
  pages = MmGetMdlPfnArray(Mdl);
  for (first = 0u; first < Bytes / PAGE_SIZE;
       first += NATIVE_RING_CHUNK / PAGE_SIZE) {
    ULONGLONG pfn = (ULONGLONG)pages[first];
    if ((pfn & (NATIVE_RING_CHUNK / PAGE_SIZE - 1u)) != 0ULL ||
        pfn > (ADMISSION_HVC_PHYSICAL_LIMIT - NATIVE_RING_CHUNK) >> PAGE_SHIFT)
      return FALSE;
    for (part = 1u; part < NATIVE_RING_CHUNK / PAGE_SIZE; ++part)
      if ((ULONGLONG)pages[first + part] != pfn + part) return FALSE;
  }
  return TRUE;
}

NTSTATUS AdmissionNativeStagingMap(ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess, ULONG Bytes, PULONGLONG Token, PULONGLONG UserVa) {
  NATIVE_RING *ring;
  PHYSICAL_ADDRESS low, high, skip;
  NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
  ULONG i, slot = NATIVE_RING_LIMIT;
  if (Token != NULL) *Token = 0ULL;
  if (UserVa != NULL) *UserVa = 0ULL;
  if (Adapter == NULL || GpuProcess == NULL || Token == NULL ||
      UserVa == NULL || Bytes == 0u || Bytes > NATIVE_RING_MAX_BYTES ||
      (Bytes & (NATIVE_RING_CHUNK - 1u)) != 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  if (InterlockedCompareExchange(&NativeState, 0, 0) != 2)
    return STATUS_INVALID_DEVICE_STATE;
  ring = (NATIVE_RING *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                       sizeof(*ring), ADMISSION_POOL_TAG);
  if (ring == NULL) return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(ring, sizeof(*ring));
  ring->Owner = PsGetCurrentProcess();
  ObReferenceObject(ring->Owner);
  ring->Adapter = Adapter;
  ring->GpuProcess = GpuProcess;
  ring->Bytes = Bytes;
  ring->Preparing = TRUE;
  ExAcquireFastMutex(&NativeLock);
  if (InterlockedCompareExchange(&NativeState, 0, 0) != 2) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Refuse;
  }
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i) {
    NATIVE_RING *other = NativeRings[i];
    if (other == NULL) { slot = i; continue; }
    if (other->Adapter == Adapter && other->GpuProcess == GpuProcess &&
        (other->Closing || other->Owner != ring->Owner)) {
      status = STATUS_ACCESS_DENIED;
      goto Refuse;
    }
  }
  if (slot == NATIVE_RING_LIMIT) goto Refuse;
  if (NativeNextToken == ~0ULL) {
    status = STATUS_INTEGER_OVERFLOW;
    goto Refuse;
  }
  ring->Token = ++NativeNextToken;
  NativeRings[slot] = ring;
  ExReleaseFastMutex(&NativeLock);
  if (!AdmissionMemoryPoolChargeNative(Adapter, Bytes)) goto Rollback;
  ring->Charged = TRUE;
  low.QuadPart = 0;
  high.QuadPart = (LONGLONG)(ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL);
  skip.QuadPart = NATIVE_RING_CHUNK;
  /* No MM_DONT_ZERO_ALLOCATION: every physical byte is zero before exposure. */
  ring->Mdl = MmAllocatePagesForMdlEx(low, high, skip, (SIZE_T)Bytes,
      MmWriteCombined,
      MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
  if (ring->Mdl == NULL) goto Rollback;
  if (!NativeGeometry(ring->Mdl, Bytes)) {
    status = STATUS_INVALID_ADDRESS;
    goto Rollback;
  }
  ExAcquireFastMutex(&NativeLock);
  if (ring->Closing || InterlockedCompareExchange(&NativeState, 0, 0) != 2) {
    status = STATUS_INVALID_DEVICE_STATE;
  } else {
    __try {
      ring->UserVa = MmMapLockedPagesSpecifyCache(ring->Mdl, UserMode,
          MmWriteCombined, NULL, FALSE, NormalPagePriority);
      status = ring->UserVa == NULL ? STATUS_INSUFFICIENT_RESOURCES : STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      status = GetExceptionCode();
    }
  }
  if (NT_SUCCESS(status)) {
    ring->Preparing = FALSE;
    *Token = ring->Token;
    *UserVa = (ULONGLONG)(ULONG_PTR)ring->UserVa;
    ExReleaseFastMutex(&NativeLock);
    return STATUS_SUCCESS;
  }
  ExReleaseFastMutex(&NativeLock);
Rollback:
  NativeReleaseRegistered(ring, slot);
  return status;
Refuse:
  ExReleaseFastMutex(&NativeLock);
  NativeFreeBacking(ring);
  NativeFreeOwner(ring);
  return status;
}

BOOLEAN AdmissionNativeStagingBind(ADMISSION_CONTEXT *Adapter,
    ULONGLONG Token, ULONGLONG Offset, ULONGLONG Bytes, PVOID *Binding) {
  NATIVE_BINDING *binding;
  ULONG i;
  if (Binding != NULL) *Binding = NULL;
  if (Adapter == NULL || Binding == NULL || Token == 0ULL || Bytes == 0ULL ||
      ((Offset | Bytes) & (NATIVE_RING_CHUNK - 1u)) != 0ULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      InterlockedCompareExchange(&NativeState, 0, 0) != 2)
    return FALSE;
  binding = (NATIVE_BINDING *)ExAllocatePool2(POOL_FLAG_NON_PAGED,
      sizeof(*binding), ADMISSION_POOL_TAG);
  if (binding == NULL) return FALSE;
  ExAcquireFastMutex(&NativeLock);
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i) {
    NATIVE_RING *ring = NativeRings[i];
    if (ring == NULL || ring->Token != Token || ring->Adapter != Adapter ||
        ring->Owner != PsGetCurrentProcess() || ring->Preparing || ring->Releasing || ring->Closing ||
        ring->UserVa == NULL || Offset > ring->Bytes || Bytes > ring->Bytes - Offset ||
        ring->Bindings == MAXULONG)
      continue;
    ++ring->Bindings;
    ring->EverBound = TRUE;
    binding->Ring = ring;
    binding->Offset = Offset;
    binding->Bytes = Bytes;
    binding->Magic = NATIVE_BINDING_MAGIC;
    *Binding = binding;
    ExReleaseFastMutex(&NativeLock);
    return TRUE;
  }
  ExReleaseFastMutex(&NativeLock);
  ExFreePoolWithTag(binding, ADMISSION_POOL_TAG);
  return FALSE;
}

NTSTATUS AdmissionNativeStagingCancel(ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess, ULONGLONG Token) {
  NATIVE_RING *ring = NULL;
  NTSTATUS status = STATUS_INVALID_HANDLE;
  ULONG i, slot = NATIVE_RING_LIMIT;
  if (Adapter == NULL || GpuProcess == NULL || Token == 0ULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  if (!NativeUsable())
    return STATUS_INVALID_DEVICE_STATE;
  ExAcquireFastMutex(&NativeLock);
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i) {
    NATIVE_RING *candidate = NativeRings[i];
    if (candidate == NULL || candidate->Adapter != Adapter ||
        candidate->GpuProcess != GpuProcess || candidate->Token != Token)
      continue;
    if (candidate->Owner != PsGetCurrentProcess()) {
      status = STATUS_ACCESS_DENIED;
    } else if (candidate->Preparing || candidate->Releasing ||
               candidate->Bindings != 0u || candidate->EverBound) {
      status = STATUS_DEVICE_BUSY;
    } else {
      ring = candidate;
      slot = i;
      ring->Releasing = TRUE;
      NativeUnmapLocked(ring);
      status = STATUS_SUCCESS;
    }
    break;
  }
  ExReleaseFastMutex(&NativeLock);
  if (ring != NULL) NativeReleaseRegistered(ring, slot);
  return status;
}

VOID AdmissionNativeStagingUnbind(PVOID Binding) {
  NATIVE_BINDING *binding = (NATIVE_BINDING *)Binding;
  if (binding == NULL || !NativeUsable()) return;
  ExAcquireFastMutex(&NativeLock);
  NT_ASSERT(binding->Magic == NATIVE_BINDING_MAGIC && binding->Ring->Bindings != 0u);
  --binding->Ring->Bindings;
  binding->Magic = 0u;
  ExReleaseFastMutex(&NativeLock);
  ExFreePoolWithTag(binding, ADMISSION_POOL_TAG);
}

/* Allocation lifetime pins Binding and its immutable original MDL. These
 * queries may run under the GPU lock at APC_LEVEL, with no registry lock. */
BOOLEAN AdmissionNativeStagingPage(PVOID Binding,
    ULONGLONG AllocationOffset, ULONGLONG Ipa) {
  const NATIVE_BINDING *binding = (const NATIVE_BINDING *)Binding;
  const PFN_NUMBER *pages;
  if (binding == NULL || binding->Magic != NATIVE_BINDING_MAGIC ||
      ((AllocationOffset | Ipa) & (PAGE_SIZE - 1u)) != 0ULL ||
      AllocationOffset >= binding->Bytes)
    return FALSE;
  pages = MmGetMdlPfnArray(binding->Ring->Mdl);
  return ((ULONGLONG)pages[(binding->Offset + AllocationOffset) >> PAGE_SHIFT]
          << PAGE_SHIFT) == Ipa;
}

/* RC61 (EXP1983): the inverse of Page, under the same lifetime contract.
 * pSystemMem is UserVa + Offset of this binding's ring, so VidMm's PTE for
 * allocation offset o carries the ring page at Offset + o. Return o for Ipa
 * only when Ipa is one of this binding's own slice pages; any other page
 * (another slice, another ring, a dummy page) is FALSE. Bind requires a
 * 64 KiB Offset/Bytes and NativeGeometry proved every 64 KiB chunk
 * contiguous, so only chunk heads are scanned (at most 2048 for 128 MiB),
 * the exact PFN is still compared. */
BOOLEAN AdmissionNativeStagingLocate(PVOID Binding, ULONGLONG Ipa,
    PULONGLONG AllocationOffset) {
  const NATIVE_BINDING *binding = (const NATIVE_BINDING *)Binding;
  const PFN_NUMBER *pages;
  ULONGLONG chunk;
  if (AllocationOffset == NULL) return FALSE;
  *AllocationOffset = 0ULL;
  if (binding == NULL || binding->Magic != NATIVE_BINDING_MAGIC ||
      (Ipa & (PAGE_SIZE - 1u)) != 0ULL)
    return FALSE;
  pages = MmGetMdlPfnArray(binding->Ring->Mdl);
  for (chunk = 0ULL; chunk < binding->Bytes; chunk += NATIVE_RING_CHUNK) {
    ULONGLONG head = (ULONGLONG)pages[(binding->Offset + chunk) >> PAGE_SHIFT]
        << PAGE_SHIFT;
    ULONGLONG inner;
    if (Ipa < head || Ipa - head >= NATIVE_RING_CHUNK) continue;
    inner = Ipa - head;
    if (((ULONGLONG)pages[(binding->Offset + chunk + inner) >> PAGE_SHIFT]
         << PAGE_SHIFT) != Ipa)
      continue;
    *AllocationOffset = chunk + inner;
    return TRUE;
  }
  return FALSE;
}

/* RC60 receipt helper, under the same lifetime contract as Page. It runs
 * only for a page the paging path already refused, so the bounded scan of
 * the immutable ring PFN array (32768 entries for 128 MiB) is off the
 * ordinary path. It decides nothing; Page remains the only authority. */
VOID AdmissionNativeStagingDescribe(PVOID Binding, ULONGLONG AllocationOffset,
    ULONGLONG Ipa, PULONGLONG ExpectedIpa, PULONGLONG BindingOffset,
    PULONGLONG BindingBytes, PULONGLONG RingOffset) {
  const NATIVE_BINDING *binding = (const NATIVE_BINDING *)Binding;
  const PFN_NUMBER *pages;
  ULONG i, count;
  if (ExpectedIpa == NULL || BindingOffset == NULL || BindingBytes == NULL ||
      RingOffset == NULL)
    return;
  *ExpectedIpa = *RingOffset = ADMISSION_NATIVE_PTE_NONE;
  *BindingOffset = *BindingBytes = 0ULL;
  if (binding == NULL || binding->Magic != NATIVE_BINDING_MAGIC) return;
  *BindingOffset = binding->Offset;
  *BindingBytes = binding->Bytes;
  pages = MmGetMdlPfnArray(binding->Ring->Mdl);
  if ((AllocationOffset & (PAGE_SIZE - 1u)) == 0ULL &&
      AllocationOffset < binding->Bytes)
    *ExpectedIpa = (ULONGLONG)pages[(binding->Offset + AllocationOffset) >>
                                    PAGE_SHIFT] << PAGE_SHIFT;
  if ((Ipa & (PAGE_SIZE - 1u)) != 0ULL) return;
  count = binding->Ring->Bytes >> PAGE_SHIFT;
  for (i = 0u; i < count; ++i)
    if (((ULONGLONG)pages[i] << PAGE_SHIFT) == Ipa) {
      *RingOffset = (ULONGLONG)i << PAGE_SHIFT;
      break;
    }
}

BOOLEAN AdmissionNativeStagingOwns(PVOID Binding,
    ADMISSION_CONTEXT *Adapter, PVOID GpuProcess) {
  const NATIVE_BINDING *binding = (const NATIVE_BINDING *)Binding;
  return binding != NULL && binding->Magic == NATIVE_BINDING_MAGIC &&
      binding->Ring->Adapter == Adapter && binding->Ring->GpuProcess == GpuProcess &&
      binding->Ring->Owner == PsGetCurrentProcess() &&
      InterlockedCompareExchange(&binding->Ring->Closing, 0, 0) == 0;
}

VOID AdmissionNativeStagingUnmapProcess(ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess) {
  ULONG i;
  if (!NativeUsable()) return;
  ExAcquireFastMutex(&NativeLock);
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i)
    if (NativeRings[i] != NULL && NativeRings[i]->Adapter == Adapter &&
        NativeRings[i]->GpuProcess == GpuProcess)
      NativeUnmapLocked(NativeRings[i]);
  ExReleaseFastMutex(&NativeLock);
}

static BOOLEAN NativeIdleLocked(ADMISSION_CONTEXT *Adapter, PVOID GpuProcess) {
  ULONG i;
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i) {
    NATIVE_RING *ring = NativeRings[i];
    if (ring != NULL && ring->Adapter == Adapter && ring->GpuProcess == GpuProcess &&
        (ring->Preparing || ring->Releasing || ring->UserVa != NULL || ring->Bindings != 0u))
      return FALSE;
  }
  return TRUE;
}

BOOLEAN AdmissionNativeStagingProcessIdle(ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess) {
  BOOLEAN idle;
  LONG state = InterlockedCompareExchange(&NativeState, 0, 0);
  if (state == 4) return FALSE;
  if (state < 2) return TRUE;
  ExAcquireFastMutex(&NativeLock);
  idle = NativeIdleLocked(Adapter, GpuProcess);
  ExReleaseFastMutex(&NativeLock);
  return idle;
}

BOOLEAN AdmissionNativeStagingAdapterIdle(ADMISSION_CONTEXT *Adapter) {
  ULONG i;
  BOOLEAN idle = TRUE;
  LONG state = InterlockedCompareExchange(&NativeState, 0, 0);
  if (state == 4) return FALSE;
  if (state < 2) return TRUE;
  ExAcquireFastMutex(&NativeLock);
  for (i = 0u; i < NATIVE_RING_LIMIT; ++i)
    if (NativeRings[i] != NULL && NativeRings[i]->Adapter == Adapter) {
      idle = FALSE;
      break;
    }
  ExReleaseFastMutex(&NativeLock);
  return idle;
}

VOID AdmissionNativeStagingDestroyProcess(ADMISSION_CONTEXT *Adapter,
    PVOID GpuProcess) {
  NATIVE_RING *release[NATIVE_RING_LIMIT];
  ULONG slots[NATIVE_RING_LIMIT];
  ULONG i, count = 0u;
  if (!NativeUsable()) return;
  ExAcquireFastMutex(&NativeLock);
  if (NativeIdleLocked(Adapter, GpuProcess)) {
    for (i = 0u; i < NATIVE_RING_LIMIT; ++i)
      if (NativeRings[i] != NULL && NativeRings[i]->Adapter == Adapter &&
          NativeRings[i]->GpuProcess == GpuProcess) {
        NativeRings[i]->Releasing = TRUE;
        slots[count] = i;
        release[count++] = NativeRings[i];
      }
  }
  ExReleaseFastMutex(&NativeLock);
  for (i = 0u; i < count; ++i) NativeReleaseRegistered(release[i], slots[i]);
}
#endif
