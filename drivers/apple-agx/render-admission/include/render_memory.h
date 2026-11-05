#ifndef APPLE_AGX_RENDER_MEMORY_H
#define APPLE_AGX_RENDER_MEMORY_H

#include "apple_agx_aperture.h"
#include "apple_agx_local_segment.h"
#include "apple_agx_physical_paging.h"
#include "apple_agx_physical_topology.h"
#include "apple_agx_software_aperture.h"

#define ADMISSION_MEMORY_UAT_CONTEXT 63u
#define ADMISSION_MEMORY_APERTURE_SEGMENT 1u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u

typedef struct _ADMISSION_MEMORY_CONTRACT {
  APPLE_AGX_PHYSICAL_TOPOLOGY Topology;
  APPLE_AGX_SOFTWARE_APERTURE Aperture;
  APPLE_AGX_U64 LocalGpuVaBase;
  APPLE_AGX_U64 LocalBytes;
  APPLE_AGX_U64 LocalAllocationBytes;
  APPLE_AGX_U64 BackendOffset;
  APPLE_AGX_U64 BackendBytes;
  APPLE_AGX_BOOL Initialized;
  APPLE_AGX_BOOL UatReady;
  APPLE_AGX_BOOL PagingReady;
} ADMISSION_MEMORY_CONTRACT;

typedef struct _ADMISSION_LOCAL_MEMORY_VIEW {
  void *CpuAddress;
  APPLE_AGX_U64 HostPhysicalAddress;
  APPLE_AGX_U64 GpuVirtualAddress;
  APPLE_AGX_U64 Bytes;
} ADMISSION_LOCAL_MEMORY_VIEW;

APPLE_AGX_BOOL AdmissionMemoryInitialize(
    ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_SOFTWARE_APERTURE_ENTRY *ApertureEntries,
    APPLE_AGX_U32 AperturePageCount, APPLE_AGX_U64 ApertureBase,
    APPLE_AGX_U64 ApertureSize, APPLE_AGX_U64 LocalGpuVaBase,
    APPLE_AGX_U64 LocalBytes);
APPLE_AGX_BOOL AdmissionMemoryMarkUatReady(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U32 Context,
    APPLE_AGX_U64 LeafSize, APPLE_AGX_U64 MappedGpuVaBase,
    APPLE_AGX_U64 MappedBytes);
APPLE_AGX_BOOL
AdmissionMemoryMarkPagingReady(ADMISSION_MEMORY_CONTRACT *Memory);
APPLE_AGX_BOOL AdmissionMemoryReady(const ADMISSION_MEMORY_CONTRACT *Memory);
APPLE_AGX_BOOL AdmissionMemoryReserveBackendTail(
    ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U64 AllocationBytes, APPLE_AGX_U64 BackendBytes);
APPLE_AGX_BOOL AdmissionMemoryBackendRange(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U64 *GpuVa, APPLE_AGX_U64 *Bytes);
APPLE_AGX_BOOL AdmissionMemoryResolveLocalView(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U64 AllocationSegmentAddress,
    APPLE_AGX_U64 AllocationSize,
    APPLE_AGX_U64 AllocationOffset,
    void *LocalCpuBase,
    APPLE_AGX_U64 LocalHostPhysicalBase,
    ADMISSION_LOCAL_MEMORY_VIEW *View);
APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryMapAperturePages(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ApertureByteOffset,
    const APPLE_AGX_U64 *PhysicalPages, APPLE_AGX_U32 PhysicalPageCount);
APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryUnmapAperturePages(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ApertureByteOffset,
    APPLE_AGX_U32 PageCount, APPLE_AGX_U64 DummyPage);
APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryMapAperture64K(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ApertureByteOffset,
    const APPLE_AGX_U64 *PhysicalPages, APPLE_AGX_U32 PhysicalPageCount);
APPLE_AGX_SOFTWARE_APERTURE_RESULT AdmissionMemoryUnmapAperture64K(
    ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U64 ApertureByteOffset,
    APPLE_AGX_U64 DummyPage);
APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT AdmissionMemoryLocalAddressToGpuVa(
    const ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U32 SegmentId,
    APPLE_AGX_U64 AllocationSegmentAddress,
    APPLE_AGX_U64 AllocationSize, APPLE_AGX_U64 AllocationOffset,
    APPLE_AGX_U64 *GpuVa);
APPLE_AGX_APERTURE_RESULT AdmissionMemoryPlanAperture64K(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U64 ApertureByteOffset,
    const APPLE_AGX_U64 *SystemPageAddresses,
    APPLE_AGX_U32 SystemPageCount, APPLE_AGX_APERTURE_RUN *Runs,
    APPLE_AGX_U32 RunCapacity, APPLE_AGX_U32 *RunCount);
APPLE_AGX_PHYSICAL_PAGING_RESULT AdmissionMemoryPlanTransfer(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U32 SourceSegmentId, APPLE_AGX_U64 SourceAddress,
    APPLE_AGX_U32 DestinationSegmentId,
    APPLE_AGX_U64 DestinationAddress, APPLE_AGX_U64 TransferOffset,
    APPLE_AGX_U64 SystemOffset, APPLE_AGX_U64 Bytes,
    APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan);
APPLE_AGX_PHYSICAL_PAGING_RESULT AdmissionMemoryPlanFill(
    const ADMISSION_MEMORY_CONTRACT *Memory,
    APPLE_AGX_U32 DestinationSegmentId,
    APPLE_AGX_U64 DestinationAddress, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U32 Pattern, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan);
APPLE_AGX_PHYSICAL_PAGING_RESULT AdmissionMemoryPlanDiscard(
    const ADMISSION_MEMORY_CONTRACT *Memory, APPLE_AGX_U32 SegmentId,
    APPLE_AGX_U64 SegmentAddress, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan);

#endif /* APPLE_AGX_RENDER_MEMORY_H */
