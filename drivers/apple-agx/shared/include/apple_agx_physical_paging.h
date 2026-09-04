#ifndef APPLE_AGX_PHYSICAL_PAGING_H
#define APPLE_AGX_PHYSICAL_PAGING_H

#include "apple_agx_state.h"

#define APPLE_AGX_PHYSICAL_PAGING_LOCAL_SEGMENT 2u

typedef enum _APPLE_AGX_PHYSICAL_PAGING_KIND {
  AppleAgxPhysicalPagingUpload = 1,
  AppleAgxPhysicalPagingDownload,
  AppleAgxPhysicalPagingLocalCopy,
  AppleAgxPhysicalPagingFill,
  AppleAgxPhysicalPagingDiscard,
} APPLE_AGX_PHYSICAL_PAGING_KIND;

typedef enum _APPLE_AGX_PHYSICAL_PAGING_RESULT {
  AppleAgxPhysicalPagingOk = 0,
  AppleAgxPhysicalPagingInvalidArgument,
  AppleAgxPhysicalPagingUnsupportedEndpoint,
  AppleAgxPhysicalPagingOutOfRange,
} APPLE_AGX_PHYSICAL_PAGING_RESULT;

typedef struct _APPLE_AGX_PHYSICAL_PAGING_PLAN {
  APPLE_AGX_PHYSICAL_PAGING_KIND Kind;
  APPLE_AGX_U64 LocalOffset;
  APPLE_AGX_U64 SecondLocalOffset;
  APPLE_AGX_U64 SystemOffset;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U32 FillPattern;
} APPLE_AGX_PHYSICAL_PAGING_PLAN;

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingPlanTransfer(
    APPLE_AGX_U32 SourceSegmentId, APPLE_AGX_U64 SourceAddress,
    APPLE_AGX_U32 DestinationSegmentId, APPLE_AGX_U64 DestinationAddress,
    APPLE_AGX_U64 LocalBase, APPLE_AGX_U64 LocalBytes,
    APPLE_AGX_U64 TransferOffset, APPLE_AGX_U64 SystemOffset,
    APPLE_AGX_U64 Bytes, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan);

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingPlanFill(
    APPLE_AGX_U32 DestinationSegmentId,
    APPLE_AGX_U64 DestinationAddress, APPLE_AGX_U64 LocalBase,
    APPLE_AGX_U64 LocalBytes, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U32 Pattern, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan);

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingPlanDiscard(
    APPLE_AGX_U32 SegmentId, APPLE_AGX_U64 SegmentAddress,
    APPLE_AGX_U64 LocalBase, APPLE_AGX_U64 LocalBytes,
    APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan);

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingExecute(
    const APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan,
    unsigned char *LocalCpuBase, APPLE_AGX_U64 LocalBytes,
    unsigned char *SystemCpuBase, APPLE_AGX_U64 SystemBytes);

#endif /* APPLE_AGX_PHYSICAL_PAGING_H */
