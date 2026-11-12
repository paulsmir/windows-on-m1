#include "mpo_contract.h"

/* S2: see mpo_contract.h.  Pure table; no Windows dependency.  All rect
 * arithmetic is done in long long so no input value can overflow. */

#define MPO_NULL ((void *)0)

static long long MpoWidth(const ADMISSION_MPO_RECT *Rect) {
  return (long long)Rect->Right - (long long)Rect->Left;
}

static long long MpoHeight(const ADMISSION_MPO_RECT *Rect) {
  return (long long)Rect->Bottom - (long long)Rect->Top;
}

/* Well-formed (not inverted) and inside [0,Width] x [0,Height]. */
static unsigned int MpoRectInside(const ADMISSION_MPO_RECT *Rect,
                                  unsigned int Width, unsigned int Height) {
  return Rect->Left >= 0 && Rect->Top >= 0 &&
         Rect->Left <= Rect->Right && Rect->Top <= Rect->Bottom &&
         (long long)Rect->Right <= (long long)Width &&
         (long long)Rect->Bottom <= (long long)Height ? 1u : 0u;
}

static unsigned int MpoRectIsPanel(const ADMISSION_MPO_RECT *Rect,
                                   unsigned int Width, unsigned int Height) {
  return Rect->Left == 0 && Rect->Top == 0 &&
         (long long)Rect->Right == (long long)Width &&
         (long long)Rect->Bottom == (long long)Height ? 1u : 0u;
}

/* Equality is a special case of containment. */
static unsigned int MpoRectContains(const ADMISSION_MPO_RECT *Outer,
                                    const ADMISSION_MPO_RECT *Inner) {
  return Outer->Left <= Inner->Left && Outer->Top <= Inner->Top &&
         Outer->Right >= Inner->Right && Outer->Bottom >= Inner->Bottom
             ? 1u : 0u;
}

/* Version 4: within ADMISSION_MPO_MAX_SCALE either way on one axis (the
 * broker's scale_ok, hv_agx_scanout_broker.c:107-110).  Both extents are
 * non-negative here (rules 11 and 13 passed). */
static unsigned int MpoScaleOk(long long Source, long long Destination) {
  return Destination <= (long long)ADMISSION_MPO_MAX_SCALE * Source &&
                 Source <= (long long)ADMISSION_MPO_MAX_SCALE * Destination
             ? 1u
             : 0u;
}

/* The rules every plane sent as a layer record meets (two-plane planes and,
 * version 4, the one-plane record path): 10..17, 32..34 (scaled only),
 * 18..21, 25. */
