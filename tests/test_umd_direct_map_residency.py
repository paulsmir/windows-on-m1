"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

An evicted SystemDirect BO must restore residency and finish paging before
LockCb / pointer publication. Transition must exclude trim/submission until
Mapped takes over; a persistent map must remain pinned. Execute production
map, restore, trim, submission and unmap bodies with controlled WDK callbacks.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_persistent_residency import BODY, ROOT, SRC
from test_umd_deallocated_slot_no_evict import function

EXTRA = r'''
typedef uint64_t APPLE_AGX_U64;
typedef unsigned APPLE_AGX_U32;
typedef unsigned char BYTE;
typedef void *PVOID;
enum { AppleAgxWin32BufferCpuRead=1, AppleAgxWin32BufferCpuWrite=2, AppleAgxWin32BufferGpuOnly=32, AppleAgxWin32BufferShareable=64, AppleAgxWin32BufferGpuWrite=8 };
#define APPLE_AGX_GPUVA_WINSYS 1
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define DXGI_ERROR_DEVICE_HUNG ((HRESULT)0x887a0006u)
static unsigned locks,unlocks,waits;
static bool fail_wait,fail_lock,fail_unlock,null_lock,rename_lock;
static unsigned expected_waits;
static unsigned data[8];
static ADMISSION_UMD_DEVICE *current;
static void try_trim();
static unsigned queries,touches;
static bool reject_pool,escape_fail,touch_fail,touch_refuse,deferred;
static HRESULT pool_query(HANDLE,D3DDDICB_ESCAPE *e) {
 ++queries;try_trim();auto &s=current->ScreenBuffers[0];
 assert(s.Transition && s.Resident && !s.Mapped && locks);
 auto *q=(APPLE_AGX_G3_NATIVE_QUERY *)e->pPrivateDriverData;
 assert(q->Version==2 && q->Reserved==1 && q->Allocation==s.KernelAllocation);
 assert(q->AllocationBytes==s.Bytes && q->GpuVa==s.CanonicalGpuVa && !q->Ready && !q->Reason && !q->FailedVa);
 if(escape_fail)return E_FAIL;
 q->Ready=!(reject_pool || deferred);q->Reason=reject_pool?0x102u:deferred?2u:0u;
 return S_OK;
}
static int touch_device(ADMISSION_UMD_DEVICE *d,uint64_t) {
 ++touches;try_trim();deferred=false;
 if(touch_fail) {d->DrawTerminal=TRUE;d->GpuvaFailure=E_FAIL;return 0;}
 if(touch_refuse) {d->GpuvaFailure=E_OUTOFMEMORY;return 0;}
 return 1;
}

static HRESULT lock_cb(HANDLE,D3DDDICB_LOCK *r) {
 ++locks;auto &s=current->ScreenBuffers[0];
 assert(s.Resident && s.Transition && !s.Mapped && "Lock before protected residency");
 assert(waits==expected_waits && "Lock before paging completion");
 try_trim();if(rename_lock)++r->hAllocation;r->pData=fail_lock || null_lock?NULL:data;return fail_lock?E_FAIL:S_OK;
}
static HRESULT unlock_cb(HANDLE,D3DDDICB_UNLOCK *r) {
 ++unlocks;assert(r->NumAllocations==1);assert(*r->phAllocations==0x100u+(rename_lock?1u:0u));try_trim();return fail_unlock?E_FAIL:S_OK;
}
static int wait_paging_at(ADMISSION_UMD_DEVICE *d,uint64_t f,UINT,uint64_t t) {
 assert(d==current && f==7 && t==10);++waits;try_trim();return !fail_wait;
}
static int AdmissionUmdGpuvaPrepareCpuMap(ADMISSION_UMD_DEVICE *,uint64_t) { return 1; }
static int AdmissionUmdGpuvaPrepareDirectMap(ADMISSION_UMD_DEVICE *,uint64_t) { return 1; }
static int AdmissionUmdGpuvaFinishDirectMap(ADMISSION_UMD_DEVICE *,uint64_t) { return 1; }
static void AdmissionUmdStagingInvalidate(int *) {}
@@PRODUCTION@@
static void try_trim() {
 UINT trimmed=99;assert(trim_residency(current,1,&trimmed)==0 && !trimmed);
 uint64_t token=10,fence=0;
 if(current->ScreenBuffers[0].Transition)
   assert(make_resident(current,&token,1,&fence)==0);
}
int main() {
 CB cb={mk,ev,lock_cb,unlock_cb};
 for(unsigned scenario=0;scenario<13;++scenario) {
   ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;
   d.PagingQueue=(HANDLE)1;d.KernelCallbacks=&cb;current=&d;
   auto &s=d.ScreenBuffers[0];s.Active=TRUE;s.Token=10;s.KernelAllocation=0x100;
   s.SystemDirect=TRUE;s.Bytes=64ull<<20;s.Flags=3;s.CanonicalGpuVa=0x10000;
   residency_status=scenario==7 || scenario==9 || scenario==10?S_OK:E_PENDING;
   returned_fence=scenario==8 || scenario==10?0:7;
   accepted_count=scenario==9?0:1;
   if(scenario==11)s.Resident=TRUE;
   expected_waits=scenario==10 || scenario==11?0:1;
   locks=unlocks=waits=made=evicted=0;fail_wait=scenario==1;fail_lock=scenario==2;
   null_lock=scenario==12;fail_unlock=scenario==3;refusals=scenario==4?100:0;
   if(scenario==5)s.SourceHolds=1;
   if(scenario==6)s.SubmissionHolds=1;
   void *address=NULL;
   int ok=AdmissionUmdScreenMapBuffer(&d,10,4,4,1,&address);
   if(scenario==0 || scenario==3 || scenario==7 || scenario==10 || scenario==11) {
     assert(ok && address==data+1 && s.Mapped && !s.Transition && s.Resident);
     assert(made==(scenario==11?0u:1u) && locks==1 && waits==expected_waits);
     assert(s.CpuAccess.Maps==1 && s.CpuAccess.Locks==1 && s.CpuAccess.ActiveLocks==1);
     try_trim();assert(evicted==0);
     assert(AdmissionUmdScreenUnmapBuffer(&d,10)==!fail_unlock);
     if(fail_unlock) { assert(s.Mapped && s.CpuAccess.ActiveLocks==1);try_trim();fail_unlock=false;assert(AdmissionUmdScreenUnmapBuffer(&d,10)); }
     UINT trimmed=0;assert(trim_residency(&d,1,&trimmed)==s.Bytes && trimmed==1);
     assert(!s.Resident && !s.Mapped && !s.Transition && s.CpuAccess.ActiveLocks==0);
     /* A second map restores once more, with another paging wait. */
     expected_waits=1;residency_status=E_PENDING;returned_fence=7;
     waits=0;assert(AdmissionUmdScreenMapBuffer(&d,10,0,4,2,&address));
     assert(made==(scenario==11?1u:2u) && waits==1 && address==data);
     assert(s.CpuAccess.Maps==2 && s.CpuAccess.Locks==2 && s.CpuAccess.ActiveLocks==1);
     assert(AdmissionUmdScreenUnmapBuffer(&d,10));
   } else {
     assert(!ok && !address && !s.Mapped && !s.Transition);
     assert(!s.CpuAccess.Maps && s.CpuAccess.Locks==(scenario==12) && s.CpuAccess.ActiveLocks==(scenario==12));
     assert(locks==(scenario==2 || scenario==12));
     if(scenario==1 || scenario==8) {
       assert(d.DrawTerminal && s.Resident);
       fail_wait=false;assert(!AdmissionUmdScreenMapBuffer(&d,10,0,4,1,&address));
       assert(locks==0 && made==1); /* never bypass a failed paging wait */
     }
     if(scenario==4) {
       // A hard budget refusal accepted no work: preserve OOM and allow retry.
       assert(!d.DrawTerminal && !s.Resident && d.LastScreenError==E_OUTOFMEMORY);
       refusals=0;expected_waits=1;waits=0;
       assert(AdmissionUmdScreenMapBuffer(&d,10,0,4,1,&address));
       assert(address==data && s.Resident && s.Mapped && !d.DrawTerminal);
       assert(AdmissionUmdScreenUnmapBuffer(&d,10));
     }
     if(scenario==5 || scenario==6)assert(made==0);
     if(scenario==9)assert(d.DrawTerminal && !s.Resident);
   }
 }
 /* Private staging returns a native pointer without calling LockCb. */
 ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;d.KernelCallbacks=&cb;current=&d;
 auto &s=d.ScreenBuffers[0];s.Active=TRUE;s.Token=10;s.KernelAllocation=0x100;
 s.Bytes=64ull<<20;s.Flags=3;s.PrivateStaging=(BYTE *)data;
 void *address=NULL;locks=unlocks=0;
 assert(AdmissionUmdScreenMapBuffer(&d,10,0,4,2,&address) && address==data);
 assert(s.CpuAccess.Maps==1 && !s.CpuAccess.Locks && !s.CpuAccess.ActiveLocks && !locks);
 assert(AdmissionUmdScreenUnmapBuffer(&d,10) && !unlocks && !s.Mapped);
 /* T7h: the authoritative slot refuses a GPU-only CPU-map request. */
 s.Flags=0x2c;address=(void *)1;locks=unlocks=waits=0;
 assert(!AdmissionUmdScreenMapBuffer(&d,10,0,4,2,&address));
 assert(!address && !locks && !unlocks && !waits && !s.Mapped && !s.Transition);
 assert(d.LastScreenError==E_INVALIDARG);
 puts("direct CPU map residency: PASS");
}
'''

