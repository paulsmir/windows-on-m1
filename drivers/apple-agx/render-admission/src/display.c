#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetPointerPosition(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return SetPointerPosition == NULL ? STATUS_INVALID_PARAMETER
                                    : STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSetPointerShape(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return SetPointerShape == NULL ? STATUS_INVALID_PARAMETER
                                 : STATUS_NOT_SUPPORTED;
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
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return SetVidPnSourceVisibility == NULL ? STATUS_INVALID_PARAMETER
                                          : STATUS_NOT_SUPPORTED;
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
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return RecommendMonitorModes == NULL ? STATUS_INVALID_PARAMETER
                                       : STATUS_NOT_SUPPORTED;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryVidPnHWCapability(
    CONST HANDLE MiniportDeviceContext,
    DXGKARG_QUERYVIDPNHWCAPABILITY *VidPnHWCaps) {
  UNREFERENCED_PARAMETER(MiniportDeviceContext);
  return VidPnHWCaps == NULL ? STATUS_INVALID_PARAMETER
                             : STATUS_NOT_SUPPORTED;
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
