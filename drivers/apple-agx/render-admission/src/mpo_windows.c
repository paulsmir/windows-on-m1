#include "render_admission.h"

/* S2 kernel wiring of the two-plane MPO contract (specification v1,
 * amendments 2026-11-11).  Everything here is effective-only: dxgkrnl calls
 * DxgkDdiGetMultiPlaneOverlayCaps and DxgkDdiCheckMultiPlaneOverlaySupport3
 * only while DRIVERCAPS.SupportMultiPlaneOverlay is TRUE (R22: ADAPTER_
 * DISPLAY::GetMultiPlaneOverlayCaps+0x88 and DXGDEVICE::CheckMultiPlane
 * OverlaySupport3+0x5c return without the miniport otherwise), and
 * DRIVERCAPS sets it only while AdmissionScanoutMpoEffective; the Set3
 * handler in display.c enters AdmissionMpoSetVidPnSourceAddress3 only
 * then.  Not effective, the two DDIs still answer truthfully (one plane). */

/* IEEE-754 single 1.0: written as bits so no floating-point instruction is
 * generated in the kernel. */
static const ULONG AdmissionMpoFloatOne = 0x3f800000u;

static ULONG AdmissionMpoFormatClass(ADMISSION_CONTEXT *Context,
                                     const ADMISSION_ALLOCATION_DESCRIPTION
                                         *Description) {
  ULONG code;
  if (Description->BytesPerPixel != ADMISSION_MPO_BYTES_PER_PIXEL)
    return AdmissionMpoFormatOther;
  code = AdmissionScanoutFormatCode(Context,
                                    (D3DDDIFORMAT)Description->Format);
  if (code == APPLE_AGX_SCANOUT_FORMAT_BGRA8888)
    return AdmissionMpoFormatBgra8;
  if (code == APPLE_AGX_SCANOUT_FORMAT_RGBA8888)
    return AdmissionMpoFormatRgba8;
  return AdmissionMpoFormatOther;
}

static ADMISSION_MPO_RECT AdmissionMpoRect(const RECT *Rect) {
  ADMISSION_MPO_RECT rect;
  rect.Left = (int)Rect->left;
  rect.Top = (int)Rect->top;
  rect.Right = (int)Rect->right;
  rect.Bottom = (int)Rect->bottom;
  return rect;
}

/* One WDK plane -> neutral table input.  PlacementKnown planes (Set3) carry
 * their segment address; the pool offset is derived exactly as the V2
 * primary's (AdmissionScanoutQueuePresent: AppleAgxLocalSegmentAddress
 * ToGpuVa over the local segment, size rounded up to 64 KiB). */
