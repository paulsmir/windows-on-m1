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
  AdmissionObjectsInitializeAdapter(&context->ObjectAdapter);
  context->FeatureReadyMask =
      APPLE_AGX_WDDM_READY_WDDM3_IDENTITY |
      APPLE_AGX_WDDM_READY_DEVICE_CONTEXT;
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
  NTSTATUS status;

  if (context == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
      NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
    return STATUS_INVALID_PARAMETER;
  *NumberOfVideoPresentSources = 0;
  *NumberOfChildren = 0;
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartEntered, STATUS_PENDING);
  context->StartInfo = *DxgkStartInfo;
  context->Interface = *DxgkInterface;
  context->InterfaceValid = TRUE;
  RtlZeroMemory(&context->DeviceInformation,
                sizeof(context->DeviceInformation));
  status = context->Interface.DxgkCbGetDeviceInformation(
      context->Interface.DeviceHandle, &context->DeviceInformation);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartDeviceInfo, status);
  if (!NT_SUCCESS(status))
    return status;

  status = AdmissionInterruptStart(context);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartInterrupt, status);
  if (!NT_SUCCESS(status))
    return status;

  status = AdmissionMemoryRuntimeStart(context);
  if (!NT_SUCCESS(status)) {
#if defined(APPLE_AGX_RENDER_MEMORY_QUALIFICATION)
    ADMISSION_MEMORY_QUALIFICATION qualification;
    NTSTATUS interruptStatus;
    RtlZeroMemory(&qualification, sizeof(qualification));
    qualification.Version = ADMISSION_MEMORY_QUALIFICATION_VERSION;
    qualification.Size = sizeof(qualification);
    qualification.QualificationStatus = status;
    qualification.CleanupStatus = context->MemoryRuntime == NULL
                                      ? STATUS_SUCCESS
                                      : STATUS_DEVICE_BUSY;
    qualification.StartStage = (ULONG)InterlockedCompareExchange(
        &context->MemoryStartStage, 0, 0);
    interruptStatus = AdmissionInterruptStop(context);
    AdmissionRecordDevice(context->PhysicalDeviceObject,
                          AdmissionReceiptMemoryQualified, status);
    AdmissionRecordMemoryQualification(context->PhysicalDeviceObject,
                                       &qualification);
    if (!NT_SUCCESS(interruptStatus))
      return interruptStatus;
#else
    (void)AdmissionInterruptStop(context);
#endif
    return status;
  }
#if defined(APPLE_AGX_RENDER_MEMORY_QUALIFICATION)
  {
    ADMISSION_MEMORY_QUALIFICATION qualification;
    NTSTATUS cleanupStatus;
    NTSTATUS interruptStatus;

    status = AdmissionMemoryRuntimeQualify(context, &qualification);
    cleanupStatus = AdmissionMemoryRuntimeStop(context);
    qualification.CleanupStatus = cleanupStatus;
    interruptStatus = AdmissionInterruptStop(context);
    AdmissionRecordDevice(context->PhysicalDeviceObject,
                          AdmissionReceiptMemoryQualified, status);
    AdmissionRecordMemoryQualification(context->PhysicalDeviceObject,
                                       &qualification);
    RtlZeroMemory(&context->Interface, sizeof(context->Interface));
    context->InterfaceValid = FALSE;
    if (!NT_SUCCESS(status))
      return status;
    if (!NT_SUCCESS(cleanupStatus))
      return cleanupStatus;
    if (!NT_SUCCESS(interruptStatus))
      return interruptStatus;
    return STATUS_NOT_SUPPORTED;
  }
