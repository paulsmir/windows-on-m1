#include "render_allocation.h"
#include "mpo_contract.h"
#include "mpo_layers.h"

#include <stdio.h>
#include <string.h>

/* PITCH-1 (investigation/analysis/PITCH-1-aligned-display-pitch.md).
 *
 * WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 *  1. A scan-out-capable allocation (GPU-only 32-bpp GDI surface texture)
 *     whose row pitch the overlay check or the scanout broker refuses: a
 *     1682-wide window got pitch 6736 (16-byte rule), Check3 answered
 *     AdmissionMpoReasonStride and DWM composited it on the GPU instead.
 *  2. A pitch / size disagreement between the description that is created,
 *     the copy every opener validates, the Asahi linear import and the
 *     broker record (a sheared picture on hardware).
 *  3. A non-displayable allocation whose pitch or size changes (the Win32
 *     class-buffer transport requires Width == Pitch == Size byte buffers;
 *     CPU-visible GDI surfaces report their pitch to GDI).
 * The broker predicate below restates m1n1 hv_agx_scanout_broker.c
 * layer_validate (stride >= width * 4, stride % 64 == 0, size == stride *
 * height); the import predicate restates AgxWin32AsahiImportLinearColor32
 * and Asahi ail_initialize_linear (stride % 16 == 0, layer size rounded up
 * to the 128-byte cache line). */

#define PANEL_W 2560u
#define PANEL_H 1600u
#define POOL_BYTES 0x3b800000ULL
#define TOP_OFFSET 0x10000ULL
#define BOTTOM_OFFSET 0x1000000ULL
#define FORMAT_A8R8G8B8 21u
#define FORMAT_A8 28u
#define TYPE_STAGING_CPUVISIBLE 2u
#define TYPE_STAGING 3u
#define TYPE_WIN32_GPU_LOCAL 0x100u

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

/* The presentation description exactly as the UMD creates it
 * (umd.c AdmissionUmdDescribePrimary, wgl_kmt_create): the allocator's
 * description with the size padded to the 128-byte AIL cache line. */
static int CreatePresentation(unsigned int Width, unsigned int Height,
                              ADMISSION_ALLOCATION_DESCRIPTION *Description) {
  if (!AdmissionAllocationDescribe(Width, Height, 4u,
                                   ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE,
                                   FORMAT_A8R8G8B8, 0u, Description))
    return 0;
  Description->Size = (Description->Size + 127ULL) & ~127ULL;
  return 1;
}

/* AgxWin32AsahiImportLinearColor32 + ail_make_miptree (linear, 1 level). */
static int ImportAccepts(const ADMISSION_ALLOCATION_DESCRIPTION *D) {
  unsigned long long layer =
      ((unsigned long long)D->Pitch * D->Height + 127ULL) & ~127ULL;
  return D->Pitch >= (unsigned long long)D->Width * 4ULL &&
         (D->Pitch & 15u) == 0u && D->Size >= layer;
}

/* m1n1 hv_agx_scanout_broker.c layer_validate: stride and size rules. */
static int BrokerStrideAccepts(const APPLE_AGX_SCANOUT_LAYER *R) {
  return R->Width != 0u && R->Height != 0u && R->Width <= PANEL_W &&
         R->Height <= PANEL_H &&
         (unsigned long long)R->Stride >= 4ULL * R->Width &&
         (R->Stride & 63u) == 0u &&
         R->SurfaceSize == (unsigned long long)R->Stride * R->Height;
}

/* mpo_windows.c AdmissionMpoPlaneInput for a scan-out-capable allocation
 * in the scanout pool. */
