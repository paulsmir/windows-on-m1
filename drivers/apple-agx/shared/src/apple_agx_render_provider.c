#include "apple_agx_render_provider.h"

#define APPLE_AGX_RENDER_PROVIDER_NULL ((void *)0)
#define APPLE_AGX_RENDER_PROVIDER_ADDRESS_LIMIT (1ULL << 40u)
#define APPLE_AGX_RENDER_PROVIDER_ASID_MASK 0xffff000000000000ULL
#define APPLE_AGX_RENDER_PROVIDER_ADDRESS_MASK                              \
  (APPLE_AGX_RENDER_PROVIDER_ADDRESS_LIMIT - 1ULL)

static void AppleAgxRenderProviderZero(void *Address,
                                       APPLE_AGX_BACKEND_U32 Bytes) {
  unsigned char *destination = (unsigned char *)Address;
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Bytes; ++index)
    destination[index] = 0u;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderPoolValid(
    const APPLE_AGX_MEMORY_OBJECT *Pool) {
  return Pool != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Pool->CpuAddress != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Pool->DeviceAddress != 0ULL &&
                 Pool->GpuVirtualAddress ==
                     APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE &&
                 Pool->Length == APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES &&
                 Pool->Context == APPLE_AGX_RENDER_PROVIDER_CONTEXT &&
                 (Pool->State == AppleAgxMemoryGpuMapped ||
                  Pool->State == AppleAgxMemoryCompleted)
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderTtbrPairValid(
    const APPLE_AGX_UAT_TTBR_PAIR *Pair) {
  APPLE_AGX_BACKEND_U64 asid;
  if (Pair == APPLE_AGX_RENDER_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  asid = Pair->Ttbr0 & APPLE_AGX_RENDER_PROVIDER_ASID_MASK;
  return (Pair->Ttbr0 & 0x3fffULL) == 1ULL &&
                 (Pair->Ttbr1 & 0x3fffULL) == 1ULL &&
                 (Pair->Ttbr1 & APPLE_AGX_RENDER_PROVIDER_ASID_MASK) == asid &&
                 asid <= ((APPLE_AGX_BACKEND_U64)
                          (J313_AGX_G2_UAT_CONTEXT_COUNT - 1u) << 48) &&
                 (Pair->Ttbr0 & ~(APPLE_AGX_RENDER_PROVIDER_ASID_MASK |
                                  APPLE_AGX_RENDER_PROVIDER_ADDRESS_MASK)) ==
                     0ULL &&
                 (Pair->Ttbr1 & ~(APPLE_AGX_RENDER_PROVIDER_ASID_MASK |
                                  APPLE_AGX_RENDER_PROVIDER_ADDRESS_MASK)) ==
                     0ULL
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderInitdataValid(
    const APPLE_AGX_INITDATA_MEMORY_GRAPH *Initdata) {
  return Initdata != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Initdata->Initialized != 0u && Initdata->Built != 0u &&
                 Initdata->InitdataVirtualAddress != 0ULL &&
                 Initdata->InitdataDeviceAddress != 0ULL &&
                 AppleAgxRenderProviderTtbrPairValid(&Initdata->TtbrPair)
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderRootsMatch(
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const APPLE_AGX_EXP208_MANIFEST *Manifest) {
  return Roots != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Manifest != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Roots->Ta[0] == Manifest->TaRoots[0] &&
                 Roots->Ta[1] == Manifest->TaRoots[1] &&
                 Roots->D3[0] == Manifest->D3Roots[0] &&
                 Roots->D3[1] == Manifest->D3Roots[1]
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderPublicationIoValid(
    const APPLE_AGX_UAT_PUBLICATION_IO *Io) {
  return Io != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Io->Map != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Io->Barrier != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 Io->Unmap != APPLE_AGX_RENDER_PROVIDER_NULL
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderInitialize(
    APPLE_AGX_RENDER_PROVIDER *Provider,
    const APPLE_AGX_RENDER_PROVIDER_CONFIG *Config) {
  APPLE_AGX_EXP208_MANIFEST manifest;
  APPLE_AGX_UAT_TTBR_PAIR render_pair;

  if (Provider == APPLE_AGX_RENDER_PROVIDER_NULL ||
      Config == APPLE_AGX_RENDER_PROVIDER_NULL || Provider->Initialized ||
      !AppleAgxRenderProviderPoolValid(Config->PreparedPool) ||
      !AppleAgxRenderProviderInitdataValid(Config->Initdata) ||
      Config->Snapshot == APPLE_AGX_RENDER_PROVIDER_NULL ||
      Config->Snapshot->GpuRegionBase != J313_AGX_G2_GPU_BASE ||
      Config->RenderRoots == APPLE_AGX_RENDER_PROVIDER_NULL ||
      !AppleAgxRenderProviderPublicationIoValid(Config->PublicationIo) ||
      AppleAgxRenderTemplateBytes() != APPLE_AGX_EXP208_ARENA_BYTES ||
      APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET > Config->PreparedPool->Length ||
      APPLE_AGX_EXP208_ARENA_BYTES >
          Config->PreparedPool->Length -
              APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET ||
      !AppleAgxExp208GetManifest(&manifest) ||
      manifest.ArenaGpuBase != APPLE_AGX_EXP208_ARENA_GPU_BASE ||
      manifest.ArenaBytes != APPLE_AGX_EXP208_ARENA_BYTES ||
      manifest.Alignment != APPLE_AGX_EXP208_ARENA_ALIGNMENT ||
      !AppleAgxRenderProviderRootsMatch(&Config->PreparedRoots, &manifest) ||
      AppleAgxUatEncodeTtbrPair(APPLE_AGX_RENDER_PROVIDER_CONTEXT,
                                Config->RenderRoots,
                                &render_pair) != AppleAgxUatResultOk ||
      !AppleAgxRenderProviderTtbrPairValid(&render_pair))
    return APPLE_AGX_BACKEND_FALSE;

  AppleAgxRenderProviderZero(Provider,
                             (APPLE_AGX_BACKEND_U32)sizeof(*Provider));
  Provider->PreparedPool = Config->PreparedPool;
  Provider->PreparedRoots = Config->PreparedRoots;
  Provider->Initdata = Config->Initdata;
  Provider->Snapshot = *Config->Snapshot;
  Provider->PublicationIo = *Config->PublicationIo;
  Provider->RenderTtbrPair = render_pair;
  Provider->Initialized = APPLE_AGX_BACKEND_TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderInstallIo(
    APPLE_AGX_RENDER_PROVIDER *Provider, APPLE_AGX_BACKEND_IO *Io) {
  if (Provider == APPLE_AGX_RENDER_PROVIDER_NULL ||
      Io == APPLE_AGX_RENDER_PROVIDER_NULL || !Provider->Initialized ||
      (Io->Context != APPLE_AGX_RENDER_PROVIDER_NULL &&
       Io->Context != Provider))
    return APPLE_AGX_BACKEND_FALSE;
  Io->Context = Provider;
  Io->Image.Relocate = AppleAgxRenderProviderRelocate;
  Io->RenderContext.Publish = AppleAgxRenderProviderPublish;
  Io->RenderContext.Unpublish = AppleAgxRenderProviderUnpublish;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderPublish(void *Context) {
  APPLE_AGX_RENDER_PROVIDER *provider =
      (APPLE_AGX_RENDER_PROVIDER *)Context;
  if (provider == APPLE_AGX_RENDER_PROVIDER_NULL ||
      !provider->Initialized || provider->ContextPublication.Active != 0u ||
      provider->JobStaged ||
      !AppleAgxRenderProviderPoolValid(provider->PreparedPool) ||
      !AppleAgxRenderProviderInitdataValid(provider->Initdata))
    return APPLE_AGX_BACKEND_FALSE;
  return AppleAgxUatPublishJ313Context(
             &provider->Snapshot, APPLE_AGX_RENDER_PROVIDER_CONTEXT,
             &provider->RenderTtbrPair, &provider->PublicationIo,
             &provider->ContextPublication) ==
                 AppleAgxUatPublicationResultOk
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static void AppleAgxRenderProviderClearStagedJob(
    APPLE_AGX_RENDER_PROVIDER *Provider) {
  AppleAgxRenderProviderZero(&Provider->StagedJob,
                             (APPLE_AGX_BACKEND_U32)sizeof(Provider->StagedJob));
  Provider->StagedSubmissionHash = 0ULL;
  Provider->StagedSubmissionBytes = 0u;
  Provider->JobStaged = APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderUnpublish(void *Context) {
  APPLE_AGX_RENDER_PROVIDER *provider =
      (APPLE_AGX_RENDER_PROVIDER *)Context;
  if (provider == APPLE_AGX_RENDER_PROVIDER_NULL ||
      !provider->Initialized || provider->ContextPublication.Active == 0u)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxRenderProviderClearStagedJob(provider);
  return AppleAgxUatUnpublishJ313(&provider->PublicationIo,
                                  &provider->ContextPublication) ==
                 AppleAgxUatPublicationResultOk
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_U64 AppleAgxRenderProviderHash(
    const unsigned char *Bytes, APPLE_AGX_BACKEND_U32 ByteCount) {
  APPLE_AGX_BACKEND_U64 hash = 0xcbf29ce484222325ULL;
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < ByteCount; ++index) {
    hash ^= Bytes[index];
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderJobConfigValid(
    const APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG *JobConfig) {
  return JobConfig != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 JobConfig->ContextIdentity != 0ULL &&
                 JobConfig->Fence != 0u &&
                 JobConfig->Parameters.ArenaGpuAddress ==
                     APPLE_AGX_EXP208_ARENA_GPU_BASE &&
                 JobConfig->Parameters.ArenaBytes ==
                     APPLE_AGX_EXP208_ARENA_BYTES &&
                 JobConfig->Objects != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 JobConfig->ObjectCount != 0u &&
                 JobConfig->ArenaObject < JobConfig->ObjectCount &&
                 JobConfig->Relocations != APPLE_AGX_RENDER_PROVIDER_NULL &&
                 JobConfig->RelocationCount != 0u
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderStageJob(
    APPLE_AGX_RENDER_PROVIDER *Provider,
    const APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG *JobConfig,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount) {
  APPLE_AGX_GDI_LOWERING_RECEIPT receipt;
  if (Provider == APPLE_AGX_RENDER_PROVIDER_NULL ||
      !Provider->Initialized || Provider->ContextPublication.Active == 0u ||
      Provider->JobStaged ||
      !AppleAgxRenderProviderJobConfigValid(JobConfig) ||
      SubmissionBytes == APPLE_AGX_RENDER_PROVIDER_NULL ||
      SubmissionByteCount == 0u ||
      !AppleAgxGdiBuildLoweringReceipt(SubmissionBytes,
                                       SubmissionByteCount, &receipt) ||
      (receipt.RequiredPrimitiveMask &
       ~AppleAgxExp208SupportedGdiPrimitiveMask()) != 0u)
    return APPLE_AGX_BACKEND_FALSE;
  Provider->StagedJob = *JobConfig;
  Provider->StagedSubmissionHash = receipt.StreamHash;
  Provider->StagedSubmissionBytes = SubmissionByteCount;
  Provider->JobStaged = APPLE_AGX_BACKEND_TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderInvocationValid(
    const APPLE_AGX_RENDER_PROVIDER *Provider, const void *Arena,
    APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission) {
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *arena_object;
  const unsigned char *arena_data;
  APPLE_AGX_EXP208_MANIFEST manifest;

  if (Provider == APPLE_AGX_RENDER_PROVIDER_NULL || !Provider->Initialized ||
      Provider->ContextPublication.Active == 0u ||
      !AppleAgxRenderProviderPoolValid(Provider->PreparedPool) ||
      Arena != Provider->PreparedPool->CpuAddress ||
      ArenaBytes != Provider->PreparedPool->Length ||
      SubmissionBytes == APPLE_AGX_RENDER_PROVIDER_NULL ||
      Submission == APPLE_AGX_RENDER_PROVIDER_NULL ||
      Submission->Submission.Kind != AppleAgxSubmissionGdi ||
      Submission->ContextIdentity != Provider->StagedJob.ContextIdentity ||
      Submission->Submission.Fence != Provider->StagedJob.Fence ||
      SubmissionByteCount != Provider->StagedSubmissionBytes ||
      AppleAgxRenderProviderHash(SubmissionBytes, SubmissionByteCount) !=
          Provider->StagedSubmissionHash ||
      !AppleAgxExp208GetManifest(&manifest) ||
      !AppleAgxRenderProviderRootsMatch(Roots, &manifest) ||
      !AppleAgxRenderProviderRootsMatch(&Provider->PreparedRoots, &manifest))
    return APPLE_AGX_BACKEND_FALSE;

  arena_data = (const unsigned char *)Arena +
               APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET;
  arena_object =
      &Provider->StagedJob.Objects[Provider->StagedJob.ArenaObject];
  return arena_object->GpuVa == APPLE_AGX_EXP208_ARENA_GPU_BASE &&
                 arena_object->PhysicalAddress ==
                     Provider->PreparedPool->DeviceAddress +
                         APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET &&
                 arena_object->Size == APPLE_AGX_EXP208_ARENA_BYTES &&
                 arena_object->Data == arena_data
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxRenderProviderRelocate(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_RENDER_PROVIDER *provider =
      (APPLE_AGX_RENDER_PROVIDER *)Context;
  APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG staged;
  APPLE_AGX_BACKEND_BOOL valid;

  if (provider == APPLE_AGX_RENDER_PROVIDER_NULL || !provider->JobStaged ||
      Job == APPLE_AGX_RENDER_PROVIDER_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  staged = provider->StagedJob;
  valid = AppleAgxRenderProviderInvocationValid(
      provider, Arena, ArenaBytes, Roots, SubmissionBytes,
      SubmissionByteCount, Submission);
  AppleAgxRenderProviderClearStagedJob(provider);
  if (!valid)
    return APPLE_AGX_BACKEND_FALSE;
  return AppleAgxExp208BuildJob(
      &staged.Parameters, staged.Objects, staged.ObjectCount,
      staged.Relocations, staged.RelocationCount, Job);
}