static VOID AdmissionMpoPlaneInput(
    ADMISSION_CONTEXT *Context, UINT LayerIndex, BOOLEAN Enabled,
    HANDLE Allocation, const DXGK_MULTIPLANE_OVERLAY_ATTRIBUTES3 *Attributes,
    BOOLEAN PlacementKnown, UINT SegmentId, LONGLONG SegmentAddress,
    ADMISSION_MPO_PLANE_INPUT *Plane) {
  ADMISSION_ALLOCATION_HANDLE *allocation =
      (ADMISSION_ALLOCATION_HANDLE *)Allocation;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  ULONGLONG segment_base = 0ULL;
  ULONGLONG pool_bytes = 0ULL;
  APPLE_AGX_U64 offset = 0ULL;

  RtlZeroMemory(Plane, sizeof(*Plane));
  Plane->LayerIndex = LayerIndex;
  Plane->Enabled = Enabled ? 1u : 0u;
  Plane->FormatClass = AdmissionMpoFormatOther;
  Plane->SurfaceIdentity = (unsigned long long)(ULONG_PTR)Allocation;
  if (Attributes != NULL) {
    Plane->ColorSpaceClass =
        Attributes->ColorSpaceType == D3DDDI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
            ? AdmissionMpoColorSpaceSdrSrgb
            : AdmissionMpoColorSpaceOther;
    /* Any reserved blend bit makes the value "not 0/1". */
    Plane->BlendAlpha = Attributes->Blend.Value;
    Plane->Source = AdmissionMpoRect(&Attributes->SrcRect);
    Plane->Destination = AdmissionMpoRect(&Attributes->DstRect);
    Plane->Clip = AdmissionMpoRect(&Attributes->ClipRect);
    Plane->Rotation = Attributes->Rotation == D3DDDI_ROTATION_IDENTITY
                          ? AdmissionMpoRotationIdentity
                          : AdmissionMpoRotationOther;
    /* VerticalFlip / HorizontalFlip; StaticCheck (bit 2) is not a flip. */
    Plane->HasUnsupportedFlags = (Attributes->Flags.Value & 3u) != 0u ? 1u : 0u;
  } else {
    Plane->ColorSpaceClass = AdmissionMpoColorSpaceOther;
    Plane->Rotation = AdmissionMpoRotationOther;
  }
  if (allocation == NULL ||
      allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC)
    return;
  description = &allocation->Object.Description;
  Plane->FormatClass = AdmissionMpoFormatClass(Context, description);
  Plane->SurfaceWidth = description->Width;
  Plane->SurfaceHeight = description->Height;
  Plane->SurfaceStride = description->Pitch;
  /* The broker scans Stride * Height bytes and requires exactly that size
   * in the record.  A valid description's allocation holds at least that
   * (render_allocation.c AdmissionAllocationDescriptionValid; presentation
   * sizes are rounded up to 128 bytes, umd.c AdmissionUmdDescribePrimary),
   * so the scanned extent is the surface; an invalid description keeps its
   * own size and fails the table's size rule. */
  Plane->DescriptionValid =
      AdmissionAllocationDescriptionValid(description) ? 1u : 0u;
  Plane->SurfaceBytes =
      Plane->DescriptionValid != 0u
          ? (unsigned long long)description->Pitch * description->Height
          : description->Size;
  /* Version 3 (one-plane V2 rule): the raw allocation facts. */
  Plane->ScanoutCapable = allocation->ScanoutCapable != 0u ? 1u : 0u;
  Plane->AllocationBytes = description->Size;
  /* allocation_windows.c: a scanout-capable (linear, GPU-only, 32 bpp GDI
   * surface texture) allocation without a Mesa class is placed only in
   * the local segment (ADMISSION_LOCAL_SEGMENT_SET), which is the scanout
   * pool, at the 64 KiB alignment CreateAllocation requests. */
  Plane->InScanoutPool =
      allocation->ScanoutCapable != 0u && allocation->Win32ClassId == 0u
          ? 1u
          : 0u;
  if (!PlacementKnown)
    return;
  Plane->PlacementKnown = 1u;
  if (SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT || SegmentAddress <= 0 ||
      !AdmissionScanoutMpoPool(Context, &segment_base, &pool_bytes) ||
      AppleAgxLocalSegmentAddressToGpuVa(
          ADMISSION_MEMORY_LOCAL_SEGMENT, SegmentId, segment_base, pool_bytes,
          0ULL, (APPLE_AGX_U64)SegmentAddress,
          (description->Size + (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) &
              ~(APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL),
          0ULL, &offset) != AppleAgxLocalSegmentAddressOk) {
    Plane->InScanoutPool = 0u;
    return;
  }
  Plane->PlacementInPool = 1u;
  Plane->PoolOffset = offset;
}

static VOID AdmissionMpoInput(ADMISSION_CONTEXT *Context,
                              const ADMISSION_MPO_PLANE_INPUT *Planes,
                              ULONG Count, ADMISSION_MPO_INPUT *Input) {
  ULONGLONG segment_base = 0ULL;
  ULONGLONG pool_bytes = 0ULL;
  RtlZeroMemory(Input, sizeof(*Input));
  /* Windows plane rectangles are in source-mode pixels. */
  Input->PanelWidth = Context->CommittedWidth;
  Input->PanelHeight = Context->CommittedHeight;
  Input->PlaneCount = Count;
  Input->Planes = Planes;
  if (AdmissionScanoutMpoPool(Context, &segment_base, &pool_bytes))
    Input->PoolBytes = pool_bytes;
  /* The V2 gate of AdmissionDdiSetVidPnSourceAddress (display.c). */
  Input->PanelStride = Context->CommittedStride;
  Input->PanelModeSupported =
      AdmissionScanoutSourceModeSupported(Context, Context->CommittedWidth,
                                          Context->CommittedHeight,
                                          Context->CommittedStride) &&
              AdmissionScanoutFormatSupported(Context,
                                              Context->CommittedFormat)
          ? 1u
          : 0u;
}

/* ---- DxgkDdiGetMultiPlaneOverlayCaps ---- */

_Use_decl_annotations_ NTSTATUS AdmissionDdiGetMultiPlaneOverlayCaps(
    CONST HANDLE hAdapter, DXGKARG_GETMULTIPLANEOVERLAYCAPS *Caps) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)hAdapter;
  BOOLEAN effective;

  if (context == NULL || Caps == NULL)
    return STATUS_INVALID_PARAMETER;
  effective = AdmissionScanoutMpoEffective(context);
  /* Specification v1: MaxPlanes 2, MaxRGBPlanes 2, MaxYUVPlanes 0,
   * OverlayCaps 0, MaxStretchFactor 1.0, MaxShrinkFactor 1.0.  Not
   * effective (dxgkrnl does not call then, R22): one plane, the values
   * dxgkrnl itself substitutes (R22 Q1, defaults at +0x58..+0x78). */
  Caps->MaxPlanes = effective && Caps->VidPnSourceId == 0 ? 2u : 1u;
  Caps->MaxRGBPlanes = Caps->MaxPlanes;
  Caps->MaxYUVPlanes = 0u;
  Caps->OverlayCaps.Value = 0u;
  RtlCopyMemory(&Caps->MaxStretchFactor, &AdmissionMpoFloatOne,
                sizeof(Caps->MaxStretchFactor));
  RtlCopyMemory(&Caps->MaxShrinkFactor, &AdmissionMpoFloatOne,
                sizeof(Caps->MaxShrinkFactor));
  if (effective)
    InterlockedIncrement(&context->Mpo.CapsCalls);
  /* S2-1b: every call, ungated; persisted on the first calls only. */
  {
    const LONG all = InterlockedIncrement(&context->MpoGate.CapsCallsAll);
    if (all == 1)
      context->MpoGate.CapsFirstQpc = AdmissionMpoGateQpc(context);
    InterlockedExchange(&context->MpoGate.CapsLastVidPnSourceId,
                        (LONG)Caps->VidPnSourceId);
    InterlockedExchange(&context->MpoGate.CapsLastMaxPlanes,
                        (LONG)Caps->MaxPlanes);
    if (all <= 8 || (all & (all - 1)) == 0)
      AdmissionRecordMpoGate(context);
  }
  return STATUS_SUCCESS;
}

