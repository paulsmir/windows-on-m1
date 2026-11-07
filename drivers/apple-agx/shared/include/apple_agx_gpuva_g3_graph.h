#ifndef APPLE_AGX_GPUVA_G3_GRAPH_H
#define APPLE_AGX_GPUVA_G3_GRAPH_H

#include "apple_agx_gpuva_broker_v5_client.h"
#include "apple_agx_g3_copy_query_receipt.h"

typedef void *(*APPLE_AGX_GPUVA_G3_ALLOC)(void *, unsigned long long);
typedef void (*APPLE_AGX_GPUVA_G3_FREE)(void *, void *);

/* Serialized by the adapter lock, shared across all process roots. A grant
 * is not an OS pin: mapping references come only from ordered VidMm PTEs. */
/* Lookup indices: every 16-KiB page operation looked frames, backings and
 * leaves up by walking whole lists (EXP1052: ~21 s of KMD CPU per 4 min in
 * UpdatePageTable outside the broker).  Lists stay the iteration order;
 * the buckets and Prev links make lookup and removal O(1). */
#define APPLE_AGX_GPUVA_G3_FRAME_BUCKETS 4096u
#define APPLE_AGX_GPUVA_G3_BACKING_BUCKETS 4096u
typedef struct _APPLE_AGX_GPUVA_G3_FRAME {
  struct _APPLE_AGX_GPUVA_G3_FRAME *Next;
  unsigned long long Ipa, Generation, Mappings, Grants;
  struct _APPLE_AGX_GPUVA_G3_FRAME *Prev, *HashNext;
} APPLE_AGX_GPUVA_G3_FRAME;
typedef struct _APPLE_AGX_GPUVA_G3_REGISTRY {
  APPLE_AGX_GPUVA_G3_FRAME *Frames;
  unsigned long long NextGeneration;
  APPLE_AGX_GPUVA_G3_FRAME *FrameBuckets[APPLE_AGX_GPUVA_G3_FRAME_BUCKETS];
} APPLE_AGX_GPUVA_G3_REGISTRY;
/* EXP1125: unreferenced shared local-reserve grants kept registered per
 * process (a broker bitmap bit each) so the next mapping of the same pages
 * skips REGISTER/REVOKE. Broker tables never live in VidMm's local segment
 * (the KMD shadows every table), so a kept grant cannot block a table. */
#define APPLE_AGX_GPUVA_G3_RETAINED_LOCAL 4096u
typedef enum _APPLE_AGX_GPUVA_G3_BACKING_KIND {
  AppleAgxGpuvaG3LocalBacking = 0,
  AppleAgxGpuvaG3SystemBacking = 1,
  AppleAgxGpuvaG3PrivateBacking = 2
} APPLE_AGX_GPUVA_G3_BACKING_KIND;

typedef struct _APPLE_AGX_GPUVA_G3_NODE {
  struct _APPLE_AGX_GPUVA_G3_NODE *Next;
  struct _APPLE_AGX_GPUVA_G3_NODE *ChildTable;
  struct _APPLE_AGX_GPUVA_G3_NODE **Slots;
  unsigned long long Ipa, AuxIpa, Generation;
  unsigned int Index, Level, References, Writable;
  unsigned int SystemRetired;
  unsigned int Retained; /* backing kept with no references (EXP1125) */
  /* GC (opt-in): the leaf was published GPU-private cached (UPDATE_LEAF
   * Flags bit AGX_GPUVA_V5_LEAF_PRIVATE_CACHED). */
  unsigned int PrivateCached;
  APPLE_AGX_GPUVA_G3_FRAME *Frame;
  APPLE_AGX_GPUVA_G3_BACKING_KIND Kind;
  struct _APPLE_AGX_GPUVA_G3_NODE *Prev, *HashNext;
} APPLE_AGX_GPUVA_G3_NODE;

/* rc81 (EXP2011): a successful proof of a large range (at least
 * APPLE_AGX_GPUVA_G3_PROOF_MIN_PAGES pages, e.g. the grown TVB heap), keyed by
 * VA, bytes and root, with the leaf tables its walk crossed. Every native
 * mutation drops it: a topology change (BindRoot, UpdateParent, table
 * re-registration) all entries, a leaf update the entries that crossed that
 * leaf table. Used under the caller's graph lock only. */
/* L3-P1 (EXP2157): a render job proves more than four large ranges per
 * parse (TVB heap, colour/depth targets, ...); 4 round-robin entries
 * missed on every pass. */
