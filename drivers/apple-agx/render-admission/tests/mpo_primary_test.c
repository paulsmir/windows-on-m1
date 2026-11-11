#include "mpo_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* S2 A1.1: one-plane Check3 must not promise what Set3 refuses.
 * Invariant: AdmissionMpoEvaluatePrimary supported for a plane without
 * placement (Check3) implies the driver's V2 path accepts that plane at any
 * placement that translates into the pool; with the placement (Set3) the
 * function answers exactly what the V2 path accepts.  The oracle below is
 * written from the V2 path's own checks:
 *   display.c AdmissionDdiSetVidPnSourceAddress: hAllocation, VidPnSource 0,
 *     AdmissionScanoutSourceModeSupported(committed) and
 *     AdmissionScanoutFormatSupported(committed format);
 *   scanout_windows.c AdmissionScanoutQueuePresent: allocation magic,
 *     ScanoutCapable, AdmissionAllocationDescriptionValid, committed mode
 *     supported, Width/Height/Pitch == committed, BytesPerPixel 4,
 *     Size == committed stride * height, AdmissionScanoutFormatCode != 0,
 *     AppleAgxLocalSegmentAddressToGpuVa inside the pool. */

static unsigned int Failures;

#define CHECK(Condition)                                                      \
  do {                                                                        \
    if (!(Condition)) {                                                       \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,       \
              #Condition);                                                    \
      ++Failures;                                                             \
    }                                                                         \
  } while (0)

typedef struct _V2_CASE {
  unsigned int ScanoutCapable;   /* includes "a KMD allocation" */
  unsigned int ModeSupported;    /* committed mode and format */
  unsigned int DescriptionValid;
  unsigned int Width, Height, Pitch, BytesPerPixelFour, FormatScanned;
  unsigned long long Size;
  unsigned int Translates;       /* segment address inside the pool */
  unsigned int CommittedWidth, CommittedHeight, CommittedStride;
} V2_CASE;

/* 1 when the V2 path queues the flip (runtime state assumed ready). */
static int V2Accepts(const V2_CASE *C) {
  return C->ModeSupported && C->ScanoutCapable && C->DescriptionValid &&
         C->Width == C->CommittedWidth && C->Height == C->CommittedHeight &&
         C->Pitch == C->CommittedStride && C->BytesPerPixelFour &&
         C->Size == (unsigned long long)C->CommittedStride *
                        C->CommittedHeight &&
         C->FormatScanned && C->Translates;
}

/* The KMD conversion (mpo_windows.c AdmissionMpoPlaneInput): bpp != 4 or
 * an unscanned format is FormatClass Other. */
static void ToInput(const V2_CASE *C, int PlacementKnown,
                    ADMISSION_MPO_PLANE_INPUT *Plane,
                    ADMISSION_MPO_INPUT *Input) {
  memset(Plane, 0, sizeof(*Plane));
  Plane->LayerIndex = 0u;
  Plane->Enabled = 1u;
  Plane->FormatClass = C->BytesPerPixelFour && C->FormatScanned
                           ? AdmissionMpoFormatBgra8
                           : AdmissionMpoFormatOther;
  Plane->SurfaceWidth = C->Width;
  Plane->SurfaceHeight = C->Height;
  Plane->SurfaceStride = C->Pitch;
  Plane->ScanoutCapable = C->ScanoutCapable;
  Plane->DescriptionValid = C->DescriptionValid;
  Plane->AllocationBytes = C->Size;
  Plane->PlacementKnown = PlacementKnown ? 1u : 0u;
  Plane->PlacementInPool = PlacementKnown ? C->Translates : 0u;
  memset(Input, 0, sizeof(*Input));
  Input->PanelWidth = C->CommittedWidth;
  Input->PanelHeight = C->CommittedHeight;
  Input->PanelStride = C->CommittedStride;
  Input->PanelModeSupported = C->ModeSupported;
  Input->PlaneCount = 1u;
  Input->Planes = Plane;
  Input->PoolBytes = 0x3b800000ULL;
}

static unsigned int Seed = 0x1a1a1u;
static unsigned int Next(unsigned int Bound) {
  Seed = Seed * 1664525u + 1013904223u;
  return (Seed >> 8) % Bound;
}

static void RandomCase(V2_CASE *C) {
  static const unsigned int modes[][2] = {{2560u, 1600u}, {1920u, 1200u},
                                          {1280u, 800u}, {1440u, 900u}};
  unsigned int m = Next(4u), g = Next(4u);
  memset(C, 0, sizeof(*C));
  C->CommittedWidth = modes[m][0];
  C->CommittedHeight = modes[m][1];
  C->CommittedStride = C->CommittedWidth * 4u;
  C->ModeSupported = Next(8u) != 0u;
  C->ScanoutCapable = Next(8u) != 0u;
  C->DescriptionValid = Next(8u) != 0u;
  C->BytesPerPixelFour = Next(8u) != 0u;
  C->FormatScanned = Next(8u) != 0u;
  C->Translates = Next(8u) != 0u;
  /* The surface: mostly the committed geometry, sometimes another mode or
   * a padded pitch / rounded size (presentation allocations). */
  C->Width = Next(3u) != 0u ? C->CommittedWidth : modes[g][0];
  C->Height = Next(3u) != 0u ? C->CommittedHeight : modes[g][1];
  C->Pitch = Next(4u) != 0u ? C->Width * 4u : C->Width * 4u + 64u;
  C->Size = (unsigned long long)C->Pitch * C->Height;
  if (Next(5u) == 0u)
    C->Size = (C->Size + 0xffffULL) & ~0xffffULL;
  if (Next(9u) == 0u)
    C->Size += 128u;
}

