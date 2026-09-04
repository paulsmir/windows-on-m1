#include "apple_agx_exp208_adapter.h"

#define APPLE_AGX_EXP208_NULL ((void *)0)
#define APPLE_AGX_EXP208_PAGE_32K 0x8000ULL
#define APPLE_AGX_EXP208_TA_FLAG_BIT63 0x8000000000000000ULL
#define APPLE_AGX_EXP208_TA_FLAG_BITS60_61 0x3000000000000000ULL
#define APPLE_AGX_EXP208_TA_FLAG_BIT50 0x0004000000000000ULL

APPLE_AGX_BACKEND_U32 AppleAgxExp208SupportedGdiPrimitiveMask(void) {
  /*
   * EXP208 currently proves event, stamp, queue-pointer and address
   * relocation patches only.  It has no documented binding for GDI surfaces,
   * rectangles, ROPs, blending, color keys, or ClearType inputs.
   */
  return APPLE_AGX_EXP208_SUPPORTED_GDI_PRIMITIVE_MASK;
}

static void AppleAgxExp208PutU32(unsigned char *Destination,
                                 APPLE_AGX_BACKEND_U32 Value) {
  Destination[0] = (unsigned char)(Value & 0xffu);
  Destination[1] = (unsigned char)((Value >> 8) & 0xffu);
  Destination[2] = (unsigned char)((Value >> 16) & 0xffu);
  Destination[3] = (unsigned char)((Value >> 24) & 0xffu);
}

static void AppleAgxExp208PutU64(unsigned char *Destination,
                                 APPLE_AGX_BACKEND_U64 Value) {
  AppleAgxExp208PutU32(Destination, (APPLE_AGX_BACKEND_U32)Value);
  AppleAgxExp208PutU32(Destination + 4,
                       (APPLE_AGX_BACKEND_U32)(Value >> 32));
}