#define APPLE_AGX_GPUVA_G3_PROOF_ENTRIES 32u
#define APPLE_AGX_GPUVA_G3_PROOF_TABLES 16u
#define APPLE_AGX_GPUVA_G3_PROOF_MIN_PAGES 64u
typedef struct _APPLE_AGX_GPUVA_G3_RANGE_PROOF {
  unsigned long long Va, RootIpa;
  unsigned long long LeafTables[APPLE_AGX_GPUVA_G3_PROOF_TABLES];
  /* L3-P1b: the 32 MiB-aligned VA at which LeafTables[k] maps this range;
   * a leaf change outside the proven pages keeps the proof. */
  unsigned long long LeafVaBase[APPLE_AGX_GPUVA_G3_PROOF_TABLES];
  unsigned int Bytes, Write, Valid, TableCount;
} APPLE_AGX_GPUVA_G3_RANGE_PROOF;

/* V1 (device value Wom1G3RetainedSlots = 1): retained per-process VM slots.
 * One table per adapter, serialized by the same lock as the graphs. A graph
 * attached to an enabled table (Count != 0) keeps its lease after its last
 * job (EndJob sends JOB_END only); BeginJob of a leased graph sends JOB_BEGIN
 * only, otherwise it leases the graph's preferred slot, else the least
 * recently used free slot, else steals the least recently used idle lease
 * (never one with a job). Every translation change of a leased idle graph
 * releases the lease first (rule R): UPDATE_LEAF/UPDATE_PARENT/RELOCATE_ROOT/
 * DESTROY run unleased, as without the table. Slots First..First+Count-1,
 * at most slot 15 (the G13 builders refuse VmSlot >= 16). The KMD starts at
 * slot 2 so that slot 1 keeps its baseline meaning (leased per job only,
 * zero TTBR when idle) for any job that ran without a table lease.
 * Unattached graphs or Count == 0: every job leases the caller's slot and
 * EndJob releases it (the baseline). */
#define APPLE_AGX_GPUVA_G3_RETAINED_SLOTS_MAX 15u
/* A quarantined slot is tried again after this many table clock ticks
 * (two per retained job): a refused LEASE has no broker side effect. */
#define APPLE_AGX_GPUVA_G3_QUARANTINE_RETRY 4096u
struct _APPLE_AGX_GPUVA_G3_GRAPH;
typedef struct _APPLE_AGX_GPUVA_G3_SLOTS {
  struct _APPLE_AGX_GPUVA_G3_GRAPH *Owner[APPLE_AGX_GPUVA_G3_RETAINED_SLOTS_MAX + 1u];
  unsigned long long LastUse[APPLE_AGX_GPUVA_G3_RETAINED_SLOTS_MAX + 1u];
  /* Clock value at which slot s was quarantined. */
  unsigned long long QuarantinedAt[APPLE_AGX_GPUVA_G3_RETAINED_SLOTS_MAX + 1u];
  unsigned long long Clock;
  unsigned int First, Count;
  /* Bit s: slot s is not leased (a refused LEASE, a failed steal) until
   * APPLE_AGX_GPUVA_G3_QUARANTINE_RETRY ticks have passed. */
  unsigned int Quarantined;
  /* Receipt-only counters (Quarantines: slots put into quarantine;
   * Recovered: quarantines that expired for a retry). */
  unsigned long long Leases, Reuses, Steals, Releases, Quarantines, Recovered;
  /* V1 receipt: rule-R releases by caller source line (first 8 lines). */
  unsigned int ReleaseSite[8];
  unsigned long long ReleaseSiteCount[8];
} APPLE_AGX_GPUVA_G3_SLOTS;

