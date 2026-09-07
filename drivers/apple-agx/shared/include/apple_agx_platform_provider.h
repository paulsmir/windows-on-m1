#ifndef APPLE_AGX_PLATFORM_PROVIDER_H
#define APPLE_AGX_PLATFORM_PROVIDER_H

#include "apple_agx_channel_memory.h"
#include "apple_agx_event_allocator.h"
#include "apple_agx_g13_codec.h"
#include "apple_agx_platform_composer.h"
#include "apple_agx_submission_coordinator.h"

/*
 * Source contract:
 *   m1n1/proxyclient/m1n1/fw/agx/channels.py (ChannelInfo group 1)
 *   m1n1/proxyclient/m1n1/fw/agx/initdata.py (ChannelInfo[13])
 *   m1n1/proxyclient/m1n1/agx/channels.py (TX/RX pointer publication)
 * Group-1 TA/3D use command state 3/4, ring 15/16 and doorbell 4/5.
 * ChannelInfo[13] uses event state 26 and ring 27, with 0x100 entries of
 * 0x38 bytes.  Generated J313 sizes remain independently validated below.
 */
#define APPLE_AGX_PLATFORM_TA_CHANNEL_INDEX 3u
#define APPLE_AGX_PLATFORM_D3_CHANNEL_INDEX 4u
#define APPLE_AGX_PLATFORM_TA_DOORBELL 4u
#define APPLE_AGX_PLATFORM_D3_DOORBELL 5u
#define APPLE_AGX_PLATFORM_COMMAND_RING_ENTRY_COUNT 0x100u
#define APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT 0x100u
#define APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET 0x00u
#define APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET 0x20u

typedef struct _APPLE_AGX_PLATFORM_TX_CHANNEL_BINDING {
  unsigned char *StateCpuAddress;
  unsigned char *RingCpuAddress;
  APPLE_AGX_BACKEND_U64 StateGpuAddress;
  APPLE_AGX_BACKEND_U64 RingGpuAddress;
  APPLE_AGX_BACKEND_U32 Doorbell;
} APPLE_AGX_PLATFORM_TX_CHANNEL_BINDING;

typedef struct _APPLE_AGX_PLATFORM_RX_CHANNEL_BINDING {
  unsigned char *StateCpuAddress;
  unsigned char *RingCpuAddress;
  APPLE_AGX_BACKEND_U64 StateGpuAddress;
  APPLE_AGX_BACKEND_U64 RingGpuAddress;
} APPLE_AGX_PLATFORM_RX_CHANNEL_BINDING;

typedef struct _APPLE_AGX_PLATFORM_CHANNEL_BINDINGS {
  APPLE_AGX_PLATFORM_TX_CHANNEL_BINDING Ta;
  APPLE_AGX_PLATFORM_TX_CHANNEL_BINDING D3;
  APPLE_AGX_PLATFORM_RX_CHANNEL_BINDING Event;
} APPLE_AGX_PLATFORM_CHANNEL_BINDINGS;

/*
 * Cache maintenance and the physical ASC doorbell are platform-owned.  The
 * portable provider requires exact implementations and never substitutes a
 * barrier, an interrupt vector, or a cache operation with a fake success.
 */
typedef struct _APPLE_AGX_PLATFORM_TRANSPORT_IO {
  void *Context;
  APPLE_AGX_BACKEND_BOOL (*FlushForDevice)(
      void *Context, const void *Address, APPLE_AGX_BACKEND_U32 Bytes);
  APPLE_AGX_BACKEND_BOOL (*FlushForCpu)(
      void *Context, const void *Address, APPLE_AGX_BACKEND_U32 Bytes);
  void (*MemoryBarrier)(void *Context);
  APPLE_AGX_BACKEND_BOOL (*PublishU32)(
      void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
      APPLE_AGX_BACKEND_U32 Value);
  APPLE_AGX_BACKEND_BOOL (*ReadU32)(
      void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
      APPLE_AGX_BACKEND_U32 *Value);
  APPLE_AGX_BACKEND_BOOL (*RingDoorbell)(
      void *Context, APPLE_AGX_BACKEND_U32 Doorbell);
  APPLE_AGX_BACKEND_BOOL (*Quiesce)(
      void *Context, APPLE_AGX_BACKEND_U32 Fence);
  APPLE_AGX_BACKEND_U64 (*NowTicks)(void *Context);
} APPLE_AGX_PLATFORM_TRANSPORT_IO;

/* Optional adapter for a render image that is already allocated, mapped and
 * published by the Windows memory owner.  It replaces only the legacy
 * RenderProvider/SubmissionCoordinator image preparation; firmware, channel,
 * queue, event and completion ownership remain in this provider. */
typedef struct _APPLE_AGX_PLATFORM_EXTERNAL_RENDER_IO {
  void *Context;
  APPLE_AGX_BACKEND_BOOL (*BuildJob)(
      void *Context, const unsigned char *SubmissionBytes,
      APPLE_AGX_BACKEND_U32 SubmissionByteCount,
      const APPLE_AGX_BACKEND_SUBMISSION *Submission,
      APPLE_AGX_BACKEND_U32 TaEvent,
      APPLE_AGX_BACKEND_U32 D3Event,
      const APPLE_AGX_G13_QUEUE_JOB_PLAN *Plan,
      APPLE_AGX_BACKEND_JOB_IMAGE *Job);
  APPLE_AGX_BACKEND_BOOL (*ResolvePreparedRange)(
      void *Context, APPLE_AGX_BACKEND_U64 GpuAddress,
      const void **CpuAddress, APPLE_AGX_BACKEND_U32 *Bytes);
} APPLE_AGX_PLATFORM_EXTERNAL_RENDER_IO;

