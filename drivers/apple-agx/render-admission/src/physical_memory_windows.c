#include "render_admission.h"
#include <intrin.h>

#pragma intrinsic(__hvc)

#define ADMISSION_PHYSICAL_TAG 'pRGA'
#define ADMISSION_PHYSICAL_SCRATCH_BYTES 0x8000u

static BOOLEAN AdmissionPhysicalCallbacksValid(
    _In_ PDXGKRNL_INTERFACE Interface) {
  return Interface != NULL && Interface->DeviceHandle != NULL &&
         Interface->DxgkCbCreatePhysicalMemoryObject != NULL &&
         Interface->DxgkCbDestroyPhysicalMemoryObject != NULL &&
         Interface->DxgkCbAllocateAdl != NULL &&
         Interface->DxgkCbFreeAdl != NULL &&
         Interface->DxgkCbMapPhysicalMemory != NULL &&
         Interface->DxgkCbUnmapPhysicalMemory != NULL;
}

static VOID AdmissionPhysicalReleaseRaw(
    _Inout_ ADMISSION_PHYSICAL_ALLOCATION *Allocation) {
  if (Allocation == NULL)
    return;
  if (Allocation->MappedBase != NULL) {
    DXGKARGCB_UNMAP_PHYSICAL_MEMORY args;
    RtlZeroMemory(&args, sizeof(args));
    args.hPhysicalMemoryObject = Allocation->PhysicalMemoryObject;
    args.pBaseAddress = Allocation->MappedBase;
    args.Size = Allocation->MappedSize;
    Allocation->Interface->DxgkCbUnmapPhysicalMemory(&args);
    Allocation->MappedBase = NULL;
  }
  if (Allocation->Adl != NULL) {
    DXGKARGCB_FREE_ADL args;
    RtlZeroMemory(&args, sizeof(args));
    args.hAdapterMemoryObject = Allocation->AdapterMemoryObject;
    args.pAdl = Allocation->Adl;
    Allocation->Interface->DxgkCbFreeAdl(&args);
    Allocation->Adl = NULL;
  }
  if (Allocation->PhysicalMemoryObject != NULL) {
    DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT args;
    RtlZeroMemory(&args, sizeof(args));
    args.hPhysicalMemoryObject = Allocation->PhysicalMemoryObject;
    args.hAdapterMemoryObject = Allocation->AdapterMemoryObject;
    Allocation->Interface->DxgkCbDestroyPhysicalMemoryObject(&args);
    Allocation->PhysicalMemoryObject = NULL;
    Allocation->AdapterMemoryObject = NULL;
  }
}

