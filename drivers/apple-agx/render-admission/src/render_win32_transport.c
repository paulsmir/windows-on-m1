#include "render_win32_transport.h"

#define ADMISSION_WIN32_NULL ((void *)0)
#define ADMISSION_WIN32_ACCESS_MASK                                          \
  ((APPLE_AGX_U32)AppleAgxWin32AccessRead |                                 \
   (APPLE_AGX_U32)AppleAgxWin32AccessWrite |                                \
   (APPLE_AGX_U32)AppleAgxWin32AccessExecute)

static int AdmissionWin32RangesOverlap(
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *Left,
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *Right) {
  APPLE_AGX_U64 leftEnd = Left->Offset + Left->Bytes;
  APPLE_AGX_U64 rightEnd = Right->Offset + Right->Bytes;
  return Left->Offset < rightEnd && Right->Offset < leftEnd;
}

ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32ContextCreateValidate(
    const void *PrivateData, APPLE_AGX_U32 PrivateDataBytes,
    APPLE_AGX_BOOL SystemOrGdi, APPLE_AGX_BOOL LegacyQualificationAllowed,
    APPLE_AGX_U32 *Generation, APPLE_AGX_BOOL *Win32Transport) {
  const ADMISSION_WIN32_CONTEXT_CREATE *create =
      (const ADMISSION_WIN32_CONTEXT_CREATE *)PrivateData;
  APPLE_AGX_U32 generation;
  APPLE_AGX_BOOL win32;
  if (Generation == ADMISSION_WIN32_NULL ||
      Win32Transport == ADMISSION_WIN32_NULL)
    return AdmissionWin32TransportArgument;
  if (SystemOrGdi) {
    if (PrivateData != ADMISSION_WIN32_NULL || PrivateDataBytes != 0u)
      return AdmissionWin32TransportContext;
    generation = 0u;
    win32 = APPLE_AGX_FALSE;
  } else if (PrivateData == ADMISSION_WIN32_NULL && PrivateDataBytes == 0u &&
             LegacyQualificationAllowed) {
    generation = 0u;
    win32 = APPLE_AGX_FALSE;
  } else {
    if (create == ADMISSION_WIN32_NULL ||
        PrivateDataBytes != sizeof(*create) ||
        create->Magic != ADMISSION_WIN32_CONTEXT_MAGIC ||
        create->Version != ADMISSION_WIN32_CONTEXT_VERSION ||
        create->Bytes != sizeof(*create) || create->Generation == 0u ||
        create->Reserved != 0u)
      return AdmissionWin32TransportContext;
    generation = create->Generation;
    win32 = APPLE_AGX_TRUE;
  }
  *Generation = generation;
  *Win32Transport = win32;
  return AdmissionWin32TransportSuccess;
}

ADMISSION_WIN32_TRANSPORT_RESULT AdmissionWin32ValidateReferences(
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    APPLE_AGX_U32 ExpectedGeneration,
    ADMISSION_WIN32_LOOKUP_ALLOCATION Lookup, void *LookupContext,
    ADMISSION_WIN32_ALLOCATION_FACT *Facts,
    APPLE_AGX_U32 FactCapacity) {
  ADMISSION_WIN32_ALLOCATION_FACT local[
      APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 other;

  if (View == ADMISSION_WIN32_NULL || View->Header == ADMISSION_WIN32_NULL ||
      View->References == ADMISSION_WIN32_NULL ||
      ExpectedGeneration == 0u || Lookup == ADMISSION_WIN32_NULL ||
      Facts == ADMISSION_WIN32_NULL || View->Header->ReferenceCount == 0u ||
      View->Header->ReferenceCount > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
      FactCapacity < View->Header->ReferenceCount)
    return AdmissionWin32TransportArgument;
  if (View->Header->Generation != ExpectedGeneration)
    return AdmissionWin32TransportStaleGeneration;

  for (index = 0u; index < View->Header->ReferenceCount; ++index) {
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *reference =
        &View->References[index];
    APPLE_AGX_U64 requiredBytes = 0ULL;
    if (!Lookup(LookupContext, reference->AllocationIndex, &local[index]) ||
        local[index].AllocationToken == 0ULL || local[index].Bytes == 0ULL)
      return AdmissionWin32TransportLookup;
    if (local[index].Generation != ExpectedGeneration)
      return AdmissionWin32TransportStaleGeneration;
    if ((reference->Offset & 3ULL) != 0ULL ||
        (reference->Bytes & 3ULL) != 0ULL)
      return AdmissionWin32TransportAlignment;
    if (reference->Bytes == 0ULL || reference->Offset > local[index].Bytes ||
        reference->Bytes > local[index].Bytes - reference->Offset)
      return AdmissionWin32TransportRange;
    if (reference->Access == 0u ||
        (reference->Access & ~ADMISSION_WIN32_ACCESS_MASK) != 0u ||
        ((reference->Access & (APPLE_AGX_U32)AppleAgxWin32AccessWrite) != 0u &&
         !local[index].Writable))
      return AdmissionWin32TransportAccess;
    if ((reference->Access & (APPLE_AGX_U32)AppleAgxWin32AccessWrite) != 0u &&
        local[index].ActiveForDisplay)
      return AdmissionWin32TransportActiveDisplay;
    if (View->Clear != ADMISSION_WIN32_NULL &&
        View->Clear->DestinationReference == index) {
      requiredBytes = (APPLE_AGX_U64)View->Clear->SurfacePitch *
                      (APPLE_AGX_U64)View->Clear->SurfaceHeight;
      if (reference->Bytes < requiredBytes)
        return AdmissionWin32TransportRange;
    }
  }

  for (index = 0u; index < View->Header->ReferenceCount; ++index) {
    for (other = index + 1u; other < View->Header->ReferenceCount; ++other) {
      if (local[index].AllocationToken == local[other].AllocationToken &&
          ((View->References[index].Access |
            View->References[other].Access) &
           (APPLE_AGX_U32)AppleAgxWin32AccessWrite) != 0u &&
          AdmissionWin32RangesOverlap(&View->References[index],
                                      &View->References[other]))
        return AdmissionWin32TransportOverlap;
    }
  }

  for (index = 0u; index < View->Header->ReferenceCount; ++index)
    Facts[index] = local[index];
  return AdmissionWin32TransportSuccess;
}
