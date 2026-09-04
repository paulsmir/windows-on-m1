#include "render_admission.h"

#define ADMISSION_MEMORY_RUNTIME_TAG 'uRGA'
#define ADMISSION_LOCAL_GPU_VA 0x1500000000ULL
#define ADMISSION_LOCAL_BYTES 0x01000000ULL
#define ADMISSION_APERTURE_GPU_VA 0x1600000000ULL
#define ADMISSION_APERTURE_BYTES 0x10000000ULL
#define ADMISSION_UAT_PAGE_CAPACITY 64u
#define ADMISSION_UAT_MAPPING_CAPACITY 8u
#define ADMISSION_APERTURE_PAGE_COUNT \
  (ADMISSION_APERTURE_BYTES / PAGE_SIZE)

typedef struct _ADMISSION_UAT_WINDOWS_IO {
  PDXGKRNL_INTERFACE Interface;
  volatile unsigned char *MappedBase;
  NTSTATUS LastStatus;
} ADMISSION_UAT_WINDOWS_IO;

typedef struct _ADMISSION_MEMORY_RUNTIME {
  ADMISSION_PHYSICAL_OWNER PhysicalOwner;
  APPLE_AGX_MEMORY_IO MemoryIo;
  APPLE_AGX_MEMORY_OBJECT LocalObject;
  APPLE_AGX_RESIDENCY_CONTEXT Residency;
  APPLE_AGX_MEMORY_OBJECT *UatObjects;
  APPLE_AGX_UAT_PAGE *UatPages;
  APPLE_AGX_UAT_MAPPING *UatMappings;
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY *ApertureEntries;
  ADMISSION_UAT_WINDOWS_IO Publication;
  APPLE_AGX_UAT_PUBLICATION_IO PublicationIo;
  APPLE_AGX_UAT_PUBLICATION_STATE Published;
  FAST_MUTEX PagingLock;
  BOOLEAN PhysicalReady;
  BOOLEAN LocalReady;
  BOOLEAN ResidencyReady;
  BOOLEAN MappingReady;
  BOOLEAN PublicationReady;
} ADMISSION_MEMORY_RUNTIME;

static unsigned char AdmissionMemoryAllocateContiguous(
    void *Opaque, unsigned long long Bytes, void **CpuBase,
    unsigned long long *DeviceBase, void **AllocationHandle) {
  ADMISSION_MEMORY_RUNTIME *runtime = (ADMISSION_MEMORY_RUNTIME *)Opaque;
  ADMISSION_PHYSICAL_ALLOCATION *allocation = NULL;
  NTSTATUS status;
  if (runtime == NULL || CpuBase == NULL || DeviceBase == NULL ||
      AllocationHandle == NULL || Bytes == 0ULL || Bytes > MAXSIZE_T)
    return 0u;
  *CpuBase = NULL;
  *DeviceBase = 0ULL;
  *AllocationHandle = NULL;
  status = AdmissionPhysicalAllocate(&runtime->PhysicalOwner, (SIZE_T)Bytes,
                                     &allocation);
  if (!NT_SUCCESS(status))
    return 0u;
  *CpuBase = allocation->CpuBase;
  *DeviceBase = allocation->HostPhysicalBase;
  *AllocationHandle = allocation;
  return 1u;
}

static unsigned char AdmissionMemoryFreeContiguous(
    void *Opaque, void *AllocationHandle) {
  ADMISSION_MEMORY_RUNTIME *runtime = (ADMISSION_MEMORY_RUNTIME *)Opaque;
  if (runtime == NULL || AllocationHandle == NULL)
    return 0u;
  return NT_SUCCESS(AdmissionPhysicalFree(
             &runtime->PhysicalOwner,
             (ADMISSION_PHYSICAL_ALLOCATION *)AllocationHandle))
             ? 1u
             : 0u;
}

