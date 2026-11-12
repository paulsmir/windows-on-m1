#include "mpo_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* S2 rule-table test.  Catches a configuration the display slots cannot
 * show being answered "supported", and the one-plane answers drifting from
 * the display.c one-plane handler. */

#define PANEL_W 2560u
#define PANEL_H 1600u
#define POOL_BYTES 0x3b800000ULL
#define TOP_OFFSET 0x10000ULL
#define BOTTOM_OFFSET 0x1000000ULL

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
  plane.InScanoutPool = 1u;
  plane.PlacementKnown = 1u;
  plane.PoolOffset = TOP_OFFSET;
  plane.SurfaceBytes = (unsigned long long)PANEL_W * 4u * PANEL_H;
  plane.SurfaceIdentity = 0x1000u;
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
  plane.InScanoutPool = 1u;
  plane.PlacementKnown = 1u;
  plane.PoolOffset = BOTTOM_OFFSET;
  plane.SurfaceBytes = 640ULL * 4u * 480u;
  plane.SurfaceIdentity = 0x2000u;
  return plane;
}

static unsigned int Eval(const ADMISSION_MPO_PLANE_INPUT *Planes,
                         unsigned int Count, ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT input;
  memset(&input, 0, sizeof(input));
  input.PanelWidth = PANEL_W;
  input.PanelHeight = PANEL_H;
  input.PlaneCount = Count;
  input.Planes = Planes;
  input.PoolBytes = POOL_BYTES;
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
  memset(&input, 0, sizeof(input));
  memset(&result, 0xa5, sizeof(result));
  CHECK(AdmissionMpoEvaluate(NULL, &result) == AdmissionMpoReasonArgument);
  CHECK(result.Supported == 0u && result.SlotCount == 0u &&
        result.FailingPlane == ADMISSION_MPO_NO_PLANE &&
        result.Slots[0].PlaneIndex == ADMISSION_MPO_NO_PLANE);
  input.PanelWidth = PANEL_W;
  input.PanelHeight = PANEL_H;
  input.PlaneCount = 2u;
  input.Planes = planes;
  input.PoolBytes = POOL_BYTES;
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
    p[bottom].SurfaceStride = 4032u;
    p[bottom].SurfaceBytes = 4032ULL * 800u;
    p[bottom].Source = Rect(360, 320, 1000, 800);
    PAIR_OK(p);
    p[bottom].Source = Rect(361, 320, 1001, 800);
    FAIL_AT(p, 2u, AdmissionMpoReasonSourceOutsideSurface, bottom);
    p[bottom].Source = Rect(-1, 0, 639, 480);
    FAIL_AT(p, 2u, AdmissionMpoReasonSourceOutsideSurface, bottom);

    /* A 64-byte multiple accepted; one byte either side rejected. */
    Pair(p, swap);
    p[bottom].SurfaceStride = 2560u + 64u;
    p[bottom].SurfaceBytes = (2560ULL + 64u) * 480u;
    PAIR_OK(p);
    p[bottom].SurfaceStride = 2560u + 63u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStride, bottom);
    p[bottom].SurfaceStride = 2560u + 65u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStride, bottom);
    /* Version 2: a 64-byte multiple below one row (640 * 4) is refused. */
    p[bottom].SurfaceStride = 2560u - 64u;
    p[bottom].SurfaceBytes = (2560ULL - 64u) * 480u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStrideBelowRow, bottom);

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
  case AdmissionMpoReasonSurfaceNotInPool:
    return x->InScanoutPool != 0u &&
           (x->PlacementKnown == 0u ||
            (x->PoolOffset <= POOL_BYTES &&
             x->SurfaceBytes <= POOL_BYTES - x->PoolOffset));
  case AdmissionMpoReasonSurfaceAlignment:
    return x->PlacementKnown == 0u || x->PoolOffset % 0x10000ULL == 0ULL;
  case AdmissionMpoReasonSurfaceSize:
    return x->SurfaceBytes ==
           (unsigned long long)x->SurfaceStride * x->SurfaceHeight;
  case AdmissionMpoReasonSurfaceLargerThanPanel:
    return x->SurfaceWidth >= 1u && x->SurfaceWidth <= PANEL_W &&
           x->SurfaceHeight >= 1u && x->SurfaceHeight <= PANEL_H;
  case AdmissionMpoReasonStrideBelowRow:
    return (unsigned long long)x->SurfaceStride >=
           4ULL * x->SurfaceWidth;
  default:
    return 0;
  }
}

/* Two-plane per-plane rules; 26.. belong to AdmissionMpoEvaluatePrimary. */
static int PerPlaneRule(unsigned int Reason) {
  return Reason < AdmissionMpoReasonPrimaryNotScanoutCapable &&
         Reason != AdmissionMpoReasonSameSurface &&
         Reason != AdmissionMpoReasonSurfaceOverlap &&
         Reason != AdmissionMpoReasonPanelGeometry;
}

