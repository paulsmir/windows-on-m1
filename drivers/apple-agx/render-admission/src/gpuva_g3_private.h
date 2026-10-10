#ifndef ADMISSION_GPUVA_G3_PRIVATE_H
#define ADMISSION_GPUVA_G3_PRIVATE_H

#include "render_admission.h"
#include "apple_agx_g3_private_storage.h"
#include "apple_agx_render_manager.h"
#include "apple_agx_g3_private_abi.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#define ADMISSION_G3_PROCESS_MAGIC 0x47335052u

/* Explicit diagnostic profile; production submission never re-hashes uploads. */
#ifndef ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB
#define ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB 0
#endif

/* R162 diagnostic: USC-window upload hashes may be checked through the
 * published graph in the explicit diagnostic profile. Evidence only. */
#define ADMISSION_G3_UPLOAD_TRACE_COUNT 64u
typedef struct _ADMISSION_G3_UPLOAD_TRACE {
  ULONGLONG ProcessId, Va;
  ULONG Bytes, Hash, GpuHash, Checks;
} ADMISSION_G3_UPLOAD_TRACE;

typedef struct _ADMISSION_G3_STATE {
  ADMISSION_CONTEXT *Adapter;
  FAST_MUTEX Lock;
  /* EXP1138: set when a native job ends (CompleteJob). Escapes that must
   * wait for their process's job wait on it instead of 1 ms timer sleeps. */
  KEVENT JobEvent;
  LIST_ENTRY Processes;
  APPLE_AGX_GPUVA_V5_CLIENT Client;
  /* Cached, physically contiguous broker mailbox page (NULL: MMIO window). */
  PVOID Mailbox;
  APPLE_AGX_GPUVA_G3_REGISTRY Registry;
  APPLE_AGX_G3_PRIVATE_POOL PrivatePool;
  ULONGLONG NextProcessId;
  ULONG ProcessCount;
  struct _ADMISSION_G3_PROCESS *ActiveProcess;
  ULONG ActiveFence;
  ULONG LastCompletedFence;
  ULONG PrivateCompletionFence;
  ULONGLONG UnpublishedGroups[32];
  ADMISSION_G3_UPLOAD_TRACE UploadTrace[ADMISSION_G3_UPLOAD_TRACE_COUNT];
  ULONG UploadTraceNext, UploadVerifyChecks, UploadVerifyMismatch,
      UploadVerifyUnmapped;
  ADMISSION_G3_UPLOAD_TRACE UploadFirstMismatch;
  ADMISSION_G3_LEAF_HISTORY LeafHistory[ADMISSION_G3_LEAF_RING];
  ULONG LeafHistoryNext;
  ADMISSION_G3_ALLOC_TRACK AllocTrack[ADMISSION_G3_ALLOC_TRACK_COUNT];
  /* EXP1087 receipt-only, under Lock: ADMISSION_G3_PRIVATE_STAT_* counters. */
  ULONGLONG PrivateStats[16];
  /* EXP1114 receipt-only, under Lock: last per-process snapshot (QPC). */
  LONGLONG ProcessReceiptQpc;
} ADMISSION_G3_STATE;
enum {
  ADMISSION_G3_PRIVATE_STAT_HIT, ADMISSION_G3_PRIVATE_STAT_MISS,
  ADMISSION_G3_PRIVATE_STAT_TRIM, ADMISSION_G3_PRIVATE_STAT_PRESSURE,
  ADMISSION_G3_PRIVATE_STAT_RELEASED, ADMISSION_G3_PRIVATE_STAT_MAPPED_PAGES,
  ADMISSION_G3_PRIVATE_STAT_UNMAPPED_PAGES, ADMISSION_G3_PRIVATE_STAT_CACHED_RELEASE,
  ADMISSION_G3_PRIVATE_STAT_CACHED_REAP, ADMISSION_G3_PRIVATE_STAT_RELEASE_QUEUED,
  ADMISSION_G3_PRIVATE_STAT_MISS_OTHER_GEOMETRY,
  ADMISSION_G3_PRIVATE_STAT_HEAP_GROW, /* EXP1115 */
  /* 12..15: the last two miss geometries, Width|Height<<16 and
   * UtileWidth|UtileHeight<<8|cached<<16. */
  ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE=12
};

typedef struct _ADMISSION_G3_TABLE_SHADOW {
  struct _ADMISSION_G3_TABLE_SHADOW *Next;
  struct _ADMISSION_G3_TABLE_SHADOW *NextBroker;
  APPLE_AGX_MEMORY_OBJECT Memory;
  ULONGLONG OriginalIpa, BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *LogicalPtes;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *ResidentPtes;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *PendingPtes;
} ADMISSION_G3_TABLE_SHADOW;

typedef struct _ADMISSION_G3_PRIVATE_SCENE {
  struct _ADMISSION_G3_PRIVATE_SCENE *Next;
  ADMISSION_RENDER_CONTEXT *Context;
  APPLE_AGX_G3_PRIVATE_SCENE Storage;
  APPLE_AGX_G4_NATIVE_RENDER Geometry;
  ULONG Fence, ResumeFence, Submitting, Queued, Started, GpuDone, Reported, ReleaseRequested, Quarantined;
  /* EXP1115: TVB blocks backed when this scene's job was built. */
  ULONG HeapBlocks;
  /* EXP1086: released after a reported completion and kept mapped for reuse
   * by an ACQUIRE of the same context and geometry. */
  ULONG Cached;
  ULONGLONG CachedAt; /* EXP1087: LRU order within the process */
} ADMISSION_G3_PRIVATE_SCENE;

