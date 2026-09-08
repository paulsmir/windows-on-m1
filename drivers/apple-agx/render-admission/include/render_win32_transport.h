#ifndef APPLE_AGX_RENDER_WIN32_TRANSPORT_H
#define APPLE_AGX_RENDER_WIN32_TRANSPORT_H

#include "apple_agx_win32_abi.h"

typedef struct _ADMISSION_WIN32_ALLOCATION_FACT {
  APPLE_AGX_U64 AllocationToken;
  APPLE_AGX_U64 Bytes;
  APPLE_AGX_U32 SegmentId;
  APPLE_AGX_U32 Writable;
  APPLE_AGX_U32 ActiveForDisplay;
  APPLE_AGX_U32 Generation;
} ADMISSION_WIN32_ALLOCATION_FACT;

typedef int (*ADMISSION_WIN32_LOOKUP_ALLOCATION)(
    void *Context, APPLE_AGX_U32 AllocationIndex,
    ADMISSION_WIN32_ALLOCATION_FACT *Fact);

typedef enum _ADMISSION_WIN32_TRANSPORT_RESULT {
  AdmissionWin32TransportSuccess = 0,
  AdmissionWin32TransportArgument,
  AdmissionWin32TransportStaleGeneration,
  AdmissionWin32TransportLookup,
  AdmissionWin32TransportAlignment,
  AdmissionWin32TransportRange,
  AdmissionWin32TransportAccess,
  AdmissionWin32TransportActiveDisplay,
  AdmissionWin32TransportOverlap,
} ADMISSION_WIN32_TRANSPORT_RESULT;

ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32ValidateReferences(
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    APPLE_AGX_U32 ExpectedGeneration,
    ADMISSION_WIN32_LOOKUP_ALLOCATION Lookup, void *LookupContext,
    ADMISSION_WIN32_ALLOCATION_FACT *Facts,
    APPLE_AGX_U32 FactCapacity);

#endif /* APPLE_AGX_RENDER_WIN32_TRANSPORT_H */
