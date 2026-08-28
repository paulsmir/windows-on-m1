#include "apple_agx_admission.h"

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
  initialization.DxgkDdiAddDevice = AppleAgxAdmissionAddDevice;
  initialization.DxgkDdiStartDevice = AppleAgxAdmissionStartDevice;
  initialization.DxgkDdiStopDevice = AppleAgxAdmissionStopDevice;
  initialization.DxgkDdiRemoveDevice = AppleAgxAdmissionRemoveDevice;
  initialization.DxgkDdiDispatchIoRequest =
      AppleAgxAdmissionDispatchIoRequest;
  initialization.DxgkDdiUnload = AppleAgxAdmissionUnload;

  status = DxgkInitialize(DriverObject, RegistryPath, &initialization);
  return status;
}
