#ifndef APPLE_AGX_RENDER_SHARED_MEMORY_H
#define APPLE_AGX_RENDER_SHARED_MEMORY_H

#include "apple_agx_memory.h"
#include "apple_agx_g13_queue_runtime.h"
#include "apple_agx_render_template.h"
#include "apple_agx_retained_root_abi.h"
#include "apple_agx_g13_compute_work.h"
#include "apple_agx_render_manager.h"

#define APPLE_AGX_RENDER_SHARED_MEMORY_TEMPLATE_OBJECT_COUNT 36u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_QUEUE_INFO 36u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_RING 37u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_POINTERS 38u
#define APPLE_AGX_RENDER_SHARED_MEMORY_COMPUTE_SIDECAR 39u
#define APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT 40u
/* The Queue's notifier (m1n1 EventControl, Asahi event::Notifier) and its
 * threshold (m1n1 event_count, Asahi Threshold). One per Queue: the TA, 3D
 * and compute work queues share it (Asahi queue/mod.rs). */
#define APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER 11u
#define APPLE_AGX_RENDER_SHARED_MEMORY_NOTIFIER_THRESHOLD 12u
#define APPLE_AGX_G13_NOTIFIER_BYTES 0xb0u
#define APPLE_AGX_G13_NOTIFIER_GENERATION_OFFSET 0x08u
#define APPLE_AGX_G13_NOTIFIER_BUFFER_OFFSET 0xa8u
#define APPLE_AGX_RENDER_STATS_TA_FIELD_OFFSET 4u
#define APPLE_AGX_RENDER_STATS_3D_FIELD_OFFSET 8u
#define APPLE_AGX_COMPUTE_PREEMPT_OFFSET 0x0000u
#define APPLE_AGX_COMPUTE_WORK_OFFSET 0x8000u
#define APPLE_AGX_COMPUTE_MICROSEQUENCE_OFFSET 0x8400u
#define APPLE_AGX_COMPUTE_STATISTICS_OFFSET 0x8600u
#define APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET 0x8780u
#define APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET 0xc000u
/* D3D-G1b: the TA queue's Barrier on the compute stamp (a render waiting on
 * its submission's compute), after the work-queue owned words. */
#define APPLE_AGX_COMPUTE_TA_BARRIER_OFFSET 0x8800u
/* GPU timestamps: a job that requests them runs the longer microsequence
 * (Timestamp ops, APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_TIMESTAMP_BYTES) and
 * the firmware writes the job/requested start and end into the block; both
 * are job words, rebuilt with the job. */
#define APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET 0x9000u
#define APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET 0x9400u
/* The G13 firmware Barrier work item: the captured WorkCommandBarrier (render
 * template object 14) and Asahi fw/workqueue.rs raw::Barrier without the
 * G14X internal_barrier_type: tag 4 at 0x00, wait stamp pointer (u64) at
 * 0x04, wait value 0x0c, wait slot 0x10, the waiting queue's own stamp 0x14,
 * uuid 0x18, external barrier 0x1c. */
#define APPLE_AGX_G13_BARRIER_BYTES 0x20u
#define APPLE_AGX_G13_BARRIER_TAG 4u
/* rc98 (depth-2 step 2, U7): two job-word sets in the compute sidecar. A
 * job that uses the sidecar (envelope compute, v5 prelude) builds its
 * RunCompute work, microsequences, TA Barrier and timestamp block in set A
 * (the offsets above) or in set B, the same layout APPLE_AGX_COMPUTE_JOB_
 * WORD_SET_B_DELTA higher, inside the free range 0x9420-0xc000. The
 * statistics (0x8600), the firmware stamp (0x8780) and gpu_buf (0xc000)
 * belong to the work queue and stay shared; so does the sidecar's own
 * preemption buffer (0x0000), which is why set B needs a job-owned one.
 * Set A is the only set of the default path (JobWordSet 0). */
