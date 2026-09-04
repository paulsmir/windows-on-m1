#include "apple_agx_runtime_handoff.h"
#include <assert.h>
#include <string.h>
int main(void) {
  APPLE_AGX_RUNTIME_HANDOFF h;
  memset(&h, 0, sizeof(h));
  h.Magic = APPLE_AGX_RUNTIME_HANDOFF_MAGIC;
  h.Version = APPLE_AGX_RUNTIME_HANDOFF_VERSION;
  h.Size = sizeof(h); h.State = APPLE_AGX_RUNTIME_HANDOFF_READY;
  h.Generation = 1; h.HandoffPhysicalBase = 0x9fff70000ULL;
  h.HandoffBytes = 0x4000u; h.Ttbr0 = 0x1001ULL; h.Ttbr1 = 0x2001ULL;
  assert(AppleAgxRuntimeHandoffValidate(&h, 0x9fff70000ULL, 0x4000u));
  h.Generation = 0;
  assert(!AppleAgxRuntimeHandoffValidate(&h, 0x9fff70000ULL, 0x4000u));
  return 0;
}