/* Independent pair oracle (both planes already passed every plane rule). */
static unsigned int PairOracle(const ADMISSION_MPO_PLANE_INPUT *A,
                               const ADMISSION_MPO_PLANE_INPUT *B) {
  int known = A->PlacementKnown && B->PlacementKnown;
  unsigned long long aEnd = A->PoolOffset + A->SurfaceBytes;
  unsigned long long bEnd = B->PoolOffset + B->SurfaceBytes;
  if (A->SurfaceIdentity == B->SurfaceIdentity ||
      (known && A->PoolOffset == B->PoolOffset))
    return AdmissionMpoReasonSameSurface;
  if (known && !(aEnd <= B->PoolOffset || bEnd <= A->PoolOffset))
    return AdmissionMpoReasonSurfaceOverlap;
  return AdmissionMpoSupported;
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

static unsigned long long JitterOffset(unsigned long long Value) {
  static const long long deltas[] = {0, 0x10000, -0x10000, 0x8000, 0x40,
                                     -0x40, 0x3b800000, 0xfa0000,
                                     -0xfa0000, 0x12c000};
  return Value + (unsigned long long)deltas[Next(
                     (unsigned int)(sizeof(deltas) / sizeof(deltas[0])))];
}

static void Mutate(ADMISSION_MPO_PLANE_INPUT *X) {
  switch (Next(22u)) {
  case 17: X->InScanoutPool = Next(4u) != 0u ? 1u : 0u; break;
  case 18: X->PoolOffset = JitterOffset(X->PoolOffset); break;
  case 19: X->SurfaceBytes = JitterOffset(X->SurfaceBytes); break;
  case 20: X->SurfaceWidth = X->SurfaceWidth + Next(3u) * 1024u; break;
  case 21: X->SurfaceIdentity = Next(3u) * 0x1000u; break;
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
        if (!PerPlaneRule(rule))
          continue;
        if (!RuleHolds(p, index, rule)) {
          expectedPlane = index;
          expectedReason = rule;
          break;
        }
      }
    }
    if (expectedReason == AdmissionMpoSupported) {
      expectedReason = PairOracle(&p[0], &p[1]);
      if (expectedReason != AdmissionMpoSupported)
        expectedPlane = 1u;
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
          CHECK(!PerPlaneRule(rule) || RuleHolds(p, index, rule));
      CHECK(PairOracle(&p[0], &p[1]) == AdmissionMpoSupported);
      CHECK(p[result.Slots[0].PlaneIndex].LayerIndex == 1u);
      CHECK(p[result.Slots[1].PlaneIndex].LayerIndex == 0u);
      CHECK(result.Slots[1].Premultiplied == 1u);
    }
  }
  /* The sweep must reach supported and every two-plane rule. */
  for (rule = AdmissionMpoSupported; rule < ADMISSION_MPO_REASON_COUNT;
       ++rule) {
    if (rule == AdmissionMpoReasonArgument ||
        rule == AdmissionMpoReasonPlaneCount ||
        rule == AdmissionMpoReasonPanelGeometry ||
        rule >= AdmissionMpoReasonPrimaryNotScanoutCapable)
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
  CHECK(AdmissionMpoReasonSurfaceNotInPool == 18);
  CHECK(AdmissionMpoReasonSurfaceAlignment == 19);
  CHECK(AdmissionMpoReasonSurfaceSize == 20);
  CHECK(AdmissionMpoReasonSurfaceLargerThanPanel == 21);
  CHECK(AdmissionMpoReasonSameSurface == 22);
  CHECK(AdmissionMpoReasonSurfaceOverlap == 23);
  CHECK(AdmissionMpoReasonPanelGeometry == 24);
  CHECK(AdmissionMpoReasonStrideBelowRow == 25);
  CHECK(AdmissionMpoReasonPrimaryNotScanoutCapable == 26);
  CHECK(AdmissionMpoReasonPrimarySourceMode == 27);
  CHECK(AdmissionMpoReasonPrimaryGeometry == 28);
  CHECK(AdmissionMpoReasonPrimarySize == 29);
  CHECK(AdmissionMpoReasonPrimaryFormat == 30);
  CHECK(AdmissionMpoReasonPrimaryPlacement == 31);
  CHECK(AdmissionMpoReasonScaleBeyondLimit == 32);
  CHECK(AdmissionMpoReasonStretchQuality == 33);
  CHECK(AdmissionMpoReasonStretchFilter == 34);
  CHECK(AdmissionMpoReasonOnePlaneBlend == 35);
  CHECK(AdmissionMpoReasonLoneNotFullPanel == 36);
  CHECK(AdmissionMpoReasonLoneBlend == 37);
  CHECK(ADMISSION_MPO_REASON_COUNT == 38);
  CHECK(ADMISSION_MPO_CONTRACT_VERSION == 6u);
}