#define APPLE_AGX_COMPUTE_JOB_WORD_SET_A 0u
#define APPLE_AGX_COMPUTE_JOB_WORD_SET_B 1u
#define APPLE_AGX_COMPUTE_JOB_WORD_SET_COUNT 2u
#define APPLE_AGX_COMPUTE_JOB_WORD_SET_B_DELTA 0x2000u
#define APPLE_AGX_COMPUTE_JOB_WORD_SET_FREE_START 0x9420u
/* rc98: the depth-2 leading Barrier of a compute queue (N+1's prelude or
 * compute job waiting on N's last firmware stamp), one per set, after the
 * set's TA Barrier. The TA leading Barrier of a render without a prelude
 * uses the set's TA Barrier slot (such a render has no prelude Barrier). */
#define APPLE_AGX_COMPUTE_QUEUE_BARRIER_OFFSET 0x8820u
#define APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(Set, OffsetA) \
  ((OffsetA) + ((Set) ? APPLE_AGX_COMPUTE_JOB_WORD_SET_B_DELTA : 0u))
/* Asahi event.rs: queue event values advance in 0x100 units. */
#define APPLE_AGX_COMPUTE_STAMP_INCREMENT 0x100u

typedef struct _APPLE_AGX_RENDER_COMPUTE_INPUT {
  APPLE_AGX_U64 CdmStreamBase;
  APPLE_AGX_U32 CdmStreamBytes;
  APPLE_AGX_U64 Counter;
  APPLE_AGX_U64 UscExecutionBase;
  /* StampValue remains the caller's identity for deduplication/sidecar
   * ownership. Use the output StampValue for firmware waits/completion. */
  APPLE_AGX_U32 VmSlot,EventNumber,StampValue,EventSequence,ClientSequence;
  /* G2: the CDM context-store buffer in the job's own VM, as Asahi
   * allocates it from the user VM (queue/compute.rs ualloc); at least
   * APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES. Zero keeps the kernel sidecar
   * area of the render prelude. */
  APPLE_AGX_U64 PreemptionGpuAddress;
  /* EXP1339: the helper program (compute scratch), as Asahi
   * queue/compute.rs passes it; all zero without one. */
  APPLE_AGX_U32 HelperProgram,HelperConfig;
  APPLE_AGX_U64 HelperArgument;
  /* Nonzero: the job records GPU timestamps (Asahi drm_asahi_cmd_compute
   * ts); Output->TimestampCpuAddress names the block. */
  APPLE_AGX_U32 Timestamps;
  /* 0/0, or the batch's sampler heap (drm_asahi_cmd_compute sampler_heap,
   * sampler_count): APPLE_AGX_G13_COMPUTE_WORK_INPUT SamplerHeap/Count. */
  APPLE_AGX_U64 SamplerHeap;
  APPLE_AGX_U32 SamplerCount;
  /* rc98: APPLE_AGX_COMPUTE_JOB_WORD_SET_*; zero (set A) is the default
   * path. Set B requires PreemptionGpuAddress (the sidecar's own
   * preemption buffer is one). */
  APPLE_AGX_U32 JobWordSet;
} APPLE_AGX_RENDER_COMPUTE_INPUT;

typedef struct _APPLE_AGX_RENDER_COMPUTE_OUTPUT {
  APPLE_AGX_U64 WorkGpuAddress,SidecarGpuAddress;
  const void *SidecarCpuAddress;
  APPLE_AGX_U32 SidecarBytes;
  APPLE_AGX_U32 NotifierThreshold;
  /* The firmware stamp the compute work writes (a Barrier waits on it). */
  APPLE_AGX_U64 FirmwareStampGpuAddress;
  /* The timestamp block (job start, job end, requested start, requested
   * end; APPLE_AGX_G13_TIMESTAMP_BLOCK_BYTES), or NULL. */
  const void *TimestampCpuAddress;
  /* rc98: the job-word set the build used. */
  APPLE_AGX_U32 JobWordSet;
  /* Firmware endpoint; may differ from the input's original fence identity. */
  APPLE_AGX_U32 StampValue;
} APPLE_AGX_RENDER_COMPUTE_OUTPUT;

typedef struct _APPLE_AGX_RENDER_BARRIER_INPUT {
  APPLE_AGX_U64 WaitStampGpuAddress;
  APPLE_AGX_U32 WaitValue, WaitSlot, StampSelf;
  /* rc98: the waiting job's job-word set (0 = set A, the default). */
  APPLE_AGX_U32 JobWordSet;
} APPLE_AGX_RENDER_BARRIER_INPUT;

