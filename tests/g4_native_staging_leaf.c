/* RC60: actual AdmissionG3UpdateLeaf (+ its native-staging helpers when the
 * tree has them) and the actual native-staging Page/Describe functions, fed
 * with the UpdatePageTable shapes VidMm may legally issue for an
 * ExistingSysMem native-staging allocation. OS allocation, memory views, the
 * MDL and the graph/broker boundary are host stand-ins. This does not emulate
 * VidMm placement, broker grants, GPU execution or TLB completion.
 *
 * WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * EXP1981: an UpdatePageTable for a native-staging allocation returned
 * STATUS_INVALID_PARAMETER (Wom1DdiStatus slot 123) and the Checked wrapper
 * poisoned the whole process (Wom1G3PrivateAcquireFailure Reserved
 * 0x002605a4); every later ACQUIRE and ring map was refused. UpdatePageTable
 * may not fail. A page that is not the authenticated ring page must stay
 * unpublished (no native leaf), the update must succeed, and the refusal must
 * be named by the receipt; exact ring mappings (4 KiB, 64 KiB, partial) must
 * still publish native leaves. Failures print "FAIL <case>". */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apple_agx_gpuva_g3_translation.h"
#include "apple_agx_g3_native_cache.h"
#include "render_omitted_leaves.h"
#include "render_native_staging_receipt.h"
#define APPLE_AGX_NATIVE_STAGING 1
#define VOID void
#define ADMISSION_ALLOCATION_OBJECT_MAGIC 42u
typedef void *PVOID, *HANDLE;
typedef int NTSTATUS, BOOLEAN;
typedef unsigned UINT, ULONG;
typedef unsigned long long ULONGLONG, *PULONGLONG;
typedef uintptr_t ULONG_PTR, PFN_NUMBER;
typedef long LONG;
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xffffffffu
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
#define PAGE_SIZE 0x1000u
#define PAGE_SHIFT 12u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define ADMISSION_MEMORY_POOL_SEGMENT 3u
#define ADMISSION_G3_FRAME_SEGMENT(s) ((s) == 0u || (s) == 3u)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory(d,s,n) memcpy(d,s,n)
typedef struct {long long QuadPart;} QPC;
static QPC KeQueryPerformanceCounter(void *p) {(void)p;return (QPC){123};}

typedef struct _ADMISSION_CONTEXT {int Id;} ADMISSION_CONTEXT;
typedef struct { int unused; } ADMISSION_G3_PAGING_FAILURE;
typedef struct { void *Owner; } *PEPROCESS;
typedef struct { PFN_NUMBER *Pfn; } MDL, *PMDL;
#define MmGetMdlPfnArray(m) ((const PFN_NUMBER *)(m)->Pfn)
#include "native_types.inc"
typedef struct {
  struct { ULONG Magic; struct { ULONGLONG Size; } Description; } Object;
  PVOID NativeStagingBinding;
} ADMISSION_ALLOCATION_HANDLE;
typedef struct _DXGK_PTE {
  union {
    struct {
      ULONGLONG Valid : 1, Zero : 1, CacheCoherent : 1, ReadOnly : 1,
          NoExecute : 1, Segment : 5, LargePage : 1, PhysicalAdapterIndex : 6,
          PageTablePageSize : 2, SystemReserved0 : 1, Reserved : 44;
    };
    ULONGLONG Flags;
  };
  ULONGLONG PageAddress;
} DXGK_PTE;
typedef struct {
  struct { UINT Repeat : 1, InitialUpdate : 1, NotifyEviction : 1,
           Use64KBPages : 1, Reserved : 28; } Flags;
  UINT NumPageTableEntries, StartIndex;
  ULONGLONG FirstPteVirtualAddress, AllocationOffsetInBytes;
  HANDLE hAllocation;
  DXGK_PTE *pPageTableEntries;
} DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE;
typedef struct { ULONGLONG GuestIpaAddress, Bytes; } ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct { ULONGLONG AuxIpa; UINT Writable, Kind; } APPLE_AGX_GPUVA_G3_NODE;
enum { AppleAgxGpuvaG3SystemBacking, AppleAgxGpuvaG3LocalBacking,
       AdmissionG3PagingFailureLeafGraph };