static void TestSweep(void) {
  V2_CASE c;
  ADMISSION_MPO_PLANE_INPUT plane;
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  unsigned int i, check3Yes = 0u, accepted = 0u;
  unsigned int seen[ADMISSION_MPO_REASON_COUNT];
  memset(seen, 0, sizeof(seen));
  for (i = 0u; i < 300000u; ++i) {
    RandomCase(&c);
    /* Check3 form: no placement.  TRUE must imply V2 acceptance at a
     * placement that translates (the only part Check3 cannot see). */
    ToInput(&c, 0, &plane, &input);
    (void)AdmissionMpoEvaluatePrimary(&input, &result);
    if (result.Reason < ADMISSION_MPO_REASON_COUNT)
      ++seen[result.Reason];
    if (result.Supported) {
      V2_CASE placed = c;
      ++check3Yes;
      placed.Translates = 1u;
      if (!V2Accepts(&placed)) {
        fprintf(stderr, "case %u: Check3 TRUE but the V2 path refuses\n", i);
        ++Failures;
        return;
      }
    }
    /* Set3 form: with the placement the answer equals the V2 path's. */
    ToInput(&c, 1, &plane, &input);
    (void)AdmissionMpoEvaluatePrimary(&input, &result);
    if (result.Reason < ADMISSION_MPO_REASON_COUNT)
      ++seen[result.Reason];
    accepted += result.Supported;
    if ((int)result.Supported != V2Accepts(&c)) {
      fprintf(stderr, "case %u: Set3 rule %u (reason %u), V2 path %d\n", i,
              result.Supported, result.Reason, V2Accepts(&c));
      ++Failures;
      return;
    }
  }
  printf("primary sweep: %u Check3 TRUE, %u Set3 accepted of 300000\n",
         check3Yes, accepted);
  CHECK(check3Yes > 1000u && accepted > 1000u);
  for (i = AdmissionMpoReasonPrimaryNotScanoutCapable;
       i <= AdmissionMpoReasonPrimaryPlacement; ++i)
    if (seen[i] == 0u) {
      fprintf(stderr, "sweep never produced reason %u\n", i);
      ++Failures;
    }
}

static void TestEdges(void) {
  V2_CASE c;
  ADMISSION_MPO_PLANE_INPUT planes[2];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  memset(&c, 0, sizeof(c));
  c.ScanoutCapable = c.ModeSupported = c.DescriptionValid = 1u;
  c.BytesPerPixelFour = c.FormatScanned = c.Translates = 1u;
  c.CommittedWidth = c.Width = 2560u;
  c.CommittedHeight = c.Height = 1600u;
  c.CommittedStride = c.Pitch = 10240u;
  c.Size = 10240ULL * 1600u;
  ToInput(&c, 1, &planes[0], &input);
  CHECK(AdmissionMpoEvaluatePrimary(&input, &result) == AdmissionMpoSupported);
  CHECK(result.Supported == 1u && result.SlotCount == 1u &&
        result.Slots[0].PlaneIndex == 0u);
  /* A disabled plane is the plane-off path: supported whatever it holds. */
  planes[0].Enabled = 0u;
  planes[0].ScanoutCapable = 0u;
  CHECK(AdmissionMpoEvaluatePrimary(&input, &result) == AdmissionMpoSupported);
  /* LayerIndex 1 alone: the v1 answer. */
  ToInput(&c, 1, &planes[0], &input);
  planes[0].LayerIndex = 1u;
  CHECK(AdmissionMpoEvaluatePrimary(&input, &result) ==
        AdmissionMpoReasonLayerIndex);
  /* Two planes are not this function's: PlaneCount. */
  ToInput(&c, 1, &planes[0], &input);
  planes[1] = planes[0];
  input.PlaneCount = 2u;
  CHECK(AdmissionMpoEvaluatePrimary(&input, &result) ==
        AdmissionMpoReasonPlaneCount);
  input.PlaneCount = 0u;
  CHECK(AdmissionMpoEvaluatePrimary(&input, &result) ==
        AdmissionMpoReasonPlaneCount);
  CHECK(AdmissionMpoEvaluatePrimary(NULL, &result) ==
        AdmissionMpoReasonArgument);
  /* A 1280x720 application surface on the 2560x1600 mode: refused. */
  c.Width = 1280u; c.Height = 720u; c.Pitch = 5120u;
  c.Size = 5120ULL * 720u;
  ToInput(&c, 0, &planes[0], &input);
  CHECK(AdmissionMpoEvaluatePrimary(&input, &result) ==
        AdmissionMpoReasonPrimaryGeometry);
  CHECK(result.FailingPlane == 0u && !result.Supported &&
        result.SlotCount == 0u);
}

int main(void) {
  TestEdges();
  TestSweep();
  if (Failures != 0u) {
    fprintf(stderr, "mpo_primary_test: %u failure(s)\n", Failures);
    return EXIT_FAILURE;
  }
  printf("mpo_primary_test: OK\n");
  return EXIT_SUCCESS;
}