/* Version 2 surface rules, each on its role, both orders. */
static void TestSurfaceRules(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  int swap;
  unsigned int top;
  unsigned int bottom;

  for (swap = 0; swap < 2; ++swap) {
    top = swap ? 1u : 0u;
    bottom = top ^ 1u;
    Pair(p, swap); p[bottom].InScanoutPool = 0u;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceNotInPool, bottom);
    Pair(p, swap); p[top].PoolOffset = POOL_BYTES - 0x10000ULL;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceNotInPool, top);
    Pair(p, swap); p[top].PoolOffset = POOL_BYTES - p[top].SurfaceBytes;
    PAIR_OK(p);
    Pair(p, swap); p[bottom].PoolOffset = 0xffffffffffff0000ULL;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceNotInPool, bottom);
    Pair(p, swap); p[bottom].PoolOffset = BOTTOM_OFFSET + 0x8000ULL;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceAlignment, bottom);
    /* Unknown placement: no offset rule applies (Check3). */
    Pair(p, swap); p[bottom].PlacementKnown = 0u;
    p[bottom].PoolOffset = 0x8001ULL;
    PAIR_OK(p);
    Pair(p, swap); p[bottom].SurfaceBytes += 0x10000ULL;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceSize, bottom);
    Pair(p, swap); p[top].SurfaceWidth = 2561u;
    p[top].SurfaceStride = 2624u * 4u;
    p[top].SurfaceBytes = 2624ULL * 4u * 1600u;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceLargerThanPanel, top);
    Pair(p, swap); p[bottom].SurfaceWidth = 700u;
    FAIL_AT(p, 2u, AdmissionMpoReasonStrideBelowRow, bottom);
    Pair(p, swap); p[bottom].SurfaceIdentity = p[top].SurfaceIdentity;
    FAIL_AT(p, 2u, AdmissionMpoReasonSameSurface, 1u);
    Pair(p, swap); p[bottom].PoolOffset = p[top].PoolOffset;
    FAIL_AT(p, 2u, AdmissionMpoReasonSameSurface, 1u);
    /* Overlap by one 64 KiB block either way; touching ranges are fine. */
    Pair(p, swap);
    p[bottom].PoolOffset = TOP_OFFSET + p[top].SurfaceBytes - 0x10000ULL;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceOverlap, 1u);
    p[bottom].PoolOffset = TOP_OFFSET + p[top].SurfaceBytes;
    PAIR_OK(p);
    /* Bottom bytes 0x12c000 rounded up to the 64 KiB placement. */
    p[top].PoolOffset = p[bottom].PoolOffset + 0x130000ULL;
    PAIR_OK(p);
    p[top].PoolOffset = p[bottom].PoolOffset + 0x120000ULL;
    FAIL_AT(p, 2u, AdmissionMpoReasonSurfaceOverlap, 1u);
    p[top].PoolOffset = p[bottom].PoolOffset + 0x130000ULL;
    /* Unknown placement cannot overlap-check; identity still applies. */
    p[top].PoolOffset = p[bottom].PoolOffset + 0x120000ULL;
    p[top].PlacementKnown = 0u;
    PAIR_OK(p);
  }
  /* Panel geometry: a non-native source mode is refused first. */
  Pair(p, 0);
  memset(&input, 0, sizeof(input));
  input.PanelWidth = 1920u;
  input.PanelHeight = 1200u;
  input.PlaneCount = 2u;
  input.Planes = p;
  input.PoolBytes = POOL_BYTES;
  CHECK(AdmissionMpoEvaluate(&input, &result) ==
        AdmissionMpoReasonPanelGeometry);
  CHECK(result.FailingPlane == ADMISSION_MPO_NO_PLANE && !result.Supported);
  /* One plane keeps the version-1 answer whatever the surface says. */
  p[0].InScanoutPool = 0u;
  p[0].SurfaceBytes = 1u;
  input.PlaneCount = 1u;
  CHECK(AdmissionMpoEvaluate(&input, &result) == AdmissionMpoSupported);
  /* Slots carry the record inputs. */
  Pair(p, 1);
  CHECK(Eval(p, 2u, &result) == AdmissionMpoSupported);
  CHECK(result.Slots[0].PoolOffset == BOTTOM_OFFSET &&
        result.Slots[0].SurfaceWidth == 640u &&
        result.Slots[0].SurfaceStride == 2560u &&
        result.Slots[0].FormatClass == AdmissionMpoFormatRgba8 &&
        result.Slots[1].PoolOffset == TOP_OFFSET &&
        result.Slots[1].SurfaceBytes == 2560ULL * 4u * 1600u);
}

/* PF-1 (EXP2355/2356): the no-DMA flip DdiPresent.  0x4 Flip and 0x4|0x8
 * FlipWithNoWait as before; 0x1000 FlipWithMultiPlaneOverlay only while the
 * driver reports MPO; every other bit refused. */
static void TestNoDmaFlipPresent(void) {
  unsigned int flags, mpo;
  for (mpo = 0u; mpo < 2u; ++mpo) {
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x4u, mpo) == 1u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0xcu, mpo) == 1u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x1004u, mpo) == mpo);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x100cu, mpo) == mpo);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x0u, mpo) == 0u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x8u, mpo) == 0u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x1000u, mpo) == 0u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x1008u, mpo) == 0u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x1u, mpo) == 0u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x5u, mpo) == 0u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x3004u, mpo) == 0u);
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x104u, mpo) == 0u);
    /* Exhaustive over the low 16 bits: accepted iff one of the four forms. */
    for (flags = 0u; flags < 0x10000u; ++flags) {
      unsigned int expect = flags == 0x4u || flags == 0xcu ||
          (mpo != 0u && (flags == 0x1004u || flags == 0x100cu));
      CHECK(AdmissionMpoNoDmaFlipAccepted(flags, mpo) == expect);
    }
    CHECK(AdmissionMpoNoDmaFlipAccepted(0x8000100cu, mpo) == 0u);
  }
  CHECK(AdmissionMpoNoDmaFlipAccepted(0x100cu, 7u) == 1u);
}


/* ---- STRETCH-1 (contract version 4) ----
 * WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  Check3 answering
 * "supported" for a scaled plane the scanout broker refuses (beyond 2x on
 * one axis, below 32x32), a scaled plane accepted without the broker's
 * scaled-source capability, a filter promise the hardware is not known to
 * keep, the 1:1 answers changing, and a non-full-screen single plane taking
 * the V2 PRESENT path (which scans the whole surface onto the whole panel
 * and would show the wrong picture). */

