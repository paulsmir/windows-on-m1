#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryChildRelations(
    PVOID MiniportDeviceContext, PDXGK_CHILD_DESCRIPTOR ChildRelations,
    ULONG ChildRelationsSize) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL || !context->Started || ChildRelations == NULL)
    return STATUS_INVALID_PARAMETER;
  if (ChildRelationsSize < 2 * sizeof(DXGK_CHILD_DESCRIPTOR))
    return STATUS_BUFFER_TOO_SMALL;

  RtlZeroMemory(ChildRelations, ChildRelationsSize);
  ChildRelations[0].ChildDeviceType = TypeVideoOutput;
  ChildRelations[0].ChildCapabilities.HpdAwareness =
      HpdAwarenessAlwaysConnected;
  ChildRelations[0].ChildCapabilities.Type.VideoOutput.InterfaceTechnology =
      D3DKMDT_VOT_INTERNAL;
  ChildRelations[0]
      .ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness =
      D3DKMDT_MOA_NONE;
  ChildRelations[0].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes =
      FALSE;
  ChildRelations[0].AcpiUid = 0;
  ChildRelations[0].ChildUid = 0;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryChildStatus(
    PVOID MiniportDeviceContext, PDXGK_CHILD_STATUS ChildStatus,
    BOOLEAN NonDestructiveOnly) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;

  UNREFERENCED_PARAMETER(NonDestructiveOnly);
  if (context == NULL || !context->Started || ChildStatus == NULL ||
      ChildStatus->ChildUid != 0)
    return STATUS_INVALID_PARAMETER;
  if (ChildStatus->Type != StatusConnection)
    return STATUS_NOT_SUPPORTED;
  ChildStatus->HotPlug.Connected = TRUE;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryDeviceDescriptor(
    PVOID MiniportDeviceContext, ULONG ChildUid,
    PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL || !context->Started || DeviceDescriptor == NULL ||
      ChildUid != 0)
    return STATUS_INVALID_PARAMETER;
  return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetPointerPosition(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL || !context->Started || SetPointerPosition == NULL ||
      SetPointerPosition->VidPnSourceId != 0)
    return STATUS_INVALID_PARAMETER;
  if (!context->DisplayActive || !SetPointerPosition->Flags.Visible)
    return STATUS_SUCCESS;
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetPointerShape(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL || !context->Started || SetPointerShape == NULL ||
      SetPointerShape->VidPnSourceId != 0)
    return STATUS_INVALID_PARAMETER;
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiIsSupportedVidPn(
    CONST HANDLE MiniportDeviceContext,
    DXGKARG_ISSUPPORTEDVIDPN *IsSupportedVidPn) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  if (IsSupportedVidPn == NULL)
    return STATUS_INVALID_PARAMETER;
  IsSupportedVidPn->IsVidPnSupported = FALSE;
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRecommendFunctionalVidPn(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *RecommendFunctionalVidPn) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(RecommendFunctionalVidPn);
  return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiEnumVidPnCofuncModality(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *EnumCofuncModality) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return EnumCofuncModality == NULL ? STATUS_INVALID_PARAMETER
                                    : STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetVidPnSourceVisibility(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *SetVidPnSourceVisibility) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL || !context->Started ||
      SetVidPnSourceVisibility == NULL ||
      (SetVidPnSourceVisibility->VidPnSourceId != 0 &&
       SetVidPnSourceVisibility->VidPnSourceId != D3DDDI_ID_ALL))
    return STATUS_INVALID_PARAMETER;
  if (SetVidPnSourceVisibility->Visible) {
    context->SourceVisible = TRUE;
    return STATUS_SUCCESS;
  }
  if (!context->DisplayActive) {
    context->SourceVisible = FALSE;
    return STATUS_SUCCESS;
  }
  return STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCommitVidPn(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_COMMITVIDPN *CommitVidPn) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return CommitVidPn == NULL ? STATUS_INVALID_PARAMETER
                             : STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiUpdateActiveVidPnPresentPath(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return UpdateActiveVidPnPresentPath == NULL ? STATUS_INVALID_PARAMETER
                                              : STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRecommendMonitorModes(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_RECOMMENDMONITORMODES *RecommendMonitorModes) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  D3DKMDT_MONITOR_SOURCE_MODE *monitorMode = NULL;
  NTSTATUS status;

  if (context == NULL || !context->Started || RecommendMonitorModes == NULL ||
      RecommendMonitorModes->VideoPresentTargetId != 0 ||
      RecommendMonitorModes->pMonitorSourceModeSetInterface == NULL ||
      RecommendMonitorModes->pMonitorSourceModeSetInterface
              ->pfnCreateNewModeInfo == NULL ||
      RecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode ==
          NULL ||
      RecommendMonitorModes->pMonitorSourceModeSetInterface
              ->pfnReleaseModeInfo == NULL)
    return STATUS_INVALID_PARAMETER;

  status = RecommendMonitorModes->pMonitorSourceModeSetInterface
               ->pfnCreateNewModeInfo(
                   RecommendMonitorModes->hMonitorSourceModeSet,
                   &monitorMode);
  if (!NT_SUCCESS(status))
    return status;

  RtlZeroMemory(monitorMode, sizeof(*monitorMode));
  monitorMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
  monitorMode->VideoSignalInfo.TotalSize.cx = 2560;
  monitorMode->VideoSignalInfo.TotalSize.cy = 1600;
  monitorMode->VideoSignalInfo.ActiveSize =
      monitorMode->VideoSignalInfo.TotalSize;
  monitorMode->VideoSignalInfo.VSyncFreq.Numerator =
      D3DKMDT_FREQUENCY_NOTSPECIFIED;
  monitorMode->VideoSignalInfo.VSyncFreq.Denominator =
      D3DKMDT_FREQUENCY_NOTSPECIFIED;
  monitorMode->VideoSignalInfo.HSyncFreq.Numerator =
      D3DKMDT_FREQUENCY_NOTSPECIFIED;
  monitorMode->VideoSignalInfo.HSyncFreq.Denominator =
      D3DKMDT_FREQUENCY_NOTSPECIFIED;
  monitorMode->VideoSignalInfo.PixelRate = D3DKMDT_FREQUENCY_NOTSPECIFIED;
  monitorMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
  monitorMode->Origin = D3DKMDT_MCO_DRIVER;
  monitorMode->Preference = D3DKMDT_MP_PREFERRED;
  monitorMode->ColorBasis = D3DKMDT_CB_SRGB;
  monitorMode->ColorCoeffDynamicRanges.FirstChannel = 8;
  monitorMode->ColorCoeffDynamicRanges.SecondChannel = 8;
  monitorMode->ColorCoeffDynamicRanges.ThirdChannel = 8;
  monitorMode->ColorCoeffDynamicRanges.FourthChannel = 8;

  status = RecommendMonitorModes->pMonitorSourceModeSetInterface->pfnAddMode(
      RecommendMonitorModes->hMonitorSourceModeSet, monitorMode);
  if (status == STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET)
    return STATUS_SUCCESS;
  if (!NT_SUCCESS(status)) {
    (void)RecommendMonitorModes->pMonitorSourceModeSetInterface
        ->pfnReleaseModeInfo(RecommendMonitorModes->hMonitorSourceModeSet,
                             monitorMode);
  }
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryVidPnHWCapability(
    CONST HANDLE MiniportDeviceContext,
    DXGKARG_QUERYVIDPNHWCAPABILITY *VidPnHWCaps) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;

  if (context == NULL || !context->Started || VidPnHWCaps == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&VidPnHWCaps->VidPnHWCaps,
                sizeof(VidPnHWCaps->VidPnHWCaps));
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetVidPnSourceAddress(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_SETVIDPNSOURCEADDRESS *SetVidPnSourceAddress) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return SetVidPnSourceAddress == NULL ? STATUS_INVALID_PARAMETER
                                       : STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS
AdmissionDdiStopDeviceAndReleasePostDisplayOwnership(
    PVOID MiniportDeviceContext, D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    PDXGK_DISPLAY_INFORMATION DisplayInfo) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  UNREFERENCED_PARAMETER(TargetId);
  return DisplayInfo == NULL ? STATUS_INVALID_PARAMETER
                             : STATUS_NOT_SUPPORTED;
}
