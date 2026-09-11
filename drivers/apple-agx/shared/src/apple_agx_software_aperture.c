#include "apple_agx_software_aperture.h"

#define APPLE_AGX_SOFTWARE_APERTURE_PAGE_SIZE 0x1000ULL

static APPLE_AGX_BOOL AppleAgxSoftwareApertureRangeValid(
    const APPLE_AGX_SOFTWARE_APERTURE *Aperture,
    APPLE_AGX_U32 FirstPage, APPLE_AGX_U32 PageCount) {
  return Aperture != (const APPLE_AGX_SOFTWARE_APERTURE *)0 &&
                 Aperture->Entries !=
                     (APPLE_AGX_SOFTWARE_APERTURE_ENTRY *)0 &&
                 Aperture->PageCount != 0u && PageCount != 0u &&
                 FirstPage < Aperture->PageCount &&
                 PageCount <= Aperture->PageCount - FirstPage
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureInitialize(
    APPLE_AGX_SOFTWARE_APERTURE *Aperture,
    APPLE_AGX_SOFTWARE_APERTURE_ENTRY *Entries, APPLE_AGX_U32 PageCount) {
  APPLE_AGX_U32 index;

  if (Aperture == (APPLE_AGX_SOFTWARE_APERTURE *)0 ||
      Entries == (APPLE_AGX_SOFTWARE_APERTURE_ENTRY *)0 || PageCount == 0u)
    return AppleAgxSoftwareApertureInvalidArgument;
  for (index = 0u; index < PageCount; ++index) {
    Entries[index].PhysicalPage = 0ULL;
    Entries[index].State = (APPLE_AGX_U32)AppleAgxSoftwareApertureEntryEmpty;
    Entries[index].Reserved = 0u;
  }
  Aperture->Entries = Entries;
  Aperture->PageCount = PageCount;
  return AppleAgxSoftwareApertureOk;
}

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureMap(
    APPLE_AGX_SOFTWARE_APERTURE *Aperture, APPLE_AGX_U32 FirstPage,
    const APPLE_AGX_U64 *PhysicalPages, APPLE_AGX_U32 PageCount) {
  APPLE_AGX_U32 index;

  if (PhysicalPages == (const APPLE_AGX_U64 *)0 || PageCount == 0u)
    return AppleAgxSoftwareApertureInvalidArgument;
  if (!AppleAgxSoftwareApertureRangeValid(Aperture, FirstPage, PageCount))
    return AppleAgxSoftwareApertureOutOfRange;
  for (index = 0u; index < PageCount; ++index) {
    if (PhysicalPages[index] == 0ULL)
      return AppleAgxSoftwareApertureInvalidArgument;
    if ((PhysicalPages[index] &
         (APPLE_AGX_SOFTWARE_APERTURE_PAGE_SIZE - 1ULL)) != 0ULL)
      return AppleAgxSoftwareApertureMisaligned;
  }
  for (index = 0u; index < PageCount; ++index) {
    APPLE_AGX_SOFTWARE_APERTURE_ENTRY *entry =
        &Aperture->Entries[FirstPage + index];
    entry->PhysicalPage = PhysicalPages[index];
    entry->State = (APPLE_AGX_U32)AppleAgxSoftwareApertureEntryMapped;
    entry->Reserved = 0u;
  }
  return AppleAgxSoftwareApertureOk;
}

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureUnmap(
    APPLE_AGX_SOFTWARE_APERTURE *Aperture, APPLE_AGX_U32 FirstPage,
    APPLE_AGX_U32 PageCount, APPLE_AGX_U64 DummyPage) {
  APPLE_AGX_U32 index;

  if (PageCount == 0u || DummyPage == 0ULL)
    return AppleAgxSoftwareApertureInvalidArgument;
  if ((DummyPage & (APPLE_AGX_SOFTWARE_APERTURE_PAGE_SIZE - 1ULL)) != 0ULL)
    return AppleAgxSoftwareApertureMisaligned;
  if (!AppleAgxSoftwareApertureRangeValid(Aperture, FirstPage, PageCount))
    return AppleAgxSoftwareApertureOutOfRange;
  for (index = 0u; index < PageCount; ++index) {
    APPLE_AGX_SOFTWARE_APERTURE_ENTRY *entry =
        &Aperture->Entries[FirstPage + index];
    entry->PhysicalPage = DummyPage;
    entry->State = (APPLE_AGX_U32)AppleAgxSoftwareApertureEntryDummy;
    entry->Reserved = 0u;
  }
  return AppleAgxSoftwareApertureOk;
}

APPLE_AGX_SOFTWARE_APERTURE_RESULT AppleAgxSoftwareApertureResolve(
    const APPLE_AGX_SOFTWARE_APERTURE *Aperture,
    APPLE_AGX_U64 ApertureByteOffset, APPLE_AGX_U64 *PhysicalAddress) {
  APPLE_AGX_U64 pageIndex;
  APPLE_AGX_U64 pageOffset;
  const APPLE_AGX_SOFTWARE_APERTURE_ENTRY *entry;

  if (PhysicalAddress == (APPLE_AGX_U64 *)0)
    return AppleAgxSoftwareApertureInvalidArgument;
  *PhysicalAddress = 0ULL;
  if (Aperture == (const APPLE_AGX_SOFTWARE_APERTURE *)0 ||
      Aperture->Entries == (APPLE_AGX_SOFTWARE_APERTURE_ENTRY *)0 ||
      Aperture->PageCount == 0u)
    return AppleAgxSoftwareApertureInvalidArgument;
  pageIndex = ApertureByteOffset / APPLE_AGX_SOFTWARE_APERTURE_PAGE_SIZE;
  pageOffset = ApertureByteOffset &
               (APPLE_AGX_SOFTWARE_APERTURE_PAGE_SIZE - 1ULL);
  if (pageIndex >= (APPLE_AGX_U64)Aperture->PageCount)
    return AppleAgxSoftwareApertureOutOfRange;
  entry = &Aperture->Entries[(APPLE_AGX_U32)pageIndex];
  if (entry->State == (APPLE_AGX_U32)AppleAgxSoftwareApertureEntryEmpty ||
      entry->PhysicalPage == 0ULL)
    return AppleAgxSoftwareApertureNotMapped;
  if (entry->State != (APPLE_AGX_U32)AppleAgxSoftwareApertureEntryMapped &&
      entry->State != (APPLE_AGX_U32)AppleAgxSoftwareApertureEntryDummy)
    return AppleAgxSoftwareApertureInvalidArgument;
  *PhysicalAddress = entry->PhysicalPage + pageOffset;
  return entry->State == (APPLE_AGX_U32)AppleAgxSoftwareApertureEntryDummy
             ? AppleAgxSoftwareApertureDummy
             : AppleAgxSoftwareApertureOk;
}
