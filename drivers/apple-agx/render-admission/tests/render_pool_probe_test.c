#include "render_pool_probe.h"

#include <assert.h>

static void test_one_aligned_run(void) {
  unsigned long long frames[8];
  ADMISSION_POOL_ATTEMPT a = {0};
  for (unsigned i = 0; i < 8; ++i) frames[i] = 0x9d0000ULL + i;
  AdmissionPoolAnalyzeFrames(frames, 8, &a);
  assert(a.ObtainedBytes == 0x8000ULL && a.Runs == 1u && a.Misaligned == 0u);
  assert(a.LowestPhysical == 0x9d0000000ULL &&
         a.HighestPhysical == 0x9d0008000ULL &&
         a.LargestRunBytes == 0x8000ULL);
}

static void test_runs_split_and_alignment(void) {
  /* Two 16 KiB-aligned 8 KiB runs out of order, then a misaligned run. */
  unsigned long long frames[] = {0x204ULL, 0x205ULL, 0x100ULL, 0x101ULL,
                                 0x301ULL, 0x302ULL, 0x303ULL};
  ADMISSION_POOL_ATTEMPT a = {0};
  AdmissionPoolAnalyzeFrames(frames, 7, &a);
  assert(a.Runs == 3u && a.Misaligned == 1u);
  assert(a.LowestPhysical == 0x100000ULL && a.HighestPhysical == 0x304000ULL);
  assert(a.LargestRunBytes == 0x3000ULL && a.ObtainedBytes == 0x7000ULL);
}

static void test_empty_keeps_request(void) {
  ADMISSION_POOL_ATTEMPT a = {0};
  a.RequestedBytes = 0x40000000ULL;
  a.SkipBytes = 0x200000ULL;
  a.Runs = 9u;
  AdmissionPoolAnalyzeFrames((void *)0, 0, &a);
  assert(a.RequestedBytes == 0x40000000ULL && a.SkipBytes == 0x200000ULL);
  assert(a.ObtainedBytes == 0ULL && a.Runs == 0u && a.Misaligned == 0u);
}

static void test_pool_policy(void) {
  const unsigned long long gib = 0x40000000ULL, mib = 0x100000ULL;
  /* J313 after M2-f2c-2: 6,279,270,400 B of Windows RAM -> 1.25 GiB. */
  assert(AdmissionPoolPolicyBytes(6279270400ULL, 4 * gib, 0) == 1280 * mib);
  /* Larger machines: capped at 1.5 GiB (broker record capacity); a small
   * window caps lower. */
  assert(AdmissionPoolPolicyBytes(15 * gib, 4 * gib, 0) == gib + 512 * mib);
  assert(AdmissionPoolPolicyBytes(60 * gib, 4 * gib, 0) == gib + 512 * mib);
  assert(AdmissionPoolPolicyBytes(60 * gib, gib, 0) == gib);
  /* Small RAM or an unknown size: the 512 MiB floor. */
  assert(AdmissionPoolPolicyBytes(3 * gib, 4 * gib, 0) == 768 * mib);
  assert(AdmissionPoolPolicyBytes(gib + 512 * mib, 4 * gib, 0) == 512 * mib);
  assert(AdmissionPoolPolicyBytes(0, 4 * gib, 0) == 512 * mib);
  /* A window that cannot hold the floor: no pool. */
  assert(AdmissionPoolPolicyBytes(8 * gib, 256 * mib, 0) == 0);
  /* Overrides: 64 MiB steps, clamped to the same bounds. */
  assert(AdmissionPoolPolicyBytes(6279270400ULL, 4 * gib, 1100) == 1088 * mib);
  assert(AdmissionPoolPolicyBytes(6279270400ULL, 4 * gib, 100) == 512 * mib);
  assert(AdmissionPoolPolicyBytes(6279270400ULL, 4 * gib, 9000) == gib + 512 * mib);
  assert(AdmissionPoolPolicyBytes(6279270400ULL, 1 * gib, 2048) == 1 * gib);
}

int main(void) {
  test_pool_policy();
  test_one_aligned_run();
  test_runs_split_and_alignment();
  test_empty_keeps_request();
  return 0;
}
