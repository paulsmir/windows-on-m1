#include "mpo_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* S2 rule-table test.  Catches a configuration the display slots cannot
 * show being answered "supported", and the one-plane answers drifting from
 * the display.c one-plane handler. */

#define PANEL_W 2560u
#define PANEL_H 1600u

static unsigned int Failures;

#define CHECK(Condition)                                                      \
  do {                                                                        \
    if (!(Condition)) {                                                       \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,       \
              #Condition);                                                    \
      ++Failures;                                                             \
    }                                                                         \
  } while (0)

static ADMISSION_MPO_RECT Rect(int Left, int Top, int Right, int Bottom) {
  ADMISSION_MPO_RECT rect;
  rect.Left = Left;
  rect.Top = Top;
  rect.Right = Right;
  rect.Bottom = Bottom;
  return rect;
}

static int RectEqual(ADMISSION_MPO_RECT A, ADMISSION_MPO_RECT B) {
  return A.Left == B.Left && A.Top == B.Top && A.Right == B.Right &&
         A.Bottom == B.Bottom;
}

/* Valid top plane: LayerIndex 0, alpha-blended, full panel. */
static ADMISSION_MPO_PLANE_INPUT TopPlane(void) {
  ADMISSION_MPO_PLANE_INPUT plane;
  memset(&plane, 0, sizeof(plane));
  plane.LayerIndex = 0u;
  plane.Enabled = 1u;
  plane.FormatClass = AdmissionMpoFormatBgra8;
  plane.ColorSpaceClass = AdmissionMpoColorSpaceSdrSrgb;
  plane.BlendAlpha = 1u;
  plane.Source = Rect(0, 0, (int)PANEL_W, (int)PANEL_H);
  plane.Destination = plane.Source;
  plane.Clip = plane.Source;
  plane.Rotation = AdmissionMpoRotationIdentity;
  plane.SurfaceWidth = PANEL_W;
  plane.SurfaceHeight = PANEL_H;
  plane.SurfaceStride = PANEL_W * 4u;
  return plane;
}

/* Valid bottom plane: LayerIndex 1, opaque 640x480 video at (100,200). */
static ADMISSION_MPO_PLANE_INPUT BottomPlane(void) {
  ADMISSION_MPO_PLANE_INPUT plane;
  memset(&plane, 0, sizeof(plane));
  plane.LayerIndex = 1u;
  plane.Enabled = 1u;
  plane.FormatClass = AdmissionMpoFormatRgba8;
  plane.ColorSpaceClass = AdmissionMpoColorSpaceSdrSrgb;
  plane.BlendAlpha = 0u;
  plane.Source = Rect(0, 0, 640, 480);
  plane.Destination = Rect(100, 200, 740, 680);
  plane.Clip = plane.Destination;
  plane.Rotation = AdmissionMpoRotationIdentity;
  plane.SurfaceWidth = 640u;
  plane.SurfaceHeight = 480u;
  plane.SurfaceStride = 640u * 4u;
  return plane;
}

static unsigned int Eval(const ADMISSION_MPO_PLANE_INPUT *Planes,
                         unsigned int Count, ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT input;
  input.PanelWidth = PANEL_W;
  input.PanelHeight = PANEL_H;
  input.PlaneCount = Count;
  input.Planes = Planes;
  return AdmissionMpoEvaluate(&input, Result);
}

static void ExpectFail(const ADMISSION_MPO_PLANE_INPUT *Planes,
                       unsigned int Count, unsigned int Reason,
                       unsigned int Plane, int Line) {
  ADMISSION_MPO_RESULT result;
  unsigned int returned = Eval(Planes, Count, &result);
  if (returned != Reason || result.Reason != Reason ||
      result.Supported != 0u || result.FailingPlane != Plane ||
      result.SlotCount != 0u) {
    fprintf(stderr,
            "line %d: expected reason %u plane %u, got %u/%u plane %u "
            "supported %u slots %u\n",
            Line, Reason, Plane, returned, result.Reason, result.FailingPlane,
            result.Supported, result.SlotCount);
    ++Failures;
  }
}

