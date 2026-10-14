#include "apple_agx_physical_topology.h"

#include <assert.h>
#include <string.h>

#define PAGE_4K 0x1000ULL
#define PAGE_64K 0x10000ULL

static void test_builds_truthful_two_segment_physical_topology(void) {
  APPLE_AGX_PHYSICAL_TOPOLOGY topology;

  memset(&topology, 0xa5, sizeof(topology));
  assert(AppleAgxPhysicalTopologyDescribe(
             0x100000000ULL, 256ULL * 1024ULL * 1024ULL,
             0x80000000ULL, 64ULL * 1024ULL * 1024ULL,
             &topology) == AppleAgxPhysicalTopologyOk);
  assert(topology.SegmentCount == 2u);
  assert(topology.PagingBufferSegmentId == 1u);
  assert(topology.PagingBufferSize == PAGE_4K);

  assert(topology.Aperture.Id == 1u);
  assert(topology.Aperture.Base == 0x100000000ULL);
  assert(topology.Aperture.Size == 256ULL * 1024ULL * 1024ULL);
  assert(topology.Aperture.CommitLimit == topology.Aperture.Size);
  assert(topology.Aperture.Aperture == APPLE_AGX_TRUE);
  assert(topology.Aperture.Use64KPages == APPLE_AGX_FALSE);
  assert(topology.Aperture.CpuVisible == APPLE_AGX_FALSE);

  assert(topology.Local.Id == 2u);
  assert(topology.Local.Base == 0x80000000ULL);
  assert(topology.Local.Size == 64ULL * 1024ULL * 1024ULL);
  assert(topology.Local.CommitLimit == topology.Local.Size);
  assert(topology.Local.Aperture == APPLE_AGX_FALSE);
  assert(topology.Local.Use64KPages == APPLE_AGX_TRUE);
  assert(topology.Local.CpuVisible == APPLE_AGX_FALSE);
  assert(topology.Local.PopulatedFromSystemMemory == APPLE_AGX_TRUE);
}

static void test_alignment_range_and_overflow_fail_closed(void) {
  APPLE_AGX_PHYSICAL_TOPOLOGY topology;

  memset(&topology, 0xa5, sizeof(topology));
  assert(AppleAgxPhysicalTopologyDescribe(
             0x100000001ULL, 16ULL * PAGE_4K, 0x80000000ULL,
             16ULL * PAGE_64K, &topology) ==
         AppleAgxPhysicalTopologyMisaligned);
  assert(topology.SegmentCount == 0u);

  assert(AppleAgxPhysicalTopologyDescribe(
             0x100000000ULL, 16ULL * PAGE_4K, 0x80001000ULL,
             16ULL * PAGE_64K, &topology) ==
         AppleAgxPhysicalTopologyMisaligned);
  assert(topology.SegmentCount == 0u);

  assert(AppleAgxPhysicalTopologyDescribe(
             0xfffffffffffff000ULL, 2ULL * PAGE_4K, 0x80000000ULL,
             16ULL * PAGE_64K, &topology) ==
         AppleAgxPhysicalTopologyOverflow);
  assert(topology.SegmentCount == 0u);

  assert(AppleAgxPhysicalTopologyDescribe(
             0x100000000ULL, 0ULL, 0x80000000ULL,
             16ULL * PAGE_64K, &topology) ==
         AppleAgxPhysicalTopologyInvalidArgument);
  assert(topology.SegmentCount == 0u);
}

static void test_pool_is_a_third_driver_populated_segment(void) {
  APPLE_AGX_PHYSICAL_TOPOLOGY topology;

  assert(AppleAgxPhysicalTopologyDescribe(
             0x1600000000ULL, 256ULL * 1024ULL * 1024ULL,
             0x1500000000ULL, 64ULL * 1024ULL * 1024ULL,
             &topology) == AppleAgxPhysicalTopologyOk);
  /* M2-f2: Windows memory the KMD allocated at start (EXP1246). */
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1700000000ULL,
             1024ULL * 1024ULL * 1024ULL) == AppleAgxPhysicalTopologyOk);
  assert(topology.SegmentCount == 3u);
  assert(topology.Pool.Id == 3u);
  assert(topology.Pool.Base == 0x1700000000ULL);
  assert(topology.Pool.Size == 1024ULL * 1024ULL * 1024ULL);
  assert(topology.Pool.CommitLimit == topology.Pool.Size);
  assert(topology.Pool.Aperture == APPLE_AGX_FALSE);
  assert(topology.Pool.Use64KPages == APPLE_AGX_TRUE);
  assert(topology.Pool.CpuVisible == APPLE_AGX_TRUE);
  assert(topology.Pool.PopulatedFromSystemMemory == APPLE_AGX_TRUE);
  /* The two original segments are unchanged. */
  assert(topology.Aperture.Id == 1u && topology.Local.Id == 2u &&
         topology.Local.Base == 0x1500000000ULL);
  /* Only once, never overlapping, 64 KiB-aligned, non-empty. */
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1800000000ULL,
             PAGE_64K) == AppleAgxPhysicalTopologyInvalidArgument);
  assert(topology.Pool.Base == 0x1700000000ULL);
}