static ADMISSION_MPO_PLANE_INPUT PlaneFrom(
    const ADMISSION_ALLOCATION_DESCRIPTION *D, unsigned int LayerIndex,
    unsigned long long Offset, ADMISSION_MPO_RECT Destination) {
  ADMISSION_MPO_PLANE_INPUT plane;
  memset(&plane, 0, sizeof(plane));
  plane.LayerIndex = LayerIndex;
  plane.Enabled = 1u;
  plane.FormatClass = AdmissionMpoFormatBgra8;
  plane.ColorSpaceClass = AdmissionMpoColorSpaceSdrSrgb;
  plane.BlendAlpha = LayerIndex == 0u ? 1u : 0u;
  plane.Source = Rect(0, 0, (int)D->Width, (int)D->Height);
  plane.Destination = Destination;
  plane.Clip = Destination;
  plane.Rotation = AdmissionMpoRotationIdentity;
  plane.SurfaceWidth = D->Width;
  plane.SurfaceHeight = D->Height;
  plane.SurfaceStride = D->Pitch;
  plane.DescriptionValid = AdmissionAllocationDescriptionValid(D) ? 1u : 0u;
  plane.SurfaceBytes = plane.DescriptionValid != 0u
                           ? (unsigned long long)D->Pitch * D->Height
                           : D->Size;
  plane.ScanoutCapable = 1u;
  plane.AllocationBytes = D->Size;
  plane.InScanoutPool = 1u;
  plane.PlacementKnown = 1u;
  plane.PlacementInPool = 1u;
  plane.PoolOffset = Offset;
  plane.SurfaceIdentity = Offset;
  return plane;
}

static void test_display_pitch_values(void) {
  static const unsigned int widths[][2] = {
      {640u, 2560u},  {1280u, 5120u}, {1682u, 6784u}, {2560u, 10240u},
      {1u, 64u},      {15u, 64u},     {16u, 64u},     {17u, 128u},
      {33u, 192u},    {1281u, 5184u}, {1919u, 7680u}, {8191u, 32768u},
      {8192u, 32768u}};
  unsigned int i, pitch = 0u;
  CHECK(ADMISSION_ALLOCATION_DISPLAY_PITCH_ALIGNMENT == 64u);
  /* Check3's stride rule and the allocator's display alignment are one
   * value (mpo_windows.c C_ASSERTs it in the kernel build). */
  CHECK(ADMISSION_MPO_STRIDE_ALIGNMENT ==
        ADMISSION_ALLOCATION_DISPLAY_PITCH_ALIGNMENT);
  for (i = 0u; i < sizeof(widths) / sizeof(widths[0]); ++i) {
    CHECK(AdmissionAllocationPitch(widths[i][0], 4u,
                                   ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE, 0u,
                                   &pitch));
    CHECK(pitch == widths[i][1]);
  }
  /* Every width the UMD admits (1..8192): the smallest 64-byte multiple
   * that holds the row. */
  for (i = 1u; i <= 8192u; ++i) {
    pitch = 0u;
    CHECK(AdmissionAllocationPitch(i, 4u,
                                   ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE, 0u,
                                   &pitch));
    CHECK(pitch % 64u == 0u && pitch >= i * 4u && pitch < i * 4u + 64u);
  }
  CHECK(!AdmissionAllocationPitch(0u, 4u,
                                  ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE, 0u,
                                  &pitch));
  CHECK(!AdmissionAllocationPitch(0xffffffffu, 4u,
                                  ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE, 0u,
                                  &pitch));
  CHECK(!AdmissionAllocationPitch(16u, 4u,
                                  ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE, 0u,
                                  0));
}

/* Create, every open (the copy the KMD returns as allocation private data,
 * validated by umd.c AdmissionUmdOpenResource, AgxD3d12DecodeShared and
 * wgl_redirect_open), the Asahi import and the size all agree. */
static void test_create_open_import_agree(void) {
  static const unsigned int heights[] = {1u, 2u, 3u, 480u, 721u, 840u,
                                         1600u};
  unsigned int w, h;
  for (w = 1u; w <= 2600u; ++w) {
    for (h = 0u; h < sizeof(heights) / sizeof(heights[0]); ++h) {
      ADMISSION_ALLOCATION_DESCRIPTION created, opened, again;
      CHECK(CreatePresentation(w, heights[h], &created));
      memcpy(&opened, &created, sizeof(opened));
      CHECK(AdmissionAllocationDescriptionValid(&opened));
      CHECK(AdmissionAllocationDescribe(
          opened.Width, opened.Height, opened.BytesPerPixel, opened.Type,
          opened.Format, opened.CpuVisible, &again));
      CHECK(again.Pitch == created.Pitch);
      CHECK(created.Size >= (unsigned long long)created.Pitch * heights[h]);
      CHECK(created.Size - (unsigned long long)created.Pitch * heights[h] <
            128ULL);
      CHECK(ImportAccepts(&opened));
    }
  }
}