static unsigned char AdmissionUatMap(
    void *Opaque, unsigned long long PhysicalAddress, unsigned int Length,
    volatile unsigned char **VirtualAddress) {
  ADMISSION_UAT_WINDOWS_IO *io = (ADMISSION_UAT_WINDOWS_IO *)Opaque;
  PHYSICAL_ADDRESS address;
  PVOID mapped = NULL;
  if (io == NULL || io->Interface == NULL ||
      io->Interface->DeviceHandle == NULL ||
      io->Interface->DxgkCbMapMemory == NULL || VirtualAddress == NULL ||
      io->MappedBase != NULL ||
      PhysicalAddress != J313_AGX_ABI_ADMISSION_GPU_BASE ||
      Length != (unsigned int)J313_AGX_ABI_ADMISSION_GPU_SIZE)
    return 0u;
  address.QuadPart = (LONGLONG)PhysicalAddress;
  io->LastStatus = io->Interface->DxgkCbMapMemory(
      io->Interface->DeviceHandle, address, Length, FALSE, FALSE,
      MmNonCached, &mapped);
  if (!NT_SUCCESS(io->LastStatus) || mapped == NULL)
    return 0u;
  io->MappedBase = (volatile unsigned char *)mapped;
  *VirtualAddress = io->MappedBase;
  return 1u;
}

static void AdmissionUatBarrier(void *Opaque) {
  UNREFERENCED_PARAMETER(Opaque);
  KeMemoryBarrier();
}

static unsigned char AdmissionUatUnmap(
    void *Opaque, volatile unsigned char *VirtualAddress) {
  ADMISSION_UAT_WINDOWS_IO *io = (ADMISSION_UAT_WINDOWS_IO *)Opaque;
  if (io == NULL || io->Interface == NULL ||
      io->Interface->DeviceHandle == NULL ||
      io->Interface->DxgkCbUnmapMemory == NULL || io->MappedBase == NULL ||
      VirtualAddress != io->MappedBase)
    return 0u;
  io->LastStatus = io->Interface->DxgkCbUnmapMemory(
      io->Interface->DeviceHandle, (PVOID)VirtualAddress);
  if (!NT_SUCCESS(io->LastStatus))
    return 0u;
  io->MappedBase = NULL;
  return 1u;
}

static BOOLEAN AdmissionGpuRegionAssigned(
    _In_ const DXGK_DEVICE_INFO *DeviceInformation) {
  PCM_RESOURCE_LIST resources;
  ULONG fullIndex;
  if (DeviceInformation == NULL)
    return FALSE;
  resources = DeviceInformation->TranslatedResourceList;
  if (resources == NULL)
    return FALSE;
  for (fullIndex = 0u; fullIndex < resources->Count; ++fullIndex) {
    PCM_FULL_RESOURCE_DESCRIPTOR full =
        &resources->List[fullIndex];
    ULONG partialIndex;
    for (partialIndex = 0u;
         partialIndex < full->PartialResourceList.Count; ++partialIndex) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
          &full->PartialResourceList.PartialDescriptors[partialIndex];
      if (descriptor->Type == CmResourceTypeMemory &&
          (ULONGLONG)descriptor->u.Memory.Start.QuadPart ==
              J313_AGX_ABI_ADMISSION_GPU_BASE &&
          descriptor->u.Memory.Length ==
              (ULONG)J313_AGX_ABI_ADMISSION_GPU_SIZE)
        return TRUE;
    }
  }
  return FALSE;
}