#endif
  status = AdmissionSchedulerStart(context);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  status = AdmissionPagingStart(context);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }

  if (context->Interface.DxgkCbAcquirePostDisplayOwnership == NULL) {
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_NOT_SUPPORTED;
  }
  RtlZeroMemory(&context->PostDisplayInformation,
                sizeof(context->PostDisplayInformation));
  status = context->Interface.DxgkCbAcquirePostDisplayOwnership(
      context->Interface.DeviceHandle, &context->PostDisplayInformation);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartPostDisplay, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  if (context->PostDisplayInformation.PhysicAddress.QuadPart == 0 ||
      context->PostDisplayInformation.Width != 2560 ||
      context->PostDisplayInformation.Height != 1600 ||
      context->PostDisplayInformation.Pitch != 10240) {
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER;
  }

  context->Started = TRUE;
  if (!AdmissionObjectsStartAdapter(&context->ObjectAdapter)) {
    context->Started = FALSE;
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_INVALID_DEVICE_STATE;
  }
  context->DisplayActive = TRUE;
  context->SourceVisible = TRUE;
  context->CommittedWidth = 2560;
  context->CommittedHeight = 1600;
  context->CommittedStride = 10240;
  context->CommittedFormat = D3DDDIFMT_A8R8G8B8;
  *NumberOfVideoPresentSources = 1;
  *NumberOfChildren = 1;
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartSucceeded, STATUS_SUCCESS);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiStopDevice(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  NTSTATUS status;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  AdmissionRecordDevice(context->PhysicalDeviceObject, AdmissionReceiptStop,
                        STATUS_SUCCESS);
  if (context->ObjectAdapter.DeviceCount != 0u)
    return STATUS_DEVICE_BUSY;
  status = AdmissionPagingStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionSchedulerStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionMemoryRuntimeStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionInterruptStop(context);
  if (!NT_SUCCESS(status))
    return status;
  if (!AdmissionObjectsStopAdapter(&context->ObjectAdapter))
    return STATUS_DEVICE_BUSY;
  context->Started = FALSE;
  context->DisplayActive = FALSE;
  context->SourceVisible = FALSE;
  RtlZeroMemory(&context->StartInfo, sizeof(context->StartInfo));
  RtlZeroMemory(&context->DeviceInformation,
                sizeof(context->DeviceInformation));
  RtlZeroMemory(&context->PostDisplayInformation,
                sizeof(context->PostDisplayInformation));
  RtlZeroMemory(&context->Interface, sizeof(context->Interface));
  context->InterfaceValid = FALSE;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRemoveDevice(PVOID MiniportDeviceContext) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  if (context->PagingWorkItem != NULL &&
      !NT_SUCCESS(AdmissionPagingStop(context)))
    return STATUS_DEVICE_BUSY;
  if (InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) != 0 &&
      !NT_SUCCESS(AdmissionSchedulerStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->MemoryRuntime != NULL &&
      !NT_SUCCESS(AdmissionMemoryRuntimeStop(context)))
    return STATUS_DEVICE_BUSY;
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

  switch (QueryAdapterInfo->Type) {
  case DXGKQAITYPE_DRIVERCAPS: {
    DXGK_DRIVERCAPS *caps;
    APPLE_AGX_WDDM_FEATURE_INPUT featureInput;
    APPLE_AGX_WDDM_FEATURE_OUTPUT featureOutput;
    APPLE_AGX_WDDM_FEATURE_CONTRACT_RESULT featureResult;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      caps = (DXGK_DRIVERCAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(caps, sizeof(*caps));
      caps->HighestAcceptableAddress.QuadPart = -1;
      RtlZeroMemory(&featureInput, sizeof(featureInput));
      featureInput.Version = APPLE_AGX_WDDM_FEATURE_CONTRACT_VERSION;
      featureInput.Size = sizeof(featureInput);
      featureInput.WddmMajor = 3u;
      featureInput.WddmMinor = 0u;
      featureInput.NodeCount = 1u;
      featureInput.ReadyMask = (ULONG)InterlockedCompareExchange(
          &context->FeatureReadyMask, 0, 0);
      featureResult = AppleAgxWddmFeatureContractEvaluate(
          &featureInput, &featureOutput);
      /* The capability writer is intentionally absent until all 14 readiness
       * bits are earned.  An incomplete contract returns a completely zeroed
       * mandatory capability group; an unexpected ready state fails closed
       * instead of leaking the old partial Type-1 vector. */
      if (featureResult == AppleAgxWddmFeatureContractIncomplete &&
          featureOutput.PublishCapsMask == 0u)
        status = STATUS_SUCCESS;
      else
        status = STATUS_INVALID_DEVICE_STATE;
    }
    break;
  }

  case DXGKQAITYPE_WDDMDEVICECAPS: {
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
    break;
  }

  case DXGKQAITYPE_QUERYSEGMENT4:
    status = AdmissionDdiQuerySegment4(context, QueryAdapterInfo);
    break;

  case DXGKQAITYPE_PHYSICAL_MEMORY_CAPS: {
    DXGK_PHYSICAL_MEMORY_CAPS *physicalMemoryCaps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*physicalMemoryCaps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      physicalMemoryCaps =
          (DXGK_PHYSICAL_MEMORY_CAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(physicalMemoryCaps, sizeof(*physicalMemoryCaps));
      physicalMemoryCaps->HighestVisibleAddress.QuadPart = 0xFFFFFFFFFFLL;
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_IOMMU_CAPS: {
    DXGK_IOMMU_CAPS *iommuCaps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*iommuCaps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      iommuCaps = (DXGK_IOMMU_CAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(iommuCaps, sizeof(*iommuCaps));
      iommuCaps->Value = 0;
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_64BITONLYCAPS: {
    DXGK_64_BIT_ONLY_CAPS *only64Caps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*only64Caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      only64Caps = (DXGK_64_BIT_ONLY_CAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(only64Caps, sizeof(*only64Caps));
      only64Caps->SupportsOnly64Bit = 1;
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION: {
    DXGK_DISPLAY_DRIVERCAPS_EXTENSION *displayCaps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*displayCaps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      displayCaps = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION *)
          QueryAdapterInfo->pOutputData;
      RtlZeroMemory(displayCaps, sizeof(*displayCaps));
      status = STATUS_SUCCESS;
    }
    break;
  }

  default:
    status = STATUS_NOT_SUPPORTED;
    break;
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
