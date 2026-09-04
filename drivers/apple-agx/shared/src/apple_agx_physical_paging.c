#include "apple_agx_physical_paging.h"

static APPLE_AGX_BOOL AppleAgxRangeWithin(APPLE_AGX_U64 Offset,
                                          APPLE_AGX_U64 Bytes,
                                          APPLE_AGX_U64 Limit) {
  return Bytes != 0u && Offset <= Limit && Bytes <= Limit - Offset;
}

static APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxLocalOffset(
    APPLE_AGX_U64 Address, APPLE_AGX_U64 AdditionalOffset,
    APPLE_AGX_U64 LocalBase, APPLE_AGX_U64 LocalBytes,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U64 *Offset) {
  APPLE_AGX_U64 relative;
  if (Offset == (APPLE_AGX_U64 *)0 || Address < LocalBase)
    return AppleAgxPhysicalPagingOutOfRange;
  relative = Address - LocalBase;
  if (AdditionalOffset > ~0ULL - relative)
    return AppleAgxPhysicalPagingOutOfRange;
  relative += AdditionalOffset;
  if (!AppleAgxRangeWithin(relative, Bytes, LocalBytes))
    return AppleAgxPhysicalPagingOutOfRange;
  *Offset = relative;
  return AppleAgxPhysicalPagingOk;
}

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingPlanTransfer(
    APPLE_AGX_U32 SourceSegmentId, APPLE_AGX_U64 SourceAddress,
    APPLE_AGX_U32 DestinationSegmentId, APPLE_AGX_U64 DestinationAddress,
    APPLE_AGX_U64 LocalBase, APPLE_AGX_U64 LocalBytes,
    APPLE_AGX_U64 TransferOffset, APPLE_AGX_U64 SystemOffset,
    APPLE_AGX_U64 Bytes, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan) {
  APPLE_AGX_PHYSICAL_PAGING_PLAN candidate;
  APPLE_AGX_PHYSICAL_PAGING_RESULT result;

  if (Plan == (APPLE_AGX_PHYSICAL_PAGING_PLAN *)0 || LocalBytes == 0u ||
      Bytes == 0u)
    return AppleAgxPhysicalPagingInvalidArgument;
  candidate.Kind = AppleAgxPhysicalPagingDiscard;
  candidate.LocalOffset = 0u;
  candidate.SecondLocalOffset = 0u;
  candidate.SystemOffset = SystemOffset;
  candidate.Bytes = Bytes;
  candidate.FillPattern = 0u;

  if (SourceSegmentId == 0u &&
      DestinationSegmentId == APPLE_AGX_PHYSICAL_PAGING_LOCAL_SEGMENT) {
    candidate.Kind = AppleAgxPhysicalPagingUpload;
    result = AppleAgxLocalOffset(DestinationAddress, TransferOffset,
                                 LocalBase, LocalBytes, Bytes,
                                 &candidate.LocalOffset);
  } else if (SourceSegmentId == APPLE_AGX_PHYSICAL_PAGING_LOCAL_SEGMENT &&
             DestinationSegmentId == 0u) {
    candidate.Kind = AppleAgxPhysicalPagingDownload;
    result = AppleAgxLocalOffset(SourceAddress, TransferOffset, LocalBase,
                                 LocalBytes, Bytes, &candidate.LocalOffset);
  } else {
    return AppleAgxPhysicalPagingUnsupportedEndpoint;
  }
  if (result != AppleAgxPhysicalPagingOk)
    return result;
  *Plan = candidate;
  return AppleAgxPhysicalPagingOk;
}

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingPlanFill(
    APPLE_AGX_U32 DestinationSegmentId,
    APPLE_AGX_U64 DestinationAddress, APPLE_AGX_U64 LocalBase,
    APPLE_AGX_U64 LocalBytes, APPLE_AGX_U64 Bytes,
    APPLE_AGX_U32 Pattern, APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan) {
  APPLE_AGX_PHYSICAL_PAGING_PLAN candidate;
  APPLE_AGX_PHYSICAL_PAGING_RESULT result;
  if (Plan == (APPLE_AGX_PHYSICAL_PAGING_PLAN *)0 || LocalBytes == 0u ||
      Bytes == 0u)
    return AppleAgxPhysicalPagingInvalidArgument;
  if (DestinationSegmentId != APPLE_AGX_PHYSICAL_PAGING_LOCAL_SEGMENT)
    return AppleAgxPhysicalPagingUnsupportedEndpoint;
  candidate.Kind = AppleAgxPhysicalPagingFill;
  candidate.SecondLocalOffset = 0u;
  candidate.SystemOffset = 0u;
  candidate.Bytes = Bytes;
  candidate.FillPattern = Pattern;
  result = AppleAgxLocalOffset(DestinationAddress, 0u, LocalBase, LocalBytes,
                               Bytes, &candidate.LocalOffset);
  if (result != AppleAgxPhysicalPagingOk)
    return result;
  *Plan = candidate;
  return AppleAgxPhysicalPagingOk;
}

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingPlanDiscard(
    APPLE_AGX_U32 SegmentId, APPLE_AGX_U64 SegmentAddress,
    APPLE_AGX_U64 LocalBase, APPLE_AGX_U64 LocalBytes,
    APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan) {
  APPLE_AGX_PHYSICAL_PAGING_PLAN candidate;
  APPLE_AGX_PHYSICAL_PAGING_RESULT result;
  if (Plan == (APPLE_AGX_PHYSICAL_PAGING_PLAN *)0 || LocalBytes == 0u)
    return AppleAgxPhysicalPagingInvalidArgument;
  if (SegmentId != APPLE_AGX_PHYSICAL_PAGING_LOCAL_SEGMENT)
    return AppleAgxPhysicalPagingUnsupportedEndpoint;
  candidate.Kind = AppleAgxPhysicalPagingDiscard;
  candidate.SecondLocalOffset = 0u;
  candidate.SystemOffset = 0u;
  candidate.Bytes = 1u;
  candidate.FillPattern = 0u;
  result = AppleAgxLocalOffset(SegmentAddress, 0u, LocalBase, LocalBytes, 1u,
                               &candidate.LocalOffset);
  if (result != AppleAgxPhysicalPagingOk)
    return result;
  candidate.Bytes = 0u;
  *Plan = candidate;
  return AppleAgxPhysicalPagingOk;
}