static unsigned int MpoCheckLayer(const ADMISSION_MPO_INPUT *Input,
                                  const ADMISSION_MPO_PLANE_INPUT *plane) {
  const unsigned int scaled =
      MpoWidth(&plane->Source) != MpoWidth(&plane->Destination) ||
              MpoHeight(&plane->Source) != MpoHeight(&plane->Destination)
          ? 1u
          : 0u;

  if (scaled != 0u && Input->ScalingSupported == 0u)
    return AdmissionMpoReasonScaling;
  if (!MpoRectInside(&plane->Destination, Input->PanelWidth,
                     Input->PanelHeight))
    return AdmissionMpoReasonDestinationOutsidePanel;
  if (MpoWidth(&plane->Destination) < (long long)ADMISSION_MPO_MIN_EXTENT ||
      MpoHeight(&plane->Destination) < (long long)ADMISSION_MPO_MIN_EXTENT)
    return AdmissionMpoReasonDestinationTooSmall;
  if (!MpoRectInside(&plane->Source, plane->SurfaceWidth,
                     plane->SurfaceHeight))
    return AdmissionMpoReasonSourceOutsideSurface;
  if (scaled != 0u) {
    if (!MpoScaleOk(MpoWidth(&plane->Source), MpoWidth(&plane->Destination)) ||
        !MpoScaleOk(MpoHeight(&plane->Source),
                    MpoHeight(&plane->Destination)))
      return AdmissionMpoReasonScaleBeyondLimit;
    /* Check3 only (ADMISSION_MPO_CHECK3_ONLY_REASONS): the quality makes
     * the Check3 answer truthful; DCP has one scaler filter and nothing on
     * the Set3 path acts on the field, which dxgkrnl need not repeat there
     * (EXP2374). */
    if (Input->Path == ADMISSION_MPO_PATH_CHECK3) {
      if (plane->StretchQuality != ADMISSION_MPO_STRETCH_QUALITY_BILINEAR &&
          plane->StretchQuality != ADMISSION_MPO_STRETCH_QUALITY_HIGH)
        return AdmissionMpoReasonStretchQuality;
      /* HIGH asks for "the highest quality filtering that it supports"
       * (Microsoft Learn, DXGK_MULTIPLANE_OVERLAY_STRETCH_QUALITY), which
       * any scaler honours; BILINEAR needs a filter known to be at least
       * that. */
      if (plane->StretchQuality == ADMISSION_MPO_STRETCH_QUALITY_BILINEAR &&
          Input->ScalerFilter != AdmissionMpoScalerFilterBilinearOrBetter)
        return AdmissionMpoReasonStretchFilter;
    }
  }
  /* A zero stride cannot address a surface; treated as a stride defect. */
  if (plane->SurfaceStride == 0u ||
      plane->SurfaceStride % ADMISSION_MPO_STRIDE_ALIGNMENT != 0u)
    return AdmissionMpoReasonStride;
  if (plane->Rotation != AdmissionMpoRotationIdentity)
    return AdmissionMpoReasonRotation;
  if (plane->HasUnsupportedFlags != 0u)
    return AdmissionMpoReasonUnsupportedFlags;
  if (!MpoRectContains(&plane->Clip, &plane->Destination))
    return AdmissionMpoReasonClip;
  /* Version 2: the scanout broker's surface rules. */
  if (plane->InScanoutPool == 0u ||
      (plane->PlacementKnown != 0u &&
       (plane->PoolOffset > Input->PoolBytes ||
        plane->SurfaceBytes > Input->PoolBytes - plane->PoolOffset)))
    return AdmissionMpoReasonSurfaceNotInPool;
  if (plane->PlacementKnown != 0u &&
      (plane->PoolOffset & (ADMISSION_MPO_POOL_ALIGNMENT - 1ULL)) != 0ULL)
    return AdmissionMpoReasonSurfaceAlignment;
  if (plane->SurfaceBytes != (unsigned long long)plane->SurfaceStride *
                                 (unsigned long long)plane->SurfaceHeight)
    return AdmissionMpoReasonSurfaceSize;
  if (plane->SurfaceWidth == 0u || plane->SurfaceHeight == 0u ||
      plane->SurfaceWidth > ADMISSION_MPO_SCANOUT_WIDTH ||
      plane->SurfaceHeight > ADMISSION_MPO_SCANOUT_HEIGHT)
    return AdmissionMpoReasonSurfaceLargerThanPanel;
  if ((unsigned long long)plane->SurfaceStride <
      (unsigned long long)plane->SurfaceWidth * ADMISSION_MPO_BYTES_PER_PIXEL)
    return AdmissionMpoReasonStrideBelowRow;
  return AdmissionMpoSupported;
}

