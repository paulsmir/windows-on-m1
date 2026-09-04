#include "render_memory.h"

#define ADMISSION_MEMORY_NULL ((void *)0)

APPLE_AGX_BOOL AdmissionMemoryInitialize(
    ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_SOFTWARE_APERTURE_ENTRY *ApertureEntries,
    APPLE_AGX_U32 AperturePageCount, APPLE_AGX_U64 ApertureBase,
    APPLE_AGX_U64 ApertureSize, APPLE_AGX_U64 LocalGpuVaBase,
    APPLE_AGX_U64 LocalBytes) {
  ADMISSION_MEMORY_CONTRACT candidate;
  APPLE_AGX_U64 requiredPages;
  unsigned char *bytes = (unsigned char *)&candidate;
  APPLE_AGX_U32 index;

  if (Memory == ADMISSION_MEMORY_NULL ||
      ApertureEntries == ADMISSION_MEMORY_NULL || ApertureSize == 0ULL ||
      (ApertureSize & (APPLE_AGX_SYSTEM_PAGE_SIZE - 1ULL)) != 0ULL)
    return APPLE_AGX_FALSE;
  requiredPages = ApertureSize / APPLE_AGX_SYSTEM_PAGE_SIZE;
  if (requiredPages == 0ULL || requiredPages > 0xffffffffULL ||
      AperturePageCount != (APPLE_AGX_U32)requiredPages)
    return APPLE_AGX_FALSE;

  for (index = 0u; index < (APPLE_AGX_U32)sizeof(candidate); ++index)
    bytes[index] = 0u;
  if (AppleAgxPhysicalTopologyDescribe(
          ApertureBase, ApertureSize, LocalGpuVaBase, LocalBytes,
          &candidate.Topology) != AppleAgxPhysicalTopologyOk)
    return APPLE_AGX_FALSE;
  if (AppleAgxSoftwareApertureInitialize(
          &candidate.Aperture, ApertureEntries,
          AperturePageCount) != AppleAgxSoftwareApertureOk)
    return APPLE_AGX_FALSE;
  candidate.LocalGpuVaBase = LocalGpuVaBase;
  candidate.LocalBytes = LocalBytes;
  candidate.Initialized = APPLE_AGX_TRUE;
  *Memory = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionMemoryMarkUatReady(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U32 Context,
    APPLE_AGX_U64 LeafSize, APPLE_AGX_U64 MappedGpuVaBase,
    APPLE_AGX_U64 MappedBytes) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE ||
      Memory->UatReady == APPLE_AGX_TRUE ||
      Context != ADMISSION_MEMORY_UAT_CONTEXT ||
      LeafSize != APPLE_AGX_UAT_PAGE_SIZE_16K ||
      MappedGpuVaBase != Memory->LocalGpuVaBase ||
      MappedBytes != Memory->LocalBytes)
    return APPLE_AGX_FALSE;
  Memory->UatReady = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL
AdmissionMemoryMarkPagingReady(ADMISSION_MEMORY_CONTRACT *Memory) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE ||
      Memory->UatReady != APPLE_AGX_TRUE ||
      Memory->PagingReady == APPLE_AGX_TRUE)
    return APPLE_AGX_FALSE;
  Memory->PagingReady = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AdmissionMemoryReady(const ADMISSION_MEMORY_CONTRACT *Memory) {
  return Memory != ADMISSION_MEMORY_NULL &&
                 Memory->Initialized == APPLE_AGX_TRUE &&
                 Memory->UatReady == APPLE_AGX_TRUE &&
                 Memory->PagingReady == APPLE_AGX_TRUE
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryMapAperture64K(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ApertureByteOffset,
    const APPLE_AGX_U64 *PhysicalPages, APPLE_AGX_U32 PhysicalPageCount) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE ||
      (ApertureByteOffset & (APPLE_AGX_WDDM_PAGE_SIZE_64K - 1ULL)) != 0ULL ||
      ApertureByteOffset / APPLE_AGX_SYSTEM_PAGE_SIZE > 0xffffffffULL ||
      PhysicalPageCount != APPLE_AGX_SYSTEM_PAGES_PER_WDDM_PAGE)
    return AppleAgxSoftwareApertureInvalidArgument;
  return AppleAgxSoftwareApertureMap(
      &Memory->Aperture,
      (APPLE_AGX_U32)(ApertureByteOffset / APPLE_AGX_SYSTEM_PAGE_SIZE),
      PhysicalPages, PhysicalPageCount);
}

APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryUnmapAperture64K(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ApertureByteOffset,
    APPLE_AGX_U64 DummyPage) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE ||
      (ApertureByteOffset & (APPLE_AGX_WDDM_PAGE_SIZE_64K - 1ULL)) != 0ULL ||
      ApertureByteOffset / APPLE_AGX_SYSTEM_PAGE_SIZE > 0xffffffffULL)
    return AppleAgxSoftwareApertureInvalidArgument;
  return AppleAgxSoftwareApertureUnmap(
      &Memory->Aperture,
      (APPLE_AGX_U32)(ApertureByteOffset / APPLE_AGX_SYSTEM_PAGE_SIZE),
      APPLE_AGX_SYSTEM_PAGES_PER_WDDM_PAGE, DummyPage);
}

APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT AdmissionMemoryLocalAddressToGpuVa(
    const ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U32 SegmentId,
    APPLE_AGX_U64 AllocationSegmentAddress,
    APPLE_AGX_U64 AllocationSize, APPLE_AGX_U64 AllocationOffset,
    APPLE_AGX_U64 *GpuVa) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE)
    return AppleAgxLocalSegmentAddressInvalidArgument;
  return AppleAgxLocalSegmentAddressToGpuVa(
      ADMISSION_MEMORY_LOCAL_SEGMENT, SegmentId,
      Memory->Topology.Local.Base, Memory->Topology.Local.Size,
      Memory->LocalGpuVaBase, AllocationSegmentAddress, AllocationSize,
      AllocationOffset, GpuVa);
}

APPLE_AGX_APERTURE_RESULT AdmissionMemoryPlanAperture64K(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U64 ApertureByteOffset,
    const APPLE_AGX_U64 *SystemPageAddresses,
    APPLE_AGX_U32 SystemPageCount, APPLE_AGX_APERTURE_RUN *Runs,
    APPLE_AGX_U32 RunCapacity, APPLE_AGX_U32 *RunCount) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE)
    return AppleAgxApertureResultInvalidArgument;
  return AppleAgxAperturePlan64K(
      Memory->Topology.Aperture.Base, Memory->Topology.Aperture.Size,
      ApertureByteOffset, SystemPageAddresses, SystemPageCount, Runs,
      RunCapacity, RunCount);
}

APPLE_AGX_PHYSICAL_PAGING_RESULT AdmissionMemoryPlanTransfer(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U32 SourceSegmentId, APPLE_AGX_U64 SourceAddress,
    APPLE_AGX_U32 DestinationSegmentId,
    APPLE_AGX_U64 DestinationAddress, APPLE_AGX_U64 TransferOffset,
    APPLE_AGX_U64 SystemOffset, APPLE_AGX_U64 Bytes,
    APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE)
    return AppleAgxPhysicalPagingInvalidArgument;
  return AppleAgxPhysicalPagingPlanTransfer(
      SourceSegmentId, SourceAddress, DestinationSegmentId,
      DestinationAddress, Memory->Topology.Local.Base,
      Memory->Topology.Local.Size, TransferOffset, SystemOffset, Bytes, Plan);
}

APPLE_AGX_PHYSICAL_PAGING_RESULT AdmissionMemoryPlanFill(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U32 DestinationSegmentId,
    APPLE_AGX_U64 DestinationAddress, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U32 Pattern, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE)
    return AppleAgxPhysicalPagingInvalidArgument;
  return AppleAgxPhysicalPagingPlanFill(
      DestinationSegmentId, DestinationAddress,
      Memory->Topology.Local.Base, Memory->Topology.Local.Size, Bytes,
      Pattern, Plan);
}

APPLE_AGX_PHYSICAL_PAGING_RESULT AdmissionMemoryPlanDiscard(
    const ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U32 SegmentId,
    APPLE_AGX_U64 SegmentAddress, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan) {
  if (Memory == ADMISSION_MEMORY_NULL ||
      Memory->Initialized != APPLE_AGX_TRUE)
    return AppleAgxPhysicalPagingInvalidArgument;
  return AppleAgxPhysicalPagingPlanDiscard(
      SegmentId, SegmentAddress, Memory->Topology.Local.Base,
      Memory->Topology.Local.Size, Plan);
}
