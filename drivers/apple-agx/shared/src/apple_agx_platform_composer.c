#include "apple_agx_platform_composer.h"

#define COMPOSER_NULL ((void *)0)

static APPLE_AGX_PLATFORM_COMPOSER *composer(void *Context) {
  APPLE_AGX_PLATFORM_COMPOSER *value = Context;
  return value != COMPOSER_NULL && value->Initialized ? value : COMPOSER_NULL;
}

static APPLE_AGX_FW_U64 fw_now(void *Context) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL ? c->Firmware.NowMs(c->Firmware.Context) : 0ULL;
}
#define FW_SIMPLE(Name, Field)                                                \
  static APPLE_AGX_FW_BOOL Name(void *Context, APPLE_AGX_FW_U64 Deadline) {  \
    APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);                       \
    return c != COMPOSER_NULL                                                 \
               ? c->Firmware.Field(c->Firmware.Context, Deadline)             \
               : APPLE_AGX_FW_FALSE;                                          \
  }
FW_SIMPLE(fw_power_on, PowerOn)
FW_SIMPLE(fw_create_uat, CreateFirmwareUat)
FW_SIMPLE(fw_boot_asc, BootAsc)
FW_SIMPLE(fw_device_control, SendDeviceControlInit)
FW_SIMPLE(fw_idle_timestamp, UpdateIdleTimestamp)
FW_SIMPLE(fw_unpublish, UnpublishInitdata)
FW_SIMPLE(fw_stop_asc, StopAsc)
FW_SIMPLE(fw_destroy_uat, DestroyFirmwareUat)
FW_SIMPLE(fw_power_off, PowerOff)
#undef FW_SIMPLE

static APPLE_AGX_FW_BOOL fw_endpoint(void *Context, APPLE_AGX_FW_U32 Endpoint,
                                      APPLE_AGX_FW_U64 Deadline) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL
             ? c->Firmware.StartEndpoint(c->Firmware.Context, Endpoint,
                                         Deadline)
             : APPLE_AGX_FW_FALSE;
}
static APPLE_AGX_FW_BOOL fw_publish(void *Context, APPLE_AGX_FW_U64 Deadline,
                                     APPLE_AGX_FW_U64 *Address) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL
             ? c->Firmware.PublishInitdata(c->Firmware.Context, Deadline,
                                           Address)
             : APPLE_AGX_FW_FALSE;
}
static APPLE_AGX_FW_BOOL fw_send(void *Context, APPLE_AGX_FW_U64 Address,
                                  APPLE_AGX_FW_U64 Deadline) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL
             ? c->Firmware.SendInitdata(c->Firmware.Context, Address, Deadline)
             : APPLE_AGX_FW_FALSE;
}
static APPLE_AGX_FW_BOOL fw_stop_endpoint(void *Context,
                                           APPLE_AGX_FW_U32 Endpoint,
                                           APPLE_AGX_FW_U64 Deadline) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL
             ? c->Firmware.StopEndpoint(c->Firmware.Context, Endpoint,
                                        Deadline)
             : APPLE_AGX_FW_FALSE;
}
static void fw_record(void *Context, APPLE_AGX_FIRMWARE_PHASE Phase,
                      APPLE_AGX_FIRMWARE_RESULT Result,
                      APPLE_AGX_FW_U32 CompletedMask) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  if (c != COMPOSER_NULL && c->Firmware.RecordPhase != COMPOSER_NULL)
    c->Firmware.RecordPhase(c->Firmware.Context, Phase, Result, CompletedMask);
}

static APPLE_AGX_BACKEND_BOOL image_relocate(
    void *Context, void *Arena, APPLE_AGX_BACKEND_U32 ArenaBytes,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_BACKEND_U32 SubmissionByteCount,
    const APPLE_AGX_BACKEND_SUBMISSION *Submission,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL
             ? c->Render.Image.Relocate(
                   c->Render.Context, Arena, ArenaBytes, Roots, SubmissionBytes,
                   SubmissionByteCount, Submission, Job)
             : APPLE_AGX_BACKEND_FALSE;
}
static APPLE_AGX_BACKEND_BOOL render_publish(void *Context) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL
             ? c->Render.RenderContext.Publish(c->Render.Context)
             : APPLE_AGX_BACKEND_FALSE;
}
static APPLE_AGX_BACKEND_BOOL render_unpublish(void *Context) {
  APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);
  return c != COMPOSER_NULL
             ? c->Render.RenderContext.Unpublish(c->Render.Context)
             : APPLE_AGX_BACKEND_FALSE;
}