/* First violated two-plane rule of Planes[Index], or Supported. */
static unsigned int MpoCheckPairPlane(const ADMISSION_MPO_INPUT *Input,
                                      unsigned int Index) {
  const ADMISSION_MPO_PLANE_INPUT *plane = &Input->Planes[Index];
  const ADMISSION_MPO_PLANE_INPUT *other = &Input->Planes[Index ^ 1u];

  if (plane->LayerIndex > 1u || plane->LayerIndex == other->LayerIndex)
    return AdmissionMpoReasonLayerIndex;
  if (plane->Enabled == 0u)
    return AdmissionMpoReasonDisabled;
  if (plane->FormatClass != AdmissionMpoFormatBgra8 &&
      plane->FormatClass != AdmissionMpoFormatRgba8)
    return AdmissionMpoReasonFormat;
  if (plane->ColorSpaceClass != AdmissionMpoColorSpaceSdrSrgb)
    return AdmissionMpoReasonColorSpace;
  if (plane->LayerIndex == 0u) {
    if (plane->BlendAlpha != 1u)
      return AdmissionMpoReasonTopBlend;
    if (!MpoRectIsPanel(&plane->Source, Input->PanelWidth,
                        Input->PanelHeight) ||
        !MpoRectIsPanel(&plane->Destination, Input->PanelWidth,
                        Input->PanelHeight))
      return AdmissionMpoReasonTopNotFullPanel;
  } else if (plane->BlendAlpha != 0u) {
    return AdmissionMpoReasonBottomBlend;
  }
  return MpoCheckLayer(Input, plane);
}

/* Pair rules on two planes that passed every per-plane rule (so a known
 * range lies inside the pool and its end cannot overflow). */
static unsigned int MpoCheckPair(const ADMISSION_MPO_PLANE_INPUT *A,
                                 const ADMISSION_MPO_PLANE_INPUT *B) {
  unsigned int known = A->PlacementKnown != 0u && B->PlacementKnown != 0u
                           ? 1u : 0u;
  if (A->SurfaceIdentity == B->SurfaceIdentity ||
      (known != 0u && A->PoolOffset == B->PoolOffset))
    return AdmissionMpoReasonSameSurface;
  if (known != 0u && A->PoolOffset < B->PoolOffset + B->SurfaceBytes &&
      B->PoolOffset < A->PoolOffset + A->SurfaceBytes)
    return AdmissionMpoReasonSurfaceOverlap;
  return AdmissionMpoSupported;
}

static void MpoFillSlot(ADMISSION_MPO_SLOT *Slot,
                        const ADMISSION_MPO_PLANE_INPUT *Plane,
                        unsigned int Index, unsigned int Premultiplied) {
  Slot->PlaneIndex = Index;
  Slot->Enabled = Plane->Enabled != 0u ? 1u : 0u;
  Slot->Premultiplied = Premultiplied;
  Slot->Source = Plane->Source;
  Slot->Destination = Plane->Destination;
  Slot->FormatClass = Plane->FormatClass;
  Slot->SurfaceWidth = Plane->SurfaceWidth;
  Slot->SurfaceHeight = Plane->SurfaceHeight;
  Slot->SurfaceStride = Plane->SurfaceStride;
  Slot->PoolOffset = Plane->PoolOffset;
  Slot->SurfaceBytes = Plane->SurfaceBytes;
}

static unsigned int MpoFail(ADMISSION_MPO_RESULT *Result, unsigned int Reason,
                            unsigned int Plane) {
  Result->Reason = Reason;
  Result->FailingPlane = Plane;
  return Reason;
}

