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
      Args->hDevice == NULL || Args->Pasid != 0 || Args->hKmdProcess != NULL)
    return STATUS_INVALID_PARAMETER;
  flags = Args->Flags.Value;
  if ((flags & ~ADMISSION_DEVICE_VALID_FLAGS) != 0u)
    return STATUS_NOT_SUPPORTED;
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

FAIL2(AdmissionDdiCreateAllocation, HANDLE, Adapter,
      DXGKARG_CREATEALLOCATION *, Args)
FAIL2(AdmissionDdiDestroyAllocation, HANDLE, Adapter,
      const DXGKARG_DESTROYALLOCATION *, Args)
FAIL2(AdmissionDdiDescribeAllocation, HANDLE, Adapter,
      DXGKARG_DESCRIBEALLOCATION *, Args)
FAIL2(AdmissionDdiGetStandardAllocationDriverData, HANDLE, Adapter,
      DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *, Args)
FAIL2(AdmissionDdiOpenAllocation, HANDLE, Device,
      const DXGKARG_OPENALLOCATION *, Args)
FAIL2(AdmissionDdiCloseAllocation, HANDLE, Device,
      const DXGKARG_CLOSEALLOCATION *, Args)
FAIL2(AdmissionDdiPatch, HANDLE, Adapter, const DXGKARG_PATCH *, Args)
FAIL2(AdmissionDdiSubmitCommand, HANDLE, Adapter,
      const DXGKARG_SUBMITCOMMAND *, Args)
FAIL2(AdmissionDdiBuildPagingBuffer, HANDLE, Adapter,
      DXGKARG_BUILDPAGINGBUFFER *, Args)
FAIL2(AdmissionDdiPreemptCommand, HANDLE, Adapter,
      const DXGKARG_PREEMPTCOMMAND *, Args)
FAIL2(AdmissionDdiRender, HANDLE, Context, DXGKARG_RENDER *, Args)

_Use_decl_annotations_ NTSTATUS AdmissionDdiPresent(
    HANDLE Context, DXGKARG_PRESENT *Present) {
  UNUSED(Context);
  if (Present == NULL || Present->pDmaBuffer != NULL)
    return STATUS_INVALID_PARAMETER;
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiResetFromTimeout(HANDLE Adapter) {
  UNUSED(Adapter);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRestartFromTimeout(HANDLE Adapter) {
  UNUSED(Adapter);
  return STATUS_NOT_SUPPORTED;
}

FAIL2(AdmissionDdiEscape, HANDLE, Adapter, const DXGKARG_ESCAPE *, Args)
FAIL2(AdmissionDdiCollectDbgInfo, HANDLE, Adapter,
      const DXGKARG_COLLECTDBGINFO *, Args)
FAIL2(AdmissionDdiQueryCurrentFence, HANDLE, Adapter,
      DXGKARG_QUERYCURRENTFENCE *, Args)

_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateContext(
    HANDLE Device, DXGKARG_CREATECONTEXT *Args) {
  ADMISSION_DEVICE *device = (ADMISSION_DEVICE *)Device;
  ADMISSION_RENDER_CONTEXT *context;
  ULONG flags;

  if (device == NULL ||
      device->Object.Magic != ADMISSION_OBJECT_DEVICE_MAGIC || Args == NULL ||
      Args->hContext == NULL || Args->pPrivateDriverData != NULL ||
      Args->PrivateDriverDataSize != 0u)
    return STATUS_INVALID_PARAMETER;
  flags = Args->Flags.Value;
  if ((flags & ~ADMISSION_CONTEXT_VALID_FLAGS) != 0u)
    return STATUS_NOT_SUPPORTED;
  context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context),
                            ADMISSION_POOL_TAG);
  if (context == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(context, sizeof(*context));
  if (!AdmissionObjectsCreateContext(
          &device->Object, Args->hContext, Args->NodeOrdinal,
          Args->EngineAffinity, flags, &context->Object)) {
    ExFreePoolWithTag(context, ADMISSION_POOL_TAG);
    return STATUS_INVALID_PARAMETER;
  }

  RtlZeroMemory(&Args->ContextInfo, sizeof(Args->ContextInfo));
  Args->ContextInfo.DmaBufferSize = ADMISSION_DMA_BUFFER_SIZE;
  Args->ContextInfo.DmaBufferSegmentSet = 0u;
  if (Args->Flags.GdiContext) {
    Args->ContextInfo.DmaBufferPrivateDataSize =
        ADMISSION_GDI_DMA_PRIVATE_SIZE;
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
  if (context == NULL || !AdmissionObjectsDestroyContext(&context->Object))
    return STATUS_DEVICE_BUSY;
  ExFreePoolWithTag(context, ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}

FAIL2(AdmissionDdiRenderKm, HANDLE, Context, DXGKARG_RENDER *, Args)
FAIL2(AdmissionDdiQueryDependentEngineGroup, HANDLE, Adapter,
      DXGKARG_QUERYDEPENDENTENGINEGROUP *, Args)
FAIL2(AdmissionDdiQueryEngineStatus, HANDLE, Adapter,
      DXGKARG_QUERYENGINESTATUS *, Args)
FAIL2(AdmissionDdiResetEngine, HANDLE, Adapter, DXGKARG_RESETENGINE *, Args)
FAIL2(AdmissionDdiCancelCommand, HANDLE, Adapter,
      const DXGKARG_CANCELCOMMAND *, Args)

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
