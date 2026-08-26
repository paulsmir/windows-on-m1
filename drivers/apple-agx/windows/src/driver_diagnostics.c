#include "apple_agx_driver.h"

static void AppleAgxWriteDiagnosticDword(PUNICODE_STRING RegistryPath,
                                         PCWSTR ValueName, ULONG Value) {
#ifdef APPLE_AGX_G2_POWER_QUALIFICATION
  OBJECT_ATTRIBUTES attributes;
  UNICODE_STRING valueName;
  HANDLE key = NULL;

  InitializeObjectAttributes(&attributes, RegistryPath,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL,
                             NULL);
  if (!NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes)))
    return;

  RtlInitUnicodeString(&valueName, ValueName);
  (void)ZwSetValueKey(key, &valueName, 0, REG_DWORD, &Value, sizeof(Value));
  ZwClose(key);
#else
  UNREFERENCED_PARAMETER(RegistryPath);
  UNREFERENCED_PARAMETER(ValueName);
  UNREFERENCED_PARAMETER(Value);
#endif
}

static void AppleAgxWriteDeviceDiagnosticDword(PDEVICE_OBJECT DeviceObject,
                                                PCWSTR ValueName,
                                                ULONG Value) {
#ifdef APPLE_AGX_G2_POWER_QUALIFICATION
  UNICODE_STRING valueName;
  HANDLE key = NULL;

  if (DeviceObject == NULL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(
          DeviceObject, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key)))
    return;

  RtlInitUnicodeString(&valueName, ValueName);
  (void)ZwSetValueKey(key, &valueName, 0, REG_DWORD, &Value, sizeof(Value));
  ZwClose(key);
#else
  UNREFERENCED_PARAMETER(DeviceObject);
  UNREFERENCED_PARAMETER(ValueName);
  UNREFERENCED_PARAMETER(Value);
#endif
}

void AppleAgxRecordDriverEntryBoundary(PUNICODE_STRING RegistryPath,
                                       ULONG Stage, NTSTATUS Status) {
  AppleAgxWriteDiagnosticDword(RegistryPath, L"Wom1DriverEntryStage", Stage);
  AppleAgxWriteDiagnosticDword(RegistryPath, L"Wom1DxgkInitializeStatus",
                               (ULONG)Status);
}

void AppleAgxRecordAddDeviceBoundary(PDEVICE_OBJECT DeviceObject,
                                     APPLE_AGX_ADD_STAGE Stage,
                                     NTSTATUS Status) {
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1AddDeviceStage",
                                     (ULONG)Stage);
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1AddDeviceStatus",
                                     (ULONG)Status);
}

void AppleAgxRecordStartDeviceBoundary(PDEVICE_OBJECT DeviceObject,
                                       APPLE_AGX_START_STAGE Stage,
                                       NTSTATUS Status) {
  AppleAgxLogStartStage(DeviceObject, Stage, Status);
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1StartDeviceStage",
                                     (ULONG)Stage);
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1StartDeviceStatus",
                                     (ULONG)Status);
}
