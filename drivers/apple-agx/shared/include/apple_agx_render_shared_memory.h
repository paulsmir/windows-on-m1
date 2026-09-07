#ifndef APPLE_AGX_RENDER_SHARED_MEMORY_H
#define APPLE_AGX_RENDER_SHARED_MEMORY_H

#include "apple_agx_memory.h"
#include "apple_agx_g13_queue_runtime.h"
#include "apple_agx_render_template.h"
#include "apple_agx_retained_root_abi.h"

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
  APPLE_AGX_U64 VirtualAddresses[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U64 ObjectOffsets[APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT];
  APPLE_AGX_U32 ObjectCount;
  APPLE_AGX_BOOL Initialized;
  APPLE_AGX_BOOL Built;
  APPLE_AGX_BOOL ClassArenasApplied;
  APPLE_AGX_RENDER_SHARED_MEMORY_RESULT LastResult;
} APPLE_AGX_RENDER_SHARED_MEMORY_OWNER;

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT AppleAgxRenderSharedMemoryBuild(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    APPLE_AGX_U64 FirstVirtualAddress);

APPLE_AGX_RENDER_SHARED_MEMORY_RESULT
AppleAgxRenderSharedMemoryApplyClassArenas(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 SharedVa, APPLE_AGX_U64 SharedBytes,
    APPLE_AGX_U64 TimestampVa, APPLE_AGX_U64 TimestampBytes);
APPLE_AGX_RENDER_SHARED_MEMORY_RESULT
AppleAgxRenderSharedMemoryApplyCommandArena(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 CommandVa, APPLE_AGX_U64 CommandBytes);

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBindRelocationObjects(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity);

/* Refresh the firmware-visible context-0 objects from the dynamically patched
 * template arena, then build the job from that active relocation graph. */
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveJob(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob);

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
