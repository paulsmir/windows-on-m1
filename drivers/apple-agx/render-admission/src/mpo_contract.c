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
  if (MpoWidth(&plane->Source) != MpoWidth(&plane->Destination) ||
      MpoHeight(&plane->Source) != MpoHeight(&plane->Destination))
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
  for (slot = 0u; slot < ADMISSION_MPO_SLOT_COUNT; ++slot) {
    out->Slots[slot].PlaneIndex = ADMISSION_MPO_NO_PLANE;
    out->Slots[slot].Enabled = 0u;
    out->Slots[slot].Premultiplied = 0u;
    out->Slots[slot].Source.Left = 0;
    out->Slots[slot].Source.Top = 0;
    out->Slots[slot].Source.Right = 0;
    out->Slots[slot].Source.Bottom = 0;
    out->Slots[slot].Destination = out->Slots[slot].Source;
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
    for (index = 0u; index < 2u; ++index) {
      reason = MpoCheckPairPlane(Input, index);
      if (reason != AdmissionMpoSupported)
        return MpoFail(out, reason, index);
    }
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
