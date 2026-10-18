#include "apple_agx_post_display_route.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

int main(void) {
  CHECK(AppleAgxPostDisplayRoute(0, 0, 0, 0, 0) ==
        AppleAgxPostDisplayAcquireFailed);
  CHECK(AppleAgxPostDisplayRoute(1, 0, 0, 0, 0) ==
        AppleAgxPostDisplayOwnScanout);
  CHECK(AppleAgxPostDisplayRoute(1, 2560, 1600, 10240, 0x1000) ==
        AppleAgxPostDisplayAdoptPost);
  CHECK(AppleAgxPostDisplayRoute(1, 2560, 1600, 10240, 0) ==
        AppleAgxPostDisplayGeometryRejected);
  CHECK(AppleAgxPostDisplayRoute(1, 2560, 0, 10240, 0x1000) ==
        AppleAgxPostDisplayGeometryRejected);
  CHECK(AppleAgxPostDisplayRoute(1, 1920, 1080, 7680, 0x1000) ==
        AppleAgxPostDisplayGeometryRejected);
  return 0;
}
