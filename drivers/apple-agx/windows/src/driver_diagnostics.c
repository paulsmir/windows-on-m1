#include "apple_agx_driver.h"

#include <ntstrsafe.h>

#define APPLE_AGX_DIAGNOSTIC_RESOURCE_LIMIT 16

static void AppleAgxWriteDiagnosticDword(PUNICODE_STRING RegistryPath,
                                         PCWSTR ValueName, ULONG Value) {
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
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
                                               PCWSTR ValueName, ULONG Value) {
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  UNICODE_STRING valueName;
  HANDLE key = NULL;

  if (DeviceObject == NULL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
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

#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                               \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION)
void AppleAgxRecordMmioQualification(
    PDEVICE_OBJECT DeviceObject, APPLE_AGX_MMIO_STAGE Stage, NTSTATUS Status,
    const APPLE_AGX_MAPPING_STATE *MappingState) {
  ULONGLONG sgxStart;

  switch (Stage) {
  case AppleAgxMmioMapped:
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1MmioMapStatus",
                                       (ULONG)Status);
    if (!NT_SUCCESS(Status) || MappingState == NULL)
      return;
    sgxStart = MappingState->SgxPhysicalAddress;
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1MmioSgxStartLow",
                                       (ULONG)sgxStart);
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1MmioSgxStartHigh",
                                       (ULONG)(sgxStart >> 32));
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1MmioSgxLength",
                                       MappingState->SgxLength);
    break;
  case AppleAgxMmioSubviewValidated:
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1MmioSubviewStatus",
                                       (ULONG)Status);
    if (!NT_SUCCESS(Status) || MappingState == NULL ||
        MappingState->SgxBase == NULL || MappingState->AscBase == NULL)
      return;
    AppleAgxWriteDeviceDiagnosticDword(
        DeviceObject, L"Wom1MmioAscOffset",
        (ULONG)(MappingState->AscBase - MappingState->SgxBase));
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1MmioAscLength",
                                       J313_AGX_G2_ASC_MMIO_SIZE);
    break;
  case AppleAgxMmioUnmapped:
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1MmioUnmapStatus",
                                       (ULONG)Status);
    break;
  default:
    break;
  }
}
#endif

#ifdef APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION
void AppleAgxRecordPowerSession(PDEVICE_OBJECT DeviceObject,
                                APPLE_AGX_POWER_RECEIPT Receipt,
                                NTSTATUS Status) {
  switch (Receipt) {
  case AppleAgxPowerReceiptAcquired:
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject,
                                       L"Wom1PowerAcquireStatus",
                                       (ULONG)Status);
    break;
  case AppleAgxPowerReceiptReleased:
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject,
                                       L"Wom1PowerReleaseStatus",
                                       (ULONG)Status);
    break;
  default:
    break;
  }
}
#endif

#if defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                           \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION)
void AppleAgxRecordAscCpuStatus(PDEVICE_OBJECT DeviceObject, NTSTATUS Status,
                                ULONG CpuStatus) {
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1AscCpuStatusReadStatus", (ULONG)Status);
  if (NT_SUCCESS(Status))
    AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1AscCpuStatus",
                                       CpuStatus);
}
#endif

#ifdef APPLE_AGX_G2_RTKIT_QUALIFICATION
void AppleAgxRecordRtkitQualification(
    PDEVICE_OBJECT DeviceObject,
    const APPLE_AGX_RTKIT_QUALIFICATION_RESULT *Result) {
  if (Result == NULL)
    return;
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1RtkitBootStatus",
                                     (ULONG)Result->BootStatus);
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1RtkitStopStatus",
                                     (ULONG)Result->StopStatus);
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1RtkitBootPhase",
                                     Result->BootPhase);
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject, L"Wom1RtkitBootFlags",
                                     Result->BootFlags);
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1RtkitNegotiatedVersion",
      Result->NegotiatedVersion);
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1RtkitFinalCpuStatusReadStatus",
      (ULONG)Result->FinalCpuStatusReadStatus);
  AppleAgxWriteDeviceDiagnosticDword(DeviceObject,
                                     L"Wom1RtkitFinalCpuStatus",
                                     Result->FinalCpuStatus);
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1RtkitMailboxSnapshotFlags",
      Result->MailboxSnapshotFlags);
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1RtkitInboxControlBeforeInit",
      Result->InboxControlBeforeInit);
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1RtkitInboxControlAfterInit",
      Result->InboxControlAfterInit);
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1RtkitInboxControlAtFailure",
      Result->InboxControlAtFailure);
  AppleAgxWriteDeviceDiagnosticDword(
      DeviceObject, L"Wom1RtkitOutboxControlAtFailure",
      Result->OutboxControlAtFailure);
}
#endif