static unsigned int EvalScaled(const ADMISSION_MPO_PLANE_INPUT *Planes,
                               unsigned int Count, unsigned int Scaling,
                               unsigned int Filter,
                               ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT input;
  memset(&input, 0, sizeof(input));
  input.PanelWidth = PANEL_W;
  input.PanelHeight = PANEL_H;
  input.PlaneCount = Count;
  input.Planes = Planes;
  input.PoolBytes = POOL_BYTES;
  input.ScalingSupported = Scaling;
  input.ScalerFilter = Filter;
  return AdmissionMpoEvaluate(&input, Result);
}

/* The bottom (LayerIndex 1) plane, source 640x480 at (0,0), shown at
 * (100,200) with the given destination size, HIGH quality. */
static void ScaledPair(ADMISSION_MPO_PLANE_INPUT *P, int Swap, int DstW,
                       int DstH) {
  unsigned int bottom = Swap ? 0u : 1u;
  Pair(P, Swap);
  P[bottom].Destination = Rect(100, 200, 100 + DstW, 200 + DstH);
  P[bottom].Clip = P[bottom].Destination;
  P[bottom].StretchQuality = ADMISSION_MPO_STRETCH_QUALITY_HIGH;
}

#define SCALED(P, S, F, R, I) ExpectScaled((P), (S), (F), (R), (I), __LINE__)
static void ExpectScaled(const ADMISSION_MPO_PLANE_INPUT *P,
                         unsigned int Scaling, unsigned int Filter,
                         unsigned int Reason, unsigned int Plane, int Line) {
  ADMISSION_MPO_RESULT result;
  unsigned int got = EvalScaled(P, 2u, Scaling, Filter, &result);
  unsigned int ok = got == Reason && result.Reason == Reason;
  if (Reason == AdmissionMpoSupported) {
    unsigned int bottom = P[0].LayerIndex == 1u ? 0u : 1u;
    ok = ok && result.Supported == 1u && result.SlotCount == 2u &&
         result.Slots[0].PlaneIndex == bottom &&
         RectEqual(result.Slots[0].Source, P[bottom].Source) &&
         RectEqual(result.Slots[0].Destination, P[bottom].Destination) &&
         result.OnePlaneRecord == 0u;
  } else {
    ok = ok && result.Supported == 0u && result.FailingPlane == Plane &&
         result.SlotCount == 0u;
  }
  if (!ok) {
    fprintf(stderr, "line %d: expected scaled reason %u plane %u, got %u "
            "plane %u\n", Line, Reason, Plane, result.Reason,
            result.FailingPlane);
    ++Failures;
  }
}

