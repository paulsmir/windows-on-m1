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
  APPLE_AGX_GPUVA_G3_FRAME *Frame;
  APPLE_AGX_GPUVA_G3_BACKING_KIND Kind;
  struct _APPLE_AGX_GPUVA_G3_NODE *Prev, *HashNext;
} APPLE_AGX_GPUVA_G3_NODE;

typedef struct _APPLE_AGX_GPUVA_G3_GRAPH {
  APPLE_AGX_GPUVA_V5_CLIENT *Client;
  APPLE_AGX_GPUVA_G3_ALLOC Allocate;
  APPLE_AGX_GPUVA_G3_FREE Free;
  void *MemoryContext;
  unsigned long long ProcessId, ProcessGeneration, RootIpa, NextGeneration;
  unsigned long long SharedBackingGeneration;
  APPLE_AGX_GPUVA_G3_REGISTRY *Registry;
  unsigned long long MappingGeneration;
  unsigned long long LeaseToken;
  APPLE_AGX_GPUVA_G3_NODE *Tables, *Parents, *Leaves, *Backings;
  APPLE_AGX_GPUVA_G3_NODE *RootTable;
  unsigned int LastStatus, Created, Uncertain, JobInFlight, Slot;
  APPLE_AGX_GPUVA_G3_NODE *TableHint;
  /* EXP1053 receipt-only: last AttachPrivate refusal (0 none, 1 state,
   * 2 job in flight, 3 lease, 4 VA, 5 middle table, 6 leaf table,
   * 7 edge conflict, 8 middle link, 9 root link). */
  unsigned int AttachFailure;
  unsigned int RetainedLocal;
  APPLE_AGX_GPUVA_G3_NODE *BackingBuckets[APPLE_AGX_GPUVA_G3_BACKING_BUCKETS];
} APPLE_AGX_GPUVA_G3_GRAPH;

#ifdef APPLE_AGX_G3_LOOKUP_STATS
void AppleAgxGpuvaG3LookupStatsReset(void);
unsigned long long AppleAgxGpuvaG3LookupStatsVisits(void);
#endif

bool AppleAgxGpuvaG3GraphInit(APPLE_AGX_GPUVA_G3_GRAPH *,
    APPLE_AGX_GPUVA_V5_CLIENT *, unsigned long long ProcessId,
    unsigned long long ProcessGeneration, APPLE_AGX_GPUVA_G3_ALLOC,
    APPLE_AGX_GPUVA_G3_FREE, void *MemoryContext);
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
bool AppleAgxGpuvaG3GraphAttachPrivate(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long ReservedVa, unsigned long long MiddleIpa,
    unsigned long long LeafIpa);
bool AppleAgxGpuvaG3GraphCanDetachPrivateRoot(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long ReservedVa, unsigned long long RootIpa,
    unsigned long long LeafIpa);
bool AppleAgxGpuvaG3GraphDetachPrivateRoot(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long ReservedVa, unsigned long long RootIpa,
    unsigned long long LeafIpa);
bool AppleAgxGpuvaG3GraphBeginJob(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned int Slot);
bool AppleAgxGpuvaG3GraphEndJob(APPLE_AGX_GPUVA_G3_GRAPH *);
bool AppleAgxGpuvaG3GraphDestroy(APPLE_AGX_GPUVA_G3_GRAPH *);

#endif
