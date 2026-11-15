#include "apple_agx_post_display_route.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

int main(void) {
  const APPLE_AGX_PANEL_LIMITS limits = {2560u, 1600u};
  const unsigned int X = APPLE_AGX_DDIFMT_X8R8G8B8;
  CHECK(AppleAgxPostDisplayRoute(0, 0, 0, 0, 0, 0, &limits) ==
        AppleAgxPostDisplayAcquireFailed);
  CHECK(AppleAgxPostDisplayRoute(1, 0, 0, 0, 0, 0, &limits) ==
        AppleAgxPostDisplayOwnScanout);
  CHECK(AppleAgxPostDisplayRoute(1, 2560, 1600, 10240, X, 0x1000, &limits) ==
        AppleAgxPostDisplayAdoptPost);
  CHECK(AppleAgxPostDisplayRoute(1, 2560, 1600, 10240, X, 0, &limits) ==
        AppleAgxPostDisplayGeometryRejected);
  CHECK(AppleAgxPostDisplayRoute(1, 2560, 0, 10240, X, 0x1000, &limits) ==
        AppleAgxPostDisplayGeometryRejected);
  /* A smaller valid POST surface is no longer rejected by a literal compare. */
  CHECK(AppleAgxPostDisplayRoute(1, 1920, 1080, 7680, X, 0x1000, &limits) ==
        AppleAgxPostDisplayAdoptPost);
  CHECK(AppleAgxPostDisplayRoute(1, 2560, 1664, 10240, X, 0x1000, &limits) ==
        AppleAgxPostDisplayGeometryRejected);
  return 0;
}