typedef struct _APPLE_AGX_GPUVA_G3_GRAPH {
  APPLE_AGX_GPUVA_V5_CLIENT *Client;
  APPLE_AGX_GPUVA_G3_ALLOC Allocate;
  APPLE_AGX_GPUVA_G3_FREE Free;
  void *MemoryContext;
  /* Optional proof-cache invalidation, before native mutation, under the
   * caller's graph lock. TableIpa=0 invalidates topology; otherwise Index
   * names the 16-KiB leaf in that table. No firmware protocol change. */
  void (*InvalidateNative)(void *Context, unsigned long long TableIpa,
                           unsigned int Index);
  void *InvalidateContext;
  unsigned long long ProcessId, ProcessGeneration, RootIpa, NextGeneration;
  unsigned long long SharedBackingGeneration;
  APPLE_AGX_GPUVA_G3_REGISTRY *Registry;
  unsigned long long MappingGeneration;
  unsigned long long LeaseToken;
  APPLE_AGX_GPUVA_G3_NODE *Tables, *Parents, *Leaves, *Backings;
  APPLE_AGX_GPUVA_G3_NODE *RootTable;
  unsigned int LastStatus, Created, Uncertain, JobInFlight, Slot;
  unsigned int LiveAdditive;
  /* GC (opt-in, device value Wom1GpuPrivateCached = 1 with a broker that
   * advertises the capability): publish GPU-only allocations cached. */
  unsigned int PrivateCached;
  APPLE_AGX_GPUVA_G3_NODE *TableHint;
  /* EXP1053 receipt-only: last AttachPrivate refusal (0 none, 1 state,
   * 2 job in flight, 3 lease, 4 VA, 5 middle table, 6 leaf table,
   * 7 edge conflict, 8 middle link, 9 root link). */
  unsigned int AttachFailure;
  unsigned int RetainedLocal;
  /* One node reserved for when the allocator fails: re-linking the private
   * leaf after a root move (detach, bind, attach) must not depend on a
   * fresh allocation (AppleAgxGpuvaG3GraphReserveNode). */
  struct _APPLE_AGX_GPUVA_G3_NODE *SpareNode;
  APPLE_AGX_GPUVA_G3_NODE *BackingBuckets[APPLE_AGX_GPUVA_G3_BACKING_BUCKETS];
  APPLE_AGX_GPUVA_G3_RANGE_PROOF Proofs[APPLE_AGX_GPUVA_G3_PROOF_ENTRIES];
  unsigned int NextProof;
  /* rc102 (depth 2): a second job of this process holds the same lease
   * (JOB_BEGIN pins the graph once; the lease ends with the last job). */
  unsigned int JobNested;
  /* V1: the retained slot table (NULL: baseline) and the slot this graph
   * leased last (Asahi's bind token; 0 none). */
  APPLE_AGX_GPUVA_G3_SLOTS *SlotTable;
  unsigned int PreferredSlot;
} APPLE_AGX_GPUVA_G3_GRAPH;

#ifdef APPLE_AGX_G3_LOOKUP_STATS
void AppleAgxGpuvaG3LookupStatsReset(void);
unsigned long long AppleAgxGpuvaG3LookupStatsVisits(void);
#endif

bool AppleAgxGpuvaG3GraphInit(APPLE_AGX_GPUVA_G3_GRAPH *,
    APPLE_AGX_GPUVA_V5_CLIENT *, unsigned long long ProcessId,
    unsigned long long ProcessGeneration, APPLE_AGX_GPUVA_G3_ALLOC,
    APPLE_AGX_GPUVA_G3_FREE, void *MemoryContext);
bool AppleAgxGpuvaG3GraphNegotiateLive(APPLE_AGX_GPUVA_G3_GRAPH *, bool);
bool AppleAgxGpuvaG3GraphCreate(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long RootIpa, bool Paging);
bool AppleAgxGpuvaG3GraphRegisterTable(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long TableIpa, unsigned int Level);
bool AppleAgxGpuvaG3GraphBindRoot(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long RootIpa);
bool AppleAgxGpuvaG3GraphUpdateParent(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long TableIpa, unsigned int Index,
    unsigned long long ChildIpa);
bool AppleAgxGpuvaG3GraphUpdateLeaf(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long TableIpa, unsigned int Index,
    unsigned long long GuestIpa, bool Writable);
bool AppleAgxGpuvaG3GraphUpdateLeafBacking(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long TableIpa, unsigned int Index,
    unsigned long long GuestIpa, bool Writable,
    APPLE_AGX_GPUVA_G3_BACKING_KIND Kind);
/* Unavailable is set only for an acknowledged system grant refusal before
 * any UAT mutation. Other failures, including uncertain stores, remain fatal. */
bool AppleAgxGpuvaG3GraphTryLeafBacking(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long TableIpa, unsigned int Index,
    unsigned long long GuestIpa, bool Writable,
    APPLE_AGX_GPUVA_G3_BACKING_KIND Kind, bool *Unavailable);
/* GC: as TryLeafBacking; gpu_private marks an allocation the CPU never maps. */
bool AppleAgxGpuvaG3GraphTryLeafBackingEx(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long TableIpa, unsigned int Index,
    unsigned long long GuestIpa, bool Writable,
    APPLE_AGX_GPUVA_G3_BACKING_KIND Kind, bool *Unavailable, bool gpu_private);