class DirectMapResidency(unittest.TestCase):
    def test_restore_wait_lock_and_persistent_map_exclusion(self):
        self.run_replay(EXTRA)

    def test_direct_pool_map_proof_and_failed_cleanup(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Direct CPU maps must prove pool placement before publication, retain persistent residency, and never forget a lock when a refused map cannot be unlocked."""
        extra = EXTRA[:EXTRA.index('int main() {')] + r'''
int main() {
 CB cb={mk,ev,lock_cb,unlock_cb,pool_query};
 ADAPTER a={ADMISSION_UMD_ADAPTER_MAGIC,{(HANDLE)1}};
 for(unsigned scenario=0;scenario<9;++scenario) {
  ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;d.PagingQueue=(HANDLE)1;
  d.KernelCallbacks=&cb;d.DirectCpuPool=TRUE;d.Adapter=&a;d.KernelContext=(HANDLE)2;d.RuntimeDevice.handle=(HANDLE)3;current=&d;
  auto &s=d.ScreenBuffers[0];s.Active=TRUE;s.Token=10;s.KernelAllocation=0x100;
  s.SystemDirect=TRUE;s.Bytes=64ull<<20;s.Flags=3;s.CanonicalGpuVa=0x10000;
  locks=unlocks=waits=made=evicted=queries=touches=0;expected_waits=1;
  reject_pool=scenario==1 || scenario==2;fail_unlock=scenario==2 || scenario==7;rename_lock=scenario==7;
  escape_fail=scenario==3;deferred=scenario==4 || scenario==5;touch_fail=scenario==5;
  touch_refuse=scenario==8;
  null_lock=scenario==6;
  void *address=(void*)1;
  int ok=AdmissionUmdScreenMapBuffer(&d,10,0,4,3,&address);
  if(scenario==0 || scenario==4) {
   assert(ok && address==data && s.Mapped && !s.PrivateStaging && s.Resident);
   assert(queries==1 && touches==1);
   try_trim();assert(!evicted);
   // CPU writes and GPU results refer to the same canonical storage.
   ((unsigned*)address)[0]=0x1234;assert(data[0]==0x1234);
   data[1]=0x5678;assert(((unsigned*)address)[1]==0x5678);
   assert(AdmissionUmdScreenUnmapBuffer(&d,10));
   UINT n=0;assert(trim_residency(&d,1,&n)==s.Bytes && n==1 && !s.Resident);
  } else {
   assert(!ok && !address && unlocks==1 && s.Resident);
   assert(!s.Transition);
   if(scenario==2 || scenario==7) {
    assert(d.DrawTerminal && s.Mapped && s.LockedBase==data && s.CpuAccess.ActiveLocks==1);
    try_trim();assert(!evicted);
    fail_unlock=false;
    assert(AdmissionUmdScreenUnmapBuffer(&d,10));
    assert(unlocks==2 && !s.Mapped && !s.CpuAccess.ActiveLocks);
   } else {
    assert(!s.Mapped && !s.LockedBase && !s.CpuAccess.ActiveLocks);
    if(scenario==5)assert(d.DrawTerminal && touches==1);
    // WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A touch refused
    // before acceptance must allow unlock/clean OOM and a later CPU-map retry.
    if(scenario==8) {
     assert(!d.DrawTerminal && d.LastScreenError==E_OUTOFMEMORY && !queries);
     touch_refuse=false;
     assert(AdmissionUmdScreenMapBuffer(&d,10,0,4,3,&address));
     assert(address==data && made==1 && s.Mapped);
     assert(AdmissionUmdScreenUnmapBuffer(&d,10));
    }
    if(scenario==1) {
     assert(d.LastScreenError==E_OUTOFMEMORY && !d.DrawTerminal);
     reject_pool=false;
     assert(AdmissionUmdScreenMapBuffer(&d,10,0,4,3,&address));
     assert(address==data && made==1 && s.Mapped);
     assert(AdmissionUmdScreenUnmapBuffer(&d,10));
    }
    if(scenario==3)assert(d.LastScreenError==E_FAIL && touches==1);
    if(scenario==6)assert(queries==0);
   }
  }
 }
 puts("direct CPU pool maps: PASS");
}
'''
        self.run_replay(extra)

    def test_native_ring_map_waits_without_runtime_lock(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        Ring CPU reads must wait the last GPU fence, never call Lock/Unlock
        or claim a runtime lock, and never run the pool-only placement query.
        """
        extra = EXTRA[:EXTRA.index('int main() {')]
        extra = extra.replace('@@PRODUCTION@@', r'''
static bool native_wait_failed;
static unsigned native_waits;
static int AdmissionUmdGpuvaWaitNativeStaging(ADMISSION_UMD_DEVICE *d,
    unsigned long long fence) {
 assert(d==current && fence==12 && d->ScreenBuffers[0].Transition);
 ++native_waits;return !native_wait_failed;
}
@@PRODUCTION@@''')
        extra += r'''
int main() {
 CB cb={mk,ev,lock_cb,unlock_cb,pool_query};
 for(unsigned failed=0;failed<2;++failed) {
  ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;
  d.KernelCallbacks=&cb;d.DirectCpuPool=TRUE;current=&d;
  auto &s=d.ScreenBuffers[0];s.Active=TRUE;s.Token=10;s.KernelAllocation=0x100;
  s.SystemDirect=TRUE;s.Resident=TRUE;s.Bytes=64ull<<20;s.Flags=3;
  s.NativeStagingAddress=(BYTE *)data;s.LastUseFence=12;
  locks=unlocks=queries=native_waits=0;native_wait_failed=failed!=0;
  void *address=NULL;
  assert(AdmissionUmdScreenMapBuffer(&d,10,4,4,1,&address)==!failed);
  assert(!locks && !queries && native_waits==1 && !s.CpuAccess.ActiveLocks);
  if(failed)assert(!s.Mapped && !s.Transition && !address);
  else {
   assert(address==data+1 && s.Mapped);
   assert(AdmissionUmdScreenUnmapBuffer(&d,10));
   assert(!unlocks && !s.Mapped && !s.CpuAccess.Locks && !s.CpuAccess.ActiveLocks);
  }
 }
}
'''
        self.run_replay(extra, native=True)

    def run_replay(self, extra, native=False):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Receipt counters must distinguish successful locks, failed locks, private staging, and retained failed unlocks while map/residency ordering stays unchanged."""
        prefix = BODY[:BODY.index('@@FUNCS@@')]
        prefix = prefix.replace('struct SLOT {', '#include "umd_cpu_access.h"\nstruct SLOT { ADMISSION_UMD_CPU_ACCESS CpuAccess;')
        prefix = prefix.replace('static HRESULT mk(', 'static HRESULT residency_status=E_PENDING;\nstatic ULONGLONG returned_fence=7;\nstatic UINT accepted_count=1;\nstatic HRESULT mk(')
        prefix = prefix.replace('r->PagingFenceValue=7;return E_PENDING;', 'r->NumAllocations=accepted_count;r->PagingFenceValue=returned_fence;return residency_status;')
        prefix = prefix.replace('uint64_t CanonicalGpuVa,Bytes,LastUseFence;',
                                'uint64_t CanonicalGpuVa,Bytes,LastUseFence; void *LockedBase; UINT LockedAccess,MapEpoch,Flags,FailedCpuLock; int Sync;')
        prefix = prefix.replace('struct CB {', '''struct D3DDDICB_LOCK { D3DKMT_HANDLE hAllocation; struct { UINT LockEntire,ReadOnly,WriteOnly; } Flags; void *pData; };
struct D3DDDICB_UNLOCK { UINT NumAllocations; D3DKMT_HANDLE *phAllocations; };
struct CB {''')
        prefix = prefix.replace('HRESULT(*pfnEvictCb)(HANDLE,const D3DDDICB_EVICT*);',
                                'HRESULT(*pfnEvictCb)(HANDLE,const D3DDDICB_EVICT*); HRESULT(*pfnLockCb)(HANDLE,D3DDDICB_LOCK*); HRESULT(*pfnUnlockCb)(HANDLE,D3DDDICB_UNLOCK*);')
        prefix = prefix.replace('UINT Magic; SLOT ScreenBuffers', 'HRESULT LastScreenError; BOOL DirectCpuPool; ADAPTER *Adapter; HANDLE KernelContext; UINT Magic; SLOT ScreenBuffers')
        prefix = prefix.replace('struct CB {', 'struct ADAPTER { UINT Magic; struct { HANDLE handle; } RuntimeAdapter; };\nstruct D3DDDICB_ESCAPE { HANDLE hDevice,hContext; void *pPrivateDriverData; UINT PrivateDriverDataSize; };\nstruct CB {')
        prefix = prefix.replace('HRESULT(*pfnUnlockCb)(HANDLE,D3DDDICB_UNLOCK*);', 'HRESULT(*pfnUnlockCb)(HANDLE,D3DDDICB_UNLOCK*); HRESULT(*pfnEscapeCb)(HANDLE,D3DDDICB_ESCAPE*);')
        prefix += '#include "apple_agx_g3_copy_abi.h"\n#define ADMISSION_UMD_ADAPTER_MAGIC 0x66u\n#define AppleAgxG3WalkLeafAbsent 1u\n#define AppleAgxG3WalkLeafNotPublished 2u\n'
        # rc107: the placement repair has its own replay (test_umd_cpu_pool_rehome).
        prefix += ('#include "umd_cpu_pool_rehome.h"\nstatic unsigned rehome_requests;\n'
                   'static int rehome_cpu_pool(ADMISSION_UMD_DEVICE *,uint64_t,uint64_t,UINT,UINT)'
                   ' {++rehome_requests;return 0;}\n')
        if native:
            prefix = '#define APPLE_AGX_NATIVE_STAGING 1\n' + prefix
            prefix = prefix.replace('struct SLOT {', 'struct SLOT { unsigned char *NativeStagingAddress;')

        gpu = SRC.read_text()
        screen = (SRC.parent/'umd_win32_screen.c').read_text()
        funcs = '\n'.join(function(gpu,n) for n in ('watch_slots','find_slot','release_copies','allocation_handle','translate_handles','evict','trim_residency_with','trim_residency','residency_request','make_resident','restore_residency'))
        funcs += '\n' + function(gpu,'AdmissionUmdGpuvaRestoreCpuMapResidency',required=False)
        funcs += '\n' + function(gpu,'AdmissionUmdGpuvaValidateCpuPoolMap',required=False)
        funcs += '\n' + '\n'.join(function(screen,n) for n in ('AdmissionUmdScreenCpuAllocation','AdmissionUmdScreenFind','AdmissionUmdScreenMapBuffer','AdmissionUmdScreenUnmapBuffer'))
        with tempfile.TemporaryDirectory(prefix='t6b-residency-') as tmp:
            src,exe=Path(tmp)/'test.cpp',Path(tmp)/'test'
            src.write_text(prefix+extra.replace('@@PRODUCTION@@',funcs))
            built=subprocess.run(['clang++','-std=c++17', '-include', str(ROOT/'tests/d3d12_wait_disabled_fixture.h'),'-Wall','-Wno-unused-function','-fsanitize=address,undefined','-I',str(ROOT/'drivers/apple-agx/render-admission/umd/include'),'-I',str(ROOT/'drivers/apple-agx/shared/include'),str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
