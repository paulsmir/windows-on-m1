#include "mpo_contract.h"
#include "mpo_layers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* S2: the rule table must never be more permissive than the scanout broker.
 * Catches a configuration Check3/Set3 would answer "supported" whose
 * records the broker refuses (a contract defect that turns into a refused
 * flip on hardware).  The reference predicate below is written from the
 * refusal list of the broker hand-back (.local/night-20261110/
 * s2-broker-v3/REPORT.md, codes 13..23), not from the m1n1 source. */

#define REF_PANEL_W 2560u
#define REF_PANEL_H 1600u
#define REF_POOL 0x3b800000ULL

static unsigned int Failures;

#define CHECK(Condition)                                                      \
  do {                                                                        \
    if (!(Condition)) {                                                       \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,       \
              #Condition);                                                    \
      ++Failures;                                                             \
    }                                                                         \
  } while (0)

static unsigned int Lo(unsigned int V) { return V & 0xffffu; }
static unsigned int Hi(unsigned int V) { return V >> 16; }

/* Within 2x up or down. */
static int RefScaleOk(unsigned int Src, unsigned int Dst) {
  return (unsigned long long)Dst <= 2ULL * Src &&
         (unsigned long long)Src <= 2ULL * Dst;
}

/* One record: 0 when accepted, else the report's refusal code. */
static unsigned int RefRecord(const APPLE_AGX_SCANOUT_LAYER *R,
                              unsigned long long Pool) {
  unsigned int sx = Lo(R->SrcOrigin), sy = Hi(R->SrcOrigin);
  unsigned int sw = Lo(R->SrcSize), sh = Hi(R->SrcSize);
  unsigned int dx = Lo(R->DstOrigin), dy = Hi(R->DstOrigin);
  unsigned int dw = Lo(R->DstSize), dh = Hi(R->DstSize);
  /* 14: a flag bit other than bit 0 (reserved fields are written 0). */
  if ((R->Flags & ~1u) != 0u)
    return 14u;
  /* 15: format is not 1 or 2. */
  if (R->Format != 1u && R->Format != 2u)
    return 15u;
  /* 16: width/height 0 or larger than 2560x1600 ... */
  if (R->Width == 0u || R->Height == 0u || R->Width > REF_PANEL_W ||
      R->Height > REF_PANEL_H)
    return 16u;
  /* 17: stride < width x 4, or not a multiple of 64. */
  if ((unsigned long long)R->Stride < 4ULL * R->Width || R->Stride % 64u != 0u)
    return 17u;
  /* 16 (second half): size != stride x height. */
  if (R->SurfaceSize != (unsigned long long)R->Stride * R->Height)
    return 16u;
  /* 18: offset not 64 KiB aligned, offset + size overflows, or the surface
   * ends past the registered pool. */
  if (R->SurfaceOffset % 0x10000ULL != 0ULL ||
      R->SurfaceOffset + R->SurfaceSize < R->SurfaceOffset ||
      R->SurfaceOffset + R->SurfaceSize > Pool)
    return 18u;
  /* 19: source rect empty or outside its surface. */
  if (sw == 0u || sh == 0u || (unsigned long long)sx + sw > R->Width ||
      (unsigned long long)sy + sh > R->Height)
    return 19u;
  /* 20: destination smaller than 32x32 or outside the panel. */
  if (dw < 32u || dh < 32u || (unsigned long long)dx + dw > REF_PANEL_W ||
      (unsigned long long)dy + dh > REF_PANEL_H)
    return 20u;
  /* 21: beyond 2x up or 2x down on either axis. */
  if (!RefScaleOk(sw, dw) || !RefScaleOk(sh, dh))
    return 21u;
  return 0u;
}

static unsigned int RefCommit(const APPLE_AGX_SCANOUT_LAYER *R,
                              unsigned int Count, unsigned long long Pool) {
  unsigned int i, code;
  /* 13: count is not 1 or 2. */
  if (Count != 1u && Count != 2u)
    return 13u;
  for (i = 0u; i < Count; ++i) {
    code = RefRecord(&R[i], Pool);
    if (code != 0u)
      return code;
  }
  if (Count == 2u) {
    /* 22: both records have the same offset. */
    if (R[0].SurfaceOffset == R[1].SurfaceOffset)
      return 22u;
    /* 23: the two byte ranges overlap. */
    if (R[0].SurfaceOffset < R[1].SurfaceOffset + R[1].SurfaceSize &&
        R[1].SurfaceOffset < R[0].SurfaceOffset + R[0].SurfaceSize)
      return 23u;
  }
  return 0u;
}