#define QUEUE_SIMPLE(Name, Field)                                           \
  static APPLE_AGX_BACKEND_BOOL Name(void *Context) {                      \
    APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);                     \
    return c != COMPOSER_NULL                                               \
               ? c->Queues.Queues.Field(c->Queues.Context)                  \
               : APPLE_AGX_BACKEND_FALSE;                                  \
  }
QUEUE_SIMPLE(queue_create, Create)
QUEUE_SIMPLE(queue_destroy, Destroy)
#undef QUEUE_SIMPLE
#define QUEUE_RUN(Name, Field)                                               \
  static APPLE_AGX_BACKEND_BOOL Name(                                       \
      void *Context, const APPLE_AGX_BACKEND_JOB_IMAGE *Job,                \
      APPLE_AGX_BACKEND_U32 Fence) {                                         \
    APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);                      \
    return c != COMPOSER_NULL                                                \
               ? c->Queues.Queues.Field(c->Queues.Context, Job, Fence)       \
               : APPLE_AGX_BACKEND_FALSE;                                   \
  }
QUEUE_RUN(queue_run_3d, Run3d)
QUEUE_RUN(queue_run_ta, RunTa)
#undef QUEUE_RUN
#define QUEUE_FENCE(Name, Field)                                             \
  static APPLE_AGX_BACKEND_BOOL Name(void *Context,                          \
                                      APPLE_AGX_BACKEND_U32 Fence) {          \
    APPLE_AGX_PLATFORM_COMPOSER *c = composer(Context);                      \
    return c != COMPOSER_NULL                                                \
               ? c->Queues.Queues.Field(c->Queues.Context, Fence)            \
               : APPLE_AGX_BACKEND_FALSE;                                   \
  }
QUEUE_FENCE(queue_stop, Stop)
QUEUE_FENCE(queue_reset, Reset)
#undef QUEUE_FENCE

