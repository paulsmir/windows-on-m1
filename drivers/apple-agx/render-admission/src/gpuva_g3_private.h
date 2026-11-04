#ifndef ADMISSION_GPUVA_G3_PRIVATE_H
#define ADMISSION_GPUVA_G3_PRIVATE_H

#include "render_admission.h"
#include "apple_agx_g3_private_storage.h"
#include "apple_agx_g3_tvb.h"
#include "apple_agx_render_manager.h"
#include "apple_agx_g3_private_abi.h"
#include "apple_agx_g3_native_cache.h"

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

/* L1 receipt-only (Wom1JobDiagnostics): wait and hold time of the G3 lock per
 * acquisition site ((file << 16) | line), and the latest long waits (with the
 * site that held the lock when the waiter arrived) and long holds. Written
 * under the lock itself; exported with the job timing (2 s throttle). */
#define ADMISSION_G3_LOCK_SITES 128u
#define ADMISSION_G3_LOCK_RING 64u
typedef struct _ADMISSION_G3_LOCK_SITE {
  ULONG Site, Reserved;
  ULONGLONG Count, WaitTicks, WaitMax, HoldTicks, HoldMax;
} ADMISSION_G3_LOCK_SITE;
typedef struct _ADMISSION_G3_LOCK_EVENT {
  ULONGLONG Qpc, Ticks;
  ULONG Site, OtherSite;
} ADMISSION_G3_LOCK_EVENT;
typedef struct _ADMISSION_G3_LOCK_PROFILE {
  ULONG Version, Bytes, Enabled, Dropped;
  ULONGLONG QpcFrequency, ThresholdTicks;
  ULONG NextWait, NextHold;
  ADMISSION_G3_LOCK_SITE Sites[ADMISSION_G3_LOCK_SITES];
  ADMISSION_G3_LOCK_EVENT Waits[ADMISSION_G3_LOCK_RING];
  ADMISSION_G3_LOCK_EVENT Holds[ADMISSION_G3_LOCK_RING];
} ADMISSION_G3_LOCK_PROFILE;

/* L1b receipt-only (with the lock profile): QPC stamps inside BeginJobAs:
 * lock acquired, re-parse done, graph begin (lease) start/end, release. */
#define ADMISSION_G3_BEGIN_SPLIT_RECEIPT 1
#define ADMISSION_G3_BEGIN_SPLIT_RING 64u
typedef struct _ADMISSION_G3_BEGIN_SPLIT {
  ULONG Fence, Successor, Refusal, Native;
  ULONGLONG Acquired, Parsed, LeaseStart, LeaseEnd, Released;
} ADMISSION_G3_BEGIN_SPLIT;
typedef struct _ADMISSION_G3_BEGIN_SPLITS {
  ULONG Version, Bytes, Next, Reserved;
  ADMISSION_G3_BEGIN_SPLIT Ring[ADMISSION_G3_BEGIN_SPLIT_RING];
} ADMISSION_G3_BEGIN_SPLITS;

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
  /* RC68: the context whose job ActiveProcess runs (set and cleared with
   * it). Compared only, never dereferenced: the copy escape accepts a UMD
   * no-reference proof only for the caller's own context's job. */
  ADMISSION_RENDER_CONTEXT *ActiveContext;
  ULONG ActiveFence;
  /* rc102 depth-2 window: the job kicked behind ActiveFence, of the same
   * process and graph lease (AdmissionGpuvaG3BeginSuccessorJob); it
   * becomes the active job when that one completes. */
  ADMISSION_RENDER_CONTEXT *SuccessorContext;
  ULONG SuccessorFence;
  ULONG LastCompletedFence;
  ULONG PrivateCompletionFence;
  ADMISSION_OMITTED_LEAVES OmittedLeaves;
#ifdef APPLE_AGX_NATIVE_STAGING
  /* RC60 receipt, under Lock: first native-staging page left unpublished. */
  ADMISSION_NATIVE_PTE_REFUSAL NativePteRefusal;