static void TestScaledPlanes(void) {
  const unsigned int on = 1u;
  const unsigned int unknown = AdmissionMpoScalerFilterUnknown;
  const unsigned int bilinear = AdmissionMpoScalerFilterBilinearOrBetter;
  ADMISSION_MPO_PLANE_INPUT p[2];
  int swap;
  unsigned int bottom, top;

  for (swap = 0; swap < 2; ++swap) {
    bottom = swap ? 0u : 1u;
    top = bottom ^ 1u;
    /* At the limit: exactly 2x up and exactly 2x down, each axis. */
    ScaledPair(p, swap, 1280, 960);
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    ScaledPair(p, swap, 320, 240);
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    /* Below the limit, and the axes independent (2x up X, 2x down Y). */
    ScaledPair(p, swap, 960, 720);
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    ScaledPair(p, swap, 1280, 240);
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    ScaledPair(p, swap, 640, 481);
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    /* One pixel beyond, per axis and per direction. */
    ScaledPair(p, swap, 1281, 960);
    SCALED(p, on, unknown, AdmissionMpoReasonScaleBeyondLimit, bottom);
    ScaledPair(p, swap, 1280, 961);
    SCALED(p, on, unknown, AdmissionMpoReasonScaleBeyondLimit, bottom);
    ScaledPair(p, swap, 319, 240);
    SCALED(p, on, unknown, AdmissionMpoReasonScaleBeyondLimit, bottom);
    ScaledPair(p, swap, 320, 239);
    SCALED(p, on, unknown, AdmissionMpoReasonScaleBeyondLimit, bottom);
    /* Without the broker capability: the version-3 answer (10). */
    ScaledPair(p, swap, 1280, 960);
    SCALED(p, 0u, bilinear, AdmissionMpoReasonScaling, bottom);
    ScaledPair(p, swap, 640, 481);
    SCALED(p, 0u, unknown, AdmissionMpoReasonScaling, bottom);
    /* Filter quality. */
    ScaledPair(p, swap, 960, 720);
    p[bottom].StretchQuality = 0u;
    SCALED(p, on, bilinear, AdmissionMpoReasonStretchQuality, bottom);
    p[bottom].StretchQuality = 3u;
    SCALED(p, on, bilinear, AdmissionMpoReasonStretchQuality, bottom);
    p[bottom].StretchQuality = ADMISSION_MPO_STRETCH_QUALITY_BILINEAR;
    SCALED(p, on, unknown, AdmissionMpoReasonStretchFilter, bottom);
    SCALED(p, on, AdmissionMpoScalerFilterPoint,
           AdmissionMpoReasonStretchFilter, bottom);
    SCALED(p, on, bilinear, AdmissionMpoSupported, 0u);
    p[bottom].StretchQuality = ADMISSION_MPO_STRETCH_QUALITY_HIGH;
    SCALED(p, on, AdmissionMpoScalerFilterPoint, AdmissionMpoSupported, 0u);
    /* An unscaled plane's StretchQuality is never examined (1:1 answers
     * are those of version 3). */
    Pair(p, swap);
    p[bottom].StretchQuality = 0u;
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    SCALED(p, 0u, unknown, AdmissionMpoSupported, 0u);
    /* The panel and minimum-destination rules come before the factor. */
    ScaledPair(p, swap, 2560, 1500);
    SCALED(p, on, unknown, AdmissionMpoReasonDestinationOutsidePanel, bottom);
    Pair(p, swap);
    p[bottom].Source = Rect(0, 0, 16, 16);
    p[bottom].Destination = Rect(100, 200, 132, 232);
    p[bottom].Clip = p[bottom].Destination;
    p[bottom].StretchQuality = ADMISSION_MPO_STRETCH_QUALITY_HIGH;
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    p[bottom].Destination = Rect(100, 200, 131, 232);
    p[bottom].Clip = p[bottom].Destination;
    SCALED(p, on, unknown, AdmissionMpoReasonDestinationTooSmall, bottom);
    p[bottom].Source = Rect(0, 0, 64, 64);
    SCALED(p, on, unknown, AdmissionMpoReasonDestinationTooSmall, bottom);
    /* A cropped source inside the surface scales; outside is 13. */
    ScaledPair(p, swap, 1280, 960);
    p[bottom].Source = Rect(0, 0, 640, 480);
    p[bottom].SurfaceHeight = 479u;
    p[bottom].SurfaceBytes = 640ULL * 4u * 479u;
    SCALED(p, on, unknown, AdmissionMpoReasonSourceOutsideSurface, bottom);
    /* An empty source cannot be scaled to a visible destination. */
    ScaledPair(p, swap, 640, 480);
    p[bottom].Source = Rect(0, 0, 0, 480);
    SCALED(p, on, unknown, AdmissionMpoReasonScaleBeyondLimit, bottom);
    /* Clip: smaller than the scaled destination is refused (the source
     * would have to be cropped by a fractional amount); larger is fine. */
    ScaledPair(p, swap, 1280, 960);
    p[bottom].Clip = Rect(101, 200, 1380, 1160);
    SCALED(p, on, unknown, AdmissionMpoReasonClip, bottom);
    p[bottom].Clip = Rect(0, 0, 2560, 1600);
    SCALED(p, on, unknown, AdmissionMpoSupported, 0u);
    /* The LayerIndex 0 plane stays full panel: never scaled. */
    ScaledPair(p, swap, 1280, 960);
    p[top].Source = Rect(0, 0, 1280, 800);
    p[top].StretchQuality = ADMISSION_MPO_STRETCH_QUALITY_HIGH;
    SCALED(p, on, unknown, AdmissionMpoReasonTopNotFullPanel, top);
    /* With the second layer: pair rules still apply to a scaled pair. */
    ScaledPair(p, swap, 1280, 960);
    p[bottom].SurfaceIdentity = p[top].SurfaceIdentity;
    SCALED(p, on, unknown, AdmissionMpoReasonSameSurface, 1u);
    ScaledPair(p, swap, 1280, 960);
    p[bottom].PoolOffset = TOP_OFFSET + 0x10000ULL;
    SCALED(p, on, unknown, AdmissionMpoReasonSurfaceOverlap, 1u);
    /* The rules after the scale rules are unchanged for a scaled plane. */
    ScaledPair(p, swap, 1280, 960);
    p[bottom].SurfaceStride = 2560u + 4u;
    SCALED(p, on, unknown, AdmissionMpoReasonStride, bottom);
    ScaledPair(p, swap, 1280, 960);
    p[bottom].InScanoutPool = 0u;
    SCALED(p, on, unknown, AdmissionMpoReasonSurfaceNotInPool, bottom);
  }
}

/* One plane through AdmissionMpoEvaluateOne. */
static ADMISSION_MPO_PLANE_INPUT OnePlane(int SrcW, int SrcH, int DstL,
                                          int DstT, int DstR, int DstB) {
  ADMISSION_MPO_PLANE_INPUT plane = BottomPlane();
  plane.LayerIndex = 0u;
  plane.BlendAlpha = 1u;
  plane.SurfaceWidth = (unsigned int)SrcW;
  plane.SurfaceHeight = (unsigned int)SrcH;
  plane.SurfaceStride = ((unsigned int)SrcW * 4u + 63u) & ~63u;
  plane.SurfaceBytes =
      (unsigned long long)plane.SurfaceStride * (unsigned long long)SrcH;
  plane.AllocationBytes = plane.SurfaceBytes;
  plane.Source = Rect(0, 0, SrcW, SrcH);
  plane.Destination = Rect(DstL, DstT, DstR, DstB);
  plane.Clip = plane.Destination;
  plane.StretchQuality = ADMISSION_MPO_STRETCH_QUALITY_HIGH;
  plane.ScanoutCapable = 1u;
  plane.DescriptionValid = 1u;
  plane.PoolOffset = TOP_OFFSET;
  plane.PlacementInPool = 1u;
  return plane;
}