/* ---- DxgkDdiCheckMultiPlaneOverlaySupport3 ---- */

/* Post-composition transforms the panel cannot apply (no
 * DxgkDdiGetPostCompositionCaps; scaling, flips, rotation). */
static BOOLEAN AdmissionMpoPostCompositionIdentity(
    const DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 *Args) {
  UINT index;
  const DXGK_MULTIPLANE_OVERLAY_POST_COMPOSITION *post;
  if (Args->PostCompositionCount == 0u)
    return TRUE;
  if (Args->ppPostComposition == NULL)
    return FALSE;
  for (index = 0u; index < Args->PostCompositionCount; ++index) {
    if (Args->ppPostComposition[index] == NULL)
      return FALSE;
    post = &Args->ppPostComposition[index]->PostComposition;
    if (post->Flags.Value != 0u ||
        (post->Rotation != D3DDDI_ROTATION_IDENTITY && post->Rotation != 0) ||
        post->SrcRect.right - post->SrcRect.left !=
            post->DstRect.right - post->DstRect.left ||
        post->SrcRect.bottom - post->SrcRect.top !=
            post->DstRect.bottom - post->DstRect.top)
      return FALSE;
  }
  return TRUE;
}

static VOID AdmissionMpoCheckAnswer(ADMISSION_CONTEXT *Context,
                                    BOOLEAN Effective,
                                    DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 *Args,
                                    ULONG Reason, ULONG FailingPlane) {
  LONG calls;
  BOOLEAN changed;
  Args->Supported = Reason == AdmissionMpoSupported ? TRUE : FALSE;
  Args->ReturnInfo.Value = 0u;
  /* FailingPlane is a 4-bit index of the first unsupported plane. */
  if (Reason != AdmissionMpoSupported && FailingPlane < 16u)
    Args->ReturnInfo.FailingPlane = FailingPlane;
  if (!Effective)
    return;
  calls = InterlockedIncrement(&Context->Mpo.Check3Calls);
  if (Reason == AdmissionMpoSupported) {
    InterlockedIncrement(&Context->Mpo.Check3Supported);
  } else {
    InterlockedIncrement(&Context->Mpo.Check3Unsupported);
    if (Reason < ADMISSION_MPO_RECEIPT_REASONS)
      InterlockedIncrement(&Context->Mpo.Check3Reasons[Reason]);
  }
  changed = InterlockedExchange(&Context->Mpo.LastCheck3Reason,
                                (LONG)Reason) != (LONG)Reason;
  InterlockedExchange(&Context->Mpo.LastCheck3FailingPlane,
                      (LONG)FailingPlane);
  InterlockedExchange(&Context->Mpo.LastCheck3Planes,
                      (LONG)Args->PlaneCount);
  /* Check3 is PASSIVE_LEVEL (Microsoft Learn); persist on an answer change
   * and on power-of-two call counts, not on every call. */
  if (changed || (calls & (calls - 1)) == 0)
    AdmissionRecordMpo(Context);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCheckMultiPlaneOverlaySupport3(
    CONST HANDLE hAdapter, DXGKARG_CHECKMULTIPLANEOVERLAYSUPPORT3 *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)hAdapter;
  ADMISSION_MPO_PLANE_INPUT planes[ADMISSION_MPO_MAX_PLANES];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  const DXGK_MULTIPLANE_OVERLAY_PLANE_WITH_SOURCE2 *plane;
  BOOLEAN effective;
  UINT index;

  if (context == NULL || Args == NULL)
    return STATUS_INVALID_PARAMETER;
  /* S2-1b: every Check3 call, ungated. */
  {
    const LONG all = InterlockedIncrement(&context->MpoGate.Check3CallsAll);
    if (all == 1)
      context->MpoGate.Check3FirstQpc = AdmissionMpoGateQpc(context);
    if (all <= 8)
      AdmissionRecordMpoGate(context);
  }
  effective = AdmissionScanoutMpoEffective(context);
  if (Args->PlaneCount == 0u ||
      Args->PlaneCount > (effective ? ADMISSION_MPO_MAX_PLANES : 1u)) {
    AdmissionMpoCheckAnswer(context, effective, Args,
                            AdmissionMpoReasonPlaneCount,
                            ADMISSION_MPO_NO_PLANE);
    return STATUS_SUCCESS;
  }
  if (Args->ppPlanes == NULL) {
    AdmissionMpoCheckAnswer(context, effective, Args,
                            AdmissionMpoReasonArgument,
                            ADMISSION_MPO_NO_PLANE);
    return STATUS_SUCCESS;
  }
  for (index = 0u; index < Args->PlaneCount; ++index) {
    plane = Args->ppPlanes[index];
    /* One source (VidPnSourceId 0); dxgkrnl merges every source's planes
     * into one call (R22 Q3), so another source is a failing plane. */
    if (plane == NULL || plane->VidPnSourceId != 0u) {
      AdmissionMpoCheckAnswer(context, effective, Args,
                              AdmissionMpoReasonArgument, index);
      return STATUS_SUCCESS;
    }
    AdmissionMpoPlaneInput(context, plane->LayerIndex, TRUE,
                           plane->hAllocation, &plane->PlaneAttributes,
                           FALSE, 0u, 0LL, &planes[index]);
  }
  if (Args->PlaneCount == 2u && !AdmissionMpoPostCompositionIdentity(Args)) {
    AdmissionMpoCheckAnswer(context, effective, Args,
                            AdmissionMpoReasonArgument,
                            ADMISSION_MPO_NO_PLANE);
    return STATUS_SUCCESS;
  }
  AdmissionMpoInput(context, planes, Args->PlaneCount, &input);
  /* A1.1: while effective, one plane is answered with the V2 path's own
   * acceptance rule, so Check3 never promises a flip Set3 refuses.  Not
   * effective: the version-1 answer, unchanged. */
  if (effective && Args->PlaneCount == 1u)
    (void)AdmissionMpoEvaluatePrimary(&input, &result);
  else
    (void)AdmissionMpoEvaluate(&input, &result);
  AdmissionMpoCheckAnswer(context, effective, Args, result.Reason,
                          result.FailingPlane);
  return STATUS_SUCCESS;
}

