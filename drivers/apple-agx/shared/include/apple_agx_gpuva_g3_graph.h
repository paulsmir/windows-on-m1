#ifndef APPLE_AGX_GPUVA_G3_GRAPH_H
#define APPLE_AGX_GPUVA_G3_GRAPH_H

#include "apple_agx_gpuva_broker_v5_client.h"
#include "apple_agx_g3_copy_query_receipt.h"

typedef void *(*APPLE_AGX_GPUVA_G3_ALLOC)(void *, unsigned long long);
typedef void (*APPLE_AGX_GPUVA_G3_FREE)(void *, void *);

/* Serialized by the adapter lock, shared across all process roots. A grant
 * is not an OS pin: mapping references come only from ordered VidMm PTEs. */
typedef struct _APPLE_AGX_GPUVA_G3_FRAME {
  struct _APPLE_AGX_GPUVA_G3_FRAME *Next;
  unsigned long long Ipa, Generation, Mappings, Grants;
} APPLE_AGX_GPUVA_G3_FRAME;
typedef struct _APPLE_AGX_GPUVA_G3_REGISTRY {
  APPLE_AGX_GPUVA_G3_FRAME *Frames;
  unsigned long long NextGeneration;
} APPLE_AGX_GPUVA_G3_REGISTRY;
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
  APPLE_AGX_GPUVA_G3_FRAME *Frame;
  APPLE_AGX_GPUVA_G3_BACKING_KIND Kind;
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
