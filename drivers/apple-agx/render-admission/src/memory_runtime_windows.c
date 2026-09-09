#include "render_admission.h"

#define ADMISSION_MEMORY_RUNTIME_TAG 'uRGA'
#define ADMISSION_LOCAL_GPU_VA 0x1500000000ULL
#define ADMISSION_LOCAL_BYTES 0x04000000ULL
#define ADMISSION_LOCAL_ALLOCATION_BYTES 0x03800000ULL
#define ADMISSION_BACKEND_BYTES 0x00800000ULL
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

static VOID AdmissionMemoryRecordStart(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ ADMISSION_MEMORY_START_STAGE Stage, _In_ NTSTATUS Status) {
  if (Context == NULL)
    return;
  InterlockedExchange(&Context->MemoryStartStage, (LONG)Stage);
  InterlockedExchange(&Context->MemoryStartStatus, (LONG)Status);
#if defined(APPLE_AGX_RENDER_MEMORY_QUALIFICATION)
  if (Context->BrokerBase != NULL && Stage != AdmissionMemoryStartNone) {
    volatile ULONGLONG *requestSequence =
        (volatile ULONGLONG *)(Context->BrokerBase +
            J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
    volatile ULONG *command =
        (volatile ULONG *)(Context->BrokerBase +
            J313_AGX_G2_POWER_REG_COMMAND);
    ULONGLONG sequence = 0x409000000ULL + (ULONGLONG)Stage * 2ULL +
                         (Status == STATUS_PENDING ? 0ULL : 1ULL);
    WRITE_REGISTER_ULONG64(requestSequence, sequence);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
    KeMemoryBarrier();
  }
#endif
}

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

  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartEntered,
                             STATUS_PENDING);
  if (Context == NULL || !Context->InterfaceValid ||
      Context->MemoryRuntime != NULL ||
      !AdmissionGpuRegionAssigned(&Context->DeviceInformation) ||
      Context->Interface.DxgkCbMapMemory == NULL ||
      Context->Interface.DxgkCbUnmapMemory == NULL) {
    AdmissionMemoryRecordStart(Context, AdmissionMemoryStartEntered,
                               STATUS_INVALID_DEVICE_STATE);
    return STATUS_INVALID_DEVICE_STATE;
  }
  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartInventories,
                             STATUS_PENDING);
  runtime = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*runtime),
                            ADMISSION_MEMORY_RUNTIME_TAG);
  if (runtime == NULL)
  {
    AdmissionMemoryRecordStart(Context, AdmissionMemoryStartInventories,
                               STATUS_INSUFFICIENT_RESOURCES);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
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

  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartPhysicalOwner,
                             STATUS_PENDING);
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

  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartLocalObject,
                             STATUS_PENDING);
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
  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartResidency,
                             STATUS_PENDING);
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
  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartMapping,
                             STATUS_PENDING);
  if (!AppleAgxResidencyMap64K(
          &runtime->LocalObject, ADMISSION_MEMORY_UAT_CONTEXT,
          &runtime->Residency.Roots, ADMISSION_LOCAL_GPU_VA,
          &runtime->Residency.Allocator, &runtime->Residency.Inventory,
          &residencyStatus)) {
    status = STATUS_INVALID_ADDRESS;
    goto Fail;
  }
  {
    static const ULONGLONG fixedGpuVa[
        APPLE_AGX_RENDER_TEMPLATE_FIXED_INPUT_COUNT] = {
        0x1100020000ULL, 0x1100010000ULL};
    static const ULONG fixedBytes[
        APPLE_AGX_RENDER_TEMPLATE_FIXED_INPUT_COUNT] = {
        0x40000u, 0x4000u};
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts =
        AppleAgxRenderTemplateObjectLayouts();
    ULONG fixedIndex;
    if (layouts == NULL) {
      status = STATUS_INVALID_IMAGE_FORMAT;
      goto Fail;
    }
    for (fixedIndex = 0u;
         fixedIndex < APPLE_AGX_RENDER_TEMPLATE_FIXED_INPUT_COUNT;
         ++fixedIndex) {
      const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *fixedInput =
          &layouts[APPLE_AGX_RENDER_TEMPLATE_FIXED_INPUT_FIRST_OBJECT +
                   fixedIndex];
      ULONGLONG fixedPhysical;
      if (fixedInput->OriginalGpuVa != fixedGpuVa[fixedIndex] ||
          fixedInput->Size != fixedBytes[fixedIndex] ||
          fixedInput->ArenaOffset > ADMISSION_BACKEND_BYTES ||
          fixedInput->Size >
              ADMISSION_BACKEND_BYTES - fixedInput->ArenaOffset ||
          runtime->LocalObject.DeviceAddress >
              MAXULONGLONG - ADMISSION_LOCAL_ALLOCATION_BYTES -
                  fixedInput->ArenaOffset) {
        status = STATUS_INVALID_IMAGE_FORMAT;
        goto Fail;
      }
      fixedPhysical = runtime->LocalObject.DeviceAddress +
                      ADMISSION_LOCAL_ALLOCATION_BYTES +
                      fixedInput->ArenaOffset;
      if (AppleAgxUatMap(
              ADMISSION_MEMORY_UAT_CONTEXT, &runtime->Residency.Roots,
              fixedGpuVa[fixedIndex], fixedPhysical, fixedBytes[fixedIndex],
              AppleAgxUatGpuSharedReadWrite, &runtime->Residency.Allocator,
              &runtime->Residency.Inventory) != AppleAgxUatResultOk) {
        status = STATUS_INVALID_ADDRESS;
        goto Fail;
      }
    }
  }
  runtime->MappingReady = TRUE;
  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartTtbr,
                             STATUS_PENDING);
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
  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartPublication,
                             STATUS_PENDING);
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
  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartContract,
                             STATUS_PENDING);
  if (!AdmissionMemoryInitialize(
          &Context->Memory, runtime->ApertureEntries,
          (APPLE_AGX_U32)ADMISSION_APERTURE_PAGE_COUNT,
          ADMISSION_APERTURE_GPU_VA, ADMISSION_APERTURE_BYTES,
          ADMISSION_LOCAL_GPU_VA, ADMISSION_LOCAL_BYTES) ||
      !AdmissionMemoryReserveBackendTail(
          &Context->Memory, ADMISSION_LOCAL_ALLOCATION_BYTES,
          ADMISSION_BACKEND_BYTES) ||
      !AdmissionMemoryMarkUatReady(
          &Context->Memory, ADMISSION_MEMORY_UAT_CONTEXT,
          APPLE_AGX_UAT_PAGE_SIZE_16K, ADMISSION_LOCAL_GPU_VA,
          ADMISSION_LOCAL_BYTES)) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Fail;
  }
  AdmissionMemoryRecordStart(Context, AdmissionMemoryStartComplete,
                             STATUS_SUCCESS);
  return STATUS_SUCCESS;