/* rc98: the queue whose firmware stamp a Barrier waits on (Asahi
 * event.fw_stamp_pointer of the queue's event): TA = template object 9 and
 * 3D = template object 10 (object 14, the captured 3D-on-TA Barrier, and
 * the 3D work command at 0x234 point there; never slotted), compute = the
 * sidecar's work-queue firmware stamp (0x8780). */
#define APPLE_AGX_RENDER_STAMP_QUEUE_TA 0u
#define APPLE_AGX_RENDER_STAMP_QUEUE_3D 1u
#define APPLE_AGX_RENDER_STAMP_QUEUE_COMPUTE 2u
#define APPLE_AGX_RENDER_TA_FIRMWARE_STAMP_OBJECT 9u
#define APPLE_AGX_RENDER_3D_FIRMWARE_STAMP_OBJECT 10u
/* rc98: the queue a depth-2 leading Barrier is published on. */
#define APPLE_AGX_RENDER_BARRIER_QUEUE_TA 0u
#define APPLE_AGX_RENDER_BARRIER_QUEUE_COMPUTE 1u

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

/* rc83 (EXP2012): the per-job build spent 266 us and the completion's
 * buffer-manager save 77 us in uncached reads: 1527 serial 8-byte loads of
 * the 12 KiB per-submission template objects from the write-combined
 * (Normal non-cacheable) arena, and 380 single-byte loads of the manager
 * objects from MmNonCached (Device) memory, while a compute build of ~4400
 * Device byte stores and 4 loads took 4-17 us. The largest template object
 * (GPUWorkQueue.gpu_buf) is 11288 bytes. */
#define APPLE_AGX_RENDER_SHARED_MEMORY_BOUNCE_BYTES 0x3000u

#if defined(APPLE_AGX_RENDER_SHARED_READ_PROBE)
/* Host model only: uncached load transactions the builder issued. */
typedef struct _APPLE_AGX_RENDER_SHARED_READ_PROBE_COUNTS {
  unsigned long SharedLoads; /* element-wise loads of firmware-shared memory */
  unsigned long ArenaLoads;  /* element-wise loads of the template arena */
  unsigned long ArenaBulk;   /* bulk (memcpy) reads of one arena object */
  unsigned long ArenaBulkBytes;
} APPLE_AGX_RENDER_SHARED_READ_PROBE_COUNTS;
extern APPLE_AGX_RENDER_SHARED_READ_PROBE_COUNTS AppleAgxRenderSharedReadProbe;
#endif

/* rc124: firmware buffer-manager slots the owner can keep bound. */
#define APPLE_AGX_RENDER_MANAGER_SLOTS 4u

#define APPLE_AGX_NOTIFIER_FLIGHT_COUNT 2u
typedef struct _APPLE_AGX_NOTIFIER_FLIGHT {
  APPLE_AGX_U32 Fence, EventCount, PreviousThreshold;
  APPLE_AGX_U32 RenderCount, ComputeCount, ComputeStamp, WordSet, WordStamp;
  APPLE_AGX_U32 ComputeQueueStamp;
  APPLE_AGX_U32 RenderSlot, SidecarSet;
  APPLE_AGX_BOOL HasRender, HasSidecar, Published, Visible, EpochReset;
} APPLE_AGX_NOTIFIER_FLIGHT;

