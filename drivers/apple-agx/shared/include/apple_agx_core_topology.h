#ifndef APPLE_AGX_CORE_TOPOLOGY_H
#define APPLE_AGX_CORE_TOPOLOGY_H

#include "apple_agx_win32_abi.h"

/* SGX identification and core-mask registers (Asahi drm/asahi regs.rs
 * get_gpu_id: ID_VERSION 0xd04000, ID_COUNTS_1 0xd04010, ID_CLUSTERS
 * 0xd0401c; G13/G14G CORE_MASK_0 0xd01500, CORE_MASK_1 0xd01514). */
#define APPLE_AGX_SGX_ID_VERSION 0xd04000u
#define APPLE_AGX_SGX_ID_COUNTS_1 0xd04010u
#define APPLE_AGX_SGX_ID_CLUSTERS 0xd0401cu
#define APPLE_AGX_SGX_CORE_MASK_0 0xd01500u
#define APPLE_AGX_SGX_CORE_MASK_1 0xd01514u

/* EXP1316 (dwmcore.dll_003d8800): Mesa sizes and fills the scratch block
 * list per present core (agx_scratch.c, params.core_masks and
 * num_cores_per_cluster); with no core reported a fragment shader using
 * scratch walked an empty list. Derivation as Asahi regs.rs: the GPU
 * generation is ID_VERSION[31:24] (4: G13), the cores per cluster
 * ID_COUNTS_1[7:0], the clusters ID_CLUSTERS[19:12]; each cluster takes
 * the next num_cores bits of the concatenated masks and no bit may be left
 * over. This adapter implements the one-cluster G13G model only. */
static inline int AppleAgxG13CoreTopology(
    APPLE_AGX_U32 IdVersion, APPLE_AGX_U32 IdCounts1, APPLE_AGX_U32 IdClusters,
    APPLE_AGX_U32 CoreMask0, APPLE_AGX_U32 CoreMask1,
    APPLE_AGX_U32 *CoresPerCluster, APPLE_AGX_U32 *CoreMask) {
  APPLE_AGX_U32 cores = IdCounts1 & 0xffu;
  APPLE_AGX_U32 clusters = (IdClusters >> 12) & 0xffu;
  APPLE_AGX_U32 limit;
  if (CoresPerCluster == 0 || CoreMask == 0) return 0;
  *CoresPerCluster = 0u;
  *CoreMask = 0u;
  if (((IdVersion >> 24) & 0xffu) != 4u || clusters != 1u ||
      cores == 0u || cores > 32u)
    return 0;
  limit = cores == 32u ? 0xffffffffu : ((1u << cores) - 1u);
  if ((CoreMask0 & limit) == 0u || (CoreMask0 & ~limit) != 0u ||
      CoreMask1 != 0u)
    return 0;
  *CoresPerCluster = cores;
  *CoreMask = CoreMask0;
  return 1;
}

#endif
