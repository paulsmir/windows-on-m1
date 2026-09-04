#include "apple_agx_aperture.h"

#define APPLE_AGX_APERTURE_NULL ((void *)0)
#define APPLE_AGX_APERTURE_U64_MAX (~(APPLE_AGX_U64)0ULL)

static APPLE_AGX_BOOL AppleAgxApertureAligned(APPLE_AGX_U64 Value,
                                              APPLE_AGX_U64 Alignment) {
  return (Value & (Alignment - 1ULL)) == 0ULL ? APPLE_AGX_TRUE
                                              : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL AppleAgxApertureLeafIsContiguous(
    const APPLE_AGX_U64 *Pages) {
  APPLE_AGX_U32 index;

  if (Pages[0] == 0ULL ||
      !AppleAgxApertureAligned(Pages[0], APPLE_AGX_UAT_PAGE_SIZE_16K))
    return APPLE_AGX_FALSE;
  for (index = 1u; index < APPLE_AGX_SYSTEM_PAGES_PER_UAT_PAGE; ++index) {
    if (Pages[index] !=
        Pages[0] + (APPLE_AGX_U64)index * APPLE_AGX_SYSTEM_PAGE_SIZE)
      return APPLE_AGX_FALSE;
  }
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxApertureValidateGdiMapping(
    const APPLE_AGX_GDI_APERTURE_CANDIDATE *Candidate,
    APPLE_AGX_GDI_APERTURE_RECEIPT *Receipt) {
  APPLE_AGX_U64 apertureEnd;
  APPLE_AGX_U64 mappingEnd;

  if (Receipt == APPLE_AGX_APERTURE_NULL)
    return APPLE_AGX_FALSE;
  Receipt->Address = 0ULL;
  Receipt->Length = 0ULL;
  Receipt->PageSize = 0ULL;
  if (Candidate == APPLE_AGX_APERTURE_NULL ||
      Candidate->IsAperture != APPLE_AGX_TRUE ||
      Candidate->CpuVisible != APPLE_AGX_TRUE ||
      Candidate->CacheCoherent != APPLE_AGX_TRUE ||
      Candidate->PageSize != APPLE_AGX_WDDM_PAGE_SIZE_64K ||
      Candidate->ApertureSize == 0ULL || Candidate->MappingLength == 0ULL ||
      !AppleAgxApertureAligned(Candidate->ApertureBase,
                               APPLE_AGX_WDDM_PAGE_SIZE_64K) ||
      !AppleAgxApertureAligned(Candidate->ApertureSize,
                               APPLE_AGX_WDDM_PAGE_SIZE_64K) ||
      !AppleAgxApertureAligned(Candidate->MappingAddress,
                               APPLE_AGX_WDDM_PAGE_SIZE_64K) ||
      !AppleAgxApertureAligned(Candidate->MappingLength,
                               APPLE_AGX_WDDM_PAGE_SIZE_64K) ||
      Candidate->ApertureBase >
          APPLE_AGX_APERTURE_U64_MAX - Candidate->ApertureSize ||
      Candidate->MappingAddress >
          APPLE_AGX_APERTURE_U64_MAX - Candidate->MappingLength)
    return APPLE_AGX_FALSE;
  apertureEnd = Candidate->ApertureBase + Candidate->ApertureSize;
  mappingEnd = Candidate->MappingAddress + Candidate->MappingLength;
  if (Candidate->MappingAddress < Candidate->ApertureBase ||
      mappingEnd > apertureEnd)
    return APPLE_AGX_FALSE;
  Receipt->Address = Candidate->MappingAddress;
  Receipt->Length = Candidate->MappingLength;
  Receipt->PageSize = Candidate->PageSize;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_APERTURE_RESULT AppleAgxAperturePlan64K(
    APPLE_AGX_U64 ApertureBase, APPLE_AGX_U64 ApertureSize,
    APPLE_AGX_U64 RangeOffsetBytes,
    const APPLE_AGX_U64 *SystemPageAddresses,
    APPLE_AGX_U32 SystemPageCount, APPLE_AGX_APERTURE_RUN *Runs,
    APPLE_AGX_U32 RunCapacity, APPLE_AGX_U32 *RunCount) {
  APPLE_AGX_U64 range_length;
  APPLE_AGX_U32 page_index;
  APPLE_AGX_U32 required_runs = 0u;
  APPLE_AGX_U64 previous_physical = 0ULL;

  if (RunCount != APPLE_AGX_APERTURE_NULL)
    *RunCount = 0u;
  if (SystemPageAddresses == APPLE_AGX_APERTURE_NULL ||
      Runs == APPLE_AGX_APERTURE_NULL ||
      RunCount == APPLE_AGX_APERTURE_NULL || SystemPageCount == 0u ||
      RunCapacity == 0u || ApertureSize == 0ULL)
    return AppleAgxApertureResultInvalidArgument;
  if (!AppleAgxApertureAligned(ApertureBase,
                               APPLE_AGX_WDDM_PAGE_SIZE_64K) ||
      !AppleAgxApertureAligned(ApertureSize,
                               APPLE_AGX_WDDM_PAGE_SIZE_64K) ||
      !AppleAgxApertureAligned(RangeOffsetBytes,
                               APPLE_AGX_WDDM_PAGE_SIZE_64K) ||
      SystemPageCount % APPLE_AGX_SYSTEM_PAGES_PER_WDDM_PAGE != 0u)
    return AppleAgxApertureResultAlignment;
  if ((APPLE_AGX_U64)SystemPageCount >
      APPLE_AGX_APERTURE_U64_MAX / APPLE_AGX_SYSTEM_PAGE_SIZE)
    return AppleAgxApertureResultRange;
  range_length =
      (APPLE_AGX_U64)SystemPageCount * APPLE_AGX_SYSTEM_PAGE_SIZE;
  if (RangeOffsetBytes > ApertureSize || range_length > ApertureSize ||
      RangeOffsetBytes > ApertureSize - range_length ||
      ApertureBase > APPLE_AGX_APERTURE_U64_MAX - RangeOffsetBytes ||
      ApertureBase + RangeOffsetBytes >
          APPLE_AGX_APERTURE_U64_MAX - range_length)
    return AppleAgxApertureResultRange;

  for (page_index = 0u; page_index < SystemPageCount;
       page_index += APPLE_AGX_SYSTEM_PAGES_PER_UAT_PAGE) {
    APPLE_AGX_U64 physical = SystemPageAddresses[page_index];
    if (!AppleAgxApertureLeafIsContiguous(&SystemPageAddresses[page_index]))
      return AppleAgxApertureResultPhysicalLayout;
    if (required_runs == 0u ||
        physical != previous_physical + APPLE_AGX_UAT_PAGE_SIZE_16K)
      ++required_runs;
    previous_physical = physical;
  }
  if (required_runs > RunCapacity)
    return AppleAgxApertureResultCapacity;

  required_runs = 0u;
  previous_physical = 0ULL;
  for (page_index = 0u; page_index < SystemPageCount;
       page_index += APPLE_AGX_SYSTEM_PAGES_PER_UAT_PAGE) {
    APPLE_AGX_U64 physical = SystemPageAddresses[page_index];
    APPLE_AGX_U64 gpu = ApertureBase + RangeOffsetBytes +
                        (APPLE_AGX_U64)page_index *
                            APPLE_AGX_SYSTEM_PAGE_SIZE;
    if (required_runs == 0u ||
        physical != previous_physical + APPLE_AGX_UAT_PAGE_SIZE_16K) {
      Runs[required_runs].GpuVirtualAddress = gpu;
      Runs[required_runs].PhysicalAddress = physical;
      Runs[required_runs].Length = APPLE_AGX_UAT_PAGE_SIZE_16K;
      ++required_runs;
    } else {
      Runs[required_runs - 1u].Length += APPLE_AGX_UAT_PAGE_SIZE_16K;
    }
    previous_physical = physical;
  }
  *RunCount = required_runs;
  return AppleAgxApertureResultOk;
}