#endif
  ULONGLONG UnpublishedGroups[32];
  /* M2-b receipt: valid 64 KiB PTEs applied by leaf updates, per segment. */
  ULONGLONG Leaf64KPtes[32];
  /* M2 receipt: segment-0 (system) 16 KiB groups of valid leaf updates,
   * [Use64KBPages * 4 + outcome]: 0 published, 1 incomplete (not four
   * contiguous 16 KiB-aligned 4 KiB pages), 2 refused by the broker. */
  ULONGLONG SystemGroups[8];
  /* M2 receipt: the same system groups per allocation shape (the leaf
   * update's hAllocation); SystemShapesGeneration counts slot changes. */
  ADMISSION_G3_SYSTEM_SHAPE SystemShapes[ADMISSION_G3_SYSTEM_SHAPE_COUNT];
  ULONG SystemShapesGeneration;
  ADMISSION_G3_UPLOAD_TRACE UploadTrace[ADMISSION_G3_UPLOAD_TRACE_COUNT];
  ULONG UploadTraceNext, UploadVerifyChecks, UploadVerifyMismatch,
      UploadVerifyUnmapped;
  ADMISSION_G3_UPLOAD_TRACE UploadFirstMismatch;
  ADMISSION_G3_LEAF_HISTORY LeafHistory[ADMISSION_G3_LEAF_RING];
  ULONG LeafHistoryNext;
  ADMISSION_G3_ALLOC_TRACK AllocTrack[ADMISSION_G3_ALLOC_TRACK_COUNT];
  /* EXP1087 receipt-only, under Lock: ADMISSION_G3_PRIVATE_STAT_* counters. */
  ULONGLONG PrivateStats[16];
  ULONG PrivatePressure[12]; /* successful foreign-cache reclaim, version 1 */
  /* EXP1114 receipt-only, under Lock: last per-process snapshot (QPC). */
  LONGLONG ProcessReceiptQpc;
  /* G2 HW2 receipt-only: private VA reservations made and refused, present
   * arenas made and refused. */
  LONG ReserveMade, ReserveRefused, PresentArenasMade, PresentArenasRefused;
  /* G2 HW2b receipt-only: presents per path reason (AdmissionG3NotePresentPath)
   * and the QPC of the last receipt write. */
  LONG PresentPath[ADMISSION_G2_PRESENT_PATH_REASONS];
  LONG PresentPathLast, PresentPathFence;
  LONGLONG PresentPathQpc;
  /* D3D-G1: BeginJob re-parses a bound v5 render (compute prelude) from a
   * contiguous envelope it composes here under Lock: the backend image keeps
   * a v2 header next to its command copy. */
  struct {
    APPLE_AGX_G4_PRIVATE_HEADER_V5 Header;
    unsigned char Command[APPLE_AGX_G4_NATIVE_MAX_BYTES];
  } G4ReparseV5;
  /* L1 receipt-only: see ADMISSION_G3_LOCK_PROFILE. Owner fields are
   * written while the lock is held. */
  volatile LONG LockOwnerSite;
  ULONGLONG LockAcquiredQpc;
  ADMISSION_G3_LOCK_PROFILE LockProfile;
  ADMISSION_G3_LOCK_PROFILE LockProfileSnapshot;
  ULONGLONG BeginLeaseStart, BeginLeaseEnd;
  ADMISSION_G3_BEGIN_SPLITS BeginSplits, BeginSplitsSnapshot;
} ADMISSION_G3_STATE;

static __inline ADMISSION_G3_LOCK_SITE *AdmissionG3LockSiteSlot(
    ADMISSION_G3_LOCK_PROFILE *p, ULONG site) {
  ULONG i = (site * 2654435761u) >> 25, n;
  for (n = 0u; n < ADMISSION_G3_LOCK_SITES;
       ++n, i = (i + 1u) & (ADMISSION_G3_LOCK_SITES - 1u)) {
    if (p->Sites[i].Site == site) return &p->Sites[i];
    if (p->Sites[i].Site == 0u) { p->Sites[i].Site = site; return &p->Sites[i]; }
  }
  ++p->Dropped;
  return NULL;
}

static __inline VOID AdmissionG3LockAt(ADMISSION_G3_STATE *s, ULONG site) {
  ULONGLONG start = 0ULL, now;
  LONG holder = 0;
  ADMISSION_G3_LOCK_SITE *slot;
  if (s->LockProfile.Enabled) {
    start = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
    holder = s->LockOwnerSite;
  }
  ExAcquireFastMutex(&s->Lock);
  s->LockOwnerSite = (LONG)site;
  s->LockAcquiredQpc = 0ULL;
  if (!s->LockProfile.Enabled) return;
  now = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
  s->LockAcquiredQpc = now;
  slot = AdmissionG3LockSiteSlot(&s->LockProfile, site);
  if (slot == NULL || start == 0ULL || now < start) return;
  ++slot->Count;
  slot->WaitTicks += now - start;
  if (now - start > slot->WaitMax) slot->WaitMax = now - start;
  if (now - start >= s->LockProfile.ThresholdTicks) {
    ADMISSION_G3_LOCK_EVENT *e = &s->LockProfile.Waits[
        s->LockProfile.NextWait++ % ADMISSION_G3_LOCK_RING];
    e->Qpc = now; e->Ticks = now - start;
    e->Site = site; e->OtherSite = (ULONG)holder;
  }
}

