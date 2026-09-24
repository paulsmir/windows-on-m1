#ifndef APPLE_AGX_GPUVA_G3_GRAPH_H
#define APPLE_AGX_GPUVA_G3_GRAPH_H

#include "apple_agx_gpuva_broker_v5_client.h"

typedef void *(*APPLE_AGX_GPUVA_G3_ALLOC)(void *, unsigned long long);
typedef void (*APPLE_AGX_GPUVA_G3_FREE)(void *, void *);

typedef struct _APPLE_AGX_GPUVA_G3_NODE {
  struct _APPLE_AGX_GPUVA_G3_NODE *Next;
  unsigned long long Ipa, AuxIpa, Generation;
  unsigned int Index, Level, References, Writable;
} APPLE_AGX_GPUVA_G3_NODE;

typedef struct _APPLE_AGX_GPUVA_G3_GRAPH {
  APPLE_AGX_GPUVA_V5_CLIENT *Client;
  APPLE_AGX_GPUVA_G3_ALLOC Allocate;
  APPLE_AGX_GPUVA_G3_FREE Free;
  void *MemoryContext;
  unsigned long long ProcessId, ProcessGeneration, RootIpa, NextGeneration;
  unsigned long long LeaseToken;
  APPLE_AGX_GPUVA_G3_NODE *Tables, *Parents, *Leaves, *Backings;
  unsigned int LastStatus, Created, Uncertain, JobInFlight, Slot;
} APPLE_AGX_GPUVA_G3_GRAPH;

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
bool AppleAgxGpuvaG3GraphFlush(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long StartVa, unsigned long long EndVa);
bool AppleAgxGpuvaG3GraphContainsRange(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long StartVa, unsigned int Bytes);
bool AppleAgxGpuvaG3GraphTranslateVa(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned long long GpuVa, unsigned long long *GuestIpa);
bool AppleAgxGpuvaG3GraphBeginJob(APPLE_AGX_GPUVA_G3_GRAPH *,
    unsigned int Slot);
bool AppleAgxGpuvaG3GraphEndJob(APPLE_AGX_GPUVA_G3_GRAPH *);
bool AppleAgxGpuvaG3GraphDestroy(APPLE_AGX_GPUVA_G3_GRAPH *);

#endif
