#include "apple_agx_runtime_handoff.h"

unsigned char AppleAgxRuntimeHandoffValidate(
    const APPLE_AGX_RUNTIME_HANDOFF *h,
    APPLE_AGX_RUNTIME_HANDOFF_U64 base,
    APPLE_AGX_RUNTIME_HANDOFF_U32 bytes) {
  return (unsigned char)(h != 0 &&
      h->Magic == APPLE_AGX_RUNTIME_HANDOFF_MAGIC &&
      h->Version == APPLE_AGX_RUNTIME_HANDOFF_VERSION &&
      h->Size == sizeof(*h) && h->State == APPLE_AGX_RUNTIME_HANDOFF_READY &&
      h->Generation != 0 && h->HandoffPhysicalBase == base &&
      h->HandoffBytes == bytes && h->Reserved == 0 &&
      h->Ttbr0 != 0 && h->Ttbr1 != 0);
}