static ADMISSION_MPO_RECT Rect(int L, int T, int R, int B) {
  ADMISSION_MPO_RECT rect;
  rect.Left = L; rect.Top = T; rect.Right = R; rect.Bottom = B;
  return rect;
}

static ADMISSION_MPO_PLANE_INPUT Desktop(void) {
  ADMISSION_MPO_PLANE_INPUT p;
  memset(&p, 0, sizeof(p));
  p.LayerIndex = 0u; p.Enabled = 1u;
  p.FormatClass = AdmissionMpoFormatBgra8;
  p.ColorSpaceClass = AdmissionMpoColorSpaceSdrSrgb;
  p.BlendAlpha = 1u;
  p.Source = Rect(0, 0, 2560, 1600);
  p.Destination = p.Source; p.Clip = p.Source;
  p.SurfaceWidth = 2560u; p.SurfaceHeight = 1600u; p.SurfaceStride = 10240u;
  p.InScanoutPool = 1u; p.PlacementKnown = 1u;
  p.PoolOffset = 0ULL; p.SurfaceBytes = 10240ULL * 1600u;
  p.SurfaceIdentity = 0x100u;
  return p;
}

static ADMISSION_MPO_PLANE_INPUT Application(void) {
  ADMISSION_MPO_PLANE_INPUT p;
  memset(&p, 0, sizeof(p));
  p.LayerIndex = 1u; p.Enabled = 1u;
  p.FormatClass = AdmissionMpoFormatRgba8;
  p.ColorSpaceClass = AdmissionMpoColorSpaceSdrSrgb;
  p.BlendAlpha = 0u;
  p.Source = Rect(0, 0, 1280, 720);
  p.Destination = Rect(400, 300, 1680, 1020); p.Clip = p.Destination;
  p.SurfaceWidth = 1280u; p.SurfaceHeight = 720u; p.SurfaceStride = 5120u;
  p.InScanoutPool = 1u; p.PlacementKnown = 1u;
  p.PoolOffset = 0x1000000ULL; p.SurfaceBytes = 5120ULL * 720u;
  p.SurfaceIdentity = 0x200u;
  return p;
}

static unsigned int Seed = 0x52b0a3u;
static unsigned int Next(unsigned int Bound) {
  Seed = Seed * 1103515245u + 12345u;
  return (Seed >> 8) % Bound;
}

static int JitterI(int Value) {
  static const int d[] = {0, 0, 1, -1, 31, -31, 32, 640, -640, 1280, 2560,
                          -2560, 1000, 65535};
  return Value + d[Next((unsigned int)(sizeof(d) / sizeof(d[0])))];
}

static unsigned long long JitterU64(unsigned long long Value) {
  static const long long d[] = {0, 0x10000, -0x10000, 0x40, -0x40, 0x8000,
                                0x3b800000, -0x1000000, 0x384000,
                                0xfa0000, 0x7fffffffffff0000LL};
  return Value + (unsigned long long)d[Next(
                     (unsigned int)(sizeof(d) / sizeof(d[0])))];
}