#define GROUPS 2048u
typedef struct {
  UINT JobInFlight, LeaseToken, Uncertain, LastStatus;
  ULONGLONG MappingGeneration, ProcessId;
  APPLE_AGX_GPUVA_G3_NODE Leaves[GROUPS];
  UINT Mappings, Publications;
} GRAPH;
typedef struct _SHADOW {
  struct _SHADOW *Next;
  ULONGLONG BrokerIpa;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *PendingPtes, *LogicalPtes, *ResidentPtes;
} ADMISSION_G3_TABLE_SHADOW;
#include "history.inc"
typedef struct {
  ADMISSION_G3_LEAF_HISTORY LeafHistory[ADMISSION_G3_LEAF_RING];
  ULONG LeafHistoryNext;
  ADMISSION_CONTEXT *Adapter;
  ADMISSION_OMITTED_LEAVES OmittedLeaves;
  ADMISSION_NATIVE_PTE_REFUSAL NativePteRefusal;
  ULONGLONG SystemGroups[8], UnpublishedGroups[32];
} STATE;
typedef struct {
  GRAPH Graph;
  APPLE_AGX_G3_NATIVE_CACHE NativeReady;
  ADMISSION_G3_TABLE_SHADOW *TableShadows;
  STATE *State;
  UINT CpuOnlyMappings, OsProcessId;
} ADMISSION_G3_PROCESS;
#include "native_invalidate.inc"
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
  (void)a; (void)p; (void)u; (void)ptes; (void)first; (void)count; return 0;
}
static void AdmissionG3PoolPagingRefs(ADMISSION_CONTEXT *a,
    APPLE_AGX_GPUVA_G3_LOGICAL_PTE *old, APPLE_AGX_GPUVA_G3_LOGICAL_PTE *next,
    UINT first, UINT count) { (void)a;(void)old;(void)next;(void)first;(void)count; }
