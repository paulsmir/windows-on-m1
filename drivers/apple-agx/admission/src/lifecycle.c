#include "apple_agx_admission.h"

static VOID AppleAgxAdmissionWriteDword(_In_ HANDLE Key, _In_ PCWSTR Name,
                                        _In_ ULONG Value) {
  UNICODE_STRING valueName;

  RtlInitUnicodeString(&valueName, Name);
  (VOID)ZwSetValueKey(Key, &valueName, 0, REG_DWORD, &Value, sizeof(Value));
}

VOID AppleAgxAdmissionRecord(_In_ PDEVICE_OBJECT PhysicalDeviceObject,
                             _In_ PCWSTR StageName, _In_ ULONG Stage,
                             _In_opt_ PCWSTR StatusName,
                             _In_ NTSTATUS Status) {
  HANDLE key = NULL;

  if (PhysicalDeviceObject == NULL || StageName == NULL)
    return;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;

  AppleAgxAdmissionWriteDword(key, StageName, Stage);
  if (StatusName != NULL)
    AppleAgxAdmissionWriteDword(key, StatusName, (ULONG)Status);
  ZwClose(key);
}

_Use_decl_annotations_ NTSTATUS AppleAgxAdmissionAddDevice(
    PDEVICE_OBJECT PhysicalDeviceObject, PVOID *MiniportDeviceContext) {
  APPLE_AGX_ADMISSION_CONTEXT *context;

  if (PhysicalDeviceObject == NULL || MiniportDeviceContext == NULL)
    return STATUS_INVALID_PARAMETER;

  AppleAgxAdmissionRecord(PhysicalDeviceObject,
                          L"Wom1AdmissionAddDeviceStage", 1,
                          L"Wom1AdmissionAddDeviceStatus", STATUS_PENDING);
  context = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*context),
                            APPLE_AGX_ADMISSION_POOL_TAG);
  if (context == NULL) {
    AppleAgxAdmissionRecord(PhysicalDeviceObject,
                            L"Wom1AdmissionAddDeviceStage", 2,
                            L"Wom1AdmissionAddDeviceStatus",
                            STATUS_INSUFFICIENT_RESOURCES);
    return STATUS_INSUFFICIENT_RESOURCES;
  }

  context->PhysicalDeviceObject = PhysicalDeviceObject;
  *MiniportDeviceContext = context;
  AppleAgxAdmissionRecord(PhysicalDeviceObject,
                          L"Wom1AdmissionAddDeviceStage", 2,
                          L"Wom1AdmissionAddDeviceStatus", STATUS_SUCCESS);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AppleAgxAdmissionStartDevice(
    PVOID MiniportDeviceContext, PDXGK_START_INFO DxgkStartInfo,
    PDXGKRNL_INTERFACE DxgkInterface, PULONG NumberOfVideoPresentSources,
    PULONG NumberOfChildren) {
  APPLE_AGX_ADMISSION_CONTEXT *context =
      (APPLE_AGX_ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL || DxgkStartInfo == NULL || DxgkInterface == NULL ||
      NumberOfVideoPresentSources == NULL || NumberOfChildren == NULL)
    return STATUS_INVALID_PARAMETER;

  *NumberOfVideoPresentSources = 0;
  *NumberOfChildren = 0;
  AppleAgxAdmissionRecord(context->PhysicalDeviceObject,
                          L"Wom1AdmissionStartDeviceStage", 1,
                          L"Wom1AdmissionStartDeviceStatus",
                          STATUS_NOT_SUPPORTED);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxAdmissionStopDevice(PVOID MiniportDeviceContext) {
  return MiniportDeviceContext == NULL ? STATUS_INVALID_PARAMETER
                                       : STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS
AppleAgxAdmissionRemoveDevice(PVOID MiniportDeviceContext) {
  APPLE_AGX_ADMISSION_CONTEXT *context =
      (APPLE_AGX_ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  ExFreePoolWithTag(context, APPLE_AGX_ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AppleAgxAdmissionDispatchIoRequest(
    PVOID MiniportDeviceContext, ULONG VidPnSourceId,
    PVIDEO_REQUEST_PACKET VideoRequestPacket) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(VidPnSourceId);
  UNREFERENCED_PARAMETER(VideoRequestPacket);
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ VOID AppleAgxAdmissionUnload(VOID) {}