static void Mutate(ADMISSION_MPO_PLANE_INPUT *P, ADMISSION_MPO_PLANE_INPUT *O) {
  switch (Next(20u)) {
  case 0: P->Source.Left = JitterI(P->Source.Left); break;
  case 1: P->Source.Right = JitterI(P->Source.Right); break;
  case 2: P->Source.Bottom = JitterI(P->Source.Bottom); break;
  case 3: P->Destination.Left = JitterI(P->Destination.Left);
          P->Destination.Right = JitterI(P->Destination.Right); break;
  case 4: P->Destination.Top = JitterI(P->Destination.Top);
          P->Destination.Bottom = JitterI(P->Destination.Bottom); break;
  case 5: { int dx = JitterI(0);
            P->Destination.Left += dx; P->Destination.Right += dx;
            P->Clip = P->Destination; break; }
  case 6: { int e = JitterI(0); /* resize source and destination together */
            P->Source.Right += e; P->Destination.Right += e;
            P->Clip = P->Destination; break; }
  case 7: P->SurfaceWidth = (unsigned int)JitterI((int)P->SurfaceWidth); break;
  case 8: P->SurfaceHeight = (unsigned int)JitterI((int)P->SurfaceHeight);
          break;
  case 9: P->SurfaceStride = P->SurfaceStride + Next(5u) * 32u - 64u; break;
  case 10: P->SurfaceBytes = JitterU64(P->SurfaceBytes); break;
  case 11: P->PoolOffset = JitterU64(P->PoolOffset); break;
  case 12: P->PoolOffset = O->PoolOffset; break;
  case 13: P->PoolOffset = O->PoolOffset + (O->SurfaceBytes & ~0xffffULL);
           break;
  case 14: P->SurfaceIdentity = O->SurfaceIdentity; break;
  case 15: P->FormatClass = Next(3u); break;
  case 16: P->Clip = Rect(0, 0, 2560, 1600); break;
  case 17: P->SurfaceBytes = (unsigned long long)P->SurfaceStride *
                             P->SurfaceHeight; break;
  case 18: P->InScanoutPool = Next(5u) != 0u ? 1u : 0u; break;
  default: { /* a big or small application window */
    unsigned int w = 32u * (1u + Next(80u)), h = 32u * (1u + Next(50u));
    P->Source = Rect(0, 0, (int)w, (int)h);
    P->SurfaceWidth = w; P->SurfaceHeight = h; P->SurfaceStride = w * 4u;
    P->SurfaceBytes = (unsigned long long)w * 4u * h;
    break;
  }
  }
}

/* Records built straight from the inputs, bypassing the table (coverage:
 * proves the sweep reaches each broker refusal). */
static void RawRecord(const ADMISSION_MPO_PLANE_INPUT *P,
                      APPLE_AGX_SCANOUT_LAYER *R) {
  memset(R, 0, sizeof(*R));
  R->SurfaceOffset = P->PoolOffset; R->SurfaceSize = P->SurfaceBytes;
  R->Width = P->SurfaceWidth; R->Height = P->SurfaceHeight;
  R->Stride = P->SurfaceStride;
  R->Format = P->FormatClass == AdmissionMpoFormatBgra8 ? 1u :
              P->FormatClass == AdmissionMpoFormatRgba8 ? 2u : 7u;
  R->SrcOrigin = APPLE_AGX_SCANOUT_PACK((unsigned int)P->Source.Left,
                                        (unsigned int)P->Source.Top);
  R->SrcSize = APPLE_AGX_SCANOUT_PACK(
      (unsigned int)(P->Source.Right - P->Source.Left),
      (unsigned int)(P->Source.Bottom - P->Source.Top));
  R->DstOrigin = APPLE_AGX_SCANOUT_PACK((unsigned int)P->Destination.Left,
                                        (unsigned int)P->Destination.Top);
  R->DstSize = APPLE_AGX_SCANOUT_PACK(
      (unsigned int)(P->Destination.Right - P->Destination.Left),
      (unsigned int)(P->Destination.Bottom - P->Destination.Top));
  R->Flags = 1u;
}