static APPLE_AGX_BACKEND_BOOL AppleAgxExp208EncodingBitsValid(
    APPLE_AGX_BACKEND_U32 Encoding, APPLE_AGX_BACKEND_U64 EncodingBits) {
  if (Encoding == (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationExactU64 ||
      Encoding ==
          (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationGpuVaPage32kU32)
    return EncodingBits == 0ULL ? APPLE_AGX_BACKEND_TRUE
                                : APPLE_AGX_BACKEND_FALSE;
  if (Encoding !=
      (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationTaFlaggedGpuVa)
    return APPLE_AGX_BACKEND_FALSE;
  return EncodingBits == APPLE_AGX_EXP208_TA_FLAG_BIT63 ||
                 EncodingBits == APPLE_AGX_EXP208_TA_FLAG_BITS60_61 ||
                 EncodingBits == APPLE_AGX_EXP208_TA_FLAG_BIT50
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxExp208ParametersValid(
    const APPLE_AGX_EXP208_JOB_PARAMETERS *Parameters) {
  return Parameters != APPLE_AGX_EXP208_NULL &&
         Parameters->ArenaGpuAddress != 0ULL &&
         (Parameters->ArenaGpuAddress &
          (APPLE_AGX_EXP208_ARENA_ALIGNMENT - 1u)) == 0ULL &&
         Parameters->ArenaBytes == APPLE_AGX_EXP208_ARENA_BYTES &&
         Parameters->ArenaGpuAddress <=
             ~(APPLE_AGX_BACKEND_U64)0 - Parameters->ArenaBytes &&
         Parameters->TaEvent < 128u && Parameters->D3Event < 128u &&
         Parameters->TaEvent != Parameters->D3Event &&
         Parameters->TaExpectedStamp != 0u &&
         Parameters->D3ExpectedStamp != 0u &&
         Parameters->TaExpectedDonePointer < APPLE_AGX_EXP208_QUEUE_CAPACITY &&
         Parameters->D3ExpectedDonePointer < APPLE_AGX_EXP208_QUEUE_CAPACITY;
}

APPLE_AGX_BACKEND_BOOL
AppleAgxExp208GetManifest(APPLE_AGX_EXP208_MANIFEST *Manifest) {
  if (Manifest == APPLE_AGX_EXP208_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  Manifest->ArenaGpuBase = APPLE_AGX_EXP208_ARENA_GPU_BASE;
  Manifest->ArenaBytes = APPLE_AGX_EXP208_ARENA_BYTES;
  Manifest->Alignment = APPLE_AGX_EXP208_ARENA_ALIGNMENT;
  Manifest->TaRoots[0] = APPLE_AGX_EXP208_TA_WORK_ROOT;
  Manifest->TaRoots[1] = APPLE_AGX_EXP208_TA_SECONDARY_ROOT;
  Manifest->D3Roots[0] = APPLE_AGX_EXP208_D3_WORK_ROOT;
  Manifest->D3Roots[1] = APPLE_AGX_EXP208_D3_SECONDARY_ROOT;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxExp208ValidateRelocations(
    const APPLE_AGX_EXP208_JOB_PARAMETERS *Parameters,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_BACKEND_U32 ObjectCount,
    const APPLE_AGX_EXP208_RELOCATION *Relocations,
    APPLE_AGX_BACKEND_U32 RelocationCount) {
  APPLE_AGX_BACKEND_U32 index;

  if (Parameters == APPLE_AGX_EXP208_NULL ||
      Objects == APPLE_AGX_EXP208_NULL ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      Relocations == APPLE_AGX_EXP208_NULL || RelocationCount == 0u)
    return APPLE_AGX_BACKEND_FALSE;
  if (Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].GpuVa !=
          Parameters->ArenaGpuAddress ||
      Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].Size !=
          Parameters->ArenaBytes ||
      Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].Data ==
          APPLE_AGX_EXP208_NULL ||
      Objects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa !=
          Parameters->ArenaGpuAddress + 0x80000ULL ||
      Objects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa !=
          Parameters->ArenaGpuAddress + 0x98000ULL ||
      Objects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa !=
          Parameters->ArenaGpuAddress + 0x70000ULL ||
      Objects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa !=
          Parameters->ArenaGpuAddress + 0x90000ULL)
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < RelocationCount; ++index) {
    const APPLE_AGX_EXP208_RELOCATION *relocation = &Relocations[index];
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *source;
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *target;
    APPLE_AGX_BACKEND_U32 width;
    APPLE_AGX_BACKEND_U64 address;

    if (relocation->SourceObject >= ObjectCount ||
        relocation->TargetObject >= ObjectCount ||
        relocation->AddressSpace >
            (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationPhysical ||
        relocation->Encoding >
            (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationGpuVaPage32kU32 ||
        !AppleAgxExp208EncodingBitsValid(relocation->Encoding,
                                         relocation->EncodingBits))
      return APPLE_AGX_BACKEND_FALSE;
    source = &Objects[relocation->SourceObject];
    target = &Objects[relocation->TargetObject];
    width = relocation->Encoding ==
                    (APPLE_AGX_BACKEND_U32)
                        AppleAgxExp208RelocationGpuVaPage32kU32
                ? 4u
                : 8u;
    if (source->Data == APPLE_AGX_EXP208_NULL || source->Size == 0u ||
        target->Data == APPLE_AGX_EXP208_NULL || target->Size == 0u ||
        relocation->SourceOffset > source->Size ||
        width > source->Size - relocation->SourceOffset ||
        relocation->TargetOffset >= target->Size)
      return APPLE_AGX_BACKEND_FALSE;
    address = relocation->AddressSpace ==
                      (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationPhysical
                  ? target->PhysicalAddress
                  : target->GpuVa;
    if (address == 0ULL ||
        address > ~((APPLE_AGX_BACKEND_U64)relocation->TargetOffset))
      return APPLE_AGX_BACKEND_FALSE;
    address += relocation->TargetOffset;
    if (relocation->Encoding ==
            (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationTaFlaggedGpuVa &&
        (relocation->AddressSpace !=
             (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationGpuVa ||
         (address & relocation->EncodingBits) != 0ULL))
      return APPLE_AGX_BACKEND_FALSE;
    if (relocation->Encoding ==
            (APPLE_AGX_BACKEND_U32)
                AppleAgxExp208RelocationGpuVaPage32kU32 &&
        (relocation->AddressSpace !=
             (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationGpuVa ||
         relocation->TargetOffset != 0u ||
         (address & (APPLE_AGX_EXP208_PAGE_32K - 1ULL)) != 0ULL ||
         address / APPLE_AGX_EXP208_PAGE_32K > 0xffffffffULL))
      return APPLE_AGX_BACKEND_FALSE;
  }
  return APPLE_AGX_BACKEND_TRUE;
}

static void AppleAgxExp208ApplyRelocations(
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    const APPLE_AGX_EXP208_RELOCATION *Relocations,
    APPLE_AGX_BACKEND_U32 RelocationCount) {
  APPLE_AGX_BACKEND_U32 index;

  for (index = 0u; index < RelocationCount; ++index) {
    const APPLE_AGX_EXP208_RELOCATION *relocation = &Relocations[index];
    APPLE_AGX_EXP208_RELOCATION_OBJECT *source =
        &Objects[relocation->SourceObject];
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *target =
        &Objects[relocation->TargetObject];
    APPLE_AGX_BACKEND_U64 address =
        relocation->AddressSpace ==
                (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationPhysical
            ? target->PhysicalAddress
            : target->GpuVa;
    address += relocation->TargetOffset;
    if (relocation->Encoding ==
        (APPLE_AGX_BACKEND_U32)AppleAgxExp208RelocationGpuVaPage32kU32) {
      AppleAgxExp208PutU32(
          source->Data + relocation->SourceOffset,
          (APPLE_AGX_BACKEND_U32)(address / APPLE_AGX_EXP208_PAGE_32K));
    } else {
      AppleAgxExp208PutU64(source->Data + relocation->SourceOffset,
                          address | relocation->EncodingBits);
    }
  }
}

APPLE_AGX_BACKEND_BOOL AppleAgxExp208BuildJob(
    const APPLE_AGX_EXP208_JOB_PARAMETERS *Parameters,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_BACKEND_U32 ObjectCount,
    const APPLE_AGX_EXP208_RELOCATION *Relocations,
    APPLE_AGX_BACKEND_U32 RelocationCount,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_BACKEND_JOB_IMAGE candidate;

  if (Job == APPLE_AGX_EXP208_NULL ||
      !AppleAgxExp208ParametersValid(Parameters) ||
      !AppleAgxExp208ValidateRelocations(
          Parameters, Objects, ObjectCount, Relocations,
          RelocationCount) ||
      ObjectCount <= APPLE_AGX_EXP208_TA_WORK_OBJECT ||
      Objects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa == 0ULL ||
      Objects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa == 0ULL ||
      Objects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa == 0ULL ||
      Objects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa == 0ULL)
    return APPLE_AGX_BACKEND_FALSE;
  candidate.TaWorkAddresses[0] =
      Objects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa;
  candidate.TaWorkAddresses[1] =
      Objects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa;
  candidate.D3WorkAddresses[0] =
      Objects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa;
  candidate.D3WorkAddresses[1] =
      Objects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa;
  candidate.TaWorkAddressCount = APPLE_AGX_EXP208_ROOT_COUNT;
  candidate.D3WorkAddressCount = APPLE_AGX_EXP208_ROOT_COUNT;
  candidate.TaEvent = Parameters->TaEvent;
  candidate.D3Event = Parameters->D3Event;
  candidate.TaExpectedStamp = Parameters->TaExpectedStamp;
  candidate.D3ExpectedStamp = Parameters->D3ExpectedStamp;
  candidate.TaExpectedDonePointer = Parameters->TaExpectedDonePointer;
  candidate.D3ExpectedDonePointer = Parameters->D3ExpectedDonePointer;
  AppleAgxExp208ApplyRelocations(Objects, Relocations, RelocationCount);
  *Job = candidate;
  return APPLE_AGX_BACKEND_TRUE;
}