static __inline VOID AdmissionG3UnlockAt(ADMISSION_G3_STATE *s) {
  ULONG site = (ULONG)s->LockOwnerSite;
  ULONGLONG acquired = s->LockAcquiredQpc, now, hold;
  ADMISSION_G3_LOCK_SITE *slot;
  s->LockOwnerSite = 0;
  s->LockAcquiredQpc = 0ULL;
  if (s->LockProfile.Enabled && acquired != 0ULL && site != 0u) {
    now = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
    hold = now >= acquired ? now - acquired : 0ULL;
    slot = AdmissionG3LockSiteSlot(&s->LockProfile, site);
    if (slot != NULL) {
      slot->HoldTicks += hold;
      if (hold > slot->HoldMax) slot->HoldMax = hold;
    }
    if (hold >= s->LockProfile.ThresholdTicks) {
      ADMISSION_G3_LOCK_EVENT *e = &s->LockProfile.Holds[
          s->LockProfile.NextHold++ % ADMISSION_G3_LOCK_RING];
      e->Qpc = now; e->Ticks = hold; e->Site = site; e->OtherSite = 0u;
    }
  }
  ExReleaseFastMutex(&s->Lock);
}

static __inline ADMISSION_G3_STATE *AdmissionG3LockState(FAST_MUTEX *m) {
  return CONTAINING_RECORD(m, ADMISSION_G3_STATE, Lock);
}
/* A file whose every fast mutex is the G3 state Lock defines
 * ADMISSION_G3_LOCK_FILE after its last #include and maps ExAcquireFastMutex /
 * ExReleaseFastMutex to these (the source keeps the plain WDK calls). */
#define ADMISSION_G3_PROFILED_ACQUIRE(m) AdmissionG3LockAt( \
    AdmissionG3LockState(m), (ADMISSION_G3_LOCK_FILE << 16) | (ULONG)__LINE__)
#define ADMISSION_G3_PROFILED_RELEASE(m) \
    AdmissionG3UnlockAt(AdmissionG3LockState(m))
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
/* rc80 (version 2): after the slots, four TVB words per slot
 * (AdmissionG3SnapshotProcesses). Version-1 offsets are unchanged. */
#define ADMISSION_G3_PROCESS_RECEIPT_TVB_WORDS 4u
#define ADMISSION_G3_PROCESS_RECEIPT_WORDS (8u + 16u * ADMISSION_G3_PROCESS_RECEIPT_SLOTS + \
    ADMISSION_G3_PROCESS_RECEIPT_TVB_WORDS * ADMISSION_G3_PROCESS_RECEIPT_SLOTS)