static void TestSweepTableImpliesBroker(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2];
  APPLE_AGX_SCANOUT_LAYER raw[2];
  unsigned int iteration, m, code, swap, app;
  unsigned int supported = 0u;
  unsigned int rawRefused[24];

  memset(rawRefused, 0, sizeof(rawRefused));
  for (iteration = 0u; iteration < 400000u; ++iteration) {
    swap = Next(2u);
    p[swap] = Desktop();
    p[swap ^ 1u] = Application();
    app = swap ^ 1u;
    for (m = Next(4u); m != 0u; --m) {
      unsigned int which = Next(4u) == 0u ? swap : app;
      Mutate(&p[which], &p[which ^ 1u]);
    }
    input.PanelWidth = Next(16u) == 0u ? 1920u : REF_PANEL_W;
    input.PanelHeight = input.PanelWidth == REF_PANEL_W ? REF_PANEL_H : 1200u;
    input.PlaneCount = 2u;
    input.Planes = p;
    input.PoolBytes = REF_POOL;
    (void)AdmissionMpoEvaluate(&input, &result);

    RawRecord(&p[app], &raw[0]);
    RawRecord(&p[swap], &raw[1]);
    code = RefCommit(raw, 2u, REF_POOL);
    if (code < 24u)
      ++rawRefused[code];

    if (!result.Supported)
      continue;
    ++supported;
    if (AdmissionMpoBuildCommit(&result, records) != 2u) {
      fprintf(stderr, "sweep %u: supported but no records\n", iteration);
      ++Failures;
      return;
    }
    /* Record 0 = LayerIndex 1 (application), record 1 = LayerIndex 0. */
    CHECK(records[0].SurfaceOffset == p[app].PoolOffset);
    CHECK(records[1].SurfaceOffset == p[swap].PoolOffset);
    CHECK(records[0].Flags == APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED &&
          records[1].Flags == APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
    code = RefCommit(records, 2u, REF_POOL);
    if (code != 0u) {
      fprintf(stderr,
              "sweep %u: table supported, broker reference refuses %u "
              "(app off 0x%llx size 0x%llx w %u h %u s %u; desk off 0x%llx)\n",
              iteration, code, records[0].SurfaceOffset,
              records[0].SurfaceSize, records[0].Width, records[0].Height,
              records[0].Stride, records[1].SurfaceOffset);
      ++Failures;
      return;
    }
  }
  printf("sweep: %u supported of 400000\n", supported);
  CHECK(supported > 1000u);
  /* The sweep must reach every record/pair refusal the table can meet. */
  for (code = 15u; code <= 23u; ++code) {
    if (rawRefused[code] == 0u) {
      fprintf(stderr, "sweep never produced broker refusal %u\n", code);
      ++Failures;
    }
  }
}

static void TestRemovalRecord(void) {
  ADMISSION_MPO_PLANE_INPUT desk = Desktop();
  ADMISSION_MPO_PLANE_INPUT app = Application();
  APPLE_AGX_SCANOUT_LAYER record;
  memset(&record, 0xa5, sizeof(record));
  CHECK(AdmissionMpoBuildRemoval(&desk, &record) == 1u);
  CHECK(record.Flags == 0u && record.SurfaceOffset == desk.PoolOffset &&
        record.DstSize == APPLE_AGX_SCANOUT_PACK(2560u, 1600u) &&
        record.SrcOrigin == 0u);
  CHECK(RefCommit(&record, 1u, REF_POOL) == 0u);
  /* Only the LayerIndex 0 plane with a known pool placement. */
  CHECK(AdmissionMpoBuildRemoval(&app, &record) == 0u);
  desk.PlacementKnown = 0u;
  CHECK(AdmissionMpoBuildRemoval(&desk, &record) == 0u);
  desk = Desktop();
  desk.InScanoutPool = 0u;
  CHECK(AdmissionMpoBuildRemoval(&desk, &record) == 0u);
}

static void TestUnsupportedWritesNothing(void) {
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2];
  memset(&result, 0, sizeof(result));
  memset(records, 0x5a, sizeof(records));
  CHECK(AdmissionMpoBuildCommit(&result, records) == 0u);
  CHECK(records[0].Flags == 0x5a5a5a5au);
}


/* S3-A (EXP2374: a Set3 refusal of a Check3-accepted configuration freezes
 * the desktop): Check3 sees no placement, Set3 does.  For every plane set the
 * Check3 view (PlacementKnown 0) supports, every placement VidMm can produce
 * for a segment-2-only, 64 KiB-aligned allocation (inside the pool, the two
 * allocations disjoint) must be supported on the Set3 view too, its commit
 * must build two records (D2) the broker reference accepts, and the
 * LayerIndex 0 plane must build a removal record (G1) the broker accepts. */
static unsigned long long Align64K(unsigned long long Bytes) {
  return (Bytes + 0xffffULL) & ~0xffffULL;
}

