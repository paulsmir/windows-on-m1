#ifndef APPLE_AGX_PHYSICAL_TOPOLOGY_H
#define APPLE_AGX_PHYSICAL_TOPOLOGY_H

#include "apple_agx_state.h"

typedef enum _APPLE_AGX_PHYSICAL_TOPOLOGY_RESULT {
  AppleAgxPhysicalTopologyOk = 0,
  AppleAgxPhysicalTopologyInvalidArgument,
  AppleAgxPhysicalTopologyMisaligned,
  AppleAgxPhysicalTopologyOverflow,
} APPLE_AGX_PHYSICAL_TOPOLOGY_RESULT;

typedef struct _APPLE_AGX_PHYSICAL_SEGMENT {
  APPLE_AGX_U32 Id;
  APPLE_AGX_U64 Base;
  APPLE_AGX_U64 Size;
  APPLE_AGX_U64 CommitLimit;
  APPLE_AGX_BOOL Aperture;
  APPLE_AGX_BOOL Use64KPages;
  APPLE_AGX_BOOL CpuVisible;
  APPLE_AGX_BOOL PopulatedFromSystemMemory;
} APPLE_AGX_PHYSICAL_SEGMENT;

typedef struct _APPLE_AGX_PHYSICAL_TOPOLOGY {
  APPLE_AGX_U32 SegmentCount;
  APPLE_AGX_U32 PagingBufferSegmentId;
  APPLE_AGX_U64 PagingBufferSize;
  APPLE_AGX_PHYSICAL_SEGMENT Aperture;
  APPLE_AGX_PHYSICAL_SEGMENT Local;
  /* M2-f2: optional third segment (Id 3) backed by Windows memory the KMD
   * allocated at start; Id 0 while absent. */
  APPLE_AGX_PHYSICAL_SEGMENT Pool;
} APPLE_AGX_PHYSICAL_TOPOLOGY;

APPLE_AGX_PHYSICAL_TOPOLOGY_RESULT AppleAgxPhysicalTopologyDescribe(
    APPLE_AGX_U64 ApertureBase, APPLE_AGX_U64 ApertureSize,
    APPLE_AGX_U64 LocalBase, APPLE_AGX_U64 LocalSize,
    APPLE_AGX_PHYSICAL_TOPOLOGY *Topology);

/* Adds the CPU-visible, driver-populated pool segment once; it must be
 * 64 KiB-aligned, non-empty and must not overlap the other segments. */
APPLE_AGX_PHYSICAL_TOPOLOGY_RESULT AppleAgxPhysicalTopologyAddPool(
    APPLE_AGX_PHYSICAL_TOPOLOGY *Topology, APPLE_AGX_U64 PoolBase,
    APPLE_AGX_U64 PoolSize);

#endif /* APPLE_AGX_PHYSICAL_TOPOLOGY_H */
