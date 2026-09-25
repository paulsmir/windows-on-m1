#ifndef APPLE_AGX_RENDER_SHARED_MEMORY_H
#define APPLE_AGX_RENDER_SHARED_MEMORY_H

#include "apple_agx_memory.h"
#include "apple_agx_g13_queue_runtime.h"
#include "apple_agx_render_template.h"
#include "apple_agx_retained_root_abi.h"
#include "apple_agx_g13_compute_work.h"

#define APPLE_AGX_RENDER_SHARED_MEMORY_TEMPLATE_OBJECT_COUNT 36u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO 36u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_RING 37u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS 38u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR 39u
#define APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT 40u
#define APPLE_AGX_RENDER_STATS_TA_FIELD_OFFSET 4u
#define APPLE_AGX_RENDER_STATS_3D_FIELD_OFFSET 8u
#define APPLE_AGX_COMPUTE_PREEMPT_OFFSET 0x0000u
#define APPLE_AGX_COMPUTE_WORK_OFFSET 0x8000u
#define APPLE_AGX_COMPUTE_MICROSEQUENCE_OFFSET 0x8400u
#define APPLE_AGX_COMPUTE_STATISTICS_OFFSET 0x8600u
#define APPLE_AGX_COMPUTE_NOTIFIER_OFFSET 0x8700u
#define APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET 0x8780u
#define APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET 0xc000u

typedef struct _APPLE_AGX_RENDER_COMPUTE_INPUT {
  APPLE_AGX_U64 CdmStreamBase;
  APPLE_AGX_U32 CdmStreamBytes;
  APPLE_AGX_U64 Counter;
  APPLE_AGX_U64 UscExecutionBase;
  APPLE_AGX_U32 VmSlot,EventNumber,StampValue,EventSequence,ClientSequence;
} APPLE_AGX_RENDER_COMPUTE_INPUT;

typedef struct _APPLE_AGX_RENDER_COMPUTE_OUTPUT {
  APPLE_AGX_U64 WorkGpuAddress,SidecarGpuAddress;
  const void *SidecarCpuAddress;
  APPLE_AGX_U32 SidecarBytes;
} APPLE_AGX_RENDER_COMPUTE_OUTPUT;

typedef struct _APPLE_AGX_RENDER_RUNTIME_BINDINGS {
  APPLE_AGX_U64 StatsTaOwnerGpuAddress;
  APPLE_AGX_U64 StatsTaOwnerBytes;
  APPLE_AGX_U64 Stats3dOwnerGpuAddress;
  APPLE_AGX_U64 Stats3dOwnerBytes;
} APPLE_AGX_RENDER_RUNTIME_BINDINGS;

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
APPLE_AGX_RENDER_SHARED_MEMORY_RESULT
AppleAgxRenderSharedMemoryApplyQueueArenas(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U64 CommandVa, APPLE_AGX_U64 CommandBytes,
    APPLE_AGX_U64 SharedVa, APPLE_AGX_U64 SharedBytes);

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBindRelocationObjects(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena,
    APPLE_AGX_U32 TemplateArenaBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *RelocationObjects,
    APPLE_AGX_U32 RelocationObjectCapacity);
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryInitializeComputeQueue(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner);
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildCompute(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_COMPUTE_INPUT *Input,
    APPLE_AGX_RENDER_COMPUTE_OUTPUT *Output);

/* Refresh the firmware-visible context-0 objects from the dynamically patched
 * template arena, then build the job from that active relocation graph. */
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveJob(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob);

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveG4Job(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
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
