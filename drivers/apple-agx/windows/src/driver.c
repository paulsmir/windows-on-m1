#include "apple_agx_driver.h"

DRIVER_INITIALIZE DriverEntry;

#pragma alloc_text(INIT, DriverEntry)

/*
 * The pinned 10.0.28000 WDK exposes its complete initialization table as 1544
 * bytes on ARM64 when no older interface layout is forced.  Dxgkrnl accepts
 * this full callback ABI while
 * the Version field below deliberately advertises only the implemented WDDM
 * 2.6 runtime surface.  Compiling the structure itself as WDDM 2.6 truncates
 * it to 1224 bytes and prevents the PnP stack from reaching StartDevice.
 */
C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 1544);

_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,
                                            PUNICODE_STRING RegistryPath) {
  DRIVER_INITIALIZATION_DATA initialization;
  NTSTATUS status;

  PAGED_CODE();
  RtlZeroMemory(&initialization, sizeof(initialization));
  initialization.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_6;
  initialization.DxgkDdiAddDevice = AppleAgxDdiAddDevice;
  initialization.DxgkDdiStartDevice = AppleAgxDdiStartDevice;
  initialization.DxgkDdiStopDevice = AppleAgxDdiStopDevice;
  initialization.DxgkDdiRemoveDevice = AppleAgxDdiRemoveDevice;
  initialization.DxgkDdiDispatchIoRequest = AppleAgxDdiDispatchIoRequest;
  initialization.DxgkDdiInterruptRoutine = AppleAgxDdiInterruptRoutine;
  initialization.DxgkDdiDpcRoutine = AppleAgxDdiDpcRoutine;
  initialization.DxgkDdiQueryChildRelations = AppleAgxDdiQueryChildRelations;
  initialization.DxgkDdiQueryChildStatus = AppleAgxDdiQueryChildStatus;
  initialization.DxgkDdiQueryDeviceDescriptor =
      AppleAgxDdiQueryDeviceDescriptor;
  initialization.DxgkDdiSetPowerState = AppleAgxDdiSetPowerState;
  initialization.DxgkDdiResetDevice = AppleAgxDdiResetDevice;
  initialization.DxgkDdiUnload = AppleAgxDdiUnload;
  initialization.DxgkDdiQueryAdapterInfo = AppleAgxDdiQueryAdapterInfo;
  initialization.DxgkDdiNotifyAcpiEvent = AppleAgxDdiNotifyAcpiEvent;
  initialization.DxgkDdiQueryInterface = AppleAgxDdiQueryInterface;
  initialization.DxgkDdiControlEtwLogging = AppleAgxDdiControlEtwLogging;
  initialization.DxgkDdiCreateDevice = AppleAgxDdiCreateDevice;
  initialization.DxgkDdiDestroyDevice = AppleAgxDdiDestroyDevice;
  initialization.DxgkDdiCreateAllocation = AppleAgxDdiCreateAllocation;
  initialization.DxgkDdiDestroyAllocation = AppleAgxDdiDestroyAllocation;
  initialization.DxgkDdiDescribeAllocation = AppleAgxDdiDescribeAllocation;
  initialization.DxgkDdiGetStandardAllocationDriverData =
      AppleAgxDdiGetStandardAllocationDriverData;
  initialization.DxgkDdiOpenAllocation = AppleAgxDdiOpenAllocation;
  initialization.DxgkDdiCloseAllocation = AppleAgxDdiCloseAllocation;
  initialization.DxgkDdiPatch = AppleAgxDdiPatch;
  initialization.DxgkDdiSubmitCommand = AppleAgxDdiSubmitCommand;
  initialization.DxgkDdiBuildPagingBuffer = AppleAgxDdiBuildPagingBuffer;
  initialization.DxgkDdiPreemptCommand = AppleAgxDdiPreemptCommand;
  initialization.DxgkDdiRender = AppleAgxDdiRender;
  initialization.DxgkDdiPresent = AppleAgxDdiPresent;
  initialization.DxgkDdiResetFromTimeout = AppleAgxDdiResetFromTimeout;
  initialization.DxgkDdiRestartFromTimeout = AppleAgxDdiRestartFromTimeout;
  initialization.DxgkDdiEscape = AppleAgxDdiEscape;
  initialization.DxgkDdiCollectDbgInfo = AppleAgxDdiCollectDbgInfo;
  initialization.DxgkDdiQueryCurrentFence = AppleAgxDdiQueryCurrentFence;
  initialization.DxgkDdiControlInterrupt = AppleAgxDdiControlInterrupt;
  initialization.DxgkDdiCreateContext = AppleAgxDdiCreateContext;
  initialization.DxgkDdiDestroyContext = AppleAgxDdiDestroyContext;
  initialization.DxgkDdiRenderKm = AppleAgxDdiRenderKm;
  initialization.DxgkDdiQueryDependentEngineGroup =
      AppleAgxDdiQueryDependentEngineGroup;
  initialization.DxgkDdiQueryEngineStatus = AppleAgxDdiQueryEngineStatus;
  initialization.DxgkDdiResetEngine = AppleAgxDdiResetEngine;
  initialization.DxgkDdiCancelCommand = AppleAgxDdiCancelCommand;
  initialization.DxgkDdiSetPowerComponentFState =
      AppleAgxDdiSetPowerComponentFState;
  initialization.DxgkDdiPowerRuntimeControlRequest =
      AppleAgxDdiPowerRuntimeControlRequest;
  initialization.DxgkDdiGetNodeMetadata = AppleAgxDdiGetNodeMetadata;
  initialization.DxgkDdiSubmitCommandVirtual =
      AppleAgxDdiSubmitCommandVirtual;
  initialization.DxgkDdiCreateProcess = AppleAgxDdiCreateProcess;
  initialization.DxgkDdiDestroyProcess = AppleAgxDdiDestroyProcess;
  initialization.DxgkDdiCalibrateGpuClock = AppleAgxDdiCalibrateGpuClock;
  initialization.DxgkDdiSetStablePowerState =
      AppleAgxDdiSetStablePowerState;

#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordDriverEntryBoundary(RegistryPath, 1, STATUS_PENDING);
#endif
  status = DxgkInitialize(DriverObject, RegistryPath, &initialization);
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  AppleAgxRecordDriverEntryBoundary(RegistryPath, 2, status);
#endif
  return status;
}