static void ExpectPairSupported(const ADMISSION_MPO_PLANE_INPUT *Planes,
                                int Line) {
  ADMISSION_MPO_RESULT result;
  unsigned int bottom = Planes[0].LayerIndex == 1u ? 0u : 1u;
  unsigned int top = bottom ^ 1u;
  if (Eval(Planes, 2u, &result) != AdmissionMpoSupported ||
      result.Supported != 1u || result.SlotCount != 2u ||
      result.FailingPlane != ADMISSION_MPO_NO_PLANE ||
      result.Slots[0].PlaneIndex != bottom ||
      result.Slots[1].PlaneIndex != top ||
      result.Slots[0].Premultiplied != 1u ||
      result.Slots[1].Premultiplied != 1u ||
      !RectEqual(result.Slots[0].Source, Planes[bottom].Source) ||
      !RectEqual(result.Slots[0].Destination, Planes[bottom].Destination) ||
      !RectEqual(result.Slots[1].Source, Planes[top].Source) ||
      !RectEqual(result.Slots[1].Destination, Planes[top].Destination)) {
    fprintf(stderr, "line %d: expected supported pair, got reason %u plane %u\n",
            Line, result.Reason, result.FailingPlane);
    ++Failures;
  }
}

#define FAIL_AT(P, N, R, I) ExpectFail((P), (N), (R), (I), __LINE__)
#define PAIR_OK(P) ExpectPairSupported((P), __LINE__)

/* Sets planes[0] = bottom, planes[1] = top when Swap, else top, bottom. */
static void Pair(ADMISSION_MPO_PLANE_INPUT *Planes, int Swap) {
  Planes[Swap ? 1 : 0] = TopPlane();
  Planes[Swap ? 0 : 1] = BottomPlane();
}

static void TestArgumentsAndCounts(void) {
  ADMISSION_MPO_PLANE_INPUT planes[3];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;

  Pair(planes, 0);
  planes[2] = BottomPlane();
  memset(&result, 0xa5, sizeof(result));
  CHECK(AdmissionMpoEvaluate(NULL, &result) == AdmissionMpoReasonArgument);
  CHECK(result.Supported == 0u && result.SlotCount == 0u &&
        result.FailingPlane == ADMISSION_MPO_NO_PLANE &&
        result.Slots[0].PlaneIndex == ADMISSION_MPO_NO_PLANE);
  input.PanelWidth = PANEL_W;
  input.PanelHeight = PANEL_H;
  input.PlaneCount = 2u;
  input.Planes = planes;
  CHECK(AdmissionMpoEvaluate(&input, NULL) == AdmissionMpoReasonArgument);
  input.Planes = NULL;
  CHECK(AdmissionMpoEvaluate(&input, &result) == AdmissionMpoReasonArgument);
  CHECK(result.FailingPlane == ADMISSION_MPO_NO_PLANE);

  FAIL_AT(planes, 0u, AdmissionMpoReasonPlaneCount, ADMISSION_MPO_NO_PLANE);
  FAIL_AT(planes, 3u, AdmissionMpoReasonPlaneCount, ADMISSION_MPO_NO_PLANE);
  FAIL_AT(planes, 0xffffffffu, AdmissionMpoReasonPlaneCount,
          ADMISSION_MPO_NO_PLANE);
}

/* display.c AdmissionDdiSetVidPnSourceAddressWithMultiPlaneOverlay3:
 * LayerIndex != 0 is rejected; disabled is plane-off (accepted); no other
 * per-plane property is examined. */
