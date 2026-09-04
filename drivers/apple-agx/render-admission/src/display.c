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
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  CONST DXGK_VIDPN_INTERFACE *vidPnInterface = NULL;
  CONST DXGK_VIDPNTOPOLOGY_INTERFACE *topologyInterface = NULL;
  CONST D3DKMDT_VIDPN_PRESENT_PATH *path = NULL;
  CONST D3DKMDT_VIDPN_PRESENT_PATH *nextPath = NULL;
  D3DKMDT_HVIDPNTOPOLOGY topology = 0;
  NTSTATUS status;

  if (context == NULL || !context->Started || IsSupportedVidPn == NULL)
    return STATUS_INVALID_PARAMETER;
  IsSupportedVidPn->IsVidPnSupported = FALSE;
  if (IsSupportedVidPn->hDesiredVidPn == 0) {
    IsSupportedVidPn->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
  }

  status = context->Interface.DxgkCbQueryVidPnInterface(
      IsSupportedVidPn->hDesiredVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1,
      &vidPnInterface);
  if (!NT_SUCCESS(status))
    return status;
  status = vidPnInterface->pfnGetTopology(IsSupportedVidPn->hDesiredVidPn,
                                          &topology,
                                          &topologyInterface);
  if (!NT_SUCCESS(status))
    return status;
  status = topologyInterface->pfnAcquireFirstPathInfo(topology, &path);
  if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) {
    IsSupportedVidPn->IsVidPnSupported = TRUE;
    return STATUS_SUCCESS;
  }
  if (!NT_SUCCESS(status))
    return status;
  if (path->VidPnSourceId != 0 || path->VidPnTargetId != 0) {
    status = STATUS_SUCCESS;
    goto Exit;
  }
  status = topologyInterface->pfnAcquireNextPathInfo(topology, path,
                                                      &nextPath);
  if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) {
    status = STATUS_SUCCESS;
    IsSupportedVidPn->IsVidPnSupported = TRUE;
  } else if (NT_SUCCESS(status)) {
    status = STATUS_SUCCESS;
  }

