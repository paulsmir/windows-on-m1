/* Actual AdmissionG3UpdateLeaf + actual address translation. OS allocation,
 * memory views and the graph/broker boundary are replaced locally. This does
 * not emulate VidMm placement, broker grants, GPU execution or TLB completion. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_gpuva_g3_translation.h"
typedef int NTSTATUS, BOOLEAN;
typedef unsigned UINT;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
#define TRUE 1
#define FALSE 0
#define MAXULONGLONG (~0ULL)
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_DEVICE_BUSY (-2)
#define STATUS_INVALID_DEVICE_STATE (-3)
#define STATUS_INSUFFICIENT_RESOURCES (-4)
#define STATUS_INTEGER_OVERFLOW (-5)
#define STATUS_DEVICE_HARDWARE_ERROR (-6)
#define NT_SUCCESS(s) ((s) >= 0)
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define ADMISSION_MEMORY_POOL_SEGMENT 3u
#define ADMISSION_G3_FRAME_SEGMENT(s) ((s) == 0u || (s) == 3u)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(d,s,n) memcpy(d,s,n)
typedef struct { ULONGLONG GuestIpaAddress, Bytes; } ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct { int unused; } ADMISSION_CONTEXT, ADMISSION_G3_PAGING_FAILURE;
typedef struct {
  UINT Valid, Zero, CacheCoherent, NoExecute, LargePage, PhysicalAdapterIndex;
  UINT PageTablePageSize, SystemReserved0, Reserved, Segment, ReadOnly;
  ULONGLONG PageAddress;
} DXGK_PTE;
typedef struct {
  struct { UINT Use64KBPages, Repeat; } Flags;
  UINT NumPageTableEntries, StartIndex;
  ULONGLONG FirstPteVirtualAddress, AllocationOffsetInBytes;
  void *hAllocation;
  DXGK_PTE *pPageTableEntries;
} DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;
typedef struct { ULONGLONG AuxIpa; UINT Writable, Kind; } APPLE_AGX_GPUVA_G3_NODE;
enum { AppleAgxGpuvaG3SystemBacking, AppleAgxGpuvaG3LocalBacking,
       AdmissionG3PagingFailureLeafGraph };
typedef struct {
  UINT JobInFlight, LeaseToken, Uncertain, LastStatus;
  ULONGLONG MappingGeneration;
  APPLE_AGX_GPUVA_G3_NODE Leaf;
  UINT Mappings, Publications;
} GRAPH;
typedef struct _SHADOW {
  struct _SHADOW *Next;
  ULONGLONG BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *PendingPtes, *LogicalPtes, *ResidentPtes;
} ADMISSION_G3_TABLE_SHADOW;
typedef struct { ULONGLONG SystemGroups[8], UnpublishedGroups[32]; } STATE;
typedef struct {
  GRAPH Graph;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  STATE *State;
  UINT CpuOnlyMappings;
} ADMISSION_G3_PROCESS;
static void *ExAllocatePool2(UINT flags, size_t bytes, UINT tag) {
  (void)flags; (void)tag; return malloc(bytes);
}
static void ExFreePoolWithTag(void *p, UINT tag) { (void)tag; free(p); }
static NTSTATUS AdmissionMemoryRuntimeLocalView(ADMISSION_CONTEXT *a,
    ADMISSION_SCANOUT_MEMORY_VIEW *v) {
  (void)a; v->GuestIpaAddress=0x80000000ULL; v->Bytes=0x100000; return 0;
}
static int AdmissionMemoryPoolView(ADMISSION_CONTEXT *a,
    ADMISSION_SCANOUT_MEMORY_VIEW *v) { (void)a; memset(v,0,sizeof(*v)); return 0; }
static NTSTATUS AdmissionG3PoolPlace(ADMISSION_CONTEXT *a,
    ADMISSION_G3_PROCESS *p, const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *u,
    APPLE_AGX_GPUVA_G3_LOGICAL_PTE *ptes, UINT first, UINT count) {
  (void)a; (void)p; (void)u;
  for (UINT i=first;i<first+count;++i) assert(ptes[i].SegmentId==0u);
  return 0;
}
static void AdmissionG3PoolPagingRefs(ADMISSION_CONTEXT *a,
    APPLE_AGX_GPUVA_G3_LOGICAL_PTE *old, APPLE_AGX_GPUVA_G3_LOGICAL_PTE *next,
    UINT first, UINT count) { (void)a;(void)old;(void)next;(void)first;(void)count; }
static void AdmissionG3CountLeaf64K(STATE *s,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *u) { (void)s;(void)u; }
static int AppleAgxGpuvaG3MappingAcquire(GRAPH *g, ULONGLONG ipa) {
  assert(ipa); ++g->Mappings; return 1;
}
static void AppleAgxGpuvaG3MappingRelease(GRAPH *g, ULONGLONG ipa) {
  assert(ipa && g->Mappings); --g->Mappings;
}
static const APPLE_AGX_GPUVA_G3_NODE *AppleAgxGpuvaG3GraphLeaf(
    GRAPH *g, ULONGLONG table, UINT index) {
  assert(table==0x90000000 && index==0); return g->Leaf.AuxIpa ? &g->Leaf : NULL;
}
static int AppleAgxGpuvaG3GraphUpdateLeafBacking(GRAPH *g, ULONGLONG table,
    UINT index, ULONGLONG ipa, bool write, UINT kind) {
  assert(table==0x90000000 && index==0);
  g->Leaf=(APPLE_AGX_GPUVA_G3_NODE){ipa,write,kind}; ++g->Publications; return 1;
}
static int AppleAgxGpuvaG3GraphTryLeafBacking(GRAPH *g, ULONGLONG table,
    UINT index, ULONGLONG ipa, bool write, UINT kind, bool *unavailable) {
  *unavailable=false;
  return AppleAgxGpuvaG3GraphUpdateLeafBacking(g,table,index,ipa,write,kind);
}
static NTSTATUS AdmissionG3RejectPaging(ADMISSION_G3_PAGING_FAILURE *f,
    UINT branch, UINT index, const DXGK_PTE *pte, ULONGLONG ipa, NTSTATUS status) {
  (void)f;(void)branch;(void)index;(void)pte;(void)ipa; return status;
}
static void AdmissionG3NoteSystemShape(STATE *s, ULONGLONG allocation,
    BOOLEAN wide, BOOLEAN complete, BOOLEAN pool, ULONGLONG va, ULONGLONG ipa) {
  (void)s;(void)allocation;(void)wide;(void)complete;(void)pool;(void)va;(void)ipa;
}
#include "leaf.inc"

int main(void) {
  ADMISSION_CONTEXT adapter={0}; STATE state={0};
  ADMISSION_G3_TABLE_SHADOW shadow={0}; ADMISSION_G3_PROCESS process={0};
  ADMISSION_G3_PAGING_FAILURE failure={0}; DXGK_PTE ptes[4]={0};
  DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE update={0};
  shadow.BrokerIpa=0x90000000; process.TableShadows=&shadow; process.State=&state;
  update.NumPageTableEntries=4; update.pPageTableEntries=ptes;
  update.hAllocation=&adapter;
  for (UINT i=0;i<4;++i) { ptes[i].Valid=1; ptes[i].PageAddress=0x10000+i; }
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0x10000000 && process.Graph.Leaf.Writable);
  assert(process.Graph.Mappings==4);

  /* Keep all four logical pages valid but replace the second PFN. */
  ptes[1].PageAddress=0x20001;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0 && process.Graph.Mappings==4);
  assert(state.UnpublishedGroups[0]==1);
  for (UINT i=0;i<4;++i) {
    assert(shadow.LogicalPtes[i].GuestIpa==ptes[i].PageAddress*0x1000ULL);
    assert(shadow.LogicalPtes[i].Allocation==(ULONGLONG)(ULONG_PTR)&adapter);
    assert(shadow.LogicalPtes[i].AllocationOffset==i*0x1000ULL);
  }
  /* A one-PTE repair must merge with the three retained pages. */
  ptes[1].PageAddress=0x10001; update.StartIndex=1;
  update.FirstPteVirtualAddress=0x1000; update.AllocationOffsetInBytes=0x1000;
  update.NumPageTableEntries=1; update.pPageTableEntries=ptes+1;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0x10000000 && process.Graph.Mappings==4);
  /* A mixed permission group must not widen write access. */
  ptes[1].ReadOnly=1;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0);
  /* Restore, then remove just one quarter: retire the old native leaf. */
  ptes[1].ReadOnly=0;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0x10000000);
  ptes[1].Valid=0;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0 && process.Graph.Mappings==3);
  assert(shadow.LogicalPtes[1].Flags==0 && shadow.LogicalPtes[2].GuestIpa==0x10002000);
  /* Full invalidation releases the remaining references. */
  memset(ptes,0,sizeof(ptes)); update.StartIndex=0; update.FirstPteVirtualAddress=0;
  update.AllocationOffsetInBytes=0; update.NumPageTableEntries=4;
  update.pPageTableEntries=ptes;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Mappings==0 && process.Graph.Leaf.AuxIpa==0);
  assert(process.Graph.Publications==7);
  free(shadow.LogicalPtes); free(shadow.ResidentPtes);
  puts("T7d native-leaf safety: PASS; scattered GPU residency remains unsupported");
  return 0;
}
