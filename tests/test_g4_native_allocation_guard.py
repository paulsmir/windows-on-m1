"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1933 accepted resident logical mappings with omitted native leaves. Only
this job's referenced allocations must have complete, owner-matching native
coverage before SubmitCommandCb; unrelated omissions and the CPU envelope
must not become the rejected T12 process-wide veto.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body
from test_umd_async_render_completion import function

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
KMD = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'
UMD = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'

class NativeAllocationGuard(unittest.TestCase):
    def compile_run(self, source, cpp=False, graph=False):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / ('replay.cpp' if cpp else 'replay.c')
            exe = Path(tmp) / 'replay'
            src.write_text(source)
            cmd = [os.environ.get('CXX' if cpp else 'CC', 'clang++' if cpp else 'clang'),
                   '-std=c++17' if cpp else '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-I', str(SHARED / 'include'), str(src)]
            if graph:
                cmd += [str(SHARED / 'src' / name) for name in (
                    'apple_agx_gpuva_g3_graph.c', 'apple_agx_gpuva_g3_translation.c',
                    'apple_agx_gpuva_broker_v5_client.c')]
            build = subprocess.run(cmd + ['-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_kmd_exact_allocation_native_coverage(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A published first leaf cannot authorize a scattered interior leaf, foreign backing or a stale native IPA."""
        text = KMD.read_text()
        self.assertIn('AdmissionGpuvaG3NativeQueryEscape(', text,
                      'No authenticated per-allocation native-coverage query')
        funcs = '\n'.join(body(text, n) for n in ('AdmissionG3CopyPte', 'AdmissionGpuvaG3NativeQueryEscape'))
        self.compile_run(KMD_FIXTURE.replace('@@FUNCS@@', funcs), graph=True)

    def test_umd_blocks_before_actual_submit_callback(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Residency alone currently lets a referenced omitted leaf reach the Windows submission callback."""
        text = UMD.read_text()
        funcs = '\n'.join(function(text, n) for n in ('native_held_ready', 'submit'))
        self.compile_run(UMD_FIXTURE.replace('@@FUNCS@@', funcs), cpp=True)

KMD_FIXTURE = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_gpuva_g3_graph.h"
#include "apple_agx_gpuva_g3_translation.h"
typedef unsigned UINT, ULONG; typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR; typedef void *HANDLE; typedef int NTSTATUS;
#define PASSIVE_LEVEL 0
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
typedef struct { APPLE_AGX_GPUVA_G3_GRAPH Graph; UINT Poisoned,CpuOnlyMappings;
 ADMISSION_RENDER_CONTEXT *Contexts; ADMISSION_G3_TABLE_SHADOW *TableShadowBrokerBuckets[256]; } ADMISSION_G3_PROCESS;
typedef struct { int Lock; ADMISSION_G3_PROCESS *Process; } ADMISSION_G3_STATE;
typedef struct { UINT hObject,Type; struct { UINT DeviceSpecific; } Flags; } DXGKARGCB_GETHANDLEDATA;
typedef struct { UINT Type; void *ReleaseHandle; } DXGKARGCB_RELEASEHANDLEDATA;
typedef struct { UINT Started; ADMISSION_G3_STATE *GpuvaG3State; int ObjectAdapter;
 struct { void *(*DxgkCbAcquireHandleData)(DXGKARGCB_GETHANDLEDATA*,void**);
 void (*DxgkCbReleaseHandleData)(DXGKARGCB_RELEASEHANDLEDATA); } Interface; } ADMISSION_CONTEXT;
typedef struct { void *pPrivateDriverData; UINT PrivateDriverDataSize; struct { UINT Value; } Flags;
 HANDLE hKmdProcessHandle,hContext,hDevice; } DXGKARG_ESCAPE;
static ADMISSION_OPEN_ALLOCATION opened;
static void *acquire(DXGKARGCB_GETHANDLEDATA *q, void **r) {
 assert(q->Type==DXGK_HANDLE_ALLOCATION && q->Flags.DeviceSpecific);
 if(q->hObject!=opened.RuntimeAllocation) return NULL;
 ++refs; *r=&opened; return &opened;
}
static void release(DXGKARGCB_RELEASEHANDLEDATA r) { assert(r.ReleaseHandle==&opened && refs==1); --refs; }
static ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(ADMISSION_G3_STATE *s,HANDLE h) {
 assert(locked); return h==s->Process ? s->Process : NULL;
}
@@FUNCS@@
static APPLE_AGX_GPUVA_G3_NODE root,middle,table,re,me,leaves[4];
static APPLE_AGX_GPUVA_G3_NODE *rs[8],*ms[2048],*ls[2048];
static APPLE_AGX_GPUVA_G3_LOGICAL_PTE ptes[8192];
int main(void) {
 ADMISSION_G3_PROCESS p={0}; ADMISSION_G3_STATE state={0,&p};
 ADMISSION_CONTEXT a={0}; ADMISSION_RENDER_CONTEXT c={0}; ADMISSION_DEVICE d={0};
 ADMISSION_ALLOCATION_HANDLE allocation={0}; ADMISSION_G3_TABLE_SHADOW shadow={0};
 const ULONGLONG va=0x1101000000ULL, ipa=0x900000000ULL, tableIpa=0x920004000ULL;
 a.Started=1; a.GpuvaG3State=&state; a.Interface.DxgkCbAcquireHandleData=acquire;
 a.Interface.DxgkCbReleaseHandleData=release; d.Object.Adapter=&a.ObjectAdapter;
 c.Object.Device=&d.Object;c.Win32Transport=1;c.GpuvaG3RootIpa=0x920000000ULL;p.Contexts=&c;
 allocation.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;allocation.Object.Description.Size=65536;
 opened.Magic=ADMISSION_OPEN_ALLOCATION_MAGIC;opened.RuntimeAllocation=42;
 opened.Device=&d;opened.Allocation=&allocation.Object;
 p.Graph.Created=1;p.Graph.RootIpa=c.GpuvaG3RootIpa;p.Graph.RootTable=&root;
 root.Slots=rs;middle.Slots=ms;table.Slots=ls;re.ChildTable=&middle;me.ChildTable=&table;me.AuxIpa=tableIpa;
 rs[(va>>36)&7]=&re;ms[(va>>25)&2047]=&me;
 shadow.BrokerIpa=tableIpa;shadow.ResidentPtes=ptes;
 p.TableShadowBrokerBuckets[((tableIpa>>14)^(tableIpa>>22)^(tableIpa>>30))&255]=&shadow;
 for(unsigned i=0;i<4;++i) {leaves[i].AuxIpa=ipa+i*16384;leaves[i].Writable=1;ls[((va>>14)&2047)+i]=&leaves[i];}
 for(unsigned i=0;i<16;++i) {unsigned n=((va>>12)&8191)+i;ptes[n].Flags=3;ptes[n].GuestIpa=ipa+i*4096;
  ptes[n].Allocation=(ULONG_PTR)&allocation;ptes[n].AllocationOffset=i*4096;}
 APPLE_AGX_G3_NATIVE_QUERY q={0}; q.Magic=APPLE_AGX_G3_NATIVE_QUERY_MAGIC;q.Version=1;q.Bytes=sizeof(q);
 q.Allocation=42;q.GpuVa=va;q.AllocationBytes=65536;q.Write=1;
 DXGKARG_ESCAPE e={&q,sizeof(q),{0},&p,&c,&d};
 #define CHECK(ready) do {q.Ready=0;q.FailedVa=0;q.Reason=0;assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)==0);assert(q.Ready==(ready));assert(!locked && !refs);} while(0)
 CHECK(1); assert(sizeof(q)==64);
 /* Imported surfaces expose logical bytes smaller than canonical capacity. */
 q.AllocationBytes=64513;CHECK(1);assert(q.AllocationBytes==65536);
 q.AllocationBytes=16384;leaves[3].AuxIpa=0;CHECK(0);
 assert(q.FailedVa==va+49152);leaves[3].AuxIpa=ipa+49152;CHECK(1);
 /* A pre-submit escape precedes the context SetRoot notification (R147). */
 c.GpuvaG3RootIpa=0;CHECK(1);c.GpuvaG3RootIpa=p.Graph.RootIpa;
 /* An absent unrelated leaf is immaterial; no process-wide census. */
 ptes[0].Flags=3;ptes[0].GuestIpa=0xa00001000ULL;CHECK(1);
 /* Interior omitted group despite all logical pages valid, as EXP1933. */
 leaves[2].AuxIpa=0;CHECK(0);assert(q.FailedVa==va+32768);leaves[2].AuxIpa=ipa+32768;CHECK(1);
 /* Read-only publication must not authorize writes. */
 leaves[3].Writable=0;CHECK(0);q.Write=0;CHECK(1);q.Write=1;leaves[3].Writable=1;
 unsigned n=(va>>12)&8191;
 ++ptes[n+5].GuestIpa;CHECK(0);assert(q.FailedVa==va+5*4096);--ptes[n+5].GuestIpa;
 ++ptes[n+5].Allocation;CHECK(0);--ptes[n+5].Allocation;
 ++ptes[n+5].AllocationOffset;CHECK(0);--ptes[n+5].AllocationOffset;
 ptes[n+5].Flags=0;CHECK(0);ptes[n+5].Flags=3;CHECK(1);
 /* Read-only query during an active job needs no wait or lease mutation. */
 p.Graph.JobInFlight=1;p.Graph.LeaseToken=77;CHECK(1);assert(p.Graph.LeaseToken==77);
 #define BAD(change,restore) do {change;q.Ready=0;q.FailedVa=0;q.Reason=0;assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)!=0);restore;assert(!locked && !refs);} while(0)
 BAD(q.AllocationBytes=65537,q.AllocationBytes=65536);
 BAD(q.Allocation=43,q.Allocation=42);
 BAD(e.hContext=NULL,e.hContext=&c);BAD(e.hKmdProcessHandle=NULL,e.hKmdProcessHandle=&p);
 BAD(e.hDevice=NULL,e.hDevice=&d);BAD(opened.Device=NULL,opened.Device=&d);
 BAD(p.Graph.Uncertain=1,p.Graph.Uncertain=0);
 BAD(p.CpuOnlyMappings=1,p.CpuOnlyMappings=0);BAD(opened.ReadOnly=1,opened.ReadOnly=0);
 BAD(q.GpuVa=(1ULL<<39)-16384,q.GpuVa=va);BAD(q.Write=2,q.Write=1);
 BAD(e.PrivateDriverDataSize--,e.PrivateDriverDataSize++);BAD(irql=1,irql=0);
 CHECK(1);
}
'''

UMD_FIXTURE = r'''
/* Receipt sink is tested separately by test_umd_va_watch. */
#define watch_slots(...) ((void)0)
#include <cassert>
#include <cstdint>
#include <cstring>
#include "apple_agx_g3_copy_abi.h"
using UINT=unsigned;using ULONGLONG=unsigned long long;using HRESULT=int;
using D3DKMT_HANDLE=unsigned;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_FAIL -1
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define ADMISSION_UMD_WRITTEN_LIMIT 128u
#define D3DDDI_MAX_WRITTEN_PRIMARIES 16u
#define ADMISSION_UMD_ADAPTER_MAGIC 123u
#define AppleAgxWin32BufferGpuWrite 8u
struct ADMISSION_UMD_SCREEN_BUFFER {bool Active,Transition,CopyHeld,Direct,WrittenPrimary;
 uint64_t Token,CanonicalGpuVa,Bytes,LastUseFence;UINT KernelAllocation,Flags,SubmissionHolds;};
struct D3DDDICB_SUBMITCOMMAND {UINT NumPrimaries,WrittenPrimaries[16];uint64_t Commands;
 UINT CommandLength,BroadcastContextCount,BroadcastContext[1],PrivateDriverDataSize,RenderCBSequence;void *pPrivateDriverData;};
struct D3DDDICB_ESCAPE {UINT hDevice,hContext;struct {UINT Value;} Flags;void *pPrivateDriverData;UINT PrivateDriverDataSize;};
struct CALLBACKS {HRESULT (*pfnSubmitCommandCb)(UINT,D3DDDICB_SUBMITCOMMAND*);
 void (*pfnSignalSynchronizationObjectFromGpu2Cb)();HRESULT (*pfnEscapeCb)(UINT,D3DDDICB_ESCAPE*);};
struct ADAPTER {UINT Magic;struct {UINT handle;} RuntimeAdapter;};
struct ADMISSION_UMD_DEVICE {UINT KernelContext,RenderSyncObject;CALLBACKS *KernelCallbacks;
 uint64_t NextRenderFence;struct {UINT handle;} RuntimeDevice;int ScreenBufferLock;
 ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[4];UINT RenderCbSequence;bool DrawTerminal;ADAPTER *Adapter;};
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 4u
static int locks,submits,signals,queries,uploads;static unsigned bad=2;static bool escape_fail,upload_fail;
static void AcquireSRWLockShared(int*) {assert(!locks);++locks;}
static void ReleaseSRWLockShared(int*) {assert(locks==1);--locks;}
static void AcquireSRWLockExclusive(int *p) {AcquireSRWLockShared(p);}
static void ReleaseSRWLockExclusive(int *p) {ReleaseSRWLockShared(p);}
static UINT InterlockedIncrement(UINT *p) {return ++*p;}
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,size_t) {}
static void AdmissionUmdScreenCpuAccessSnapshot(ADMISSION_UMD_DEVICE *,HRESULT,UINT) {}
static int mark_written(ADMISSION_UMD_DEVICE *,const uint64_t*,unsigned) {return 1;}
static int transfer_held(ADMISSION_UMD_DEVICE *,bool down) {assert(!down);++uploads;return !upload_fail;}
static int signal_render(ADMISSION_UMD_DEVICE *,uint64_t) {++signals;return 1;}
static int finish_submission(ADMISSION_UMD_DEVICE *,uint64_t n,UINT,uint64_t *f) {*f=n;return 1;}
static HRESULT cb(UINT,D3DDDICB_SUBMITCOMMAND*) {++submits;return 0;}
static void signal_cb() {}
static HRESULT escape_cb(UINT,D3DDDICB_ESCAPE *e) {
 assert(uploads && !locks && !e->Flags.Value && e->hDevice==3 && e->hContext==7);
 ++queries;
#ifdef APPLE_AGX_G3_NATIVE_QUERY_MAGIC
 auto *q=(APPLE_AGX_G3_NATIVE_QUERY*)e->pPrivateDriverData;
 assert(e->PrivateDriverDataSize==sizeof(*q) && q->Magic==APPLE_AGX_G3_NATIVE_QUERY_MAGIC);
 assert(q->Allocation!=1 && q->Allocation!=4 && q->AllocationBytes==65536);
 q->Ready=q->Allocation!=bad;q->FailedVa=q->Ready?0:q->GpuVa+32768;
#endif
 return escape_fail ? E_FAIL : S_OK;
}
@@FUNCS@@
static void hold(ADMISSION_UMD_DEVICE &d) {for(unsigned i=0;i<3;++i)d.ScreenBuffers[i].CopyHeld=true;}
int main() {
 CALLBACKS cbks={cb,signal_cb,escape_cb};ADAPTER a={ADMISSION_UMD_ADAPTER_MAGIC,{9}};
 ADMISSION_UMD_DEVICE d={};d.Adapter=&a;d.KernelCallbacks=&cbks;d.KernelContext=7;
 d.RuntimeDevice.handle=3;d.RenderSyncObject=8;
 for(unsigned i=0;i<4;++i) {auto &s=d.ScreenBuffers[i];s.Active=true;s.Token=i+1;s.KernelAllocation=i+1;
  s.CanonicalGpuVa=0x10000ull*(i+1);s.Bytes=65536;s.Flags=8;s.SubmissionHolds=1;}
 uint64_t fence=0;unsigned private_data=0;hold(d);
 /* Real old submit reaches cb even though referenced allocation 2 is bad. */
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==0);
 assert(!submits && !signals && !fence && !locks && queries==1);
 /* Unreferenced allocation 4 is bad; command 1 has no native leaf either. */
 bad=4;queries=0;hold(d);assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1);
 assert(submits==1 && signals==1 && queries==2 && fence==1);
 /* Revalidate every submission; no stale Ready cache after residency moves. */
 bad=3;queries=0;hold(d);fence=0;assert(!submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence));
 assert(submits==1 && queries==2 && !fence);
 bad=0;escape_fail=true;hold(d);assert(!submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence));
 assert(submits==1);escape_fail=false;upload_fail=true;queries=0;hold(d);
 assert(!submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence) && !queries && submits==1);
}
'''