static void TestOnePlaneMirrorsHandler(void) {
  ADMISSION_MPO_PLANE_INPUT plane;
  ADMISSION_MPO_RESULT result;

  plane = TopPlane();
  CHECK(Eval(&plane, 1u, &result) == AdmissionMpoSupported);
  CHECK(result.Supported == 1u && result.SlotCount == 1u &&
        result.Slots[0].PlaneIndex == 0u && result.Slots[0].Enabled == 1u &&
        result.Slots[0].Premultiplied == 1u &&
        RectEqual(result.Slots[0].Source, plane.Source) &&
        RectEqual(result.Slots[0].Destination, plane.Destination) &&
        result.Slots[1].PlaneIndex == ADMISSION_MPO_NO_PLANE);

  plane.Enabled = 0u;
  CHECK(Eval(&plane, 1u, &result) == AdmissionMpoSupported);
  CHECK(result.Slots[0].Enabled == 0u);

  plane = TopPlane();
  plane.LayerIndex = 1u;
  FAIL_AT(&plane, 1u, AdmissionMpoReasonLayerIndex, 0u);
  plane.LayerIndex = 0xffffffffu;
  FAIL_AT(&plane, 1u, AdmissionMpoReasonLayerIndex, 0u);
  plane.Enabled = 0u;
  FAIL_AT(&plane, 1u, AdmissionMpoReasonLayerIndex, 0u);

  /* None of the two-plane rules applies to one plane today. */
  plane = BottomPlane();
  plane.LayerIndex = 0u;
  plane.FormatClass = AdmissionMpoFormatOther;
  plane.ColorSpaceClass = AdmissionMpoColorSpaceOther;
  plane.BlendAlpha = 7u;
  plane.Source = Rect(-5, -5, 3, 3);
  plane.Destination = Rect(5000, 5000, 1, 1);
  plane.Clip = Rect(0, 0, 0, 0);
  plane.Rotation = AdmissionMpoRotationOther;
  plane.HasUnsupportedFlags = 1u;
  plane.SurfaceWidth = 0u;
  plane.SurfaceHeight = 0u;
  plane.SurfaceStride = 3u;
  CHECK(Eval(&plane, 1u, &result) == AdmissionMpoSupported);
  CHECK(result.SlotCount == 1u &&
        RectEqual(result.Slots[0].Destination, plane.Destination));
}

/* Each reason code in both array orders; the violation is applied to the
 * named role and must be reported on that role's array index. */
static void TestEveryReasonBothOrders(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  int swap;
  unsigned int top;
  unsigned int bottom;

  for (swap = 0; swap < 2; ++swap) {
    top = swap ? 1u : 0u;
    bottom = top ^ 1u;

    Pair(p, swap);
    PAIR_OK(p);

    Pair(p, swap); p[top].LayerIndex = 2u;
    FAIL_AT(p, 2u, AdmissionMpoReasonLayerIndex, top);
    Pair(p, swap); p[bottom].LayerIndex = 2u;
    FAIL_AT(p, 2u, AdmissionMpoReasonLayerIndex, bottom);
    Pair(p, swap); p[bottom].LayerIndex = 0u;
    FAIL_AT(p, 2u, AdmissionMpoReasonLayerIndex, 0u);
    Pair(p, swap); p[top].LayerIndex = 1u;
    FAIL_AT(p, 2u, AdmissionMpoReasonLayerIndex, 0u);

    Pair(p, swap); p[top].Enabled = 0u;
    FAIL_AT(p, 2u, AdmissionMpoReasonDisabled, top);
    Pair(p, swap); p[bottom].Enabled = 0u;
    FAIL_AT(p, 2u, AdmissionMpoReasonDisabled, bottom);

    Pair(p, swap); p[bottom].FormatClass = AdmissionMpoFormatOther;
    FAIL_AT(p, 2u, AdmissionMpoReasonFormat, bottom);
    Pair(p, swap); p[top].FormatClass = 99u;
    FAIL_AT(p, 2u, AdmissionMpoReasonFormat, top);

    Pair(p, swap); p[bottom].ColorSpaceClass = AdmissionMpoColorSpaceOther;
    FAIL_AT(p, 2u, AdmissionMpoReasonColorSpace, bottom);

    Pair(p, swap); p[top].BlendAlpha = 0u;
    FAIL_AT(p, 2u, AdmissionMpoReasonTopBlend, top);
    Pair(p, swap); p[top].BlendAlpha = 2u;
    FAIL_AT(p, 2u, AdmissionMpoReasonTopBlend, top);

    Pair(p, swap); p[top].Destination = Rect(0, 0, 2559, 1600);
    p[top].Source = p[top].Destination;
    FAIL_AT(p, 2u, AdmissionMpoReasonTopNotFullPanel, top);
    Pair(p, swap); p[top].Source = Rect(1, 0, 2561, 1600);
    p[top].SurfaceWidth = 4096u;
    FAIL_AT(p, 2u, AdmissionMpoReasonTopNotFullPanel, top);

    Pair(p, swap); p[bottom].BlendAlpha = 1u;
    FAIL_AT(p, 2u, AdmissionMpoReasonBottomBlend, bottom);

    Pair(p, swap); p[bottom].Destination = Rect(100, 200, 1380, 1160);
    FAIL_AT(p, 2u, AdmissionMpoReasonScaling, bottom);
    Pair(p, swap); p[bottom].Source = Rect(0, 0, 640, 479);
    FAIL_AT(p, 2u, AdmissionMpoReasonScaling, bottom);

    Pair(p, swap); p[bottom].Destination = Rect(-1, 200, 639, 680);
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationOutsidePanel, bottom);

    Pair(p, swap); p[bottom].Source = Rect(0, 0, 31, 480);
    p[bottom].Destination = Rect(100, 200, 131, 680);
    p[bottom].Clip = p[bottom].Destination;
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationTooSmall, bottom);

    Pair(p, swap); p[bottom].SurfaceHeight = 479u;
    FAIL_AT(p, 2u, AdmissionMpoReasonSourceOutsideSurface, bottom);

    Pair(p, swap); p[bottom].SurfaceStride = 2560u + 4u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStride, bottom);
    Pair(p, swap); p[top].SurfaceStride = 0u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStride, top);

    Pair(p, swap); p[bottom].Rotation = AdmissionMpoRotationOther;
    FAIL_AT(p, 2u, AdmissionMpoReasonRotation, bottom);

    Pair(p, swap); p[top].HasUnsupportedFlags = 1u;
    FAIL_AT(p, 2u, AdmissionMpoReasonUnsupportedFlags, top);

    Pair(p, swap); p[bottom].Clip = Rect(101, 200, 740, 680);
    FAIL_AT(p, 2u, AdmissionMpoReasonClip, bottom);
    Pair(p, swap); p[bottom].Clip = Rect(0, 0, 0, 0);
    FAIL_AT(p, 2u, AdmissionMpoReasonClip, bottom);
    Pair(p, swap); p[bottom].Clip = Rect(0, 0, 2560, 1600);
    PAIR_OK(p);

    /* First failing plane in array order wins, whatever its rule. */
    Pair(p, swap); p[0].Rotation = AdmissionMpoRotationOther;
    p[1].Enabled = 0u;
    FAIL_AT(p, 2u, AdmissionMpoReasonRotation, 0u);
    /* Within a plane the first rule in the listed order wins. */
    Pair(p, swap); p[bottom].HasUnsupportedFlags = 1u;
    p[bottom].FormatClass = AdmissionMpoFormatOther;
    p[bottom].Clip = Rect(0, 0, 1, 1);
    FAIL_AT(p, 2u, AdmissionMpoReasonFormat, bottom);
  }
}