/*
 * Persistent owner for the exact group-1 transport slice.  The provider owns
 * the event lease, queue provider, composer and their wrapped transport IO for
 * the entire started-adapter lifetime; none of these objects may live on a
 * StartDevice stack frame.
 */
typedef struct _APPLE_AGX_PLATFORM_PROVIDER_CONFIG {
  APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory;
  APPLE_AGX_PLATFORM_TRANSPORT_IO Transport;
  const APPLE_AGX_FIRMWARE_IO *Firmware;
  const APPLE_AGX_BACKEND_IO *Render;
  APPLE_AGX_RENDER_PROVIDER *RenderProvider;
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *RenderSharedMemory;
  APPLE_AGX_BACKEND_RUNTIME *Runtime;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG QueueConfig;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO QueueRuntimeIo;
  APPLE_AGX_G13_QUEUE_PROVIDER_IO QueueProviderIo;
  APPLE_AGX_PLATFORM_EXTERNAL_RENDER_IO ExternalRender;
  APPLE_AGX_BACKEND_BOOL DeferFirmwareMappings;
} APPLE_AGX_PLATFORM_PROVIDER_CONFIG;

typedef enum _APPLE_AGX_PLATFORM_POLL_GUARD {
  AppleAgxPlatformPollGuardOk = 0u,
  AppleAgxPlatformPollGuardInvalid = 1u,
  AppleAgxPlatformPollGuardDrainEvents = 2u,
  AppleAgxPlatformPollGuardClock = 3u,
  AppleAgxPlatformPollGuardTimeoutCheck = 4u,
  AppleAgxPlatformPollGuardTimeoutApply = 5u,
} APPLE_AGX_PLATFORM_POLL_GUARD;

typedef struct _APPLE_AGX_PLATFORM_PROVIDER {
  APPLE_AGX_PLATFORM_CHANNEL_BINDINGS Channels;
  APPLE_AGX_PLATFORM_TRANSPORT_IO Transport;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO CallerQueueRuntimeIo;
  APPLE_AGX_G13_QUEUE_PROVIDER QueueProvider;
  APPLE_AGX_SUBMISSION_COORDINATOR SubmissionCoordinator;
  APPLE_AGX_PLATFORM_COMPOSER Composer;
  APPLE_AGX_EVENT_ALLOCATOR EventAllocator;
  APPLE_AGX_EVENT_PAIR EventPair;
  APPLE_AGX_BACKEND_RUNTIME *Runtime;
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *RenderSharedMemory;
  APPLE_AGX_BACKEND_IO CallerRenderIo;
  APPLE_AGX_BACKEND_IO QueueBackendIo;
  APPLE_AGX_PLATFORM_EXTERNAL_RENDER_IO ExternalRender;
  /* Durable event handoff across ring ACK and Windows-side application. */
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH PendingEventBatch;
  APPLE_AGX_BACKEND_U32 PendingEventNextRead;
  APPLE_AGX_BACKEND_BOOL EventBatchPending;
  APPLE_AGX_BACKEND_BOOL EventBatchAcknowledged;
  APPLE_AGX_BACKEND_BOOL BindingReady;
  APPLE_AGX_BACKEND_BOOL ExternalRenderReady;
  APPLE_AGX_BACKEND_BOOL Initialized;
  APPLE_AGX_BACKEND_U32 LastPollGuard;
} APPLE_AGX_PLATFORM_PROVIDER;

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderBindChannels(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *ChannelMemory,
    APPLE_AGX_PLATFORM_CHANNEL_BINDINGS *Bindings);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderPublishRun(
    const APPLE_AGX_PLATFORM_CHANNEL_BINDINGS *Bindings,
    const APPLE_AGX_PLATFORM_TRANSPORT_IO *Io,
    APPLE_AGX_G13_QUEUE_TYPE QueueType,
    const unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderInitialize(
    APPLE_AGX_PLATFORM_PROVIDER *Provider,
    const APPLE_AGX_PLATFORM_PROVIDER_CONFIG *Config,
    APPLE_AGX_BACKEND_IO *Io);

/*
 * Consume at most MaxMessages from ChannelInfo[13] (state 26/ring 27).
 * The firmware-visible read pointer is published only after the composer has
 * accepted the corresponding 0x38-byte message.
 */
APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderDrainEvents(
    APPLE_AGX_PLATFORM_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U32 MaxMessages,
    APPLE_AGX_BACKEND_U32 *DrainedMessages,
    APPLE_AGX_BACKEND_U32 *CompletedFence);

/*
 * One bounded PASSIVE polling pass: drain at most MaxMessages, then check the
 * real queue deadline.  Timeout completion is emitted only when the platform
 * quiesce contract has succeeded inside the queue provider.
 */
APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderPoll(
    APPLE_AGX_PLATFORM_PROVIDER *Provider,
    APPLE_AGX_BACKEND_U32 MaxMessages,
    APPLE_AGX_BACKEND_U32 *DrainedMessages,
    APPLE_AGX_BACKEND_U32 *CompletedFence);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformProviderDestroy(
    APPLE_AGX_PLATFORM_PROVIDER *Provider);

#endif /* APPLE_AGX_PLATFORM_PROVIDER_H */
