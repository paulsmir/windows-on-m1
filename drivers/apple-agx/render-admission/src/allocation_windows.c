#include "render_admission.h"

#define ADMISSION_LOCAL_SEGMENT_SET \
  (1u << (ADMISSION_MEMORY_LOCAL_SEGMENT - 1u))
#define ADMISSION_APERTURE_SEGMENT_SET \
  (1u << (ADMISSION_MEMORY_APERTURE_SEGMENT - 1u))
#define ADMISSION_CPU_VISIBLE_SEGMENT_SET \
  (ADMISSION_APERTURE_SEGMENT_SET | ADMISSION_LOCAL_SEGMENT_SET)

C_ASSERT(D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE ==
         ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE);
C_ASSERT(D3DDDIFMT_A8 == ADMISSION_WIN32_ALLOCATION_FORMAT_A8);

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
static VOID AdmissionOpenAllocationTraceWrite(
    ADMISSION_CONTEXT *Context, ULONG Field, ULONG Value) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(
      request, AdmissionOpenAllocationTraceWord(Field, Value));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

static BOOLEAN AdmissionOpenAllocationTraceBegin(
    ADMISSION_CONTEXT *Context, const ADMISSION_DEVICE *Device,
    const DXGKARG_OPENALLOCATION *Args) {
  const DXGK_OPENALLOCATIONINFO *info = NULL;
  if (Context == NULL || Device == NULL || Context->BrokerBase == NULL ||
      InterlockedCompareExchange(
          &Context->OpenAllocationTraceClaimed, 1, 0) != 0)
    return FALSE;
  if (Args != NULL && Args->NumAllocations != 0u &&
      Args->pOpenAllocation != NULL)
    info = &Args->pOpenAllocation[0];
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceVersion, 1u);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceIrql,
      (ULONG)KeGetCurrentIrql());
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceDeviceFlags,
      Device->Object.Flags);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceCount,
      Args == NULL ? 0u : Args->NumAllocations);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceFlags,
      Args == NULL ? 0u : Args->Flags.Value);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceSubresource,
      Args == NULL ? 0u : Args->SubresourceIndex);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceHandle,
      info == NULL ? 0u : info->hAllocation);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTracePrivateSize,
      info == NULL ? 0u : info->PrivateDriverDataSize);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceDeviceSpecificPresent,
      info != NULL && info->hDeviceSpecificAllocation != NULL ? 1u : 0u);
  return TRUE;
}

static VOID AdmissionOpenAllocationTraceResult(
    ADMISSION_CONTEXT *Context, BOOLEAN Enabled, ULONG Guard,
    NTSTATUS Status) {
  if (!Enabled || Context == NULL)
    return;
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceGuard, Guard);
  AdmissionOpenAllocationTraceWrite(
      Context, AdmissionOpenAllocationTraceStatus, (ULONG)Status);
}
#else
#define AdmissionOpenAllocationTraceBegin(Context, Device, Args)             \
  ((void)(Context), (void)(Device), (void)(Args), FALSE)
#define AdmissionOpenAllocationTraceResult(Context, Enabled, Guard, Status)  \
  do {                                                                       \
    (void)(Context);                                                         \
    (void)(Enabled);                                                         \
    (void)(Guard);                                                           \
    (void)(Status);                                                          \
  } while (0)