unsigned int AdmissionMpoEvaluate(const ADMISSION_MPO_INPUT *Input,
                                  ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_RESULT scratch;
  ADMISSION_MPO_RESULT *out = Result != MPO_NULL ? Result : &scratch;
  unsigned int index;
  unsigned int reason;
  unsigned int slot;

  out->Supported = 0u;
  out->Reason = AdmissionMpoReasonArgument;
  out->FailingPlane = ADMISSION_MPO_NO_PLANE;
  out->SlotCount = 0u;
  out->OnePlaneRecord = 0u;
  for (slot = 0u; slot < ADMISSION_MPO_SLOT_COUNT; ++slot) {
    out->Slots[slot].PlaneIndex = ADMISSION_MPO_NO_PLANE;
    out->Slots[slot].Enabled = 0u;
    out->Slots[slot].Premultiplied = 0u;
    out->Slots[slot].Source.Left = 0;
    out->Slots[slot].Source.Top = 0;
    out->Slots[slot].Source.Right = 0;
    out->Slots[slot].Source.Bottom = 0;
    out->Slots[slot].Destination = out->Slots[slot].Source;
    out->Slots[slot].FormatClass = AdmissionMpoFormatOther;
    out->Slots[slot].SurfaceWidth = 0u;
    out->Slots[slot].SurfaceHeight = 0u;
    out->Slots[slot].SurfaceStride = 0u;
    out->Slots[slot].PoolOffset = 0ULL;
    out->Slots[slot].SurfaceBytes = 0ULL;
  }
  if (Input == MPO_NULL || Result == MPO_NULL)
    return MpoFail(out, AdmissionMpoReasonArgument, ADMISSION_MPO_NO_PLANE);
  if (Input->PlaneCount == 0u || Input->PlaneCount > ADMISSION_MPO_MAX_PLANES)
    return MpoFail(out, AdmissionMpoReasonPlaneCount, ADMISSION_MPO_NO_PLANE);
  if (Input->Planes == MPO_NULL)
    return MpoFail(out, AdmissionMpoReasonArgument, ADMISSION_MPO_NO_PLANE);

  if (Input->PlaneCount == 1u) {
    /* display.c one-plane handler: LayerIndex 0 only; enabled and
     * disabled (plane off) are both accepted. */
    if (Input->Planes[0].LayerIndex != 0u)
      return MpoFail(out, AdmissionMpoReasonLayerIndex, 0u);
    /* OPEN design item: slot 0 has nothing beneath it, so the
     * premultiplied flag is irrelevant there; it is reported as 1 for
     * uniformity with the two-plane slot 0.  Do not rely on it. */
    MpoFillSlot(&out->Slots[0], &Input->Planes[0], 0u, 1u);
    out->SlotCount = 1u;
  } else {
    if (Input->PanelWidth != ADMISSION_MPO_SCANOUT_WIDTH ||
        Input->PanelHeight != ADMISSION_MPO_SCANOUT_HEIGHT)
      return MpoFail(out, AdmissionMpoReasonPanelGeometry,
                     ADMISSION_MPO_NO_PLANE);
    for (index = 0u; index < 2u; ++index) {
      reason = MpoCheckPairPlane(Input, index);
      if (reason != AdmissionMpoSupported)
        return MpoFail(out, reason, index);
    }
    reason = MpoCheckPair(&Input->Planes[0], &Input->Planes[1]);
    if (reason != AdmissionMpoSupported)
      return MpoFail(out, reason, 1u);
    for (index = 0u; index < 2u; ++index) {
      /* LayerIndex 1 (bottom) -> slot 0; LayerIndex 0 (top) -> slot 1.
       * OPEN design item: slot 0 premultiplied = 1 is reported but
       * irrelevant (nothing beneath it); do not rely on it. */
      slot = Input->Planes[index].LayerIndex == 1u ? 0u : 1u;
      MpoFillSlot(&out->Slots[slot], &Input->Planes[index], index, 1u);
    }
    out->SlotCount = 2u;
  }
  out->Supported = 1u;
  out->Reason = AdmissionMpoSupported;
  out->FailingPlane = ADMISSION_MPO_NO_PLANE;
  return AdmissionMpoSupported;
}

