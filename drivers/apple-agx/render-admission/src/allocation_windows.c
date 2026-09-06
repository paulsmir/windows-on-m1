#include "render_admission.h"

#define ADMISSION_LOCAL_SEGMENT_SET \
  (1u << (ADMISSION_MEMORY_LOCAL_SEGMENT - 1u))

static BOOLEAN AdmissionFormatBytesPerPixel(D3DDDIFORMAT Format,
                                             PULONG BytesPerPixel) {
  if (BytesPerPixel == NULL)
    return FALSE;
  switch (Format) {
  case D3DDDIFMT_A8B8G8R8:
  case D3DDDIFMT_X8B8G8R8:
  case D3DDDIFMT_A8R8G8B8:
  case D3DDDIFMT_X8R8G8B8:
    *BytesPerPixel = 4u;
    return TRUE;
  case D3DDDIFMT_A8:
    *BytesPerPixel = 1u;
    return TRUE;
  default:
    *BytesPerPixel = 0u;
    return FALSE;
  }
}

static BOOLEAN AdmissionSurfaceTypeSupported(D3DKMDT_GDISURFACETYPE Type,
                                              D3DDDIFORMAT Format,
                                              PBOOLEAN CpuVisible) {
  if (CpuVisible == NULL)
    return FALSE;
  *CpuVisible = FALSE;
  switch (Type) {
  case D3DKMDT_GDISURFACE_TEXTURE:
  case D3DKMDT_GDISURFACE_STAGING:
    break;
  case D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE:
  case D3DKMDT_GDISURFACE_EXISTINGSYSMEM:
    *CpuVisible = TRUE;
    break;
  case D3DKMDT_GDISURFACE_LOOKUPTABLE:
    return Format == D3DDDIFMT_A8;
  default:
    return FALSE;
  }
  if (Format == D3DDDIFMT_A8)
    return Type == D3DKMDT_GDISURFACE_STAGING ||
           Type == D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE;
  return TRUE;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiGetStandardAllocationDriverData(
    HANDLE Adapter,
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *StandardAllocation) {
  D3DKMDT_GDISURFACEDATA *surface = NULL;
  ADMISSION_ALLOCATION_DESCRIPTION description;
  ULONG bytesPerPixel;
  UINT suppliedBytes;
  BOOLEAN cpuVisible;

  if (Adapter == NULL || StandardAllocation == NULL ||
      (StandardAllocation->StandardAllocationType !=
           D3DKMDT_STANDARDALLOCATION_GDISURFACE &&
       StandardAllocation->StandardAllocationType !=
           D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE) ||
      StandardAllocation->PhysicalAdapterIndex != 0u ||
      StandardAllocation->pResourcePrivateDriverData != NULL)
    return STATUS_INVALID_PARAMETER;
  suppliedBytes = StandardAllocation->AllocationPrivateDriverDataSize;
  StandardAllocation->AllocationPrivateDriverDataSize = sizeof(description);
  StandardAllocation->ResourcePrivateDriverDataSize = 0u;
  /* The sizing phase must not modify the standard creation-data union. */
  if (StandardAllocation->pAllocationPrivateDriverData == NULL)
    return STATUS_SUCCESS;
  if (suppliedBytes < sizeof(description))
    return STATUS_BUFFER_TOO_SMALL;

  if (StandardAllocation->StandardAllocationType ==
      D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE) {
    const D3DKMDT_SHAREDPRIMARYSURFACEDATA *primary =
        StandardAllocation->pCreateSharedPrimarySurfaceData;
    /* Windows tags/pins the primary. Its backing uses our existing linear
       local-segment allocation, not a new resource or platform owner. */
    if (primary == NULL || primary->VidPnSourceId != 0u ||
        primary->Width != APPLE_AGX_SCANOUT_J313_WIDTH ||
        primary->Height != APPLE_AGX_SCANOUT_J313_HEIGHT ||
        primary->Format != D3DDDIFMT_A8R8G8B8 ||
        !AdmissionAllocationDescribe(
            primary->Width, primary->Height, 4u,
            (UINT)D3DKMDT_GDISURFACE_TEXTURE, (UINT)primary->Format,
            0u, &description))
      return STATUS_INVALID_PARAMETER;
  } else {
    surface = StandardAllocation->pCreateGdiSurfaceData;
    if (surface == NULL || surface->Flags.Value != 0u ||
        !AdmissionFormatBytesPerPixel(surface->Format, &bytesPerPixel) ||
        !AdmissionSurfaceTypeSupported(surface->Type, surface->Format,
                                       &cpuVisible) ||
        !AdmissionAllocationDescribe(
            surface->Width, surface->Height, bytesPerPixel,
            (UINT)surface->Type, (UINT)surface->Format,
            cpuVisible ? 1u : 0u, &description))
      return STATUS_INVALID_PARAMETER;
  }
  if (surface != NULL)
    surface->Pitch = description.Pitch;
  RtlCopyMemory(StandardAllocation->pAllocationPrivateDriverData,
                &description, sizeof(description));
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateAllocation(
    HANDLE Adapter, DXGKARG_CREATEALLOCATION *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  DXGK_ALLOCATIONINFO *info;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  ADMISSION_ALLOCATION_HANDLE *allocation;
  ULONGLONG aligned;

  if (context == NULL || Args == NULL || Args->NumAllocations != 1u ||
      Args->pAllocationInfo == NULL || Args->PrivateDriverDataSize != 0u ||
      Args->pPrivateDriverData != NULL ||
      Args->hResource != NULL)
    return STATUS_INVALID_PARAMETER;
  /* Resource grouping needs no additional KMD handle: the one allocation
     object below owns all private state and its existing open-count lifetime. */
  if (!AdmissionMemoryReady(&context->Memory))
    return STATUS_NOT_SUPPORTED;
  info = &Args->pAllocationInfo[0];
  if (info->pPrivateDriverData == NULL ||
      info->PrivateDriverDataSize != sizeof(*description))
    return STATUS_INVALID_PARAMETER;
  description = (const ADMISSION_ALLOCATION_DESCRIPTION *)
      info->pPrivateDriverData;
  if (!AdmissionAllocationDescriptionValid(description) ||
      !AdmissionAllocationAlign64K(description->Size, &aligned) ||
      aligned > MAXSIZE_T)
    return STATUS_INVALID_PARAMETER;
  allocation = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*allocation),
                               ADMISSION_POOL_TAG);
  if (allocation == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(allocation, sizeof(*allocation));
  if (!AdmissionAllocationCreate(description, &allocation->Object)) {
    ExFreePoolWithTag(allocation, ADMISSION_POOL_TAG);
    return STATUS_INVALID_PARAMETER;
  }
  info->Alignment = (UINT)ADMISSION_ALLOCATION_ALIGNMENT;
  info->Size = (SIZE_T)aligned;
  info->PitchAlignedSize = (SIZE_T)aligned;
  info->HintedBank.Value = 0u;
  info->PreferredSegment.Value = 0u;
  info->PreferredSegment.SegmentId0 = ADMISSION_MEMORY_LOCAL_SEGMENT;
  info->SupportedReadSegmentSet = ADMISSION_LOCAL_SEGMENT_SET;
  info->SupportedWriteSegmentSet = ADMISSION_LOCAL_SEGMENT_SET;
  info->EvictionSegmentSet = 0u;
  info->hAllocation = allocation;
  info->FlagsWddm2.Value = 0u;
  info->FlagsWddm2.AccessedPhysically = 1u;
  info->pAllocationUsageHint = NULL;
  info->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  info->Flags2.Value = 0u;
  info->PhysicalAdapterIndex = 0u;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyAllocation(
    HANDLE Adapter, const DXGKARG_DESTROYALLOCATION *Args) {
  UINT index;
  if (Adapter == NULL || Args == NULL || Args->hResource != NULL ||
      Args->NumAllocations == 0u || Args->pAllocationList == NULL)
    return STATUS_INVALID_PARAMETER;
  for (index = 0u; index < Args->NumAllocations; ++index) {
    ADMISSION_ALLOCATION_HANDLE *allocation =
        (ADMISSION_ALLOCATION_HANDLE *)Args->pAllocationList[index];
    if (allocation == NULL ||
        allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC ||
        allocation->Object.OpenCount != 0u)
      return STATUS_DEVICE_BUSY;
  }
  for (index = 0u; index < Args->NumAllocations; ++index) {
    ADMISSION_ALLOCATION_HANDLE *allocation =
        (ADMISSION_ALLOCATION_HANDLE *)Args->pAllocationList[index];
    (void)AdmissionAllocationDestroy(&allocation->Object);
    ExFreePoolWithTag(allocation, ADMISSION_POOL_TAG);
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiDescribeAllocation(
    HANDLE Adapter, DXGKARG_DESCRIBEALLOCATION *Args) {
  ADMISSION_ALLOCATION_HANDLE *allocation;
  if (Adapter == NULL || Args == NULL)
    return STATUS_INVALID_PARAMETER;
  allocation = (ADMISSION_ALLOCATION_HANDLE *)Args->hAllocation;
  if (allocation == NULL ||
      !AdmissionAllocationDescriptionValid(&allocation->Object.Description))
    return STATUS_INVALID_PARAMETER;
  Args->Width = allocation->Object.Description.Width;
  Args->Height = allocation->Object.Description.Height;
  Args->Format = (D3DDDIFORMAT)allocation->Object.Description.Format;
  RtlZeroMemory(&Args->MultisampleMethod, sizeof(Args->MultisampleMethod));
  Args->RefreshRate.Numerator = 0u;
  Args->RefreshRate.Denominator = 1u;
  Args->PrivateDriverFormatAttribute = 0u;
  Args->Flags.Value = 0u;
  Args->Rotation = D3DDDI_ROTATION_IDENTITY;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiOpenAllocation(
    HANDLE Device, const DXGKARG_OPENALLOCATION *Args) {
  ADMISSION_DEVICE *device = (ADMISSION_DEVICE *)Device;
  ADMISSION_CONTEXT *adapter;
  NTSTATUS status = STATUS_INVALID_PARAMETER;
  UINT index;
  if (device == NULL || device->Object.Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      device->Object.Adapter == NULL ||
      Args == NULL || Args->NumAllocations == 0u ||
      Args->pOpenAllocation == NULL || Args->pPrivateDriverData != NULL ||
      Args->PrivateDriverSize != 0u)
    return STATUS_INVALID_PARAMETER;
  adapter = CONTAINING_RECORD(device->Object.Adapter, ADMISSION_CONTEXT,
                              ObjectAdapter);
  if (!adapter->InterfaceValid ||
      adapter->Interface.DxgkCbGetHandleData == NULL)
    return STATUS_INVALID_DEVICE_STATE;
  for (index = 0u; index < Args->NumAllocations; ++index) {
    DXGK_OPENALLOCATIONINFO *info = &Args->pOpenAllocation[index];
    DXGKARGCB_GETHANDLEDATA query;
    const ADMISSION_ALLOCATION_DESCRIPTION *description;
    ADMISSION_ALLOCATION_HANDLE *allocation;
    ADMISSION_OPEN_ALLOCATION *opened;
    if (info->hDeviceSpecificAllocation != NULL ||
        info->pPrivateDriverData == NULL ||
        info->PrivateDriverDataSize != sizeof(*description))
      goto Rollback;
    description = (const ADMISSION_ALLOCATION_DESCRIPTION *)
        info->pPrivateDriverData;
    /* hAllocation is a dxgkrnl token, not the KMD object returned at Create. */
    RtlZeroMemory(&query, sizeof(query));
    query.hObject = info->hAllocation;
    query.Type = DXGK_HANDLE_ALLOCATION;
    allocation = (ADMISSION_ALLOCATION_HANDLE *)
        adapter->Interface.DxgkCbGetHandleData(&query);
    if (allocation == NULL ||
        allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC) {
      status = STATUS_INVALID_HANDLE;
      goto Rollback;
    }
    if (!AdmissionAllocationDescriptionValid(description) ||
        !AdmissionAllocationDescriptionValid(&allocation->Object.Description) ||
        RtlCompareMemory(description, &allocation->Object.Description,
                         sizeof(*description)) != sizeof(*description) ||
        !AdmissionAllocationOpen(&allocation->Object))
      goto Rollback;
    opened = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*opened),
                             ADMISSION_POOL_TAG);
    if (opened == NULL) {
      (void)AdmissionAllocationClose(&allocation->Object);
      goto Rollback;
    }
    RtlZeroMemory(opened, sizeof(*opened));
    opened->Magic = ADMISSION_OPEN_ALLOCATION_MAGIC;
    opened->Device = device;
    opened->RuntimeAllocation = info->hAllocation;
    opened->Allocation = &allocation->Object;
    opened->ReadOnly = Args->Flags.ReadOnly ? TRUE : FALSE;
    info->hDeviceSpecificAllocation = opened;
    ++device->Object.AllocationCount;
  }
  return STATUS_SUCCESS;

Rollback:
  while (index != 0u) {
    ADMISSION_OPEN_ALLOCATION *opened;
    --index;
    opened = (ADMISSION_OPEN_ALLOCATION *)
        Args->pOpenAllocation[index].hDeviceSpecificAllocation;
    if (opened != NULL) {
      (void)AdmissionAllocationClose(opened->Allocation);
      opened->Magic = 0u;
      ExFreePoolWithTag(opened, ADMISSION_POOL_TAG);
      Args->pOpenAllocation[index].hDeviceSpecificAllocation = NULL;
      --device->Object.AllocationCount;
    }
  }
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCloseAllocation(
    HANDLE Device, const DXGKARG_CLOSEALLOCATION *Args) {
  ADMISSION_DEVICE *device = (ADMISSION_DEVICE *)Device;
  UINT index;
  if (device == NULL || device->Object.Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      Args == NULL || Args->NumAllocations == 0u ||
      Args->pOpenHandleList == NULL)
    return STATUS_INVALID_PARAMETER;
  for (index = 0u; index < Args->NumAllocations; ++index) {
    ADMISSION_OPEN_ALLOCATION *opened =
        (ADMISSION_OPEN_ALLOCATION *)Args->pOpenHandleList[index];
    if (opened == NULL || opened->Magic != ADMISSION_OPEN_ALLOCATION_MAGIC ||
        opened->Device != device || opened->Allocation == NULL ||
        opened->Allocation->OpenCount == 0u)
      return STATUS_INVALID_PARAMETER;
  }
  for (index = 0u; index < Args->NumAllocations; ++index) {
    ADMISSION_OPEN_ALLOCATION *opened =
        (ADMISSION_OPEN_ALLOCATION *)Args->pOpenHandleList[index];
    (void)AdmissionAllocationClose(opened->Allocation);
    opened->Magic = 0u;
    ExFreePoolWithTag(opened, ADMISSION_POOL_TAG);
    --device->Object.AllocationCount;
  }
  return STATUS_SUCCESS;
}