#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
static void AppleAgxWriteDiagnosticDwordToKey(HANDLE Key, PCWSTR ValueName,
                                              ULONG Value) {
  UNICODE_STRING valueName;

  RtlInitUnicodeString(&valueName, ValueName);
  (void)ZwSetValueKey(Key, &valueName, 0, REG_DWORD, &Value, sizeof(Value));
}

static void AppleAgxWriteIndexedDiagnosticDword(HANDLE Key, PCWSTR Format,
                                                ULONG Index, ULONG Value) {
  WCHAR valueName[64];

  if (!NT_SUCCESS(RtlStringCchPrintfW(valueName, RTL_NUMBER_OF(valueName),
                                      Format, Index)))
    return;
  AppleAgxWriteDiagnosticDwordToKey(Key, valueName, Value);
}
#endif

void AppleAgxRecordTranslatedResources(PDEVICE_OBJECT DeviceObject,
                                       PCM_RESOURCE_LIST TranslatedResources) {
#ifdef APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS
  HANDLE key = NULL;
  ULONG descriptorCount = 0;
  ULONG fullIndex;
  ULONG resourceIndex = 0;

  if (DeviceObject == NULL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;

  AppleAgxWriteDiagnosticDwordToKey(
      key, L"Wom1ResourceFullCount",
      TranslatedResources == NULL ? 0 : TranslatedResources->Count);
  if (TranslatedResources != NULL) {
    for (fullIndex = 0; fullIndex < TranslatedResources->Count; ++fullIndex)
      descriptorCount +=
          TranslatedResources->List[fullIndex].PartialResourceList.Count;
  }
  AppleAgxWriteDiagnosticDwordToKey(key, L"Wom1ResourceDescriptorCount",
                                    descriptorCount);
  AppleAgxWriteDiagnosticDwordToKey(
      key, L"Wom1ResourceOverflow",
      descriptorCount > APPLE_AGX_DIAGNOSTIC_RESOURCE_LIMIT ? 1 : 0);

  if (TranslatedResources == NULL) {
    ZwClose(key);
    return;
  }

  for (fullIndex = 0; fullIndex < TranslatedResources->Count; ++fullIndex) {
    PCM_FULL_RESOURCE_DESCRIPTOR full = &TranslatedResources->List[fullIndex];
    ULONG partialIndex;

    for (partialIndex = 0; partialIndex < full->PartialResourceList.Count;
         ++partialIndex, ++resourceIndex) {
      PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;

      if (resourceIndex >= APPLE_AGX_DIAGNOSTIC_RESOURCE_LIMIT)
        continue;
      descriptor = &full->PartialResourceList.PartialDescriptors[partialIndex];
      AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luType",
                                          resourceIndex, descriptor->Type);
      AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luShare",
                                          resourceIndex,
                                          descriptor->ShareDisposition);
      AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luFlags",
                                          resourceIndex, descriptor->Flags);

      if (descriptor->Type == CmResourceTypeMemory) {
        ULONGLONG start = (ULONGLONG)descriptor->u.Memory.Start.QuadPart;
        AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luStartLow",
                                            resourceIndex, (ULONG)start);
        AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luStartHigh",
                                            resourceIndex,
                                            (ULONG)(start >> 32));
        AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luLength",
                                            resourceIndex,
                                            descriptor->u.Memory.Length);
      } else if (descriptor->Type == CmResourceTypeInterrupt) {
        ULONGLONG affinity = (ULONGLONG)descriptor->u.Interrupt.Affinity;
        AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luLevel",
                                            resourceIndex,
                                            descriptor->u.Interrupt.Level);
        AppleAgxWriteIndexedDiagnosticDword(key, L"Wom1Resource%02luVector",
                                            resourceIndex,
                                            descriptor->u.Interrupt.Vector);
        AppleAgxWriteIndexedDiagnosticDword(key,
                                            L"Wom1Resource%02luAffinityLow",
                                            resourceIndex, (ULONG)affinity);
        AppleAgxWriteIndexedDiagnosticDword(
            key, L"Wom1Resource%02luAffinityHigh", resourceIndex,
            (ULONG)(affinity >> 32));
      }
    }
  }
  ZwClose(key);
#else
  UNREFERENCED_PARAMETER(DeviceObject);
  UNREFERENCED_PARAMETER(TranslatedResources);
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
