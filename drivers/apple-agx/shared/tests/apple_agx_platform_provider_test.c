#include "apple_agx_platform_provider.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct _FAKE_TRANSPORT {
  unsigned int FlushDeviceCalls;
  unsigned int FlushCpuCalls;
  unsigned int BarrierCalls;
  unsigned int PublishCalls;
  unsigned int FailPublishAt;
  unsigned int DoorbellCalls;
  unsigned int LastDoorbell;
  unsigned int ZeroNow;
} FAKE_TRANSPORT;

typedef struct _FAKE_COMPONENTS {
  unsigned int QueueInitializeCalls;
  unsigned int QueueInstallCalls;
  unsigned int ComposerInitializeCalls;
  unsigned int PrepareCalls;
  unsigned int ApplyCalls;
  unsigned int TimeoutChecks;
  unsigned int FailPrepareAt;
  unsigned int FailApplyAt;
  unsigned int FailTimeout;
  APPLE_AGX_G13_QUEUE_PROVIDER_IO CapturedProviderIo;
  APPLE_AGX_BACKEND_IO CapturedRenderIo;
  APPLE_AGX_BACKEND_IO CapturedQueueIo;
  unsigned int ExternalBuildCalls;
  unsigned int ExternalResolveCalls;
} FAKE_COMPONENTS;

typedef struct _FAKE_EXTERNAL_RENDER {
  unsigned char Ranges[4][0x80];
} FAKE_EXTERNAL_RENDER;

static FAKE_COMPONENTS *g_components;
static FAKE_TRANSPORT *g_transport;