static VOID AdmissionMemoryFreeInventories(
    _Inout_ ADMISSION_MEMORY_RUNTIME *Runtime) {
  if (Runtime->ApertureEntries != NULL)
    ExFreePoolWithTag(Runtime->ApertureEntries,
                      ADMISSION_MEMORY_RUNTIME_TAG);
  if (Runtime->UatMappings != NULL)
    ExFreePoolWithTag(Runtime->UatMappings, ADMISSION_MEMORY_RUNTIME_TAG);
  if (Runtime->UatPages != NULL)
    ExFreePoolWithTag(Runtime->UatPages, ADMISSION_MEMORY_RUNTIME_TAG);
  if (Runtime->UatObjects != NULL)
    ExFreePoolWithTag(Runtime->UatObjects, ADMISSION_MEMORY_RUNTIME_TAG);
  Runtime->ApertureEntries = NULL;
  Runtime->UatMappings = NULL;
  Runtime->UatPages = NULL;
  Runtime->UatObjects = NULL;
}

static NTSTATUS AdmissionMemoryRuntimeDestroy(
    _Inout_ ADMISSION_MEMORY_RUNTIME *Runtime) {
  APPLE_AGX_RESIDENCY_STATUS residencyStatus;
  if (Runtime->PublicationReady) {
    if (AppleAgxUatUnpublishJ313(
            &Runtime->PublicationIo, &Runtime->Published) !=
        AppleAgxUatPublicationResultOk)
      return STATUS_DEVICE_BUSY;
    Runtime->PublicationReady = FALSE;
  }
  if (Runtime->MappingReady) {
    if (!AppleAgxResidencyUnmap64K(
            &Runtime->LocalObject, &Runtime->Residency.Roots,
            &Runtime->Residency.Allocator,
            &Runtime->Residency.Inventory, &residencyStatus))
      return STATUS_DEVICE_BUSY;
    Runtime->MappingReady = FALSE;
  }
  if (Runtime->ResidencyReady) {
    if (!AppleAgxResidencyContextDestroy(&Runtime->Residency,
                                         &residencyStatus))
      return STATUS_DEVICE_BUSY;
    Runtime->ResidencyReady = FALSE;
  }
  if (Runtime->LocalReady) {
    if (AppleAgxMemoryRelease(&Runtime->MemoryIo,
                              &Runtime->LocalObject) !=
        AppleAgxMemoryResultOk)
      return STATUS_DEVICE_BUSY;
    Runtime->LocalReady = FALSE;
  }
  AdmissionMemoryFreeInventories(Runtime);
  if (Runtime->PhysicalReady) {
    NTSTATUS status =
        AdmissionPhysicalOwnerDestroy(&Runtime->PhysicalOwner);
    if (!NT_SUCCESS(status))
      return status;
    Runtime->PhysicalReady = FALSE;
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeStart(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_MEMORY_RUNTIME *runtime;
  APPLE_AGX_RESIDENCY_STATUS residencyStatus;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot;
  APPLE_AGX_UAT_TTBR_PAIR pair;
  NTSTATUS status;

  if (Context == NULL || !Context->InterfaceValid ||
      Context->MemoryRuntime != NULL ||
      !AdmissionGpuRegionAssigned(&Context->DeviceInformation) ||
      Context->Interface.DxgkCbMapMemory == NULL ||
      Context->Interface.DxgkCbUnmapMemory == NULL)
    return STATUS_INVALID_DEVICE_STATE;
  runtime = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*runtime),
                            ADMISSION_MEMORY_RUNTIME_TAG);
  if (runtime == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(runtime, sizeof(*runtime));
  ExInitializeFastMutex(&runtime->PagingLock);
  Context->MemoryRuntime = runtime;

  runtime->UatObjects = ExAllocatePool2(
      POOL_FLAG_NON_PAGED,
      sizeof(*runtime->UatObjects) * ADMISSION_UAT_PAGE_CAPACITY,
      ADMISSION_MEMORY_RUNTIME_TAG);
  runtime->UatPages = ExAllocatePool2(
      POOL_FLAG_NON_PAGED,
      sizeof(*runtime->UatPages) * ADMISSION_UAT_PAGE_CAPACITY,
      ADMISSION_MEMORY_RUNTIME_TAG);
  runtime->UatMappings = ExAllocatePool2(
      POOL_FLAG_NON_PAGED,
      sizeof(*runtime->UatMappings) * ADMISSION_UAT_MAPPING_CAPACITY,
      ADMISSION_MEMORY_RUNTIME_TAG);
  runtime->ApertureEntries = ExAllocatePool2(
      POOL_FLAG_NON_PAGED,
      sizeof(*runtime->ApertureEntries) * ADMISSION_APERTURE_PAGE_COUNT,
      ADMISSION_MEMORY_RUNTIME_TAG);
  if (runtime->UatObjects == NULL || runtime->UatPages == NULL ||
      runtime->UatMappings == NULL || runtime->ApertureEntries == NULL) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto Fail;
  }
  RtlZeroMemory(runtime->UatObjects,
                sizeof(*runtime->UatObjects) * ADMISSION_UAT_PAGE_CAPACITY);
  RtlZeroMemory(runtime->UatPages,
                sizeof(*runtime->UatPages) * ADMISSION_UAT_PAGE_CAPACITY);
  RtlZeroMemory(runtime->UatMappings,
                sizeof(*runtime->UatMappings) *
                    ADMISSION_UAT_MAPPING_CAPACITY);

  status = AdmissionPhysicalOwnerInitialize(
      &Context->Interface, Context->PhysicalDeviceObject,
      &runtime->PhysicalOwner);
  if (!NT_SUCCESS(status))
    goto Fail;
  runtime->PhysicalReady = TRUE;
  runtime->MemoryIo.Context = runtime;
  runtime->MemoryIo.AllocateContiguous =
      AdmissionMemoryAllocateContiguous;
  runtime->MemoryIo.FreeContiguous = AdmissionMemoryFreeContiguous;

  if (AppleAgxMemoryAllocateAligned(
          &runtime->MemoryIo, ADMISSION_LOCAL_BYTES,
          ADMISSION_ALLOCATION_ALIGNMENT,
          &runtime->LocalObject) != AppleAgxMemoryResultOk ||
      AppleAgxMemoryMarkCpuWritten(&runtime->LocalObject) !=
          AppleAgxMemoryResultOk ||
      AppleAgxMemoryMarkPrepared(&runtime->LocalObject) !=
          AppleAgxMemoryResultOk) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto Fail;
  }
  runtime->LocalReady = TRUE;
  if (!AppleAgxResidencyContextCreate(
          &runtime->Residency, ADMISSION_MEMORY_UAT_CONTEXT,
          &runtime->MemoryIo, runtime->UatObjects,
          ADMISSION_UAT_PAGE_CAPACITY, runtime->UatPages,
          ADMISSION_UAT_PAGE_CAPACITY, runtime->UatMappings,
          ADMISSION_UAT_MAPPING_CAPACITY, &residencyStatus)) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto Fail;
  }
  runtime->ResidencyReady = TRUE;
  if (!AppleAgxResidencyMap64K(
          &runtime->LocalObject, ADMISSION_MEMORY_UAT_CONTEXT,
          &runtime->Residency.Roots, ADMISSION_LOCAL_GPU_VA,
          &runtime->Residency.Allocator, &runtime->Residency.Inventory,
          &residencyStatus)) {
    status = STATUS_INVALID_ADDRESS;
    goto Fail;
  }
  runtime->MappingReady = TRUE;
  if (AppleAgxUatEncodeTtbrPair(
          ADMISSION_MEMORY_UAT_CONTEXT, &runtime->Residency.Roots,
          &pair) != AppleAgxUatResultOk) {
    status = STATUS_INVALID_ADDRESS;
    goto Fail;
  }
  runtime->Publication.Interface = &Context->Interface;
  runtime->PublicationIo.Context = &runtime->Publication;
  runtime->PublicationIo.Map = AdmissionUatMap;
  runtime->PublicationIo.Barrier = AdmissionUatBarrier;
  runtime->PublicationIo.Unmap = AdmissionUatUnmap;
  RtlZeroMemory(&snapshot, sizeof(snapshot));
  snapshot.GpuRegionBase = J313_AGX_ABI_ADMISSION_GPU_BASE;
  if (AppleAgxUatPublishJ313Context(
          &snapshot, ADMISSION_MEMORY_UAT_CONTEXT, &pair,
          &runtime->PublicationIo, &runtime->Published) !=
          AppleAgxUatPublicationResultOk ||
      runtime->Published.Context != ADMISSION_MEMORY_UAT_CONTEXT ||
      runtime->Published.PublishedTtbr0 != pair.Ttbr0 ||
      runtime->Published.PublishedTtbr1 != pair.Ttbr1) {
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Fail;
  }
  runtime->PublicationReady = TRUE;
  if (!AdmissionMemoryInitialize(
          &Context->Memory, runtime->ApertureEntries,
          (APPLE_AGX_U32)ADMISSION_APERTURE_PAGE_COUNT,
          ADMISSION_APERTURE_GPU_VA, ADMISSION_APERTURE_BYTES,
          ADMISSION_LOCAL_GPU_VA, ADMISSION_LOCAL_BYTES) ||
      !AdmissionMemoryMarkUatReady(
          &Context->Memory, ADMISSION_MEMORY_UAT_CONTEXT,
          APPLE_AGX_UAT_PAGE_SIZE_16K, ADMISSION_LOCAL_GPU_VA,
          ADMISSION_LOCAL_BYTES)) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Fail;
  }
  return STATUS_SUCCESS;

