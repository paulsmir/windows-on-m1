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
  p.StretchQuality = 2u; /* HIGH; examined only when scaled */
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

/* STRETCH-1: a destination size around the 2x limits, per axis. */
static int ScaleExtent(int Source) {
  switch (Next(9u)) {
  case 0: return Source * 2;
  case 1: return Source * 2 + 1;
  case 2: return Source / 2;
  case 3: return Source / 2 - 1;
  case 4: return (Source * 3) / 2;
  case 5: return (Source * 2) / 3;
  case 6: return Source * 3;
  case 7: return Source + 1;
  default: return Source;
  }
}

static void Mutate(ADMISSION_MPO_PLANE_INPUT *P, ADMISSION_MPO_PLANE_INPUT *O) {
  switch (Next(23u)) {
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
  case 19: P->StretchQuality = Next(4u); break;
  case 20: case 21: { /* scale the destination about its origin */
    int w = ScaleExtent(P->Source.Right - P->Source.Left);
    int h = Next(2u) != 0u ? ScaleExtent(P->Source.Bottom - P->Source.Top)
                           : P->Source.Bottom - P->Source.Top;
    P->Destination.Right = P->Destination.Left + w;
    P->Destination.Bottom = P->Destination.Top + h;
    P->Clip = P->Destination;
    break;
  }
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
  unsigned int scaledSupported = 0u;
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
    memset(&input, 0, sizeof(input));
    input.ScalingSupported = Next(4u) != 0u ? 1u : 0u;
    input.ScalerFilter = Next(3u);
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
    if (p[app].Source.Right - p[app].Source.Left !=
            p[app].Destination.Right - p[app].Destination.Left ||
        p[app].Source.Bottom - p[app].Source.Top !=
            p[app].Destination.Bottom - p[app].Destination.Top) {
      ++scaledSupported;
      CHECK(input.ScalingSupported != 0u);
    }
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
  printf("sweep: %u supported of 400000 (%u scaled)\n", supported,
         scaledSupported);
  CHECK(supported > 1000u);
  CHECK(scaledSupported > 1000u);
  /* The sweep must reach every record/pair refusal the table can meet
   * (21 = beyond 2x, reachable since STRETCH-1). */
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


/* STRETCH-1: AdmissionMpoEvaluateOne's record path implies the broker
 * accepts the one-record commit AdmissionMpoBuildRemoval writes for it. */
static void TestSweepOnePlaneRecord(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER record;
  unsigned int iteration, m, code, records = 0u, scaled = 0u;

  for (iteration = 0u; iteration < 200000u; ++iteration) {
    p[0] = Application();
    p[1] = Desktop();
    p[0].LayerIndex = 0u;
    p[0].BlendAlpha = Next(3u);
    p[0].StretchQuality = 2u;
    if (Next(2u) != 0u) { /* a full-screen game */
      p[0].Destination = Rect(0, 0, 2560, 1600);
      p[0].Clip = p[0].Destination;
    }
    for (m = Next(4u); m != 0u; --m)
      Mutate(&p[0], &p[1]);
    memset(&input, 0, sizeof(input));
    input.PanelWidth = REF_PANEL_W;
    input.PanelHeight = REF_PANEL_H;
    input.PanelStride = REF_PANEL_W * 4u;
    input.PanelModeSupported = 1u;
    input.PlaneCount = 1u;
    input.Planes = p;
    input.PoolBytes = REF_POOL;
    input.ScalingSupported = Next(4u) != 0u ? 1u : 0u;
    input.ScalerFilter = Next(3u);
    if (AdmissionMpoEvaluateOne(&input, &result) != 0u ||
        result.OnePlaneRecord == 0u)
      continue;
    ++records;
    if (p[0].Source.Right - p[0].Source.Left !=
            p[0].Destination.Right - p[0].Destination.Left ||
        p[0].Source.Bottom - p[0].Source.Top !=
            p[0].Destination.Bottom - p[0].Destination.Top)
      ++scaled;
    memset(&record, 0, sizeof(record));
    if (AdmissionMpoBuildRemoval(&p[0], &record) != 1u) {
      fprintf(stderr, "one-plane sweep %u: supported but no record\n",
              iteration);
      ++Failures;
      return;
    }
    code = RefCommit(&record, 1u, REF_POOL);
    if (code != 0u) {
      fprintf(stderr, "one-plane sweep %u: broker reference refuses %u\n",
              iteration, code);
      ++Failures;
      return;
    }
  }
  printf("one-plane sweep: %u records (%u scaled)\n", records, scaled);
  CHECK(records > 1000u && scaled > 1000u);
}

/* STRETCH-1: the records of a scaled pair carry source and destination
 * separately (V3 layer record SrcSize / DstSize). */
static void TestScaledRecords(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2];
  p[0] = Desktop();
  p[1] = Application(); /* 1280x720 source */
  p[1].Destination = Rect(0, 0, 2560, 1440);
  p[1].Clip = p[1].Destination;
  p[1].StretchQuality = 2u;
  memset(&input, 0, sizeof(input));
  input.PanelWidth = REF_PANEL_W;
  input.PanelHeight = REF_PANEL_H;
  input.PlaneCount = 2u;
  input.Planes = p;
  input.PoolBytes = REF_POOL;
  input.ScalingSupported = 1u;
  CHECK(AdmissionMpoEvaluate(&input, &result) == 0u);
  CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
  CHECK(records[0].SrcOrigin == 0u &&
        records[0].SrcSize == APPLE_AGX_SCANOUT_PACK(1280u, 720u) &&
        records[0].DstOrigin == 0u &&
        records[0].DstSize == APPLE_AGX_SCANOUT_PACK(2560u, 1440u) &&
        records[0].Width == 1280u && records[0].Height == 720u &&
        records[0].Stride == 5120u);
  CHECK(records[1].SrcSize == APPLE_AGX_SCANOUT_PACK(2560u, 1600u) &&
        records[1].DstSize == APPLE_AGX_SCANOUT_PACK(2560u, 1600u));
  CHECK(RefCommit(records, 2u, REF_POOL) == 0u);
  /* A cropped, offset source keeps its origin. */
  p[1].Source = Rect(64, 32, 704, 512); /* 640x480 */
  p[1].Destination = Rect(10, 20, 970, 740); /* 960x720 = 1.5x */
  p[1].Clip = p[1].Destination;
  CHECK(AdmissionMpoEvaluate(&input, &result) == 0u);
  CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
  CHECK(records[0].SrcOrigin == APPLE_AGX_SCANOUT_PACK(64u, 32u) &&
        records[0].SrcSize == APPLE_AGX_SCANOUT_PACK(640u, 480u) &&
        records[0].DstOrigin == APPLE_AGX_SCANOUT_PACK(10u, 20u) &&
        records[0].DstSize == APPLE_AGX_SCANOUT_PACK(960u, 720u));
  CHECK(RefCommit(records, 2u, REF_POOL) == 0u);
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
    input.Path = ADMISSION_MPO_PATH_CHECK3;
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
      input.Path = ADMISSION_MPO_PATH_SET3; /* STRETCH-1b: the Set3 rule set */
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


/* FS-1: records for every form transition DWM-only -> app-only ->
 * two-plane -> app-only -> DWM-only, each accepted by the broker reference;
 * the lone record is slot 0, opaque, Src/Dst as given (scaled 2x too). */
static unsigned int EvalLone(ADMISSION_MPO_PLANE_INPUT *P, ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT input;
  memset(&input, 0, sizeof(input));
  input.PanelWidth = REF_PANEL_W; input.PanelHeight = REF_PANEL_H;
  input.PanelStride = REF_PANEL_W * 4u; input.PanelModeSupported = 1u;
  input.PlaneCount = 1u; input.Planes = P; input.PoolBytes = REF_POOL;
  input.ScalingSupported = 1u; input.Path = ADMISSION_MPO_PATH_SET3;
  return AdmissionMpoEvaluateOne(&input, Result);
}

static void TestLoneTransitions(void) {
  ADMISSION_MPO_PLANE_INPUT desk = Desktop(), app = Application(), pair[2];
  ADMISSION_MPO_INPUT input;
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2], one;
  unsigned int step;
  /* The full-screen game: LayerIndex 1, opaque, full panel, 1:1 and 2x. */
  app.Source = Rect(0, 0, 2560, 1600); app.Destination = app.Source; app.Clip = app.Source;
  app.SurfaceWidth = 2560u; app.SurfaceHeight = 1600u; app.SurfaceStride = 10240u;
  app.SurfaceBytes = 10240ULL * 1600u; app.PoolOffset = 0x2000000ULL;
  for (step = 0u; step < 5u; ++step) {
    switch (step) {
    case 0: case 4: /* DWM-only: the desktop as one record */
      CHECK(AdmissionMpoBuildRemoval(&desk, &one) == 1u);
      CHECK(one.SurfaceOffset == desk.PoolOffset && RefCommit(&one, 1u, REF_POOL) == 0u);
      break;
    case 1: case 3: /* app-only */
      CHECK(EvalLone(&app, &result) == AdmissionMpoSupported && result.OnePlaneRecord == 1u);
      CHECK(AdmissionMpoBuildLone(&app, &one) == 1u);
      CHECK(one.SurfaceOffset == app.PoolOffset && one.Flags == 0u &&
            one.DstSize == APPLE_AGX_SCANOUT_PACK(2560u, 1600u));
      CHECK(RefCommit(&one, 1u, REF_POOL) == 0u);
      break;
    case 2: /* two-plane: the game under DWM's full-panel top plane */
      pair[0] = desk; pair[1] = app;
      memset(&input, 0, sizeof(input));
      input.PanelWidth = REF_PANEL_W; input.PanelHeight = REF_PANEL_H;
      input.PlaneCount = 2u; input.Planes = pair; input.PoolBytes = REF_POOL;
      input.Path = ADMISSION_MPO_PATH_SET3;
      CHECK(AdmissionMpoEvaluate(&input, &result) == AdmissionMpoSupported);
      CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
      CHECK(records[0].SurfaceOffset == app.PoolOffset && RefCommit(records, 2u, REF_POOL) == 0u);
      break;
    }
  }
  /* Scaled 2x lone plane (STRETCHFS). */
  app = Application();
  app.Source = Rect(0, 0, 1280, 800); app.Destination = Rect(0, 0, 2560, 1600); app.Clip = app.Destination;
  app.SurfaceWidth = 1280u; app.SurfaceHeight = 800u; app.SurfaceStride = 5120u;
  app.SurfaceBytes = 5120ULL * 800u; app.StretchQuality = 0u; /* Set3 carries 0 (EXP2375) */
  CHECK(EvalLone(&app, &result) == AdmissionMpoSupported);
  CHECK(AdmissionMpoBuildLone(&app, &one) == 1u);
  CHECK(one.SrcSize == APPLE_AGX_SCANOUT_PACK(1280u, 800u) &&
        one.DstSize == APPLE_AGX_SCANOUT_PACK(2560u, 1600u) && RefCommit(&one, 1u, REF_POOL) == 0u);
  /* BuildLone takes only LayerIndex 1; BuildRemoval only LayerIndex 0. */
  CHECK(AdmissionMpoBuildLone(&desk, &one) == 0u);
  CHECK(AdmissionMpoBuildRemoval(&app, &one) == 0u);
}

/* ---- CUR-1 v2: scratch placement, records, fill, double buffering ---- */
static void ExpectPlace(int L, int T, int W, int H, int LayerX, int LayerY,
                        unsigned int OffX, unsigned int OffY, int Line) {
  ADMISSION_MPO_RECT clip = Rect(L, T, L + W, T + H), layer;
  unsigned int ox = 0u, oy = 0u;
  if (AdmissionMpoScratchPlace(&clip, REF_PANEL_W, REF_PANEL_H, &layer, &ox, &oy) != 1u ||
      layer.Left != LayerX || layer.Top != LayerY || layer.Right != LayerX + 64 ||
      layer.Bottom != LayerY + 64 || ox != OffX || oy != OffY ||
      ox + (unsigned int)W > 64u || oy + (unsigned int)H > 64u ||
      layer.Right > (int)REF_PANEL_W || layer.Bottom > (int)REF_PANEL_H) {
    fprintf(stderr, "line %d: scratch place (%d,%d %dx%d) -> (%d,%d) off %u,%u\n", Line, L, T, W, H,
            layer.Left, layer.Top, ox, oy);
    ++Failures;
  }
}
#define PLACE(L, T, W, H, X, Y, OX, OY) ExpectPlace(L, T, W, H, X, Y, OX, OY, __LINE__)

static void TestScratchPlacement(void) {
  ADMISSION_MPO_RECT layer, clip;
  unsigned int ox, oy, w, h;
  static const unsigned int sizes[][2] = {{1, 1}, {24, 27}, {32, 30}, {40, 39}, {64, 64}};
  PLACE(1280, 800, 1, 1, 1280, 800, 0u, 0u);       /* hidden pointer, panel centre */
  PLACE(0, 0, 1, 1, 0, 0, 0u, 0u);                 /* top-left corner */
  PLACE(2559, 0, 1, 1, 2496, 0, 63u, 0u);          /* top-right corner */
  PLACE(0, 1599, 1, 1, 0, 1536, 0u, 63u);          /* bottom-left corner */
  PLACE(2559, 1599, 1, 1, 2496, 1536, 63u, 63u);   /* bottom-right corner */
  PLACE(1000, 0, 24, 27, 1000, 0, 0u, 0u);         /* top edge */
  PLACE(0, 700, 24, 27, 0, 700, 0u, 0u);           /* left edge */
  PLACE(2536, 700, 24, 27, 2496, 700, 40u, 0u);    /* right edge */
  PLACE(1000, 1573, 24, 27, 1000, 1536, 0u, 37u);  /* bottom edge */
  PLACE(2496, 1536, 64, 64, 2496, 1536, 0u, 0u);   /* full scratch at the corner */
  /* Every size at every corner keeps the block inside the 64x64 layer. */
  for (w = 0u; w < sizeof(sizes) / sizeof(sizes[0]); ++w)
    for (h = 0u; h < 4u; ++h) {
      int x = (h & 1u) ? (int)REF_PANEL_W - (int)sizes[w][0] : 0;
      int y = (h & 2u) ? (int)REF_PANEL_H - (int)sizes[w][1] : 0;
      clip = Rect(x, y, x + (int)sizes[w][0], y + (int)sizes[w][1]);
      CHECK(AdmissionMpoScratchPlace(&clip, REF_PANEL_W, REF_PANEL_H, &layer, &ox, &oy) == 1u);
      CHECK(ox + sizes[w][0] <= 64u && oy + sizes[w][1] <= 64u);
      CHECK(layer.Left + (int)ox == clip.Left && layer.Top + (int)oy == clip.Top);
    }
  /* Refused: larger than 64 on an axis, empty, outside the panel. */
  clip = Rect(0, 0, 65, 40);
  CHECK(AdmissionMpoScratchPlace(&clip, REF_PANEL_W, REF_PANEL_H, &layer, &ox, &oy) == 0u);
  clip = Rect(10, 10, 10, 11);
  CHECK(AdmissionMpoScratchPlace(&clip, REF_PANEL_W, REF_PANEL_H, &layer, &ox, &oy) == 0u);
  clip = Rect(2550, 10, 2561, 20);
  CHECK(AdmissionMpoScratchPlace(&clip, REF_PANEL_W, REF_PANEL_H, &layer, &ox, &oy) == 0u);
  clip = Rect(-1, 10, 5, 20);
  CHECK(AdmissionMpoScratchPlace(&clip, REF_PANEL_W, REF_PANEL_H, &layer, &ox, &oy) == 0u);
}

static unsigned int EvalClipped(ADMISSION_MPO_PLANE_INPUT *P, ADMISSION_MPO_RECT Clip,
                                ADMISSION_MPO_RESULT *Result) {
  ADMISSION_MPO_INPUT input;
  P[0] = Desktop();
  P[1] = Application();
  P[0].Clip = Clip;
  memset(&input, 0, sizeof(input));
  input.PanelWidth = REF_PANEL_W;
  input.PanelHeight = REF_PANEL_H;
  input.PlaneCount = 2u;
  input.Planes = P;
  input.PoolBytes = REF_POOL;
  input.ScratchAvailable = 1u;
  return AdmissionMpoEvaluate(&input, Result);
}

static void TestClippedRecords(void) {
  ADMISSION_MPO_PLANE_INPUT p[2];
  ADMISSION_MPO_RESULT result;
  APPLE_AGX_SCANOUT_LAYER records[2];
  const unsigned long long scratch = REF_POOL - 0x20000ULL; /* top 128 KiB, 64 KiB aligned */
  /* Scratch: 24x27 at the centre. */
  CHECK(EvalClipped(p, Rect(1280, 800, 1304, 827), &result) == AdmissionMpoSupported);
  CHECK(result.Slots[1].ClipMode == AdmissionMpoClipScratch);
  CHECK(AdmissionMpoBuildCommit(&result, records) == 0u); /* no scratch placement yet */
  result.ScratchOffset = scratch;
  CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
  CHECK(records[1].SurfaceOffset == scratch && records[1].SurfaceSize == 0x4000ULL &&
        records[1].Width == 64u && records[1].Height == 64u && records[1].Stride == 256u &&
        records[1].SrcOrigin == 0u && records[1].SrcSize == APPLE_AGX_SCANOUT_PACK(64u, 64u) &&
        records[1].DstOrigin == APPLE_AGX_SCANOUT_PACK(1280u, 800u) &&
        records[1].DstSize == APPLE_AGX_SCANOUT_PACK(64u, 64u) &&
        records[1].Flags == APPLE_AGX_SCANOUT_LAYER_FLAG_PREMULTIPLIED);
  CHECK(records[0].SurfaceOffset == p[1].PoolOffset); /* the application, unchanged */
  CHECK(RefCommit(records, 2u, REF_POOL) == 0u);
  /* Scratch at the bottom-right corner: the layer pulled inside. */
  CHECK(EvalClipped(p, Rect(2559, 1599, 2560, 1600), &result) == AdmissionMpoSupported);
  result.ScratchOffset = scratch + 0x10000ULL;
  CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
  CHECK(records[1].DstOrigin == APPLE_AGX_SCANOUT_PACK(2496u, 1536u));
  CHECK(RefCommit(records, 2u, REF_POOL) == 0u);
  /* Crop: 65x40, Src = Dst = the visible part of the desktop surface. */
  CHECK(EvalClipped(p, Rect(100, 200, 165, 240), &result) == AdmissionMpoSupported);
  CHECK(result.Slots[1].ClipMode == AdmissionMpoClipCrop);
  CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
  CHECK(records[1].SurfaceOffset == p[0].PoolOffset &&
        records[1].SrcOrigin == APPLE_AGX_SCANOUT_PACK(100u, 200u) &&
        records[1].SrcSize == APPLE_AGX_SCANOUT_PACK(65u, 40u) &&
        records[1].DstOrigin == APPLE_AGX_SCANOUT_PACK(100u, 200u) &&
        records[1].DstSize == APPLE_AGX_SCANOUT_PACK(65u, 40u));
  CHECK(RefCommit(records, 2u, REF_POOL) == 0u);
  /* Unclipped: exactly the previous full-panel record. */
  CHECK(EvalClipped(p, Rect(0, 0, 2560, 1600), &result) == AdmissionMpoSupported);
  CHECK(result.Slots[1].ClipMode == AdmissionMpoClipNone);
  CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
  CHECK(records[1].DstSize == APPLE_AGX_SCANOUT_PACK(2560u, 1600u));
}

static void TestScratchFillAndSelect(void) {
  unsigned char scratch[0x4000], source[3 * 4 * 2 + 64];
  ADMISSION_MPO_SCRATCH_STATE state;
  unsigned int i, x, y, write, index, bad = 0u;
  const unsigned int pitch = 3u * 4u + 8u; /* row padding is skipped */
  memset(scratch, 0xee, sizeof(scratch));
  for (i = 0u; i < sizeof(source); ++i) source[i] = (unsigned char)(i + 1u);
  AdmissionMpoScratchFill(scratch, source, pitch, 3u, 2u, 5u, 7u);
  for (y = 0u; y < 64u; ++y)
    for (x = 0u; x < 256u; ++x) {
      unsigned int px = x / 4u;
      unsigned char want = (y >= 7u && y < 9u && px >= 5u && px < 8u)
                               ? source[(y - 7u) * pitch + (px - 5u) * 4u + (x & 3u)] : 0u;
      if (scratch[y * 256u + x] != want) ++bad;
    }
  CHECK(bad == 0u);
  memset(source, 0, sizeof(source));
  CHECK(AdmissionMpoBlockIsZero(source, pitch, 3u, 2u) == 1u);
  source[pitch + 11u] = 1u; /* last byte of the block's second row */
  CHECK(AdmissionMpoBlockIsZero(source, pitch, 3u, 2u) == 0u);
  source[pitch + 11u] = 0u; source[12] = 9u; /* row padding is not the block */
  CHECK(AdmissionMpoBlockIsZero(source, pitch, 3u, 2u) == 1u);
  /* Double buffering and zero-skip. */
  AdmissionMpoScratchReset(&state);
  index = AdmissionMpoScratchSelect(&state, 1u, &write);
  CHECK(index == 0u && write == 1u);          /* nothing scanned yet: write 0 */
  AdmissionMpoScratchCommitted(&state, index, 1u);
  index = AdmissionMpoScratchSelect(&state, 1u, &write);
  CHECK(index == 0u && write == 0u);          /* zero over zero: keep, no write */
  index = AdmissionMpoScratchSelect(&state, 0u, &write);
  CHECK(index == 1u && write == 1u);          /* content: the free one */
  /* That commit answered Busy: nothing changes, the same free one again. */
  index = AdmissionMpoScratchSelect(&state, 0u, &write);
  CHECK(index == 1u && write == 1u);
  AdmissionMpoScratchCommitted(&state, index, 0u);
  index = AdmissionMpoScratchSelect(&state, 1u, &write);
  CHECK(index == 0u && write == 1u);          /* zero over content: clear the free one */
  AdmissionMpoScratchCommitted(&state, index, 1u);
  index = AdmissionMpoScratchSelect(&state, 0u, &write);
  CHECK(index == 1u && write == 1u);          /* never the scanned one */
}

int main(void) {
  TestUnsupportedWritesNothing();
  TestScaledRecords();
  TestSweepOnePlaneRecord();
  TestRemovalRecord();
  TestSweepTableImpliesBroker();
  TestCheck3ImpliesSet3AnyLegalPlacement();
  TestLoneTransitions();
  TestScratchPlacement();
  TestClippedRecords();
  TestScratchFillAndSelect();
  if (Failures != 0u) {
    fprintf(stderr, "mpo_broker_reference_test: %u failure(s)\n", Failures);
    return EXIT_FAILURE;
  }
  printf("mpo_broker_reference_test: OK\n");
  return EXIT_SUCCESS;
}