Exit:
  if (nextPath != NULL)
    topologyInterface->pfnReleasePathInfo(topology, nextPath);
  if (path != NULL)
    topologyInterface->pfnReleasePathInfo(topology, path);
  return status;
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
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  CONST DXGK_VIDPN_INTERFACE *vidPnInterface = NULL;
  CONST DXGK_VIDPNTOPOLOGY_INTERFACE *topologyInterface = NULL;
  CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *sourceSetInterface = NULL;
  CONST DXGK_VIDPNTARGETMODESET_INTERFACE *targetSetInterface = NULL;
  CONST D3DKMDT_VIDPN_PRESENT_PATH *path = NULL;
  CONST D3DKMDT_VIDPN_PRESENT_PATH *nextPath = NULL;
  CONST D3DKMDT_VIDPN_SOURCE_MODE *pinnedSourceMode = NULL;
  CONST D3DKMDT_VIDPN_TARGET_MODE *pinnedTargetMode = NULL;
  D3DKMDT_VIDPN_SOURCE_MODE *sourceMode = NULL;
  D3DKMDT_VIDPN_TARGET_MODE *targetMode = NULL;
  D3DKMDT_HVIDPNTOPOLOGY topology = 0;
  D3DKMDT_HVIDPNSOURCEMODESET sourceSet = 0;
  D3DKMDT_HVIDPNTARGETMODESET targetSet = 0;
  NTSTATUS status = STATUS_INVALID_PARAMETER;

  if (context == NULL || !context->Started || EnumCofuncModality == NULL ||
      EnumCofuncModality->hConstrainingVidPn == 0)
    goto Exit;
  status = context->Interface.DxgkCbQueryVidPnInterface(
      EnumCofuncModality->hConstrainingVidPn,
      DXGK_VIDPN_INTERFACE_VERSION_V1, &vidPnInterface);
  if (!NT_SUCCESS(status))
    goto Exit;
  status = vidPnInterface->pfnGetTopology(
      EnumCofuncModality->hConstrainingVidPn, &topology,
      &topologyInterface);
  if (!NT_SUCCESS(status))
    goto Exit;
  status = topologyInterface->pfnAcquireFirstPathInfo(topology, &path);
  if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET) {
    status = STATUS_SUCCESS;
    goto Exit;
  }
  if (!NT_SUCCESS(status))
    goto Exit;
  if (path->VidPnSourceId != 0 || path->VidPnTargetId != 0) {
    status = STATUS_GRAPHICS_VIDPN_TOPOLOGY_NOT_SUPPORTED;
    goto Exit;
  }

  status = vidPnInterface->pfnAcquireSourceModeSet(
      EnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId,
      &sourceSet, &sourceSetInterface);
  if (!NT_SUCCESS(status))
    goto Exit;
  status = sourceSetInterface->pfnAcquirePinnedModeInfo(sourceSet,
                                                        &pinnedSourceMode);
  if (!NT_SUCCESS(status))
    goto Exit;
  if (pinnedSourceMode == NULL &&
      !(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNSOURCE &&
        EnumCofuncModality->EnumPivot.VidPnSourceId == path->VidPnSourceId)) {
    status = vidPnInterface->pfnReleaseSourceModeSet(
        EnumCofuncModality->hConstrainingVidPn, sourceSet);
    if (!NT_SUCCESS(status))
      goto Exit;
    sourceSet = 0;
    sourceSetInterface = NULL;
    status = vidPnInterface->pfnCreateNewSourceModeSet(
        EnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId,
        &sourceSet, &sourceSetInterface);
    if (!NT_SUCCESS(status))
      goto Exit;
    status = sourceSetInterface->pfnCreateNewModeInfo(sourceSet, &sourceMode);
    if (!NT_SUCCESS(status))
      goto Exit;
    RtlZeroMemory(sourceMode, sizeof(*sourceMode));
    sourceMode->Type = D3DKMDT_RMT_GRAPHICS;
    sourceMode->Format.Graphics.PrimSurfSize.cx = 2560;
    sourceMode->Format.Graphics.PrimSurfSize.cy = 1600;
    sourceMode->Format.Graphics.VisibleRegionSize =
        sourceMode->Format.Graphics.PrimSurfSize;
    sourceMode->Format.Graphics.Stride = 10240;
    sourceMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
    sourceMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
    sourceMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
    status = sourceSetInterface->pfnAddMode(sourceSet, sourceMode);
    if (!NT_SUCCESS(status))
      goto Exit;
    sourceMode = NULL;
    status = vidPnInterface->pfnAssignSourceModeSet(
        EnumCofuncModality->hConstrainingVidPn, path->VidPnSourceId,
        sourceSet);
    if (!NT_SUCCESS(status))
      goto Exit;
    sourceSet = 0;
    sourceSetInterface = NULL;
  }

  if (!(EnumCofuncModality->EnumPivotType == D3DKMDT_EPT_VIDPNTARGET &&
        EnumCofuncModality->EnumPivot.VidPnTargetId == path->VidPnTargetId)) {
    status = vidPnInterface->pfnAcquireTargetModeSet(
        EnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId,
        &targetSet, &targetSetInterface);
    if (!NT_SUCCESS(status))
      goto Exit;
    status = targetSetInterface->pfnAcquirePinnedModeInfo(targetSet,
                                                          &pinnedTargetMode);
    if (!NT_SUCCESS(status))
      goto Exit;
    if (pinnedTargetMode == NULL) {
      status = vidPnInterface->pfnReleaseTargetModeSet(
          EnumCofuncModality->hConstrainingVidPn, targetSet);
      if (!NT_SUCCESS(status))
        goto Exit;
      targetSet = 0;
      targetSetInterface = NULL;
      status = vidPnInterface->pfnCreateNewTargetModeSet(
          EnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId,
          &targetSet, &targetSetInterface);
      if (!NT_SUCCESS(status))
        goto Exit;
      status = targetSetInterface->pfnCreateNewModeInfo(targetSet,
                                                        &targetMode);
      if (!NT_SUCCESS(status))
        goto Exit;
      RtlZeroMemory(targetMode, sizeof(*targetMode));
      targetMode->VideoSignalInfo.VideoStandard = D3DKMDT_VSS_OTHER;
      targetMode->VideoSignalInfo.TotalSize.cx = 2560;
      targetMode->VideoSignalInfo.TotalSize.cy = 1600;
      targetMode->VideoSignalInfo.ActiveSize =
          targetMode->VideoSignalInfo.TotalSize;
      targetMode->VideoSignalInfo.VSyncFreq.Numerator =
          D3DKMDT_FREQUENCY_NOTSPECIFIED;
      targetMode->VideoSignalInfo.VSyncFreq.Denominator =
          D3DKMDT_FREQUENCY_NOTSPECIFIED;
      targetMode->VideoSignalInfo.HSyncFreq.Numerator =
          D3DKMDT_FREQUENCY_NOTSPECIFIED;
      targetMode->VideoSignalInfo.HSyncFreq.Denominator =
          D3DKMDT_FREQUENCY_NOTSPECIFIED;
      targetMode->VideoSignalInfo.PixelRate =
          D3DKMDT_FREQUENCY_NOTSPECIFIED;
      targetMode->VideoSignalInfo.ScanLineOrdering =
          D3DDDI_VSSLO_PROGRESSIVE;
      targetMode->Preference = D3DKMDT_MP_PREFERRED;
      status = targetSetInterface->pfnAddMode(targetSet, targetMode);
      if (!NT_SUCCESS(status))
        goto Exit;
      targetMode = NULL;
      status = vidPnInterface->pfnAssignTargetModeSet(
          EnumCofuncModality->hConstrainingVidPn, path->VidPnTargetId,
          targetSet);
      if (!NT_SUCCESS(status))
        goto Exit;
      targetSet = 0;
      targetSetInterface = NULL;
    }
  }

  {
    D3DKMDT_VIDPN_PRESENT_PATH updatedPath = *path;
    BOOLEAN changed = FALSE;
    if (path->ContentTransformation.Scaling == D3DKMDT_VPPS_UNPINNED) {
      RtlZeroMemory(&updatedPath.ContentTransformation.ScalingSupport,
                    sizeof(updatedPath.ContentTransformation.ScalingSupport));
      updatedPath.ContentTransformation.ScalingSupport.Identity = 1;
      changed = TRUE;
    }
    if (path->ContentTransformation.Rotation == D3DKMDT_VPPR_UNPINNED) {
      RtlZeroMemory(&updatedPath.ContentTransformation.RotationSupport,
                    sizeof(updatedPath.ContentTransformation.RotationSupport));
      updatedPath.ContentTransformation.RotationSupport.Identity = 1;
      updatedPath.ContentTransformation.RotationSupport.Offset0 = 1;
      changed = TRUE;
    }
    if (changed) {
      status = topologyInterface->pfnUpdatePathSupportInfo(topology,
                                                           &updatedPath);
      if (!NT_SUCCESS(status))
        goto Exit;
    }
  }

  status = topologyInterface->pfnAcquireNextPathInfo(topology, path,
                                                      &nextPath);
  if (status == STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET)
    status = STATUS_SUCCESS;
  else if (NT_SUCCESS(status))
    status = STATUS_GRAPHICS_VIDPN_TOPOLOGY_NOT_SUPPORTED;

