#include "apple_agx_driver.h"

_Use_decl_annotations_ NTSTATUS AppleAgxDdiAddDevice(
    PDEVICE_OBJECT PhysicalDeviceObject, PVOID *MiniportDeviceContext) {
  APPLE_AGX_ADAPTER *adapter;

  if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
    return STATUS_INVALID_PARAMETER;

#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordAddDeviceBoundary(PhysicalDeviceObject, AppleAgxAddEntered,
                                  STATUS_PENDING);
#endif

  adapter = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*adapter),
                            APPLE_AGX_POOL_TAG);
  if (adapter == NULL) {
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
    AppleAgxRecordAddDeviceBoundary(PhysicalDeviceObject, AppleAgxAddReturned,
                                    STATUS_INSUFFICIENT_RESOURCES);
#endif
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  RtlZeroMemory(adapter, sizeof(*adapter));
  adapter->PhysicalDeviceObject = PhysicalDeviceObject;
  AppleAgxStateInitialize(&adapter->State);
  *MiniportDeviceContext = adapter;
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordAddDeviceBoundary(PhysicalDeviceObject, AppleAgxAddReturned,
                                  STATUS_SUCCESS);
#endif
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

#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                    AppleAgxStartEntered, STATUS_SUCCESS);
#endif

  *NumberOfVideoPresentSources = 0;
  *NumberOfChildren = 0;
  RtlZeroMemory(&deviceInfo, sizeof(deviceInfo));
  status = DxgkInterface->DxgkCbGetDeviceInformation(
      DxgkInterface->DeviceHandle, &deviceInfo);
  if (!NT_SUCCESS(status)) {
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
    AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                      AppleAgxStartDeviceInformation, status);
#endif
    return status;
  }
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                    AppleAgxStartDeviceInformation,
                                    STATUS_SUCCESS);
  AppleAgxRecordTranslatedResources(adapter->PhysicalDeviceObject,
                                    deviceInfo.TranslatedResourceList);
#endif

  status =
      AppleAgxValidateTranslatedResources(deviceInfo.TranslatedResourceList);
  if (!NT_SUCCESS(status)) {
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
    AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                      AppleAgxStartResourcesValidated, status);
#endif
    return status;
  }
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                    AppleAgxStartResourcesValidated,
                                    STATUS_SUCCESS);
#endif

  if (!AppleAgxStateValidateResources(&adapter->State)) {
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
    AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                      AppleAgxStartStateValidated,
                                      STATUS_INVALID_DEVICE_STATE);
#endif
    return STATUS_INVALID_DEVICE_STATE;
  }
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                    AppleAgxStartStateValidated,
                                    STATUS_SUCCESS);
#endif

#ifdef APPLE_AGX_G2_POWER_QUALIFICATION
  {
    PHYSICAL_ADDRESS powerBrokerAddress;

    status = AppleAgxGetPowerBrokerAddress(deviceInfo.TranslatedResourceList,
                                           &powerBrokerAddress);
    AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                      AppleAgxStartBrokerAddress, status);
    if (NT_SUCCESS(status)) {
      status = AppleAgxQualifyPowerBroker(DxgkInterface, powerBrokerAddress);
      AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                        AppleAgxStartBrokerTransaction, status);
    }
    if (!NT_SUCCESS(status)) {
      AppleAgxStateInitialize(&adapter->State);
      return status;
    }
  }
#endif

#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION
  adapter->DxgkInterface = *DxgkInterface;
  adapter->DxgkInterfaceValid = TRUE;
  status = AppleAgxQualifyMmioMapping(&adapter->DxgkInterface,
                                      &adapter->MappingState);
  AppleAgxRecordMmioQualification(adapter->PhysicalDeviceObject,
                                  AppleAgxMmioMapped, status,
                                  &adapter->MappingState);
  if (NT_SUCCESS(status)) {
    AppleAgxRecordMmioQualification(adapter->PhysicalDeviceObject,
                                    AppleAgxMmioSubviewValidated,
                                    STATUS_SUCCESS, &adapter->MappingState);
    status = AppleAgxReleaseMmioMapping(&adapter->DxgkInterface,
                                        &adapter->MappingState);
    AppleAgxRecordMmioQualification(adapter->PhysicalDeviceObject,
                                    AppleAgxMmioUnmapped, status, NULL);
  }
  if (!NT_SUCCESS(status)) {
    AppleAgxStateInitialize(&adapter->State);
    return status;
  }
  RtlZeroMemory(&adapter->DxgkInterface, sizeof(adapter->DxgkInterface));
  adapter->DxgkInterfaceValid = FALSE;
