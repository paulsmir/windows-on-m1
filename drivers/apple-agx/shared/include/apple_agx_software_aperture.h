#ifndef APPLE_AGX_SOFTWARE_APERTURE_H
#define APPLE_AGX_SOFTWARE_APERTURE_H

#include "apple_agx_state.h"

typedef enum _APPLE_AGX_SOFTWARE_APERTURE_RESULT {
  AppleAgxSoftwareApertureOk = 0,
  AppleAgxSoftwareApertureDummy,
  AppleAgxSoftwareApertureInvalidArgument,
  AppleAgxSoftwareApertureMisaligned,
  AppleAgxSoftwareApertureOutOfRange,
  AppleAgxSoftwareApertureNotMapped,
} APPLE_AGX_SOFTWARE_APERTURE_RESULT;

typedef enum _APPLE_AGX_SOFTWARE_APERTURE_ENTRY_STATE {
  AppleAgxSoftwareApertureEntryEmpty = 0,
  AppleAgxSoftwareApertureEntryMapped,
  AppleAgxSoftwareApertureEntryDummy,
} APPLE_AGX_SOFTWARE_APERTURE_ENTRY_STATE;

typedef struct _APPLE_AGX_SOFTWARE_APERTURE_ENTRY {
  APPLE_AGX_U64 PhysicalPage;
  APPLE_AGX_U32 State;
  APPLE_AGX_U32 Reserved;
} APPLE_AGX_SOFTWARE_APERTURE_ENTRY;

typedef struct _APPLE_AGX_SOFTWARE_APERTURE {
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY *Entries;
  APPLE_AGX_U32 PageCount;
} APPLE_AGX_SOFTWARE_APERTURE;

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureInitialize(
    APPLE_AGX_SOFTWARE_APERTURE *Aperture,
    APPLE_AGX_SOFTWARE_APERTURE_ENTRY *Entries, APPLE_AGX_U32 PageCount);

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureMap(
    APPLE_AGX_SOFTWARE_APERTURE *Aperture, APPLE_AGX_U32 FirstPage,
    const APPLE_AGX_U64 *PhysicalPages, APPLE_AGX_U32 PageCount);

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureUnmap(
    APPLE_AGX_SOFTWARE_APERTURE *Aperture, APPLE_AGX_U32 FirstPage,
    APPLE_AGX_U32 PageCount, APPLE_AGX_U64 DummyPage);

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureResolve(
    const APPLE_AGX_SOFTWARE_APERTURE *Aperture,
    APPLE_AGX_U64 ApertureByteOffset, APPLE_AGX_U64 *PhysicalAddress);

#endif /* APPLE_AGX_SOFTWARE_APERTURE_H */