Exit:
  if (sourceMode != NULL && sourceSetInterface != NULL && sourceSet != 0)
    sourceSetInterface->pfnReleaseModeInfo(sourceSet, sourceMode);
  if (targetMode != NULL && targetSetInterface != NULL && targetSet != 0)
    targetSetInterface->pfnReleaseModeInfo(targetSet, targetMode);
  if (pinnedSourceMode != NULL && sourceSetInterface != NULL && sourceSet != 0)
    sourceSetInterface->pfnReleaseModeInfo(sourceSet, pinnedSourceMode);
  if (pinnedTargetMode != NULL && targetSetInterface != NULL && targetSet != 0)
    targetSetInterface->pfnReleaseModeInfo(targetSet, pinnedTargetMode);
  if (sourceSet != 0 && vidPnInterface != NULL)
    vidPnInterface->pfnReleaseSourceModeSet(
        EnumCofuncModality->hConstrainingVidPn, sourceSet);
  if (targetSet != 0 && vidPnInterface != NULL)
    vidPnInterface->pfnReleaseTargetModeSet(
        EnumCofuncModality->hConstrainingVidPn, targetSet);
  if (nextPath != NULL && topologyInterface != NULL)
    topologyInterface->pfnReleasePathInfo(topology, nextPath);
  if (path != NULL && topologyInterface != NULL)
    topologyInterface->pfnReleasePathInfo(topology, path);
  return status;
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
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  CONST DXGK_VIDPN_INTERFACE *vidPnInterface = NULL;
  CONST DXGK_VIDPNTOPOLOGY_INTERFACE *topologyInterface = NULL;
  CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *sourceSetInterface = NULL;
  CONST D3DKMDT_VIDPN_SOURCE_MODE *pinnedSourceMode = NULL;
  CONST D3DKMDT_VIDPN_PRESENT_PATH *path = NULL;
  D3DKMDT_HVIDPNTOPOLOGY topology = 0;
  D3DKMDT_HVIDPNSOURCEMODESET sourceSet = 0;
  SIZE_T numberOfPaths = 0;
  NTSTATUS status = STATUS_INVALID_PARAMETER;

  if (context == NULL || !context->Started || CommitVidPn == NULL ||
      CommitVidPn->AffectedVidPnSourceId != 0)
    goto Exit;
  if (CommitVidPn->Flags.PathPoweredOff || CommitVidPn->hFunctionalVidPn == 0) {
    context->DisplayActive = FALSE;
    status = STATUS_SUCCESS;
    goto Exit;
  }
  status = context->Interface.DxgkCbQueryVidPnInterface(
      CommitVidPn->hFunctionalVidPn, DXGK_VIDPN_INTERFACE_VERSION_V1,
      &vidPnInterface);
  if (!NT_SUCCESS(status))
    goto Exit;
  status = vidPnInterface->pfnGetTopology(CommitVidPn->hFunctionalVidPn,
                                          &topology,
                                          &topologyInterface);
  if (!NT_SUCCESS(status))
    goto Exit;
  status = topologyInterface->pfnGetNumPathsFromSource(
      topology, CommitVidPn->AffectedVidPnSourceId, &numberOfPaths);
  if (status == STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY) {
    context->DisplayActive = FALSE;
    status = STATUS_SUCCESS;
    goto Exit;
  }
  if (!NT_SUCCESS(status))
    goto Exit;
  if (numberOfPaths == 0) {
    context->DisplayActive = FALSE;
    status = STATUS_SUCCESS;
    goto Exit;
  }
  if (numberOfPaths != 1) {
    status = STATUS_GRAPHICS_VIDPN_TOPOLOGY_NOT_SUPPORTED;
    goto Exit;
  }
  status = vidPnInterface->pfnAcquireSourceModeSet(
      CommitVidPn->hFunctionalVidPn, CommitVidPn->AffectedVidPnSourceId,
      &sourceSet, &sourceSetInterface);
  if (!NT_SUCCESS(status))
    goto Exit;
  status = sourceSetInterface->pfnAcquirePinnedModeInfo(sourceSet,
                                                        &pinnedSourceMode);
  if (!NT_SUCCESS(status))
    goto Exit;
  if (pinnedSourceMode == NULL ||
      pinnedSourceMode->Type != D3DKMDT_RMT_GRAPHICS ||
      pinnedSourceMode->Format.Graphics.PrimSurfSize.cx != 2560 ||
      pinnedSourceMode->Format.Graphics.PrimSurfSize.cy != 1600 ||
      pinnedSourceMode->Format.Graphics.Stride != 10240 ||
      pinnedSourceMode->Format.Graphics.PixelFormat !=
          D3DDDIFMT_A8R8G8B8 ||
      pinnedSourceMode->Format.Graphics.PixelValueAccessMode !=
          D3DKMDT_PVAM_DIRECT) {
    status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    goto Exit;
  }
  status = topologyInterface->pfnAcquireFirstPathInfo(topology, &path);
  if (!NT_SUCCESS(status))
    goto Exit;
  if (path->VidPnSourceId != 0 || path->VidPnTargetId != 0) {
    status = STATUS_GRAPHICS_VIDPN_TOPOLOGY_NOT_SUPPORTED;
    goto Exit;
  }
  context->CommittedWidth = 2560;
  context->CommittedHeight = 1600;
  context->CommittedStride = 10240;
  context->CommittedFormat = D3DDDIFMT_A8R8G8B8;
  context->DisplayActive = TRUE;
  status = STATUS_SUCCESS;