static NTSTATUS AdmissionPhysicalCreateRaw(
    _In_ PDXGKRNL_INTERFACE Interface, _In_ SIZE_T Bytes,
    _Outptr_ ADMISSION_PHYSICAL_ALLOCATION **Result) {
  ADMISSION_PHYSICAL_ALLOCATION *allocation;
  DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT createArgs;
  DXGKARGCB_ALLOCATE_ADL adlArgs;
  DXGKARGCB_MAP_PHYSICAL_MEMORY mapArgs;
  ULONGLONG pageCount;
  ULONGLONG basePage;
  NTSTATUS status;

  if (Result == NULL)
    return STATUS_INVALID_PARAMETER;
  *Result = NULL;
  if (!AdmissionPhysicalCallbacksValid(Interface) || Bytes == 0u ||
      (Bytes & (PAGE_SIZE - 1u)) != 0u)
    return STATUS_INVALID_PARAMETER;
  allocation = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*allocation),
                               ADMISSION_PHYSICAL_TAG);
  if (allocation == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(allocation, sizeof(*allocation));
  allocation->Interface = Interface;
  allocation->Size = Bytes;

  RtlZeroMemory(&createArgs, sizeof(createArgs));
  createArgs.hAdapter = Interface->DeviceHandle;
  createArgs.Size = Bytes;
  createArgs.Context = (ULONG_PTR)allocation;
  createArgs.Type = DXGK_PHYSICAL_MEMORY_TYPE_CONTIGUOUS_MEMORY;
  createArgs.CacheType = DXGK_MEMORY_CACHING_TYPE_NON_CACHED;
  createArgs.ContiguousMemory.LowestAcceptableAddress.QuadPart = 0;
  createArgs.ContiguousMemory.HighestAcceptableAddress.QuadPart =
      ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL;
  status = Interface->DxgkCbCreatePhysicalMemoryObject(&createArgs);
  if (!NT_SUCCESS(status) || createArgs.hPhysicalMemoryObject == NULL ||
      createArgs.hAdapterMemoryObject == NULL) {
    status = NT_SUCCESS(status) ? STATUS_INVALID_HANDLE : status;
    goto Fail;
  }
  allocation->PhysicalMemoryObject = createArgs.hPhysicalMemoryObject;
  allocation->AdapterMemoryObject = createArgs.hAdapterMemoryObject;

  RtlZeroMemory(&adlArgs, sizeof(adlArgs));
  adlArgs.hAdapterMemoryObject = allocation->AdapterMemoryObject;
  adlArgs.Size = Bytes;
  adlArgs.Flags.RequireContiguous = 1u;
  status = Interface->DxgkCbAllocateAdl(&adlArgs);
  if (!NT_SUCCESS(status) || adlArgs.pAdl == NULL ||
      !adlArgs.pAdl->Flags.Contiguous) {
    status = NT_SUCCESS(status) ? STATUS_INVALID_ADDRESS : status;
    goto Fail;
  }
  allocation->Adl = adlArgs.pAdl;
  pageCount = ((ULONGLONG)Bytes + PAGE_SIZE - 1ULL) / PAGE_SIZE;
  if ((ULONGLONG)allocation->Adl->PageCount != pageCount) {
    status = STATUS_INVALID_BUFFER_SIZE;
    goto Fail;
  }
  basePage = (ULONGLONG)allocation->Adl->BasePageNumber;
  if (basePage == 0ULL || basePage > (MAXULONGLONG >> PAGE_SHIFT)) {
    status = STATUS_INVALID_ADDRESS;
    goto Fail;
  }
  allocation->GuestIpaBase = basePage << PAGE_SHIFT;

  RtlZeroMemory(&mapArgs, sizeof(mapArgs));
  mapArgs.hPhysicalMemoryObject = allocation->PhysicalMemoryObject;
  mapArgs.AccessMode = DXGK_ACCESS_MODE_KERNEL_MODE;
  mapArgs.Size = Bytes;
  status = Interface->DxgkCbMapPhysicalMemory(&mapArgs);
  if (!NT_SUCCESS(status) || mapArgs.pMappedAddress == NULL ||
      mapArgs.Offset > mapArgs.Size || Bytes > mapArgs.Size - mapArgs.Offset) {
    status = NT_SUCCESS(status) ? STATUS_INVALID_ADDRESS : status;
    goto Fail;
  }
  allocation->MappedBase = mapArgs.pMappedAddress;
  allocation->MappedSize = mapArgs.Size;
  allocation->CpuBase = (PUCHAR)mapArgs.pMappedAddress + mapArgs.Offset;
  *Result = allocation;
  return STATUS_SUCCESS;

Fail:
  AdmissionPhysicalReleaseRaw(allocation);
  ExFreePoolWithTag(allocation, ADMISSION_PHYSICAL_TAG);
  return status;
}

static unsigned int AdmissionPhysicalInvokeHvc(
    void *Context, unsigned int Immediate, unsigned long long RequestIpa,
  struct hv_guest_ipa_pa_request *Request) {
  ADMISSION_PHYSICAL_OWNER *owner = (ADMISSION_PHYSICAL_OWNER *)Context;
  ULONG status;
  if (Immediate != HV_GUEST_IPA_PA_HVC_IMMEDIATE)
    return HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST;
  KeMemoryBarrier();
  status = __hvc(HV_GUEST_IPA_PA_HVC_IMMEDIATE, RequestIpa);
  KeMemoryBarrier();
  if (owner != NULL && Request != NULL) {
    owner->LastHvcReturnStatus = status;
    owner->LastHvcPayloadStatus = Request->status;
    ++owner->HvcInvocationCount;
    if (status == HV_GUEST_IPA_PA_STATUS_SUCCESS &&
        Request->status == HV_GUEST_IPA_PA_STATUS_SUCCESS &&
        Request->count <= MAXULONG - owner->TranslatedPageCount)
      owner->TranslatedPageCount += Request->count;
  }
  return status;
}