static void TestBoundaries(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  int swap;
  unsigned int bottom;

  for (swap = 0; swap < 2; ++swap) {
    bottom = swap ? 0u : 1u;

    /* 32x32 accepted, 31 in either dimension rejected. */
    Pair(p, swap);
    p[bottom].Source = Rect(0, 0, 32, 32);
    p[bottom].Destination = Rect(10, 10, 42, 42);
    p[bottom].Clip = p[bottom].Destination;
    PAIR_OK(p);
    p[bottom].Source = Rect(0, 0, 32, 31);
    p[bottom].Destination = Rect(10, 10, 42, 41);
    p[bottom].Clip = p[bottom].Destination;
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationTooSmall, bottom);
    p[bottom].Source = Rect(0, 0, 31, 32);
    p[bottom].Destination = Rect(10, 10, 41, 42);
    p[bottom].Clip = p[bottom].Destination;
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationTooSmall, bottom);

    /* Destination touching each panel edge is inside. */
    Pair(p, swap);
    p[bottom].Destination = Rect(0, 0, 640, 480);
    p[bottom].Clip = p[bottom].Destination;
    PAIR_OK(p);
    p[bottom].Destination = Rect(2560 - 640, 1600 - 480, 2560, 1600);
    p[bottom].Clip = p[bottom].Destination;
    PAIR_OK(p);
    /* One pixel outside on each edge is rejected. */
    p[bottom].Destination = Rect(2561 - 640, 1120, 2561, 1600);
    p[bottom].Clip = p[bottom].Destination;
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationOutsidePanel, bottom);
    p[bottom].Destination = Rect(1920, 1121, 2560, 1601);
    p[bottom].Clip = p[bottom].Destination;
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationOutsidePanel, bottom);
    p[bottom].Destination = Rect(0, -1, 640, 479);
    p[bottom].Clip = p[bottom].Destination;
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationOutsidePanel, bottom);

    /* Source touching the surface edge is inside; one pixel past is not. */
    Pair(p, swap);
    p[bottom].SurfaceWidth = 1000u;
    p[bottom].SurfaceHeight = 800u;
    p[bottom].Source = Rect(360, 320, 1000, 800);
    PAIR_OK(p);
    p[bottom].Source = Rect(361, 320, 1001, 800);
    FAIL_AT(p, 2u, AdmissionMpoReasonSourceOutsideSurface, bottom);
    p[bottom].Source = Rect(-1, 0, 639, 480);
    FAIL_AT(p, 2u, AdmissionMpoReasonSourceOutsideSurface, bottom);

    /* Stride exactly 64 accepted; 64 +/- 1 rejected. */
    Pair(p, swap);
    p[bottom].SurfaceStride = 64u;
    PAIR_OK(p);
    p[bottom].SurfaceStride = 63u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStride, bottom);
    p[bottom].SurfaceStride = 65u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStride, bottom);

    /* Inverted destination with matching inverted source: not scaling,
     * but not inside the panel either. */
    Pair(p, swap);
    p[bottom].Source = Rect(640, 480, 0, 0);
    p[bottom].Destination = Rect(740, 680, 100, 200);
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationOutsidePanel, bottom);

    /* Extreme coordinates must not overflow into a false "same size". */
    Pair(p, swap);
    p[bottom].Source = Rect(-2147483647 - 1, 0, 2147483647, 480);
    p[bottom].Destination = Rect(-2147483647 - 1, 200, 2147483647, 680);
    FAIL_AT(p, 2u, AdmissionMpoReasonDestinationOutsidePanel, bottom);
  }
}