Fail:
  if (!NT_SUCCESS(AdmissionMemoryRuntimeDestroy(runtime)))
    return STATUS_DEVICE_BUSY;
  ExFreePoolWithTag(runtime, ADMISSION_MEMORY_RUNTIME_TAG);
  Context->MemoryRuntime = NULL;
  RtlZeroMemory(&Context->Memory, sizeof(Context->Memory));
  return status;
}

static ADMISSION_MEMORY_RUNTIME *AdmissionMemoryGetRuntime(
    _In_ ADMISSION_CONTEXT *Context) {
  ADMISSION_MEMORY_RUNTIME *runtime;
  if (Context == NULL || Context->MemoryRuntime == NULL ||
      Context->Memory.Initialized != APPLE_AGX_TRUE ||
      Context->Memory.UatReady != APPLE_AGX_TRUE)
    return NULL;
  runtime = (ADMISSION_MEMORY_RUNTIME *)Context->MemoryRuntime;
  return runtime->PhysicalReady && runtime->LocalReady &&
                 runtime->ResidencyReady && runtime->MappingReady &&
                 runtime->PublicationReady
             ? runtime
             : NULL;
}

static ULONGLONG AdmissionMemoryReadU64(
    _In_reads_(8) volatile const unsigned char *Address) {
  ULONGLONG value = 0ULL;
  ULONG index;
  for (index = 0u; index < 8u; ++index)
    value |= (ULONGLONG)Address[index] << (index * 8u);
  return value;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeQualify(
    ADMISSION_CONTEXT *Context,
    ADMISSION_MEMORY_QUALIFICATION *Qualification) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  ADMISSION_PHYSICAL_ALLOCATION *allocation;
  APPLE_AGX_UAT_MAPPING *mapping;
  volatile const unsigned char *pairBase;
  ULONGLONG firstPhysical = 0ULL;
  ULONGLONG lastPhysical = 0ULL;
  ULONGLONG firstDescriptor = 0ULL;
  ULONGLONG lastDescriptor = 0ULL;
  ULONGLONG offset;

  if (Qualification == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(Qualification, sizeof(*Qualification));
  Qualification->Version = ADMISSION_MEMORY_QUALIFICATION_VERSION;
  Qualification->Size = sizeof(*Qualification);
  Qualification->QualificationStatus = STATUS_DEVICE_NOT_READY;
  Qualification->CleanupStatus = STATUS_PENDING;
  if (runtime == NULL || runtime->LocalObject.AllocationHandle == NULL ||
      runtime->Residency.Inventory.MappingCount != 1u ||
      runtime->Publication.MappedBase == NULL)
    return STATUS_INVALID_DEVICE_STATE;
  allocation = (ADMISSION_PHYSICAL_ALLOCATION *)
      runtime->LocalObject.AllocationHandle;
  mapping = &runtime->Residency.Inventory.Mappings[0];
  if ((PUCHAR)runtime->LocalObject.CpuAddress <
      (PUCHAR)runtime->LocalObject.AllocationCpuBase)
    return STATUS_INVALID_ADDRESS;
  offset = (ULONGLONG)((PUCHAR)runtime->LocalObject.CpuAddress -
                       (PUCHAR)runtime->LocalObject.AllocationCpuBase);

  Qualification->HvcReturnStatus =
      runtime->PhysicalOwner.LastHvcReturnStatus;
  Qualification->HvcPayloadStatus =
      runtime->PhysicalOwner.LastHvcPayloadStatus;
  Qualification->HvcInvocationCount =
      runtime->PhysicalOwner.HvcInvocationCount;
  Qualification->TranslatedPageCount =
      runtime->PhysicalOwner.TranslatedPageCount;
  Qualification->Context = runtime->Residency.Context;
  Qualification->UatPageCount = runtime->Residency.Inventory.PageCount;
  Qualification->UatMappingCount =
      runtime->Residency.Inventory.MappingCount;
  Qualification->GuestIpaBase = allocation->GuestIpaBase + offset;
  Qualification->HostPhysicalBase = runtime->LocalObject.DeviceAddress;
  Qualification->LocalGpuVa = runtime->LocalObject.GpuVirtualAddress;
  Qualification->LocalBytes = runtime->LocalObject.Length;
  Qualification->Ttbr0 = runtime->Published.PublishedTtbr0;
  Qualification->Ttbr1 = runtime->Published.PublishedTtbr1;

  if (Qualification->HvcReturnStatus != HV_GUEST_IPA_PA_STATUS_SUCCESS ||
      Qualification->HvcPayloadStatus != HV_GUEST_IPA_PA_STATUS_SUCCESS ||
      Qualification->HvcInvocationCount == 0u ||
      Qualification->TranslatedPageCount == 0u ||
      Qualification->Context != ADMISSION_MEMORY_UAT_CONTEXT ||
      Qualification->GuestIpaBase == 0ULL ||
      Qualification->HostPhysicalBase == 0ULL ||
      Qualification->HostPhysicalBase >= ADMISSION_HVC_PHYSICAL_LIMIT ||
      Qualification->LocalGpuVa != ADMISSION_LOCAL_GPU_VA ||
      Qualification->LocalBytes != ADMISSION_LOCAL_BYTES ||
      mapping->Context != ADMISSION_MEMORY_UAT_CONTEXT ||
      mapping->VirtualAddress != ADMISSION_LOCAL_GPU_VA ||
      mapping->PhysicalAddress != runtime->LocalObject.DeviceAddress ||
      mapping->Length != ADMISSION_LOCAL_BYTES ||
      mapping->PhysicalStride != APPLE_AGX_UAT_PAGE_SIZE_16K ||
      runtime->Published.Context != ADMISSION_MEMORY_UAT_CONTEXT)
    return STATUS_DEVICE_HARDWARE_ERROR;

  pairBase = runtime->Publication.MappedBase +
             (ULONGLONG)ADMISSION_MEMORY_UAT_CONTEXT * 16ULL;
  KeMemoryBarrier();
  if (AdmissionMemoryReadU64(pairBase) != Qualification->Ttbr0 ||
      AdmissionMemoryReadU64(pairBase + 8u) != Qualification->Ttbr1)
    return STATUS_DEVICE_HARDWARE_ERROR;
  if (AppleAgxUatResolvePage(
          ADMISSION_MEMORY_UAT_CONTEXT, &runtime->Residency.Roots,
          ADMISSION_LOCAL_GPU_VA, &runtime->Residency.Inventory,
          &firstPhysical, &firstDescriptor) != AppleAgxUatResultOk ||
      AppleAgxUatResolvePage(
          ADMISSION_MEMORY_UAT_CONTEXT, &runtime->Residency.Roots,
          ADMISSION_LOCAL_GPU_VA + ADMISSION_LOCAL_BYTES -
              APPLE_AGX_UAT_PAGE_SIZE_16K,
          &runtime->Residency.Inventory, &lastPhysical,
          &lastDescriptor) != AppleAgxUatResultOk ||
      firstPhysical != runtime->LocalObject.DeviceAddress ||
      lastPhysical != runtime->LocalObject.DeviceAddress +
                          ADMISSION_LOCAL_BYTES -
                          APPLE_AGX_UAT_PAGE_SIZE_16K)
    return STATUS_DEVICE_HARDWARE_ERROR;
  Qualification->FirstResolvedPhysical = firstPhysical;
  Qualification->LastResolvedPhysical = lastPhysical;
  Qualification->FirstLeafDescriptor = firstDescriptor;
  Qualification->LastLeafDescriptor = lastDescriptor;
  Qualification->QualificationStatus = STATUS_SUCCESS;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeMapAperture(
    ADMISSION_CONTEXT *Context, ULONGLONG ApertureByteOffset, PMDL Mdl,
    SIZE_T MdlPageOffset, UINT PageCount) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  PPFN_NUMBER pfns;
  SIZE_T mdlPages;
  ULONGLONG *pages;
  SIZE_T bytes;
  UINT index;
  APPLE_AGX_SOFTWARE_APERTURE_RESULT result;
  NTSTATUS status = STATUS_SUCCESS;

  if (runtime == NULL || Mdl == NULL || PageCount == 0u ||
      PageCount != APPLE_AGX_SYSTEM_PAGES_PER_WDDM_PAGE ||
      (SIZE_T)PageCount > MAXSIZE_T / sizeof(*pages))
    return STATUS_INVALID_PARAMETER;
  mdlPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(
      MmGetMdlVirtualAddress(Mdl), MmGetMdlByteCount(Mdl));
  if (MdlPageOffset > mdlPages || PageCount > mdlPages - MdlPageOffset)
    return STATUS_INVALID_PARAMETER;
  pfns = MmGetMdlPfnArray(Mdl);
  if (pfns == NULL)
    return STATUS_INVALID_PARAMETER;
  bytes = (SIZE_T)PageCount * sizeof(*pages);
  pages = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes,
                          ADMISSION_MEMORY_RUNTIME_TAG);
  if (pages == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  for (index = 0u; index < PageCount; ++index) {
    PFN_NUMBER pfn = pfns[MdlPageOffset + index];
    if ((ULONGLONG)pfn > (MAXULONGLONG >> PAGE_SHIFT)) {
      status = STATUS_INTEGER_OVERFLOW;
      goto Done;
    }
    pages[index] = (ULONGLONG)pfn << PAGE_SHIFT;
  }
  ExAcquireFastMutex(&runtime->PagingLock);
  result = AdmissionMemoryMapAperture64K(
      &Context->Memory, ApertureByteOffset, pages, PageCount);
  ExReleaseFastMutex(&runtime->PagingLock);
  if (result != AppleAgxSoftwareApertureOk)
    status = result == AppleAgxSoftwareApertureMisaligned
                 ? STATUS_DATATYPE_MISALIGNMENT
                 : result == AppleAgxSoftwareApertureOutOfRange
                       ? STATUS_INVALID_ADDRESS
                       : STATUS_INVALID_PARAMETER;
Done:
  RtlSecureZeroMemory(pages, bytes);
  ExFreePoolWithTag(pages, ADMISSION_MEMORY_RUNTIME_TAG);
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeUnmapAperture(
    ADMISSION_CONTEXT *Context, ULONGLONG ApertureByteOffset,
    ULONGLONG DummyPage) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  APPLE_AGX_SOFTWARE_APERTURE_RESULT result;
  if (runtime == NULL)
    return STATUS_INVALID_DEVICE_STATE;
  ExAcquireFastMutex(&runtime->PagingLock);
  result = AdmissionMemoryUnmapAperture64K(
      &Context->Memory, ApertureByteOffset, DummyPage);
  ExReleaseFastMutex(&runtime->PagingLock);
  if (result == AppleAgxSoftwareApertureOk ||
      result == AppleAgxSoftwareApertureDummy)
    return STATUS_SUCCESS;
  return result == AppleAgxSoftwareApertureMisaligned
             ? STATUS_DATATYPE_MISALIGNMENT
             : result == AppleAgxSoftwareApertureOutOfRange
                   ? STATUS_INVALID_ADDRESS
                   : STATUS_INVALID_PARAMETER;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeExecutePaging(
    ADMISSION_CONTEXT *Context, const ADMISSION_PAGING_RECORD *Record) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  PMDL mdl;
  PVOID systemBase = NULL;
  ULONGLONG systemBytes = 0ULL;
  BOOLEAN createdMapping = FALSE;
  APPLE_AGX_PHYSICAL_PAGING_RESULT result;
  if (runtime == NULL || Record == NULL ||
      Record->Header.Magic != ADMISSION_PAGING_MAGIC ||
      Record->Header.Version != ADMISSION_PAGING_VERSION ||
      Record->Header.RecordBytes != sizeof(*Record) ||
      Record->Header.Reserved != 0u)
    return STATUS_INVALID_PARAMETER;
  mdl = (PMDL)Record->SystemMdl;
  if (mdl != NULL) {
    createdMapping =
        (mdl->MdlFlags &
         (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL)) == 0;
    systemBase = MmGetSystemAddressForMdlSafe(
        mdl, NormalPagePriority | MdlMappingNoExecute);
    if (systemBase == NULL)
      return STATUS_INSUFFICIENT_RESOURCES;
    systemBytes = MmGetMdlByteCount(mdl);
  }
  ExAcquireFastMutex(&runtime->PagingLock);
  result = AppleAgxPhysicalPagingExecute(
      &Record->Plan, (unsigned char *)runtime->LocalObject.CpuAddress,
      runtime->LocalObject.Length, (unsigned char *)systemBase, systemBytes);
  KeMemoryBarrier();
  ExReleaseFastMutex(&runtime->PagingLock);
  if (createdMapping && systemBase != NULL)
    MmUnmapLockedPages(systemBase, mdl);
  if (result == AppleAgxPhysicalPagingOk)
    return STATUS_SUCCESS;
  if (result == AppleAgxPhysicalPagingOutOfRange)
    return STATUS_INVALID_ADDRESS;
  if (result == AppleAgxPhysicalPagingUnsupportedEndpoint)
    return STATUS_NOT_SUPPORTED;
  return STATUS_INVALID_PARAMETER;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeStop(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_MEMORY_RUNTIME *runtime;
  NTSTATUS status;
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  runtime = (ADMISSION_MEMORY_RUNTIME *)Context->MemoryRuntime;
  if (runtime == NULL)
    return STATUS_SUCCESS;
  status = AdmissionMemoryRuntimeDestroy(runtime);
  if (!NT_SUCCESS(status))
    return status;
  ExFreePoolWithTag(runtime, ADMISSION_MEMORY_RUNTIME_TAG);
  Context->MemoryRuntime = NULL;
  Context->ApertureEntries = NULL;
  RtlZeroMemory(&Context->Memory, sizeof(Context->Memory));
  return STATUS_SUCCESS;
}
