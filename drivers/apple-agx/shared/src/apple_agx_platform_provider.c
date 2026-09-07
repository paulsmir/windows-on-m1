#include "apple_agx_platform_provider.h"

#include "j313_agx_g2.generated.h"

#define PLATFORM_NULL ((void *)0)

static void AppleAgxPlatformZero(void *Address, APPLE_AGX_BACKEND_U32 Bytes) {
  unsigned char *cursor = (unsigned char *)Address;
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Bytes; ++index)
    cursor[index] = 0u;
}

static void AppleAgxPlatformCopy(unsigned char *Destination,
                                 const unsigned char *Source,
                                 APPLE_AGX_BACKEND_U32 Bytes) {
  APPLE_AGX_BACKEND_U32 index;
  for (index = 0u; index < Bytes; ++index)
    Destination[index] = Source[index];
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformObjectValid(
    const APPLE_AGX_MEMORY_OBJECT *Object, APPLE_AGX_BACKEND_U64 MinimumBytes) {
  return Object != PLATFORM_NULL && Object->CpuAddress != PLATFORM_NULL &&
         Object->DeviceAddress != 0ULL && Object->Length >= MinimumBytes &&
         Object->State >= AppleAgxMemoryPrepared
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderBindChannels(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    APPLE_AGX_PLATFORM_CHANNEL_BINDINGS *Bindings) {
  const APPLE_AGX_MEMORY_OBJECT *ta_state;
  const APPLE_AGX_MEMORY_OBJECT *ta_ring;
  const APPLE_AGX_MEMORY_OBJECT *d3_state;
  const APPLE_AGX_MEMORY_OBJECT *d3_ring;
  const APPLE_AGX_MEMORY_OBJECT *event_state;
  const APPLE_AGX_MEMORY_OBJECT *event_ring;

  if (ChannelMemory == PLATFORM_NULL || Bindings == PLATFORM_NULL ||
      !ChannelMemory->Initialized || !ChannelMemory->Built ||
      ChannelMemory->ObjectCount != APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT)
    return APPLE_AGX_BACKEND_FALSE;

  ta_state = &ChannelMemory->Objects[
      AppleAgxChannelMemoryCommandStateBase +
      APPLE_AGX_PLATFORM_TA_CHANNEL_INDEX];
  ta_ring = &ChannelMemory->Objects[
      AppleAgxChannelMemoryCommandRingBase +
      APPLE_AGX_PLATFORM_TA_CHANNEL_INDEX];
  d3_state = &ChannelMemory->Objects[
      AppleAgxChannelMemoryCommandStateBase +
      APPLE_AGX_PLATFORM_D3_CHANNEL_INDEX];
  d3_ring = &ChannelMemory->Objects[
      AppleAgxChannelMemoryCommandRingBase +
      APPLE_AGX_PLATFORM_D3_CHANNEL_INDEX];
  event_state =
      &ChannelMemory->Objects[AppleAgxChannelMemoryEventState];
  event_ring = &ChannelMemory->Objects[AppleAgxChannelMemoryEventRing];

  if (!AppleAgxPlatformObjectValid(
          ta_state, J313_AGX_G2_CHANNEL_STATE_STRIDE) ||
      !AppleAgxPlatformObjectValid(
          ta_ring, J313_AGX_G2_CMD_QUEUE_RING_SIZE) ||
      !AppleAgxPlatformObjectValid(
          d3_state, J313_AGX_G2_CHANNEL_STATE_STRIDE) ||
      !AppleAgxPlatformObjectValid(
          d3_ring, J313_AGX_G2_CMD_QUEUE_RING_SIZE) ||
      !AppleAgxPlatformObjectValid(
          event_state, J313_AGX_G2_CHANNEL_STATE_STRIDE) ||
      !AppleAgxPlatformObjectValid(
          event_ring, J313_AGX_G2_EVENT_RING_SIZE))
    return APPLE_AGX_BACKEND_FALSE;

  AppleAgxPlatformZero(Bindings,
                       (APPLE_AGX_BACKEND_U32)sizeof(*Bindings));
  Bindings->Ta.StateCpuAddress = ta_state->CpuAddress;
  Bindings->Ta.RingCpuAddress = ta_ring->CpuAddress;
  Bindings->Ta.StateGpuAddress = ChannelMemory->VirtualAddresses[
      AppleAgxChannelMemoryCommandStateBase +
      APPLE_AGX_PLATFORM_TA_CHANNEL_INDEX];
  Bindings->Ta.RingGpuAddress = ChannelMemory->VirtualAddresses[
      AppleAgxChannelMemoryCommandRingBase +
      APPLE_AGX_PLATFORM_TA_CHANNEL_INDEX];
  Bindings->Ta.Doorbell = APPLE_AGX_PLATFORM_TA_DOORBELL;
  Bindings->D3.StateCpuAddress = d3_state->CpuAddress;
  Bindings->D3.RingCpuAddress = d3_ring->CpuAddress;
  Bindings->D3.StateGpuAddress = ChannelMemory->VirtualAddresses[
      AppleAgxChannelMemoryCommandStateBase +
      APPLE_AGX_PLATFORM_D3_CHANNEL_INDEX];
  Bindings->D3.RingGpuAddress = ChannelMemory->VirtualAddresses[
      AppleAgxChannelMemoryCommandRingBase +
      APPLE_AGX_PLATFORM_D3_CHANNEL_INDEX];
  Bindings->D3.Doorbell = APPLE_AGX_PLATFORM_D3_DOORBELL;
  Bindings->Event.StateCpuAddress = event_state->CpuAddress;
  Bindings->Event.RingCpuAddress = event_ring->CpuAddress;
  Bindings->Event.StateGpuAddress =
      ChannelMemory->VirtualAddresses[AppleAgxChannelMemoryEventState];
  Bindings->Event.RingGpuAddress =
      ChannelMemory->VirtualAddresses[AppleAgxChannelMemoryEventRing];
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValid(
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Io) {
  return Io != PLATFORM_NULL && Io->Context != PLATFORM_NULL &&
         Io->FlushForDevice != PLATFORM_NULL &&
         Io->FlushForCpu != PLATFORM_NULL &&
         Io->MemoryBarrier != PLATFORM_NULL &&
         Io->PublishU32 != PLATFORM_NULL && Io->ReadU32 != PLATFORM_NULL &&
         Io->RingDoorbell != PLATFORM_NULL && Io->Quiesce != PLATFORM_NULL &&
         Io->NowTicks != PLATFORM_NULL
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformExternalRenderValid(
    const APPLE_AGX_PLATFORM_EXTERNAL_RENDER_IO *Io) {
  return Io != PLATFORM_NULL && Io->Context != PLATFORM_NULL &&
                 Io->BuildJob != PLATFORM_NULL &&
                 Io->ResolvePreparedRange != PLATFORM_NULL
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformPreparedRange(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_BACKEND_U64 GpuAddress,
    const void **CpuAddress,
    APPLE_AGX_BACKEND_U32 *Bytes) {
  APPLE_AGX_BACKEND_U32 index;
  if (Owner == PLATFORM_NULL || CpuAddress == PLATFORM_NULL ||
      Bytes == PLATFORM_NULL || GpuAddress == 0ULL)
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < Owner->ObjectCount; ++index) {
    if (Owner->VirtualAddresses[index] != GpuAddress)
      continue;
    if (Owner->Objects[index].CpuAddress == PLATFORM_NULL ||
        Owner->Objects[index].Length == 0ULL ||
        Owner->Objects[index].Length > 0xffffffffULL ||
        Owner->Objects[index].State != AppleAgxMemoryGpuMapped)
      return APPLE_AGX_BACKEND_FALSE;
    *CpuAddress = Owner->Objects[index].CpuAddress;
    *Bytes = (APPLE_AGX_BACKEND_U32)Owner->Objects[index].Length;
    return APPLE_AGX_BACKEND_TRUE;
  }
  return APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformBuildSubmission(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission) {
  APPLE_AGX_PLATFORM_PROVIDER *provider =
      (APPLE_AGX_PLATFORM_PROVIDER *)Context;
  APPLE_AGX_BACKEND_U32 index;
  if (provider == PLATFORM_NULL || Job == PLATFORM_NULL || Fence == 0u ||
      Submission == PLATFORM_NULL ||
      Job->TaWorkAddressCount != APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      Job->D3WorkAddressCount != APPLE_AGX_BACKEND_QUEUE_WORK_COUNT ||
      (!provider->ExternalRenderReady &&
       provider->RenderSharedMemory == PLATFORM_NULL) ||
      provider->Transport.NowTicks == PLATFORM_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxPlatformZero(Submission,
                       (APPLE_AGX_BACKEND_U32)sizeof(*Submission));
  Submission->Fence = Fence;
  Submission->Timestamp = 0ULL;
  Submission->NowTicks =
      provider->Transport.NowTicks(provider->Transport.Context);
  if (Submission->NowTicks == 0ULL)
    return APPLE_AGX_BACKEND_FALSE;
  for (index = 0u; index < APPLE_AGX_BACKEND_QUEUE_WORK_COUNT; ++index) {
    Submission->Ta.GpuAddresses[index] = Job->TaWorkAddresses[index];
    Submission->D3.GpuAddresses[index] = Job->D3WorkAddresses[index];
    if (provider->ExternalRenderReady) {
      if (!provider->ExternalRender.ResolvePreparedRange(
              provider->ExternalRender.Context,
              Job->TaWorkAddresses[index],
              &Submission->Ta.PreparedRanges[index].Address,
              &Submission->Ta.PreparedRanges[index].Bytes) ||
          !provider->ExternalRender.ResolvePreparedRange(
              provider->ExternalRender.Context,
              Job->D3WorkAddresses[index],
              &Submission->D3.PreparedRanges[index].Address,
              &Submission->D3.PreparedRanges[index].Bytes))
        return APPLE_AGX_BACKEND_FALSE;
    } else if (!AppleAgxPlatformPreparedRange(
                   provider->RenderSharedMemory,
                   Job->TaWorkAddresses[index],
                   &Submission->Ta.PreparedRanges[index].Address,
                   &Submission->Ta.PreparedRanges[index].Bytes) ||
               !AppleAgxPlatformPreparedRange(
                   provider->RenderSharedMemory,
                   Job->D3WorkAddresses[index],
                   &Submission->D3.PreparedRanges[index].Address,
                   &Submission->D3.PreparedRanges[index].Bytes)) {
      return APPLE_AGX_BACKEND_FALSE;
    }
  }
  Submission->Ta.GpuAddressCount = APPLE_AGX_BACKEND_QUEUE_WORK_COUNT;
  Submission->Ta.PreparedRangeCount = APPLE_AGX_BACKEND_QUEUE_WORK_COUNT;
  Submission->Ta.ExpectedStamp = Job->TaExpectedStamp;
  Submission->D3.GpuAddressCount = APPLE_AGX_BACKEND_QUEUE_WORK_COUNT;
  Submission->D3.PreparedRangeCount = APPLE_AGX_BACKEND_QUEUE_WORK_COUNT;
  Submission->D3.ExpectedStamp = Job->D3ExpectedStamp;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderPublishRun(
    const APPLE_AGX_PLATFORM_CHANNEL_BINDINGS *Bindings,
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Io,
    APPLE_AGX_G13_QUEUE_TYPE QueueType,
    const unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) {
  const APPLE_AGX_PLATFORM_TX_CHANNEL_BINDING *binding;
  volatile APPLE_AGX_BACKEND_U32 *read_pointer;
  volatile APPLE_AGX_BACKEND_U32 *write_pointer;
  APPLE_AGX_BACKEND_U32 read;
  APPLE_AGX_BACKEND_U32 write;
  APPLE_AGX_BACKEND_U32 next;
  unsigned char *destination;

  if (Bindings == PLATFORM_NULL || Message == PLATFORM_NULL ||
      !AppleAgxPlatformTransportValid(Io))
    return APPLE_AGX_BACKEND_FALSE;
  if (QueueType == AppleAgxG13QueueTa)
    binding = &Bindings->Ta;
  else if (QueueType == AppleAgxG13Queue3d)
    binding = &Bindings->D3;
  else
    return APPLE_AGX_BACKEND_FALSE;
  if (binding->StateCpuAddress == PLATFORM_NULL ||
      binding->RingCpuAddress == PLATFORM_NULL)
    return APPLE_AGX_BACKEND_FALSE;

  if (!Io->FlushForCpu(Io->Context, binding->StateCpuAddress,
                       J313_AGX_G2_CHANNEL_STATE_STRIDE))
    return APPLE_AGX_BACKEND_FALSE;
  Io->MemoryBarrier(Io->Context);
  read_pointer = (volatile APPLE_AGX_BACKEND_U32 *)(
      binding->StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
  write_pointer = (volatile APPLE_AGX_BACKEND_U32 *)(
      binding->StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
  if (!Io->ReadU32(Io->Context, read_pointer, &read) ||
      !Io->ReadU32(Io->Context, write_pointer, &write) ||
      read >= APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT ||
      write >= APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT)
    return APPLE_AGX_BACKEND_FALSE;
  next = (write + 1u) % APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT;
  if (next == read)
    return APPLE_AGX_BACKEND_FALSE;

  destination = binding->RingCpuAddress +
                write * APPLE_AGX_G13_RUN_MESSAGE_SIZE;
  AppleAgxPlatformCopy(destination, Message,
                       APPLE_AGX_G13_RUN_MESSAGE_SIZE);
  if (!Io->FlushForDevice(Io->Context, destination,
                          APPLE_AGX_G13_RUN_MESSAGE_SIZE))
    return APPLE_AGX_BACKEND_FALSE;
  Io->MemoryBarrier(Io->Context);
  if (!Io->PublishU32(Io->Context, write_pointer, next))
    return APPLE_AGX_BACKEND_FALSE;
  Io->MemoryBarrier(Io->Context);
  return Io->RingDoorbell(Io->Context, binding->Doorbell);
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformCallerQueueIoValid(
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *Io) {
  return Io != PLATFORM_NULL && Io->Context != PLATFORM_NULL &&
         Io->FlushForDevice != PLATFORM_NULL &&
         Io->MemoryBarrier != PLATFORM_NULL &&
         Io->PublishU32 != PLATFORM_NULL && Io->ReadU32 != PLATFORM_NULL &&
         Io->Quiesce != PLATFORM_NULL
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueueFlushForDevice(
    void *Context, const void *Address, APPLE_AGX_BACKEND_U32 Bytes) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->CallerQueueRuntimeIo.FlushForDevice(
                   provider->CallerQueueRuntimeIo.Context, Address, Bytes)
             : APPLE_AGX_BACKEND_FALSE;
}

static void AppleAgxPlatformQueueMemoryBarrier(void *Context) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  if (provider != PLATFORM_NULL && provider->BindingReady)
    provider->CallerQueueRuntimeIo.MemoryBarrier(
        provider->CallerQueueRuntimeIo.Context);
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueuePublishU32(
    void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 Value) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->CallerQueueRuntimeIo.PublishU32(
                   provider->CallerQueueRuntimeIo.Context, Address, Value)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueueReadU32(
    void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 *Value) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->CallerQueueRuntimeIo.ReadU32(
                   provider->CallerQueueRuntimeIo.Context, Address, Value)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueueSendRunMessage(
    void *Context, APPLE_AGX_BACKEND_U32 QueueType,
    const unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  if (provider == PLATFORM_NULL || !provider->BindingReady)
    return APPLE_AGX_BACKEND_FALSE;
  return AppleAgxPlatformProviderPublishRun(
      &provider->Channels, &provider->Transport,
      (APPLE_AGX_G13_QUEUE_TYPE)QueueType, Message);
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueueQuiesce(
    void *Context, APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->CallerQueueRuntimeIo.Quiesce(
                   provider->CallerQueueRuntimeIo.Context, Fence)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformRenderRelocate(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  APPLE_AGX_G13_QUEUE_JOB_PLAN plan;
  if (provider == PLATFORM_NULL || !provider->BindingReady ||
      Arena == PLATFORM_NULL || ArenaBytes == 0u || Roots == PLATFORM_NULL ||
      SubmissionBytes == PLATFORM_NULL || SubmissionByteCount == 0u ||
      Submission == PLATFORM_NULL || Job == PLATFORM_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  if (provider->ExternalRenderReady) {
    AppleAgxPlatformZero(&plan, (APPLE_AGX_BACKEND_U32)sizeof(plan));
    return AppleAgxG13QueueProviderPlanJob(
               &provider->QueueProvider, &plan) &&
                   provider->ExternalRender.BuildJob(
                       provider->ExternalRender.Context, SubmissionBytes,
                       SubmissionByteCount, Submission,
                       provider->EventPair.Ta, provider->EventPair.D3,
                       &plan, Job)
               ? APPLE_AGX_BACKEND_TRUE
               : APPLE_AGX_BACKEND_FALSE;
  }
#if defined(APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY)
  return APPLE_AGX_BACKEND_FALSE;
#else
  return AppleAgxSubmissionCoordinatorStage(
             &provider->SubmissionCoordinator, Submission,
             SubmissionBytes, SubmissionByteCount) &&
                 provider->CallerRenderIo.Image.Relocate(
                     provider->CallerRenderIo.Context, Arena, ArenaBytes,
                     Roots, SubmissionBytes, SubmissionByteCount, Submission,
                     Job)
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
#endif
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformRenderPublish(void *Context) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->CallerRenderIo.RenderContext.Publish(
                   provider->CallerRenderIo.Context)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformRenderUnpublish(void *Context) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->CallerRenderIo.RenderContext.Unpublish(
                   provider->CallerRenderIo.Context)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueuesCreate(void *Context) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG mapped_config;
  /* Preparation never grants residency: retain strict mapped validation at
   * the actual queue-create boundary, after successful firmware startup. */
  if (provider == PLATFORM_NULL || !AppleAgxRenderSharedMemoryBuildQueueConfig(
          provider->RenderSharedMemory, 1u, &mapped_config))
    return APPLE_AGX_BACKEND_FALSE;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->QueueBackendIo.Queues.Create(
                   provider->QueueBackendIo.Context)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueuesDestroy(void *Context) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  if (provider == PLATFORM_NULL || !provider->BindingReady ||
      !provider->QueueBackendIo.Queues.Destroy(
          provider->QueueBackendIo.Context))
    return APPLE_AGX_BACKEND_FALSE;
  if (provider->ExternalRenderReady)
    return APPLE_AGX_BACKEND_TRUE;
#if defined(APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY)
  return APPLE_AGX_BACKEND_FALSE;
#else
  return AppleAgxSubmissionCoordinatorReset(
      &provider->SubmissionCoordinator);
#endif
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueuesRun3d(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->QueueBackendIo.Queues.Run3d(
                   provider->QueueBackendIo.Context, Job, Fence)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueuesRunTa(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return provider != PLATFORM_NULL && provider->BindingReady
             ? provider->QueueBackendIo.Queues.RunTa(
                   provider->QueueBackendIo.Context, Job, Fence)
             : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueueFence(
    APPLE_AGX_PLATFORM_PROVIDER *Provider,
    APPLE_AGX_BACKEND_BOOL (*Operation)(void *, APPLE_AGX_BACKEND_U32),
    APPLE_AGX_BACKEND_U32 Fence) {
  if (Provider == PLATFORM_NULL || !Provider->BindingReady ||
      Operation == PLATFORM_NULL ||
      !Operation(Provider->QueueBackendIo.Context, Fence))
    return APPLE_AGX_BACKEND_FALSE;
  if (!Provider->QueueProvider.Runtime.BufferManagerInitialized)
#if defined(APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY)
    return Provider->ExternalRenderReady ? APPLE_AGX_BACKEND_TRUE
                                         : APPLE_AGX_BACKEND_FALSE;
#else
    return Provider->ExternalRenderReady
               ? APPLE_AGX_BACKEND_TRUE
               : AppleAgxSubmissionCoordinatorReset(
                     &Provider->SubmissionCoordinator);
#endif
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueuesStop(
    void *Context, APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return AppleAgxPlatformQueueFence(
      provider, provider != PLATFORM_NULL
                    ? provider->QueueBackendIo.Queues.Stop
                    : PLATFORM_NULL,
      Fence);
}

static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformQueuesReset(
    void *Context, APPLE_AGX_BACKEND_U32 Fence) {
  APPLE_AGX_PLATFORM_PROVIDER *provider = Context;
  return AppleAgxPlatformQueueFence(
      provider, provider != PLATFORM_NULL
                    ? provider->QueueBackendIo.Queues.Reset
                    : PLATFORM_NULL,
      Fence);
}

static void AppleAgxPlatformProviderReset(
    APPLE_AGX_PLATFORM_PROVIDER *Provider, APPLE_AGX_BACKEND_BOOL ReleasePair) {
  if (Provider == PLATFORM_NULL)
    return;
  if (ReleasePair && Provider->EventPair.Lease != 0u)
    (void)AppleAgxEventAllocatorReleasePair(&Provider->EventAllocator,
                                            &Provider->EventPair);
  AppleAgxPlatformZero(Provider,
                       (APPLE_AGX_BACKEND_U32)sizeof(*Provider));
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderInitialize(
    APPLE_AGX_PLATFORM_PROVIDER *Provider,
    const APPLE_AGX_PLATFORM_PROVIDER_CONFIG *Config,
    APPLE_AGX_BACKEND_IO *Io) {
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG queue_config;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO queue_io;
  APPLE_AGX_BACKEND_IO queue_backend_io;
  APPLE_AGX_BACKEND_IO render_backend_io;
  APPLE_AGX_PLATFORM_COMPOSER_CONFIG composer_config;
#if !defined(APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY)
  APPLE_AGX_SUBMISSION_COORDINATOR_CONFIG coordinator_config;
#endif
  APPLE_AGX_G13_QUEUE_PROVIDER_IO provider_io;
  APPLE_AGX_BACKEND_BOOL external_render;
  APPLE_AGX_BACKEND_BOOL legacy_render;

  external_render = Config != PLATFORM_NULL
                        ? AppleAgxPlatformExternalRenderValid(
                              &Config->ExternalRender)
                        : APPLE_AGX_BACKEND_FALSE;
#if defined(APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY)
  legacy_render = APPLE_AGX_BACKEND_FALSE;
#else
  legacy_render = Config != PLATFORM_NULL &&
                          Config->RenderProvider != PLATFORM_NULL &&
                          Config->RenderProvider->Initdata != PLATFORM_NULL &&
                          Config->ChannelMemory ==
                              &Config->RenderProvider->Initdata
                                   ->ChannelMemory &&
                          Config->RenderSharedMemory ==
                              &Config->RenderProvider->Initdata
                                   ->RenderSharedMemory
                      ? APPLE_AGX_BACKEND_TRUE
                      : APPLE_AGX_BACKEND_FALSE;
#endif

  if (Provider == PLATFORM_NULL || Config == PLATFORM_NULL ||
      Io == PLATFORM_NULL || Provider->Initialized ||
      Config->Firmware == PLATFORM_NULL || Config->Render == PLATFORM_NULL ||
      (!external_render && !legacy_render) ||
      Config->ChannelMemory == PLATFORM_NULL ||
      Config->RenderSharedMemory == PLATFORM_NULL ||
      Config->Runtime == PLATFORM_NULL ||
      (!external_render &&
       Config->Render->Image.Relocate == PLATFORM_NULL) ||
      Config->Render->RenderContext.Publish == PLATFORM_NULL ||
      Config->Render->RenderContext.Unpublish == PLATFORM_NULL ||
      !AppleAgxPlatformTransportValid(&Config->Transport) ||
      !AppleAgxPlatformCallerQueueIoValid(&Config->QueueRuntimeIo))
    return APPLE_AGX_BACKEND_FALSE;

  AppleAgxPlatformZero(Io, (APPLE_AGX_BACKEND_U32)sizeof(*Io));
  AppleAgxPlatformZero(Provider,
                       (APPLE_AGX_BACKEND_U32)sizeof(*Provider));
  if (!AppleAgxPlatformProviderBindChannels(Config->ChannelMemory,
                                            &Provider->Channels))
    return APPLE_AGX_BACKEND_FALSE;
  Provider->Transport = Config->Transport;
  Provider->CallerQueueRuntimeIo = Config->QueueRuntimeIo;
  Provider->Runtime = Config->Runtime;
  Provider->RenderSharedMemory = Config->RenderSharedMemory;
  Provider->CallerRenderIo = *Config->Render;
  Provider->ExternalRender = Config->ExternalRender;
  Provider->ExternalRenderReady = external_render;
  Provider->BindingReady = APPLE_AGX_BACKEND_TRUE;

  if (!AppleAgxEventAllocatorInitialize(&Provider->EventAllocator) ||
      !AppleAgxEventAllocatorReservePair(&Provider->EventAllocator,
                                         &Provider->EventPair)) {
    AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_FALSE);
    return APPLE_AGX_BACKEND_FALSE;
  }

  if (!(Config->DeferFirmwareMappings
      ? AppleAgxRenderSharedMemoryPrepareQueueConfig(
          Config->RenderSharedMemory, Config->QueueConfig.TimeoutTicks, &queue_config)
      : AppleAgxRenderSharedMemoryBuildQueueConfig(
          Config->RenderSharedMemory, Config->QueueConfig.TimeoutTicks, &queue_config))) {
    AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_TRUE);
    return APPLE_AGX_BACKEND_FALSE;
  }
  queue_config.Ta.EventNumber = Provider->EventPair.Ta;
  queue_config.D3.EventNumber = Provider->EventPair.D3;
  AppleAgxPlatformZero(&queue_io,
                       (APPLE_AGX_BACKEND_U32)sizeof(queue_io));
  queue_io.Context = Provider;
  queue_io.FlushForDevice = AppleAgxPlatformQueueFlushForDevice;
  queue_io.MemoryBarrier = AppleAgxPlatformQueueMemoryBarrier;
  queue_io.PublishU32 = AppleAgxPlatformQueuePublishU32;
  queue_io.ReadU32 = AppleAgxPlatformQueueReadU32;
  queue_io.SendRunMessage = AppleAgxPlatformQueueSendRunMessage;
  queue_io.Quiesce = AppleAgxPlatformQueueQuiesce;
  AppleAgxPlatformZero(&provider_io,
                       (APPLE_AGX_BACKEND_U32)sizeof(provider_io));
  provider_io.Context = Provider;
  provider_io.BuildSubmission = AppleAgxPlatformBuildSubmission;
  if (!AppleAgxG13QueueProviderInitialize(
          &Provider->QueueProvider, &queue_config, &queue_io, &provider_io)) {
    AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_TRUE);
    return APPLE_AGX_BACKEND_FALSE;
  }

  if (!external_render) {
#if defined(APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY)
    AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_TRUE);
    return APPLE_AGX_BACKEND_FALSE;
#else
    AppleAgxPlatformZero(&coordinator_config,
                         (APPLE_AGX_BACKEND_U32)sizeof(coordinator_config));
    coordinator_config.RenderProvider = Config->RenderProvider;
    coordinator_config.QueueProvider = &Provider->QueueProvider;
    coordinator_config.EventPair = Provider->EventPair;
    if (!AppleAgxSubmissionCoordinatorInitialize(
            &Provider->SubmissionCoordinator, &coordinator_config)) {
      AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_TRUE);
      return APPLE_AGX_BACKEND_FALSE;
    }
#endif
  }

  AppleAgxPlatformZero(&queue_backend_io,
                       (APPLE_AGX_BACKEND_U32)sizeof(queue_backend_io));
  queue_backend_io.Context = &Provider->QueueProvider;
  if (!AppleAgxG13QueueProviderInstall(&queue_backend_io)) {
    AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_TRUE);
    return APPLE_AGX_BACKEND_FALSE;
  }
  Provider->QueueBackendIo = queue_backend_io;
  queue_backend_io.Context = Provider;
  queue_backend_io.Queues.Create = AppleAgxPlatformQueuesCreate;
  queue_backend_io.Queues.Destroy = AppleAgxPlatformQueuesDestroy;
  queue_backend_io.Queues.Run3d = AppleAgxPlatformQueuesRun3d;
  queue_backend_io.Queues.RunTa = AppleAgxPlatformQueuesRunTa;
  queue_backend_io.Queues.Stop = AppleAgxPlatformQueuesStop;
  queue_backend_io.Queues.Reset = AppleAgxPlatformQueuesReset;

  render_backend_io = *Config->Render;
  render_backend_io.Context = Provider;
  render_backend_io.Image.Relocate = AppleAgxPlatformRenderRelocate;
  render_backend_io.RenderContext.Publish = AppleAgxPlatformRenderPublish;
  render_backend_io.RenderContext.Unpublish =
      AppleAgxPlatformRenderUnpublish;

  AppleAgxPlatformZero(&composer_config,
                       (APPLE_AGX_BACKEND_U32)sizeof(composer_config));
  composer_config.Firmware = Config->Firmware;
  composer_config.Render = &render_backend_io;
  composer_config.Queues = &queue_backend_io;
  composer_config.QueueProvider = &Provider->QueueProvider;
  composer_config.Runtime = Config->Runtime;
  if (!AppleAgxPlatformComposerInitialize(&Provider->Composer,
                                          &composer_config, Io)) {
    AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_TRUE);
    AppleAgxPlatformZero(Io, (APPLE_AGX_BACKEND_U32)sizeof(*Io));
    return APPLE_AGX_BACKEND_FALSE;
  }
  Provider->Initialized = APPLE_AGX_BACKEND_TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderDrainEvents(
    APPLE_AGX_PLATFORM_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U32 MaxMessages,
    APPLE_AGX_BACKEND_U32 *DrainedMessages,
    APPLE_AGX_BACKEND_U32 *CompletedFence) {
  APPLE_AGX_PLATFORM_RX_CHANNEL_BINDING *binding;
  volatile APPLE_AGX_BACKEND_U32 *read_pointer;
  volatile APPLE_AGX_BACKEND_U32 *write_pointer;
  APPLE_AGX_BACKEND_U32 read;
  APPLE_AGX_BACKEND_U32 write;
  APPLE_AGX_BACKEND_U32 drained;
  APPLE_AGX_BACKEND_U32 completed;

  if (DrainedMessages != PLATFORM_NULL)
    *DrainedMessages = 0u;
  if (CompletedFence != PLATFORM_NULL)
    *CompletedFence = 0u;
  if (Provider != PLATFORM_NULL) {
    Provider->LastDrainGuard = AppleAgxPlatformDrainGuardInvalid;
    Provider->LastEventReadPointer = 0xffffffffu;
    Provider->LastEventWritePointer = 0xffffffffu;
  }
  if (Provider == PLATFORM_NULL || !Provider->Initialized ||
      MaxMessages == 0u || DrainedMessages == PLATFORM_NULL ||
      CompletedFence == PLATFORM_NULL ||
      !AppleAgxPlatformTransportValid(&Provider->Transport))
    return APPLE_AGX_BACKEND_FALSE;

  binding = &Provider->Channels.Event;
  if (binding->StateCpuAddress == PLATFORM_NULL ||
      binding->RingCpuAddress == PLATFORM_NULL) {
    Provider->LastDrainGuard = AppleAgxPlatformDrainGuardBinding;
    return APPLE_AGX_BACKEND_FALSE;
  }
  if (!Provider->Transport.FlushForCpu(
          Provider->Transport.Context, binding->StateCpuAddress,
          J313_AGX_G2_CHANNEL_STATE_STRIDE)) {
    Provider->LastDrainGuard = AppleAgxPlatformDrainGuardFlushState;
    return APPLE_AGX_BACKEND_FALSE;
  }
  Provider->Transport.MemoryBarrier(Provider->Transport.Context);
  read_pointer = (volatile APPLE_AGX_BACKEND_U32 *)(
      binding->StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET);
  write_pointer = (volatile APPLE_AGX_BACKEND_U32 *)(
      binding->StateCpuAddress +
      APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET);
  if (!Provider->Transport.ReadU32(Provider->Transport.Context,
                                   read_pointer, &read)) {
    Provider->LastDrainGuard = AppleAgxPlatformDrainGuardReadPointer;
    return APPLE_AGX_BACKEND_FALSE;
  }
  Provider->LastEventReadPointer = read;
  if (!Provider->Transport.ReadU32(Provider->Transport.Context,
                                   write_pointer, &write)) {
    Provider->LastDrainGuard = AppleAgxPlatformDrainGuardWritePointer;
    return APPLE_AGX_BACKEND_FALSE;
  }
  Provider->LastEventWritePointer = write;
  if (read >= APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT ||
      write >= APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT) {
    Provider->LastDrainGuard = AppleAgxPlatformDrainGuardPointerRange;
    return APPLE_AGX_BACKEND_FALSE;
  }

  drained = 0u;
  completed = 0u;
  while ((Provider->EventBatchPending || read != write) &&
         drained < MaxMessages) {
    unsigned char *message =
        binding->RingCpuAddress + read * APPLE_AGX_G13_EVENT_MESSAGE_SIZE;
    APPLE_AGX_BACKEND_U32 message_fence = 0u;
    if (!Provider->EventBatchPending) {
      if (!Provider->Transport.FlushForCpu(
              Provider->Transport.Context, message,
              APPLE_AGX_G13_EVENT_MESSAGE_SIZE)) {
        Provider->LastDrainGuard = AppleAgxPlatformDrainGuardFlushMessage;
        return APPLE_AGX_BACKEND_FALSE;
      }
      Provider->Transport.MemoryBarrier(Provider->Transport.Context);
      if (!AppleAgxPlatformComposerPrepareEvent(
              &Provider->Composer, message, APPLE_AGX_G13_EVENT_MESSAGE_SIZE,
              &Provider->PendingEventBatch)) {
        Provider->LastDrainGuard = AppleAgxPlatformDrainGuardPrepareEvent;
        *DrainedMessages = drained;
        *CompletedFence = completed;
        return APPLE_AGX_BACKEND_FALSE;
      }
      Provider->PendingEventNextRead =
          (read + 1u) % APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT;
      Provider->EventBatchPending = APPLE_AGX_BACKEND_TRUE;
      Provider->EventBatchAcknowledged = APPLE_AGX_BACKEND_FALSE;
    }
    if (!Provider->EventBatchAcknowledged) {
      if (!Provider->Transport.PublishU32(
              Provider->Transport.Context, read_pointer,
              Provider->PendingEventNextRead)) {
        Provider->LastDrainGuard = AppleAgxPlatformDrainGuardPublishRead;
        *DrainedMessages = drained;
        *CompletedFence = completed;
        return APPLE_AGX_BACKEND_FALSE;
      }
      Provider->Transport.MemoryBarrier(Provider->Transport.Context);
      Provider->EventBatchAcknowledged = APPLE_AGX_BACKEND_TRUE;
      ++drained;
    }
    if (!AppleAgxPlatformComposerApplyEvent(
            &Provider->Composer, &Provider->PendingEventBatch,
            &message_fence)) {
      Provider->LastDrainGuard = AppleAgxPlatformDrainGuardApplyEvent;
      *DrainedMessages = drained;
      *CompletedFence = completed;
      return APPLE_AGX_BACKEND_FALSE;
    }
    read = Provider->PendingEventNextRead;
    AppleAgxPlatformZero(
        &Provider->PendingEventBatch,
        (APPLE_AGX_BACKEND_U32)sizeof(Provider->PendingEventBatch));
    Provider->PendingEventNextRead = 0u;
    Provider->EventBatchPending = APPLE_AGX_BACKEND_FALSE;
    Provider->EventBatchAcknowledged = APPLE_AGX_BACKEND_FALSE;
    if (message_fence != 0u)
      completed = message_fence;
  }
  *DrainedMessages = drained;
  *CompletedFence = completed;
  Provider->LastDrainGuard = AppleAgxPlatformDrainGuardOk;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderPoll(
    APPLE_AGX_PLATFORM_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U32 MaxMessages,
    APPLE_AGX_BACKEND_U32 *DrainedMessages,
    APPLE_AGX_BACKEND_U32 *CompletedFence) {
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH timeout_batch;
  APPLE_AGX_BACKEND_U32 drained = 0u;
  APPLE_AGX_BACKEND_U32 completed = 0u;
  APPLE_AGX_BACKEND_U64 now;

  if (DrainedMessages != PLATFORM_NULL)
    *DrainedMessages = 0u;
  if (CompletedFence != PLATFORM_NULL)
    *CompletedFence = 0u;
  if (Provider != PLATFORM_NULL)
    Provider->LastPollGuard = AppleAgxPlatformPollGuardInvalid;
  if (Provider == PLATFORM_NULL || !Provider->Initialized ||
      MaxMessages == 0u || DrainedMessages == PLATFORM_NULL ||
      CompletedFence == PLATFORM_NULL || Provider->Runtime == PLATFORM_NULL ||
      Provider->Transport.NowTicks == PLATFORM_NULL)
    return APPLE_AGX_BACKEND_FALSE;
  if (!AppleAgxPlatformProviderDrainEvents(Provider, MaxMessages, &drained,
                                           &completed)) {
    Provider->LastPollGuard = AppleAgxPlatformPollGuardDrainEvents;
    return APPLE_AGX_BACKEND_FALSE;
  }
  *DrainedMessages = drained;
  *CompletedFence = completed;
  if (Provider->Runtime->Phase != AppleAgxBackendRuntimeSubmitted) {
    Provider->LastPollGuard = AppleAgxPlatformPollGuardOk;
    return APPLE_AGX_BACKEND_TRUE;
  }
  /*
   * Consuming the complete caller budget does not prove the event ring is
   * empty.  A matching completion may already be queued behind this batch;
   * defer timeout classification until a later poll observes spare budget.
   */
  if (drained == MaxMessages) {
    Provider->LastPollGuard = AppleAgxPlatformPollGuardOk;
    return APPLE_AGX_BACKEND_TRUE;
  }
  now = Provider->Transport.NowTicks(Provider->Transport.Context);
  if (now == 0ULL) {
    Provider->LastPollGuard = AppleAgxPlatformPollGuardClock;
    return APPLE_AGX_BACKEND_FALSE;
  }
  if (!AppleAgxG13QueueProviderCheckTimeout(
          &Provider->QueueProvider, now, &timeout_batch)) {
    Provider->LastPollGuard = AppleAgxPlatformPollGuardTimeoutCheck;
    return APPLE_AGX_BACKEND_FALSE;
  }
  if (timeout_batch.ObservationCount == 0u) {
    Provider->LastPollGuard = AppleAgxPlatformPollGuardOk;
    return APPLE_AGX_BACKEND_TRUE;
  }
  if (!AppleAgxPlatformComposerApplyEvent(&Provider->Composer,
                                           &timeout_batch, &completed)) {
    Provider->LastPollGuard = AppleAgxPlatformPollGuardTimeoutApply;
    return APPLE_AGX_BACKEND_FALSE;
  }
  *CompletedFence = completed;
  Provider->LastPollGuard = AppleAgxPlatformPollGuardOk;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderDestroy(
    APPLE_AGX_PLATFORM_PROVIDER *Provider) {
  if (Provider == PLATFORM_NULL || !Provider->Initialized ||
      Provider->QueueProvider.Phase != AppleAgxG13QueueProviderInitialized ||
      !AppleAgxEventAllocatorReleasePair(&Provider->EventAllocator,
                                         &Provider->EventPair))
    return APPLE_AGX_BACKEND_FALSE;
  AppleAgxPlatformProviderReset(Provider, APPLE_AGX_BACKEND_FALSE);
  return APPLE_AGX_BACKEND_TRUE;
}

#undef PLATFORM_NULL