/* Version 3: see mpo_contract.h AdmissionMpoEvaluatePrimary. */
static unsigned int MpoCheckPrimary(const ADMISSION_MPO_INPUT *Input,
                                    const ADMISSION_MPO_PLANE_INPUT *Plane) {
  if (Plane->ScanoutCapable == 0u)
    return AdmissionMpoReasonPrimaryNotScanoutCapable;
  if (Input->PanelModeSupported == 0u)
    return AdmissionMpoReasonPrimarySourceMode;
  if (Plane->DescriptionValid == 0u ||
      Plane->SurfaceWidth != Input->PanelWidth ||
      Plane->SurfaceHeight != Input->PanelHeight ||
      Plane->SurfaceStride != Input->PanelStride)
    return AdmissionMpoReasonPrimaryGeometry;
  if (Plane->AllocationBytes != (unsigned long long)Input->PanelStride *
                                    (unsigned long long)Input->PanelHeight)
    return AdmissionMpoReasonPrimarySize;
  if (Plane->FormatClass != AdmissionMpoFormatBgra8 &&
      Plane->FormatClass != AdmissionMpoFormatRgba8)
    return AdmissionMpoReasonPrimaryFormat;
  if (Plane->PlacementKnown != 0u && Plane->PlacementInPool == 0u)
    return AdmissionMpoReasonPrimaryPlacement;
  return AdmissionMpoSupported;
}

unsigned int AdmissionMpoEvaluatePrimary(const ADMISSION_MPO_INPUT *Input,
                                         ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT one;
  unsigned int reason;

  if (Input == MPO_NULL || Result == MPO_NULL)
    return AdmissionMpoEvaluate(Input, Result);
  one = *Input;
  /* Exactly one plane; any other count is PlaneCount (the v1 answer for a
   * count of 0, forced for 2). */
  if (one.PlaneCount != 1u)
    one.PlaneCount = 0u;
  /* The v1 one-plane answer (LayerIndex 0, slot fill) comes first. */
  reason = AdmissionMpoEvaluate(&one, Result);
  if (one.PlaneCount != 1u)
    return reason;
  if (reason != AdmissionMpoSupported || Input->Planes[0].Enabled == 0u)
    return reason;
  reason = MpoCheckPrimary(Input, &Input->Planes[0]);
  if (reason == AdmissionMpoSupported)
    return reason;
  Result->Supported = 0u;
  Result->SlotCount = 0u;
  Result->Slots[0].PlaneIndex = ADMISSION_MPO_NO_PLANE;
  return MpoFail(Result, reason, 0u);
}

