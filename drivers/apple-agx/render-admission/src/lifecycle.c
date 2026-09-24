#include "render_admission.h"
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION) || \
    defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#include "apple_agx_render_template_vm_slot.h"
static BOOLEAN AdmissionGpuvaArmed(ADMISSION_CONTEXT *context,
                                   PCWSTR value_name) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  NTSTATUS status;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (context == NULL || context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, value_name);
  status = ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                           value, sizeof(data.Buffer), &bytes);
  ZwClose(key);
  return NT_SUCCESS(status) && value->Type == REG_DWORD &&
         value->DataLength == sizeof(ULONG) &&
         *(ULONG *)value->Data == 1u;
}
#endif

_Use_decl_annotations_ NTSTATUS AdmissionDdiAddDevice(
    PDEVICE_OBJECT PhysicalDeviceObject, PVOID *MiniportDeviceContext) {
  ADMISSION_CONTEXT *context;
  AdmissionRecordDevice(PhysicalDeviceObject, AdmissionReceiptAddEntered,
                        STATUS_PENDING);
  if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
    return STATUS_INVALID_PARAMETER;
  context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context),
                            ADMISSION_POOL_TAG);
  if (context == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(context, sizeof(*context));
  AdmissionObjectsInitializeAdapter(&context->ObjectAdapter);
  context->Win32BootGeneration = (ULONG)KeQueryInterruptTime();
  if (context->Win32BootGeneration == 0u)
    context->Win32BootGeneration = 1u;
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

  AdmissionRecordDevice(context == NULL ? NULL : context->PhysicalDeviceObject,
                        AdmissionReceiptStartEntered, STATUS_PENDING);
  AdmissionRecordStartStage(context, AdmissionStartEntered, STATUS_PENDING);
  if (context == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
      NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
    return STATUS_INVALID_PARAMETER;
  *NumberOfVideoPresentSources = 0;
  *NumberOfChildren = 0;
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  if (!AdmissionGpuvaArmed(context, L"B1Armed")) {
    AdmissionRecordB1Qualification(
        context, 0u, STATUS_NOT_SUPPORTED, STATUS_NOT_SUPPORTED,
        0u, 0u, 0ULL,
        0u, 0u, 0u, 0u, 0u);
    AdmissionRecordStartStage(context, AdmissionStartEntered,
                              STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
  }
#endif
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!AdmissionGpuvaArmed(context, L"G3Armed")) {
    AdmissionRecordStartStage(context, AdmissionStartEntered,
                              STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
  }
#endif
  context->StartInfo = *DxgkStartInfo;
  context->Interface = *DxgkInterface;
  context->InterfaceValid = TRUE;
  RtlZeroMemory(&context->DeviceInformation,
                sizeof(context->DeviceInformation));
  status = context->Interface.DxgkCbGetDeviceInformation(
      context->Interface.DeviceHandle, &context->DeviceInformation);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartDeviceInfo, status);
  AdmissionRecordStartStage(context, AdmissionStartDeviceInfo, status);
  if (!NT_SUCCESS(status))
    return status;

  status = AdmissionInterruptStart(context);
  AdmissionRecordDevice(context->PhysicalDeviceObject,
                        AdmissionReceiptStartInterrupt, status);
  AdmissionRecordStartStage(context, AdmissionStartInterrupt, status);
  if (!NT_SUCCESS(status))
    return status;

  status = AdmissionMemoryRuntimeStart(context);
  AdmissionRecordStartStage(context, AdmissionStartMemory, status);
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
  status = AdmissionBackendImageStart(context);
  AdmissionRecordStartStage(context, AdmissionStartBackendImage, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION) || \
    defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!AppleAgxRenderTemplateSelectVmSlot(
          context->BackendImage.ArenaCpuAddress,
          context->BackendImage.ArenaCapacity, 1u)) {
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_INVALID_IMAGE_FORMAT;
  }
#endif
  status = AdmissionSchedulerStart(context);
  AdmissionRecordStartStage(context, AdmissionStartScheduler, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  status = AdmissionPagingStart(context);
  AdmissionRecordStartStage(context, AdmissionStartPaging, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  status = AdmissionPlatformRuntimeStart(context);
  AdmissionRecordStartStage(context, AdmissionStartPlatform, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }

#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  status = AdmissionGpuvaB1Qualify(context);
  if (context->GpuvaB1State != NULL)
    return STATUS_DEVICE_BUSY; /* Preserve pages with uncertain GPU use. */
  if (!NT_SUCCESS(AdmissionPlatformRuntimeStop(context)))
    return STATUS_DEVICE_BUSY;
  if (!NT_SUCCESS(AdmissionPagingStop(context)) ||
      !NT_SUCCESS(AdmissionSchedulerStop(context)) ||
      !NT_SUCCESS(AdmissionBackendImageStop(context)) ||
      !NT_SUCCESS(AdmissionMemoryRuntimeStop(context)) ||
      !NT_SUCCESS(AdmissionInterruptStop(context)))
    return STATUS_DEVICE_BUSY;
  return NT_SUCCESS(status) ? STATUS_NOT_SUPPORTED : status;
#endif

  if (context->Interface.DxgkCbAcquirePostDisplayOwnership == NULL) {
    AdmissionRecordStartStage(
        context, AdmissionStartPostDisplay, STATUS_NOT_SUPPORTED);
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
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
  AdmissionRecordStartStage(context, AdmissionStartPostDisplay, status);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
  if (context->PostDisplayInformation.PhysicAddress.QuadPart == 0 ||
      context->PostDisplayInformation.Width != 2560 ||
      context->PostDisplayInformation.Height != 1600 ||
      context->PostDisplayInformation.Pitch != 10240) {
    AdmissionRecordStartStage(
        context, AdmissionStartPostDisplay,
        STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER);
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_GRAPHICS_INVALID_DISPLAY_ADAPTER;
  }
  status = AdmissionScanoutStart(context);
  AdmissionRecordStartStage(context, AdmissionStartScanout, status);
  if (!NT_SUCCESS(status)) {
    /* An uncertain REGISTER/RELEASE result retains every lower memory owner.
     * PnP teardown may retry AdmissionScanoutStop, but must not unmap a pool
     * that m1n1/DCP could still own. */
    if (context->ScanoutRuntime != NULL)
      return status;
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }

  context->Started = TRUE;
  if (!AdmissionObjectsStartAdapter(&context->ObjectAdapter)) {
    AdmissionRecordStartStage(
        context, AdmissionStartObjects, STATUS_INVALID_DEVICE_STATE);
    context->Started = FALSE;
    status = AdmissionScanoutStop(context);
    if (!NT_SUCCESS(status))
      return status;
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return STATUS_INVALID_DEVICE_STATE;
  }
  AdmissionRecordStartStage(context, AdmissionStartObjects, STATUS_SUCCESS);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  status = AdmissionGpuvaG3Start(context);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionObjectsStopAdapter(&context->ObjectAdapter);
    context->Started = FALSE;
    (void)AdmissionScanoutStop(context);
    (void)AdmissionPlatformRuntimeStop(context);
    (void)AdmissionPagingStop(context);
    (void)AdmissionSchedulerStop(context);
    (void)AdmissionBackendImageStop(context);
    (void)AdmissionMemoryRuntimeStop(context);
    (void)AdmissionInterruptStop(context);
    return status;
  }
#endif
  context->DisplayActive = TRUE;
  context->SourceVisible = TRUE;
  context->CommittedWidth = 2560;
  context->CommittedHeight = 1600;
  context->CommittedStride = 10240;
  context->CommittedFormat = D3DDDIFMT_A8R8G8B8;
  /* QueryAdapterInfo cannot run until StartDevice returns. Publish the complete
   * immutable implementation vector only after every runtime owner above has
   * started and the adapter object is live. */
  InterlockedExchange(&context->FeatureReadyMask,
                      APPLE_AGX_WDDM_REQUIRED_READY_MASK);
  AdmissionRecordStartStage(context, AdmissionStartComplete, STATUS_SUCCESS);
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
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  if (context->GpuvaB1State != NULL)
    return STATUS_DEVICE_BUSY;
#endif
  AdmissionFlushSourceAddressReceipt(context);
  AdmissionFlushPresentTransfer(context);
  AdmissionFlushGdiReceipt(context);
  AdmissionRecordDevice(context->PhysicalDeviceObject, AdmissionReceiptStop,
                        STATUS_SUCCESS);
  if (context->ObjectAdapter.DeviceCount != 0u)
    return STATUS_DEVICE_BUSY;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  status = AdmissionGpuvaG3Stop(context);
  if (!NT_SUCCESS(status)) return status;
#endif
  status = AdmissionScanoutStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionPagingStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionPlatformRuntimeStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionSchedulerStop(context);
  if (!NT_SUCCESS(status))
    return status;
  status = AdmissionBackendImageStop(context);
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
  InterlockedExchange(
      &context->FeatureReadyMask,
      APPLE_AGX_WDDM_READY_WDDM3_IDENTITY |
          APPLE_AGX_WDDM_READY_DEVICE_CONTEXT);
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
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!NT_SUCCESS(AdmissionGpuvaG3Stop(context))) return STATUS_DEVICE_BUSY;
#endif
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  if (context->GpuvaB1State != NULL)
    return STATUS_DEVICE_BUSY;
#endif
  AdmissionFlushSourceAddressReceipt(context);
  AdmissionFlushPresentTransfer(context);
  AdmissionFlushGdiReceipt(context);
  if (context->ScanoutRuntime != NULL &&
      !NT_SUCCESS(AdmissionScanoutStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->PagingWorkItem != NULL &&
      !NT_SUCCESS(AdmissionPagingStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->PlatformRuntime != NULL &&
      !NT_SUCCESS(AdmissionPlatformRuntimeStop(context)))
    return STATUS_DEVICE_BUSY;
  if (InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) != 0 &&
      !NT_SUCCESS(AdmissionSchedulerStop(context)))
    return STATUS_DEVICE_BUSY;
  if (context->BackendImage.Ready == APPLE_AGX_TRUE &&
      !NT_SUCCESS(AdmissionBackendImageStop(context)))
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
  case DXGKQAITYPE_UMDRIVERPRIVATE: {
    AGX_WIN32_DEVICE_INFO *info;
    if (QueryAdapterInfo->pInputData != NULL ||
        QueryAdapterInfo->InputDataSize != 0u ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize != sizeof(*info)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    info = (AGX_WIN32_DEVICE_INFO *)QueryAdapterInfo->pOutputData;
    RtlZeroMemory(info, sizeof(*info));
    info->Magic = AGX_WIN32_DEVICE_INFO_MAGIC;
    info->Version = AGX_WIN32_DEVICE_INFO_VERSION;
    info->Bytes = sizeof(*info);
    info->BootGeneration = context->Win32BootGeneration;
    info->GpuGeneration = 13u;
    info->GpuVariant = AgxWin32GpuG13G;
    info->PageBytes = 0x4000u;
    info->ClassCount = AGX_WIN32_BUFFER_CLASS_COUNT;
    info->Classes[0].ClassId = AgxWin32BufferClassGeneral;
    info->Classes[0].MinimumAlignment = 0x4000u;
    info->Classes[0].MaximumBytes = AGX_RR_SHARED_ARENA_BYTES;
    info->Classes[0].Flags = AppleAgxWin32BufferCpuRead |
        AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead |
        AppleAgxWin32BufferGpuWrite;
    info->Classes[1].ClassId = AgxWin32BufferClassShader;
    info->Classes[1].MinimumAlignment = 0x4000u;
    info->Classes[1].MaximumBytes = AGX_RR_SHARED_ARENA_BYTES;
    info->Classes[1].Flags = AppleAgxWin32BufferCpuWrite |
        AppleAgxWin32BufferGpuRead;
    info->Classes[2].ClassId = AgxWin32BufferClassEncoder;
    info->Classes[2].MinimumAlignment = 0x4000u;
    info->Classes[2].MaximumBytes = AGX_RR_COMMAND_ARENA_BYTES;
    info->Classes[2].Flags = AppleAgxWin32BufferCpuWrite |
        AppleAgxWin32BufferGpuRead;
    status = STATUS_SUCCESS;
    break;
  }

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
      /* WDK 26100: must match DXGK_WDDMDEVICECAPS.WDDMVersion. */
      caps->WDDMVersion = ADMISSION_G1B_WDDM_VERSION;
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
      if (featureResult == AppleAgxWddmFeatureContractIncomplete &&
          featureOutput.PublishCapsMask == 0u) {
        status = STATUS_SUCCESS;
      } else if (featureResult == AppleAgxWddmFeatureContractReady &&
                 featureOutput.PublishCapsMask ==
                     APPLE_AGX_WDDM_MANDATORY_CAPS_MASK) {
        caps->MaxAllocationListSlotId =
            ADMISSION_GDI_ALLOCATION_LIST_SIZE - 1u;
        caps->MaxQueuedFlipOnVSync = 1u;
        caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1u;
        caps->SchedulingCaps.MultiEngineAware = 1u;
        caps->SchedulingCaps.PreemptionAware = 1u;
        caps->PreemptionCaps.GraphicsPreemptionGranularity =
            D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps->PreemptionCaps.ComputePreemptionGranularity =
            D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
        caps->FlipCaps.FlipOnVSyncMmIo = 1u;
        caps->FlipCaps.FlipIndependent = 1u;
        caps->SupportNonVGA = TRUE;
        /* WDDM 1.2+ full graphics requires this even when the only exposed
         * path transform is the already-supported identity/Offset0 pair. */
        caps->SupportSmoothRotation = TRUE;
        caps->SupportPerEngineTDR = TRUE;
        caps->SupportDirectFlip = TRUE;
        caps->PresentationCaps.SupportKernelModeCommandBuffer = 1u;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
        if (context->GpuvaG3State != NULL) {
          caps->MemoryManagementCaps.VirtualAddressingSupported = 1u;
          caps->MemoryManagementCaps.GpuMmuSupported = 1u;
        }
#endif
        status = STATUS_SUCCESS;
      } else {
        status = STATUS_INVALID_DEVICE_STATE;
      }
    }
    break;
  }

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  case DXGKQAITYPE_GPUMMUCAPS: {
    const DXGK_QUERYGPUMMUCAPSIN *input;
    DXGK_GPUMMUCAPS *caps;
    if (context->GpuvaG3State == NULL) {
      status = STATUS_INVALID_DEVICE_STATE;
    } else if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      input = (const DXGK_QUERYGPUMMUCAPSIN *)QueryAdapterInfo->pInputData;
      caps = (DXGK_GPUMMUCAPS *)QueryAdapterInfo->pOutputData;
      if (input->PhysicalAdapterIndex != 0u) {
        status = STATUS_INVALID_PARAMETER;
      } else {
        RtlZeroMemory(caps, sizeof(*caps));
        caps->ReadOnlyMemorySupported = 1u;
        caps->ExplicitPageTableInvalidation = 1u;
        caps->PageTableUpdateRequireAddressSpaceIdle = 1u;
        caps->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_GPU_PHYSICAL;
        caps->VirtualAddressBitCount = 39u;
        caps->PageTableLevelCount = 3u;
        status = STATUS_SUCCESS;
      }
    }
    break;
  }

  case DXGKQAITYPE_PAGETABLELEVELDESC: {
    const DXGK_QUERYPAGETABLELEVELDESCIN *input;
    DXGK_PAGE_TABLE_LEVEL_DESC *level;
    if (context->GpuvaG3State == NULL) {
      status = STATUS_INVALID_DEVICE_STATE;
    } else if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*level)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      input = (const DXGK_QUERYPAGETABLELEVELDESCIN *)
          QueryAdapterInfo->pInputData;
      level = (DXGK_PAGE_TABLE_LEVEL_DESC *)QueryAdapterInfo->pOutputData;
      if (input->PhysicalAdapterIndex != 0u || input->LevelIndex >= 3u) {
        status = STATUS_INVALID_PARAMETER;
      } else {
        RtlZeroMemory(level, sizeof(*level));
        level->PageTableIndexBitCount =
            input->LevelIndex == 0u ? 13u :
            input->LevelIndex == 1u ? 11u : 3u;
        level->PageTableSegmentId = ADMISSION_MEMORY_LOCAL_SEGMENT;
        level->PagingProcessPageTableSegmentId =
            ADMISSION_MEMORY_LOCAL_SEGMENT;
        level->PageTableSizeInBytes = 0x4000u;
        level->PageTableAlignmentInBytes = 0x4000u;
        status = STATUS_SUCCESS;
      }
    }
    break;
  }