#endif

#ifdef APPLE_AGX_G2_FIRMWARE_QUALIFICATION
  {
    ULONG cpuStatus = 0;
    NTSTATUS unmapStatus;

    adapter->DxgkInterface = *DxgkInterface;
    adapter->DxgkInterfaceValid = TRUE;
    status = AppleAgxQualifyMmioMapping(&adapter->DxgkInterface,
                                        &adapter->MappingState);
    if (NT_SUCCESS(status))
      status = AppleAgxQualifyAscCpuStatus(
          adapter->MappingState.AscBase, J313_AGX_G2_ASC_MMIO_SIZE, &cpuStatus);
    AppleAgxRecordAscCpuStatus(adapter->PhysicalDeviceObject, status,
                               cpuStatus);
    unmapStatus = AppleAgxReleaseMmioMapping(&adapter->DxgkInterface,
                                             &adapter->MappingState);
    if (NT_SUCCESS(status))
      status = unmapStatus;
    RtlZeroMemory(&adapter->DxgkInterface, sizeof(adapter->DxgkInterface));
    adapter->DxgkInterfaceValid = FALSE;
    if (!NT_SUCCESS(status)) {
      AppleAgxStateInitialize(&adapter->State);
      return status;
    }
  }
#endif

  /*
   * This remains enumeration-only.  A qualification build may execute the
   * bounded ON/QUERY/OFF broker receipt above, but firmware, UAT, queues and
   * render callbacks remain unavailable.
   */
  AppleAgxStateInitialize(&adapter->State);
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordStartDeviceBoundary(adapter->PhysicalDeviceObject,
                                    AppleAgxStartFailClosed,
                                    STATUS_NOT_SUPPORTED);
#endif
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxDdiStopDevice(PVOID MiniportDeviceContext) {
  APPLE_AGX_ADAPTER *adapter = (APPLE_AGX_ADAPTER *)MiniportDeviceContext;
  NTSTATUS status = STATUS_SUCCESS;
  if (adapter == NULL)
    return STATUS_INVALID_PARAMETER;
#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION)
  if (adapter->DxgkInterfaceValid) {
    status = AppleAgxReleaseMmioMapping(&adapter->DxgkInterface,
                                        &adapter->MappingState);
    if (!NT_SUCCESS(status))
      return status;
    RtlZeroMemory(&adapter->DxgkInterface, sizeof(adapter->DxgkInterface));
    adapter->DxgkInterfaceValid = FALSE;
  }
#endif
  AppleAgxStateInitialize(&adapter->State);
  return status;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxDdiRemoveDevice(PVOID MiniportDeviceContext) {
  APPLE_AGX_ADAPTER *adapter = (APPLE_AGX_ADAPTER *)MiniportDeviceContext;
  NTSTATUS status = STATUS_SUCCESS;
  if (adapter == NULL)
    return STATUS_INVALID_PARAMETER;
#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION)
  if (adapter->DxgkInterfaceValid) {
    status = AppleAgxReleaseMmioMapping(&adapter->DxgkInterface,
                                        &adapter->MappingState);
    if (!NT_SUCCESS(status))
      return status;
    RtlZeroMemory(&adapter->DxgkInterface, sizeof(adapter->DxgkInterface));
    adapter->DxgkInterfaceValid = FALSE;
  }
#endif
  AppleAgxStateInitialize(&adapter->State);
  ExFreePoolWithTag(adapter, APPLE_AGX_POOL_TAG);
  return status;
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
  if (adapter != NULL) {
#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION)
    if (adapter->DxgkInterfaceValid &&
        NT_SUCCESS(AppleAgxReleaseMmioMapping(&adapter->DxgkInterface,
                                              &adapter->MappingState))) {
      RtlZeroMemory(&adapter->DxgkInterface, sizeof(adapter->DxgkInterface));
      adapter->DxgkInterfaceValid = FALSE;
    }
#endif
    AppleAgxStateInitialize(&adapter->State);
  }
}

_Use_decl_annotations_ VOID AppleAgxDdiUnload(VOID) {}

_Use_decl_annotations_ NTSTATUS AppleAgxDdiQueryAdapterInfo(
    HANDLE Adapter, const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo) {
  UNREFERENCED_PARAMETER(Adapter);
  UNREFERENCED_PARAMETER(QueryAdapterInfo);
  return STATUS_NOT_SUPPORTED;
}