typedef struct _ADMISSION_G3_PROCESS {
  LIST_ENTRY Link;
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  APPLE_AGX_G3_NATIVE_CACHE NativeReady;
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
  /* G2 HW2b: the KMD present arena (apple_agx_present_job.h), an extent of
   * the private range (inside the USC window); 0 Generation until made. */
  APPLE_AGX_G3_PRIVATE_EXTENT PresentArena;
  APPLE_AGX_G3_PRIVATE_MANAGER PrivateManager;
  APPLE_AGX_RENDER_MANAGER_STATE FirmwareManager;
  /* rc80 (apple_agx_g3_tvb.h): the 256 MiB TVB window reserved right after
   * the private window (0 when CreateProcess could not reserve it), its
   * leaf tables (private-pool extents, registered and linked below the
   * private middle table), and two slab leases per backed window block
   * (TvbLeases, NonPaged, allocated at the first window growth). The
   * manager's Blocks counts the floor plus Tvb.LeaseBlocks. */
  ULONGLONG TvbVa;
  APPLE_AGX_G3_PRIVATE_EXTENT TvbLeafTables[APPLE_AGX_G3_TVB_LEAVES];
  ULONGLONG TvbLeafIpa[APPLE_AGX_G3_TVB_LEAVES];
  ADMISSION_POOL_PRIVATE *TvbLeases;
  APPLE_AGX_G3_TVB_STATE Tvb;
  ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;
  ADMISSION_RENDER_CONTEXT *Contexts;
  HANDLE DxgkProcess;
  ULONG Magic, DeviceRefs, ContextRefs, OsProcessId;
#ifdef APPLE_AGX_NATIVE_STAGING
  ULONG NativeMapRefs;
  BOOLEAN NativeClosing;
#endif
  /* Lifetime is this process object, not a reusable numeric PID. State Lock
   * protects arming, one-shot consumption and captured hardware evidence. */
  ADMISSION_GPU_FAULT_DRILL FaultDrill;
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

/* Logical identity can change even when a native leaf is byte-identical.
 * Invalidate both owners of every affected quartet, including neighbours
 * whose NativeIpa changes after a one-PTE edit. No key is dereferenced. */
static inline void AdmissionG3NativeInvalidatePtes(ADMISSION_G3_PROCESS *p,
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *ptes, UINT first, UINT count) {
  UINT i;
  if (!ptes) return;
  for (i = first; i < first + count; ++i)
    if (ptes[i].Allocation)
      AppleAgxG3NativeCacheInvalidate(&p->NativeReady, ptes[i].Allocation);
}

/* Link the private window's leaf, then every registered TVB window leaf,
 * below the current root (after a root bind or a parent update). */
static inline BOOLEAN AdmissionG3AttachPrivateLeaves(ADMISSION_G3_PROCESS *p) {
  UINT i;
  if (!AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph, p->PrivateVa,
          p->PrivateMiddleIpa, p->PrivateLeafIpa)) return FALSE;
  for (i = 0u; i < APPLE_AGX_G3_TVB_LEAVES; ++i)
    if (p->TvbVa && p->TvbLeafIpa[i] &&
        !AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph,
            p->TvbVa + (ULONGLONG)i * APPLE_AGX_G3_TVB_LEAF_BYTES,
            p->PrivateMiddleIpa, p->TvbLeafIpa[i])) return FALSE;
  return TRUE;
}

/* The private leaf set for root parking: entry 0 is the private window. */
static inline UINT AdmissionG3PrivateLeafSet(const ADMISSION_G3_PROCESS *p,
    ULONGLONG vas[1u + APPLE_AGX_G3_TVB_LEAVES],
    ULONGLONG leaves[1u + APPLE_AGX_G3_TVB_LEAVES]) {
  UINT i, count = 1u;
  vas[0] = p->PrivateVa;
  leaves[0] = p->PrivateLeafIpa;
  for (i = 0u; i < APPLE_AGX_G3_TVB_LEAVES; ++i)
    if (p->TvbVa && p->TvbLeafIpa[i]) {
      vas[count] = p->TvbVa + (ULONGLONG)i * APPLE_AGX_G3_TVB_LEAF_BYTES;
      leaves[count++] = p->TvbLeafIpa[i];
    }
  return count;
}

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
/* rc102: BeginJob for a job kicked behind the active one: the same
 * re-proof of the bound image under the graph lock, but the job joins the
 * active job's process lease (AppleAgxGpuvaG3GraphBeginNestedJob) as its
 * successor, with its output proven from its own (kicked) entry's packet.
 * Refused for another process or with a successor already. *Refusal: the
 * failed check (AdmissionGpuvaG3BeginJobAs; 9 = no kicked entry). */
NTSTATUS AdmissionGpuvaG3BeginSuccessorJob(ADMISSION_CONTEXT *Adapter,
                                           ADMISSION_RENDER_CONTEXT *Context,
                                           ULONG Fence, ULONG *Refusal);
/* rc102 (EXP2051): the buffer-manager binding BeginJob will give the bound
 * native render of Fence (its private scene's: the process manager, its
 * key from the scene's lists, heap window and backed blocks). TRUE with
 * *Manager and *Key when BeginJob will set them; FALSE when it would not
 * (no private scene for that fence): the image's binding stays. Read-only,
 * PASSIVE_LEVEL. The depth-2 decision proves the manager-bind need from
 * this, not from the image's binding of the previous render. */
/* q1: resolve an idle manager snapshot by process key, never by a retained
 * process pointer. A destroyed/obsolete owner needs no saved state. */
BOOLEAN AdmissionGpuvaG3StoreManagerSnapshot(ADMISSION_CONTEXT *Adapter,
    const APPLE_AGX_RENDER_MANAGER_STATE *Snapshot);
BOOLEAN AdmissionGpuvaG3PendingManager(ADMISSION_CONTEXT *Adapter,
                                       ADMISSION_RENDER_CONTEXT *Context,
                                       ULONG Fence,
                                       APPLE_AGX_RENDER_MANAGER_STATE **Manager,
                                       APPLE_AGX_RENDER_MANAGER_KEY *Key);

#endif
#endif