static unsigned int EvalOne(const ADMISSION_MPO_PLANE_INPUT *Plane,
                            unsigned int Scaling,
                            ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT input;
  memset(&input, 0, sizeof(input));
  input.PanelWidth = PANEL_W;
  input.PanelHeight = PANEL_H;
  input.PanelStride = PANEL_W * 4u;
  input.PanelModeSupported = 1u;
  input.PlaneCount = 1u;
  input.Planes = Plane;
  input.PoolBytes = POOL_BYTES;
  input.ScalingSupported = Scaling;
  input.ScalerFilter = AdmissionMpoScalerFilterUnknown;
  memset(Result, 0xa5, sizeof(*Result));
  return AdmissionMpoEvaluateOne(&input, Result);
}

static void TestEvaluateOne(void) {
  ADMISSION_MPO_PLANE_INPUT plane;
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result, primary;

  /* The full-screen primary: exactly AdmissionMpoEvaluatePrimary, V2. */
  plane = OnePlane((int)PANEL_W, (int)PANEL_H, 0, 0, (int)PANEL_W,
                   (int)PANEL_H);
  plane.StretchQuality = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported);
  CHECK(result.OnePlaneRecord == 0u && result.SlotCount == 1u);
  memset(&input, 0, sizeof(input));
  input.PanelWidth = PANEL_W; input.PanelHeight = PANEL_H;
  input.PanelStride = PANEL_W * 4u; input.PanelModeSupported = 1u;
  input.PlaneCount = 1u; input.Planes = &plane; input.PoolBytes = POOL_BYTES;
  plane.SurfaceStride = PANEL_W * 4u + 64u; /* primary geometry differs */
  CHECK(AdmissionMpoEvaluatePrimary(&input, &primary) ==
        AdmissionMpoReasonPrimaryGeometry);
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonPrimaryGeometry);
  CHECK(result.OnePlaneRecord == 0u && result.FailingPlane == 0u);
  /* A disabled plane is the plane-off path, as before. */
  plane = OnePlane(1280, 800, 0, 0, 1280, 800);
  plane.Enabled = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported &&
        result.OnePlaneRecord == 0u);
  /* FS-1: LayerIndex 1 alone is the lone application plane (TestLonePlane);
   * blended it is refused. */
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonLoneBlend);

  /* A 1280x800 full-screen game on the 2560x1600 panel: one layer record,
   * slot 0 = source and destination, not premultiplied. */
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported);
  CHECK(result.Supported == 1u && result.OnePlaneRecord == 1u &&
        result.SlotCount == 1u && result.Slots[0].PlaneIndex == 0u &&
        result.Slots[0].Premultiplied == 0u &&
        RectEqual(result.Slots[0].Source, Rect(0, 0, 1280, 800)) &&
        RectEqual(result.Slots[0].Destination, Rect(0, 0, 2560, 1600)) &&
        result.Slots[0].PoolOffset == TOP_OFFSET &&
        result.Slots[0].SurfaceStride == 1280u * 4u);
  /* Same without the broker capability: Scaling, as version 3 would. */
  CHECK(EvalOne(&plane, 0u, &result) == AdmissionMpoReasonScaling &&
        result.Supported == 0u && result.SlotCount == 0u &&
        result.OnePlaneRecord == 0u);
  /* Letterboxed 16:9 at 2x. */
  plane = OnePlane(1280, 720, 0, 80, 2560, 1520);
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported &&
        result.OnePlaneRecord == 1u);
  /* A 1:1 positioned plane needs no scaling capability. */
  plane = OnePlane(800, 600, 100, 100, 900, 700);
  CHECK(EvalOne(&plane, 0u, &result) == AdmissionMpoSupported &&
        result.OnePlaneRecord == 1u);
  /* A full-panel surface cropped 1:1 is not the full-screen primary. */
  plane = OnePlane((int)PANEL_W, (int)PANEL_H, 0, 0, 1280, 800);
  plane.Source = Rect(0, 0, 1280, 800);
  CHECK(EvalOne(&plane, 0u, &result) == AdmissionMpoSupported &&
        result.OnePlaneRecord == 1u);
  /* Beyond the limit: 854x534 -> 2560x1600 is 2.997x. */
  plane = OnePlane(854, 534, 0, 0, 2560, 1600);
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonScaleBeyondLimit &&
        result.FailingPlane == 0u && result.OnePlaneRecord == 0u);
  /* The one-plane record rules, in order. */
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.FormatClass = AdmissionMpoFormatOther;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonFormat);
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.ColorSpaceClass = AdmissionMpoColorSpaceOther;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonColorSpace);
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.BlendAlpha = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported);
  plane.BlendAlpha = 2u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonOnePlaneBlend);
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.StretchQuality = ADMISSION_MPO_STRETCH_QUALITY_BILINEAR;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonStretchFilter);
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.InScanoutPool = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonSurfaceNotInPool);
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.Clip = Rect(0, 0, 2560, 1599);
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonClip);
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.PlacementKnown = 1u;
  plane.PoolOffset = TOP_OFFSET + 0x40ULL;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonSurfaceAlignment);
  /* Arguments. */
  CHECK(AdmissionMpoEvaluateOne(NULL, &result) == AdmissionMpoReasonArgument);
  memset(&input, 0, sizeof(input));
  input.PanelWidth = PANEL_W; input.PanelHeight = PANEL_H;
  input.PlaneCount = 2u; input.Planes = &plane;
  CHECK(AdmissionMpoEvaluateOne(&input, &result) ==
        AdmissionMpoReasonPlaneCount);
}