#endif

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
  D3DKMDT_SHADOWSURFACEDATA *shadow = NULL;
  ADMISSION_ALLOCATION_DESCRIPTION description;
  ULONG bytesPerPixel;
  UINT suppliedBytes;
  BOOLEAN cpuVisible;

  if (Adapter == NULL || StandardAllocation == NULL ||
      (StandardAllocation->StandardAllocationType !=
           D3DKMDT_STANDARDALLOCATION_GDISURFACE &&
       StandardAllocation->StandardAllocationType !=
           D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE &&
       StandardAllocation->StandardAllocationType !=
           D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE) ||
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
  } else if (StandardAllocation->StandardAllocationType ==
             D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE) {
    shadow = StandardAllocation->pCreateShadowSurfaceData;
    if (shadow == NULL || shadow->Format != D3DDDIFMT_A8R8G8B8 ||
        !AdmissionAllocationDescribe(
            shadow->Width, shadow->Height, 4u,
            (UINT)D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE,
            (UINT)shadow->Format, 1u, &description))
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
  if (shadow != NULL)
    shadow->Pitch = description.Pitch;
  RtlCopyMemory(StandardAllocation->pAllocationPrivateDriverData,
                &description, sizeof(description));
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateAllocation(
    HANDLE Adapter, DXGKARG_CREATEALLOCATION *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  DXGK_ALLOCATIONINFO *info;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  ADMISSION_ALLOCATION_DESCRIPTION parsedDescription;
  ADMISSION_WIN32_TRANSPORT_RESULT parseResult;
  ADMISSION_ALLOCATION_HANDLE *allocation;
  ULONGLONG aligned;
  ULONG classId = 0u;
  ULONG flags = 0u;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  ADMISSION_ALLOCATION_DESCRIPTION normalized;
  BOOLEAN correlated = FALSE;
#endif

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
  if (info->pPrivateDriverData == NULL)
    return STATUS_INVALID_PARAMETER;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (info->PrivateDriverDataSize == sizeof(normalized) &&
      ((const ADMISSION_ALLOCATION_DESCRIPTION *)
           info->pPrivateDriverData)->Reserved ==
          ADMISSION_UMD_CORRELATION_COOKIE) {
    normalized = *(const ADMISSION_ALLOCATION_DESCRIPTION *)
        info->pPrivateDriverData;
    normalized.Reserved = 0u;
    correlated = TRUE;
    parseResult = AdmissionWin32AllocationCreateValidate(
        &normalized, sizeof(normalized), &parsedDescription,
        &classId, &flags);
  } else {
    parseResult = AdmissionWin32AllocationCreateValidate(
        info->pPrivateDriverData, info->PrivateDriverDataSize,
        &parsedDescription, &classId, &flags);
  }
#else
  parseResult = AdmissionWin32AllocationCreateValidate(
      info->pPrivateDriverData, info->PrivateDriverDataSize,
      &parsedDescription, &classId, &flags);
#endif
  description = &parsedDescription;
  if (parseResult != AdmissionWin32TransportSuccess ||
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
  allocation->Win32ClassId = classId;
  allocation->Win32Flags = flags;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  allocation->QualificationCookie = correlated
      ? ADMISSION_UMD_CORRELATION_COOKIE : 0u;
#endif
  info->Alignment = (UINT)ADMISSION_ALLOCATION_ALIGNMENT;
  info->Size = (SIZE_T)aligned;
  info->PitchAlignedSize = (SIZE_T)aligned;
  info->HintedBank.Value = 0u;
  info->PreferredSegment.Value = 0u;
  info->PreferredSegment.SegmentId0 = ADMISSION_MEMORY_LOCAL_SEGMENT;
  info->SupportedReadSegmentSet = description->CpuVisible != 0u
                                      ? ADMISSION_CPU_VISIBLE_SEGMENT_SET
                                      : ADMISSION_LOCAL_SEGMENT_SET;
  info->SupportedWriteSegmentSet = info->SupportedReadSegmentSet;
  info->EvictionSegmentSet = 0u;
  info->hAllocation = allocation;
  info->FlagsWddm2.Value = 0u;
  info->FlagsWddm2.CpuVisible = description->CpuVisible != 0u;
  info->FlagsWddm2.AccessedPhysically = 1u;
  info->pAllocationUsageHint = NULL;
  info->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  info->Flags2.Value = 0u;
  info->PhysicalAdapterIndex = 0u;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyAllocation(
    HANDLE Adapter, const DXGKARG_DESTROYALLOCATION *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  UINT index;
  if (context == NULL || Args == NULL || Args->hResource != NULL ||
      Args->NumAllocations == 0u || Args->pAllocationList == NULL)
    return STATUS_INVALID_PARAMETER;
  for (index = 0u; index < Args->NumAllocations; ++index) {
    ADMISSION_ALLOCATION_HANDLE *allocation =
        (ADMISSION_ALLOCATION_HANDLE *)Args->pAllocationList[index];
    if (allocation == NULL ||
        allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC)
      return STATUS_DEVICE_BUSY;
  }
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  for (index = 0u; index < Args->NumAllocations; ++index) {
    ADMISSION_ALLOCATION_HANDLE *allocation =
        (ADMISSION_ALLOCATION_HANDLE *)Args->pAllocationList[index];
    NTSTATUS retire = AdmissionScanoutRetireAllocation(
        context, &allocation->Object);
    if (!NT_SUCCESS(retire))
      return retire;
  }
#endif
  for (index = 0u; index < Args->NumAllocations; ++index) {
    ADMISSION_ALLOCATION_HANDLE *allocation =
        (ADMISSION_ALLOCATION_HANDLE *)Args->pAllocationList[index];
    if (allocation->Object.OpenCount != 0u)
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
  DXGKARGCB_RELEASEHANDLEDATA reference;
  NTSTATUS status = STATUS_INVALID_PARAMETER;
  ULONG guard = AdmissionOpenAllocationGuardArgs;
  UINT index;
  BOOLEAN trace;

#define OPEN_ALLOCATION_RETURN(value, result)                                \
  do {                                                                       \
    NTSTATUS openStatus = (result);                                          \
    AdmissionOpenAllocationTraceResult(adapter, trace, (value), openStatus); \
    return openStatus;                                                       \
  } while (0)

  if (device == NULL || device->Object.Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      device->Object.Adapter == NULL)
    return STATUS_INVALID_PARAMETER;
  adapter = CONTAINING_RECORD(device->Object.Adapter, ADMISSION_CONTEXT,
                              ObjectAdapter);
  trace = AdmissionOpenAllocationTraceBegin(adapter, device, Args);
  if (Args == NULL || Args->NumAllocations == 0u ||
      Args->pOpenAllocation == NULL || Args->pPrivateDriverData != NULL ||
      Args->PrivateDriverSize != 0u)
    OPEN_ALLOCATION_RETURN(AdmissionOpenAllocationGuardArgs,
                           STATUS_INVALID_PARAMETER);
  if (!adapter->InterfaceValid ||
      adapter->Interface.DxgkCbAcquireHandleData == NULL ||
      adapter->Interface.DxgkCbReleaseHandleData == NULL)
    OPEN_ALLOCATION_RETURN(AdmissionOpenAllocationGuardInterface,
                           STATUS_INVALID_DEVICE_STATE);
  RtlZeroMemory(&reference, sizeof(reference));
  reference.Type = DXGK_HANDLE_ALLOCATION;
  for (index = 0u; index < Args->NumAllocations; ++index) {
    DXGK_OPENALLOCATIONINFO *info = &Args->pOpenAllocation[index];
    DXGKARGCB_GETHANDLEDATA query;
    const ADMISSION_ALLOCATION_DESCRIPTION *description;
    ADMISSION_ALLOCATION_DESCRIPTION parsedDescription;
    ADMISSION_WIN32_TRANSPORT_RESULT parseResult;
    ULONG classId = 0u;
    ULONG flags = 0u;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    const ADMISSION_ALLOCATION_DESCRIPTION *submittedDescription;
    ADMISSION_ALLOCATION_DESCRIPTION normalized;
#endif
    ADMISSION_ALLOCATION_HANDLE *allocation;
    ADMISSION_OPEN_ALLOCATION *opened;
    if (info->hDeviceSpecificAllocation != NULL ||
        info->pPrivateDriverData == NULL) {
      guard = AdmissionOpenAllocationGuardPrivate;
      goto Rollback;
    }
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    submittedDescription = info->PrivateDriverDataSize ==
        sizeof(ADMISSION_ALLOCATION_DESCRIPTION)
        ? (const ADMISSION_ALLOCATION_DESCRIPTION *)info->pPrivateDriverData
        : NULL;
    if (submittedDescription != NULL &&
        submittedDescription->Reserved == ADMISSION_UMD_CORRELATION_COOKIE) {
      normalized = *submittedDescription;
      normalized.Reserved = 0u;
      parseResult = AdmissionWin32AllocationCreateValidate(
          &normalized, sizeof(normalized), &parsedDescription,
          &classId, &flags);
    } else {
      parseResult = AdmissionWin32AllocationCreateValidate(
          info->pPrivateDriverData, info->PrivateDriverDataSize,
          &parsedDescription, &classId, &flags);
    }
#else
    parseResult = AdmissionWin32AllocationCreateValidate(
        info->pPrivateDriverData, info->PrivateDriverDataSize,
        &parsedDescription, &classId, &flags);
#endif
    if (parseResult != AdmissionWin32TransportSuccess) {
      guard = AdmissionOpenAllocationGuardPrivate;
      goto Rollback;
    }
    description = &parsedDescription;
    /* hAllocation is a dxgkrnl token, not the KMD object returned at Create. */
    RtlZeroMemory(&query, sizeof(query));
    query.hObject = info->hAllocation;
    query.Type = DXGK_HANDLE_ALLOCATION;
    allocation = (ADMISSION_ALLOCATION_HANDLE *)
        adapter->Interface.DxgkCbAcquireHandleData(
            &query, &reference.ReleaseHandle);
    if (allocation == NULL || reference.ReleaseHandle == NULL ||
        allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC) {
      status = STATUS_INVALID_HANDLE;
      guard = AdmissionOpenAllocationGuardAcquire;
      goto Rollback;
    }
    if (!AdmissionAllocationDescriptionValid(description) ||
        !AdmissionAllocationDescriptionValid(&allocation->Object.Description) ||
        RtlCompareMemory(description, &allocation->Object.Description,
                         sizeof(*description)) != sizeof(*description) ||
        allocation->Win32ClassId != classId ||
        allocation->Win32Flags != flags ||
        !AdmissionAllocationOpen(&allocation->Object)) {
      guard = AdmissionOpenAllocationGuardDescription;
      goto Rollback;
    }
    opened = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*opened),
                             ADMISSION_POOL_TAG);
    if (opened == NULL) {
      (void)AdmissionAllocationClose(&allocation->Object);
      guard = AdmissionOpenAllocationGuardPool;
      goto Rollback;
    }
    RtlZeroMemory(opened, sizeof(*opened));
    opened->Magic = ADMISSION_OPEN_ALLOCATION_MAGIC;
    opened->Device = device;
    opened->RuntimeAllocation = info->hAllocation;
    opened->Allocation = &allocation->Object;
    opened->ReadOnly = Args->Flags.ReadOnly ? TRUE : FALSE;
    opened->Win32Generation = (ULONG)InterlockedCompareExchange(
        &device->Win32Generation, 0, 0);
    opened->Win32ClassId = allocation->Win32ClassId;
    opened->Win32Flags = allocation->Win32Flags;
    info->hDeviceSpecificAllocation = opened;
    ++device->Object.AllocationCount;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    AdmissionUmdRenderTraceArm(adapter);
    if (submittedDescription != NULL &&
        allocation->QualificationCookie == ADMISSION_UMD_CORRELATION_COOKIE &&
        submittedDescription->Reserved == ADMISSION_UMD_CORRELATION_COOKIE) {
      InterlockedExchange(&adapter->PagingCorrelationArmed, 1);
    }
#endif
    /* The returned binding follows Close-before-Destroy lifetime. Only this
       lookup needs the transient dxgkrnl reference; do not retain a cycle. */
    adapter->Interface.DxgkCbReleaseHandleData(reference);
    reference.ReleaseHandle = NULL;
  }
  OPEN_ALLOCATION_RETURN(AdmissionOpenAllocationGuardAccepted,
                         STATUS_SUCCESS);

Rollback:
  if (reference.ReleaseHandle != NULL)
    adapter->Interface.DxgkCbReleaseHandleData(reference);
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
  AdmissionOpenAllocationTraceResult(adapter, trace, guard, status);
  return status;
#undef OPEN_ALLOCATION_RETURN
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
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    ADMISSION_CONTEXT *adapter = CONTAINING_RECORD(
        device->Object.Adapter, ADMISSION_CONTEXT, ObjectAdapter);
    ADMISSION_ALLOCATION_HANDLE *owner = CONTAINING_RECORD(
        opened->Allocation, ADMISSION_ALLOCATION_HANDLE, Object);
    if (owner->QualificationCookie == ADMISSION_UMD_CORRELATION_COOKIE) {
      InterlockedExchange(&adapter->PagingCorrelationArmed, 0);
    }
    AdmissionUmdRenderTraceDisarm(adapter);
#endif
    (void)AdmissionAllocationClose(opened->Allocation);
    opened->Magic = 0u;
    ExFreePoolWithTag(opened, ADMISSION_POOL_TAG);
    --device->Object.AllocationCount;
  }
  return STATUS_SUCCESS;
}
