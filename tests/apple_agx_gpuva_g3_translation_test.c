#include "../drivers/apple-agx/shared/include/apple_agx_gpuva_g3_translation.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static void valid_group(APPLE_AGX_GPUVA_G3_LOGICAL_PTE p[4],
                        uint64_t base, uint32_t segment, uint32_t flags) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (APPLE_AGX_GPUVA_G3_LOGICAL_PTE){base + i * 0x1000u,
                                             segment, flags};
}

int main(void) {
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE p[8];
  APPLE_AGX_GPUVA_G3_NATIVE_LEAF out[2], sentinel[2];
  uint32_t n = 99u;
  for (uint32_t profile = 0x4000u; profile <= 0x10000u;
       profile *= 4u) {
    valid_group(p, 0x20000000ULL, 1u,
                APPLE_AGX_GPUVA_G3_VALID | APPLE_AGX_GPUVA_G3_WRITE);
    valid_group(p + 4, 0x20004000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
    assert(AppleAgxGpuvaG3PlanSpan(p, 8184u, 8u, 0x1fffc000ULL,
        1u, profile, out, 2u, &n) == AppleAgxGpuvaG3Ok);
    assert(n == 2u && out[0].GpuVa == 0x1fffc000ULL &&
           out[1].GpuVa == 0x20000000ULL);
    assert(out[0].GuestIpa == 0x20000000ULL &&
           out[0].ValidMask == 15u && out[0].WritableMask == 15u);
    assert(out[1].GuestIpa == 0x20004000ULL &&
           out[1].ValidMask == 15u && out[1].WritableMask == 0u);
    memset(p, 0, sizeof(p));
    assert(AppleAgxGpuvaG3PlanSpan(p, 8188u, 4u, 0x1fffc000ULL,
        1u, profile, out, 2u, &n) == AppleAgxGpuvaG3Unmap);
    assert(n == 1u && out[0].ValidMask == 0u && out[0].GuestIpa == 0u);
  }

  valid_group(p, 0x20000000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
  memset(sentinel, 0xa5, sizeof(sentinel));
  for (unsigned mask = 1; mask < 15; ++mask) {
    valid_group(p, 0x20000000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
    for (unsigned i = 0; i < 4; ++i)
      if (!(mask & (1u << i))) p[i].Flags = 0u;
    memcpy(out, sentinel, sizeof(out));
    assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x1500000000ULL,
        1u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Unrepresentable);
    assert(memcmp(out, sentinel, sizeof(out)) == 0);
    valid_group(p, 0x20000000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
    for (unsigned i = 0; i < 4; ++i)
      if (mask & (1u << i)) p[i].Flags |= APPLE_AGX_GPUVA_G3_WRITE;
    assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x1500000000ULL,
        1u, 0x4000u, out, 2u, &n) == AppleAgxGpuvaG3Unrepresentable);
    assert(memcmp(out, sentinel, sizeof(out)) == 0);
  }
  valid_group(p, 0x20000000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
  p[1].GuestIpa = 0x21001000ULL;
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x1500000000ULL,
      1u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Unrepresentable);
  valid_group(p, 0x20000000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
  p[2].SegmentId = 0u;
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x1500000000ULL,
      1u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Unrepresentable);
  valid_group(p, 0x20000000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
  valid_group(p + 4, 0x20004000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
  p[7].GuestIpa = 0x21007000ULL;
  memcpy(out, sentinel, sizeof(out));
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 8u, 0x1500000000ULL,
      1u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Unrepresentable);
  assert(memcmp(out, sentinel, sizeof(out)) == 0);
  valid_group(p, 0x20000000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
  assert(AppleAgxGpuvaG3PlanSpan(p, 1u, 4u, 0x1500001000ULL,
      1u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Invalid);
  assert(AppleAgxGpuvaG3PlanSpan(p, 8188u, 8u, 0x1500000000ULL,
      1u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Invalid);
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, (1ULL << 39) - 0x1000ULL,
      1u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Invalid);
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x1500000000ULL,
      1u, 0x2000u, out, 2u, &n) == AppleAgxGpuvaG3Invalid);
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x1500000000ULL,
      1u, 0x10000u, out, 0u, &n) == AppleAgxGpuvaG3Invalid);
  return 0;
}