static NTSTATUS AdmissionPhysicalTranslate(
    _Inout_ ADMISSION_PHYSICAL_OWNER *Owner,
    _Inout_ ADMISSION_PHYSICAL_ALLOCATION *Allocation) {
  ADMISSION_HVC_IO io;
  ULONGLONG *ipaPages = NULL;
  ULONGLONG *physicalPages = NULL;
  SIZE_T pageCount = Allocation->Size / PAGE_SIZE;
  SIZE_T bytes;
  SIZE_T index;
  NTSTATUS status = STATUS_DEVICE_HARDWARE_ERROR;

  if (pageCount == 0u || pageCount > MAXULONG ||
      pageCount > MAXSIZE_T / sizeof(*ipaPages))
    return STATUS_INTEGER_OVERFLOW;
  bytes = pageCount * sizeof(*ipaPages);
  ipaPages = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes,
                             ADMISSION_PHYSICAL_TAG);
  physicalPages = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes,
                                  ADMISSION_PHYSICAL_TAG);
  if (ipaPages == NULL || physicalPages == NULL) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto Done;
  }
  for (index = 0u; index < pageCount; ++index) {
    if (Allocation->GuestIpaBase >
        MAXULONGLONG - (ULONGLONG)index * PAGE_SIZE) {
      status = STATUS_INTEGER_OVERFLOW;
      goto Done;
    }
    ipaPages[index] = Allocation->GuestIpaBase +
                      (ULONGLONG)index * PAGE_SIZE;
  }
  io.Context = Owner;
  io.Invoke = AdmissionPhysicalInvokeHvc;
  if (!AdmissionHvcTranslatePages(&io, Owner->Request,
                                  Owner->RequestIpa, ipaPages,
                                  (UINT)pageCount, physicalPages))
    goto Done;
  for (index = 1u; index < pageCount; ++index) {
    if (physicalPages[index] != physicalPages[0] +
                                    (ULONGLONG)index * PAGE_SIZE)
      goto Done;
  }
  if (physicalPages[0] >= ADMISSION_HVC_PHYSICAL_LIMIT ||
      Allocation->Size > ADMISSION_HVC_PHYSICAL_LIMIT - physicalPages[0])
    goto Done;
  Allocation->HostPhysicalBase = physicalPages[0];
  status = STATUS_SUCCESS;

