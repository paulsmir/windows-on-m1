#include "apple_agx_local_segment.h"

#define APPLE_AGX_LOCAL_SEGMENT_PAGE_SIZE 0x10000ULL
#define APPLE_AGX_LOCAL_SEGMENT_U64_MAX (~(APPLE_AGX_U64)0ULL)

static APPLE_AGX_BOOL AppleAgxLocalSegmentAddOverflows(APPLE_AGX_U64 Left,
                                                       APPLE_AGX_U64 Right) {
  return Left > APPLE_AGX_LOCAL_SEGMENT_U64_MAX - Right ? APPLE_AGX_TRUE
                                                        : APPLE_AGX_FALSE;
}

APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT AppleAgxLocalSegmentAddressToGpuVa(
    APPLE_AGX_U32 ExpectedSegmentId, APPLE_AGX_U32 ActualSegmentId,
    APPLE_AGX_U64 SegmentBase, APPLE_AGX_U64 SegmentSize,
    APPLE_AGX_U64 GpuVaBase, APPLE_AGX_U64 AllocationSegmentAddress,
    APPLE_AGX_U64 AllocationSize, APPLE_AGX_U64 AllocationOffset,
    APPLE_AGX_U64 *GpuVa) {
  APPLE_AGX_U64 segmentOffset;
  APPLE_AGX_U64 allocationEnd;

  if (GpuVa == (APPLE_AGX_U64 *)0)
    return AppleAgxLocalSegmentAddressInvalidArgument;
  *GpuVa = 0ULL;

  if (ExpectedSegmentId == 0u || SegmentSize == 0ULL ||
      AllocationSize == 0ULL)
    return AppleAgxLocalSegmentAddressInvalidArgument;
  if (ActualSegmentId != ExpectedSegmentId)
    return AppleAgxLocalSegmentAddressWrongSegment;
  if ((SegmentBase & (APPLE_AGX_LOCAL_SEGMENT_PAGE_SIZE - 1ULL)) != 0ULL ||
      (SegmentSize & (APPLE_AGX_LOCAL_SEGMENT_PAGE_SIZE - 1ULL)) != 0ULL ||
      (GpuVaBase & (APPLE_AGX_LOCAL_SEGMENT_PAGE_SIZE - 1ULL)) != 0ULL ||
      (AllocationSegmentAddress &
       (APPLE_AGX_LOCAL_SEGMENT_PAGE_SIZE - 1ULL)) != 0ULL ||
      (AllocationSize & (APPLE_AGX_LOCAL_SEGMENT_PAGE_SIZE - 1ULL)) != 0ULL)
    return AppleAgxLocalSegmentAddressMisaligned;
  if (AllocationSegmentAddress < SegmentBase)
    return AppleAgxLocalSegmentAddressOutsideSegment;

  segmentOffset = AllocationSegmentAddress - SegmentBase;
  if (segmentOffset >= SegmentSize ||
      AppleAgxLocalSegmentAddOverflows(segmentOffset, AllocationSize))
    return AppleAgxLocalSegmentAddressOutsideSegment;
  allocationEnd = segmentOffset + AllocationSize;
  if (allocationEnd > SegmentSize)
    return AppleAgxLocalSegmentAddressOutsideSegment;
  if (AllocationOffset >= AllocationSize)
    return AppleAgxLocalSegmentAddressOutsideAllocation;
  if (AppleAgxLocalSegmentAddOverflows(GpuVaBase, segmentOffset) ||
      AppleAgxLocalSegmentAddOverflows(GpuVaBase + segmentOffset,
                                       AllocationOffset))
    return AppleAgxLocalSegmentAddressOverflow;

  *GpuVa = GpuVaBase + segmentOffset + AllocationOffset;
  return AppleAgxLocalSegmentAddressOk;
}