/* Independent oracle: one predicate per listed rule, in listed order. */
static int RuleHolds(const ADMISSION_MPO_PLANE_INPUT *P, unsigned int Index,
                     unsigned int Reason) {
  const ADMISSION_MPO_PLANE_INPUT *x = &P[Index];
  const ADMISSION_MPO_PLANE_INPUT *o = &P[Index ^ 1u];
  long long sw = (long long)x->Source.Right - x->Source.Left;
  long long sh = (long long)x->Source.Bottom - x->Source.Top;
  long long dw = (long long)x->Destination.Right - x->Destination.Left;
  long long dh = (long long)x->Destination.Bottom - x->Destination.Top;
  switch (Reason) {
  case AdmissionMpoReasonLayerIndex:
    /* The pair rule {0,1} is charged to a plane whose own index is not 0
     * or 1, or that duplicates the other plane's index. */
    return x->LayerIndex <= 1u && x->LayerIndex != o->LayerIndex;
  case AdmissionMpoReasonDisabled:
    return x->Enabled != 0u;
  case AdmissionMpoReasonFormat:
    return x->FormatClass == AdmissionMpoFormatBgra8 ||
           x->FormatClass == AdmissionMpoFormatRgba8;
  case AdmissionMpoReasonColorSpace:
    return x->ColorSpaceClass == AdmissionMpoColorSpaceSdrSrgb;
  case AdmissionMpoReasonTopBlend:
    return x->LayerIndex != 0u || x->BlendAlpha == 1u;
  case AdmissionMpoReasonTopNotFullPanel:
    return x->LayerIndex != 0u ||
           (RectEqual(x->Source, Rect(0, 0, (int)PANEL_W, (int)PANEL_H)) &&
            RectEqual(x->Destination,
                      Rect(0, 0, (int)PANEL_W, (int)PANEL_H)));
  case AdmissionMpoReasonBottomBlend:
    return x->LayerIndex != 1u || x->BlendAlpha == 0u;
  case AdmissionMpoReasonScaling:
    return sw == dw && sh == dh;
  case AdmissionMpoReasonDestinationOutsidePanel:
    return x->Destination.Left >= 0 && x->Destination.Top >= 0 &&
           dw >= 0 && dh >= 0 &&
           x->Destination.Right <= (long long)PANEL_W &&
           x->Destination.Bottom <= (long long)PANEL_H;
  case AdmissionMpoReasonDestinationTooSmall:
    return dw >= 32 && dh >= 32;
  case AdmissionMpoReasonSourceOutsideSurface:
    return x->Source.Left >= 0 && x->Source.Top >= 0 && sw >= 0 && sh >= 0 &&
           x->Source.Right <= (long long)x->SurfaceWidth &&
           x->Source.Bottom <= (long long)x->SurfaceHeight;
  case AdmissionMpoReasonStride:
    return x->SurfaceStride != 0u && x->SurfaceStride % 64u == 0u;
  case AdmissionMpoReasonRotation:
    return x->Rotation == AdmissionMpoRotationIdentity;
  case AdmissionMpoReasonUnsupportedFlags:
    return x->HasUnsupportedFlags == 0u;
  case AdmissionMpoReasonClip:
    return x->Clip.Left <= x->Destination.Left &&
           x->Clip.Top <= x->Destination.Top &&
           x->Clip.Right >= x->Destination.Right &&
           x->Clip.Bottom >= x->Destination.Bottom;
  default:
    return 0;
  }
}