Exit:
  if (path != NULL && topologyInterface != NULL)
    topologyInterface->pfnReleasePathInfo(topology, path);
  if (pinnedSourceMode != NULL && sourceSetInterface != NULL && sourceSet != 0)
    sourceSetInterface->pfnReleaseModeInfo(sourceSet, pinnedSourceMode);
  if (sourceSet != 0 && vidPnInterface != NULL && CommitVidPn != NULL &&
      CommitVidPn->hFunctionalVidPn != 0)
    vidPnInterface->pfnReleaseSourceModeSet(
        CommitVidPn->hFunctionalVidPn, sourceSet);
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiUpdateActiveVidPnPresentPath(
    CONST HANDLE MiniportDeviceContext,
    CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *UpdateActiveVidPnPresentPath) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  CONST D3DKMDT_VIDPN_PRESENT_PATH *path;

  if (context == NULL || !context->Started ||
      UpdateActiveVidPnPresentPath == NULL)
    return STATUS_INVALID_PARAMETER;
  path = &UpdateActiveVidPnPresentPath->VidPnPresentPathInfo;
  if (path->VidPnSourceId != 0)
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
  if (path->VidPnTargetId != 0)
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
  if (path->GammaRamp.Type != D3DDDI_GAMMARAMP_DEFAULT)
    return STATUS_GRAPHICS_GAMMA_RAMP_NOT_SUPPORTED;
  if (path->ContentTransformation.Scaling != D3DKMDT_VPPS_IDENTITY &&
      path->ContentTransformation.Scaling != D3DKMDT_VPPS_NOTSPECIFIED &&
      path->ContentTransformation.Scaling != D3DKMDT_VPPS_UNINITIALIZED)
    return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
  if (path->ContentTransformation.Rotation != D3DKMDT_VPPR_IDENTITY &&
      path->ContentTransformation.Rotation != D3DKMDT_VPPR_NOTSPECIFIED &&
      path->ContentTransformation.Rotation != D3DKMDT_VPPR_UNINITIALIZED)
    return STATUS_GRAPHICS_VIDPN_MODALITY_NOT_SUPPORTED;
  return STATUS_SUCCESS;
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
