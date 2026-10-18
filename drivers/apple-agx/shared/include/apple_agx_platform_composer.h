#ifndef APPLE_AGX_PLATFORM_COMPOSER_H
#define APPLE_AGX_PLATFORM_COMPOSER_H

#include "apple_agx_backend_runtime.h"
#include "apple_agx_g13_queue_provider.h"

/*
 * APPLE_AGX_BACKEND_IO has one Context, while the independently verified
 * firmware, image/context and queue providers each own a different context.
 * This composer is the sole backend context and forwards every callback to
 * the exact component table that supplied it.
 */
typedef struct _APPLE_AGX_PLATFORM_COMPOSER_CONFIG {
  const APPLE_AGX_FIRMWARE_IO *Firmware;
  const APPLE_AGX_BACKEND_IO *Render;
  const APPLE_AGX_BACKEND_IO *Queues;
  APPLE_AGX_G13_QUEUE_PROVIDER *QueueProvider;
  APPLE_AGX_BACKEND_RUNTIME *Runtime;
} APPLE_AGX_PLATFORM_COMPOSER_CONFIG;

typedef struct _APPLE_AGX_PLATFORM_COMPOSER {
  APPLE_AGX_FIRMWARE_IO Firmware;
  APPLE_AGX_BACKEND_IO Render;
  APPLE_AGX_BACKEND_IO Queues;
  APPLE_AGX_G13_QUEUE_PROVIDER *QueueProvider;
  APPLE_AGX_BACKEND_RUNTIME *Runtime;
  APPLE_AGX_BACKEND_BOOL Initialized;
} APPLE_AGX_PLATFORM_COMPOSER;

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerInitialize(
    APPLE_AGX_PLATFORM_COMPOSER *Composer,
    const APPLE_AGX_PLATFORM_COMPOSER_CONFIG *Config,
    APPLE_AGX_BACKEND_IO *Io);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerIngestEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_BACKEND_U32 *CompletedFence);

/* Decode and advance the AGX queue-provider state without publishing a
 * completion into the Windows-facing backend runtime.  The event-ring owner
 * must acknowledge the consumed message before calling ApplyEvent. */
APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerPrepareEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformComposerApplyEvent(
    APPLE_AGX_PLATFORM_COMPOSER *Composer,
    APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *Batch,
    APPLE_AGX_BACKEND_U32 *CompletedFence);

#endif /* APPLE_AGX_PLATFORM_COMPOSER_H */
