#include "render_admission.h"

DRIVER_INITIALIZE DriverEntry;
#pragma alloc_text(INIT, DriverEntry)

C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 1296);

_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,
                                             PUNICODE_STRING RegistryPath) {
  DRIVER_INITIALIZATION_DATA initialization;
  NTSTATUS status;

  PAGED_CODE();
  RtlZeroMemory(&initialization, sizeof(initialization));
  initialization.Version = DXGKDDI_INTERFACE_VERSION_WDDM3_0;
  initialization.DxgkDdiAddDevice = AdmissionDdiAddDevice;
  initialization.DxgkDdiStartDevice = AdmissionDdiStartDevice;
  initialization.DxgkDdiStopDevice = AdmissionDdiStopDevice;
  initialization.DxgkDdiRemoveDevice = AdmissionDdiRemoveDevice;
  initialization.DxgkDdiDispatchIoRequest = AdmissionDdiDispatchIoRequest;
  initialization.DxgkDdiInterruptRoutine = AdmissionDdiInterruptRoutine;
  initialization.DxgkDdiDpcRoutine = AdmissionDdiDpcRoutine;
  initialization.DxgkDdiQueryChildRelations = AdmissionDdiQueryChildRelations;
  initialization.DxgkDdiQueryChildStatus = AdmissionDdiQueryChildStatus;
  initialization.DxgkDdiQueryDeviceDescriptor = AdmissionDdiQueryDeviceDescriptor;
  initialization.DxgkDdiSetPowerState = AdmissionDdiSetPowerState;
  initialization.DxgkDdiResetDevice = AdmissionDdiResetDevice;
  initialization.DxgkDdiUnload = AdmissionDdiUnload;
  initialization.DxgkDdiQueryAdapterInfo = AdmissionDdiQueryAdapterInfo;
  initialization.DxgkDdiSetPalette = AdmissionDdiSetPalette;
  initialization.DxgkDdiSetPointerPosition = AdmissionDdiSetPointerPosition;
  initialization.DxgkDdiSetPointerShape = AdmissionDdiSetPointerShape;
  initialization.DxgkDdiIsSupportedVidPn =
      AdmissionTraceDdiIsSupportedVidPn;
  initialization.DxgkDdiRecommendFunctionalVidPn =
      AdmissionTraceDdiRecommendFunctionalVidPn;
  initialization.DxgkDdiEnumVidPnCofuncModality =
      AdmissionTraceDdiEnumVidPnCofuncModality;
  initialization.DxgkDdiSetVidPnSourceVisibility =
      AdmissionTraceDdiSetVidPnSourceVisibility;
  initialization.DxgkDdiCommitVidPn = AdmissionTraceDdiCommitVidPn;
  initialization.DxgkDdiUpdateActiveVidPnPresentPath =
      AdmissionTraceDdiUpdateActiveVidPnPresentPath;
  initialization.DxgkDdiRecommendMonitorModes =
      AdmissionTraceDdiRecommendMonitorModes;
  initialization.DxgkDdiGetScanLine = AdmissionDdiGetScanLine;
  initialization.DxgkDdiStopCapture = AdmissionDdiStopCapture;
  initialization.DxgkDdiQueryVidPnHWCapability =
      AdmissionTraceDdiQueryVidPnHWCapability;
  initialization.DxgkDdiSetVidPnSourceAddress =
      AdmissionDdiSetVidPnSourceAddress;
  initialization.DxgkDdiStopDeviceAndReleasePostDisplayOwnership =
      AdmissionDdiStopDeviceAndReleasePostDisplayOwnership;
  initialization.DxgkDdiNotifyAcpiEvent = AdmissionDdiNotifyAcpiEvent;
  initialization.DxgkDdiQueryInterface = AdmissionDdiQueryInterface;
  initialization.DxgkDdiControlEtwLogging = AdmissionDdiControlEtwLogging;
  initialization.DxgkDdiCreateDevice = AdmissionDdiCreateDevice;
  initialization.DxgkDdiDestroyDevice = AdmissionDdiDestroyDevice;
  initialization.DxgkDdiCreateAllocation = AdmissionDdiCreateAllocation;
  initialization.DxgkDdiDestroyAllocation = AdmissionDdiDestroyAllocation;
  initialization.DxgkDdiDescribeAllocation = AdmissionDdiDescribeAllocation;
  initialization.DxgkDdiGetStandardAllocationDriverData = AdmissionDdiGetStandardAllocationDriverData;
  initialization.DxgkDdiOpenAllocation = AdmissionDdiOpenAllocation;
  initialization.DxgkDdiCloseAllocation = AdmissionDdiCloseAllocation;
  initialization.DxgkDdiPatch = AdmissionDdiPatch;
  initialization.DxgkDdiSubmitCommand = AdmissionDdiSubmitCommand;
  initialization.DxgkDdiBuildPagingBuffer = AdmissionDdiBuildPagingBuffer;
  initialization.DxgkDdiPreemptCommand = AdmissionDdiPreemptCommand;
  initialization.DxgkDdiRender = AdmissionDdiRender;
  initialization.DxgkDdiPresent = AdmissionDdiPresent;
  initialization.DxgkDdiCreateOverlay = AdmissionDdiCreateOverlay;
  initialization.DxgkDdiUpdateOverlay = AdmissionDdiUpdateOverlay;
  initialization.DxgkDdiFlipOverlay = AdmissionDdiFlipOverlay;
  initialization.DxgkDdiDestroyOverlay = AdmissionDdiDestroyOverlay;
  initialization.DxgkDdiResetFromTimeout = AdmissionDdiResetFromTimeout;
  initialization.DxgkDdiRestartFromTimeout = AdmissionDdiRestartFromTimeout;
  initialization.DxgkDdiEscape = AdmissionDdiEscape;
  initialization.DxgkDdiCollectDbgInfo = AdmissionDdiCollectDbgInfo;
  initialization.DxgkDdiQueryCurrentFence = AdmissionDdiQueryCurrentFence;
  initialization.DxgkDdiControlInterrupt = AdmissionDdiControlInterrupt;
  initialization.DxgkDdiCreateContext = AdmissionDdiCreateContext;
  initialization.DxgkDdiDestroyContext = AdmissionDdiDestroyContext;
  initialization.DxgkDdiRenderKm = AdmissionDdiRenderKm;
  initialization.DxgkDdiQueryDependentEngineGroup = AdmissionDdiQueryDependentEngineGroup;
  initialization.DxgkDdiQueryEngineStatus = AdmissionDdiQueryEngineStatus;
  initialization.DxgkDdiResetEngine = AdmissionDdiResetEngine;
  initialization.DxgkDdiCancelCommand = AdmissionDdiCancelCommand;
  initialization.DxgkDdiSetPowerComponentFState = AdmissionDdiSetPowerComponentFState;
  initialization.DxgkDdiPowerRuntimeControlRequest = AdmissionDdiPowerRuntimeControlRequest;
  initialization.DxgkDdiGetNodeMetadata = AdmissionDdiGetNodeMetadata;
  initialization.DxgkDdiSubmitCommandVirtual = AdmissionDdiSubmitCommandVirtual;
  initialization.DxgkDdiCreateProcess = AdmissionDdiCreateProcess;
  initialization.DxgkDdiDestroyProcess = AdmissionDdiDestroyProcess;
  initialization.DxgkDdiCalibrateGpuClock = AdmissionDdiCalibrateGpuClock;
  initialization.DxgkDdiSetStablePowerState = AdmissionDdiSetStablePowerState;

  AdmissionRecordService(RegistryPath, L"Wom1CleanDriverEntryStage", 1);
  status = DxgkInitialize(DriverObject, RegistryPath, &initialization);
  AdmissionRecordService(RegistryPath, L"Wom1CleanDxgkInitializeStatus", (ULONG)status);
  return status;
}
