#include "apple_agx_relocation.h"

#define APPLE_AGX_RELOCATION_NULL ((void *)0)
#define APPLE_AGX_BM_PAGE_SIZE 0x8000ULL
#define APPLE_AGX_TA_FLAG_BIT63 0x8000000000000000ULL
#define APPLE_AGX_TA_FLAG_BITS60_61 0x3000000000000000ULL
#define APPLE_AGX_TA_FLAG_BIT50 0x0004000000000000ULL

static APPLE_AGX_BOOL AppleAgxRelocationEncodingBitsValid(
    APPLE_AGX_U32 Encoding, APPLE_AGX_U64 EncodingBits) {
  if (Encoding == (APPLE_AGX_U32)AppleAgxRelocationExactU64 ||
      Encoding == (APPLE_AGX_U32)AppleAgxRelocationGpuVaPage32kU32)
    return EncodingBits == 0ULL ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
  if (Encoding != (APPLE_AGX_U32)AppleAgxRelocationTaFlaggedGpuVa)
    return APPLE_AGX_FALSE;
  return EncodingBits == APPLE_AGX_TA_FLAG_BIT63 ||
                 EncodingBits == APPLE_AGX_TA_FLAG_BITS60_61 ||
                 EncodingBits == APPLE_AGX_TA_FLAG_BIT50
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static void AppleAgxRelocationPutU32(unsigned char *Destination,
                                     APPLE_AGX_U32 Value) {
  Destination[0] = (unsigned char)(Value & 0xffu);
  Destination[1] = (unsigned char)((Value >> 8) & 0xffu);
  Destination[2] = (unsigned char)((Value >> 16) & 0xffu);
  Destination[3] = (unsigned char)((Value >> 24) & 0xffu);
}

static void AppleAgxRelocationPutU64(unsigned char *Destination,
                                     APPLE_AGX_U64 Value) {
  AppleAgxRelocationPutU32(Destination, (APPLE_AGX_U32)Value);
  AppleAgxRelocationPutU32(Destination + 4,
                           (APPLE_AGX_U32)(Value >> 32));
}

APPLE_AGX_BOOL AppleAgxApplyRelocations(
    APPLE_AGX_RELOCATION_OBJECT *Objects, APPLE_AGX_U32 ObjectCount,
    const APPLE_AGX_RELOCATION *Relocations,
    APPLE_AGX_U32 RelocationCount) {
  APPLE_AGX_U32 index;

  if (Objects == APPLE_AGX_RELOCATION_NULL || ObjectCount == 0u ||
      (RelocationCount != 0u && Relocations == APPLE_AGX_RELOCATION_NULL))
    return APPLE_AGX_FALSE;

  /* Validate the complete batch before modifying any submitted bytes. */
  for (index = 0u; index < RelocationCount; ++index) {
    const APPLE_AGX_RELOCATION *relocation = &Relocations[index];
    const APPLE_AGX_RELOCATION_OBJECT *source;
    const APPLE_AGX_RELOCATION_OBJECT *target;
    APPLE_AGX_U32 width;
    APPLE_AGX_U64 address;

    if (relocation->SourceObject >= ObjectCount ||
        relocation->TargetObject >= ObjectCount ||
        relocation->AddressSpace > (APPLE_AGX_U32)AppleAgxRelocationPhysical ||
        relocation->Encoding >
            (APPLE_AGX_U32)AppleAgxRelocationGpuVaPage32kU32 ||
        !AppleAgxRelocationEncodingBitsValid(relocation->Encoding,
                                             relocation->EncodingBits))
      return APPLE_AGX_FALSE;
    source = &Objects[relocation->SourceObject];
    target = &Objects[relocation->TargetObject];
    width = relocation->Encoding ==
                    (APPLE_AGX_U32)AppleAgxRelocationGpuVaPage32kU32
                ? 4u
                : 8u;
    if (source->Data == APPLE_AGX_RELOCATION_NULL || source->Size == 0u ||
        target->Data == APPLE_AGX_RELOCATION_NULL || target->Size == 0u ||
        relocation->SourceOffset > source->Size ||
        width > source->Size - relocation->SourceOffset ||
        relocation->TargetOffset >= target->Size)
      return APPLE_AGX_FALSE;
    address = relocation->AddressSpace ==
                      (APPLE_AGX_U32)AppleAgxRelocationPhysical
                  ? target->PhysicalAddress
                  : target->GpuVa;
    if (address == 0ULL || address > ~((APPLE_AGX_U64)relocation->TargetOffset))
      return APPLE_AGX_FALSE;
    address += relocation->TargetOffset;
    if (relocation->Encoding ==
            (APPLE_AGX_U32)AppleAgxRelocationTaFlaggedGpuVa &&
        (relocation->AddressSpace !=
             (APPLE_AGX_U32)AppleAgxRelocationGpuVa ||
         (address & relocation->EncodingBits) != 0ULL))
      return APPLE_AGX_FALSE;
    if (relocation->Encoding ==
            (APPLE_AGX_U32)AppleAgxRelocationGpuVaPage32kU32 &&
        (relocation->AddressSpace !=
             (APPLE_AGX_U32)AppleAgxRelocationGpuVa ||
         relocation->TargetOffset != 0u ||
         (address & (APPLE_AGX_BM_PAGE_SIZE - 1ULL)) != 0ULL ||
         address / APPLE_AGX_BM_PAGE_SIZE > 0xffffFFFFULL))
      return APPLE_AGX_FALSE;
  }

  for (index = 0u; index < RelocationCount; ++index) {
    const APPLE_AGX_RELOCATION *relocation = &Relocations[index];
    APPLE_AGX_RELOCATION_OBJECT *source =
        &Objects[relocation->SourceObject];
    const APPLE_AGX_RELOCATION_OBJECT *target =
        &Objects[relocation->TargetObject];
    APPLE_AGX_U64 address =
        relocation->AddressSpace == (APPLE_AGX_U32)AppleAgxRelocationPhysical
            ? target->PhysicalAddress
            : target->GpuVa;
    address += relocation->TargetOffset;
    if (relocation->Encoding ==
        (APPLE_AGX_U32)AppleAgxRelocationGpuVaPage32kU32) {
      AppleAgxRelocationPutU32(
          source->Data + relocation->SourceOffset,
          (APPLE_AGX_U32)(address / APPLE_AGX_BM_PAGE_SIZE));
    } else {
      AppleAgxRelocationPutU64(
          source->Data + relocation->SourceOffset,
          address | relocation->EncodingBits);
    }
  }
  return APPLE_AGX_TRUE;
}
