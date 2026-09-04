#include "render_admission.h"

static void WriteDword(HANDLE Key, PCWSTR Name, ULONG Value) {
  UNICODE_STRING name;
  RtlInitUnicodeString(&name, Name);
  (void)ZwSetValueKey(Key, &name, 0, REG_DWORD, &Value, sizeof(Value));
}

static void WriteBinary(HANDLE Key, PCWSTR Name, const VOID *Value,
                        ULONG Bytes) {
  UNICODE_STRING name;
  RtlInitUnicodeString(&name, Name);
  (void)ZwSetValueKey(Key, &name, 0, REG_BINARY, (PVOID)Value, Bytes);
}

_Use_decl_annotations_ void AdmissionRecordService(
    PUNICODE_STRING RegistryPath, PCWSTR Name, ULONG Value) {
  OBJECT_ATTRIBUTES attributes;
  HANDLE key = NULL;
  InitializeObjectAttributes(&attributes, RegistryPath,
                             OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL,
                             NULL);
  if (!NT_SUCCESS(ZwOpenKey(&key, KEY_SET_VALUE, &attributes)))
    return;
  WriteDword(key, Name, Value);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordDevice(
    PDEVICE_OBJECT DeviceObject, ADMISSION_RECEIPT Receipt, NTSTATUS Status) {
  HANDLE key = NULL;
  if (DeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1CleanReceipt", (ULONG)Receipt);
  WriteDword(key, L"Wom1CleanStatus", (ULONG)Status);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordStartStage(
    ADMISSION_CONTEXT *Context, ADMISSION_START_STAGE Stage,
    NTSTATUS Status) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1StartStage", (ULONG)Stage);
  WriteDword(key, L"Wom1StartStatus", (ULONG)Status);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordPlatformStage(
    ADMISSION_CONTEXT *Context, ADMISSION_PLATFORM_STAGE Stage,
    NTSTATUS Status) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1PlatformStage", (ULONG)Stage);
  WriteDword(key, L"Wom1PlatformStatus", (ULONG)Status);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordBackendStartResult(
    ADMISSION_CONTEXT *Context, APPLE_AGX_BACKEND_RUNTIME_RESULT Result) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1BackendStartResult", (ULONG)Result);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordFirmwarePhase(
    ADMISSION_CONTEXT *Context, APPLE_AGX_FIRMWARE_PHASE Phase,
    APPLE_AGX_FIRMWARE_RESULT Result, ULONG CompletedMask) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1FirmwarePhase", (ULONG)Phase);
  WriteDword(key, L"Wom1FirmwareResult", (ULONG)Result);
  WriteDword(key, L"Wom1FirmwareCompletedMask", CompletedMask);
  if (Phase == AppleAgxFirmwareFailed) {
    WriteDword(key, L"Wom1FirmwareFailureResult", (ULONG)Result);
    WriteDword(key, L"Wom1FirmwareFailureMask", CompletedMask);
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordQuery(
    PDEVICE_OBJECT DeviceObject, DXGK_QUERYADAPTERINFOTYPE Type,
    ULONG OutputDataSize, NTSTATUS Status) {
  HANDLE key = NULL;
  if (DeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1CleanReceipt", (ULONG)AdmissionReceiptQueryAdapterInfo);
  WriteDword(key, L"Wom1CleanQueryType", (ULONG)Type);
  WriteDword(key, L"Wom1CleanQuerySize", OutputDataSize);
  WriteDword(key, L"Wom1CleanStatus", (ULONG)Status);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordMemoryQualification(
    PDEVICE_OBJECT DeviceObject,
    const ADMISSION_MEMORY_QUALIFICATION *Qualification) {
  HANDLE key = NULL;
  if (DeviceObject == NULL || Qualification == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1MemoryQualification", Qualification,
              sizeof(*Qualification));
  ZwClose(key);
}