Fail:
  if (!NT_SUCCESS(AdmissionMemoryRuntimeDestroy(runtime))) {
    AdmissionMemoryRecordStart(
        Context, (ADMISSION_MEMORY_START_STAGE)Context->MemoryStartStage,
        STATUS_DEVICE_BUSY);
    return STATUS_DEVICE_BUSY;
  }
  ExFreePoolWithTag(runtime, ADMISSION_MEMORY_RUNTIME_TAG);
  Context->MemoryRuntime = NULL;
  RtlZeroMemory(&Context->Memory, sizeof(Context->Memory));
  AdmissionMemoryRecordStart(
      Context, (ADMISSION_MEMORY_START_STAGE)Context->MemoryStartStage,
      status);
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

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeBackendView(
    ADMISSION_CONTEXT *Context,
    ADMISSION_BACKEND_MEMORY_VIEW *View) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  ULONGLONG gpu_address = 0ULL;
  ULONGLONG bytes = 0ULL;
  ULONGLONG offset;

  if (View == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(View, sizeof(*View));
  if (runtime == NULL ||
      !AdmissionMemoryBackendRange(
          &Context->Memory, &gpu_address, &bytes))
    return STATUS_INVALID_DEVICE_STATE;
  offset = Context->Memory.BackendOffset;
  if (runtime->LocalObject.CpuAddress == NULL ||
      runtime->LocalObject.DeviceAddress == 0ULL ||
      runtime->LocalObject.GpuVirtualAddress == 0ULL ||
      offset > runtime->LocalObject.Length ||
      bytes > runtime->LocalObject.Length - offset ||
      runtime->LocalObject.DeviceAddress > MAXULONGLONG - offset ||
      runtime->LocalObject.GpuVirtualAddress > MAXULONGLONG - offset ||
      runtime->LocalObject.GpuVirtualAddress + offset != gpu_address)
    return STATUS_INVALID_ADDRESS;
  View->CpuAddress =
      (PUCHAR)runtime->LocalObject.CpuAddress + offset;
  View->HostPhysicalAddress =
      runtime->LocalObject.DeviceAddress + offset;
  View->GpuVirtualAddress = gpu_address;
  View->Bytes = bytes;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeResolveLocal(
    ADMISSION_CONTEXT *Context,
    ULONGLONG AllocationSegmentAddress,
    ULONGLONG AllocationSize,
    ULONGLONG AllocationOffset,
    ADMISSION_LOCAL_MEMORY_VIEW *View) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);

  if (View == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(View, sizeof(*View));
  if (runtime == NULL ||
      !AdmissionMemoryResolveLocalView(
          &Context->Memory, AllocationSegmentAddress, AllocationSize,
          AllocationOffset, runtime->LocalObject.CpuAddress,
          runtime->LocalObject.DeviceAddress, View))
    return STATUS_INVALID_ADDRESS;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeReadResident(
    ADMISSION_CONTEXT *Context, ULONG SegmentId,
    ULONGLONG AllocationSegmentAddress, ULONGLONG AllocationSize,
    ULONGLONG AllocationOffset, PVOID Destination, ULONG Bytes) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  NTSTATUS status = STATUS_SUCCESS;
  ULONGLONG apertureOffset = 0ULL;
  ULONGLONG copied = 0ULL;

  if (runtime == NULL || Destination == NULL || Bytes == 0u ||
      (SegmentId != ADMISSION_MEMORY_APERTURE_SEGMENT &&
       SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT))
    return STATUS_INVALID_PARAMETER;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  if (AllocationOffset > AllocationSize ||
      Bytes > AllocationSize - AllocationOffset)
    return STATUS_INVALID_ADDRESS;
  if (SegmentId == ADMISSION_MEMORY_APERTURE_SEGMENT) {
    if (AllocationSegmentAddress < Context->Memory.Topology.Aperture.Base)
      return STATUS_INVALID_ADDRESS;
    apertureOffset =
        AllocationSegmentAddress - Context->Memory.Topology.Aperture.Base;
    if (apertureOffset > Context->Memory.Topology.Aperture.Size ||
        AllocationSize >
            Context->Memory.Topology.Aperture.Size - apertureOffset)
      return STATUS_INVALID_ADDRESS;
  }

  ExAcquireFastMutex(&runtime->PagingLock);
  if (SegmentId == ADMISSION_MEMORY_LOCAL_SEGMENT) {
    ADMISSION_LOCAL_MEMORY_VIEW view;
    status = AdmissionMemoryRuntimeResolveLocal(
        Context, AllocationSegmentAddress, AllocationSize,
        AllocationOffset, &view);
    if (NT_SUCCESS(status)) {
      if (view.CpuAddress == NULL || Bytes > view.Bytes)
        status = STATUS_INVALID_ADDRESS;
      else
        RtlCopyMemory(Destination, view.CpuAddress, Bytes);
    }
  } else {
    while (copied < Bytes) {
      ULONGLONG position = apertureOffset + AllocationOffset + copied;
      ULONGLONG physical;
      ULONG chunk;
      SIZE_T actual = 0u;
      MM_COPY_ADDRESS source;
      if (AppleAgxSoftwareApertureResolve(
              &Context->Memory.Aperture, position, &physical) !=
          AppleAgxSoftwareApertureOk) {
        status = STATUS_INVALID_ADDRESS;
        break;
      }
      chunk = (ULONG)PAGE_SIZE - (ULONG)(position & (PAGE_SIZE - 1ULL));
      if (chunk > Bytes - (ULONG)copied)
        chunk = Bytes - (ULONG)copied;
      source.PhysicalAddress.QuadPart = (LONGLONG)physical;
      status = MmCopyMemory(
          (PUCHAR)Destination + (SIZE_T)copied, source, chunk,
          MM_COPY_MEMORY_PHYSICAL, &actual);
      if (!NT_SUCCESS(status) || actual != chunk) {
        if (NT_SUCCESS(status))
          status = STATUS_PARTIAL_COPY;
        break;
      }
      copied += chunk;
    }
  }
  KeMemoryBarrier();
  ExReleaseFastMutex(&runtime->PagingLock);
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeBorrowIo(
    ADMISSION_CONTEXT *Context, APPLE_AGX_MEMORY_IO *Io) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  if (Io == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(Io, sizeof(*Io));
  if (runtime == NULL || runtime->MemoryIo.Context != runtime ||
      runtime->MemoryIo.AllocateContiguous == NULL ||
      runtime->MemoryIo.FreeContiguous == NULL)
    return STATUS_INVALID_DEVICE_STATE;
  *Io = runtime->MemoryIo;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ BOOLEAN AdmissionMemoryRuntimeContextPublished(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  return runtime != NULL && runtime->PublicationReady &&
                 runtime->Published.Active != 0u &&
                 runtime->Published.Context == ADMISSION_MEMORY_UAT_CONTEXT &&
                 runtime->Published.PublishedTtbr0 != 0ULL &&
                 runtime->Published.PublishedTtbr1 != 0ULL
             ? TRUE
             : FALSE;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeScanoutView(
    ADMISSION_CONTEXT *Context, ADMISSION_SCANOUT_MEMORY_VIEW *View) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  ADMISSION_PHYSICAL_ALLOCATION *allocation;
  ULONGLONG offset;
  if (View == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(View, sizeof(*View));
  if (runtime == NULL || runtime->LocalObject.AllocationHandle == NULL ||
      runtime->LocalObject.CpuAddress == NULL ||
      runtime->LocalObject.DeviceAddress == 0ULL ||
      runtime->LocalObject.GpuVirtualAddress != ADMISSION_LOCAL_GPU_VA ||
      Context->Memory.LocalAllocationBytes !=
          ADMISSION_LOCAL_ALLOCATION_BYTES)
    return STATUS_INVALID_DEVICE_STATE;
  allocation = (ADMISSION_PHYSICAL_ALLOCATION *)
      runtime->LocalObject.AllocationHandle;
  if ((PUCHAR)runtime->LocalObject.CpuAddress <
      (PUCHAR)runtime->LocalObject.AllocationCpuBase)
    return STATUS_INVALID_ADDRESS;
  offset = (ULONGLONG)((PUCHAR)runtime->LocalObject.CpuAddress -
                       (PUCHAR)runtime->LocalObject.AllocationCpuBase);
  if (allocation->GuestIpaBase > MAXULONGLONG - offset)
    return STATUS_INTEGER_OVERFLOW;
  View->CpuAddress = runtime->LocalObject.CpuAddress;
  View->GuestIpaAddress = allocation->GuestIpaBase + offset;
  View->HostPhysicalAddress = runtime->LocalObject.DeviceAddress;
  View->GpuVirtualAddress = runtime->LocalObject.GpuVirtualAddress;
  View->Bytes = Context->Memory.LocalAllocationBytes;
  return STATUS_SUCCESS;
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
  Qualification->StartStage = (ULONG)InterlockedCompareExchange(
      &Context->MemoryStartStage, 0, 0);
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
      Qualification->StartStage != AdmissionMemoryStartComplete ||
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
      PageCount > Context->Memory.Aperture.PageCount ||
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
  result = AdmissionMemoryMapAperturePages(
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
    UINT PageCount, ULONGLONG DummyPage) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  APPLE_AGX_SOFTWARE_APERTURE_RESULT result;
  if (runtime == NULL)
    return STATUS_INVALID_DEVICE_STATE;
  ExAcquireFastMutex(&runtime->PagingLock);
  result = AdmissionMemoryUnmapAperturePages(
      &Context->Memory, ApertureByteOffset, PageCount, DummyPage);
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

typedef struct _ADMISSION_PRESENT_MEMORY_IO {
  ADMISSION_CONTEXT *Adapter;
  ADMISSION_LOCAL_MEMORY_VIEW Source, Destination;
  ULONGLONG ApertureOffset, SourceBytes;
  UINT SourceSegment;
  NTSTATUS Status;
} ADMISSION_PRESENT_MEMORY_IO;

static int AdmissionPresentReadMemory(void *Opaque, unsigned long long Offset,
    void *Bytes, unsigned int ByteCount) {
  ADMISSION_PRESENT_MEMORY_IO *io = Opaque;
  ULONGLONG physical, position;
  SIZE_T copied;
  UINT chunk;
  MM_COPY_ADDRESS source;
  if (Offset > io->SourceBytes || ByteCount > io->SourceBytes - Offset)
    return 0;
  if (io->SourceSegment == 2u) {
    RtlCopyMemory(Bytes, (PUCHAR)io->Source.CpuAddress + Offset, ByteCount);
    return 1;
  }
  while (ByteCount != 0u) {
    position = io->ApertureOffset + Offset;
    if (AppleAgxSoftwareApertureResolve(&io->Adapter->Memory.Aperture,
            position, &physical) != AppleAgxSoftwareApertureOk) {
      io->Status = STATUS_INVALID_ADDRESS;
      return 0;
    }
    chunk = (UINT)PAGE_SIZE - (UINT)(position & (PAGE_SIZE - 1ULL));
    if (chunk > ByteCount)
      chunk = ByteCount;
    source.PhysicalAddress.QuadPart = (LONGLONG)physical;
    copied = 0;
    io->Status = MmCopyMemory(Bytes, source, chunk, MM_COPY_MEMORY_PHYSICAL, &copied);
    if (!NT_SUCCESS(io->Status) || copied != chunk) {
      if (NT_SUCCESS(io->Status))
        io->Status = STATUS_PARTIAL_COPY;
      return 0;
    }
    Offset += chunk;
    Bytes = (PUCHAR)Bytes + chunk;
    ByteCount -= chunk;
  }
  return 1;
}

static int AdmissionPresentWriteMemory(void *Opaque, unsigned long long Offset,
    void *Bytes, unsigned int ByteCount) {
  ADMISSION_PRESENT_MEMORY_IO *io = Opaque;
  if (Offset > io->Destination.Bytes || ByteCount > io->Destination.Bytes - Offset)
    return 0;
  RtlCopyMemory((PUCHAR)io->Destination.CpuAddress + Offset, Bytes, ByteCount);
  return 1;
}

_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeExecutePresent(
    ADMISSION_CONTEXT *Context, const VOID *Command, UINT Bytes,
    ULONGLONG *BytesCopied) {
  ADMISSION_MEMORY_RUNTIME *runtime = AdmissionMemoryGetRuntime(Context);
  ADMISSION_PRESENT_BLT_COMMAND command;
  ADMISSION_PRESENT_MEMORY_IO io;
  ULONGLONG sourceAddress, destinationAddress, sourceBacking, destinationBacking;
  UINT destinationSegment, scratchBytes;
  PVOID scratch;
  NTSTATUS status;
  int completed;
  if (runtime == NULL || BytesCopied == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !AdmissionPresentBltValidate(Command, Bytes, 1, &command) ||
      !AdmissionPresentBltScratchBytes(&command, &scratchBytes))
    return STATUS_INVALID_PARAMETER;
  *BytesCopied = 0;
  RtlZeroMemory(&io, sizeof(io));
  io.Adapter = Context;
  io.Status = STATUS_SUCCESS;
  io.SourceBytes = command.SourceDescription.Size;
  if (!AdmissionPresentLocationDecode(command.SourceLocation, &io.SourceSegment, &sourceAddress) ||
      !AdmissionPresentLocationDecode(command.DestinationLocation, &destinationSegment, &destinationAddress) ||
      destinationSegment != 2u ||
      !AdmissionAllocationAlign64K(command.SourceDescription.Size, &sourceBacking) ||
      !AdmissionAllocationAlign64K(command.DestinationDescription.Size, &destinationBacking))
    return STATUS_INVALID_ADDRESS;
  status = AdmissionMemoryRuntimeResolveLocal(Context, destinationAddress,
      destinationBacking, 0ULL, &io.Destination);
  if (!NT_SUCCESS(status))
    return status;
  io.Destination.Bytes = command.DestinationDescription.Size;
  if (io.SourceSegment == 2u) {
    status = AdmissionMemoryRuntimeResolveLocal(Context, sourceAddress,
        sourceBacking, 0ULL, &io.Source);
    if (!NT_SUCCESS(status))
      return status;
  } else {
    if (sourceAddress < Context->Memory.Topology.Aperture.Base)
      return STATUS_INVALID_ADDRESS;
    io.ApertureOffset = sourceAddress - Context->Memory.Topology.Aperture.Base;
    if (io.ApertureOffset > Context->Memory.Topology.Aperture.Size ||
        io.SourceBytes > Context->Memory.Topology.Aperture.Size - io.ApertureOffset)
      return STATUS_INVALID_ADDRESS;
  }
  scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, scratchBytes, ADMISSION_MEMORY_RUNTIME_TAG);
  if (scratch == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  /* Map/unmap and the actual aperture reads share this existing owner lock.
   * Windows PFNs are IPAs, not the host PA used by AGX/UAT. */
  ExAcquireFastMutex(&runtime->PagingLock);
  completed = AdmissionPresentBltExecute(Command, Bytes,
      AdmissionPresentReadMemory, AdmissionPresentWriteMemory, &io,
      scratch, scratchBytes, BytesCopied);
  KeMemoryBarrier();
  ExReleaseFastMutex(&runtime->PagingLock);
  ExFreePoolWithTag(scratch, ADMISSION_MEMORY_RUNTIME_TAG);
  return completed ? STATUS_SUCCESS :
      NT_SUCCESS(io.Status) ? STATUS_INVALID_PARAMETER : io.Status;
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