/* Version 4: see mpo_contract.h AdmissionMpoEvaluateOne. */
unsigned int AdmissionMpoEvaluateOne(const ADMISSION_MPO_INPUT *Input,
                                     ADMISSION_MPO_RESULT *Result) {
  const ADMISSION_MPO_PLANE_INPUT *plane;
  unsigned int reason;

  if (Input == MPO_NULL || Result == MPO_NULL || Input->PlaneCount != 1u ||
      Input->Planes == MPO_NULL)
    return AdmissionMpoEvaluatePrimary(Input, Result);
  plane = &Input->Planes[0];
  /* FS-1: the application plane alone (DWM's top plane off): a full-panel,
   * opaque LayerIndex 1 plane, scaled within the layer limits, sent as one
   * layer record.  The full-screen independent flip of a game. */
  if (plane->Enabled != 0u && plane->LayerIndex == 1u) {
    (void)AdmissionMpoEvaluate(Input, Result); /* argument and slot reset */
    Result->OnePlaneRecord = 0u;
    if (plane->FormatClass != AdmissionMpoFormatBgra8 &&
        plane->FormatClass != AdmissionMpoFormatRgba8)
      reason = AdmissionMpoReasonFormat;
    else if (plane->ColorSpaceClass != AdmissionMpoColorSpaceSdrSrgb)
      reason = AdmissionMpoReasonColorSpace;
    else if (plane->BlendAlpha != 0u)
      reason = AdmissionMpoReasonLoneBlend;
    else if (!MpoRectIsPanel(&plane->Destination, Input->PanelWidth,
                             Input->PanelHeight))
      reason = AdmissionMpoReasonLoneNotFullPanel;
    else
      reason = MpoCheckLayer(Input, plane);
    if (reason != AdmissionMpoSupported) {
      Result->Supported = 0u;
      Result->SlotCount = 0u;
      Result->Slots[0].PlaneIndex = ADMISSION_MPO_NO_PLANE;
      return MpoFail(Result, reason, 0u);
    }
    Result->Supported = 1u;
    Result->Reason = AdmissionMpoSupported;
    Result->FailingPlane = ADMISSION_MPO_NO_PLANE;
    MpoFillSlot(&Result->Slots[0], plane, 0u, 0u);
    Result->SlotCount = 1u;
    Result->OnePlaneRecord = 1u;
    return AdmissionMpoSupported;
  }
  if (plane->Enabled == 0u || plane->LayerIndex != 0u ||
      (MpoRectIsPanel(&plane->Source, Input->PanelWidth, Input->PanelHeight) &&
       MpoRectIsPanel(&plane->Destination, Input->PanelWidth,
                      Input->PanelHeight))) {
    reason = AdmissionMpoEvaluatePrimary(Input, Result);
    Result->OnePlaneRecord = 0u;
    return reason;
  }
  /* The v1 one-plane answer (argument and slot reset) first. */
  (void)AdmissionMpoEvaluate(Input, Result);
  Result->OnePlaneRecord = 0u;
  if (plane->FormatClass != AdmissionMpoFormatBgra8 &&
      plane->FormatClass != AdmissionMpoFormatRgba8)
    reason = AdmissionMpoReasonFormat;
  else if (plane->ColorSpaceClass != AdmissionMpoColorSpaceSdrSrgb)
    reason = AdmissionMpoReasonColorSpace;
  else if (plane->BlendAlpha > 1u)
    reason = AdmissionMpoReasonOnePlaneBlend;
  else
    reason = MpoCheckLayer(Input, plane);
  if (reason != AdmissionMpoSupported) {
    Result->Supported = 0u;
    Result->SlotCount = 0u;
    Result->Slots[0].PlaneIndex = ADMISSION_MPO_NO_PLANE;
    return MpoFail(Result, reason, 0u);
  }
  MpoFillSlot(&Result->Slots[0], plane, 0u, 0u);
  Result->SlotCount = 1u;
  Result->OnePlaneRecord = 1u;
  return AdmissionMpoSupported;
}

void AdmissionMpoCapsFor(unsigned int Effective, unsigned int Scaling,
                         unsigned int ScalerFilter, unsigned int VidPnSourceId,
                         ADMISSION_MPO_CAPS *Caps) {
  const unsigned int two =
      Effective != 0u && VidPnSourceId == 0u ? 1u : 0u;
  const unsigned int stretch = two != 0u && Scaling != 0u ? 1u : 0u;
  if (Caps == MPO_NULL)
    return;
  Caps->MaxPlanes = two != 0u ? 2u : 1u;
  Caps->StretchRgb = stretch;
  Caps->BilinearFilter =
      stretch != 0u &&
              ScalerFilter == (unsigned int)AdmissionMpoScalerFilterBilinearOrBetter
          ? 1u
          : 0u;
  Caps->HighFilter = 0u;
  Caps->MaxStretchBits =
      stretch != 0u ? ADMISSION_MPO_FLOAT_BITS_TWO : ADMISSION_MPO_FLOAT_BITS_ONE;
  Caps->MaxShrinkBits =
      stretch != 0u ? ADMISSION_MPO_FLOAT_BITS_HALF : ADMISSION_MPO_FLOAT_BITS_ONE;
}

unsigned int AdmissionMpoNoDmaFlipAccepted(unsigned int Flags,
                                           unsigned int MpoReported) {
  const unsigned int flip = 0x4u, noWait = 0x8u, mpo = 0x1000u;
  if ((Flags & ~noWait) == flip)
    return 1u;
  return MpoReported != 0u && (Flags & ~(noWait | mpo)) == flip &&
                 (Flags & mpo) != 0u
             ? 1u
             : 0u;
}