bool AppleAgxGpuvaG3MappingAcquire(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long GuestIpa);
void AppleAgxGpuvaG3MappingRelease(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long GuestIpa);
bool AppleAgxGpuvaG3GraphFlush(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long StartVa, unsigned long long EndVa);
bool AppleAgxGpuvaG3GraphContainsRange(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long StartVa, unsigned int Bytes);
bool AppleAgxGpuvaG3GraphContainsRangeAccess(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long StartVa, unsigned int Bytes, bool Write);
/* AppleAgxGpuvaG3GraphContainsRangeAccess with the proof cache above: the
 * same answer, O(1) for a repeated large range until a mutation can change
 * it. Failures are not cached; a read proof never answers a write. */
bool AppleAgxGpuvaG3GraphContainsRangeAccessCached(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long StartVa, unsigned int Bytes, bool Write);
/* The leaf edge at (level-2 table, index), or NULL; O(1). */
const APPLE_AGX_GPUVA_G3_NODE *AppleAgxGpuvaG3GraphLeaf(
    APPLE_AGX_GPUVA_G3_GRAPH *, unsigned long long table_ipa,
    unsigned int index);
bool AppleAgxGpuvaG3GraphTranslateVa(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long GpuVa, unsigned long long *GuestIpa);
bool AppleAgxGpuvaG3GraphLeafTableIpa(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long GpuVa, unsigned long long *TableIpa);
bool AppleAgxGpuvaG3GraphInspectRangeAccess(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long StartVa, unsigned int Bytes, bool Write,
    APPLE_AGX_GPUVA_G3_WALK_FAILURE *Failure);
/* Keep one node in reserve for an allocation failure; false only when
 * none is held and none can be allocated. */
bool AppleAgxGpuvaG3GraphReserveNode(APPLE_AGX_GPUVA_G3_GRAPH *);
bool AppleAgxGpuvaG3GraphAttachPrivate(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long ReservedVa, unsigned long long MiddleIpa,
    unsigned long long LeafIpa);
bool AppleAgxGpuvaG3GraphCanDetachPrivateRoot(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long ReservedVa, unsigned long long RootIpa,
    unsigned long long LeafIpa);
bool AppleAgxGpuvaG3GraphDetachPrivateRoot(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long ReservedVa, unsigned long long RootIpa,
    unsigned long long LeafIpa);
/* rc80: the same for every private leaf of the branch (entry 0 is the
 * private window's leaf); all VAs share one root slot. */
bool AppleAgxGpuvaG3GraphCanDetachPrivateRootSet(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long RootIpa, const unsigned long long *ReservedVas,
    const unsigned long long *LeafIpas, unsigned Count);
bool AppleAgxGpuvaG3GraphDetachPrivateRootSet(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long RootIpa, const unsigned long long *ReservedVas,
    const unsigned long long *LeafIpas, unsigned Count);
/* Slot: the VM slot to lease without an enabled slot table (1..62). With
 * one, the table picks the graph's slot (Graph.Slot after success). */
bool AppleAgxGpuvaG3GraphBeginJob(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned int Slot);
/* Ends the job; releases the lease unless the graph retains it (V1). */
bool AppleAgxGpuvaG3GraphEndJob(APPLE_AGX_GPUVA_G3_GRAPH *);
/* V1: RELEASE a lease held with no job in flight (true when none is held;
 * false with a job, an Uncertain graph or a refused RELEASE, which makes
 * the graph Uncertain). */
bool AppleAgxGpuvaG3GraphReleaseIdle(APPLE_AGX_GPUVA_G3_GRAPH *);
/* V1 rule R: as ReleaseIdle for a graph that retains its lease and has no
 * job in flight; true and nothing sent otherwise (the baseline). */
bool AppleAgxGpuvaG3GraphReleaseRetainedAt(APPLE_AGX_GPUVA_G3_GRAPH *,
                                           unsigned int site);
/* V1 receipt: the caller's line names the rule-R release reason. */
#define AppleAgxGpuvaG3GraphReleaseRetained(g) \
  AppleAgxGpuvaG3GraphReleaseRetainedAt((g), (unsigned int)__LINE__)
/* V1: enable a zeroed table with slots First..First+Count-1 (First >= 1,
 * last <= 15); false otherwise or when already enabled differently. */
bool AppleAgxGpuvaG3SlotsEnable(APPLE_AGX_GPUVA_G3_SLOTS *, unsigned int First,
                                unsigned int Count);
/* rc102: a second job joins the job in flight on the same lease and VM
 * slot (no broker call); EndJob then ends one job, the lease only with the
 * last. At most two jobs. */
bool AppleAgxGpuvaG3GraphBeginNestedJob(APPLE_AGX_GPUVA_G3_GRAPH *);
bool AppleAgxGpuvaG3GraphDestroy(APPLE_AGX_GPUVA_G3_GRAPH *);

#endif
