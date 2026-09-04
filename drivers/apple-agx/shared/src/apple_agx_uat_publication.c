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
  if (pair == 0)
    return 0u;
  if ((pair->Ttbr0 & 0x3fffULL) != 1ULL ||
      (pair->Ttbr1 & 0x3fffULL) != 1ULL)
    return 0u;
  if (pair->Ttbr0 >= address_limit || pair->Ttbr1 >= address_limit)
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
  volatile unsigned char *mapped = 0;

  if (Snapshot == 0 || State == 0 ||
      AppleAgxUatPublicationIoValid(Io) == 0u || State->Active != 0u ||
      State->MappedBase != 0 ||
      Snapshot->GpuRegionBase != J313_AGX_G2_GPU_BASE ||
      AppleAgxUatPublicationPairValid(Pair) == 0u)
    return AppleAgxUatPublicationResultInvalidArgument;
  if (Io->Map(Io->Context, Snapshot->GpuRegionBase,
              (unsigned int)J313_AGX_G2_GPU_SIZE, &mapped) == 0u ||
      mapped == 0)
    return AppleAgxUatPublicationResultMapFailed;

  State->MappedBase = mapped;
  State->OriginalTtbr0 = AppleAgxUatPublicationReadU64(mapped);
  State->OriginalTtbr1 = AppleAgxUatPublicationReadU64(mapped + 8u);
  State->PublishedTtbr0 = Pair->Ttbr0;
  State->PublishedTtbr1 = Pair->Ttbr1;
  AppleAgxUatPublicationWriteU64(mapped, Pair->Ttbr0);
  Io->Barrier(Io->Context);
  AppleAgxUatPublicationWriteU64(mapped + 8u, Pair->Ttbr1);
  Io->Barrier(Io->Context);
  State->Active = 1u;
  return AppleAgxUatPublicationResultOk;
}

APPLE_AGX_UAT_PUBLICATION_RESULT AppleAgxUatUnpublishJ313(
    const APPLE_AGX_UAT_PUBLICATION_IO *Io,
    APPLE_AGX_UAT_PUBLICATION_STATE *State) {
  if (State == 0 || AppleAgxUatPublicationIoValid(Io) == 0u ||
      State->Active == 0u || State->MappedBase == 0)
    return AppleAgxUatPublicationResultInvalidArgument;

  AppleAgxUatPublicationWriteU64(State->MappedBase,
                                 State->OriginalTtbr0);
  Io->Barrier(Io->Context);
  AppleAgxUatPublicationWriteU64(State->MappedBase + 8u,
                                 State->OriginalTtbr1);
  Io->Barrier(Io->Context);
  if (Io->Unmap(Io->Context, State->MappedBase) == 0u)
    return AppleAgxUatPublicationResultUnmapFailed;

  State->MappedBase = 0;
  State->OriginalTtbr0 = 0ULL;
  State->OriginalTtbr1 = 0ULL;
  State->PublishedTtbr0 = 0ULL;
  State->PublishedTtbr1 = 0ULL;
  State->Active = 0u;
  return AppleAgxUatPublicationResultOk;
}