#endif

  case DXGKQAITYPE_WDDMDEVICECAPS: {
    DXGK_WDDMDEVICECAPS *caps;
    if (QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*caps)) {
      status = STATUS_BUFFER_TOO_SMALL;
    } else {
      caps = (DXGK_WDDMDEVICECAPS *)QueryAdapterInfo->pOutputData;
      RtlZeroMemory(caps, sizeof(*caps));
      caps->WDDMVersion = ADMISSION_G1B_WDDM_VERSION;
      status = STATUS_SUCCESS;
    }
    break;
  }

  case DXGKQAITYPE_QUERYSEGMENT4:
    status = AdmissionDdiQuerySegment4(context, QueryAdapterInfo);
    break;

#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
  case DXGKQAITYPE_QUERYPAGINGBUFFERINFO: {
    const DXGK_QUERYPAGINGBUFFERINFOIN *input;
    DXGK_QUERYPAGINGBUFFERINFOOUT *output;
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYPAGINGBUFFERINFOIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYPAGINGBUFFERINFOOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u ||
        !AdmissionMemoryReady(&context->Memory)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    output->PagingBufferSize =
        (UINT32)context->Memory.Topology.PagingBufferSize;
    output->PagingBufferPrivateDataSize = PAGE_SIZE;
    status = STATUS_SUCCESS;
    break;
  }
  case DXGKQAITYPE_QUERYSEGMENTCOUNT: {
    const DXGK_QUERYSEGMENTCOUNTIN *input;
    DXGK_QUERYSEGMENTCOUNTOUT *output;
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYSEGMENTCOUNTIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYSEGMENTCOUNTOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u || input->Reserved != 0u ||
        !AdmissionMemoryReady(&context->Memory)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    RtlZeroMemory(output, sizeof(*output));
    output->SegmentCount = (UINT16)context->Memory.Topology.SegmentCount;
    status = STATUS_SUCCESS;
    break;
  }
  case DXGKQAITYPE_QUERYSEGMENT5:
    status = AdmissionDdiQuerySegment5(context, QueryAdapterInfo);
    break;
  case DXGKQAITYPE_QUERYMMUCOUNT: {
    const DXGK_QUERYMMUCOUNTIN *input;
    DXGK_QUERYMMUCOUNTOUT *output;
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYMMUCOUNTIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYMMUCOUNTOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u || input->Reserved != 0u) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    RtlZeroMemory(output, sizeof(*output));
    /* Physical-mode trial advertises no VidMm-managed MMU. */
    output->MmuCount = 0u;
    status = STATUS_SUCCESS;
    break;
  }
  case DXGKQAITYPE_QUERYMMUS: {
    const DXGK_QUERYMMUSIN *input;
    DXGK_QUERYMMUSOUT *output;
    if (QueryAdapterInfo->pInputData == NULL ||
        QueryAdapterInfo->InputDataSize < sizeof(*input) ||
        QueryAdapterInfo->pOutputData == NULL ||
        QueryAdapterInfo->OutputDataSize < sizeof(*output)) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    input = (const DXGK_QUERYMMUSIN *)QueryAdapterInfo->pInputData;
    output = (DXGK_QUERYMMUSOUT *)QueryAdapterInfo->pOutputData;
    if (input->PhysicalAdapterIndex != 0u) {
      status = STATUS_INVALID_PARAMETER;
      break;
    }
    RtlZeroMemory(output, sizeof(*output));
    output->DisplayMmuId = DXGK_INVALID_MMU_ID;
    status = STATUS_SUCCESS;
    break;
  }
#endif

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
                       QueryAdapterInfo->OutputDataSize, status,
                       QueryAdapterInfo->pOutputData);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionRecordGpuvaG3Query(context->PhysicalDeviceObject,
                              QueryAdapterInfo, status);
#endif
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