/* An old 16-byte-aligned display-class description cannot pass as valid:
 * it would reach the overlay check with a stride the broker refuses. */
static void test_display_class_validity(void) {
  ADMISSION_ALLOCATION_DESCRIPTION d;
  CHECK(CreatePresentation(1682u, 840u, &d));
  CHECK(d.Pitch == 6784u && AdmissionAllocationDescriptionValid(&d));
  d.Pitch = 6736u; /* the pre-PITCH-1 pitch of a 1682-wide window */
  d.Size = 6736ULL * 840u;
  CHECK(!AdmissionAllocationDescriptionValid(&d));
  d.Pitch = 6784u + 16u;
  d.Size = (unsigned long long)d.Pitch * 840u;
  CHECK(!AdmissionAllocationDescriptionValid(&d));
  d.Pitch = 6784u + 64u; /* a larger 64-byte multiple stays admissible */
  d.Size = (unsigned long long)d.Pitch * 840u;
  CHECK(AdmissionAllocationDescriptionValid(&d));
}

/* Check3 / Set3 accept every window the allocator can produce as the
 * application plane under a full-panel desktop plane, and the records
 * they produce pass the broker's stride and size rules. */
static void test_overlay_accepts_every_allocator_width(void) {
  static const unsigned int heights[] = {32u, 33u, 721u, 840u, 1600u};
  ADMISSION_ALLOCATION_DESCRIPTION top;
  unsigned int w, h;
  CHECK(CreatePresentation(PANEL_W, PANEL_H, &top));
  for (w = ADMISSION_MPO_MIN_EXTENT; w <= PANEL_W; ++w) {
    for (h = 0u; h < sizeof(heights) / sizeof(heights[0]); ++h) {
      ADMISSION_ALLOCATION_DESCRIPTION window;
      ADMISSION_MPO_PLANE_INPUT planes[2];
      ADMISSION_MPO_INPUT input;
      ADMISSION_MPO_RESULT result;
      APPLE_AGX_SCANOUT_LAYER records[2];
      CHECK(CreatePresentation(w, heights[h], &window));
      planes[0] = PlaneFrom(&top, 0u, TOP_OFFSET,
                            Rect(0, 0, (int)PANEL_W, (int)PANEL_H));
      planes[1] = PlaneFrom(&window, 1u, BOTTOM_OFFSET,
                            Rect(0, 0, (int)w, (int)heights[h]));
      memset(&input, 0, sizeof(input));
      input.PanelWidth = PANEL_W;
      input.PanelHeight = PANEL_H;
      input.PlaneCount = 2u;
      input.Planes = planes;
      input.PoolBytes = POOL_BYTES;
      if (AdmissionMpoEvaluate(&input, &result) != AdmissionMpoSupported) {
        fprintf(stderr, "width %u height %u pitch %u: reason %u plane %u\n",
                w, heights[h], window.Pitch, result.Reason,
                result.FailingPlane);
        ++Failures;
        continue;
      }
      CHECK(AdmissionMpoBuildCommit(&result, records) == 2u);
      CHECK(BrokerStrideAccepts(&records[0]) &&
            BrokerStrideAccepts(&records[1]));
      CHECK(records[0].Stride == window.Pitch &&
            records[0].SurfaceSize ==
                (unsigned long long)window.Pitch * heights[h]);
    }
  }
}

