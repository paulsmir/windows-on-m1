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
#include "render_omitted_leaves.h"
#define VOID void
#define ADMISSION_ALLOCATION_OBJECT_MAGIC 42
typedef struct { struct { unsigned Magic; struct {uint64_t Size;} Description;} Object;} ADMISSION_ALLOCATION_HANDLE;
typedef struct {long long QuadPart;} QPC;
static QPC KeQueryPerformanceCounter(void *p) {(void)p;return (QPC){123};}
static void AdmissionRecordG4OmittedLeaves(void *a,const ADMISSION_OMITTED_LEAVES *r) {(void)a;assert(r->Version==1 && r->Bytes==2976);}
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
  ULONGLONG MappingGeneration, ProcessId;
  APPLE_AGX_GPUVA_G3_NODE Leaf, OtherLeaves[2];
  UINT Mappings, Publications;
} GRAPH;
typedef struct _SHADOW {
  struct _SHADOW *Next;
  ULONGLONG BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *PendingPtes, *LogicalPtes, *ResidentPtes;
} ADMISSION_G3_TABLE_SHADOW;
typedef struct { void *Adapter; ADMISSION_OMITTED_LEAVES OmittedLeaves; ULONGLONG SystemGroups[8], UnpublishedGroups[32]; } STATE;
typedef struct {
  GRAPH Graph;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  STATE *State;
  UINT CpuOnlyMappings, OsProcessId;
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
  assert(table==0x90000000 && index<3);
  APPLE_AGX_GPUVA_G3_NODE *leaf=index?&g->OtherLeaves[index-1]:&g->Leaf;
  return leaf->AuxIpa ? leaf : NULL;
}
static int AppleAgxGpuvaG3GraphUpdateLeafBacking(GRAPH *g, ULONGLONG table,
    UINT index, ULONGLONG ipa, bool write, UINT kind) {
  assert(table==0x90000000 && index<3);
  APPLE_AGX_GPUVA_G3_NODE *leaf=index?&g->OtherLeaves[index-1]:&g->Leaf;
  *leaf=(APPLE_AGX_GPUVA_G3_NODE){ipa,write,kind}; ++g->Publications; return 1;
}
static int fail_publication;
static int AppleAgxGpuvaG3GraphTryLeafBacking(GRAPH *g, ULONGLONG table,
    UINT index, ULONGLONG ipa, bool write, UINT kind, bool *unavailable) {
  *unavailable=false;
  if (fail_publication) {g->LastStatus=17;return 0;}
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
  ADMISSION_ALLOCATION_HANDLE allocation={.Object={42,{0x10000}}};
  update.hAllocation=&allocation;
  for (UINT i=0;i<4;++i) { ptes[i].Valid=1; ptes[i].PageAddress=0x10000+i; }
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0x10000000 && process.Graph.Leaf.Writable);
  assert(process.Graph.Mappings==4);

  /* Keep all four logical pages valid but replace the second PFN. */
  ptes[1].PageAddress=0x20001;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0 && process.Graph.Mappings==4);
  assert(state.UnpublishedGroups[0]==1);
  assert(state.OmittedLeaves.TotalEvents==1 && state.OmittedLeaves.TotalGroups==1);
  const ADMISSION_OMITTED_EVENT *event=&state.OmittedLeaves.Events[0];
  assert(event->Allocation==(uintptr_t)&allocation && event->AllocationBytes==0x10000);
  assert(event->UpdateVa==0 && event->UpdateBytes==0x4000 && event->OmittedGroups==1);
  assert(event->Bitmap[0]==1 && event->GroupCount==1 && event->Flags==3);
  for (UINT i=0;i<4;++i) {
    assert(shadow.LogicalPtes[i].GuestIpa==ptes[i].PageAddress*0x1000ULL);
    assert(shadow.LogicalPtes[i].Allocation==(ULONGLONG)(ULONG_PTR)&allocation);
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

  /* T7f: adjacency alone is insufficient: a contiguous quartet starting
   * one Windows page into a native page must not publish a rounded alias. */
  for (UINT i=0;i<4;++i) { ptes[i].Valid=1; ptes[i].PageAddress=0x30001+i; }
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Leaf.AuxIpa==0 && process.Graph.Mappings==4);
  for (UINT i=0;i<4;++i)
    assert(shadow.LogicalPtes[i].GuestIpa==ptes[i].PageAddress*0x1000ULL);
  memset(ptes,0,sizeof(ptes));
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(process.Graph.Mappings==0);
  free(shadow.LogicalPtes); free(shadow.ResidentPtes);

  /* Three groups with a published hole; ring wrap counts updates, while
   * failed publication contributes no event. First invalid PTE is covered. */
  memset(&shadow,0,sizeof(shadow));memset(&process,0,sizeof(process));memset(&state,0,sizeof(state));
  shadow.BrokerIpa=0x90000000;process.TableShadows=&shadow;process.State=&state;
  process.Graph.ProcessId=77;process.OsProcessId=123;
  DXGK_PTE many[12]={0};
  for(UINT i=0;i<12;i++){many[i].Valid=1;many[i].PageAddress=0x50000+i;}
  many[0].Valid=0;many[9].PageAddress=0x60000;
  update.NumPageTableEntries=12;update.pPageTableEntries=many;
  update.FirstPteVirtualAddress=0x90000;update.AllocationOffsetInBytes=0x10000;
  for(UINT pass=0;pass<10;pass++) {
    assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
    assert(state.OmittedLeaves.TotalEvents==pass+1 && state.OmittedLeaves.TotalGroups==2*(pass+1));
    event=&state.OmittedLeaves.Events[(state.OmittedLeaves.Next+7)%8];
    assert(event->Bitmap[0]==5 && event->GroupCount==3 && event->OmittedGroups==2);
    assert(event->FirstOmittedVa==0x90000 && event->LastOmittedVa==0x98000);
    assert(event->UpdateBytes==0xc000 && event->AllocationVa==0x80000);
    assert(event->ProcessId==77 && event->OsProcessId==123);
  }
  assert(state.OmittedLeaves.Count==8 && state.OmittedLeaves.Next==2);
  fail_publication=1;many[1].PageAddress=0x70000;
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)<0);
  assert(state.OmittedLeaves.TotalEvents==10 && shadow.LogicalPtes[1].GuestIpa==0x50001000);
  fail_publication=0;
  free(shadow.LogicalPtes);free(shadow.ResidentPtes);
  update.NumPageTableEntries=4;update.pPageTableEntries=ptes;update.FirstPteVirtualAddress=0;update.AllocationOffsetInBytes=0;

  /* CPU-only paging VAs must remain usable for copying scattered system
   * backing without acquiring GPU grants or activating a GPU shadow. */
  memset(&shadow,0,sizeof(shadow)); memset(&process,0,sizeof(process));
  shadow.BrokerIpa=0x90000000; process.TableShadows=&shadow;
  process.State=&state; process.CpuOnlyMappings=1;
  for (UINT i=0;i<4;++i) { ptes[i].Valid=1; ptes[i].PageAddress=0x40001+9*i; }
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  assert(!process.Graph.Leaf.AuxIpa && !process.Graph.Mappings);
  assert(!process.Graph.Publications && !shadow.ResidentPtes);
  assert(state.OmittedLeaves.TotalEvents==10);
  for (UINT i=0;i<4;++i) {
    assert(shadow.LogicalPtes[i].GuestIpa==ptes[i].PageAddress*0x1000ULL);
    assert(shadow.LogicalPtes[i].Allocation==(ULONGLONG)(ULONG_PTR)&allocation);
  }
  memset(ptes,0,sizeof(ptes));
  assert(AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure)==0);
  for (UINT i=0;i<4;++i) assert(!shadow.LogicalPtes[i].Flags);
  assert(!process.Graph.Mappings && !process.Graph.Publications);
  free(shadow.LogicalPtes);
  puts("T7d native-leaf safety: PASS; scattered GPU residency remains unsupported");
  return 0;
}
