#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionDdiAddDevice(
    PDEVICE_OBJECT PhysicalDeviceObject, PVOID *MiniportDeviceContext) {
  ADMISSION_CONTEXT *context;
  if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
    return STATUS_INVALID_PARAMETER;
  AdmissionRecordDevice(PhysicalDeviceObject, AdmissionReceiptAddEntered,
                        STATUS_PENDING);
  context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context),
                            ADMISSION_POOL_TAG);
  if (context == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(context, sizeof(*context));
  context->PhysicalDeviceObject = PhysicalDeviceObject;
  *MiniportDeviceContext = context;
  AdmissionRecordDevice(PhysicalDeviceObject, AdmissionReceiptAddSucceeded,
                        STATUS_SUCCESS);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiStartDevice(
    PVOID MiniportDeviceContext, PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface, PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  DXGK_DEVICE_INFO deviceInformation;
  NTSTATUS status;

  if (context == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
      NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
    return STATUS_INVALID_PARAMETER;
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartEntered, STATUS_PENDING);
  *NumberOfVideoPresentSources = 0;
  *NumberOfChildren = 0;
  RtlZeroMemory(&deviceInformation, sizeof(deviceInformation));
  status = DxgkInterface->DxgkCbGetDeviceInformation(
      DxgkInterface->DeviceHandle, &deviceInformation);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartDeviceInfo, status);
  if (!NT_SUCCESS(status))
    return status;
  context->Interface = *DxgkInterface;
  context->InterfaceValid = TRUE;
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartSucceeded, STATUS_SUCCESS);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiStopDevice(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  AdmissionRecordDevice(context->PhysicalDeviceObject, AdmissionReceiptStop,
                        STATUS_SUCCESS);
  RtlZeroMemory(&context->Interface, sizeof(context->Interface));
  context->InterfaceValid = FALSE;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRemoveDevice(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  AdmissionRecordDevice(context->PhysicalDeviceObject, AdmissionReceiptRemove,
                        STATUS_SUCCESS);
  ExFreePoolWithTag(context, ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryAdapterInfo(
    HANDLE Adapter, const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  NTSTATUS status = STATUS_NOT_SUPPORTED;
  if (context == NULL || QueryAdapterInfo == NULL)
    return STATUS_INVALID_PARAMETER;
  if (QueryAdapterInfo->Type == DXGKQAITYPE_DRIVERCAPS) {
    DXGK_DRIVERCAPS *caps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      caps = (DXGK_DRIVERCAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(caps, sizeof(*caps));
      caps->HighestAcceptableAddress.QuadPart = -1;
      caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1;
      caps->FlipCaps.FlipOnVSyncMmIo = TRUE;
      status = STATUS_SUCCESS;
    }
  } else if (QueryAdapterInfo->Type == DXGKQAITYPE_WDDMDEVICECAPS) {
    DXGK_WDDMDEVICECAPS *caps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      caps = (DXGK_WDDMDEVICECAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(caps, sizeof(*caps));
      caps->WDDMVersion = DXGKDDI_WDDMv3_0;
      status = STATUS_SUCCESS;
    }
  }
  AdmissionRecordQuery(context->PhysicalDeviceObject, QueryAdapterInfo->Type,
                       QueryAdapterInfo->OutputDataSize, status);
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiDispatchIoRequest(
    PVOID MiniportDeviceContext, ULONG VidPnSourceId,
    PVIDEO_REQUEST_PACKET VideoRequestPacket) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(VidPnSourceId);
  UNREFERENCED_PARAMETER(VideoRequestPacket);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryChildRelations(
    PVOID MiniportDeviceContext, PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(ChildRelations);
  UNREFERENCED_PARAMETER(ChildRelationsSize);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryChildStatus(
    PVOID MiniportDeviceContext, PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(ChildStatus);
  UNREFERENCED_PARAMETER(NonDestructiveOnly);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryDeviceDescriptor(
    PVOID MiniportDeviceContext, ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(ChildUid);
  UNREFERENCED_PARAMETER(DeviceDescriptor);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetPowerState(
    PVOID MiniportDeviceContext, ULONG DeviceUid,
    DEVICE_POWER_STATE DevicePowerState, POWER_ACTION ActionType) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(DeviceUid);
  UNREFERENCED_PARAMETER(DevicePowerState);
  UNREFERENCED_PARAMETER(ActionType);
  return STATUS_SUCCESS;
}

VOID AdmissionDdiResetDevice(PVOID MiniportDeviceContext) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

VOID AdmissionDdiUnload(VOID) {}