/* Everything outside the display class keeps the 16-byte rule exactly. */
static void test_non_display_unchanged(void) {
  ADMISSION_ALLOCATION_DESCRIPTION d;
  unsigned int bytes;
  /* Win32 class buffers (umd_win32_screen.c): Width == Pitch == Size. */
  for (bytes = 0x4000u; bytes <= 0x40000u; bytes += 0x4000u) {
    CHECK(AdmissionAllocationDescribe(bytes, 1u, 1u, TYPE_STAGING_CPUVISIBLE,
                                      FORMAT_A8, 1u, &d));
    CHECK(d.Pitch == bytes && d.Size == bytes);
    CHECK(AdmissionAllocationDescribe(bytes, 1u, 1u, TYPE_WIN32_GPU_LOCAL,
                                      FORMAT_A8, 0u, &d));
    CHECK(d.Pitch == bytes && d.Size == bytes);
  }
  CHECK(AdmissionAllocationDescribe(1001u, 1u, 1u, TYPE_STAGING_CPUVISIBLE,
                                    FORMAT_A8, 1u, &d));
  CHECK(d.Pitch == 1008u);
  /* CPU-visible or staging 32-bpp surfaces (GDI shadow / staging): the
   * pitch GDI was given before. */
  CHECK(AdmissionAllocationDescribe(1682u, 840u, 4u, TYPE_STAGING_CPUVISIBLE,
                                    FORMAT_A8R8G8B8, 1u, &d));
  CHECK(d.Pitch == 6736u && d.Size == 6736ULL * 840u);
  CHECK(AdmissionAllocationDescribe(1682u, 840u, 4u, TYPE_STAGING,
                                    FORMAT_A8R8G8B8, 0u, &d));
  CHECK(d.Pitch == 6736u);
  CHECK(AdmissionAllocationDescribe(1682u, 840u, 4u,
                                    ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE,
                                    FORMAT_A8R8G8B8, 1u, &d));
  CHECK(d.Pitch == 6736u);
  /* A non-display description keeps the old validity rule too. */
  d.Pitch = 6752u;
  d.Size = 6752ULL * 840u;
  CHECK(AdmissionAllocationDescriptionValid(&d));
  CHECK(!AdmissionAllocationDisplayClass(TYPE_STAGING, 4u, 0u));
  CHECK(!AdmissionAllocationDisplayClass(
      ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE, 1u, 0u));
  CHECK(AdmissionAllocationDisplayClass(ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE,
                                        4u, 0u));
}

/* PITCH-2: create (umd.c AdmissionUmdCreateResourceWithMetadata,
 * wgl_kmt_create) and every open (AdmissionUmdOpenResource,
 * AgxD3d12DecodeShared, wgl_redirect_open) apply
 * AdmissionPresentationSizeCheck to the same description fields.
 * WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  A surface create accepts
 * that open refuses (the former 16 MiB open cap refused e.g. 513x8000 once
 * its pitch grew), or an open limit that admits a surface the render side
 * cannot adopt (larger than the reported General class maximum). */
static unsigned int OpenReason(const ADMISSION_ALLOCATION_DESCRIPTION *Created,
                               unsigned long long ClassMaximum) {
  ADMISSION_ALLOCATION_DESCRIPTION opened;
  memcpy(&opened, Created, sizeof(opened)); /* the KMD returns it unchanged */
  if (!AdmissionAllocationDescriptionValid(&opened))
    return 0xffffffffu;
  return AdmissionPresentationSizeCheck(opened.Width, opened.Height,
                                        opened.Size, ClassMaximum);
}