typedef struct _APPLE_AGX_RENDER_SHARED_MEMORY_OWNER {
  /* q2: serialized build/commit, two exact-fence lifetime records. Render
   * parity and sidecar selection are separately retained in each record. */
  APPLE_AGX_BOOL NotifierFlight;
  APPLE_AGX_U32 BuildingFlight; /* index + 1, zero when no build */
  APPLE_AGX_NOTIFIER_FLIGHT Flights[APPLE_AGX_NOTIFIER_FLIGHT_COUNT];
  APPLE_AGX_BOOL (*NotifierFlush)(void *Context, const void *Address, APPLE_AGX_U32 Bytes);
  void *NotifierFlushContext;
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
  APPLE_AGX_RENDER_MANAGER_KEY ManagerKey;
  APPLE_AGX_U32 ManagerFence;
  /* Q6: bounded exact users of the same resident slot. ManagerFence remains
   * the oldest-user busy summary consumed by legacy lifetime checks. */
  APPLE_AGX_BOOL ManagerDepth2;
  APPLE_AGX_U32 ManagerSecondFence;
  APPLE_AGX_BOOL ManagerPoisoned;
  APPLE_AGX_U32 ManagerTerminalFence[2];
  /* The notifier threshold counts every notifying command since the
   * queue was initialised (Asahi notification_count = 2 * render +
   * compute): the render part is the last render job's 2 * sequence (its
   * event_count from the template arena), the compute part is counted
   * here; event_count carries their sum. */
  APPLE_AGX_U32 NotifierRenderCount;
  APPLE_AGX_U32 NotifierComputeCount;
  APPLE_AGX_U32 NotifierComputeStamp;
  /* Q23: opt-in queue event units, independent of Windows fence values.
   * NotifierComputeStamp and JobWordStamp retain their raw fence identity. */
  APPLE_AGX_BOOL ComputeQueueStamps;
  APPLE_AGX_U32 ComputeQueueStamp;
  /* rc83 receipt: optional progress callback of a render build, Point one
   * of APPLE_AGX_RENDER_BUILD_MARK_*; NULL (zeroed owner) = none. It only
   * timestamps and never changes the build. */
  void (*BuildMark)(void *Context, APPLE_AGX_U32 Point);
  void *BuildMarkContext;
  /* G2 receipt (device value Wom1TaStartTimestamps): point the TA
   * microsequence's Timestamp ops at their start/end pointer fields
   * (Asahi queue/render.rs update_ts); default off keeps 0xdeadbeef. */
  APPLE_AGX_BOOL TaStartTimestamps;
  /* The last managed G4 build restored the saved buffer-manager state. */
  APPLE_AGX_BOOL LastBuildRestored;
  /* rc83 A/B: 1 = the pre-rc83 element-wise reads of the template arena
   * and of firmware-shared memory (device value Wom1BuildDirectReads). */
  APPLE_AGX_BOOL DirectReads;
  /* rc83: event_count as the last bind copied it (valid when set). */
  APPLE_AGX_BOOL StagedEventCountValid;
  APPLE_AGX_U32 StagedEventCount;
  /* rc83: cached bounce buffer of one template object read from the
   * write-combined arena in bulk (8 bytes of alignment slack). */
  APPLE_AGX_U64 ArenaBounce[(APPLE_AGX_RENDER_SHARED_MEMORY_BOUNCE_BYTES + 8u) / 8u];
  /* rc98: the job-word set and stamp of the last successful compute build
   * (valid when JobWordStamp != 0); cleared with the compute queue. */
  APPLE_AGX_U32 JobWordSet;
  APPLE_AGX_U32 JobWordStamp;
  /* rc102 (EXP2047): why the last managed/active render build refused,
   * APPLE_AGX_RENDER_BUILD_REFUSAL_* (0 = it did not refuse). Diagnostic
   * only (Wom1JobWindowKickFail). */
  APPLE_AGX_U32 LastBuildRefusal;
  /* rc124 (Asahi buffer.rs BufferManager, NUM_BUFFERS 127): firmware
   * buffer-manager slots. Slot s keeps its own buffer-manager objects
   * {1 Info, 20 BlockControl, 21 Counter, 22 Stats} below the originals in
   * their pages, and its number in WorkCommandInitBM/TA and StartTA/3D. A
   * key bound to a slot stays firmware-owned there: its next job needs no
   * InitBM and no restore. ManagerSlotCount 0 or 1 is the single slot 0
   * (pre-rc124); the Windows build sets it from Wom1ManagerSlots. */
  APPLE_AGX_U32 ManagerSlotCount;
  APPLE_AGX_U32 ManagerSlot;      /* slot of the last managed build */
  APPLE_AGX_U32 ManagerSlotClock; /* LRU clock */
  APPLE_AGX_BOOL ManagerSlotLive; /* the last build reused a bound slot */
  APPLE_AGX_RENDER_MANAGER_KEY SlotKey[APPLE_AGX_RENDER_MANAGER_SLOTS];
  APPLE_AGX_BOOL SlotBound[APPLE_AGX_RENDER_MANAGER_SLOTS];
  APPLE_AGX_U32 SlotUse[APPLE_AGX_RENDER_MANAGER_SLOTS];
  /* q1: save-on-eviction; Q6 retains all exact same-slot users. No process
   * pointers retained in a slot. Evict
   * synchronizes Objects for CPU reads and stores a snapshot by Key under
   * the process registry lock; FALSE leaves the slot dirty and bound. */
  APPLE_AGX_BOOL ManagerSaveOnEvict;
  APPLE_AGX_U32 ManagerBuildCounter; /* rollback of a proven unpublished build */
  APPLE_AGX_U32 SlotDirtyFence[APPLE_AGX_RENDER_MANAGER_SLOTS];
  APPLE_AGX_BOOL (*ManagerEvict)(void *Context,
      const APPLE_AGX_RENDER_MANAGER_KEY *Key, APPLE_AGX_U32 Fence,
      const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects);
  void *ManagerEvictContext;
} APPLE_AGX_RENDER_SHARED_MEMORY_OWNER;