static void AdmissionG3CountLeaf64K(STATE *s,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *u) { (void)s;(void)u; }
static int AppleAgxGpuvaG3MappingAcquire(GRAPH *g, ULONGLONG ipa) {
  if (!ipa) abort(); ++g->Mappings; return 1;
}
static void AppleAgxGpuvaG3MappingRelease(GRAPH *g, ULONGLONG ipa) {
  if (!ipa || !g->Mappings) abort(); --g->Mappings;
}
static const APPLE_AGX_GPUVA_G3_NODE *AppleAgxGpuvaG3GraphLeaf(
    GRAPH *g, ULONGLONG table, UINT index) {
  if (table!=0x90000000 || index>=GROUPS) abort();
  return g->Leaves[index].AuxIpa ? &g->Leaves[index] : NULL;
}
static int AppleAgxGpuvaG3GraphUpdateLeafBacking(GRAPH *g, ULONGLONG table,
    UINT index, ULONGLONG ipa, bool write, UINT kind) {
  if (table!=0x90000000 || index>=GROUPS) abort();
  g->Leaves[index]=(APPLE_AGX_GPUVA_G3_NODE){ipa,write,kind}; ++g->Publications;
  return 1;
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
static void AdmissionRecordG4OmittedLeaves(ADMISSION_CONTEXT *a,
    const ADMISSION_OMITTED_LEAVES *r) {(void)a;(void)r;}
static unsigned staged;
static ADMISSION_NATIVE_PTE_REFUSAL last_staged;
static void AdmissionRecordG3NativePteRefusal(ADMISSION_CONTEXT *a,
    const ADMISSION_NATIVE_PTE_REFUSAL *r) {
  if (!a || r->Version != ADMISSION_NATIVE_PTE_REFUSAL_VERSION) abort();
  ++staged; last_staged=*r;
}
#include "native.inc"
#include "leaf.inc"

static unsigned failures;
#define EXPECT(c, label) do { if (!(c)) { printf("FAIL %s: %s (line %d)\n", \
    label, #c, __LINE__); ++failures; } } while (0)

/* Ring: four 64 KiB native chunks, deliberately not physically ordered. */
static PFN_NUMBER ring_pfn[64];
static MDL ring_mdl={ring_pfn};
static NATIVE_RING ring;
static NATIVE_BINDING binding;
static ADMISSION_ALLOCATION_HANDLE native_owner, ordinary_owner;
static ADMISSION_CONTEXT adapter;
static STATE state;
static ADMISSION_G3_TABLE_SHADOW shadow;
static ADMISSION_G3_PROCESS process;
static ADMISSION_G3_PAGING_FAILURE failure;
static DXGK_PTE ptes[64];
static DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE update;

static ULONGLONG ring_page(UINT allocation_page) {
  return ring_pfn[(binding.Offset >> 12) + allocation_page];
}

static void reset(void) {
  free(shadow.LogicalPtes); free(shadow.ResidentPtes);
  memset(&shadow,0,sizeof(shadow)); memset(&process,0,sizeof(process));
  memset(&state,0,sizeof(state)); memset(&update,0,sizeof(update));
  memset(ptes,0,sizeof(ptes)); staged=0;
  shadow.BrokerIpa=0x90000000; process.TableShadows=&shadow;
  process.State=&state; state.Adapter=&adapter;
  process.Graph.ProcessId=7; process.OsProcessId=10028;
  update.pPageTableEntries=ptes; update.hAllocation=&native_owner;
}

static NTSTATUS leaf(void) {
  return AdmissionG3UpdateLeaf(&process,shadow.BrokerIpa,&update,&adapter,&failure);
}

/* Map allocation pages [page, page+count) 4 KiB each at VA page*4 KiB. */
static void map_exact(UINT page, UINT count) {
  memset(ptes,0,sizeof(ptes));
  update.Flags.Repeat=0; update.Flags.Use64KBPages=0;
  update.StartIndex=page; update.NumPageTableEntries=count;
  update.FirstPteVirtualAddress=(ULONGLONG)page*0x1000;
  update.AllocationOffsetInBytes=(ULONGLONG)page*0x1000;
  for (UINT i=0;i<count;++i) { ptes[i].Valid=1; ptes[i].PageAddress=ring_page(page+i); }
}

static UINT native_leaves(UINT first_group, UINT groups) {
  UINT n=0;
  for (UINT g=first_group;g<first_group+groups;++g)
    if (process.Graph.Leaves[g].AuxIpa==ring_page(g*4u)<<12 &&
        process.Graph.Leaves[g].Kind==AppleAgxGpuvaG3SystemBacking) ++n;
  return n;
}

static void exact_mappings(void) {
  reset();
  map_exact(0,32);
  EXPECT(leaf()==STATUS_SUCCESS, "exact-4k status");
  EXPECT(native_leaves(0,8)==8, "exact-4k publishes every group");
  EXPECT(state.NativePteRefusal.Version==0 && !staged, "exact-4k no refusal");
  for (UINT i=0;i<32;++i)
    EXPECT(shadow.ResidentPtes[i].NativeIpa==ring_page(i)<<12, "exact-4k native identity");
  /* Partial update of the middle 8 pages retains the neighbours. */
  map_exact(8,8);
  EXPECT(leaf()==STATUS_SUCCESS, "partial status");
  EXPECT(native_leaves(0,8)==8, "partial keeps all groups native");
  /* VidMm invalidation (hAllocation NULL) of everything. */
  memset(ptes,0,sizeof(ptes)); update.hAllocation=NULL;
  update.StartIndex=0; update.NumPageTableEntries=32;
  update.FirstPteVirtualAddress=0; update.AllocationOffsetInBytes=0;
  EXPECT(leaf()==STATUS_SUCCESS, "unmap status");
  EXPECT(native_leaves(0,8)==0 && process.Graph.Mappings==0, "unmap retires");
  update.hAllocation=&native_owner;
  /* 64 KiB PTEs over the two native chunks. */
  memset(ptes,0,sizeof(ptes));
  update.Flags.Use64KBPages=1; update.StartIndex=0; update.NumPageTableEntries=2;
  ptes[0].Valid=1; ptes[0].PageAddress=ring_page(0);
  ptes[1].Valid=1; ptes[1].PageAddress=ring_page(16);
  EXPECT(leaf()==STATUS_SUCCESS, "64k status");
  EXPECT(native_leaves(0,8)==8, "64k publishes every group");
  EXPECT(state.NativePteRefusal.Version==0, "64k no refusal");
}

/* A dummy page repeated over the range (Flags.Repeat): legal VidMm input
 * that is not ring backing. Then the real backing returns. */
static void repeated_dummy_page(void) {
  reset();
  update.Flags.Repeat=1; update.StartIndex=0; update.NumPageTableEntries=32;
  ptes[0].Valid=1; ptes[0].PageAddress=0x12345;
  NTSTATUS status=leaf();
  EXPECT(status==STATUS_SUCCESS, "repeat-dummy status");
  if (status!=STATUS_SUCCESS) return;
  EXPECT(native_leaves(0,8)==0, "repeat-dummy publishes no native leaf");
  for (UINT g=0;g<8;++g) EXPECT(process.Graph.Leaves[g].AuxIpa==0, "repeat-dummy no leaf");
  for (UINT i=0;i<32;++i) {
    EXPECT(shadow.LogicalPtes[i].GuestIpa==0x12345000ULL, "repeat-dummy logical page");
    EXPECT(shadow.LogicalPtes[i].NativeIpa==0, "repeat-dummy no native identity");
  }
  const ADMISSION_NATIVE_PTE_REFUSAL *r=&state.NativePteRefusal;
  EXPECT(r->Version==1 && r->Bytes==sizeof(*r) && r->Bytes==184, "receipt header");
  EXPECT(r->Reason==ADMISSION_NATIVE_PTE_FOREIGN, "repeat-dummy reason");
  EXPECT(r->UpdateFlags==1 && r->InputIndex==0 && r->PageIndex==0, "repeat-dummy input");
  EXPECT(r->InputPageAddress==0x12345 && (r->InputPteFlags & 1), "repeat-dummy pte");
  EXPECT(r->PageIpa==0x12345000ULL && r->ExpectedIpa==ring_page(0)<<12, "repeat-dummy ipa");
  EXPECT(r->BindingOffset==0x10000 && r->BindingBytes==0x20000, "repeat-dummy binding");
  EXPECT(r->RingOffset==ADMISSION_NATIVE_PTE_NONE, "repeat-dummy not a ring page");
  EXPECT(r->Allocation==(ULONGLONG)(ULONG_PTR)&native_owner && r->PageOwner==r->Allocation, "repeat-dummy owner");
  EXPECT(r->OsProcessId==10028 && r->ProcessId==7 && r->Qpc==123, "repeat-dummy process");
  EXPECT(r->RefusedGroups==8 && r->RefusedUpdates==1, "repeat-dummy totals");
  EXPECT(staged==4 && last_staged.RefusedGroups==8, "repeat-dummy staged at 1,2,4,8");
  /* The real ring backing replaces the dummy page: native again. */
  map_exact(0,32);
  EXPECT(leaf()==STATUS_SUCCESS, "repeat-remap status");
  EXPECT(native_leaves(0,8)==8, "repeat-remap republishes");
  EXPECT(r->RefusedGroups==8 && r->Reason==ADMISSION_NATIVE_PTE_FOREIGN, "first refusal kept");
}

/* Ring pages one group away from their allocation offset: geometrically a
 * complete aligned quartet, but not this allocation's authenticated bytes. */
static void misplaced_ring_pages(void) {
  reset();
  map_exact(0,28);
  update.AllocationOffsetInBytes=0x4000;
  NTSTATUS status=leaf();
  EXPECT(status==STATUS_SUCCESS, "misplaced status");
  if (status!=STATUS_SUCCESS) return;
  EXPECT(native_leaves(0,7)==0, "misplaced publishes no native leaf");
  const ADMISSION_NATIVE_PTE_REFUSAL *r=&state.NativePteRefusal;
  EXPECT(r->Reason==ADMISSION_NATIVE_PTE_RING && r->PageIndex==0, "misplaced reason");
  EXPECT(r->PageAllocationOffset==0x4000 && r->ExpectedIpa==ring_page(4)<<12, "misplaced expected");
  EXPECT(r->RingOffset==0x10000, "misplaced ring offset names the actual page");
  EXPECT(r->RefusedGroups==7, "misplaced totals");
  /* Offsets past the binding: range refusal, still not a failure. */
  reset();
  map_exact(0,4);
  update.AllocationOffsetInBytes=0x20000;
  EXPECT(leaf()==STATUS_SUCCESS, "range status");
  EXPECT(state.NativePteRefusal.Reason==ADMISSION_NATIVE_PTE_RANGE &&
         state.NativePteRefusal.ExpectedIpa==ADMISSION_NATIVE_PTE_NONE, "range reason");
}

/* One foreign PFN omits only its own group. */
static void one_foreign_page(void) {
  reset();
  map_exact(0,32);
  ptes[5].PageAddress=0x55555;
  NTSTATUS status=leaf();
  EXPECT(status==STATUS_SUCCESS, "foreign-page status");
  if (status!=STATUS_SUCCESS) return;
  EXPECT(native_leaves(0,1)==1 && native_leaves(2,6)==6, "foreign-page others native");
  EXPECT(process.Graph.Leaves[1].AuxIpa==0, "foreign-page group omitted");
  EXPECT(shadow.LogicalPtes[5].GuestIpa==0x55555000ULL, "foreign-page logical page");
  const ADMISSION_NATIVE_PTE_REFUSAL *r=&state.NativePteRefusal;
  EXPECT(r->Reason==ADMISSION_NATIVE_PTE_FOREIGN && r->PageIndex==5 &&
         r->InputIndex==5 && r->InputPageAddress==0x55555, "foreign-page receipt");
  EXPECT(r->PageVa==0x5000 && r->RefusedGroups==1 && staged==1, "foreign-page totals");
  EXPECT(state.UnpublishedGroups[0]==1, "foreign-page unpublished tally");
}

/* A complete local-segment quartet for a native allocation is not ring
 * backing: logical-only, no failure. */
static void wrong_segment(void) {
  reset();
  update.StartIndex=0; update.NumPageTableEntries=4;
  for (UINT i=0;i<4;++i) { ptes[i].Valid=1; ptes[i].Segment=2; ptes[i].PageAddress=i; }
  NTSTATUS status=leaf();
  EXPECT(status==STATUS_SUCCESS, "segment status");
  if (status!=STATUS_SUCCESS) return;
  EXPECT(process.Graph.Leaves[0].AuxIpa==0, "segment publishes no leaf");
  EXPECT(state.NativePteRefusal.Reason==ADMISSION_NATIVE_PTE_SEGMENT &&
         state.NativePteRefusal.Segment==2, "segment reason");
}

/* Ordinary allocations and opaque handles keep the existing rule. */
static void other_owners(void) {
  reset();
  update.hAllocation=&ordinary_owner;
  map_exact(0,8);
  ptes[5].PageAddress=0x55555;
  EXPECT(leaf()==STATUS_SUCCESS, "ordinary status");
  EXPECT(process.Graph.Leaves[0].AuxIpa==ring_page(0)<<12 &&
         process.Graph.Leaves[1].AuxIpa==0, "ordinary geometry rule");
  EXPECT(state.NativePteRefusal.Version==0, "ordinary no native receipt");
  /* Only the identity prefix of an opaque handle may be read (ASan). */
  unsigned *opaque=malloc(sizeof(*opaque)); *opaque=0;
  reset(); update.hAllocation=opaque; map_exact(0,4);
  EXPECT(leaf()==STATUS_SUCCESS, "opaque status");
  EXPECT(state.NativePteRefusal.Version==0, "opaque no native receipt");
  free(opaque);
}

int main(void) {
  static const PFN_NUMBER chunks[4]={0x90000,0x90040,0x90100,0x90020};
  for (UINT c=0;c<4;++c) for (UINT i=0;i<16;++i) ring_pfn[c*16+i]=chunks[c]+i;
  ring.Mdl=&ring_mdl; ring.Bytes=0x40000;
  binding.Ring=&ring; binding.Offset=0x10000; binding.Bytes=0x20000;
  binding.Magic=NATIVE_BINDING_MAGIC;
  native_owner.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
  native_owner.Object.Description.Size=0x20000;
  native_owner.NativeStagingBinding=&binding;
  ordinary_owner=native_owner; ordinary_owner.NativeStagingBinding=NULL;
  exact_mappings();
  repeated_dummy_page();
  misplaced_ring_pages();
  one_foreign_page();
  wrong_segment();
  other_owners();
  reset();
  if (failures) { printf("native staging leaf: %u FAILED\n", failures); return 1; }
  puts("RC60 native staging leaf: PASS");
  return 0;
}