/* EXP1086: released scenes a process keeps mapped. EXP1087: a miss trims the
 * least recently cached beyond this many (the miss path runs with no job in
 * flight). EXP1088: four entries thrashed between DWM's geometries (15
 * misses/s); a scene takes at least six 64 KiB units of the 8 MiB process
 * budget, so pool pressure evicts the oldest entries one at a time. */
#define ADMISSION_G3_PRIVATE_SCENE_CACHE 8u

/* EXP1114 receipt-only: 8 header words, then 16 words for each of the 16
 * processes holding the most private units. */
#define ADMISSION_G3_PROCESS_RECEIPT_SLOTS 16u
#define ADMISSION_G3_PROCESS_RECEIPT_WORDS (8u + 16u * ADMISSION_G3_PROCESS_RECEIPT_SLOTS)

typedef struct _ADMISSION_G3_PROCESS {
  LIST_ENTRY Link;
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  APPLE_AGX_MEMORY_IO Io;
  APPLE_AGX_MEMORY_OBJECT BootstrapRoot;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  ADMISSION_G3_TABLE_SHADOW *TableShadowBrokerBuckets[256];
  ULONGLONG BootstrapIpa;
  ULONGLONG LastSetRootIpa;
  ULONG SetRootCount;
  ULONGLONG PrivateVa;
  ULONGLONG PrivateMiddleIpa, PrivateLeafIpa;
  ULONGLONG PrivateCacheClock; /* EXP1087 */
  /* EXP1114 receipt-only: largest AppleAgxG4MinTvbBlocks this process asked
   * private storage for, and that render's Width | Height << 16. */
  ULONG MaxTvbBlocks, MaxShape;
  APPLE_AGX_G3_PRIVATE_EXTENT PrivateTables[2];
  APPLE_AGX_G3_PRIVATE_MANAGER PrivateManager;
  APPLE_AGX_RENDER_MANAGER_STATE FirmwareManager;
  ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;
  ADMISSION_RENDER_CONTEXT *Contexts;
  HANDLE DxgkProcess;
  ULONG Magic, DeviceRefs, ContextRefs, OsProcessId;
  BOOLEAN Poisoned;
  /* VidMm's system paging process. Its VA space exists only for paging
   * operations, which this driver executes on the CPU through the logical
   * shadow, so its leaves are never published to the GPU (no grants). */
  BOOLEAN CpuOnlyMappings;
  /* EXP997 diagnostic: first poison site, (file << 16) | line. */
  ULONG PoisonSite;
  /* EXP1030: broker status (graph LastStatus) when first poisoned. */
  ULONG PoisonBrokerStatus;
} ADMISSION_G3_PROCESS;

#define ADMISSION_G3_POISON(Process, File) do { \
    (Process)->Poisoned = TRUE; \
    if (!(Process)->PoisonSite) { \
      (Process)->PoisonSite = ((ULONG)(File) << 16) | (ULONG)__LINE__; \
      (Process)->PoisonBrokerStatus = (Process)->Graph.LastStatus; \
    } \
  } while (0)

NTSTATUS AdmissionG3PreparePrivateStorage(
    ADMISSION_G3_PROCESS *Process, const APPLE_AGX_G4_NATIVE_RENDER *Render,
    APPLE_AGX_G3_PRIVATE_MANAGER *Manager, APPLE_AGX_G3_PRIVATE_SCENE *Scene);

ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(
    ADMISSION_G3_STATE *State, HANDLE ProcessHandle);
NTSTATUS AdmissionGpuvaG3ResolveTable(
    ADMISSION_CONTEXT *Adapter, const DXGK_PAGETABLEUPDATEADDRESS *Address,
    DXGK_PAGETABLEUPDATEMODE Mode, ULONGLONG *TableIpa);
NTSTATUS AdmissionGpuvaG3BrokerTable(
    ADMISSION_G3_PROCESS *Process, ULONGLONG OriginalIpa,
    BOOLEAN Create, ULONGLONG *BrokerIpa);
NTSTATUS AdmissionGpuvaG3MirrorTable(
    ADMISSION_G3_PROCESS *Process, ULONGLONG OriginalIpa,
    PVOID OriginalCpuAddress);
NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(ADMISSION_CONTEXT *Adapter,
                                           DXGKARG_BUILDPAGINGBUFFER *Args);
NTSTATUS AdmissionG3ExecuteVirtualPaging(
    ADMISSION_CONTEXT *Adapter, const ADMISSION_PAGING_RECORD *Record);
NTSTATUS AdmissionGpuvaG3SubmitVirtualPaging(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *Args);
NTSTATUS AdmissionGpuvaG3BeginJob(ADMISSION_CONTEXT *Adapter,
                                  ADMISSION_RENDER_CONTEXT *Context,
                                  ULONG Fence);
BOOLEAN AdmissionGpuvaG3CompleteJob(ADMISSION_CONTEXT *Adapter, ULONG Fence);

#endif
#endif