static void test_pool_rejects_bad_ranges(void) {
  APPLE_AGX_PHYSICAL_TOPOLOGY topology;

  assert(AppleAgxPhysicalTopologyDescribe(
             0x1600000000ULL, 256ULL * 1024ULL * 1024ULL,
             0x1500000000ULL, 64ULL * 1024ULL * 1024ULL,
             &topology) == AppleAgxPhysicalTopologyOk);
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1700001000ULL,
             16ULL * PAGE_64K) == AppleAgxPhysicalTopologyMisaligned);
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1700000000ULL,
             16ULL * PAGE_64K + PAGE_4K) == AppleAgxPhysicalTopologyMisaligned);
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1700000000ULL, 0ULL) ==
         AppleAgxPhysicalTopologyInvalidArgument);
  /* Overlaps the local segment or the aperture. */
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1500000000ULL,
             16ULL * PAGE_64K) == AppleAgxPhysicalTopologyInvalidArgument);
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x15ffff0000ULL,
             2ULL * PAGE_64K) == AppleAgxPhysicalTopologyInvalidArgument);
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0xffffffffffff0000ULL,
             2ULL * PAGE_64K) == AppleAgxPhysicalTopologyOverflow);
  assert(AppleAgxPhysicalTopologyAddPool((APPLE_AGX_PHYSICAL_TOPOLOGY *)0,
             0x1700000000ULL, PAGE_64K) ==
         AppleAgxPhysicalTopologyInvalidArgument);
  assert(topology.SegmentCount == 2u && topology.Pool.Id == 0u);
}

/* M2-f8c (EXP1265-D, EXP1267): dxgmms2 requires an aperture in the set of a
 * CPU-visible allocation, and VidMm demotes a class BO into it when segment 3
 * is full, where the GPU cannot map it. The gate is an aperture smaller than
 * the 64 KiB allocation unit: it satisfies the rule and can hold no class
 * BO. It follows the pool, after the aperture, and overlaps nothing. */
static void test_gate_is_a_fourth_aperture_too_small_for_any_bo(void) {
  APPLE_AGX_PHYSICAL_TOPOLOGY topology;

  assert(AppleAgxPhysicalTopologyDescribe(
             0x1600000000ULL, 256ULL * 1024ULL * 1024ULL,
             0x1500000000ULL, 64ULL * 1024ULL * 1024ULL,
             &topology) == AppleAgxPhysicalTopologyOk);
  /* Only after the pool. */
  assert(AppleAgxPhysicalTopologyAddGate(&topology) ==
         AppleAgxPhysicalTopologyInvalidArgument);
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1700000000ULL,
             1024ULL * 1024ULL * 1024ULL) == AppleAgxPhysicalTopologyOk);
  assert(AppleAgxPhysicalTopologyAddGate(&topology) == AppleAgxPhysicalTopologyOk);
  assert(topology.SegmentCount == 4u && topology.Gate.Id == 4u);
  assert(topology.Gate.Aperture == APPLE_AGX_TRUE);
  assert(topology.Gate.Size == APPLE_AGX_TOPOLOGY_GATE_BYTES);
  assert(APPLE_AGX_TOPOLOGY_GATE_BYTES < PAGE_64K &&
         APPLE_AGX_TOPOLOGY_GATE_BYTES % PAGE_4K == 0ULL);
  assert(topology.Gate.CommitLimit == topology.Gate.Size);
  assert(topology.Gate.Base == topology.Aperture.Base + topology.Aperture.Size);
  assert(topology.Gate.Use64KPages == APPLE_AGX_FALSE &&
         topology.Gate.CpuVisible == APPLE_AGX_FALSE &&
         topology.Gate.PopulatedFromSystemMemory == APPLE_AGX_FALSE);
  /* Only once. */
  assert(AppleAgxPhysicalTopologyAddGate(&topology) ==
         AppleAgxPhysicalTopologyInvalidArgument);
  assert(AppleAgxPhysicalTopologyAddGate((APPLE_AGX_PHYSICAL_TOPOLOGY *)0) ==
         AppleAgxPhysicalTopologyInvalidArgument);
  /* A gate that would overlap the local segment or the pool is refused. */
  assert(AppleAgxPhysicalTopologyDescribe(
             0x1600000000ULL, 256ULL * 1024ULL * 1024ULL,
             0x1610000000ULL, 64ULL * 1024ULL * 1024ULL,
             &topology) == AppleAgxPhysicalTopologyOk);
  assert(AppleAgxPhysicalTopologyAddPool(&topology, 0x1700000000ULL,
             PAGE_64K) == AppleAgxPhysicalTopologyOk);
  assert(AppleAgxPhysicalTopologyAddGate(&topology) ==
         AppleAgxPhysicalTopologyInvalidArgument);
  assert(topology.SegmentCount == 3u && topology.Gate.Id == 0u);
}

int main(void) {
  test_builds_truthful_two_segment_physical_topology();
  test_alignment_range_and_overflow_fail_closed();
  test_pool_is_a_third_driver_populated_segment();
  test_gate_is_a_fourth_aperture_too_small_for_any_bo();
  test_pool_rejects_bad_ranges();
  return 0;
}