static float BitsToFloat(unsigned int Bits) {
  float value;
  memcpy(&value, &Bits, sizeof(value));
  return value;
}

static void TestCapsDerivation(void) {
  ADMISSION_MPO_CAPS caps;
  unsigned int effective, scaling, filter, source;

  CHECK(sizeof(float) == sizeof(unsigned int));
  CHECK(BitsToFloat(ADMISSION_MPO_FLOAT_BITS_ONE) == 1.0f);
  CHECK(BitsToFloat(ADMISSION_MPO_FLOAT_BITS_TWO) == 2.0f);
  CHECK(BitsToFloat(ADMISSION_MPO_FLOAT_BITS_HALF) == 0.5f);
  for (effective = 0u; effective < 2u; ++effective)
    for (scaling = 0u; scaling < 2u; ++scaling)
      for (filter = 0u; filter < 3u; ++filter)
        for (source = 0u; source < 2u; ++source) {
          unsigned int two = effective != 0u && source == 0u;
          unsigned int stretch = two && scaling != 0u;
          memset(&caps, 0xa5, sizeof(caps));
          AdmissionMpoCapsFor(effective, scaling, filter, source, &caps);
          CHECK(caps.MaxPlanes == (two ? 2u : 1u));
          CHECK(caps.StretchRgb == stretch);
          CHECK(caps.BilinearFilter ==
                (stretch && filter == AdmissionMpoScalerFilterBilinearOrBetter
                     ? 1u : 0u));
          CHECK(caps.HighFilter == 0u);
          /* The reported factors are exactly the table's limit: Windows is
           * never promised a factor the table refuses, and every factor
           * the table accepts is within what Windows was told. */
          CHECK(BitsToFloat(caps.MaxStretchBits) ==
                (stretch ? (float)ADMISSION_MPO_MAX_SCALE : 1.0f));
          CHECK(BitsToFloat(caps.MaxShrinkBits) ==
                (stretch ? 1.0f / (float)ADMISSION_MPO_MAX_SCALE : 1.0f));
        }
  AdmissionMpoCapsFor(1u, 1u, 0u, 0u, NULL);
}


/* STRETCH-1b (EXP2374): a configuration Check3 supports is never refused on
 * the Set3 path by a Check3-only rule.  Walks every reason the table can
 * return; for the Check3-only ones (ADMISSION_MPO_CHECK3_ONLY_REASONS)
 * sweeps the inputs that reach them on the Check3 path and asserts the
 * Set3 path answers supported, and for every sampled configuration asserts
 * Check3 supported implies Set3 supported (both plane counts). */
static unsigned int EvalPath(const ADMISSION_MPO_PLANE_INPUT *Planes,
                             unsigned int Count, unsigned int Filter,
                             unsigned int Path, ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT input;
  memset(&input, 0, sizeof(input));
  input.PanelWidth = PANEL_W;
  input.PanelHeight = PANEL_H;
  input.PanelStride = PANEL_W * 4u;
  input.PanelModeSupported = 1u;
  input.PlaneCount = Count;
  input.Planes = Planes;
  input.PoolBytes = POOL_BYTES;
  input.ScalingSupported = 1u;
  input.ScalerFilter = Filter;
  input.Path = Path;
  memset(Result, 0xa5, sizeof(*Result));
  return Count == 1u ? AdmissionMpoEvaluateOne(&input, Result)
                     : AdmissionMpoEvaluate(&input, Result);
}

static void TestCheck3OnlyRulesNotReachableFromSet3(void) {
  static const int sizes[][2] = {{640, 480}, {960, 720}, {1280, 960},
                                 {320, 240}, {1281, 960}, {640, 481}};
  static const unsigned int filters[] = {AdmissionMpoScalerFilterUnknown,
                                         AdmissionMpoScalerFilterPoint,
                                         AdmissionMpoScalerFilterBilinearOrBetter};
  ADMISSION_MPO_PLANE_INPUT p[2], one;
  ADMISSION_MPO_RESULT check3, set3;
  unsigned long long reached = 0u, reason;
  unsigned int q, f, z, swap, rc, rs;

  CHECK(ADMISSION_MPO_PATH_CHECK3 == 0u); /* a zeroed input is the Check3 path */
  for (reason = 1u; reason < 64u; ++reason)
    if ((ADMISSION_MPO_CHECK3_ONLY_REASONS >> reason) & 1ull)
      CHECK(reason == AdmissionMpoReasonStretchQuality ||
            reason == AdmissionMpoReasonStretchFilter);
  for (swap = 0u; swap < 2u; ++swap)
    for (z = 0u; z < sizeof(sizes) / sizeof(sizes[0]); ++z)
      for (q = 0u; q < 6u; ++q)
        for (f = 0u; f < sizeof(filters) / sizeof(filters[0]); ++f) {
          unsigned int bottom = swap ? 0u : 1u;
          ScaledPair(p, (int)swap, sizes[z][0], sizes[z][1]);
          p[bottom].StretchQuality = q;
          rc = EvalPath(p, 2u, filters[f], ADMISSION_MPO_PATH_CHECK3, &check3);
          rs = EvalPath(p, 2u, filters[f], ADMISSION_MPO_PATH_SET3, &set3);
          if ((ADMISSION_MPO_CHECK3_ONLY_REASONS >> rc) & 1ull) reached |= 1ull << rc;
          CHECK(!((ADMISSION_MPO_CHECK3_ONLY_REASONS >> rs) & 1ull));
          CHECK(rc != AdmissionMpoSupported || rs == AdmissionMpoSupported);
          if ((ADMISSION_MPO_CHECK3_ONLY_REASONS >> rc) & 1ull)
            CHECK(rs == AdmissionMpoSupported);
          /* The same plane alone through the one-plane record path. */
          one = p[bottom];
          one.LayerIndex = 0u;
          rc = EvalPath(&one, 1u, filters[f], ADMISSION_MPO_PATH_CHECK3, &check3);
          rs = EvalPath(&one, 1u, filters[f], ADMISSION_MPO_PATH_SET3, &set3);
          CHECK(!((ADMISSION_MPO_CHECK3_ONLY_REASONS >> rs) & 1ull));
          CHECK(rc != AdmissionMpoSupported || rs == AdmissionMpoSupported);
        }
  /* The sweep did reach both Check3-only reasons on the Check3 path. */
  CHECK((reached >> AdmissionMpoReasonStretchQuality) & 1ull);
  CHECK((reached >> AdmissionMpoReasonStretchFilter) & 1ull);
}


