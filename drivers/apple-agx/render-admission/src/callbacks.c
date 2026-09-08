#include "render_admission.h"

#define UNUSED(value) UNREFERENCED_PARAMETER(value)
#define FAIL2(name, type1, arg1, type2, arg2)                                 \
  _Use_decl_annotations_ NTSTATUS name(type1 arg1, type2 arg2) {             \
    UNUSED(arg1);                                                             \
    UNUSED(arg2);                                                             \
    return STATUS_NOT_SUPPORTED;                                              \
  }

_Use_decl_annotations_ NTSTATUS AdmissionDdiNotifyAcpiEvent(
    PVOID MiniportDeviceContext, DXGK_EVENT_TYPE EventType, ULONG Event,
    PVOID Argument, PULONG AcpiFlags) {
  UNUSED(MiniportDeviceContext);
  UNUSED(EventType);
  UNUSED(Event);
  UNUSED(Argument);
  if (AcpiFlags != NULL)
    *AcpiFlags = 0;
  return STATUS_NOT_SUPPORTED;
}

FAIL2(AdmissionDdiQueryInterface, PVOID, MiniportDeviceContext,
      PQUERY_INTERFACE, QueryInterface)

_Use_decl_annotations_ VOID AdmissionDdiControlEtwLogging(
    BOOLEAN Enable, ULONG Flags, UCHAR Level) {
  UNUSED(Enable);
  UNUSED(Flags);
  UNUSED(Level);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateDevice(
    HANDLE Adapter, DXGKARG_CREATEDEVICE *Args) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)Adapter;
  ADMISSION_DEVICE *device;
  ULONG flags;

  if (adapter == NULL || !adapter->Started || Args == NULL ||
      Args->Pasid != 0 || Args->hKmdProcess != NULL)
    return STATUS_INVALID_PARAMETER;
  flags = Args->Flags.Value;
  if ((flags & ~ADMISSION_DEVICE_VALID_FLAGS) != 0u)
    return STATUS_NOT_SUPPORTED;
  /* VidSch supplies no runtime device handle for its SystemDevice. The
   * returned driver-owned object still has the normal lifetime/ownership. */
  if (Args->hDevice == NULL && (flags & ADMISSION_DEVICE_SYSTEM) == 0u)
    return STATUS_INVALID_PARAMETER;
  device = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*device),
                           ADMISSION_POOL_TAG);
  if (device == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(device, sizeof(*device));
  if (!AdmissionObjectsCreateDevice(&adapter->ObjectAdapter, Args->hDevice,
                                    flags, &device->Object)) {
    ExFreePoolWithTag(device, ADMISSION_POOL_TAG);
    return STATUS_INVALID_PARAMETER;
  }
  Args->hDevice = device;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyDevice(HANDLE Device) {
  ADMISSION_DEVICE *device = (ADMISSION_DEVICE *)Device;
  if (device == NULL || !AdmissionObjectsDestroyDevice(&device->Object))
    return STATUS_DEVICE_BUSY;
  ExFreePoolWithTag(device, ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiPresent(
    HANDLE Context, DXGKARG_PRESENT *Present) {
  ADMISSION_DEVICE *device = NULL;
  ADMISSION_CONTEXT *adapter = NULL;
  ADMISSION_RENDER_CONTEXT *renderContext =
      (ADMISSION_RENDER_CONTEXT *)Context;
  ADMISSION_OPEN_ALLOCATION *source;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  NTSTATUS status;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  ADMISSION_STANDARD_PRESENT_EVENT traceEvent;
  RtlZeroMemory(&traceEvent, sizeof(traceEvent));
  traceEvent.Kind = AdmissionStandardPresentEventPresent;
  traceEvent.Phase = AdmissionStandardPresentPhaseEntry;
  traceEvent.ContextToken = (ULONGLONG)(ULONG_PTR)Context;
  traceEvent.Irql = KeGetCurrentIrql();
  if (Present != NULL) {
    traceEvent.Flags = Present->Flags.Value;
    traceEvent.NumSrc = Present->NumSrcAllocations;
    traceEvent.NumDst = Present->NumDstAllocations;
  }
#endif

  if (renderContext != NULL &&
      renderContext->Object.Magic == ADMISSION_OBJECT_CONTEXT_MAGIC &&
      renderContext->Object.Device != NULL &&
      renderContext->Object.Device->Magic == ADMISSION_OBJECT_DEVICE_MAGIC)
    device = CONTAINING_RECORD(renderContext->Object.Device,
                               ADMISSION_DEVICE, Object);
  else if (Context != NULL &&
           ((ADMISSION_DEVICE *)Context)->Object.Magic ==
               ADMISSION_OBJECT_DEVICE_MAGIC)
    device = (ADMISSION_DEVICE *)Context;

  if (device != NULL && device->Object.Adapter != NULL &&
      device->Object.Adapter->Magic == ADMISSION_OBJECT_ADAPTER_MAGIC)
    adapter = CONTAINING_RECORD(device->Object.Adapter,
                                ADMISSION_CONTEXT, ObjectAdapter);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  AdmissionStandardPresentTraceRecordWindows(adapter, &traceEvent);
#endif

  if (adapter != NULL)
    AdmissionFlushPresentTransfer(adapter);
  if (adapter != NULL)
    AdmissionFlushGdiReceipt(adapter);
  if (device != NULL && Present != NULL && Present->Flags.Value == 1u) {
    status = AdmissionPresentBlt(device, Context, Present);
    if (!NT_SUCCESS(status))
      AdmissionRecordPresent(device, Present, 4u, status);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    if (NT_SUCCESS(status) && Present->pAllocationInfo != NULL) {
      source = (ADMISSION_OPEN_ALLOCATION *)
          Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX]
              .hDeviceSpecificAllocation;
      if (source != NULL &&
          source->Magic == ADMISSION_OPEN_ALLOCATION_MAGIC &&
          source->Device == device && source->Allocation != NULL &&
          source->Allocation->Magic == ADMISSION_ALLOCATION_OBJECT_MAGIC)
        traceEvent.AllocationToken =
            (ULONGLONG)(ULONG_PTR)source->Allocation;
    }
    traceEvent.Phase = AdmissionStandardPresentPhaseExit;
    traceEvent.Status = (ULONG)status;
    AdmissionStandardPresentTraceRecordWindows(adapter, &traceEvent);
#endif
    return status;
  }

  if (device == NULL || Present == NULL || Present->pDmaBuffer != NULL ||
      Present->Flags.Value != 0x4u || Present->pAllocationInfo == NULL ||
      Present->NumSrcAllocations != 1u || Present->NumDstAllocations != 0u ||
      Present->pPrivateDriverData != NULL ||
      Present->PrivateDriverDataSize != 0u ||
      Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX]
              .hDeviceSpecificAllocation != NULL) {
    AdmissionRecordPresent(device, Present, 1u, STATUS_INVALID_PARAMETER);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    traceEvent.Phase = AdmissionStandardPresentPhaseExit;
    traceEvent.Status = (ULONG)STATUS_INVALID_PARAMETER;
    AdmissionStandardPresentTraceRecordWindows(adapter, &traceEvent);
#endif
    return STATUS_INVALID_PARAMETER;
  }
  source = (ADMISSION_OPEN_ALLOCATION *)
      Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX]
          .hDeviceSpecificAllocation;
  if (source == NULL || source->Magic != ADMISSION_OPEN_ALLOCATION_MAGIC ||
      source->Device != device || source->Allocation == NULL ||
      source->Allocation->Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC) {
    AdmissionRecordPresent(device, Present, 2u, STATUS_INVALID_HANDLE);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    traceEvent.Phase = AdmissionStandardPresentPhaseExit;
    traceEvent.Status = (ULONG)STATUS_INVALID_HANDLE;
    AdmissionStandardPresentTraceRecordWindows(adapter, &traceEvent);
#endif
    return STATUS_INVALID_HANDLE;
  }
  description = &source->Allocation->Description;
  if (!AdmissionAllocationDescriptionValid(description) ||
      description->Width != 2560u || description->Height != 1600u ||
      description->Pitch != 10240u || description->BytesPerPixel != 4u ||
      description->Size != APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      description->Format != (UINT)D3DDDIFMT_A8R8G8B8) {
    AdmissionRecordPresent(device, Present, 3u,
                            STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
    traceEvent.Phase = AdmissionStandardPresentPhaseExit;
    traceEvent.Status =
        (ULONG)STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    AdmissionStandardPresentTraceRecordWindows(adapter, &traceEvent);
#endif
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
  }
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  traceEvent.Phase = AdmissionStandardPresentPhaseExit;
  traceEvent.Status = (ULONG)STATUS_SUCCESS;
  traceEvent.AllocationToken = (ULONGLONG)(ULONG_PTR)source->Allocation;
  AdmissionStandardPresentTraceRecordWindows(adapter, &traceEvent);
#endif
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiStopCapture(
    HANDLE Adapter, const DXGKARG_STOPCAPTURE *StopCapture) {
  UNUSED(Adapter);
  UNUSED(StopCapture);
  /* No capture engine or capture allocations exist, so nothing is active. */
  return STATUS_SUCCESS;
}

FAIL2(AdmissionDdiCreateOverlay, HANDLE, Adapter, DXGKARG_CREATEOVERLAY *, Args)
FAIL2(AdmissionDdiUpdateOverlay, HANDLE, Overlay, const DXGKARG_UPDATEOVERLAY *,
      Args)
FAIL2(AdmissionDdiFlipOverlay, HANDLE, Overlay, const DXGKARG_FLIPOVERLAY *,
      Args)

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyOverlay(HANDLE Overlay) {
  UNUSED(Overlay);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiEscape(
    HANDLE Adapter, const DXGKARG_ESCAPE *Args) {
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  ULONG magic;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  ADMISSION_PRESENT_QUERY *query;
#endif
  if (context == NULL || !context->Started || Args == NULL ||
      Args->pPrivateDriverData == NULL || Args->PrivateDriverDataSize < sizeof(magic))
    return STATUS_INVALID_PARAMETER;
  magic = *(const ULONG *)Args->pPrivateDriverData;
  if (magic == ADMISSION_STANDARD_PRESENT_TRACE_MAGIC &&
      Args->PrivateDriverDataSize == sizeof(ADMISSION_STANDARD_PRESENT_TRACE))
    return AdmissionStandardPresentTraceQueryWindows(
        context, (ADMISSION_STANDARD_PRESENT_TRACE *)Args->pPrivateDriverData);
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  if (magic == ADMISSION_RETIREMENT_QUERY_MAGIC &&
      Args->PrivateDriverDataSize == sizeof(ADMISSION_RETIREMENT_QUERY))
    return AdmissionScanoutRetireQualification(
        context, (ADMISSION_RETIREMENT_QUERY *)Args->pPrivateDriverData);
  if (magic != ADMISSION_PRESENT_QUERY_MAGIC ||
      Args->PrivateDriverDataSize != sizeof(*query))
    return STATUS_INVALID_PARAMETER;
  query = (ADMISSION_PRESENT_QUERY *)Args->pPrivateDriverData;
  return AdmissionScanoutQueryQualification(context, query);
#else
  return STATUS_INVALID_PARAMETER;
#endif
#else
  UNREFERENCED_PARAMETER(Adapter);
  UNREFERENCED_PARAMETER(Args);
  return STATUS_NOT_SUPPORTED;
#endif
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateContext(
    HANDLE Device, DXGKARG_CREATECONTEXT *Args) {
  ADMISSION_DEVICE *device = (ADMISSION_DEVICE *)Device;
  ADMISSION_CONTEXT *adapter;
  ADMISSION_RENDER_CONTEXT *context;
  KIRQL oldIrql;
  ULONG flags;

  if (device == NULL ||
      device->Object.Magic != ADMISSION_OBJECT_DEVICE_MAGIC || Args == NULL ||
      Args->pPrivateDriverData != NULL ||
      Args->PrivateDriverDataSize != 0u)
    return STATUS_INVALID_PARAMETER;
  flags = Args->Flags.Value;
  if ((flags & ~ADMISSION_CONTEXT_VALID_FLAGS) != 0u)
    return STATUS_NOT_SUPPORTED;
  /* VidSch also supplies no runtime handle for its paging SystemContext. */
  if (Args->hContext == NULL && (flags & ADMISSION_CONTEXT_SYSTEM) == 0u)
    return STATUS_INVALID_PARAMETER;
  context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context),
                            ADMISSION_POOL_TAG);
  if (context == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(context, sizeof(*context));
  AppleAgxSchedulerContextInitialize(&context->SchedulerContext);
  AdmissionPrepatchedInitialize(&context->PrepatchedRender);
  if (!AdmissionObjectsCreateContext(
          &device->Object, Args->hContext, Args->NodeOrdinal,
          Args->EngineAffinity, flags, &context->Object)) {
    ExFreePoolWithTag(context, ADMISSION_POOL_TAG);
    return STATUS_INVALID_PARAMETER;
  }
  adapter = CONTAINING_RECORD(device->Object.Adapter, ADMISSION_CONTEXT,
                              ObjectAdapter);
  KeAcquireSpinLock(&adapter->SchedulerLock, &oldIrql);
  if (!AppleAgxSchedulerCreateContext(
          &adapter->Scheduler, &context->SchedulerContext,
          Args->NodeOrdinal, Args->EngineAffinity)) {
    KeReleaseSpinLock(&adapter->SchedulerLock, oldIrql);
    (void)AdmissionObjectsDestroyContext(&context->Object);
    ExFreePoolWithTag(context, ADMISSION_POOL_TAG);
    return STATUS_INVALID_DEVICE_STATE;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, oldIrql);

  RtlZeroMemory(&Args->ContextInfo, sizeof(Args->ContextInfo));
  Args->ContextInfo.DmaBufferSize = ADMISSION_DMA_BUFFER_SIZE;
  Args->ContextInfo.DmaBufferSegmentSet = 0u;
  Args->ContextInfo.DmaBufferPrivateDataSize =
      ADMISSION_GDI_DMA_PRIVATE_SIZE;
  if (Args->Flags.GdiContext) {
    Args->ContextInfo.AllocationListSize =
        ADMISSION_GDI_ALLOCATION_LIST_SIZE;
    Args->ContextInfo.PatchLocationListSize =
        ADMISSION_GDI_PATCH_LIST_SIZE;
  } else {
    Args->ContextInfo.AllocationListSize = ADMISSION_ALLOCATION_LIST_SIZE;
    Args->ContextInfo.PatchLocationListSize = ADMISSION_PATCH_LIST_SIZE;
  }
  Args->hContext = context;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyContext(HANDLE Context) {
  ADMISSION_RENDER_CONTEXT *context = (ADMISSION_RENDER_CONTEXT *)Context;
  ADMISSION_CONTEXT *adapter;
  KIRQL oldIrql;
  if (context == NULL ||
      context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL || context->Object.Device->Adapter == NULL ||
      context->Object.FenceOutstanding != 0u ||
      AdmissionPrepatchedActive(&context->PrepatchedRender))
    return STATUS_DEVICE_BUSY;
  adapter = CONTAINING_RECORD(context->Object.Device->Adapter,
                              ADMISSION_CONTEXT, ObjectAdapter);
  KeAcquireSpinLock(&adapter->SchedulerLock, &oldIrql);
  if (!AppleAgxSchedulerDestroyContext(
          &adapter->Scheduler, &context->SchedulerContext)) {
    KeReleaseSpinLock(&adapter->SchedulerLock, oldIrql);
    return STATUS_DEVICE_BUSY;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, oldIrql);
  if (!AdmissionObjectsDestroyContext(&context->Object))
    return STATUS_DEVICE_BUSY;
  ExFreePoolWithTag(context, ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}


_Use_decl_annotations_ NTSTATUS AdmissionDdiSetPowerComponentFState(
    PVOID MiniportDeviceContext, UINT ComponentIndex, UINT FState) {
  UNUSED(MiniportDeviceContext);
  UNUSED(ComponentIndex);
  UNUSED(FState);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiPowerRuntimeControlRequest(
    PVOID MiniportDeviceContext, LPCGUID PowerControlCode, PVOID InBuffer,
    SIZE_T InBufferSize, PVOID OutBuffer, SIZE_T OutBufferSize,
    PSIZE_T BytesReturned) {
  UNUSED(MiniportDeviceContext);
  UNUSED(PowerControlCode);
  UNUSED(InBuffer);
  UNUSED(InBufferSize);
  UNUSED(OutBuffer);
  UNUSED(OutBufferSize);
  if (BytesReturned != NULL)
    *BytesReturned = 0;
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiGetNodeMetadata(
    HANDLE Adapter, UINT NodeOrdinal, DXGKARG_GETNODEMETADATA *NodeMetadata) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  NTSTATUS status;

  if (context == NULL || NodeMetadata == NULL)
    return STATUS_INVALID_PARAMETER;
  if (NodeOrdinal != 0)
    return STATUS_INVALID_PARAMETER;

  RtlZeroMemory(NodeMetadata, sizeof(*NodeMetadata));
  NodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
  status = RtlStringCchCopyW(NodeMetadata->FriendlyName,
                             RTL_NUMBER_OF(NodeMetadata->FriendlyName),
                             L"Apple AGX 3D node");
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptNodeMetadata, status);
  if (!NT_SUCCESS(status))
    return status;
  return STATUS_SUCCESS;
}

FAIL2(AdmissionDdiSubmitCommandVirtual, HANDLE, Adapter,
      const DXGKARG_SUBMITCOMMANDVIRTUAL *, Args)
FAIL2(AdmissionDdiCreateProcess, PVOID, MiniportDeviceContext,
      DXGKARG_CREATEPROCESS *, Args)

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyProcess(
    PVOID MiniportDeviceContext, HANDLE KmdProcessHandle) {
  UNUSED(MiniportDeviceContext);
  UNUSED(KmdProcessHandle);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCalibrateGpuClock(
    HANDLE Adapter, UINT32 NodeOrdinal, UINT32 EngineOrdinal,
    DXGKARG_CALIBRATEGPUCLOCK *ClockCalibration) {
  UNUSED(Adapter);
  UNUSED(NodeOrdinal);
  UNUSED(EngineOrdinal);
  UNUSED(ClockCalibration);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ VOID AdmissionDdiSetStablePowerState(
    HANDLE Adapter, const DXGKARG_SETSTABLEPOWERSTATE *StablePowerState) {
  UNUSED(Adapter);
  UNUSED(StablePowerState);
}

#undef FAIL2
#undef UNUSED
