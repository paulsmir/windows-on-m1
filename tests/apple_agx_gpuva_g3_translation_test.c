#include "../drivers/apple-agx/shared/include/apple_agx_gpuva_g3_translation.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static void valid_group(APPLE_AGX_GPUVA_G3_LOGICAL_PTE p[4],
                        uint64_t base, uint32_t segment, uint32_t flags) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (APPLE_AGX_GPUVA_G3_LOGICAL_PTE){base + i * 0x1000u,
                                             segment, flags, 0, 0};
}

int main(void) {
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE p[8];
  APPLE_AGX_GPUVA_G3_NATIVE_LEAF out[2], sentinel[2];
  uint32_t n = 99u;
  unsigned long long resolved = 0;
  /* EXP782: the PTE carries address bits 63:12, not a byte offset. */
  assert(AppleAgxGpuvaG3PteAddressBytes(0xcULL, &resolved));
  assert(resolved == 0xc000ULL);
  assert(AppleAgxGpuvaG3ResolvePageAddress(2u, resolved,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Ok);
  assert(resolved == 0x90000c000ULL);
  assert(AppleAgxGpuvaG3PteAddressBytes(0x10ULL, &resolved));
  assert(resolved == 0x10000ULL && (resolved & 0xffffULL) == 0ULL);
  assert(AppleAgxGpuvaG3ResolvePageAddress(2u, resolved,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Ok);
  assert(resolved == 0x900010000ULL);
  assert(!AppleAgxGpuvaG3PteAddressBytes((~0ULL >> 12) + 1ULL, &resolved));
  assert(AppleAgxGpuvaG3ResolvePageAddress(0u, 0x850000000ULL,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Ok);
  assert(resolved == 0x850000000ULL);
  assert(AppleAgxGpuvaG3ResolvePageAddress(1u, 0x850004000ULL,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Ok);
  assert(resolved == 0x850004000ULL);
  assert(AppleAgxGpuvaG3ResolvePageAddress(2u, 0x4000ULL,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Ok);
  assert(resolved == 0x900004000ULL);
  assert(AppleAgxGpuvaG3ResolvePageAddress(2u, 0ULL,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Ok);
  assert(resolved == 0x900000000ULL);
  assert(AppleAgxGpuvaG3ResolvePageAddress(2u, 0x4000000ULL,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Invalid);
  assert(AppleAgxGpuvaG3ResolvePageAddress(3u, 0x4000ULL,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Invalid);
  assert(AppleAgxGpuvaG3ResolvePageAddress(2u, 0x4001ULL,
      2u, 0x900000000ULL, 0x4000000ULL, &resolved) == AppleAgxGpuvaG3Invalid);
  valid_group(p, 0x850000000ULL, 0u, APPLE_AGX_GPUVA_G3_VALID);
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x4000ULL,
      2u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Ok);
  valid_group(p, 0x850004000ULL, 1u, APPLE_AGX_GPUVA_G3_VALID);
  assert(AppleAgxGpuvaG3PlanSpan(p, 0u, 4u, 0x4000ULL,
      2u, 0x10000u, out, 2u, &n) == AppleAgxGpuvaG3Ok);
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
  {
    APPLE_AGX_GPUVA_G3_LOGICAL_PTE wide[2] = {
        {0x20000000ULL, 2u, APPLE_AGX_GPUVA_G3_VALID | APPLE_AGX_GPUVA_G3_WRITE, 0, 0},
        {0x20010000ULL, 2u, APPLE_AGX_GPUVA_G3_VALID, 0, 0}};
    APPLE_AGX_GPUVA_G3_NATIVE_LEAF four[8];
    assert(AppleAgxGpuvaG3Plan64KSpan(wide, 510u, 2u, 0x1fffe0000ULL,
        2u, four, 8u, &n) == AppleAgxGpuvaG3Ok);
    assert(n == 8u && four[0].GuestIpa == 0x20000000ULL &&
           four[3].GuestIpa == 0x2000c000ULL &&
           four[4].GuestIpa == 0x20010000ULL &&
           four[0].WritableMask == 15u && four[4].WritableMask == 0u);
    assert(four[0].GpuVa == 0x1fffe0000ULL &&
           four[4].GpuVa == 0x1ffff0000ULL);
    wide[1].GuestIpa = 0x20011000ULL;
    memset(four, 0xa5, sizeof(four));
    assert(AppleAgxGpuvaG3Plan64KSpan(wide, 510u, 2u, 0x1fffe0000ULL,
        2u, four, 8u, &n) == AppleAgxGpuvaG3Unrepresentable);
    for (unsigned i = 0; i < sizeof(four); ++i)
      assert(((unsigned char *)four)[i] == 0xa5u);
    wide[1].GuestIpa = 0x20010000ULL;
    assert(AppleAgxGpuvaG3Plan64KSpan(wide, 511u, 2u, 0x1fffe0000ULL,
        2u, four, 8u, &n) == AppleAgxGpuvaG3Invalid);
    wide[0].SegmentId = 1u; /* aperture offset is not a system PFN */
    assert(AppleAgxGpuvaG3Plan64KSpan(wide, 510u, 2u, 0x1fffe0000ULL,
        2u, four, 8u, &n) == AppleAgxGpuvaG3Unrepresentable);
    wide[0].SegmentId = 2u;
    assert(AppleAgxGpuvaG3Plan64KSpan(wide, 510u, 2u, 0x1fffe0000ULL,
        2u, four, 7u, &n) == AppleAgxGpuvaG3Invalid);
    wide[0].Flags = 0u;
    wide[1].Flags = 0u;
    assert(AppleAgxGpuvaG3Plan64KSpan(wide, 510u, 2u, 0x1fffe0000ULL,
        2u, four, 8u, &n) == AppleAgxGpuvaG3Unmap);
    assert(n == 8u && four[0].ValidMask == 0u &&
           four[7].GuestIpa == 0ULL);
  }
  valid_group(p, 0x851000000ULL, 0u, 3u);
  assert(AppleAgxGpuvaG3PlanSpan(p,0,4,0x10000,2,0x4000,out,2,&n)==AppleAgxGpuvaG3Ok);
  assert(out[0].SegmentId==0u);
  { APPLE_AGX_GPUVA_G3_NATIVE_LEAF sys[4];
    assert(AppleAgxGpuvaG3Plan64KSpan(p,0,1,0x10000,2,sys,4,&n)==AppleAgxGpuvaG3Ok);
    assert(n==4 && sys[3].SegmentId==0 && sys[3].GuestIpa==0x85100c000ULL);
  }
  return 0;
}
