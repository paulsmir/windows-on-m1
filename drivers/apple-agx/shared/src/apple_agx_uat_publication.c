#include "apple_agx_uat_publication.h"

static unsigned long long AppleAgxUatPublicationReadU64(
    volatile const unsigned char *source) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)source[index] << (index * 8u);
  return value;
}

static void AppleAgxUatPublicationWriteU64(
    volatile unsigned char *destination, unsigned long long value) {
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    destination[index] = (unsigned char)(value >> (index * 8u));
}

static unsigned char AppleAgxUatPublicationPairValid(
    const APPLE_AGX_UAT_TTBR_PAIR *pair) {
  const unsigned long long address_limit = 1ULL << 40u;
  const unsigned long long address_mask = address_limit - 1ULL;
  const unsigned long long asid_mask = 0xffff000000000000ULL;
  unsigned long long asid;
  if (pair == 0)
    return 0u;
  asid = pair->Ttbr0 & asid_mask;
  if ((pair->Ttbr0 & 0x3fffULL) != 1ULL ||
      (pair->Ttbr1 & 0x3fffULL) != 1ULL ||
      (pair->Ttbr1 & asid_mask) != asid ||
      asid >
          ((unsigned long long)(J313_AGX_G2_UAT_CONTEXT_COUNT - 1u) << 48) ||
      (pair->Ttbr0 & ~(asid_mask | address_mask)) != 0ULL ||
      (pair->Ttbr1 & ~(asid_mask | address_mask)) != 0ULL)
    return 0u;
  return 1u;
}

static unsigned char AppleAgxUatPublicationIoValid(
    const APPLE_AGX_UAT_PUBLICATION_IO *io) {
  return (unsigned char)(io != 0 && io->Map != 0 && io->Barrier != 0 &&
                         io->Unmap != 0);
}

APPLE_AGX_UAT_PUBLICATION_RESULT AppleAgxUatInspectJ313(
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot,
    const APPLE_AGX_UAT_PUBLICATION_IO *Io,
    APPLE_AGX_UAT_ROOT_SNAPSHOT *Roots) {
  APPLE_AGX_UAT_TTBR_PAIR pair;
  volatile unsigned char *mapped = 0;

  if (Roots == 0)
    return AppleAgxUatPublicationResultInvalidArgument;
  Roots->Ttbr0 = 0ULL;
  Roots->Ttbr1 = 0ULL;
  Roots->PairValid = 0u;
  if (Snapshot == 0 || AppleAgxUatPublicationIoValid(Io) == 0u ||
      Snapshot->GpuRegionBase != J313_AGX_G2_GPU_BASE)
    return AppleAgxUatPublicationResultInvalidArgument;
  if (Io->Map(Io->Context, Snapshot->GpuRegionBase,
              (unsigned int)J313_AGX_G2_GPU_SIZE, &mapped) == 0u ||
      mapped == 0)
    return AppleAgxUatPublicationResultMapFailed;

  Io->Barrier(Io->Context);
  pair.Ttbr0 = AppleAgxUatPublicationReadU64(mapped);
  pair.Ttbr1 = AppleAgxUatPublicationReadU64(mapped + 8u);
  Io->Barrier(Io->Context);
  if (Io->Unmap(Io->Context, mapped) == 0u)
    return AppleAgxUatPublicationResultUnmapFailed;

  Roots->Ttbr0 = pair.Ttbr0;
  Roots->Ttbr1 = pair.Ttbr1;
  Roots->PairValid = AppleAgxUatPublicationPairValid(&pair);
  return AppleAgxUatPublicationResultOk;
}

APPLE_AGX_UAT_PUBLICATION_RESULT AppleAgxUatPublishJ313(
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot,
    const APPLE_AGX_UAT_TTBR_PAIR *Pair,
    const APPLE_AGX_UAT_PUBLICATION_IO *Io,
    APPLE_AGX_UAT_PUBLICATION_STATE *State) {
  return AppleAgxUatPublishJ313Context(Snapshot, 0u, Pair, Io, State);
}

