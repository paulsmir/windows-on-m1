#include "apple_agx_gpuva_g3_translation.h"

static APPLE_AGX_GPUVA_G3_RESULT plan_group(
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *p, unsigned int local_segment,
    APPLE_AGX_GPUVA_G3_NATIVE_LEAF *leaf) {
  unsigned int flags = p[0].Flags;
  unsigned long long base = p[0].GuestIpa;
  unsigned int i;
  if ((flags & ~(APPLE_AGX_GPUVA_G3_VALID | APPLE_AGX_GPUVA_G3_WRITE)) != 0u)
    return AppleAgxGpuvaG3Unrepresentable;
  if ((flags & APPLE_AGX_GPUVA_G3_VALID) == 0u) {
    for (i = 0u; i < 4u; ++i)
      if (p[i].Flags != 0u)
        return AppleAgxGpuvaG3Unrepresentable;
    if (leaf != 0) {
      leaf->GuestIpa = 0ULL;
      leaf->SegmentId = 0u;
      leaf->ValidMask = 0u;
      leaf->WritableMask = 0u;
    }
    return AppleAgxGpuvaG3Unmap;
  }
  if (base == 0ULL || (base & (APPLE_AGX_GPUVA_G3_NATIVE_PAGE - 1u)) ||
      base > ~0ULL - (APPLE_AGX_GPUVA_G3_NATIVE_PAGE - 1u) ||
      p[0].SegmentId != local_segment)
    return AppleAgxGpuvaG3Unrepresentable;
  for (i = 0u; i < 4u; ++i)
    if (p[i].Flags != flags || p[i].SegmentId != local_segment ||
        p[i].GuestIpa != base + (unsigned long long)i *
            APPLE_AGX_GPUVA_G3_LOGICAL_PAGE)
      return AppleAgxGpuvaG3Unrepresentable;
  if (leaf != 0) {
    leaf->GuestIpa = base;
    leaf->SegmentId = local_segment;
    leaf->ValidMask = 15u;
    leaf->WritableMask = (flags & APPLE_AGX_GPUVA_G3_WRITE) ? 15u : 0u;
  }
  return AppleAgxGpuvaG3Ok;
}

APPLE_AGX_GPUVA_G3_RESULT AppleAgxGpuvaG3PlanSpan(
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *entries,
    unsigned int start_index, unsigned int count,
    unsigned long long first_gpu_va, unsigned int local_segment,
    unsigned int segment_page_bytes,
    APPLE_AGX_GPUVA_G3_NATIVE_LEAF *leaves, unsigned int leaf_capacity,
    unsigned int *leaf_count) {
  const unsigned long long va_limit = 1ULL << 39;
  APPLE_AGX_GPUVA_G3_RESULT result, overall = AppleAgxGpuvaG3Unmap;
  unsigned int groups, i;
  if (entries == 0 || leaves == 0 || leaf_count == 0 ||
      local_segment == 0u ||
      (segment_page_bytes != 0x4000u && segment_page_bytes != 0x10000u) ||
      count == 0u || (count & 3u) || (start_index & 3u) ||
      start_index >= APPLE_AGX_GPUVA_G3_LOGICAL_LEAF_ENTRIES ||
      count > APPLE_AGX_GPUVA_G3_LOGICAL_LEAF_ENTRIES - start_index ||
      (first_gpu_va & (APPLE_AGX_GPUVA_G3_NATIVE_PAGE - 1u)) ||
      first_gpu_va >= va_limit ||
      (unsigned long long)count * APPLE_AGX_GPUVA_G3_LOGICAL_PAGE >
          va_limit - first_gpu_va)
    return AppleAgxGpuvaG3Invalid;
  groups = count / 4u;
  if (groups > leaf_capacity)
    return AppleAgxGpuvaG3Invalid;
  /* Validate the complete update before writing any output. */
  for (i = 0u; i < groups; ++i) {
    result = plan_group(entries + i * 4u, local_segment, 0);
    if (result != AppleAgxGpuvaG3Ok && result != AppleAgxGpuvaG3Unmap)
      return result;
    if (result == AppleAgxGpuvaG3Ok)
      overall = AppleAgxGpuvaG3Ok;
  }
  for (i = 0u; i < groups; ++i) {
    leaves[i].GpuVa = first_gpu_va + (unsigned long long)i *
        APPLE_AGX_GPUVA_G3_NATIVE_PAGE;
    (void)plan_group(entries + i * 4u, local_segment, &leaves[i]);
  }
  *leaf_count = groups;
  return overall;
}