Done:
  if (physicalPages != NULL) {
    RtlSecureZeroMemory(physicalPages, bytes);
    ExFreePoolWithTag(physicalPages, ADMISSION_PHYSICAL_TAG);
  }
  if (ipaPages != NULL) {
    RtlSecureZeroMemory(ipaPages, bytes);
    ExFreePoolWithTag(ipaPages, ADMISSION_PHYSICAL_TAG);
  }
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionPhysicalOwnerInitialize(
    PDXGKRNL_INTERFACE Interface, PDEVICE_OBJECT DeviceObject,
    ADMISSION_PHYSICAL_OWNER *Owner) {
  ADMISSION_PHYSICAL_ALLOCATION *scratch = NULL;
  ULONGLONG requestIpa;
  SIZE_T requestOffset;
  NTSTATUS status;

  if (Owner == NULL || DeviceObject == NULL ||
      !AdmissionPhysicalCallbacksValid(Interface))
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(Owner, sizeof(*Owner));
  Owner->Interface = Interface;
  Owner->DeviceObject = DeviceObject;
  ExInitializeFastMutex(&Owner->Lock);
  status = AdmissionPhysicalCreateRaw(
      Interface, ADMISSION_PHYSICAL_SCRATCH_BYTES, &scratch);
  if (!NT_SUCCESS(status))
    goto Fail;
  requestIpa = (scratch->GuestIpaBase +
                HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE - 1ULL) &
               ~(HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE - 1ULL);
  requestOffset = (SIZE_T)(requestIpa - scratch->GuestIpaBase);
  if (requestOffset > scratch->Size ||
      sizeof(*Owner->Request) > scratch->Size - requestOffset ||
      !AdmissionHvcRequestFitsLeaf(requestIpa)) {
    status = STATUS_INVALID_ADDRESS;
    goto Fail;
  }
  Owner->Scratch = scratch;
  Owner->Request =
      (struct hv_guest_ipa_pa_request *)(scratch->CpuBase + requestOffset);
  Owner->RequestIpa = requestIpa;
  Owner->Initialized = TRUE;
  status = AdmissionPhysicalTranslate(Owner, scratch);
  if (!NT_SUCCESS(status))
    goto Fail;
  return STATUS_SUCCESS;

Fail:
  if (Owner->Request != NULL)
    RtlSecureZeroMemory(Owner->Request, sizeof(*Owner->Request));
  AdmissionPhysicalReleaseRaw(scratch);
  if (scratch != NULL)
    ExFreePoolWithTag(scratch, ADMISSION_PHYSICAL_TAG);
  RtlZeroMemory(Owner, sizeof(*Owner));
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionPhysicalAllocate(
    ADMISSION_PHYSICAL_OWNER *Owner, SIZE_T Bytes,
    ADMISSION_PHYSICAL_ALLOCATION **Allocation) {
  ADMISSION_PHYSICAL_ALLOCATION *created = NULL;
  NTSTATUS status;
  if (Owner == NULL || Allocation == NULL || !Owner->Initialized ||
      Bytes == 0u || (Bytes & (PAGE_SIZE - 1u)) != 0u)
    return STATUS_INVALID_PARAMETER;
  *Allocation = NULL;
  ExAcquireFastMutex(&Owner->Lock);
  status = AdmissionPhysicalCreateRaw(Owner->Interface, Bytes, &created);
  if (NT_SUCCESS(status))
    status = AdmissionPhysicalTranslate(Owner, created);
  if (NT_SUCCESS(status)) {
    InterlockedIncrement(&Owner->AllocationCount);
    *Allocation = created;
  } else if (created != NULL) {
    AdmissionPhysicalReleaseRaw(created);
    ExFreePoolWithTag(created, ADMISSION_PHYSICAL_TAG);
  }
  ExReleaseFastMutex(&Owner->Lock);
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionPhysicalFree(
    ADMISSION_PHYSICAL_OWNER *Owner,
    ADMISSION_PHYSICAL_ALLOCATION *Allocation) {
  if (Owner == NULL || Allocation == NULL || !Owner->Initialized ||
      Allocation == Owner->Scratch || Allocation->Interface != Owner->Interface)
    return STATUS_INVALID_PARAMETER;
  ExAcquireFastMutex(&Owner->Lock);
  if (Owner->AllocationCount <= 0) {
    ExReleaseFastMutex(&Owner->Lock);
    return STATUS_INVALID_DEVICE_STATE;
  }
  AdmissionPhysicalReleaseRaw(Allocation);
  RtlSecureZeroMemory(Allocation, sizeof(*Allocation));
  ExFreePoolWithTag(Allocation, ADMISSION_PHYSICAL_TAG);
  InterlockedDecrement(&Owner->AllocationCount);
  ExReleaseFastMutex(&Owner->Lock);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionPhysicalOwnerDestroy(
    ADMISSION_PHYSICAL_OWNER *Owner) {
  ADMISSION_PHYSICAL_ALLOCATION *scratch;
  if (Owner == NULL || !Owner->Initialized)
    return STATUS_INVALID_PARAMETER;
  ExAcquireFastMutex(&Owner->Lock);
  if (Owner->AllocationCount != 0) {
    ExReleaseFastMutex(&Owner->Lock);
    return STATUS_DEVICE_BUSY;
  }
  scratch = Owner->Scratch;
  RtlSecureZeroMemory(Owner->Request, sizeof(*Owner->Request));
  Owner->Scratch = NULL;
  Owner->Request = NULL;
  Owner->RequestIpa = 0ULL;
  Owner->Initialized = FALSE;
  ExReleaseFastMutex(&Owner->Lock);
  AdmissionPhysicalReleaseRaw(scratch);
  RtlSecureZeroMemory(scratch, sizeof(*scratch));
  ExFreePoolWithTag(scratch, ADMISSION_PHYSICAL_TAG);
  RtlZeroMemory(Owner, sizeof(*Owner));
  return STATUS_SUCCESS;
}
