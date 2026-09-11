#include "apple_agx_mapping.h"

static unsigned char AppleAgxMappingStateIsZero(
    const APPLE_AGX_MAPPING_STATE *State) {
  return (unsigned char)(State->SgxBase == 0 && State->AscBase == 0 &&
                         State->SgxPhysicalAddress == 0ULL &&
                         State->SgxLength == 0u && State->Active == 0u);
}

static void AppleAgxMappingClear(APPLE_AGX_MAPPING_STATE *State) {
  State->SgxBase = 0;
  State->AscBase = 0;
  State->SgxPhysicalAddress = 0ULL;
  State->SgxLength = 0u;
  State->Active = 0u;
}

static APPLE_AGX_UAT_RESULT AppleAgxMappingStartWithRanges(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State,
    unsigned long long SgxPhysicalAddress, unsigned int SgxLength,
    unsigned long long AscPhysicalAddress, unsigned int AscLength) {
  unsigned long long asc_offset;
  unsigned char *base = 0;

  if (Io == 0 || State == 0 || Io->Map == 0 || Io->Unmap == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  if (State->Active != 0u) {
    return AppleAgxUatResultAlreadyMapped;
  }
  if (AppleAgxMappingStateIsZero(State) == 0u) {
    return AppleAgxUatResultInvalidArgument;
  }
  if (SgxLength == 0u || AscLength == 0u ||
      SgxPhysicalAddress > ~0ULL - (unsigned long long)SgxLength ||
      AscPhysicalAddress < SgxPhysicalAddress) {
    return AppleAgxUatResultOutOfRange;
  }
  asc_offset = AscPhysicalAddress - SgxPhysicalAddress;
  if (asc_offset > (unsigned long long)SgxLength ||
      (unsigned long long)AscLength >
          (unsigned long long)SgxLength - asc_offset) {
    return AppleAgxUatResultOutOfRange;
  }
  if (Io->Map(Io->Context, SgxPhysicalAddress, SgxLength, &base) == 0u ||
      base == 0) {
    return AppleAgxUatResultAllocationFailed;
  }

  State->SgxBase = base;
  State->AscBase = base + asc_offset;
  State->SgxPhysicalAddress = SgxPhysicalAddress;
  State->SgxLength = SgxLength;
  State->Active = 1u;
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxMappingStart(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State) {
  return AppleAgxMappingStartWithRanges(
      Io, State, J313_AGX_G2_SGX_MMIO_BASE,
      (unsigned int)J313_AGX_G2_SGX_MMIO_SIZE,
      J313_AGX_G2_ASC_MMIO_BASE,
      (unsigned int)J313_AGX_G2_ASC_MMIO_SIZE);
}

APPLE_AGX_UAT_RESULT AppleAgxMappingStop(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State) {
  if (Io == 0 || State == 0 || Io->Unmap == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  if (State->Active == 0u) {
    return AppleAgxMappingStateIsZero(State) != 0u
               ? AppleAgxUatResultOk
               : AppleAgxUatResultInvalidArgument;
  }
  if (State->SgxBase == 0 || State->AscBase == 0 || State->SgxLength == 0u) {
    return AppleAgxUatResultInvalidArgument;
  }
  if (Io->Unmap(Io->Context, State->SgxBase) == 0u) {
    return AppleAgxUatResultAllocationFailed;
  }
  AppleAgxMappingClear(State);
  return AppleAgxUatResultOk;
}

#ifdef APPLE_AGX_MAPPING_TEST
APPLE_AGX_UAT_RESULT AppleAgxMappingStartWithRangesForTest(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State,
    unsigned long long SgxPhysicalAddress, unsigned int SgxLength,
    unsigned long long AscPhysicalAddress, unsigned int AscLength) {
  return AppleAgxMappingStartWithRanges(Io, State, SgxPhysicalAddress,
                                        SgxLength, AscPhysicalAddress,
                                        AscLength);
}
#endif
