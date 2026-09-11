#include "apple_agx_physical_topology.h"

#define APPLE_AGX_TOPOLOGY_PAGE_4K 0x1000ULL
#define APPLE_AGX_TOPOLOGY_PAGE_64K 0x10000ULL
#define APPLE_AGX_TOPOLOGY_U64_MAX (~(APPLE_AGX_U64)0ULL)

static void AppleAgxPhysicalTopologyClear(
    APPLE_AGX_PHYSICAL_TOPOLOGY *Topology) {
  unsigned char *bytes = (unsigned char *)Topology;
  APPLE_AGX_U32 index;
  for (index = 0u; index < (APPLE_AGX_U32)sizeof(*Topology); ++index)
    bytes[index] = 0u;
}

APPLE_AGX_PHYSICAL_TOPOLOGY_RESULT AppleAgxPhysicalTopologyDescribe(
    APPLE_AGX_U64 ApertureBase, APPLE_AGX_U64 ApertureSize,
    APPLE_AGX_U64 LocalBase, APPLE_AGX_U64 LocalSize,
    APPLE_AGX_PHYSICAL_TOPOLOGY *Topology) {
  if (Topology == (APPLE_AGX_PHYSICAL_TOPOLOGY *)0)
    return AppleAgxPhysicalTopologyInvalidArgument;
  AppleAgxPhysicalTopologyClear(Topology);
  if (ApertureSize == 0ULL || LocalSize == 0ULL)
    return AppleAgxPhysicalTopologyInvalidArgument;
  if ((ApertureBase & (APPLE_AGX_TOPOLOGY_PAGE_4K - 1ULL)) != 0ULL ||
      (ApertureSize & (APPLE_AGX_TOPOLOGY_PAGE_4K - 1ULL)) != 0ULL ||
      (LocalBase & (APPLE_AGX_TOPOLOGY_PAGE_64K - 1ULL)) != 0ULL ||
      (LocalSize & (APPLE_AGX_TOPOLOGY_PAGE_64K - 1ULL)) != 0ULL)
    return AppleAgxPhysicalTopologyMisaligned;
  if (ApertureBase > APPLE_AGX_TOPOLOGY_U64_MAX - ApertureSize ||
      LocalBase > APPLE_AGX_TOPOLOGY_U64_MAX - LocalSize)
    return AppleAgxPhysicalTopologyOverflow;

  Topology->SegmentCount = 2u;
  Topology->PagingBufferSegmentId = 1u;
  Topology->PagingBufferSize = APPLE_AGX_TOPOLOGY_PAGE_4K;

  Topology->Aperture.Id = 1u;
  Topology->Aperture.Base = ApertureBase;
  Topology->Aperture.Size = ApertureSize;
  Topology->Aperture.CommitLimit = ApertureSize;
  Topology->Aperture.Aperture = APPLE_AGX_TRUE;

  Topology->Local.Id = 2u;
  Topology->Local.Base = LocalBase;
  Topology->Local.Size = LocalSize;
  Topology->Local.CommitLimit = LocalSize;
  Topology->Local.Use64KPages = APPLE_AGX_TRUE;
  Topology->Local.PopulatedFromSystemMemory = APPLE_AGX_TRUE;
  return AppleAgxPhysicalTopologyOk;
}
