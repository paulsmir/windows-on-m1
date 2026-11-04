"""rc107: canonical CPU maps survive VidMm's segment-0 republish (EXP1974).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1974 mapped a 128 MiB
CPU-visible pool allocation, VidMm then republished it in segment 0 with
every native leaf omitted (pool full), and the next submission referencing it
failed native proof (LeafAbsent) and removed the device. A held or newly
mapped CPU-visible pool allocation found outside the pool must be paged back
into the pool by a bounded repair, and only the unchanged complete native
proof may then admit the submission. The KMD placement census must never
count as a readiness proof.

The replay links the production UMD bodies (native_held_ready, the repair,
eager trim, residency_request, touch_device, the CPU pool map proof) with the
production KMD query and batch handlers over the real shared G3 graph and
native proof cache. Only VidMm's placement decisions are modelled: a page-in
lands in the pool when the pool has room and in segment 0 otherwise, and an
eager eviction is processed at the next scheduling of the device.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
KMD = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'
UMD = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'
UMD_INCLUDE = ROOT / 'drivers/apple-agx/render-admission/umd/include'


def umd_function(text, name):
    m = re.search(r'(?m)^(?:static )?[A-Za-z_][A-Za-z0-9_ *]*?\b' + name +
                  r'\([^;{]*\)\s*\{', text)
    if not m:
        raise AssertionError('missing UMD function ' + name)
    start = text.index('{', m.start())
    depth, end = 1, start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]


KMD_SIDE = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_g3_copy_query_receipt.h"
#include "apple_agx_gpuva_g3_graph.h"
#include "apple_agx_gpuva_g3_translation.h"
#include "apple_agx_g3_native_cache.h"
typedef unsigned UINT, ULONG; typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR; typedef void *HANDLE; typedef int NTSTATUS;
#define PASSIVE_LEVEL 0
#define ADMISSION_MEMORY_POOL_SEGMENT 3u
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define STATUS_INVALID_HANDLE -2
#define STATUS_INVALID_DEVICE_STATE -3
#define MAXULONG 0xffffffffu
#define TRUE 1
#define DXGK_HANDLE_ALLOCATION 1
#define ADMISSION_OPEN_ALLOCATION_MAGIC 2
#define ADMISSION_ALLOCATION_OBJECT_MAGIC 3
#define CONTAINING_RECORD(p,t,f) ((t*)((char*)(p)-offsetof(t,f)))
#define RtlCopyMemory memcpy
#define RtlZeroMemory(p,n) memset(p,0,n)
static int locked, refs, irql;
static int KeGetCurrentIrql(void) { return irql; }
static void ExAcquireFastMutex(int *m) { (void)m; assert(!locked); locked=1; }
static void ExReleaseFastMutex(int *m) { (void)m; assert(locked); locked=0; }
typedef struct { struct { ULONGLONG Size; } Description; UINT Magic; } ALLOCATION;
typedef struct { ALLOCATION Object; } ADMISSION_ALLOCATION_HANDLE;
typedef struct { struct { void *Adapter; } Object; } ADMISSION_DEVICE;
typedef struct { UINT Magic, RuntimeAllocation, ReadOnly; ADMISSION_DEVICE *Device; ALLOCATION *Allocation; } ADMISSION_OPEN_ALLOCATION;
typedef struct _CTX { struct _CTX *GpuvaG3NextContext; struct { void *Device; } Object;
 UINT Win32Transport,GpuvaG3Closing,GpuvaG3Poisoned; ULONGLONG GpuvaG3RootIpa; } ADMISSION_RENDER_CONTEXT;
typedef struct _SHADOW { struct _SHADOW *NextBroker; ULONGLONG BrokerIpa;
 APPLE_AGX_GPUVA_G3_LOGICAL_PTE *ResidentPtes; } ADMISSION_G3_TABLE_SHADOW;
typedef struct { APPLE_AGX_GPUVA_G3_GRAPH Graph; APPLE_AGX_G3_NATIVE_CACHE NativeReady; UINT Poisoned,CpuOnlyMappings;
 ADMISSION_RENDER_CONTEXT *Contexts; ADMISSION_G3_TABLE_SHADOW *TableShadowBrokerBuckets[256]; } ADMISSION_G3_PROCESS;
typedef struct { int Lock; ADMISSION_G3_PROCESS *Process; } ADMISSION_G3_STATE;
typedef struct { UINT hObject,Type; struct { UINT DeviceSpecific; } Flags; } DXGKARGCB_GETHANDLEDATA;
typedef struct { UINT Type; void *ReleaseHandle; } DXGKARGCB_RELEASEHANDLEDATA;
typedef struct { UINT Started; ADMISSION_G3_STATE *GpuvaG3State; int ObjectAdapter;
 struct { void *(*DxgkCbAcquireHandleData)(DXGKARGCB_GETHANDLEDATA*,void**);
 void (*DxgkCbReleaseHandleData)(DXGKARGCB_RELEASEHANDLEDATA); } Interface; } ADMISSION_CONTEXT;
typedef struct { void *pPrivateDriverData; UINT PrivateDriverDataSize; struct { UINT Value; } Flags;
 HANDLE hKmdProcessHandle,hContext,hDevice; } DXGKARG_ESCAPE;
#define KMD_ALLOCATIONS 3u
static ADMISSION_OPEN_ALLOCATION opened[KMD_ALLOCATIONS];
static void *acquire(DXGKARGCB_GETHANDLEDATA *q, void **r) {
 unsigned i;
 assert(q->Type==DXGK_HANDLE_ALLOCATION && q->Flags.DeviceSpecific);
 for(i=0;i<KMD_ALLOCATIONS;++i) if(q->hObject==opened[i].RuntimeAllocation) {
  ++refs; *r=&opened[i]; return &opened[i];
 }
 return NULL;
}
static void release(DXGKARGCB_RELEASEHANDLEDATA r) { assert(r.ReleaseHandle && refs==1); --refs; }
static ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(ADMISSION_G3_STATE *s,HANDLE h) {
 assert(locked); return h==s->Process ? s->Process : NULL;
}
@@KMD@@
/* One leaf table: allocation i spans [VA0 + i*64 KiB, +64 KiB): 16 PTEs,
 * four native 16-KiB groups. Pool pages are contiguous and aligned; VidMm's
 * segment-0 pages are scattered 4 KiB system pages (no native leaf). */
#define VA0 0x1101000000ULL
#define POOL_IPA 0x900000000ULL
#define SYSTEM_IPA 0xa00000000ULL
static APPLE_AGX_GPUVA_G3_NODE root,middle,table,re,me,leaves[KMD_ALLOCATIONS*4u];
static APPLE_AGX_GPUVA_G3_NODE *rs[8],*ms[2048],*ls[2048];
static APPLE_AGX_GPUVA_G3_LOGICAL_PTE ptes[8192];
static ADMISSION_G3_PROCESS p; static ADMISSION_G3_STATE state;
static ADMISSION_CONTEXT a; static ADMISSION_RENDER_CONTEXT c; static ADMISSION_DEVICE d;
static ADMISSION_ALLOCATION_HANDLE allocation[KMD_ALLOCATIONS];
static ADMISSION_G3_TABLE_SHADOW shadow;
unsigned kmd_escapes, kmd_batches, kmd_placements;
void kmd_place(unsigned i, unsigned where);
void kmd_init(void) {
 const ULONGLONG tableIpa=0x920004000ULL;
 unsigned i;
 memset(&p,0,sizeof(p));memset(&state,0,sizeof(state));memset(&a,0,sizeof(a));
 memset(&c,0,sizeof(c));memset(&d,0,sizeof(d));memset(&shadow,0,sizeof(shadow));
 memset(ptes,0,sizeof(ptes));memset(ls,0,sizeof(ls));
 state.Process=&p;
 a.Started=1; a.GpuvaG3State=&state; a.Interface.DxgkCbAcquireHandleData=acquire;
 a.Interface.DxgkCbReleaseHandleData=release; d.Object.Adapter=&a.ObjectAdapter;
 c.Object.Device=&d.Object;c.Win32Transport=1;c.GpuvaG3RootIpa=0x920000000ULL;p.Contexts=&c;
 p.Graph.Created=1;p.Graph.RootIpa=c.GpuvaG3RootIpa;p.Graph.RootTable=&root;
 root.Slots=rs;middle.Slots=ms;table.Slots=ls;re.ChildTable=&middle;me.ChildTable=&table;me.AuxIpa=tableIpa;
 rs[(VA0>>36)&7]=&re;ms[(VA0>>25)&2047]=&me;
 shadow.BrokerIpa=tableIpa;shadow.ResidentPtes=ptes;
 p.TableShadowBrokerBuckets[((tableIpa>>14)^(tableIpa>>22)^(tableIpa>>30))&255]=&shadow;
 for(i=0;i<KMD_ALLOCATIONS;++i) {
  allocation[i].Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
  allocation[i].Object.Description.Size=65536;
  opened[i].Magic=ADMISSION_OPEN_ALLOCATION_MAGIC;opened[i].RuntimeAllocation=0x41u+i;
  opened[i].ReadOnly=0;opened[i].Device=&d;opened[i].Allocation=&allocation[i].Object;
  kmd_place(i,3u);
 }
 kmd_escapes=kmd_batches=kmd_placements=0;
}
/* VidMm's UpdatePageTable outcome for allocation i, as the paging path would
 * project it: 3 = pool (native leaves), 0 = segment-0 system backing (all
 * native groups omitted, EXP1974 Wom1G4OmittedLeaves), ~0u = not resident.
 * The paging path invalidates the allocation's proof hint on every change. */
void kmd_place(unsigned i, unsigned where) {
 const ULONGLONG va=VA0+(ULONGLONG)i*65536u;
 unsigned j;
 for(j=0;j<16;++j) {
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte=&ptes[((va>>12)&8191)+j];
  memset(pte,0,sizeof(*pte));
  if(where==~0u) continue;
  pte->Flags=APPLE_AGX_GPUVA_G3_VALID|APPLE_AGX_GPUVA_G3_WRITE;
  pte->SegmentId=where;
  pte->GuestIpa=where==3u ? POOL_IPA+(ULONGLONG)i*65536u+j*4096u
                          : SYSTEM_IPA+(ULONGLONG)i*0x100000u+j*12288u;
  pte->NativeIpa=where==3u ? pte->GuestIpa : 0u;
  pte->Allocation=(ULONG_PTR)&allocation[i];pte->AllocationOffset=j*4096u;
 }
 for(j=0;j<4;++j) {
  APPLE_AGX_GPUVA_G3_NODE *leaf=&leaves[i*4u+j];
  memset(leaf,0,sizeof(*leaf));
  leaf->AuxIpa=POOL_IPA+(ULONGLONG)i*65536u+j*16384u;leaf->Writable=1;
  ls[((va>>14)&2047)+j]=where==3u ? leaf : NULL;
 }
 AppleAgxG3NativeCacheInvalidate(&p.NativeReady,(ULONGLONG)(ULONG_PTR)&allocation[i]);
 ++kmd_placements;
}
void kmd_foreign(unsigned i) { ptes[((VA0+(ULONGLONG)i*65536u)>>12)&8191].AllocationOffset+=4096u; }
/* Pool PTEs whose native leaves are not (yet) published. */
void kmd_withdraw(unsigned i) {
 unsigned j;
 for(j=0;j<4;++j) ls[(((VA0+(ULONGLONG)i*65536u)>>14)&2047)+j]=NULL;
 AppleAgxG3NativeCacheInvalidate(&p.NativeReady,(ULONGLONG)(ULONG_PTR)&allocation[i]);
}
int kmd_escape(void *data, unsigned size) {
 DXGKARG_ESCAPE e={data,size,{0},&p,&c,&d};
 int status;
 assert(!locked && !refs && size>=4);
 ++kmd_escapes;
 if(*(const UINT *)data==APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAGIC) {
  ++kmd_batches;
  status=AdmissionGpuvaG3NativeQueryBatchEscape(&a,&e);
 } else {
  status=AdmissionGpuvaG3NativeQueryEscape(&a,&e);
 }
 assert(!locked && !refs);
 return status;
}
'''

UMD_SIDE = r'''
#define watch_slots(...) ((void)0)
#define APPLE_AGX_NATIVE_STAGING 1
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_g3_copy_query_receipt.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "apple_agx_g4_submit.h"
#pragma clang diagnostic pop
#include "umd_residency_trim.h"
#include "umd_cpu_pool_rehome.h"
using UINT=unsigned;using ULONGLONG=unsigned long long;using HRESULT=int;using LONG=int;
using D3DKMT_HANDLE=unsigned;using HANDLE=void*;using BOOL=int;using BYTE=unsigned char;
using SIZE_T=size_t;using PVOID=void*;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_PENDING ((HRESULT)0x8000000Au)
#define E_OUTOFMEMORY ((HRESULT)0x8007000Eu)
#define E_FAIL ((HRESULT)0x80004005u)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define ZeroMemory(p,n) memset((p),0,(n))
#define HEAP_ZERO_MEMORY 8u
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define ADMISSION_UMD_ADAPTER_MAGIC 0x66u
#define ADMISSION_UMD_IN_FLIGHT_SETS 4u
#define AppleAgxWin32BufferGpuWrite 8u
#define SLOTS 5u
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) SLOTS
extern "C" {
void kmd_init(void); void kmd_place(unsigned,unsigned); void kmd_foreign(unsigned);
void kmd_withdraw(unsigned);
int kmd_escape(void*,unsigned);
extern unsigned kmd_escapes, kmd_batches, kmd_placements;
}
struct ADMISSION_UMD_SCREEN_BUFFER {
 uint64_t Token; BOOL Active,Transition,CopyHeld,Direct,Mapped,Resident,Borrowed,
 KmtResidencyHeld,HeapStaging,SystemDirect,WrittenPrimary; UINT SourceHolds,SubmissionHolds,Flags,ClassId;
 D3DKMT_HANDLE KernelAllocation,StagingAllocation; BYTE *PrivateStaging,*NativeStagingAddress;
 uint64_t CanonicalGpuVa,Bytes,LastUseFence,TrimRecency,NativeReadyToken; void *LockedBase;
};
struct D3DDDI_MAKERESIDENT { struct { UINT CantTrimFurther, MustSucceed; } Flags; HANDLE hPagingQueue;
 UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; const UINT *PriorityList;
 ULONGLONG PagingFenceValue; ULONGLONG NumBytesToTrim; };
struct D3DDDICB_EVICT { struct { UINT EvictOnlyIfNecessary; } Flags; UINT NumAllocations;
 const D3DKMT_HANDLE *AllocationList; ULONGLONG NumBytesToTrim; };
struct D3DDDICB_ESCAPE { HANDLE hDevice,hContext; struct { UINT Value; } Flags;
 void *pPrivateDriverData; UINT PrivateDriverDataSize; };
struct D3DDDICB_SUBMITCOMMAND { UINT NumPrimaries; D3DKMT_HANDLE WrittenPrimaries[16];
 uint64_t Commands; UINT CommandLength,BroadcastContextCount;
 HANDLE BroadcastContext[1]; void *pPrivateDriverData; UINT PrivateDriverDataSize,RenderCBSequence; };
struct CALLBACKS {
 HRESULT (*pfnMakeResidentCb)(HANDLE,D3DDDI_MAKERESIDENT*);
 HRESULT (*pfnEvictCb)(HANDLE,const D3DDDICB_EVICT*);
 HRESULT (*pfnEscapeCb)(HANDLE,D3DDDICB_ESCAPE*);
 HRESULT (*pfnSubmitCommandCb)(HANDLE,D3DDDICB_SUBMITCOMMAND*);
 void (*pfnSignalSynchronizationObjectFromGpu2Cb)();
};
struct ADAPTER { UINT Magic; struct { HANDLE handle; } RuntimeAdapter; };
struct ADMISSION_UMD_DEVICE {
 ADAPTER *Adapter; struct { HANDLE handle; } RuntimeDevice; HANDLE KernelContext,PagingQueue;
 D3DKMT_HANDLE RenderSyncObject; CALLBACKS *KernelCallbacks; int ScreenBufferLock;
 ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[SLOTS]; uint64_t NextRenderFence; UINT RenderCbSequence;
 BOOL DrawTerminal,ScreenClosing,BorrowedContext,DirectCpuPool; HRESULT GpuvaFailure;
};
static int locks;
static void AcquireSRWLockShared(int *l) {assert(*l>=0);++*l;++locks;}
static void ReleaseSRWLockShared(int *l) {assert(*l>0);--*l;--locks;}
static void AcquireSRWLockExclusive(int *l) {assert(*l==0);*l=-1;++locks;}
static void ReleaseSRWLockExclusive(int *l) {assert(*l==-1);*l=0;--locks;}
static UINT InterlockedIncrement(UINT *v) {return ++*v;}
static void *GetProcessHeap() {return nullptr;}
static void *HeapAlloc(void *,unsigned,size_t n) {return calloc(1,n);}
static void HeapFree(void *,unsigned,void *q) {free(q);}
static unsigned GetEnvironmentVariableA(const char *name,char *out,unsigned size) {
 const char *v=getenv(name);if(!v)return 0;unsigned n=(unsigned)strlen(v);
 if(n>=size)return n+1;memcpy(out,v,n+1);return n;
}
static unsigned sleeps;
static void Sleep(unsigned) {++sleeps;}
static bool AdmissionUmdMeasureEnabled() {return false;}
static unsigned long long AdmissionUmdWatchVa() {return 0;}
static unsigned rehome_receipts; static HRESULT last_rehome_hr; static UINT last_rehome[11];
static void AdmissionUmdDiagnostic(const char *name,HRESULT hr,const UINT *v,size_t n) {
 if(!strcmp(name,"measure-cpu-pool-rehome")) {
  assert(n==11);++rehome_receipts;last_rehome_hr=hr;memcpy(last_rehome,v,sizeof(last_rehome));
 }
}
static void va_record(const void *,UINT,ULONGLONG,ULONGLONG,ULONGLONG,ULONGLONG,HRESULT) {}
static unsigned evicts; static uint64_t render_waited; static unsigned evicts_before_wait;
static int wait_object(ADMISSION_UMD_DEVICE *,D3DKMT_HANDLE object,uint64_t fence) {
 assert(!locks);if(fence==10u) {render_waited=fence;evicts_before_wait=evicts;}return object!=0;
}
static int wait_paging_at(ADMISSION_UMD_DEVICE *,uint64_t fence,UINT,uint64_t) {assert(!locks);return fence!=0;}
static int signal_render(ADMISSION_UMD_DEVICE *,uint64_t) {return 1;}

/* ---- VidMm placement model -------------------------------------------- */
enum { PLACE_POOL=3u, PLACE_SYSTEM=0u, PLACE_NONE=~0u };
static const D3DKMT_HANDLE handle_of[3]={0x41u,0x42u,0x43u};
static unsigned where[3], refcount[3], pending_evict[3];
static uint64_t pool_free;           /* free bytes in segment 3 */
static bool stuck_eviction;          /* VidMm never processes an eviction */
static bool keep_demoting;           /* someone else refills the pool */
static bool leaves_withheld;         /* pool PTEs whose native leaves never publish */
static unsigned makes, eager_evicts, lazy_evicts, touches, jobs;
static unsigned index_of(D3DKMT_HANDLE h) {for(unsigned i=0;i<3;++i) if(handle_of[i]==h) return i; assert(!"handle");return 0;}
static void place(unsigned i,unsigned w) {
 if(where[i]==PLACE_POOL && w!=PLACE_POOL) pool_free+=65536;
 if(w==PLACE_POOL && where[i]!=PLACE_POOL) {assert(pool_free>=65536);pool_free-=65536;}
 where[i]=w;kmd_place(i,w);
 if(w==PLACE_POOL && leaves_withheld) kmd_withdraw(i);
}
static HRESULT make_cb(HANDLE,D3DDDI_MAKERESIDENT *r) {
 assert(!locks && r->NumAllocations && !r->Flags.MustSucceed);
 for(UINT k=0;k<r->NumAllocations;++k) {
  unsigned i=index_of(r->AllocationList[k]);++makes;++refcount[i];
  pending_evict[i]=0;                       /* a queued eviction is cancelled */
  if(where[i]==PLACE_NONE) place(i,pool_free>=65536 && !keep_demoting ? PLACE_POOL : PLACE_SYSTEM);
 }
 r->PagingFenceValue=77;return E_PENDING;
}
static HRESULT evict_cb(HANDLE,const D3DDDICB_EVICT *e) {
 assert(!locks && e->NumAllocations);
 for(UINT k=0;k<e->NumAllocations;++k) {
  unsigned i=index_of(e->AllocationList[k]);++evicts;
  assert(refcount[i]);--refcount[i];
  if(e->Flags.EvictOnlyIfNecessary) ++lazy_evicts;
  else {++eager_evicts;if(!refcount[i]) pending_evict[i]=1;}
 }
 return S_OK;
}
static HRESULT escape_cb(HANDLE adapter,D3DDDICB_ESCAPE *e) {
 assert(!locks && adapter==(HANDLE)9 && e->hDevice==(HANDLE)3 && e->hContext==(HANDLE)7);
 return kmd_escape(e->pPrivateDriverData,e->PrivateDriverDataSize)==0 ? S_OK : E_FAIL;
}
static HRESULT submit_cb(HANDLE,D3DDDICB_SUBMITCOMMAND *r) {
 assert(!locks);
 auto *t=(const APPLE_AGX_G4_TOUCH *)r->pPrivateDriverData;
 if(t && r->PrivateDriverDataSize==sizeof(*t) && t->Magic==APPLE_AGX_G4_TOUCH_MAGIC) {
  ++touches;
  /* Scheduling the device lets VidMm run its queued page-outs. */
  for(unsigned i=0;i<3 && !stuck_eviction;++i)
   if(pending_evict[i]) {pending_evict[i]=0;place(i,PLACE_NONE);}
  return S_OK;
 }
 ++jobs;return S_OK;
}
static void signal_cb() {}
/* g4 envelope, staging and completion have their own replays. */
static int mark_written(ADMISSION_UMD_DEVICE *,const uint64_t *,unsigned) {return 1;}
static int transfer_held(ADMISSION_UMD_DEVICE *,bool) {return 1;}
static int g4_preflight(ADMISSION_UMD_DEVICE *,uint64_t,uint32_t,const void *,uint32_t) {return 1;}
static int finish_submission(ADMISSION_UMD_DEVICE *,uint64_t n,UINT,uint64_t *f) {*f=n;return 1;}
#define D3DDDI_MAX_WRITTEN_PRIMARIES 16u
#define ADMISSION_UMD_WRITTEN_LIMIT 128u
static void AdmissionUmdScreenCpuAccessSnapshot(ADMISSION_UMD_DEVICE *,HRESULT,UINT) {}
static void AdmissionUmdVaRecordDeallocate(const void *,ULONGLONG,ULONGLONG,ULONGLONG,HRESULT) {}
@@UMD@@
static CALLBACKS cbks={make_cb,evict_cb,escape_cb,submit_cb,signal_cb};
static ADAPTER adapter={ADMISSION_UMD_ADAPTER_MAGIC,{(HANDLE)9}};
static BYTE cpu_bytes[65536];
/* Slot 0: command buffer. 1: the 64-KiB CPU-visible pool canonical upload
 * heap S, persistently mapped (EXP1974 0x400035c0). 2: a held GPU-only
 * resource. 3: an idle GPU-only resource. 4: an inactive slot. */
static void init(ADMISSION_UMD_DEVICE &d) {
 d={};d.Adapter=&adapter;d.KernelCallbacks=&cbks;d.KernelContext=(HANDLE)7;
 d.RuntimeDevice.handle=(HANDLE)3;d.RenderSyncObject=8;d.NextRenderFence=20;d.PagingQueue=(HANDLE)5;
 d.DirectCpuPool=TRUE;
 kmd_init();
 for(unsigned i=0;i<3;++i) {where[i]=PLACE_POOL;refcount[i]=1;pending_evict[i]=0;}
 pool_free=0;stuck_eviction=keep_demoting=leaves_withheld=false;
 makes=evicts=eager_evicts=lazy_evicts=touches=jobs=sleeps=rehome_receipts=0;
 render_waited=0;evicts_before_wait=~0u;
 auto &cmd=d.ScreenBuffers[0];cmd.Active=TRUE;cmd.Token=100;cmd.CanonicalGpuVa=0x10000;
 cmd.Bytes=65536;cmd.KernelAllocation=0x99u;cmd.Resident=TRUE;
 for(unsigned i=0;i<3;++i) {
  auto &s=d.ScreenBuffers[i+1];s.Active=TRUE;s.Token=101+i;s.KernelAllocation=handle_of[i];
  s.CanonicalGpuVa=0x1101000000ULL+i*65536ULL;s.Bytes=65536;s.Resident=TRUE;
  s.Flags=AppleAgxWin32BufferGpuWrite;s.LastUseFence=10;
 }
 auto &heap=d.ScreenBuffers[1];heap.SystemDirect=TRUE;heap.Mapped=TRUE;heap.LockedBase=cpu_bytes;
 /* allocation index 0 is S, 1 the held GPU-only, 2 the idle GPU-only */
}
static void hold(ADMISSION_UMD_DEVICE &d) {
 for(unsigned i=0;i<3;++i) {auto &s=d.ScreenBuffers[i];s.CopyHeld=TRUE;s.SubmissionHolds=1;}
 d.ScreenBuffers[3].CopyHeld=FALSE;d.ScreenBuffers[3].SubmissionHolds=0;
}
static void release(ADMISSION_UMD_DEVICE &d) {
 for(unsigned i=0;i<SLOTS;++i) {d.ScreenBuffers[i].CopyHeld=FALSE;d.ScreenBuffers[i].SubmissionHolds=0;}
}
int main() {
 ADMISSION_UMD_DEVICE d; uint64_t fence=0; unsigned private_data=0;
 (void)fence;(void)private_data;
@@CASES@@
 return 0;
}
'''


class CpuPoolRehome(unittest.TestCase):
    def build(self, cases, umd_text=None, env=None):
        kmd = KMD.read_text()
        kmd_funcs = '\n'.join(body(kmd, n) for n in (
            'AdmissionG3CopyPte', 'AdmissionGpuvaG3NativeQueryEscape',
            'AdmissionGpuvaG3NativeQueryBatchEscape'))
        umd = umd_text if umd_text is not None else UMD.read_text()
        names = ('find_slot', 'trim_residency_with', 'trim_residency',
                 'residency_request', 'touch_device', 'placement_query',
                 'evict_now', 'rehome_cpu_pool', 'native_held_ready', 'submit',
                 'AdmissionUmdGpuvaValidateCpuPoolMap')
        umd_funcs = '\n'.join(umd_function(umd, n) for n in names
                              if n in umd or n == 'find_slot')
        with tempfile.TemporaryDirectory(prefix='rc107-rehome-') as tmp:
            tmp = Path(tmp)
            (tmp / 'kmd.c').write_text(KMD_SIDE.replace('@@KMD@@', kmd_funcs))
            (tmp / 'umd.cpp').write_text(UMD_SIDE.replace('@@UMD@@', umd_funcs)
                                         .replace('@@CASES@@', cases))
            flags = ['-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                     '-ftrivial-auto-var-init=pattern', '-I', str(SHARED / 'include')]
            objs = []
            for src, lang in (('kmd.c', 'c'), ('umd.cpp', 'cpp')):
                obj = tmp / (src + '.o')
                if lang == 'c':
                    cmd = [os.environ.get('CC', 'clang'), '-std=c11'] + flags
                else:
                    cmd = [os.environ.get('CXX', 'clang++'), '-std=c++17', '-include',
                           str(ROOT / 'tests/d3d12_wait_disabled_fixture.h'),
                           '-I', str(UMD_INCLUDE)] + flags + ['-Wno-unused-function']
                built = subprocess.run(cmd + ['-c', str(tmp / src), '-o', str(obj)],
                                       capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                objs.append(str(obj))
            for name in ('apple_agx_gpuva_g3_graph.c', 'apple_agx_gpuva_g3_translation.c',
                         'apple_agx_gpuva_broker_v5_client.c'):
                obj = tmp / (name + '.o')
                built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11'] + flags +
                                       ['-c', str(SHARED / 'src' / name), '-o', str(obj)],
                                       capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                objs.append(str(obj))
            exe = tmp / 'replay'
            built = subprocess.run([os.environ.get('CXX', 'clang++'),
                                    '-fsanitize=address,undefined'] + objs + ['-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run_env = dict(os.environ)
            run_env.pop('APPLE_AGX_CPU_POOL_REHOME', None)
            run_env.update(env or {})
            run = subprocess.run([str(exe)], capture_output=True, text=True,
                                 timeout=60, env=run_env)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_submission_pages_demoted_mapped_heap_back_into_pool(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1974: map, then
        VidMm republishes the mapped pool heap in segment 0 (all native leaves
        omitted), then submit. Before rc107 the submission was refused with
        LeafAbsent (device removed). It must page that heap back into the pool
        after freeing idle residency, with eager page-outs only, keep the CPU
        pointer, and admit the job only through a new complete native proof.
        """
        self.build(r'''
 init(d);
 /* Map succeeded with the heap in the pool; then the pool filled and
  * VidMm moved the still-resident, still-locked heap to segment 0. */
 place(0,PLACE_SYSTEM);assert(pool_free==65536);
 /* Another tenant consumed the room VidMm freed by moving it. */
 pool_free=0;
 hold(d);
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1);
 /* scheduling touch, one census poll, the post-page-in touch, the job */
 assert(jobs==1 && touches==3 && fence==24 && !d.DrawTerminal && d.GpuvaFailure==S_OK);
 assert(where[0]==PLACE_POOL && where[2]==PLACE_NONE && where[1]==PLACE_POOL);
 /* idle resource and the heap were evicted now; no lazy eviction. */
 assert(eager_evicts==2 && !lazy_evicts && makes==1 && refcount[0]==1 && !refcount[2]);
 auto &heap=d.ScreenBuffers[1];
 assert(heap.Resident && heap.Mapped && heap.LockedBase==cpu_bytes);
 assert(!d.ScreenBuffers[3].Resident && d.ScreenBuffers[2].Resident);
 assert(rehome_receipts==1 && last_rehome_hr==S_OK);
 assert(last_rehome[0]==ADMISSION_UMD_REHOME_SUBMIT && last_rehome[1]==0);
 assert(last_rehome[2]==APPLE_AGX_G3_NATIVE_QUERY_NOT_POOL && last_rehome[3]==0 && last_rehome[4]==16);
 assert(last_rehome[5]==1 && (last_rehome[7]&0x100u) && last_rehome[8]==0 && last_rehome[9]==16 && !last_rehome[10]);
 assert(!locks && !render_waited);
 /* An older in-flight submission still holds the heap: its render fence
  * must complete before the heap is evicted. */
 init(d);place(0,PLACE_SYSTEM);pool_free=0;hold(d);d.ScreenBuffers[1].SubmissionHolds=2;
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1 && jobs==1);
 assert(render_waited==10 && evicts_before_wait==0 && where[0]==PLACE_POOL);
''')

    def test_pre_rc107_source_refuses_the_exp1974_replay(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? RED control: the
        same replay against the kill switch reproduces EXP1974's refusal
        (E_OUTOFMEMORY from LeafAbsent, no job, no eviction)."""
        self.build(r'''
 init(d);place(0,PLACE_SYSTEM);pool_free=0;hold(d);
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==0);
 assert(!jobs && d.GpuvaFailure==E_OUTOFMEMORY && !d.DrawTerminal);
 assert(!evicts && !makes && where[0]==PLACE_SYSTEM && !rehome_receipts);
''', env={'APPLE_AGX_CPU_POOL_REHOME': '0'})

    def test_full_pool_stays_a_bounded_clean_refusal(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? With no idle
        residency anywhere, VidMm demotes the page-in again. The repair must
        stop after ADMISSION_UMD_REHOME_ROUNDS, never weaken proof, keep the
        heap's residency reference and report clean pressure, not terminal
        uncertainty."""
        self.build(r'''
 init(d);place(0,PLACE_SYSTEM);pool_free=0;keep_demoting=true;
 d.ScreenBuffers[3].Active=FALSE;  /* nothing idle to trim */
 hold(d);
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==0);
 assert(!jobs && d.GpuvaFailure==E_OUTOFMEMORY && !d.DrawTerminal);
 assert(eager_evicts==ADMISSION_UMD_REHOME_ROUNDS && makes==ADMISSION_UMD_REHOME_ROUNDS);
 assert(where[0]==PLACE_SYSTEM && refcount[0]==1 && d.ScreenBuffers[1].Resident);
 assert(rehome_receipts==ADMISSION_UMD_REHOME_ROUNDS && last_rehome_hr==E_OUTOFMEMORY);
 assert(last_rehome[1]==1 && last_rehome[8]==APPLE_AGX_G3_NATIVE_QUERY_NOT_POOL);
 /* The next submission retries from a consistent state. */
 release(d);keep_demoting=false;pool_free=65536;hold(d);
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1 && jobs==1);
 assert(where[0]==PLACE_POOL);
''')

    def test_pool_census_never_replaces_native_proof(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? After the page-in
        the census can report every page in the pool while a native leaf is
        still missing. Only the complete native proof may admit the job; a
        census-ready heap with missing leaves is not repaired again."""
        self.build(r'''
 init(d);place(0,PLACE_SYSTEM);pool_free=0;leaves_withheld=true;hold(d);
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==0);
 assert(!jobs && d.GpuvaFailure==E_OUTOFMEMORY && where[0]==PLACE_POOL);
 assert(makes==1 && rehome_receipts==2 && last_rehome_hr==E_FAIL && last_rehome[2]==0);
''')

    def test_unprocessed_eviction_is_never_mistaken_for_a_move(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A MakeResident
        that reaches VidMm before the queued eviction cancels it, leaving the
        heap in segment 0. The repair must poll the census (bounded), then
        refuse instead of reporting a page-in that did not happen."""
        self.build(r'''
 init(d);place(0,PLACE_SYSTEM);pool_free=0;stuck_eviction=true;hold(d);
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==0);
 assert(!jobs && d.GpuvaFailure==E_OUTOFMEMORY && where[0]==PLACE_SYSTEM);
 assert(last_rehome_hr==E_OUTOFMEMORY && (last_rehome[7]&0xffu)==ADMISSION_UMD_REHOME_POLLS);
 assert(!(last_rehome[7]&0x100u) && sleeps==ADMISSION_UMD_REHOME_ROUNDS*(ADMISSION_UMD_REHOME_POLLS-1u));
 assert(refcount[0]==1 && d.ScreenBuffers[1].Resident);
''')

    def test_only_cpu_visible_pool_slots_are_moved(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A GPU-only slot,
        a native-staging (ExistingSysMem) slot, a borrowed import or a
        foreign-owned page cannot be repaired by a pool page-in; a missing
        leaf there must keep the old single touch and refusal."""
        self.build(r'''
 for(unsigned shape=0;shape<5;++shape) {
  init(d);hold(d);
  auto &heap=d.ScreenBuffers[1];
  if(shape==0) {heap.SystemDirect=FALSE;heap.Mapped=FALSE;}
  if(shape==1) heap.NativeStagingAddress=cpu_bytes;
  if(shape==2) heap.Borrowed=TRUE;
  if(shape==3) heap.KmtResidencyHeld=TRUE;
  place(0,PLACE_SYSTEM);pool_free=0;
  if(shape==4) kmd_foreign(0);
  assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==0);
  assert(!jobs && !evicts && !makes && where[0]==PLACE_SYSTEM && touches==1);
  assert(rehome_receipts==(shape==4 ? 1u : 0u));
 }
''')

    def test_cpu_map_of_heap_created_in_segment_zero(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A CPU-visible heap
        created while the pool is full starts in segment 0, so its first Map
        failed the pool proof (E_OUTOFMEMORY to the app). The map path must
        repair it under the same Lock and Transition, then pass the complete
        pool-only native proof."""
        self.build(r'''
 init(d);d.ScreenBuffers[0].Active=FALSE;
 auto &heap=d.ScreenBuffers[1];heap.Mapped=FALSE;heap.LockedBase=nullptr;heap.Transition=TRUE;
 place(0,PLACE_SYSTEM);pool_free=0;
 assert(AdmissionUmdGpuvaValidateCpuPoolMap(&d,101)==1);
 assert(where[0]==PLACE_POOL && d.GpuvaFailure==S_OK && heap.Resident && heap.Transition);
 assert(eager_evicts==2 && makes==1 && last_rehome[0]==ADMISSION_UMD_REHOME_MAP);
 /* An ordinary unmovable failure still refuses after one repair round set. */
 init(d);d.ScreenBuffers[0].Active=FALSE;
 heap.Mapped=FALSE;heap.Transition=TRUE;place(0,PLACE_SYSTEM);pool_free=0;keep_demoting=true;
 d.ScreenBuffers[2].Active=d.ScreenBuffers[3].Active=FALSE;
 assert(!AdmissionUmdGpuvaValidateCpuPoolMap(&d,101) && d.GpuvaFailure==E_OUTOFMEMORY);
 assert(makes==ADMISSION_UMD_REHOME_ROUNDS && !d.DrawTerminal && heap.Resident);
''')

    def test_placement_census_is_not_a_readiness_proof(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The new KMD query
        reports segment placement only. It must authenticate like the proof,
        classify pool / segment 0 / not resident / foreign pages exactly, and
        the batch transport must refuse it, so a census can never admit a job
        whose native leaves are absent."""
        self.build(r'''
 init(d);
 APPLE_AGX_G3_NATIVE_QUERY q={};
 auto census=[&](D3DKMT_HANDLE h,uint64_t va,UINT reserved,uint64_t token) {
  q={};q.Magic=APPLE_AGX_G3_NATIVE_QUERY_MAGIC;q.Version=APPLE_AGX_G3_NATIVE_QUERY_PLACEMENT_VERSION;
  q.Bytes=sizeof(q);q.Allocation=h;q.GpuVa=va;q.AllocationBytes=65536;q.Reserved=reserved;q.Reserved2=token;
  return kmd_escape(&q,sizeof(q));
 };
 const uint64_t va=0x1101000000ULL;
 assert(census(0x41,va,0,0)==0 && q.Ready==1 && !q.Reason && q.Reserved2==16);
 place(0,PLACE_SYSTEM);
 assert(census(0x41,va,0,0)==0 && !q.Ready && q.Reason==APPLE_AGX_G3_NATIVE_QUERY_NOT_POOL);
 assert(q.FailedVa==va && q.Reserved2==(16ull<<32));
 place(0,PLACE_NONE);
 assert(census(0x41,va,0,0)==0 && !q.Ready && q.Reason==APPLE_AGX_G3_NATIVE_QUERY_NOT_RESIDENT && !q.Reserved2);
 place(0,PLACE_POOL);kmd_foreign(0);
 assert(census(0x41,va,0,0)==0 && !q.Ready && q.Reason==APPLE_AGX_G3_NATIVE_QUERY_IDENTITY && q.FailedVa==va);
 place(0,PLACE_POOL);
 /* Same authentication and input rules as the proof. */
 assert(census(0x41,va,1,0)!=0 && census(0x41,va,0,5)!=0 && census(0x44,va,0,0)!=0);
 assert(census(0x41,va+1,0,0)!=0);
 APPLE_AGX_G3_NATIVE_QUERY_BATCH *b=(APPLE_AGX_G3_NATIVE_QUERY_BATCH *)calloc(1,sizeof(*b)+sizeof(q));
 auto *entry=(APPLE_AGX_G3_NATIVE_QUERY *)(b+1);
 auto batch=[&](UINT version) {
  memset(b,0,sizeof(*b)+sizeof(q));
  b->Magic=APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAGIC;b->Version=APPLE_AGX_G3_NATIVE_QUERY_BATCH_CACHED_VERSION;
  b->Bytes=sizeof(*b)+sizeof(q);b->Count=1;
  entry->Magic=APPLE_AGX_G3_NATIVE_QUERY_MAGIC;entry->Version=version;entry->Bytes=sizeof(q);
  entry->Allocation=0x41;entry->GpuVa=va;entry->AllocationBytes=65536;entry->Write=1;
  return kmd_escape(b,b->Bytes);
 };
 assert(batch(APPLE_AGX_G3_NATIVE_QUERY_PLACEMENT_VERSION)!=0 && !b->Ready);
 assert(batch(APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION)==0 && b->Ready==1);
 /* Pool PTEs with the native leaves withdrawn: census Ready, proof refuses. */
 kmd_withdraw(0);
 assert(census(0x41,va,0,0)==0 && q.Ready==1 && q.Reserved2==16);
 assert(batch(APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION)==0 && !b->Ready && !b->FailedIndex);
 assert(entry->Reason==AppleAgxG3WalkLeafAbsent && !entry->Reserved2);
 free(b);
''')


if __name__ == '__main__':
    unittest.main()