/* ---- DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay3, effective ---- */

static VOID AdmissionMpoCompletionAdd(ADMISSION_MPO_COMPLETION *Completion,
                                      ULONG LayerIndex, ULONGLONG PresentId) {
  if (Completion->Count >= 2u)
    return;
  Completion->LayerIndex[Completion->Count] = LayerIndex;
  Completion->PresentId[Completion->Count] = PresentId;
  ++Completion->Count;
}

static VOID AdmissionMpoRefuseSet3(ADMISSION_CONTEXT *Context) {
  InterlockedIncrement(&Context->Mpo.Set3Refusals);
}

/* Planes a Set3 does not list keep their state (ADMISSION_MPO_PLANE_STATE);
 * the call's planes update a copy, which becomes current only when the
 * resulting flip was accepted.
 *   both planes enabled          -> one COMMIT_LAYERS with two records;
 *   LayerIndex 0 only, slot 1 may
 *   be on screen                 -> COMMIT_LAYERS with one record (removal);
 *   LayerIndex 0 only, slot 1 off-> today's V2 PRESENT path, unchanged;
 *   no plane enabled             -> today's plane-off completion (or the
 *                                   removal while slot 1 may be shown);
 *   LayerIndex 1 alone           -> refused (no Check3 answer admits it). */