APPLE_AGX_PHYSICAL_PAGING_RESULT AppleAgxPhysicalPagingExecute(
    const APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan,
    unsigned char *LocalCpuBase, APPLE_AGX_U64 LocalBytes,
    unsigned char *SystemCpuBase, APPLE_AGX_U64 SystemBytes) {
  APPLE_AGX_U64 index;
  if (Plan == (const APPLE_AGX_PHYSICAL_PAGING_PLAN *)0 ||
      LocalCpuBase == (unsigned char *)0 || LocalBytes == 0u)
    return AppleAgxPhysicalPagingInvalidArgument;
  if (Plan->Kind == AppleAgxPhysicalPagingDiscard)
    return Plan->LocalOffset < LocalBytes ? AppleAgxPhysicalPagingOk
                                          : AppleAgxPhysicalPagingOutOfRange;
  if (!AppleAgxRangeWithin(Plan->LocalOffset, Plan->Bytes, LocalBytes))
    return AppleAgxPhysicalPagingOutOfRange;
  if (Plan->Kind == AppleAgxPhysicalPagingUpload ||
      Plan->Kind == AppleAgxPhysicalPagingDownload) {
    if (SystemCpuBase == (unsigned char *)0 ||
        !AppleAgxRangeWithin(Plan->SystemOffset, Plan->Bytes, SystemBytes))
      return AppleAgxPhysicalPagingOutOfRange;
    if (Plan->Kind == AppleAgxPhysicalPagingUpload) {
      for (index = 0u; index < Plan->Bytes; ++index)
        LocalCpuBase[Plan->LocalOffset + index] =
            SystemCpuBase[Plan->SystemOffset + index];
    } else {
      for (index = 0u; index < Plan->Bytes; ++index)
        SystemCpuBase[Plan->SystemOffset + index] =
            LocalCpuBase[Plan->LocalOffset + index];
    }
    return AppleAgxPhysicalPagingOk;
  }
  if (Plan->Kind == AppleAgxPhysicalPagingFill) {
    for (index = 0u; index < Plan->Bytes; ++index)
      LocalCpuBase[Plan->LocalOffset + index] =
          (unsigned char)(Plan->FillPattern >> ((index & 3u) * 8u));
    return AppleAgxPhysicalPagingOk;
  }
  return AppleAgxPhysicalPagingInvalidArgument;
}
