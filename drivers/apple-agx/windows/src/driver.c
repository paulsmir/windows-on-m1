#include "apple_agx_driver.h"

DRIVER_INITIALIZE DriverEntry;

#pragma alloc_text(INIT, DriverEntry)

_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,
                                            PUNICODE_STRING RegistryPath) {
  DRIVER_INITIALIZATION_DATA initialization;

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

  return DxgkInitialize(DriverObject, RegistryPath, &initialization);
}
