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

_Use_decl_annotations_ void AdmissionRecordProviderBootstrap(
    ADMISSION_CONTEXT *Context, ULONG Phase, UCHAR Success, ULONG State) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1ProviderBootPhase", Phase);
  WriteDword(key, L"Wom1ProviderBootSucceeded", Success);
  WriteDword(key, L"Wom1ProviderBootOwnedState", State);
  if (Phase == APPLE_AGX_PROVIDER_BOOT_CLEANUP)
    WriteDword(key, L"Wom1ProviderBootCleanupSucceeded", Success);
  else if (!Success)
    WriteDword(key, L"Wom1ProviderBootFailurePhase", Phase);
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

_Use_decl_annotations_ void AdmissionRecordFirmwarePowerOn(
    ADMISSION_CONTEXT *Context, BOOLEAN Acquired, ULONG State, ULONG Result,
    ULONGLONG ReceiptSequence) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(
          Context->PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE,
          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1FirmwarePowerAcquire", Acquired ? 1u : 0u);
  WriteDword(key, L"Wom1FirmwarePowerState", State);
  WriteDword(key, L"Wom1FirmwarePowerResult", Result);
  {
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"Wom1FirmwarePowerReceiptSequence");
    (void)ZwSetValueKey(key, &name, 0, REG_QWORD, &ReceiptSequence,
                        sizeof(ReceiptSequence));
  }
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordRtkitBoot(
    ADMISSION_CONTEXT *Context, APPLE_AGX_RTKIT_SESSION_RESULT Result,
    const APPLE_AGX_RTKIT_SESSION *Session) {
  HANDLE key = NULL;
  if (Context == NULL || Session == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1RtkitBootResult", (ULONG)Result);
  WriteDword(key, L"Wom1RtkitBootPhase", (ULONG)Session->Boot.Phase);
  WriteDword(key, L"Wom1RtkitCpuReady", (ULONG)Session->CpuReady);
  WriteDword(key, L"Wom1RtkitBootBegun", (ULONG)Session->Boot.Begun);
  WriteDword(key, L"Wom1RtkitHelloSeen", (ULONG)Session->Boot.HelloSeen);
  WriteDword(key, L"Wom1RtkitEndpointMap", (ULONG)Session->Boot.EndpointMapComplete);
  WriteDword(key, L"Wom1RtkitIopPower", (ULONG)Session->Boot.IopPowerReady);
  WriteDword(key, L"Wom1RtkitApPower", (ULONG)Session->Boot.ApPowerReady);
  WriteDword(key, L"Wom1RtkitInboxControl", Session->InboxControlAtFailure);
  WriteDword(key, L"Wom1RtkitOutboxControl", Session->OutboxControlAtFailure);
  WriteDword(key, L"Wom1RtkitReceivedCount", Session->ReceivedCount);
  WriteDword(key, L"Wom1RtkitLastRxEndpoint", Session->LastRxEndpoint);
  WriteDword(key, L"Wom1RtkitLastRxPayloadLow", (ULONG)Session->LastRxPayload);
  WriteDword(key, L"Wom1RtkitLastRxPayloadHigh",
              (ULONG)(Session->LastRxPayload >> 32));
  WriteDword(key, L"Wom1RtkitEndpointMap0", Session->Boot.EndpointMap[0]);
  WriteDword(key, L"Wom1RtkitEndpointMap1", Session->Boot.EndpointMap[1]);
  WriteDword(key, L"Wom1RtkitCrashlogRequestedBytes", Session->CrashlogRequestedBytes);
  WriteDword(key, L"Wom1RtkitCrashlogCapacityBytes", Session->CrashlogCapacityBytes);
  WriteDword(key, L"Wom1RtkitCrashlogReplySent", Session->CrashlogReplySent);
  WriteDword(key, L"Wom1RtkitCrashlogCrashed", Session->CrashlogCrashed);
  WriteDword(key, L"Wom1RtkitCrashlogGpuVaLow", (ULONG)Session->CrashlogGpuAddress);
  WriteDword(key, L"Wom1RtkitCrashlogGpuVaHigh", (ULONG)(Session->CrashlogGpuAddress >> 32));
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordRtkitCrashlog(
    ADMISSION_CONTEXT *Context, const VOID *Data, ULONG Bytes) {
  HANDLE key = NULL;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL || Data == NULL ||
      Bytes != APPLE_AGX_RTKIT_CRASHLOG_BYTES ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteBinary(key, L"Wom1RtkitCrashlogImage", Data, Bytes);
  ZwClose(key);
}

_Use_decl_annotations_ void AdmissionRecordPreManagementUat(
    ADMISSION_CONTEXT *Context, BOOLEAN Published,
    const APPLE_AGX_UAT_PUBLICATION_STATE *State) {
  HANDLE key = NULL;
  if (Context == NULL || State == NULL || Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  WriteDword(key, L"Wom1PreManagementUatPublished", Published ? 1u : 0u);
  WriteDword(key, L"Wom1PreManagementUatActive", State->Active);
  WriteDword(key, L"Wom1PreManagementUatTtbr0Low", (ULONG)State->PublishedTtbr0);
  WriteDword(key, L"Wom1PreManagementUatTtbr0High", (ULONG)(State->PublishedTtbr0 >> 32));
  WriteDword(key, L"Wom1PreManagementUatTtbr1Low", (ULONG)State->PublishedTtbr1);
  WriteDword(key, L"Wom1PreManagementUatTtbr1High", (ULONG)(State->PublishedTtbr1 >> 32));
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