static unsigned int Seed = 0x5eed2u;

static unsigned int Next(unsigned int Bound) {
  Seed = Seed * 1664525u + 1013904223u;
  return (Seed >> 8) % Bound;
}

static int Jitter(int Value) {
  static const int deltas[] = {0, 0, 0, 1, -1, 31, -31, 32, -32, 640, -640,
                               2560};
  return Value + deltas[Next((unsigned int)(sizeof(deltas) /
                                            sizeof(deltas[0])))];
}

static void Mutate(ADMISSION_MPO_PLANE_INPUT *X) {
  switch (Next(17u)) {
  case 0: X->LayerIndex = Next(3u); break;
  case 1: X->Enabled = Next(2u); break;
  case 2: X->FormatClass = Next(4u); break;
  case 3: X->ColorSpaceClass = Next(3u); break;
  case 4: X->BlendAlpha = Next(3u); break;
  case 5: X->Source.Left = Jitter(X->Source.Left);
          X->Source.Right = Jitter(X->Source.Right); break;
  case 6: X->Source.Top = Jitter(X->Source.Top);
          X->Source.Bottom = Jitter(X->Source.Bottom); break;
  case 7: X->Destination.Left = Jitter(X->Destination.Left);
          X->Destination.Right = Jitter(X->Destination.Right); break;
  case 8: X->Destination.Top = Jitter(X->Destination.Top);
          X->Destination.Bottom = Jitter(X->Destination.Bottom); break;
  case 9: X->Clip.Left = Jitter(X->Clip.Left);
          X->Clip.Bottom = Jitter(X->Clip.Bottom); break;
  case 10: X->Rotation = Next(2u); break;
  case 11: X->HasUnsupportedFlags = Next(2u); break;
  case 12: X->SurfaceWidth = (unsigned int)Jitter((int)X->SurfaceWidth); break;
  case 13: X->SurfaceHeight = (unsigned int)Jitter((int)X->SurfaceHeight);
           break;
  case 14: X->SurfaceStride = X->SurfaceStride + Next(3u) * 32u; break;
  case 15: {
    /* Resize source and destination together around the 32 px limit. */
    int extent = 30 + (int)Next(4u);
    if (Next(2u) != 0u) {
      X->Source.Right = X->Source.Left + extent;
      X->Destination.Right = X->Destination.Left + extent;
    } else {
      X->Source.Bottom = X->Source.Top + extent;
      X->Destination.Bottom = X->Destination.Top + extent;
    }
    break;
  }
  default: {
    /* Move the destination (keeps sizes equal; clip follows it). */
    int dx = Jitter(0);
    X->Destination.Left += dx; X->Destination.Right += dx;
    X->Clip = X->Destination;
    break;
  }
  }
}

/* Supported implies every listed rule holds on both planes; unsupported
 * implies the reported plane/rule is exactly the first violation. */
