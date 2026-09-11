#ifndef APPLE_AGX_LOCAL_SEGMENT_H
#define APPLE_AGX_LOCAL_SEGMENT_H

#include "apple_agx_state.h"

typedef enum _APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT {
  AppleAgxLocalSegmentAddressOk = 0,
  AppleAgxLocalSegmentAddressInvalidArgument,
  AppleAgxLocalSegmentAddressWrongSegment,
  AppleAgxLocalSegmentAddressMisaligned,
  AppleAgxLocalSegmentAddressOutsideSegment,
  AppleAgxLocalSegmentAddressOutsideAllocation,
  AppleAgxLocalSegmentAddressOverflow,
} APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT;

APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT AppleAgxLocalSegmentAddressToGpuVa(
    APPLE_AGX_U32 ExpectedSegmentId, APPLE_AGX_U32 ActualSegmentId,
    APPLE_AGX_U64 SegmentBase, APPLE_AGX_U64 SegmentSize,
    APPLE_AGX_U64 GpuVaBase, APPLE_AGX_U64 AllocationSegmentAddress,
    APPLE_AGX_U64 AllocationSize, APPLE_AGX_U64 AllocationOffset,
    APPLE_AGX_U64 *GpuVa);

#endif /* APPLE_AGX_LOCAL_SEGMENT_H */