/* Cancel is valid only before a queue pointer was published. Retire requires
 * joined terminal completion or a synchronous proof that firmware stopped. */
APPLE_AGX_BOOL AppleAgxRenderFlightBegin(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxRenderFlightPublish(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxRenderFlightCancel(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *, APPLE_AGX_U32 Fence);
void AppleAgxRenderFlightVisible(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *, APPLE_AGX_U32 Fence);
/* Only after a synchronous firmware/ASC stop, including failed submissions. */
void AppleAgxRenderFlightStopped(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *);
void AppleAgxRenderFlightRetire(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *, APPLE_AGX_U32 Fence);

#define APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_FENCE    1u  /* Owner->ManagerFence */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_PENDING  2u  /* Manager->PendingFence */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_KEY      3u  /* key or manager objects */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_BIND     4u  /* rebind without InitBM */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_BACKING  5u  /* backing vs objects 41-43 */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_RELOCATE 6u  /* Info/Scene relocation */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_MANAGER_SNAPSHOT 7u /* eviction/snapshot not safe */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_JOB_ARGUMENTS    10u /* build_active_job inputs */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_STAGED_JOB       11u /* staged job fields */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_RUNTIME_BINDINGS 12u
#define APPLE_AGX_RENDER_BUILD_REFUSAL_BIND             13u /* bind_relocation_objects */
#define APPLE_AGX_RENDER_BUILD_REFUSAL_INITBM           14u
#define APPLE_AGX_RENDER_BUILD_REFUSAL_RELOCATIONS      15u
#define APPLE_AGX_RENDER_BUILD_REFUSAL_STATS            16u
#define APPLE_AGX_RENDER_BUILD_REFUSAL_THRESHOLD        17u

/* rc83: points of a render build reported through Owner->BuildMark. */
#define APPLE_AGX_RENDER_BUILD_MARK_COPIED 1u    /* template objects copied */
#define APPLE_AGX_RENDER_BUILD_MARK_RELOCATED 2u /* relocations, stats, threshold */
#define APPLE_AGX_RENDER_BUILD_MARK_MANAGED 3u   /* manager state restored */

/* Caller serializes against submission and has proved joined completion.
 * Drain fails with any manager job pending. Used before queue reinit/destroy. */
/* Terminal dropped work is not a restorable snapshot; cancel is ONLY for
 * a build that was never published to firmware. Both require the exact fence. */
APPLE_AGX_BOOL AppleAgxRenderManagerAbort(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *State, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxRenderManagerCancelBuild(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *State, APPLE_AGX_U32 Fence,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects);
APPLE_AGX_BOOL AppleAgxRenderManagerDrain(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner);
/* Objects have been synchronized for CPU access; no pending GPU writer. */
APPLE_AGX_BOOL AppleAgxRenderManagerSnapshot(APPLE_AGX_RENDER_MANAGER_STATE *State,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key, APPLE_AGX_U32 Fence,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects, APPLE_AGX_BOOL DirectReads);
/* Under the process registry lock; an obsolete key is safely discarded. */
APPLE_AGX_BOOL AppleAgxRenderManagerAcceptSnapshot(APPLE_AGX_RENDER_MANAGER_STATE *State,
    const APPLE_AGX_RENDER_MANAGER_STATE *Snapshot);

/* True only for a retained same-key live manager with room for one user. */
APPLE_AGX_BOOL AppleAgxRenderManagerCanShare(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_MANAGER_STATE *State,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key);

APPLE_AGX_BOOL AppleAgxRenderManagerNeedsBind(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key);
/* Caller has observed joined completion and synchronized these shared objects
 * for CPU access. A failure cannot be treated as successful retirement. */
APPLE_AGX_BOOL AppleAgxRenderManagerSave(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *State, APPLE_AGX_U32 Fence,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects);
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildManagedG4Job(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_RENDER_MANAGER_STATE *Manager,
    const APPLE_AGX_RENDER_MANAGER_KEY *Key, APPLE_AGX_U32 Fence,
    APPLE_AGX_BOOL InitializeQueues,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob);

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
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
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
/* D3D-G1b: builds the TA queue's Barrier on a compute stamp in the compute
 * sidecar (published with the compute work) and returns its firmware VA. The
 * Barrier stamps the TA queue with StampSelf, the TA job's own stamp value,
 * as Asahi's render barrier stamps ev_frag.value.next(). */
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildComputeBarrier(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_BARRIER_INPUT *Input,
    APPLE_AGX_U64 *BarrierGpuAddress);

/* rc98: the G13 Barrier work item bytes (Asahi fw/workqueue.rs raw::Barrier,
 * queue/mod.rs:787-820 explicit vdm/cdm barrier), the encoder of every
 * Barrier the KMD builds. FALSE for a null/unaligned stamp, a zero value or
 * self stamp, or a slot >= 0x80. */
APPLE_AGX_BOOL AppleAgxG13BarrierEncode(
    const APPLE_AGX_RENDER_BARRIER_INPUT *Input,
    unsigned char Barrier[APPLE_AGX_G13_BARRIER_BYTES]);
/* rc98: a queue's firmware stamp VA (APPLE_AGX_RENDER_STAMP_QUEUE_*), the
 * wait_stamp of a Barrier on that queue's last job; 0 when unavailable. */
APPLE_AGX_U64 AppleAgxRenderSharedMemoryFirmwareStampAddress(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner, APPLE_AGX_U32 Queue);
/* rc98: the job-word set a compute build for StampValue uses: set A when
 * SecondSet is FALSE (the default path); otherwise the set of the last build
 * for a rebuild of the same stamp, else the other set than the last build,
 * so two consecutive sidecar jobs (the most a depth-2 window holds) never
 * share their words. A new compute queue starts with set A. */
APPLE_AGX_U32 AppleAgxRenderSharedMemoryNextJobWordSet(
    const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    APPLE_AGX_U32 StampValue, APPLE_AGX_BOOL SecondSet);
/* rc98 (COMPUTE_BARRIER / TA_ON_3D): the depth-2 leading Barrier of N+1's
 * first queue on N's last firmware stamp, built in N+1's job-word set:
 * Queue TA uses the set's TA Barrier slot (a render without a prelude),
 * Queue COMPUTE the set's compute-queue Barrier slot (a compute job or a
 * v5 prelude). StampSelf is N+1's own stamp on that queue. Not published by
 * any path yet (no second kick before the depth-2 window, step 3/4). */
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildLeadingBarrier(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const APPLE_AGX_RENDER_BARRIER_INPUT *Input, APPLE_AGX_U32 Queue,
    APPLE_AGX_U64 *BarrierGpuAddress);

/* Refresh the firmware-visible context-0 objects from the dynamically patched
 * template arena, then build the job from that active relocation graph. */
APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveJob(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
    const void *TemplateArena, APPLE_AGX_U32 TemplateArenaBytes,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *SourceObjects,
    APPLE_AGX_U32 SourceObjectCount, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_BOOL IncludeInitBm,
    const APPLE_AGX_RENDER_RUNTIME_BINDINGS *RuntimeBindings,
    const APPLE_AGX_BACKEND_JOB_IMAGE *StagedJob,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_BACKEND_JOB_IMAGE *ActiveJob);

APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildActiveG4Job(
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner,
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
