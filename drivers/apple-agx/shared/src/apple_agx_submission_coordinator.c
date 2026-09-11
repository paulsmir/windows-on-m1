#include "apple_agx_submission_coordinator.h"

#define COORDINATOR_NULL ((void *)0)

static void AppleAgxCoordinatorZero(void *Address, APPLE_AGX_U32 Bytes) {
  unsigned char *cursor = (unsigned char *)Address;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    cursor[index] = 0u;
}

static APPLE_AGX_BOOL AppleAgxCoordinatorPoolValid(
    const APPLE_AGX_RENDER_PROVIDER *Provider) {
  const APPLE_AGX_MEMORY_OBJECT *pool;
  if (Provider == COORDINATOR_NULL || !Provider->Initialized ||
      Provider->PreparedPool == COORDINATOR_NULL)
    return APPLE_AGX_FALSE;
  pool = Provider->PreparedPool;
  return pool->CpuAddress != COORDINATOR_NULL && pool->DeviceAddress != 0ULL &&
                 pool->Length >= APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET +
                                     APPLE_AGX_EXP208_ARENA_BYTES &&
                 pool->State >= AppleAgxMemoryPrepared
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorInitialize(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator,
    const APPLE_AGX_SUBMISSION_COORDINATOR_CONFIG *Config) {
  if (Coordinator == COORDINATOR_NULL || Config == COORDINATOR_NULL ||
      Config->RenderProvider == COORDINATOR_NULL ||
      Config->QueueProvider == COORDINATOR_NULL ||
      Config->RenderProvider->Initdata == COORDINATOR_NULL ||
      !Config->RenderProvider->Initdata->RenderSharedMemory.Initialized ||
      !Config->RenderProvider->Initdata->RenderSharedMemory.Built ||
      Config->RenderProvider->Initdata->RenderSharedMemory.ObjectCount !=
          APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT ||
      Config->EventPair.Lease == 0u ||
      Config->EventPair.Ta >= APPLE_AGX_EVENT_COUNT ||
      Config->EventPair.D3 >= APPLE_AGX_EVENT_COUNT ||
      Config->EventPair.Ta == Config->EventPair.D3 ||
      !AppleAgxCoordinatorPoolValid(Config->RenderProvider) ||
      AppleAgxRenderTemplateRuntimeObjectCount() !=
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      AppleAgxRenderTemplateArenaObjectIndex() !=
          APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX ||
      AppleAgxRenderTemplateRelocationCount() !=
          APPLE_AGX_RENDER_TEMPLATE_RELOCATION_COUNT)
    return APPLE_AGX_FALSE;
  AppleAgxCoordinatorZero(Coordinator,
                          (APPLE_AGX_U32)sizeof(*Coordinator));
  Coordinator->RenderProvider = Config->RenderProvider;
  Coordinator->QueueProvider = Config->QueueProvider;
  Coordinator->RenderSharedMemory =
      (APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *)&
          Config->RenderProvider->Initdata->RenderSharedMemory;
  Coordinator->EventPair = Config->EventPair;
  Coordinator->Initialized = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorStage(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount) {
  APPLE_AGX_G13_QUEUE_JOB_PLAN plan;
  APPLE_AGX_EXP208_DYNAMIC_INPUT dynamic_input;
  APPLE_AGX_EXP208_DYNAMIC_RESULT dynamic_result;
  APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG job;
  APPLE_AGX_GDI_LOWERING_RECEIPT lowering_receipt;
  APPLE_AGX_MEMORY_OBJECT *pool;
  unsigned char *arena;
  APPLE_AGX_U32 sequence;

  if (Coordinator == COORDINATOR_NULL || !Coordinator->Initialized ||
      Submission == COORDINATOR_NULL || SubmissionBytes == COORDINATOR_NULL ||
      SubmissionByteCount == 0u ||
      Submission->ContextIdentity == 0ULL ||
      Submission->Submission.Fence == 0u ||
      Coordinator->Sequence == ~(APPLE_AGX_U32)0 ||
      Coordinator->RenderProvider->JobStaged ||
      !AppleAgxCoordinatorPoolValid(Coordinator->RenderProvider) ||
      !AppleAgxGdiBuildLoweringReceipt(
          SubmissionBytes, SubmissionByteCount, &lowering_receipt) ||
      (lowering_receipt.RequiredPrimitiveMask &
       ~AppleAgxExp208SupportedGdiPrimitiveMask()) != 0u ||
      !AppleAgxG13QueueProviderPlanJob(Coordinator->QueueProvider, &plan))
    return APPLE_AGX_FALSE;

  sequence = Coordinator->Sequence + 1u;
  dynamic_input.Sequence = sequence;
  dynamic_input.TaEventNumber = Coordinator->EventPair.Ta;
  dynamic_input.D3EventNumber = Coordinator->EventPair.D3;
  dynamic_input.IncludeInitBm = plan.IncludeInitBm;
  if (!AppleAgxExp208DeriveDynamic(&dynamic_input, &dynamic_result))
    return APPLE_AGX_FALSE;

  pool = Coordinator->RenderProvider->PreparedPool;
  arena = (unsigned char *)pool->CpuAddress +
          APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET;
  if (!AppleAgxRenderTemplateBuildRelocationObjects(
          arena, APPLE_AGX_EXP208_ARENA_BYTES,
          pool->DeviceAddress + APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET,
          Coordinator->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT) ||
      !AppleAgxExp208PatchDynamic(arena, APPLE_AGX_EXP208_ARENA_BYTES,
                                  &dynamic_input) ||
      !AppleAgxRenderSharedMemoryBindRelocationObjects(
          Coordinator->RenderSharedMemory, arena,
          APPLE_AGX_EXP208_ARENA_BYTES, Coordinator->Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT))
    return APPLE_AGX_FALSE;

  AppleAgxCoordinatorZero(&job, (APPLE_AGX_U32)sizeof(job));
  job.ContextIdentity = Submission->ContextIdentity;
  job.Fence = Submission->Submission.Fence;
  job.Parameters.ArenaGpuAddress = APPLE_AGX_EXP208_ARENA_GPU_BASE;
  job.Parameters.ArenaBytes = APPLE_AGX_EXP208_ARENA_BYTES;
  job.Parameters.TaEvent = Coordinator->EventPair.Ta;
  job.Parameters.D3Event = Coordinator->EventPair.D3;
  job.Parameters.TaExpectedStamp = dynamic_result.TaCurrentStamp;
  job.Parameters.D3ExpectedStamp = dynamic_result.D3CurrentStamp;
  job.Parameters.TaExpectedDonePointer = plan.TaExpectedDonePointer;
  job.Parameters.D3ExpectedDonePointer = plan.D3ExpectedDonePointer;
  job.Objects = Coordinator->Objects;
  job.ObjectCount = APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT;
  job.ArenaObject = APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX;
  job.Relocations = AppleAgxRenderTemplateRelocations();
  job.RelocationCount = APPLE_AGX_RENDER_TEMPLATE_RELOCATION_COUNT;
  if (!AppleAgxRenderProviderStageJob(
          Coordinator->RenderProvider, &job, SubmissionBytes,
          SubmissionByteCount))
    return APPLE_AGX_FALSE;
  Coordinator->Sequence = sequence;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorReset(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator) {
  if (Coordinator == COORDINATOR_NULL || !Coordinator->Initialized ||
      Coordinator->RenderProvider->JobStaged ||
      Coordinator->QueueProvider->Runtime.BufferManagerInitialized)
    return APPLE_AGX_FALSE;
  Coordinator->Sequence = 0u;
  return APPLE_AGX_TRUE;
}

#undef COORDINATOR_NULL