static APPLE_AGX_BACKEND_BOOL firmware_valid(const APPLE_AGX_FIRMWARE_IO *Io) {
  return Io != COMPOSER_NULL && Io->Context != COMPOSER_NULL &&
         Io->NowMs != COMPOSER_NULL && Io->PowerOn != COMPOSER_NULL &&
         Io->CreateFirmwareUat != COMPOSER_NULL && Io->BootAsc != COMPOSER_NULL &&
         Io->StartEndpoint != COMPOSER_NULL &&
         Io->PublishInitdata != COMPOSER_NULL && Io->SendInitdata != COMPOSER_NULL &&
         Io->SendDeviceControlInit != COMPOSER_NULL &&
         Io->UpdateIdleTimestamp != COMPOSER_NULL &&
         Io->UnpublishInitdata != COMPOSER_NULL && Io->StopEndpoint != COMPOSER_NULL &&
         Io->StopAsc != COMPOSER_NULL && Io->DestroyFirmwareUat != COMPOSER_NULL &&
         Io->PowerOff != COMPOSER_NULL;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerInitialize(
    APPLE_AGX_PLATFORM_COMPOSER *Composer,
    const APPLE_AGX_PLATFORM_COMPOSER_CONFIG *Config,
    APPLE_AGX_BACKEND_IO *Io) {
  if (Composer == COMPOSER_NULL || Config == COMPOSER_NULL ||
      Io == COMPOSER_NULL || Composer->Initialized ||
      !firmware_valid(Config->Firmware) || Config->Render == COMPOSER_NULL ||
      Config->Queues == COMPOSER_NULL || Config->QueueProvider == COMPOSER_NULL ||
      Config->Runtime == COMPOSER_NULL ||
      Config->Render->Context == COMPOSER_NULL ||
      Config->Render->Image.Relocate == COMPOSER_NULL ||
      Config->Render->RenderContext.Publish == COMPOSER_NULL ||
      Config->Render->RenderContext.Unpublish == COMPOSER_NULL ||
      Config->Queues->Context == COMPOSER_NULL ||
      Config->Queues->Queues.Create == COMPOSER_NULL ||
      Config->Queues->Queues.Destroy == COMPOSER_NULL ||
      Config->Queues->Queues.Run3d == COMPOSER_NULL ||
      Config->Queues->Queues.RunTa == COMPOSER_NULL ||
      Config->Queues->Queues.Stop == COMPOSER_NULL ||
      Config->Queues->Queues.Reset == COMPOSER_NULL)
    return APPLE_AGX_BACKEND_FALSE;

  Composer->Firmware = *Config->Firmware;
  Composer->Render = *Config->Render;
  Composer->Queues = *Config->Queues;
  Composer->QueueProvider = Config->QueueProvider;
  Composer->Runtime = Config->Runtime;
  Composer->Initialized = APPLE_AGX_BACKEND_TRUE;

  Io->Context = Composer;
  Io->Firmware.Context = Composer;
  Io->Firmware.NowMs = fw_now;
  Io->Firmware.PowerOn = fw_power_on;
  Io->Firmware.CreateFirmwareUat = fw_create_uat;
  Io->Firmware.BootAsc = fw_boot_asc;
  Io->Firmware.StartEndpoint = fw_endpoint;
  Io->Firmware.PublishInitdata = fw_publish;
  Io->Firmware.SendInitdata = fw_send;
  Io->Firmware.SendDeviceControlInit = fw_device_control;
  Io->Firmware.UpdateIdleTimestamp = fw_idle_timestamp;
  Io->Firmware.UnpublishInitdata = fw_unpublish;
  Io->Firmware.StopEndpoint = fw_stop_endpoint;
  Io->Firmware.StopAsc = fw_stop_asc;
  Io->Firmware.DestroyFirmwareUat = fw_destroy_uat;
  Io->Firmware.PowerOff = fw_power_off;
  Io->Firmware.RecordPhase = fw_record;
  Io->Image.Relocate = image_relocate;
  Io->RenderContext.Publish = render_publish;
  Io->RenderContext.Unpublish = render_unpublish;
  Io->Queues.Create = queue_create;
  Io->Queues.Destroy = queue_destroy;
  Io->Queues.Run3d = queue_run_3d;
  Io->Queues.RunTa = queue_run_ta;
  Io->Queues.Stop = queue_stop;
  Io->Queues.Reset = queue_reset;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerPrepareEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch) {
  if (Composer == COMPOSER_NULL || !Composer->Initialized ||
      Message == COMPOSER_NULL || MessageBytes == 0u ||
      Batch == COMPOSER_NULL ||
      !AppleAgxG13QueueProviderIngestEvent(Composer->QueueProvider, Message,
                                           MessageBytes, Batch))
    return APPLE_AGX_BACKEND_FALSE;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerApplyEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    APPLE_AGX_BACKEND_U32 *CompletedFence) {
  if (CompletedFence != COMPOSER_NULL)
    *CompletedFence = 0u;
  if (Composer == COMPOSER_NULL || !Composer->Initialized ||
      Batch == COMPOSER_NULL || CompletedFence == COMPOSER_NULL ||
      Batch->ObservationCount >
          APPLE_AGX_G13_QUEUE_PROVIDER_MAX_OBSERVATIONS ||
      Batch->AppliedObservationCount > Batch->ObservationCount)
    return APPLE_AGX_BACKEND_FALSE;
  while (Batch->AppliedObservationCount < Batch->ObservationCount) {
    if (AppleAgxBackendRuntimeObserve(Composer->Runtime,
                                      &Batch->Observations[
                                          Batch->AppliedObservationCount]) !=
        AppleAgxBackendRuntimeResultOk)
      return APPLE_AGX_BACKEND_FALSE;
    ++Batch->AppliedObservationCount;
  }
  *CompletedFence = Batch->CompletedFence;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerIngestEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_BACKEND_U32 *CompletedFence) {
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH batch;
  if (!AppleAgxPlatformComposerPrepareEvent(Composer, Message, MessageBytes,
                                             &batch))
    return APPLE_AGX_BACKEND_FALSE;
  if (!AppleAgxPlatformComposerApplyEvent(Composer, &batch, CompletedFence))
    return APPLE_AGX_BACKEND_FALSE;
  return APPLE_AGX_BACKEND_TRUE;
}

#undef COMPOSER_NULL