static APPLE_AGX_BACKEND_BOOL dummy_queue_create(void *Context) {
  return Context != NULL ? APPLE_AGX_BACKEND_TRUE : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL dummy_render_lifetime(void *Context) {
  return Context != NULL ? APPLE_AGX_BACKEND_TRUE : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_BACKEND_BOOL dummy_relocate(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *Bytes, APPLE_AGX_BACKEND_U32 ByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  return Context != NULL && Arena != NULL && ArenaBytes != 0u &&
                 Roots != NULL && Bytes != NULL && ByteCount != 0u &&
                 Submission != NULL && Job != NULL
             ? APPLE_AGX_BACKEND_TRUE
             : APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorInitialize(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator,
    const APPLE_AGX_SUBMISSION_COORDINATOR_CONFIG *Config) {
  assert(Coordinator != NULL && Config != NULL &&
         Config->RenderProvider != NULL && Config->QueueProvider != NULL &&
         Config->EventPair.Lease != 0u);
  memset(Coordinator, 0, sizeof(*Coordinator));
  Coordinator->Initialized = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorStage(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount) {
  return Coordinator != NULL && Coordinator->Initialized &&
                 Submission != NULL && SubmissionBytes != NULL &&
                 SubmissionByteCount != 0u
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxSubmissionCoordinatorReset(
    APPLE_AGX_SUBMISSION_COORDINATOR *Coordinator) {
  if (Coordinator == NULL || !Coordinator->Initialized)
    return APPLE_AGX_FALSE;
  Coordinator->Sequence = 0u;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderInitialize(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *RuntimeIo,
    const APPLE_AGX_G13_QUEUE_PROVIDER_IO *ProviderIo) {
  assert(Provider != NULL && Config != NULL && RuntimeIo != NULL &&
         ProviderIo != NULL);
  assert(Config->Ta.EventNumber == 0u);
  assert(Config->D3.EventNumber == 1u);
  assert(RuntimeIo->Context != NULL && RuntimeIo->SendRunMessage != NULL);
  g_components->CapturedProviderIo = *ProviderIo;
  memset(Provider, 0, sizeof(*Provider));
  Provider->Phase = AppleAgxG13QueueProviderInitialized;
  ++g_components->QueueInitializeCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderInstall(
    APPLE_AGX_BACKEND_IO *Io) {
  assert(Io != NULL && Io->Context != NULL);
  ++g_components->QueueInstallCalls;
  Io->Queues.Create = dummy_queue_create;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderCheckTimeout(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U64 NowTicks,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  assert(Provider != NULL && NowTicks == 1234ULL && Batch != NULL);
  ++g_components->TimeoutChecks;
  if (g_components->FailTimeout)
    return APPLE_AGX_BACKEND_FALSE;
  memset(Batch, 0, sizeof(*Batch));
  Batch->ObservationCount = 1u;
  Batch->Observations[0].Status = AppleAgxBackendObservationTimeout;
  Batch->CompletedFence = 77u;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderPlanJob(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider,
    APPLE_AGX_G13_QUEUE_JOB_PLAN *Plan) {
  assert(Provider != NULL && Plan != NULL);
  Plan->TaExpectedDonePointer = 2u;
  Plan->D3ExpectedDonePointer = 2u;
  Plan->IncludeInitBm = APPLE_AGX_BACKEND_TRUE;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerInitialize(
    APPLE_AGX_PLATFORM_COMPOSER *Composer,
    const APPLE_AGX_PLATFORM_COMPOSER_CONFIG *Config,
    APPLE_AGX_BACKEND_IO *Io) {
  assert(Composer != NULL && Config != NULL && Io != NULL);
  assert(Config->QueueProvider != NULL && Config->Runtime != NULL);
  g_components->CapturedRenderIo = *Config->Render;
  g_components->CapturedQueueIo = *Config->Queues;
  memset(Composer, 0, sizeof(*Composer));
  Composer->Initialized = APPLE_AGX_BACKEND_TRUE;
  memset(Io, 0, sizeof(*Io));
  Io->Context = Composer;
  ++g_components->ComposerInitializeCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL external_build_job(
    void *Context, const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_U32 TaEvent, APPLE_AGX_BACKEND_U32 D3Event,
    const APPLE_AGX_G13_QUEUE_JOB_PLAN *Plan,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  FAKE_EXTERNAL_RENDER *external = Context;
  (void)external;
  assert(SubmissionBytes != NULL && SubmissionByteCount != 0u);
  assert(Submission != NULL && Submission->Submission.Fence == 77u);
  assert(TaEvent == 0u && D3Event == 1u);
  assert(Plan != NULL && Plan->TaExpectedDonePointer == 2u &&
         Plan->D3ExpectedDonePointer == 2u && Plan->IncludeInitBm);
  assert(Job != NULL);
  memset(Job, 0, sizeof(*Job));
  Job->TaWorkAddresses[0] = 0x1500880000ULL;
  Job->TaWorkAddresses[1] = 0x1500898000ULL;
  Job->TaWorkAddressCount = 2u;
  Job->D3WorkAddresses[0] = 0x1500870000ULL;
  Job->D3WorkAddresses[1] = 0x1500890000ULL;
  Job->D3WorkAddressCount = 2u;
  Job->TaEvent = TaEvent;
  Job->D3Event = D3Event;
  ++g_components->ExternalBuildCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL external_resolve_range(
    void *Context, APPLE_AGX_BACKEND_U64 GpuAddress,
    const void **CpuAddress, APPLE_AGX_BACKEND_U32 *Bytes) {
  FAKE_EXTERNAL_RENDER *external = Context;
  unsigned int index;
  static const APPLE_AGX_BACKEND_U64 addresses[4] = {
      0x1500880000ULL, 0x1500898000ULL,
      0x1500870000ULL, 0x1500890000ULL};
  assert(external != NULL && CpuAddress != NULL && Bytes != NULL);
  for (index = 0u; index < 4u; ++index) {
    if (addresses[index] == GpuAddress) {
      *CpuAddress = external->Ranges[index];
      *Bytes = sizeof(external->Ranges[index]);
      ++g_components->ExternalResolveCalls;
      return APPLE_AGX_BACKEND_TRUE;
    }
  }
  return APPLE_AGX_BACKEND_FALSE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerPrepareEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  assert(Composer != NULL && Composer->Initialized && Message != NULL &&
         MessageBytes == APPLE_AGX_G13_EVENT_MESSAGE_SIZE &&
         Batch != NULL);
  ++g_components->PrepareCalls;
  if (g_components->FailPrepareAt == g_components->PrepareCalls)
    return APPLE_AGX_BACKEND_FALSE;
  memset(Batch, 0, sizeof(*Batch));
  Batch->CompletedFence = Message[0];
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerApplyEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    APPLE_AGX_BACKEND_U32 *CompletedFence) {
  assert(Composer != NULL && Composer->Initialized && Batch != NULL &&
         CompletedFence != NULL && g_transport != NULL);
  ++g_components->ApplyCalls;
  /* Applying the batch may complete a Windows fence, so the event-ring ACK
   * must already be globally visible. */
  if (Batch->ObservationCount == 0u ||
      Batch->Observations[0].Status == AppleAgxBackendObservationComplete)
    assert(g_transport->PublishCalls != 0u);
  if (g_components->FailApplyAt == g_components->ApplyCalls)
    return APPLE_AGX_BACKEND_FALSE;
  *CompletedFence = Batch->CompletedFence;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL flush_device(void *Context,
                                            const void *Address,
                                            APPLE_AGX_BACKEND_U32 Bytes) {
  FAKE_TRANSPORT *fake = Context;
  assert(Address != NULL && Bytes != 0u);
  ++fake->FlushDeviceCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL flush_cpu(void *Context, const void *Address,
                                        APPLE_AGX_BACKEND_U32 Bytes) {
  FAKE_TRANSPORT *fake = Context;
  assert(Address != NULL && Bytes != 0u);
  ++fake->FlushCpuCalls;
  return APPLE_AGX_BACKEND_TRUE;
}

static void barrier(void *Context) {
  ++((FAKE_TRANSPORT *)Context)->BarrierCalls;
}

static APPLE_AGX_BACKEND_BOOL publish_u32(
    void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 Value) {
  FAKE_TRANSPORT *fake = (FAKE_TRANSPORT *)Context;
  ++fake->PublishCalls;
  if (fake->FailPublishAt == fake->PublishCalls)
    return APPLE_AGX_BACKEND_FALSE;
  *Address = Value;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL read_u32(
    void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
    APPLE_AGX_BACKEND_U32 *Value) {
  (void)Context;
  if (Address == NULL || Value == NULL)
    return APPLE_AGX_BACKEND_FALSE;
  *Value = *Address;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_BOOL ring_doorbell(
    void *Context, APPLE_AGX_BACKEND_U32 Doorbell) {
  FAKE_TRANSPORT *fake = Context;
  ++fake->DoorbellCalls;
  fake->LastDoorbell = Doorbell;
  return APPLE_AGX_BACKEND_TRUE;
}

static APPLE_AGX_BACKEND_U64 now_ticks(void *Context) {
  FAKE_TRANSPORT *fake = Context;
  return fake->ZeroNow ? 0ULL : 1234ULL;
}

static APPLE_AGX_BACKEND_BOOL quiesce(void *Context,
                                      APPLE_AGX_BACKEND_U32 Fence) {
  (void)Context;
  return Fence != 0u ? APPLE_AGX_BACKEND_TRUE : APPLE_AGX_BACKEND_FALSE;
}

static APPLE_AGX_PLATFORM_TRANSPORT_IO transport_io(FAKE_TRANSPORT *Fake) {
  APPLE_AGX_PLATFORM_TRANSPORT_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = Fake;
  io.FlushForDevice = flush_device;
  io.FlushForCpu = flush_cpu;
  io.MemoryBarrier = barrier;
  io.PublishU32 = publish_u32;
  io.ReadU32 = read_u32;
  io.RingDoorbell = ring_doorbell;
  io.Quiesce = quiesce;
  io.NowTicks = now_ticks;
  return io;
}

static APPLE_AGX_BACKEND_BOOL queue_flush(
    void *Context, const void *Address, APPLE_AGX_BACKEND_U32 Bytes) {
  return flush_device(Context, Address, Bytes);
}

static APPLE_AGX_BACKEND_BOOL queue_build(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence,
    APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission) {
  (void)Context;
  (void)Job;
  (void)Fence;
  (void)Submission;
  return APPLE_AGX_BACKEND_FALSE;
}

static void prepare_channel_memory(APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner,
                                   unsigned char Storage[35][0x4000]) {
  unsigned int index;
  memset(Owner, 0, sizeof(*Owner));
  memset(Storage, 0, 35u * 0x4000u);
  Owner->Initialized = 1u;
  Owner->Built = 1u;
  Owner->ObjectCount = APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT;
  for (index = 0u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index) {
    Owner->Objects[index].CpuAddress = Storage[index];
    Owner->Objects[index].DeviceAddress = 0x100000000ULL + index * 0x10000ULL;
    Owner->Objects[index].Length = 0x4000u;
    Owner->Objects[index].State = AppleAgxMemoryGpuMapped;
    Owner->VirtualAddresses[index] =
        0xffffffa000100000ULL + index * 0x8000ULL;
  }
  Owner->Objects[AppleAgxChannelMemoryCommandRingBase + 3u].Length = 0x3000u;
  Owner->Objects[AppleAgxChannelMemoryCommandRingBase + 4u].Length = 0x3000u;
  Owner->Objects[AppleAgxChannelMemoryEventRing].Length = 0x3800u;
}

static void test_exact_group1_bindings(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_CHANNEL_BINDINGS bindings;
  unsigned char storage[35][0x4000];
  prepare_channel_memory(&owner, storage);
  memset(&bindings, 0, sizeof(bindings));

  assert(AppleAgxPlatformProviderBindChannels(&owner, &bindings));
  assert(bindings.Ta.StateCpuAddress == storage[3]);
  assert(bindings.Ta.RingCpuAddress == storage[15]);
  assert(bindings.Ta.StateGpuAddress == owner.VirtualAddresses[3]);
  assert(bindings.Ta.RingGpuAddress == owner.VirtualAddresses[15]);
  assert(bindings.Ta.Doorbell == 4u);
  assert(bindings.D3.StateCpuAddress == storage[4]);
  assert(bindings.D3.RingCpuAddress == storage[16]);
  assert(bindings.D3.StateGpuAddress == owner.VirtualAddresses[4]);
  assert(bindings.D3.RingGpuAddress == owner.VirtualAddresses[16]);
  assert(bindings.D3.Doorbell == 5u);
  assert(bindings.Event.StateCpuAddress == storage[26]);
  assert(bindings.Event.RingCpuAddress == storage[27]);
  assert(bindings.Event.StateGpuAddress == owner.VirtualAddresses[26]);
  assert(bindings.Event.RingGpuAddress == owner.VirtualAddresses[27]);

  owner.Built = 0u;
  assert(!AppleAgxPlatformProviderBindChannels(&owner, &bindings));
  owner.Built = 1u;
  owner.Objects[15].Length = 0x2fffu;
  assert(!AppleAgxPlatformProviderBindChannels(&owner, &bindings));
}

static void test_exact_run_channel_publication(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_CHANNEL_BINDINGS bindings;
  APPLE_AGX_PLATFORM_TRANSPORT_IO io;
  FAKE_TRANSPORT fake = {0};
  unsigned char storage[35][0x4000];
  unsigned char message[APPLE_AGX_G13_RUN_MESSAGE_SIZE];
  volatile APPLE_AGX_BACKEND_U32 *read;
  volatile APPLE_AGX_BACKEND_U32 *write;
  unsigned int index;

  prepare_channel_memory(&owner, storage);
  assert(AppleAgxPlatformProviderBindChannels(&owner, &bindings));
  io = transport_io(&fake);
  for (index = 0u; index < sizeof(message); ++index)
    message[index] = (unsigned char)(index + 1u);

  read = (volatile APPLE_AGX_BACKEND_U32 *)(storage[3] + 0x00u);
  write = (volatile APPLE_AGX_BACKEND_U32 *)(storage[3] + 0x20u);
  *read = 0u;
  *write = 0u;
  assert(AppleAgxPlatformProviderPublishRun(
      &bindings, &io, AppleAgxG13QueueTa, message));
  assert(*write == 1u);
  assert(memcmp(storage[15], message, sizeof(message)) == 0);
  assert(fake.LastDoorbell == 4u);

  read = (volatile APPLE_AGX_BACKEND_U32 *)(storage[4] + 0x00u);
  write = (volatile APPLE_AGX_BACKEND_U32 *)(storage[4] + 0x20u);
  *read = 2u;
  *write = 255u;
  assert(AppleAgxPlatformProviderPublishRun(
      &bindings, &io, AppleAgxG13Queue3d, message));
  assert(*write == 0u);
  assert(memcmp(storage[16] + 255u * sizeof(message), message,
                sizeof(message)) == 0);
  assert(fake.LastDoorbell == 5u);

  *read = 1u;
  *write = 0u;
  assert(!AppleAgxPlatformProviderPublishRun(
      &bindings, &io, AppleAgxG13Queue3d, message));
  assert(fake.DoorbellCalls == 2u);

  io.FlushForDevice = NULL;
  assert(!AppleAgxPlatformProviderPublishRun(
      &bindings, &io, AppleAgxG13QueueTa, message));
}

static APPLE_AGX_PLATFORM_PROVIDER_CONFIG provider_config(
    APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner, FAKE_TRANSPORT *Transport,
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *RenderSharedMemory) {
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG config;
  static APPLE_AGX_FIRMWARE_IO firmware;
  static APPLE_AGX_BACKEND_IO render;
  static APPLE_AGX_RENDER_PROVIDER render_provider;
  static APPLE_AGX_INITDATA_MEMORY_GRAPH initdata;
  memset(&config, 0, sizeof(config));
  memset(&firmware, 0, sizeof(firmware));
  memset(&render, 0, sizeof(render));
  memset(&render_provider, 0, sizeof(render_provider));
  memset(&initdata, 0, sizeof(initdata));
  initdata.ChannelMemory = *Owner;
  initdata.RenderSharedMemory = *RenderSharedMemory;
  render_provider.Initdata = &initdata;
  config.ChannelMemory = &initdata.ChannelMemory;
  config.Transport = transport_io(Transport);
  config.Firmware = &firmware;
  config.Render = &render;
  config.RenderProvider = &render_provider;
  render.Context = &render_provider;
  render.Image.Relocate = dummy_relocate;
  render.RenderContext.Publish = dummy_render_lifetime;
  render.RenderContext.Unpublish = dummy_render_lifetime;
  config.Runtime = Runtime;
  config.RenderSharedMemory = &initdata.RenderSharedMemory;
  config.QueueRuntimeIo.Context = Transport;
  config.QueueRuntimeIo.FlushForDevice = queue_flush;
  config.QueueRuntimeIo.MemoryBarrier = barrier;
  config.QueueRuntimeIo.PublishU32 = publish_u32;
  config.QueueRuntimeIo.ReadU32 = read_u32;
  config.QueueRuntimeIo.SendRunMessage = NULL;
  config.QueueRuntimeIo.Quiesce = quiesce;
  config.QueueProviderIo.Context = Transport;
  config.QueueProviderIo.BuildSubmission = queue_build;
  config.QueueConfig.Ta.QueueType = AppleAgxG13QueueTa;
  config.QueueConfig.D3.QueueType = AppleAgxG13Queue3d;
  config.QueueConfig.TimeoutTicks = 500u;
  return config;
}

static void prepare_render_shared_memory(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    unsigned char Storage[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT]
                         [0x8000]) {
  unsigned int index;
  memset(Owner, 0, sizeof(*Owner));
  memset(Storage, 0,
         APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT * 0x8000u);
  Owner->Initialized = APPLE_AGX_TRUE;
  Owner->Built = APPLE_AGX_TRUE;
  Owner->ObjectCount = APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    Owner->Objects[index].CpuAddress = Storage[index];
    Owner->Objects[index].DeviceAddress = 0x50000000ULL + index * 0x10000ULL;
    Owner->Objects[index].Length = 0x8000u;
    Owner->Objects[index].Context = 0u;
    Owner->Objects[index].State = AppleAgxMemoryGpuMapped;
    Owner->VirtualAddresses[index] =
        0xffffffa010000000ULL + index * 0x8000ULL;
    Owner->Objects[index].GpuVirtualAddress = Owner->VirtualAddresses[index];
  }
}

static void test_persistent_owner_and_bounded_event_drain(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_PROVIDER provider;
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG config;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  FAKE_TRANSPORT transport = {0};
  FAKE_COMPONENTS components = {0};
  unsigned char storage[35][0x4000];
  volatile APPLE_AGX_BACKEND_U32 *event_read;
  volatile APPLE_AGX_BACKEND_U32 *event_write;
  APPLE_AGX_BACKEND_U32 drained = 0u;
  APPLE_AGX_BACKEND_U32 completed = 0u;
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER render_shared;
  static unsigned char
      render_storage[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT][0x8000];
  APPLE_AGX_BACKEND_JOB_IMAGE job;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION built_submission;

  memset(&provider, 0, sizeof(provider));
  memset(&runtime, 0, sizeof(runtime));
  memset(&io, 0, sizeof(io));
  assert(!AppleAgxPlatformProviderPoll(&provider, 8u, &drained, &completed));
  assert(provider.LastPollGuard == AppleAgxPlatformPollGuardInvalid);
  prepare_channel_memory(&owner, storage);
  prepare_render_shared_memory(&render_shared, render_storage);
  config = provider_config(&owner, &transport, &runtime, &render_shared);
  g_components = &components;
  g_transport = &transport;

  assert(AppleAgxPlatformProviderInitialize(&provider, &config, &io));
  assert(provider.Initialized);
  assert(provider.EventPair.Ta == 0u && provider.EventPair.D3 == 1u);
  assert(components.QueueInitializeCalls == 1u);
  assert(components.QueueInstallCalls == 1u);
  assert(components.ComposerInitializeCalls == 1u);
  memset(&job, 0, sizeof(job));
  job.TaWorkAddresses[0] = render_shared.VirtualAddresses[16];
  job.TaWorkAddresses[1] = render_shared.VirtualAddresses[19];
  job.TaWorkAddressCount = 2u;
  job.D3WorkAddresses[0] = render_shared.VirtualAddresses[14];
  job.D3WorkAddresses[1] = render_shared.VirtualAddresses[18];
  job.D3WorkAddressCount = 2u;
  memset(&built_submission, 0, sizeof(built_submission));
  assert(components.CapturedProviderIo.BuildSubmission(
      components.CapturedProviderIo.Context, &job, 77u,
      &built_submission));
  assert(built_submission.Fence == 77u && built_submission.NowTicks == 1234u);
  assert(built_submission.Ta.PreparedRangeCount == 2u);
  assert(built_submission.Ta.PreparedRanges[0].Address ==
         render_shared.Objects[16].CpuAddress);
  assert(built_submission.Ta.PreparedRanges[1].Address ==
         render_shared.Objects[19].CpuAddress);
  assert(built_submission.D3.PreparedRangeCount == 2u);
  assert(built_submission.D3.PreparedRanges[0].Address ==
         render_shared.Objects[14].CpuAddress);
  assert(built_submission.D3.PreparedRanges[1].Address ==
         render_shared.Objects[18].CpuAddress);

  event_read = (volatile APPLE_AGX_BACKEND_U32 *)(storage[26] + 0x00u);
  event_write = (volatile APPLE_AGX_BACKEND_U32 *)(storage[26] + 0x20u);
  *event_read = 0u;
  *event_write = 3u;
  storage[27][0x00u] = 7u;
  storage[27][0x38u] = 8u;
  storage[27][0x70u] = 9u;

  assert(AppleAgxPlatformProviderDrainEvents(
      &provider, 2u, &drained, &completed));
  assert(drained == 2u && completed == 8u && *event_read == 2u);
  assert(components.PrepareCalls == 2u && components.ApplyCalls == 2u);

  components.FailPrepareAt = 3u;
  assert(!AppleAgxPlatformProviderDrainEvents(
      &provider, 2u, &drained, &completed));
  assert(drained == 0u && *event_read == 2u);
  assert(components.PrepareCalls == 3u && components.ApplyCalls == 2u);

  components.FailPrepareAt = 0u;
  assert(AppleAgxPlatformProviderDrainEvents(
      &provider, 2u, &drained, &completed));
  assert(drained == 1u && completed == 9u && *event_read == 3u);

  *event_write = 4u;
  storage[27][0xa8u] = 10u;
  transport.FailPublishAt = transport.PublishCalls + 1u;
  assert(!AppleAgxPlatformProviderDrainEvents(
      &provider, 2u, &drained, &completed));
  assert(drained == 0u && completed == 0u && *event_read == 3u);
  assert(components.PrepareCalls == 5u && components.ApplyCalls == 3u);
  assert(provider.EventBatchPending && !provider.EventBatchAcknowledged);

  transport.FailPublishAt = 0u;
  components.FailApplyAt = 4u;
  assert(!AppleAgxPlatformProviderDrainEvents(
      &provider, 2u, &drained, &completed));
  assert(drained == 1u && completed == 0u && *event_read == 4u);
  assert(components.PrepareCalls == 5u && components.ApplyCalls == 4u);
  assert(provider.EventBatchPending && provider.EventBatchAcknowledged);
  /* Retry the durably transferred batch: no re-decode and no duplicate ACK. */
  {
    unsigned int publish_calls = transport.PublishCalls;
    assert(AppleAgxPlatformProviderDrainEvents(
        &provider, 2u, &drained, &completed));
    assert(drained == 0u && completed == 10u && *event_read == 4u);
    assert(components.PrepareCalls == 5u && components.ApplyCalls == 5u);
    assert(transport.PublishCalls == publish_calls);
    assert(!provider.EventBatchPending);
  }
  assert(AppleAgxPlatformProviderDestroy(&provider));
  assert(!provider.Initialized);

  config.Transport.FlushForCpu = NULL;
  assert(!AppleAgxPlatformProviderInitialize(&provider, &config, &io));

  config = provider_config(&owner, &transport, &runtime, &render_shared);
  config.ChannelMemory = &owner;
  assert(!AppleAgxPlatformProviderInitialize(&provider, &config, &io));
}

static void test_bounded_poll_checks_timeout_after_empty_event_ring(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_PROVIDER provider;
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG config;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  FAKE_TRANSPORT transport = {0};
  FAKE_COMPONENTS components = {0};
  unsigned char storage[35][0x4000];
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER render_shared;
  static unsigned char
      render_storage[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT][0x8000];
  APPLE_AGX_BACKEND_U32 drained = 99u;
  APPLE_AGX_BACKEND_U32 completed = 99u;
  volatile APPLE_AGX_BACKEND_U32 *event_read;
  volatile APPLE_AGX_BACKEND_U32 *event_write;

  memset(&provider, 0, sizeof(provider));
  memset(&runtime, 0, sizeof(runtime));
  memset(&io, 0, sizeof(io));
  prepare_channel_memory(&owner, storage);
  prepare_render_shared_memory(&render_shared, render_storage);
  config = provider_config(&owner, &transport, &runtime, &render_shared);
  g_components = &components;
  g_transport = &transport;
  assert(AppleAgxPlatformProviderInitialize(&provider, &config, &io));
  runtime.Phase = AppleAgxBackendRuntimeSubmitted;

  event_read = (volatile APPLE_AGX_BACKEND_U32 *)(storage[26] + 0x00u);
  event_write = (volatile APPLE_AGX_BACKEND_U32 *)(storage[26] + 0x20u);
  *event_read = 0u;
  *event_write = 1u;
  storage[27][0] = 1u;
  assert(AppleAgxPlatformProviderPoll(&provider, 1u, &drained, &completed));
  assert(drained == 1u);
  assert(components.TimeoutChecks == 0u);

  assert(AppleAgxPlatformProviderPoll(&provider, 8u, &drained, &completed));
  assert(drained == 0u);
  assert(completed == 77u);
  assert(components.TimeoutChecks == 1u);
  assert(components.ApplyCalls == 2u);
}

static void test_poll_records_exact_failure_owner(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_PROVIDER provider;
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG config;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  FAKE_TRANSPORT transport = {0};
  FAKE_COMPONENTS components = {0};
  unsigned char storage[35][0x4000];
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER render_shared;
  static unsigned char
      render_storage[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT][0x8000];
  APPLE_AGX_BACKEND_U32 drained = 99u;
  APPLE_AGX_BACKEND_U32 completed = 99u;
  volatile APPLE_AGX_BACKEND_U32 *event_read;
  volatile APPLE_AGX_BACKEND_U32 *event_write;

  memset(&provider, 0, sizeof(provider));
  memset(&runtime, 0, sizeof(runtime));
  memset(&io, 0, sizeof(io));
  prepare_channel_memory(&owner, storage);
  prepare_render_shared_memory(&render_shared, render_storage);
  config = provider_config(&owner, &transport, &runtime, &render_shared);
  g_components = &components;
  g_transport = &transport;
  assert(AppleAgxPlatformProviderInitialize(&provider, &config, &io));
  runtime.Phase = AppleAgxBackendRuntimeSubmitted;
  event_read = (volatile APPLE_AGX_BACKEND_U32 *)(storage[26] + 0x00u);
  event_write = (volatile APPLE_AGX_BACKEND_U32 *)(storage[26] + 0x20u);

  *event_read = 0u;
  *event_write = APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT;
  assert(!AppleAgxPlatformProviderPoll(&provider, 8u, &drained, &completed));
  assert(provider.LastPollGuard == AppleAgxPlatformPollGuardDrainEvents);

  *event_write = 0u;
  transport.ZeroNow = 1u;
  assert(!AppleAgxPlatformProviderPoll(&provider, 8u, &drained, &completed));
  assert(provider.LastPollGuard == AppleAgxPlatformPollGuardClock);

  transport.ZeroNow = 0u;
  components.FailTimeout = 1u;
  assert(!AppleAgxPlatformProviderPoll(&provider, 8u, &drained, &completed));
  assert(provider.LastPollGuard == AppleAgxPlatformPollGuardTimeoutCheck);

  components.FailTimeout = 0u;
  components.FailApplyAt = components.ApplyCalls + 1u;
  assert(!AppleAgxPlatformProviderPoll(&provider, 8u, &drained, &completed));
  assert(provider.LastPollGuard == AppleAgxPlatformPollGuardTimeoutApply);

  components.FailApplyAt = 0u;
  assert(AppleAgxPlatformProviderPoll(&provider, 8u, &drained, &completed));
  assert(provider.LastPollGuard == AppleAgxPlatformPollGuardOk);
  assert(AppleAgxPlatformProviderDestroy(&provider));
}

static void test_external_rebased_image_mode_uses_exact_job_and_ranges(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_PROVIDER provider;
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG config;
  APPLE_AGX_BACKEND_RUNTIME runtime;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_BACKEND_SUBMISSION submission;
  APPLE_AGX_BACKEND_JOB_IMAGE job;
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION built;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  FAKE_TRANSPORT transport = {0};
  FAKE_COMPONENTS components = {0};
  FAKE_EXTERNAL_RENDER external = {{{0}}};
  unsigned char storage[35][0x4000];
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER render_shared;
  static unsigned char
      render_storage[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT][0x8000];
  unsigned char command[32] = {1u};

  memset(&provider, 0, sizeof(provider));
  memset(&runtime, 0, sizeof(runtime));
  memset(&io, 0, sizeof(io));
  memset(&submission, 0, sizeof(submission));
  memset(&roots, 0, sizeof(roots));
  prepare_channel_memory(&owner, storage);
  prepare_render_shared_memory(&render_shared, render_storage);
  config = provider_config(
      &owner, &transport, &runtime, &render_shared);
  config.ChannelMemory = &owner;
  config.RenderProvider = NULL;
  config.ExternalRender.Context = &external;
  config.ExternalRender.BuildJob = external_build_job;
  config.ExternalRender.ResolvePreparedRange = external_resolve_range;
  g_components = &components;
  g_transport = &transport;

  assert(AppleAgxPlatformProviderInitialize(&provider, &config, &io));
  submission.Submission.Fence = 77u;
  assert(components.CapturedRenderIo.Image.Relocate(
      components.CapturedRenderIo.Context, external.Ranges,
      sizeof(external.Ranges), &roots, command, sizeof(command),
      &submission, &job));
  assert(components.ExternalBuildCalls == 1u);
  memset(&built, 0, sizeof(built));
  assert(components.CapturedProviderIo.BuildSubmission(
      components.CapturedProviderIo.Context, &job, 77u, &built));
  assert(components.ExternalResolveCalls == 4u);
  assert(built.Ta.PreparedRanges[0].Address == external.Ranges[0]);
  assert(built.D3.PreparedRanges[0].Address == external.Ranges[2]);
  assert(AppleAgxPlatformProviderDestroy(&provider));
}

static void test_deferred_bind_does_not_authorize_queue_creation(void) {
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  APPLE_AGX_PLATFORM_PROVIDER provider = {0};
  APPLE_AGX_PLATFORM_PROVIDER_CONFIG config;
  APPLE_AGX_BACKEND_RUNTIME runtime = {0};
  APPLE_AGX_BACKEND_IO io = {0};
  FAKE_TRANSPORT transport = {0};
  FAKE_COMPONENTS components = {0};
  unsigned char storage[35][0x4000];
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER render_shared;
  static unsigned char render_storage[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT][0x8000];
  unsigned int i;
  prepare_channel_memory(&owner, storage);
  prepare_render_shared_memory(&render_shared, render_storage);
  for (i = 0; i < owner.ObjectCount; ++i) {
    owner.Objects[i].State = AppleAgxMemoryPrepared;
    owner.Objects[i].GpuVirtualAddress = 0;
  }
  for (i = 0; i < render_shared.ObjectCount; ++i) {
    render_shared.Objects[i].State = AppleAgxMemoryPrepared;
    render_shared.Objects[i].GpuVirtualAddress = 0;
  }
  config = provider_config(&owner, &transport, &runtime, &render_shared);
  config.DeferFirmwareMappings = APPLE_AGX_BACKEND_TRUE;
  g_components = &components; g_transport = &transport;
  assert(AppleAgxPlatformProviderInitialize(&provider, &config, &io));
  assert(!components.CapturedQueueIo.Queues.Create(components.CapturedQueueIo.Context));
  assert(transport.DoorbellCalls == 0 && transport.PublishCalls == 0);
  for (i = 0; i < config.RenderSharedMemory->ObjectCount; ++i) {
    config.RenderSharedMemory->Objects[i].State = AppleAgxMemoryGpuMapped;
    config.RenderSharedMemory->Objects[i].GpuVirtualAddress = config.RenderSharedMemory->VirtualAddresses[i];
  }
  assert(components.CapturedQueueIo.Queues.Create(components.CapturedQueueIo.Context));
  assert(AppleAgxPlatformProviderDestroy(&provider));
}

int main(void) {
  test_deferred_bind_does_not_authorize_queue_creation();
  test_exact_group1_bindings();
  test_exact_run_channel_publication();
  test_persistent_owner_and_bounded_event_drain();
  test_bounded_poll_checks_timeout_after_empty_event_ring();
  test_poll_records_exact_failure_owner();
  test_external_rebased_image_mode_uses_exact_job_and_ranges();
  puts("apple_agx_platform_provider_test: ok");
  return 0;
}
