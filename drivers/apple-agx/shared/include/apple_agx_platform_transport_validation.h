#ifndef APPLE_AGX_PLATFORM_TRANSPORT_VALIDATION_H
#define APPLE_AGX_PLATFORM_TRANSPORT_VALIDATION_H

#include "apple_agx_channel_memory.h"
#include "apple_agx_device_control.h"
#include "apple_agx_render_shared_memory.h"

typedef struct _APPLE_AGX_PLATFORM_TRANSPORT_MEMORY {
  const APPLE_AGX_CHANNEL_MEMORY_OWNER *Channels;
  const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Render;
} APPLE_AGX_PLATFORM_TRANSPORT_MEMORY;

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidateFlushForDevice(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory, const void *Address,
    APPLE_AGX_BACKEND_U32 Bytes);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidateFlushForCpu(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory, const void *Address,
    APPLE_AGX_BACKEND_U32 Bytes);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidateReadU32(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory,
    const volatile APPLE_AGX_BACKEND_U32 *Address);

APPLE_AGX_BACKEND_BOOL AppleAgxPlatformTransportValidatePublishU32(
    const APPLE_AGX_PLATFORM_TRANSPORT_MEMORY *Memory,
    volatile APPLE_AGX_BACKEND_U32 *Address);

#endif /* APPLE_AGX_PLATFORM_TRANSPORT_VALIDATION_H */
