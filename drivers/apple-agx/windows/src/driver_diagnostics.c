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

void AppleAgxRecordDriverEntryBoundary(PUNICODE_STRING RegistryPath,
                                       ULONG Stage, NTSTATUS Status) {
  AppleAgxWriteDiagnosticDword(RegistryPath, L"Wom1DriverEntryStage", Stage);
  AppleAgxWriteDiagnosticDword(RegistryPath, L"Wom1DxgkInitializeStatus",
                               (ULONG)Status);
}