_Use_decl_annotations_ NTSTATUS AdmissionMpoSetVidPnSourceAddress3(
    ADMISSION_CONTEXT *Context,
    DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 *Args) {
  ADMISSION_MPO_PLANE_STATE next[2];
  ADMISSION_MPO_PLANE_INPUT pair[2];
  ADMISSION_MPO_COMPLETION completion;
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2];
  DXGKARG_SETVIDPNSOURCEADDRESS legacy;
  DXGK_MULTIPLANE_OVERLAY_PLANE3 *plane;
  const DXGK_PRIMARYCONTEXTDATA *primary;
  BOOLEAN listed[2] = {FALSE, FALSE};
  BOOLEAN slot1;
  NTSTATUS status;
  UINT index;
  UINT layer;

  if (Args == NULL || Args->VidPnSourceId != 0 ||
      Args->PlaneCount > ADMISSION_MPO_MAX_PLANES ||
      (Args->PlaneCount != 0u && Args->ppPlanes == NULL)) {
    AdmissionMpoRefuseSet3(Context);
    return STATUS_INVALID_PARAMETER;
  }
  InterlockedIncrement(&Context->Mpo.Set3Calls);
  Args->OutputFlags.Value = 0u;
  RtlCopyMemory(next, Context->MpoPlanes, sizeof(next));
  RtlZeroMemory(&completion, sizeof(completion));
  for (index = 0u; index < Args->PlaneCount; ++index) {
    plane = Args->ppPlanes[index];
    if (plane == NULL || plane->LayerIndex > 1u || listed[plane->LayerIndex]) {
      AdmissionMpoRefuseSet3(Context);
      return STATUS_INVALID_PARAMETER;
    }
    layer = plane->LayerIndex;
    listed[layer] = TRUE;
    plane->OutputFlags.Value = 0u;
    AdmissionMpoCompletionAdd(&completion, layer, plane->PresentId);
    if (!plane->InputFlags.Enabled) {
      /* The plane's last surface stays recorded for a removal record. */
      next[layer].Enabled = 0u;
      continue;
    }
    if (plane->ContextCount == 0u || plane->ppContextData == NULL ||
        plane->ppContextData[0] == NULL) {
      AdmissionMpoRefuseSet3(Context);
      return STATUS_INVALID_PARAMETER;
    }
    primary = plane->ppContextData[0];
    next[layer].Valid = 1u;
    next[layer].Enabled = 1u;
    next[layer].PrimarySegment = primary->SegmentId;
    next[layer].PrimaryAddress = primary->SegmentAddress.QuadPart;
    next[layer].hAllocation = primary->hAllocation;
    next[layer].FlipImmediate = plane->InputFlags.FlipImmediate;
    next[layer].FlipOnNextVSync = plane->InputFlags.FlipOnNextVSync;
    AdmissionMpoPlaneInput(Context, layer, TRUE, primary->hAllocation,
                           &plane->PlaneAttributes, TRUE, primary->SegmentId,
                           primary->SegmentAddress.QuadPart,
                           &next[layer].Input);
  }
  if (Args->PlaneCount == 0u) {
    /* dxgkrnl ForcePlanesOff (EXP955): every plane off. */
    next[0].Enabled = 0u;
    next[1].Enabled = 0u;
    AdmissionMpoCompletionAdd(&completion, 0u, 0ULL);
  }

  if (next[0].Enabled && next[1].Enabled) {
    pair[0] = next[0].Input;
    pair[1] = next[1].Input;
    AdmissionMpoInput(Context, pair, 2u, &input);
    if (AdmissionMpoEvaluate(&input, &result) != AdmissionMpoSupported ||
        AdmissionMpoBuildCommit(&result, records) != 2u) {
      /* Check3 cannot see placement; Set3 can.  Refuse before the broker
       * rather than send records the table rejects. */
      InterlockedIncrement(&Context->Mpo.Set3TableRefusals);
      InterlockedExchange(&Context->Mpo.LastSet3TableReason,
                          (LONG)result.Reason);
      return STATUS_INVALID_PARAMETER;
    }
    status = AdmissionScanoutMpoCommit(Context, records, 2u, &completion,
                                       next[0].PrimaryAddress);
    if (NT_SUCCESS(status)) {
      RtlCopyMemory(Context->MpoPlanes, next, sizeof(next));
      InterlockedIncrement(&Context->Mpo.TwoPlaneCommits);
    }
    return status;
  }
  if (!next[0].Enabled && next[1].Enabled) {
    AdmissionMpoRefuseSet3(Context);
    return STATUS_INVALID_PARAMETER;
  }
  slot1 = AdmissionScanoutMpoSlot1MayShow(Context);
  if (slot1) {
    /* Two planes -> one (or none): the removal swap keeps the LayerIndex 0
     * surface in slot 0 and takes slot 1 off at the same latch. */
    if (!next[0].Valid ||
        AdmissionMpoBuildRemoval(&next[0].Input, &records[0]) != 1u) {
      AdmissionMpoRefuseSet3(Context);
      return STATUS_INVALID_PARAMETER;
    }
    /* WP-1: after the removal LayerIndex 0 is the full-screen scan-out, so
     * its allocation must have the committed mode's geometry (the rule the
     * one-plane path applies through AdmissionMpoEvaluatePrimary).  User
     * mode no longer refuses non-mode-size primaries at CreateResource. */
    pair[0] = next[0].Input;
    AdmissionMpoInput(Context, pair, 1u, &input);
    if (AdmissionMpoEvaluatePrimary(&input, &result) != AdmissionMpoSupported) {
      InterlockedIncrement(&Context->Mpo.Set3TableRefusals);
      InterlockedExchange(&Context->Mpo.LastSet3TableReason,
                          (LONG)result.Reason);
      return STATUS_INVALID_PARAMETER;
    }
    status = AdmissionScanoutMpoCommit(Context, records, 1u, &completion,
                                       next[0].PrimaryAddress);
    if (NT_SUCCESS(status)) {
      RtlCopyMemory(Context->MpoPlanes, next, sizeof(next));
      InterlockedIncrement(&Context->Mpo.RemovalCommits);
    }
    return status;
  }
  if (next[0].Enabled && listed[0]) {
    /* One plane, slot 1 not on screen: today's V2 PRESENT, same bytes,
     * after the same rule one-plane Check3 answered with (A1.1). */
    pair[0] = next[0].Input;
    AdmissionMpoInput(Context, pair, 1u, &input);
    if (AdmissionMpoEvaluatePrimary(&input, &result) != AdmissionMpoSupported) {
      InterlockedIncrement(&Context->Mpo.Set3TableRefusals);
      InterlockedExchange(&Context->Mpo.LastSet3TableReason,
                          (LONG)result.Reason);
      return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&legacy, sizeof(legacy));
    legacy.VidPnSourceId = 0;
    legacy.PrimarySegment = next[0].PrimarySegment;
    legacy.PrimaryAddress.QuadPart = next[0].PrimaryAddress;
    legacy.hAllocation = next[0].hAllocation;
    legacy.Flags.FlipImmediate = next[0].FlipImmediate & 1u;
    legacy.Flags.FlipOnNextVSync = next[0].FlipOnNextVSync & 1u;
    AdmissionScanoutTagMpoFlipSet(Context, &completion);
    status = AdmissionDdiSetVidPnSourceAddress(Context, &legacy);
    if (!NT_SUCCESS(status)) {
      AdmissionScanoutClearMpoFlip(Context);
      return status;
    }
    RtlCopyMemory(Context->MpoPlanes, next, sizeof(next));
    InterlockedIncrement(&Context->Mpo.V2Presents);
    return status;
  }
  /* Nothing to swap (planes switched off, or only LayerIndex 1 disabled
   * while it was never on screen): the scanning surface stays and the
   * listed PresentIds complete on the next vsync, as today's plane-off. */
  status = AdmissionScanoutMpoPlaneOffSet(Context, &completion);
  if (NT_SUCCESS(status)) {
    RtlCopyMemory(Context->MpoPlanes, next, sizeof(next));
    InterlockedIncrement(&Context->Mpo.PlaneOffCompletions);
  }
  return status;
}
