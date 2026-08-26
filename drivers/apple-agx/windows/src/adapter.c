#include "apple_agx_driver.h"

_Use_decl_annotations_ NTSTATUS AppleAgxDdiAddDevice(
    PDEVICE_OBJECT PhysicalDeviceObject, PVOID *MiniportDeviceContext) {
  APPLE_AGX_ADAPTER *adapter;

  if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
    return STATUS_INVALID_PARAMETER;

  adapter = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*adapter),
                            APPLE_AGX_POOL_TAG);
  if (adapter == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;

  RtlZeroMemory(adapter, sizeof(*adapter));
  adapter->PhysicalDeviceObject = PhysicalDeviceObject;
  AppleAgxStateInitialize(&adapter->State);
  *MiniportDeviceContext = adapter;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiStartDevice(
    PVOID MiniportDeviceContext, PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface, PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren) {
  APPLE_AGX_ADAPTER *adapter = (APPLE_AGX_ADAPTER *)MiniportDeviceContext;
  DXGK_DEVICE_INFO deviceInfo;
  NTSTATUS status;

  if (adapter == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
      NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
    return STATUS_INVALID_PARAMETER;

  *NumberOfVideoPresentSources = 0;
  *NumberOfChildren = 0;
  RtlZeroMemory(&deviceInfo, sizeof(deviceInfo));
  status = DxgkInterface->DxgkCbGetDeviceInformation(
      DxgkInterface->DeviceHandle, &deviceInfo);
  if (!NT_SUCCESS(status))
    return status;

  status =
      AppleAgxValidateTranslatedResources(deviceInfo.TranslatedResourceList);
  if (!NT_SUCCESS(status))
    return status;

  if (!AppleAgxStateValidateResources(&adapter->State))
    return STATUS_INVALID_DEVICE_STATE;

  /*
   * G2 Task 3 is intentionally enumeration-only.  It validates the immutable
   * resource contract but refuses to start until firmware, UAT, queue and TDR
   * ownership are implemented.  In particular, this path performs no MMIO.
   */
  AppleAgxStateInitialize(&adapter->State);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxDdiStopDevice(PVOID MiniportDeviceContext) {
  APPLE_AGX_ADAPTER *adapter = (APPLE_AGX_ADAPTER *)MiniportDeviceContext;
  if (adapter == NULL)
    return STATUS_INVALID_PARAMETER;
  AppleAgxStateInitialize(&adapter->State);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxDdiRemoveDevice(PVOID MiniportDeviceContext) {
  APPLE_AGX_ADAPTER *adapter = (APPLE_AGX_ADAPTER *)MiniportDeviceContext;
  if (adapter == NULL)
    return STATUS_INVALID_PARAMETER;
  AppleAgxStateInitialize(&adapter->State);
  ExFreePoolWithTag(adapter, APPLE_AGX_POOL_TAG);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxDdiDispatchIoRequest(PVOID MiniportDeviceContext, ULONG VidPnSourceId,
                             PVIDEO_REQUEST_PACKET VideoRequestPacket) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(VidPnSourceId);
  UNREFERENCED_PARAMETER(VideoRequestPacket);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ BOOLEAN
AppleAgxDdiInterruptRoutine(PVOID MiniportDeviceContext, ULONG MessageNumber) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(MessageNumber);
  return FALSE;
}

_Use_decl_annotations_ VOID AppleAgxDdiDpcRoutine(PVOID MiniportDeviceContext) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiQueryChildRelations(
    PVOID MiniportDeviceContext, PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(ChildRelations);
  UNREFERENCED_PARAMETER(ChildRelationsSize);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiQueryChildStatus(
    PVOID MiniportDeviceContext, PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(ChildStatus);
  UNREFERENCED_PARAMETER(NonDestructiveOnly);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxDdiQueryDeviceDescriptor(PVOID MiniportDeviceContext, ULONG ChildUid,
                                 PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(ChildUid);
  UNREFERENCED_PARAMETER(DeviceDescriptor);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiSetPowerState(
    PVOID MiniportDeviceContext, ULONG DeviceUid,
    DEVICE_POWER_STATE DevicePowerState, POWER_ACTION ActionType) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(DeviceUid);
  UNREFERENCED_PARAMETER(DevicePowerState);
  UNREFERENCED_PARAMETER(ActionType);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID
AppleAgxDdiResetDevice(PVOID MiniportDeviceContext) {
  APPLE_AGX_ADAPTER *adapter = (APPLE_AGX_ADAPTER *)MiniportDeviceContext;
  if (adapter != NULL)
    AppleAgxStateInitialize(&adapter->State);
}

_Use_decl_annotations_ VOID AppleAgxDdiUnload(VOID) {}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiQueryAdapterInfo(
    HANDLE Adapter, const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo) {
  UNREFERENCED_PARAMETER(Adapter);
  UNREFERENCED_PARAMETER(QueryAdapterInfo);
  return STATUS_NOT_SUPPORTED;
}
