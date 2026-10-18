#ifndef APPLE_AGX_APERTURE_H
#define APPLE_AGX_APERTURE_H

#include "apple_agx_state.h"

#define APPLE_AGX_SYSTEM_PAGE_SIZE 0x1000ULL
#define APPLE_AGX_UAT_PAGE_SIZE_16K 0x4000ULL
#define APPLE_AGX_WDDM_PAGE_SIZE_64K 0x10000ULL
#define APPLE_AGX_SYSTEM_PAGES_PER_UAT_PAGE 4u
#define APPLE_AGX_SYSTEM_PAGES_PER_WDDM_PAGE 16u

typedef enum _APPLE_AGX_APERTURE_RESULT {
  AppleAgxApertureResultOk = 0,
  AppleAgxApertureResultInvalidArgument,
  AppleAgxApertureResultAlignment,
  AppleAgxApertureResultRange,
  AppleAgxApertureResultPhysicalLayout,
  AppleAgxApertureResultCapacity,
} APPLE_AGX_APERTURE_RESULT;

typedef struct _APPLE_AGX_APERTURE_RUN {
  APPLE_AGX_U64 GpuVirtualAddress;
  APPLE_AGX_U64 PhysicalAddress;
  APPLE_AGX_U64 Length;
} APPLE_AGX_APERTURE_RUN;

typedef struct _APPLE_AGX_GDI_APERTURE_CANDIDATE {
  APPLE_AGX_U64 ApertureBase;
  APPLE_AGX_U64 ApertureSize;
  APPLE_AGX_U64 MappingAddress;
  APPLE_AGX_U64 MappingLength;
  APPLE_AGX_U64 PageSize;
  APPLE_AGX_BOOL IsAperture;
  APPLE_AGX_BOOL CpuVisible;
  APPLE_AGX_BOOL CacheCoherent;
} APPLE_AGX_GDI_APERTURE_CANDIDATE;

typedef struct _APPLE_AGX_GDI_APERTURE_RECEIPT {
  APPLE_AGX_U64 Address;
  APPLE_AGX_U64 Length;
  APPLE_AGX_U64 PageSize;
} APPLE_AGX_GDI_APERTURE_RECEIPT;

APPLE_AGX_BOOL AppleAgxApertureValidateGdiMapping(
    const APPLE_AGX_GDI_APERTURE_CANDIDATE *Candidate,
    APPLE_AGX_GDI_APERTURE_RECEIPT *Receipt);

/*
 * Converts a 64-KiB-granular WDDM aperture byte range backed by 4-KiB
 * system-memory pages into physically contiguous Apple 16-KiB UAT runs.
 *
 * The caller is responsible for converting the WDDM MDL/ADL fields into the
 * byte offset and ordered page-address array.  This function deliberately
 * refuses any four-page group that cannot form one Apple hardware leaf.  It
 * validates the complete request and output capacity before writing Runs.
 */
APPLE_AGX_APERTURE_RESULT AppleAgxAperturePlan64K(
    APPLE_AGX_U64 ApertureBase, APPLE_AGX_U64 ApertureSize,
    APPLE_AGX_U64 RangeOffsetBytes,
    const APPLE_AGX_U64 *SystemPageAddresses,
    APPLE_AGX_U32 SystemPageCount, APPLE_AGX_APERTURE_RUN *Runs,
    APPLE_AGX_U32 RunCapacity, APPLE_AGX_U32 *RunCount);

#endif /* APPLE_AGX_APERTURE_H */