APPLE_AGX_UAT_PUBLICATION_RESULT AppleAgxUatPublishJ313Context(
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot, unsigned int Context,
    const APPLE_AGX_UAT_TTBR_PAIR *Pair,
    const APPLE_AGX_UAT_PUBLICATION_IO *Io,
    APPLE_AGX_UAT_PUBLICATION_STATE *State) {
  volatile unsigned char *mapped = 0;
  volatile unsigned char *pairBase;
#ifdef APPLE_AGX_FULL_CONTEXT0_BROKER
  if (Context == 0u) return AppleAgxUatPublicationResultInvalidArgument;
#endif

  if (Snapshot == 0 || State == 0 ||
      AppleAgxUatPublicationIoValid(Io) == 0u || State->Active != 0u ||
      State->MappedBase != 0 ||
      Snapshot->GpuRegionBase != J313_AGX_G2_GPU_BASE ||
      Context >= J313_AGX_G2_UAT_CONTEXT_COUNT ||
      AppleAgxUatPublicationPairValid(Pair) == 0u ||
      (Pair->Ttbr0 >> 48) != Context || (Pair->Ttbr1 >> 48) != Context)
    return AppleAgxUatPublicationResultInvalidArgument;
  if (Io->Map(Io->Context, Snapshot->GpuRegionBase,
              (unsigned int)J313_AGX_G2_GPU_SIZE, &mapped) == 0u ||
      mapped == 0)
    return AppleAgxUatPublicationResultMapFailed;

  State->MappedBase = mapped;
  State->Context = Context;
  pairBase = mapped + (unsigned long long)Context * 16u;
  State->OriginalTtbr0 = AppleAgxUatPublicationReadU64(pairBase);
  State->OriginalTtbr1 = AppleAgxUatPublicationReadU64(pairBase + 8u);
  State->PublishedTtbr0 = Pair->Ttbr0;
  State->PublishedTtbr1 = Pair->Ttbr1;
  AppleAgxUatPublicationWriteU64(pairBase, Pair->Ttbr0);
  Io->Barrier(Io->Context);
  AppleAgxUatPublicationWriteU64(pairBase + 8u, Pair->Ttbr1);
  Io->Barrier(Io->Context);
  State->Active = 1u;
  return AppleAgxUatPublicationResultOk;
}

APPLE_AGX_UAT_PUBLICATION_RESULT AppleAgxUatUnpublishJ313(
    const APPLE_AGX_UAT_PUBLICATION_IO *Io,
    APPLE_AGX_UAT_PUBLICATION_STATE *State) {
#ifdef APPLE_AGX_FULL_CONTEXT0_BROKER
  if (State != 0 && State->Context == 0u)
    return AppleAgxUatPublicationResultInvalidArgument;
#endif
  if (State == 0 || AppleAgxUatPublicationIoValid(Io) == 0u ||
      State->Active == 0u || State->MappedBase == 0)
    return AppleAgxUatPublicationResultInvalidArgument;

  volatile unsigned char *pairBase;

  if (State != 0 && State->Context >= J313_AGX_G2_UAT_CONTEXT_COUNT)
    return AppleAgxUatPublicationResultInvalidArgument;
  pairBase = State == 0 ? 0 :
      State->MappedBase + (unsigned long long)State->Context * 16u;
  AppleAgxUatPublicationWriteU64(pairBase,
                                 State->OriginalTtbr0);
  Io->Barrier(Io->Context);
  AppleAgxUatPublicationWriteU64(pairBase + 8u,
                                 State->OriginalTtbr1);
  Io->Barrier(Io->Context);
  if (Io->Unmap(Io->Context, State->MappedBase) == 0u)
    return AppleAgxUatPublicationResultUnmapFailed;

  State->MappedBase = 0;
  State->OriginalTtbr0 = 0ULL;
  State->OriginalTtbr1 = 0ULL;
  State->PublishedTtbr0 = 0ULL;
  State->PublishedTtbr1 = 0ULL;
  State->Context = 0u;
  State->Active = 0u;
  return AppleAgxUatPublicationResultOk;
}