/* FS-1: the application plane alone (EXP2368/2375: DWM asks it for a
 * full-screen game; refused with reason 3 before).  Full panel yes, partial
 * no, scaled up to 2x yes, beyond no, blended no, LayerIndex 0 unchanged. */
static void TestLonePlane(void) {
  ADMISSION_MPO_PLANE_INPUT plane;
  ADMISSION_MPO_RESULT result;
  /* 1:1 full screen (CS at 2560x1600). */
  plane = OnePlane(2560, 1600, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u; plane.BlendAlpha = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported);
  CHECK(result.OnePlaneRecord == 1u && result.SlotCount == 1u &&
        result.Slots[0].PlaneIndex == 0u && result.Slots[0].Premultiplied == 0u &&
        RectEqual(result.Slots[0].Destination, Rect(0, 0, 2560, 1600)));
  CHECK(EvalOne(&plane, 0u, &result) == AdmissionMpoSupported); /* unscaled needs no scaler */
  /* Scaled 2x (STRETCHFS 1280x800), and 1.5x. */
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u; plane.BlendAlpha = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported && result.OnePlaneRecord == 1u);
  CHECK(EvalOne(&plane, 0u, &result) == AdmissionMpoReasonScaling);
  plane = OnePlane(1707, 1067, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u; plane.BlendAlpha = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoSupported);
  /* Beyond 2x (1024x768 -> 2560x1600 = 2.5x / 2.08x). */
  plane = OnePlane(1024, 768, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u; plane.BlendAlpha = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonScaleBeyondLimit);
  /* Partial destination (a window): not the lone form. */
  plane = OnePlane(1280, 720, 640, 457, 1920, 1177);
  plane.LayerIndex = 1u; plane.BlendAlpha = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonLoneNotFullPanel);
  /* Blended. */
  plane = OnePlane(2560, 1600, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u; plane.BlendAlpha = 1u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonLoneBlend);
  /* Format and pool rules still apply. */
  plane.BlendAlpha = 0u; plane.FormatClass = AdmissionMpoFormatOther;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonFormat);
  plane = OnePlane(2560, 1600, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u; plane.BlendAlpha = 0u; plane.InScanoutPool = 0u;
  CHECK(EvalOne(&plane, 1u, &result) == AdmissionMpoReasonSurfaceNotInPool);
  /* The same plane in the Set3 rule set. */
  plane = OnePlane(1280, 800, 0, 0, 2560, 1600);
  plane.LayerIndex = 1u; plane.BlendAlpha = 0u; plane.StretchQuality = 0u;
  {
    ADMISSION_MPO_INPUT input;
    memset(&input, 0, sizeof(input));
    input.PanelWidth = PANEL_W; input.PanelHeight = PANEL_H;
    input.PanelStride = PANEL_W * 4u; input.PanelModeSupported = 1u;
    input.PlaneCount = 1u; input.Planes = &plane; input.PoolBytes = POOL_BYTES;
    input.ScalingSupported = 1u; input.Path = ADMISSION_MPO_PATH_SET3;
    CHECK(AdmissionMpoEvaluateOne(&input, &result) == AdmissionMpoSupported);
    input.Path = ADMISSION_MPO_PATH_CHECK3;
    CHECK(AdmissionMpoEvaluateOne(&input, &result) == AdmissionMpoReasonStretchQuality);
  }
}

int main(void) {
  TestStableReasonValues();
  TestArgumentsAndCounts();
  TestOnePlaneMirrorsHandler();
  TestEveryReasonBothOrders();
  TestBoundaries();
  TestSurfaceRules();
  TestPropertySweep();
  TestNoDmaFlipPresent();
  TestScaledPlanes();
  TestEvaluateOne();
  TestCapsDerivation();
  TestCheck3OnlyRulesNotReachableFromSet3();
  TestLonePlane();
  if (Failures != 0u) {
    fprintf(stderr, "mpo_contract_test: %u failure(s)\n", Failures);
    return EXIT_FAILURE;
  }
  printf("mpo_contract_test: OK\n");
  return EXIT_SUCCESS;
}
