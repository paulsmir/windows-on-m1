#include "apple_agx_post_display_release.h"

#include <assert.h>
#include <string.h>

static const APPLE_AGX_PANEL_LIMITS j313 = {2560u, 1600u};
static const APPLE_AGX_PANEL_LIMITS m2 = {2560u, 1664u};

/* T3. WHAT REAL BUG: a panel other than J313 (M2 2560x1664) rejected as
 * GeometryRejected; Pitch < W*4, PA 0, unaligned PA or a non-32bpp format
 * accepted. */
static void test_surface_valid(void) {
  const APPLE_AGX_SCANOUT_U32 X = APPLE_AGX_DDIFMT_X8R8G8B8, A = APPLE_AGX_DDIFMT_A8R8G8B8;
  assert(AppleAgxPostDisplaySurfaceValid(2560, 1600, 10240, X, 0x85F000000ull, &j313));
  assert(AppleAgxPostDisplaySurfaceValid(2560, 1600, 10240, A, 0x85F000000ull, &j313));
  assert(AppleAgxPostDisplaySurfaceValid(2560, 1664, 10240, X, 0x85F000000ull, &m2));
  assert(AppleAgxPostDisplaySurfaceValid(1920, 1080, 7680, X, 0x85F000000ull, &j313));
  assert(AppleAgxPostDisplaySurfaceValid(2560, 1600, 10304, X, 0x85F000000ull, &j313)); /* padded pitch */
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 1664, 10240, X, 0x85F000000ull, &j313)); /* above limits */
  assert(!AppleAgxPostDisplaySurfaceValid(0, 1600, 10240, X, 0x85F000000ull, &j313));
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 0, 10240, X, 0x85F000000ull, &j313));
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 1600, 10236, X, 0x85F000000ull, &j313));
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 1600, 10240, X, 0, &j313));
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 1600, 10240, X, 0x85F000800ull, &j313));
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 1600, 10240, 23u /* R5G6B5 */, 0x85F000000ull, &j313));
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 1600, 0x80000000u, X, 0x85F000000ull, &j313)); /* Pitch*H > 32 bits */
  assert(!AppleAgxPostDisplaySurfaceValid(2560, 1600, 10240, X, 0x85F000000ull, 0));
  assert(!AppleAgxPostDisplaySurfaceValid(0x40000001u, 1, 0xFFFFFFFFu, X, 0x1000ull, &(APPLE_AGX_PANEL_LIMITS){0xFFFFFFFFu, 0xFFFFFFFFu})); /* W*4 overflow */
}

/* T4. WHAT REAL BUG: TargetId 0xFFFFFFFF (from the acquire) returned, AcpiId
 * not matching the child, an unknown TargetIn rejected instead of mapped to the
 * active internal panel, NOT_SUPPORTED used for a missing surface. */
static void test_build_release_info(void) {
  const APPLE_AGX_RELEASE_CHILD panel[1] = {{0u, 0u, 1, 1}};
  const APPLE_AGX_RELEASE_CHILD two[2] = {{5u, 9u, 0, 1}, {0u, 0u, 1, 1}};
  const APPLE_AGX_RELEASE_CHILD dark[1] = {{0u, 0u, 1, 0}};
  APPLE_AGX_RELEASE_INFO acq = {2560u, 1600u, 10240u, APPLE_AGX_DDIFMT_X8R8G8B8,
                                0x85F000000ull, 0xFFFFFFFFu, 0xFFFFFFFFu};
  APPLE_AGX_RELEASE_INFO out;
  APPLE_AGX_SCANOUT_U32 mapped = 77u;
  memset(&out, 0xAB, sizeof(out));
  assert(AppleAgxBuildReleaseInfo(&acq, panel, 1, 0u, &j313, &out, &mapped) == AppleAgxReleaseOk);
  assert(out.Width == 2560u && out.Height == 1600u && out.Pitch == 10240u);
  assert(out.ColorFormat == APPLE_AGX_DDIFMT_X8R8G8B8 && out.PhysicalAddress == 0x85F000000ull);
  assert(out.TargetId == 0u && out.AcpiId == 0u && mapped == 0u);
  /* Any TargetIn (incl. D3DDDI_ID_UNINITIALIZED) maps to the active internal panel. */
  assert(AppleAgxBuildReleaseInfo(&acq, panel, 1, 0xFFFFFFFFu, &j313, &out, &mapped) == AppleAgxReleaseOk);
  assert(out.TargetId == 0u && mapped == 0u);
  /* An active TargetIn is kept; AcpiId follows that child. */
  assert(AppleAgxBuildReleaseInfo(&acq, two, 2, 5u, &j313, &out, &mapped) == AppleAgxReleaseOk);
  assert(out.TargetId == 5u && out.AcpiId == 9u && mapped == 5u);
  assert(AppleAgxBuildReleaseInfo(&acq, two, 2, 3u, &j313, &out, &mapped) == AppleAgxReleaseOk);
  assert(out.TargetId == 0u && out.AcpiId == 0u);
  /* No connected display: the documented NOT_SUPPORTED case. */
  assert(AppleAgxBuildReleaseInfo(&acq, dark, 1, 0u, &j313, &out, &mapped) == AppleAgxReleaseNoDisplay);
  assert(AppleAgxBuildReleaseInfo(&acq, panel, 0, 0u, &j313, &out, &mapped) == AppleAgxReleaseNoDisplay);
  /* Own-scanout route (Width 0) or an invalid surface: no usable surface. */
  acq.Width = 0u;
  assert(AppleAgxBuildReleaseInfo(&acq, panel, 1, 0u, &j313, &out, &mapped) == AppleAgxReleaseNoSurface);
  acq.Width = 2560u; acq.ColorFormat = 23u;
  assert(AppleAgxBuildReleaseInfo(&acq, panel, 1, 0u, &j313, &out, &mapped) == AppleAgxReleaseNoSurface);
  acq.ColorFormat = APPLE_AGX_DDIFMT_A8R8G8B8;
  assert(AppleAgxBuildReleaseInfo(&acq, panel, 1, 0u, &j313, &out, &mapped) == AppleAgxReleaseOk);
  assert(out.ColorFormat == APPLE_AGX_DDIFMT_A8R8G8B8);
  assert(AppleAgxBuildReleaseInfo(0, panel, 1, 0u, &j313, &out, &mapped) == AppleAgxReleaseInvalid);
  assert(AppleAgxBuildReleaseInfo(&acq, panel, 1, 0u, &j313, 0, &mapped) == AppleAgxReleaseInvalid);
}

int main(void) {
  test_surface_valid();
  test_build_release_info();
  return 0;
}