static void test_create_and_open_share_the_size_limit(void) {
  static const unsigned long long classes[] = {
      0x1000000ULL,  /* AdmissionGeneralMaximumBytes legacy (non-G3) build */
      0x3b800000ULL, /* a G3 pool (the MPO tests' registered pool) */
      ~0ULL};
  static const unsigned int edges[] = {1u,    2u,    15u,   16u,   17u,
                                       33u,   513u,  640u,  1281u, 1682u,
                                       2048u, 2049u, 2560u, 4095u, 4096u,
                                       8000u, 8191u, 8192u};
  const unsigned long long maximum = AdmissionPresentationMaximumBytes();
  unsigned int c, w, h;
  CHECK(maximum == 32768ULL * 8192ULL);
  for (c = 0u; c < sizeof(classes) / sizeof(classes[0]); ++c) {
    for (w = 1u; w <= ADMISSION_PRESENTATION_MAX_EXTENT; ++w) {
      for (h = 0u; h < sizeof(edges) / sizeof(edges[0]); ++h) {
        unsigned int dims[2][2];
        unsigned int k;
        dims[0][0] = w; dims[0][1] = edges[h];
        dims[1][0] = edges[h]; dims[1][1] = w;
        for (k = 0u; k < 2u; ++k) {
          ADMISSION_ALLOCATION_DESCRIPTION created;
          unsigned int createReason;
          CHECK(CreatePresentation(dims[k][0], dims[k][1], &created));
          createReason = AdmissionPresentationSizeCheck(
              created.Width, created.Height, created.Size, classes[c]);
          CHECK(OpenReason(&created, classes[c]) == createReason);
          /* Inside the allocator's own bound only the class limit refuses. */
          CHECK(createReason == (created.Size > classes[c]
                                     ? ADMISSION_PRESENTATION_SIZE_BYTES
                                     : ADMISSION_PRESENTATION_SIZE_OK));
        }
      }
    }
  }
  {
    ADMISSION_ALLOCATION_DESCRIPTION d;
    /* 513x8000: 2112 * 8000 > 16 MiB; opens wherever it can be created. */
    CHECK(CreatePresentation(513u, 8000u, &d));
    CHECK(AdmissionPresentationSizeCheck(d.Width, d.Height, d.Size, ~0ULL) ==
          ADMISSION_PRESENTATION_SIZE_OK);
    CHECK(OpenReason(&d, ~0ULL) == ADMISSION_PRESENTATION_SIZE_OK);
    CHECK(OpenReason(&d, 0x1000000ULL) == ADMISSION_PRESENTATION_SIZE_BYTES);
    /* 8192 x 8192 is exactly the maximum. */
    CHECK(CreatePresentation(8192u, 8192u, &d) && d.Size == maximum);
    CHECK(OpenReason(&d, ~0ULL) == ADMISSION_PRESENTATION_SIZE_OK);
    /* Above the extent: both refuse with the same reason. */
    CHECK(CreatePresentation(8193u, 16u, &d));
    CHECK(AdmissionPresentationSizeCheck(d.Width, d.Height, d.Size, ~0ULL) ==
          ADMISSION_PRESENTATION_SIZE_EXTENT);
    CHECK(OpenReason(&d, ~0ULL) == ADMISSION_PRESENTATION_SIZE_EXTENT);
    CHECK(CreatePresentation(16u, 8193u, &d));
    CHECK(OpenReason(&d, ~0ULL) == ADMISSION_PRESENTATION_SIZE_EXTENT);
    /* A larger shared pitch cannot carry Size past the maximum. */
    CHECK(CreatePresentation(8192u, 8192u, &d));
    d.Pitch += 64u;
    d.Size = (unsigned long long)d.Pitch * d.Height;
    CHECK(AdmissionAllocationDescriptionValid(&d));
    CHECK(OpenReason(&d, ~0ULL) == ADMISSION_PRESENTATION_SIZE_BYTES);
    /* Unknown class limit (0): everything refused, create and open alike. */
    CHECK(CreatePresentation(64u, 64u, &d));
    CHECK(OpenReason(&d, 0ULL) == ADMISSION_PRESENTATION_SIZE_BYTES);
    CHECK(AdmissionPresentationSizeCheck(0u, 1u, 64u, ~0ULL) ==
          ADMISSION_PRESENTATION_SIZE_EXTENT);
  }
}

int main(void) {
  test_display_pitch_values();
  test_create_open_import_agree();
  test_display_class_validity();
  test_overlay_accepts_every_allocator_width();
  test_non_display_unchanged();
  test_create_and_open_share_the_size_limit();
  if (Failures != 0u) {
    fprintf(stderr, "display_pitch_test: %u failure(s)\n", Failures);
    return 1;
  }
  printf("display_pitch_test: OK\n");
  return 0;
}
