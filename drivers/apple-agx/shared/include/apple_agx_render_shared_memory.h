#ifndef APPLE_AGX_RENDER_SHARED_MEMORY_H
#define APPLE_AGX_RENDER_SHARED_MEMORY_H

#include "apple_agx_memory.h"
#include "apple_agx_g13_queue_runtime.h"
#include "apple_agx_render_template.h"

#define APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT 36u

typedef enum _APPLE_AGX_RENDER_SHARED_MEMORY_RESULT {
  AppleAgxRenderSharedMemoryResultOk = 0,
  AppleAgxRenderSharedMemoryResultInvalidArgument,
  AppleAgxRenderSharedMemoryResultAllocationFailed,
  AppleAgxRenderSharedMemoryResultReleaseFailed,
} APPLE_AGX_RENDER_SHARED_MEMORY_RESULT;

typedef struct _APPLE_AGX_RENDER_SHARED_MEMORY_OWNER {
  const APPLE_AGX_MEMORY_IO *MemoryIo;
  APPLE_AGX_MEMORY_OBJECT
      Objects[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  /* Page-aligned broker mapping bases remain separate from the exact
   * firmware-visible EXP208 object identities inside those pages. */
  APPLE_AGX_U64 VirtualAddresses[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U64 ObjectAddresses[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U32 DataOffsets[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U32 ObjectCount;
  APPLE_AGX_BOOL Initialized;
  APPLE_AGX_BOOL Built;
  APPLE_AGX_RENDER_SHARED_MEMORY_RESULT LastResult;
} APPLE_AGX_RENDER_SHARED_MEMORY_OWNER;

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT AppleAgxRenderSharedMemoryBuild(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    APPLE_AGX_U64 FirstVirtualAddress);

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBindRelocationObjects(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity);

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildQueueConfig(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 TimeoutTicks,
    APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config);

/* CPU-only provider configuration before firmware-private prefix import.
 * This does not assert GPU residency or authorize queue creation. */
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryPrepareQueueConfig(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 TimeoutTicks,
    APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config);

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT AppleAgxRenderSharedMemoryDestroy(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner);

#endif /* APPLE_AGX_RENDER_SHARED_MEMORY_H */
