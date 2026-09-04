#include "apple_agx_platform_composer.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_COMPONENTS {
  unsigned int FirmwareCalls;
  unsigned int RenderCalls;
  unsigned int QueueCalls;
  unsigned int Observations;
  unsigned int FailObservationAt;
} FAKE_COMPONENTS;

static APPLE_AGX_FW_U64 now_ms(void *Context) {
  ++((FAKE_COMPONENTS *)Context)->FirmwareCalls;
  return 10u;
}
static APPLE_AGX_FW_BOOL fw_simple(void *Context, APPLE_AGX_FW_U64 Deadline) {
  ++((FAKE_COMPONENTS *)Context)->FirmwareCalls;
  return Deadline >= 10u;
}
static APPLE_AGX_FW_BOOL fw_endpoint(void *Context, APPLE_AGX_FW_U32 Endpoint,
                                     APPLE_AGX_FW_U64 Deadline) {
  (void)Endpoint;
  return fw_simple(Context, Deadline);
}
static APPLE_AGX_FW_BOOL fw_publish(void *Context, APPLE_AGX_FW_U64 Deadline,
                                    APPLE_AGX_FW_U64 *Address) {
  if (!fw_simple(Context, Deadline))
    return 0u;
  *Address = 0x1000u;
  return 1u;
}
static APPLE_AGX_FW_BOOL fw_send(void *Context, APPLE_AGX_FW_U64 Address,
                                 APPLE_AGX_FW_U64 Deadline) {
  return Address == 0x1000u ? fw_simple(Context, Deadline) : 0u;
}
static APPLE_AGX_BACKEND_BOOL relocate(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *Bytes, APPLE_AGX_BACKEND_U32 ByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  FAKE_COMPONENTS *fake = Context;
  (void)Arena; (void)ArenaBytes; (void)Roots; (void)Bytes; (void)ByteCount;
  (void)Submission; (void)Job;
  ++fake->RenderCalls;
  return 1u;
}
static APPLE_AGX_BACKEND_BOOL render_simple(void *Context) {
  ++((FAKE_COMPONENTS *)Context)->RenderCalls;
  return 1u;
}
static APPLE_AGX_BACKEND_BOOL queue_simple(void *Context) {
  ++((FAKE_COMPONENTS *)Context)->QueueCalls;
  return 1u;
}
static APPLE_AGX_BACKEND_BOOL queue_run(
    void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,
    APPLE_AGX_BACKEND_U32 Fence) {
  (void)Job;
  if (Fence == 0u)
    return 0u;
  ++((FAKE_COMPONENTS *)Context)->QueueCalls;
  return 1u;
}
static APPLE_AGX_BACKEND_BOOL queue_fence(void *Context,
                                          APPLE_AGX_BACKEND_U32 Fence) {
  (void)Fence;
  ++((FAKE_COMPONENTS *)Context)->QueueCalls;
  return 1u;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderIngestEvent(
    APPLE_AGX_G13_QUEUE_PROVIDER *Provider, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  (void)Provider; (void)Message; (void)MessageBytes;
  memset(Batch, 0, sizeof(*Batch));
  Batch->ObservationCount = 2u;
  Batch->Observations[0].Queue = AppleAgxBackendQueueTa;
  Batch->Observations[1].Queue = AppleAgxBackendQueue3d;
  Batch->CompletedFence = 7u;
  return 1u;
}
APPLE_AGX_BACKEND_RUNTIME_RESULT AppleAgxBackendRuntimeObserve(
    APPLE_AGX_BACKEND_RUNTIME *Runtime,
    const APPLE_AGX_BACKEND_OBSERVATION *Observation) {
  FAKE_COMPONENTS *fake = (FAKE_COMPONENTS *)Runtime;
  (void)Observation;
  ++fake->Observations;
  if (fake->FailObservationAt == fake->Observations)
    return AppleAgxBackendRuntimeResultBusy;
  return AppleAgxBackendRuntimeResultOk;
}

static APPLE_AGX_FIRMWARE_IO firmware_io(FAKE_COMPONENTS *Fake) {
  APPLE_AGX_FIRMWARE_IO io;
  memset(&io, 0, sizeof(io));
  io.Context = Fake;
  io.NowMs = now_ms;
  io.PowerOn = fw_simple;
  io.CreateFirmwareUat = fw_simple;
  io.BootAsc = fw_simple;
  io.StartEndpoint = fw_endpoint;
  io.PublishInitdata = fw_publish;
  io.SendInitdata = fw_send;
  io.SendDeviceControlInit = fw_simple;
  io.UpdateIdleTimestamp = fw_simple;
  io.UnpublishInitdata = fw_simple;
  io.StopEndpoint = fw_endpoint;
  io.StopAsc = fw_simple;
  io.DestroyFirmwareUat = fw_simple;
  io.PowerOff = fw_simple;
  return io;
}

int main(void) {
  FAKE_COMPONENTS fake = {0};
  APPLE_AGX_FIRMWARE_IO firmware = firmware_io(&fake);
  APPLE_AGX_BACKEND_IO render;
  APPLE_AGX_BACKEND_IO queues;
  APPLE_AGX_BACKEND_IO combined;
  APPLE_AGX_PLATFORM_COMPOSER composer_state;
  APPLE_AGX_G13_QUEUE_PROVIDER queue_provider;
  APPLE_AGX_PLATFORM_COMPOSER_CONFIG config;
  APPLE_AGX_BACKEND_JOB_IMAGE job;
  APPLE_AGX_BACKEND_U32 completed = 0u;
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH batch;
  unsigned char event[4] = {0};

  memset(&render, 0, sizeof(render));
  memset(&queues, 0, sizeof(queues));
  memset(&combined, 0, sizeof(combined));
  memset(&composer_state, 0, sizeof(composer_state));
  memset(&queue_provider, 0, sizeof(queue_provider));
  memset(&job, 0, sizeof(job));
  render.Context = &fake;
  render.Image.Relocate = relocate;
  render.RenderContext.Publish = render_simple;
  render.RenderContext.Unpublish = render_simple;
  queues.Context = &fake;
  queues.Queues.Create = queue_simple;
  queues.Queues.Destroy = queue_simple;
  queues.Queues.Run3d = queue_run;
  queues.Queues.RunTa = queue_run;
  queues.Queues.Stop = queue_fence;
  queues.Queues.Reset = queue_fence;
  config.Firmware = &firmware;
  config.Render = &render;
  config.Queues = &queues;
  config.QueueProvider = &queue_provider;
  config.Runtime = (APPLE_AGX_BACKEND_RUNTIME *)&fake;

  assert(AppleAgxPlatformComposerInitialize(&composer_state, &config,
                                             &combined));
  assert(combined.Firmware.NowMs(combined.Context) == 10u);
  assert(combined.Firmware.PowerOn(combined.Context, 10u));
  assert(combined.RenderContext.Publish(combined.Context));
  assert(combined.Queues.Create(combined.Context));
  assert(combined.Queues.Run3d(combined.Context, &job, 7u));
  assert(fake.FirmwareCalls == 2u);
  assert(fake.RenderCalls == 1u);
  assert(fake.QueueCalls == 2u);
  memset(&batch, 0, sizeof(batch));
  assert(AppleAgxPlatformComposerPrepareEvent(
      &composer_state, event, sizeof(event), &batch));
  fake.FailObservationAt = 2u;
  assert(!AppleAgxPlatformComposerApplyEvent(
      &composer_state, &batch, &completed));
  assert(batch.AppliedObservationCount == 1u);
  fake.FailObservationAt = 0u;
  assert(AppleAgxPlatformComposerApplyEvent(
      &composer_state, &batch, &completed));
  assert(completed == 7u);
  assert(fake.Observations == 3u);
  assert(batch.AppliedObservationCount == 2u);
  return 0;
}