static void TestCheck3ImpliesSet3AnyLegalPlacement(void) {
  ADMISSION_MPO_PLANE_INPUT p[2], set3[2];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2], removal;
  unsigned int iteration, m, swap, app, k, accepted = 0u, placed = 0u;
  unsigned long long a, b, slots, first;

  for (iteration = 0u; iteration < 200000u; ++iteration) {
    swap = Next(2u);
    p[swap] = Desktop();
    p[swap ^ 1u] = Application();
    app = swap ^ 1u;
    for (m = Next(4u); m != 0u; --m) {
      unsigned int which = Next(4u) == 0u ? swap : app;
      Mutate(&p[which], &p[which ^ 1u]);
    }
    p[0].PlacementKnown = 0u; p[1].PlacementKnown = 0u;
    p[0].PoolOffset = 0ULL; p[1].PoolOffset = 0ULL;
    memset(&input, 0, sizeof(input));
    input.PanelWidth = REF_PANEL_W; input.PanelHeight = REF_PANEL_H;
    input.PlaneCount = 2u; input.Planes = p; input.PoolBytes = REF_POOL;
    if (AdmissionMpoEvaluate(&input, &result) != AdmissionMpoSupported)
      continue;
    ++accepted;
    a = Align64K(p[0].SurfaceBytes); b = Align64K(p[1].SurfaceBytes);
    if (a == 0ULL || b == 0ULL || a + b > REF_POOL)
      continue; /* no segment-2 placement exists: the allocation cannot be resident */
    for (k = 0u; k < 4u; ++k) {
      set3[0] = p[0]; set3[1] = p[1];
      set3[0].PlacementKnown = 1u; set3[1].PlacementKnown = 1u;
      slots = (REF_POOL - a - b) / 0x10000ULL;
      first = (slots != 0ULL ? (unsigned long long)Next(1u << 16) % (slots + 1ULL) : 0ULL) * 0x10000ULL;
      if (k & 1u) { /* plane 0 first, plane 1 anywhere after it */
        set3[0].PoolOffset = first;
        set3[1].PoolOffset = REF_POOL - b - (k & 2u ? 0ULL : ((REF_POOL - b - first - a) / 0x10000ULL / 2ULL) * 0x10000ULL);
      } else {             /* plane 1 first */
        set3[1].PoolOffset = first;
        set3[0].PoolOffset = REF_POOL - a - (k & 2u ? 0ULL : ((REF_POOL - a - first - b) / 0x10000ULL / 2ULL) * 0x10000ULL);
      }
      CHECK(set3[0].PoolOffset % 0x10000ULL == 0ULL && set3[1].PoolOffset % 0x10000ULL == 0ULL);
      CHECK(set3[0].PoolOffset + a <= set3[1].PoolOffset || set3[1].PoolOffset + b <= set3[0].PoolOffset);
      input.Planes = set3;
      ++placed;
      if (AdmissionMpoEvaluate(&input, &result) != AdmissionMpoSupported) {
        fprintf(stderr, "s3a %u/%u: Check3 supported, Set3 view refuses %u (offs 0x%llx 0x%llx bytes 0x%llx 0x%llx)\n",
                iteration, k, result.Reason, set3[0].PoolOffset, set3[1].PoolOffset,
                set3[0].SurfaceBytes, set3[1].SurfaceBytes);
        ++Failures;
        return;
      }
      CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);       /* D2 */
      CHECK(RefCommit(records, 2u, REF_POOL) == 0u);
      CHECK(AdmissionMpoBuildRemoval(&set3[swap], &removal) == 1u); /* G1 */
      CHECK(RefCommit(&removal, 1u, REF_POOL) == 0u);
    }
  }
  printf("s3a: %u Check3-supported, %u placements checked\n", accepted, placed);
  CHECK(accepted > 1000u && placed > 1000u);
}

int main(void) {
  TestUnsupportedWritesNothing();
  TestRemovalRecord();
  TestSweepTableImpliesBroker();
  TestCheck3ImpliesSet3AnyLegalPlacement();
  if (Failures != 0u) {
    fprintf(stderr, "mpo_broker_reference_test: %u failure(s)\n", Failures);
    return EXIT_FAILURE;
  }
  printf("mpo_broker_reference_test: OK\n");
  return EXIT_SUCCESS;
}