static void TestPropertySweep(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  ADMISSION_MPO_RESULT result;
  unsigned int iteration;
  unsigned int mutations;
  unsigned int index;
  unsigned int rule;
  unsigned int reasonSeen[ADMISSION_MPO_REASON_COUNT];
  unsigned int expectedPlane;
  unsigned int expectedReason;

  memset(reasonSeen, 0, sizeof(reasonSeen));
  for (iteration = 0u; iteration < 200000u; ++iteration) {
    Pair(p, (int)Next(2u));
    for (mutations = Next(4u); mutations != 0u; --mutations)
      Mutate(&p[Next(2u)]);
    (void)Eval(p, 2u, &result);
    expectedPlane = ADMISSION_MPO_NO_PLANE;
    expectedReason = AdmissionMpoSupported;
    for (index = 0u; index < 2u && expectedPlane == ADMISSION_MPO_NO_PLANE;
         ++index) {
      for (rule = AdmissionMpoReasonLayerIndex;
           rule < ADMISSION_MPO_REASON_COUNT; ++rule) {
        if (!RuleHolds(p, index, rule)) {
          expectedPlane = index;
          expectedReason = rule;
          break;
        }
      }
    }
    ++reasonSeen[result.Reason < ADMISSION_MPO_REASON_COUNT ?
                 result.Reason : 0u];
    if (result.Reason != expectedReason ||
        result.FailingPlane != expectedPlane ||
        result.Supported != (expectedReason == AdmissionMpoSupported) ||
        result.SlotCount != (result.Supported ? 2u : 0u)) {
      fprintf(stderr, "sweep %u: got reason %u plane %u, oracle %u plane %u\n",
              iteration, result.Reason, result.FailingPlane, expectedReason,
              expectedPlane);
      ++Failures;
      return;
    }
    if (result.Supported) {
      for (index = 0u; index < 2u; ++index)
        for (rule = AdmissionMpoReasonLayerIndex;
             rule < ADMISSION_MPO_REASON_COUNT; ++rule)
          CHECK(RuleHolds(p, index, rule));
      CHECK(p[result.Slots[0].PlaneIndex].LayerIndex == 1u);
      CHECK(p[result.Slots[1].PlaneIndex].LayerIndex == 0u);
      CHECK(result.Slots[1].Premultiplied == 1u);
    }
  }
  /* The sweep must reach supported and every two-plane rule. */
  for (rule = AdmissionMpoSupported; rule < ADMISSION_MPO_REASON_COUNT;
       ++rule) {
    if (rule == AdmissionMpoReasonArgument ||
        rule == AdmissionMpoReasonPlaneCount)
      continue;
    if (reasonSeen[rule] == 0u) {
      fprintf(stderr, "sweep never produced reason %u\n", rule);
      ++Failures;
    }
  }
}

static void TestStableReasonValues(void) {
  CHECK(AdmissionMpoSupported == 0);
  CHECK(AdmissionMpoReasonArgument == 1);
  CHECK(AdmissionMpoReasonPlaneCount == 2);
  CHECK(AdmissionMpoReasonLayerIndex == 3);
  CHECK(AdmissionMpoReasonDisabled == 4);
  CHECK(AdmissionMpoReasonFormat == 5);
  CHECK(AdmissionMpoReasonColorSpace == 6);
  CHECK(AdmissionMpoReasonTopBlend == 7);
  CHECK(AdmissionMpoReasonTopNotFullPanel == 8);
  CHECK(AdmissionMpoReasonBottomBlend == 9);
  CHECK(AdmissionMpoReasonScaling == 10);
  CHECK(AdmissionMpoReasonDestinationOutsidePanel == 11);
  CHECK(AdmissionMpoReasonDestinationTooSmall == 12);
  CHECK(AdmissionMpoReasonSourceOutsideSurface == 13);
  CHECK(AdmissionMpoReasonStride == 14);
  CHECK(AdmissionMpoReasonRotation == 15);
  CHECK(AdmissionMpoReasonUnsupportedFlags == 16);
  CHECK(AdmissionMpoReasonClip == 17);
  CHECK(ADMISSION_MPO_REASON_COUNT == 18);
}

int main(void) {
  TestStableReasonValues();
  TestArgumentsAndCounts();
  TestOnePlaneMirrorsHandler();
  TestEveryReasonBothOrders();
  TestBoundaries();
  TestPropertySweep();
  if (Failures != 0u) {
    fprintf(stderr, "mpo_contract_test: %u failure(s)\n", Failures);
    return EXIT_FAILURE;
  }
  printf("mpo_contract_test: OK\n");
  return EXIT_SUCCESS;
}
