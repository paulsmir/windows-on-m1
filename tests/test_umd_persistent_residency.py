"""EXP978: UMD residency is a persistent per-slot reference.

EXP977 ETW showed VidMm moving ~10.7 GB local->system and ~10.5 GB back in 30 s:
the UMD called pfnMakeResidentCb before every submit and pfnEvictCb after it,
so idle trimming paged the whole working set out and in at the frame rate.
Invariant: a slot takes exactly one MakeResident reference in its lifetime;
ending a submission only releases its copy holds; later submissions call
MakeResident only for slots that are not resident yet.

M2-f11 (EXP1271): a request VidMm refuses for budget (E_OUTOFMEMORY with
NumBytesToTrim) drops the reference of idle GPU-local slots, least recently
used first, and is retried; slots held by a submission are never trimmed, and
the loop ends when nothing more can be trimmed.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        raise AssertionError('missing ' + name)
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG; typedef size_t SIZE_T; typedef void* HANDLE;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_PENDING ((HRESULT)0x8000000Au)
#define E_OUTOFMEMORY ((HRESULT)0x8007000Eu)
#define E_FAIL ((HRESULT)0x80004005u)
#define HEAP_ZERO_MEMORY 8u
#define ZeroMemory(p,n) memset((p),0,(n))
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 16u
#define ADMISSION_UMD_IN_FLIGHT_SETS 4u  /* umd_internal.h (EXP1203) */
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) ADMISSION_UMD_SCREEN_BUFFER_LIMIT
#define ADMISSION_UMD_DEVICE_MAGIC 0x55u
static unsigned GetEnvironmentVariableA(const char *name,char *out,unsigned size){
  const char *v=getenv(name);if(!v)return 0;
  unsigned n=(unsigned)strlen(v);if(n>=size)return n+1;
  memcpy(out,v,n+1);return n;
}
static unsigned GetLastError(){return 0;}
static void SetLastError(unsigned){}
static void* GetProcessHeap(){return nullptr;}
static void* HeapAlloc(void*,unsigned,size_t n){return calloc(1,n);}
static void HeapFree(void*,unsigned,void*p){free(p);}
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*l){assert(*l==0);*l=-1;}
static void ReleaseSRWLockExclusive(SRWLOCK*l){assert(*l==-1);*l=0;}
static void AcquireSRWLockShared(SRWLOCK*l){assert(*l>=0);++*l;}
static void ReleaseSRWLockShared(SRWLOCK*l){assert(*l>0);--*l;}
#include "umd_residency_trim.h"
struct D3DDDI_MAKERESIDENT { struct { UINT CantTrimFurther, MustSucceed; } Flags; HANDLE hPagingQueue; UINT NumAllocations; const D3DKMT_HANDLE*AllocationList; const UINT*PriorityList; ULONGLONG PagingFenceValue; ULONGLONG NumBytesToTrim; };
struct SLOT { uint64_t Token; BOOL SystemDirect,Active,Transition,CopyHeld,Direct,Mapped,Resident,Borrowed,KmtResidencyHeld,HeapStaging; UINT SourceHolds,SubmissionHolds;
  D3DKMT_HANDLE KernelAllocation,StagingAllocation; unsigned char *PrivateStaging; uint64_t CanonicalGpuVa,Bytes,LastUseFence; void*NativeBo; void*NativeMapRelease; };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
static unsigned made, evicted, made_handles, refusals;  /* refusals: next mk calls refused for budget */
static D3DKMT_HANDLE last_evicted[16]; static unsigned last_evicted_count;
static HRESULT mk(HANDLE,D3DDDI_MAKERESIDENT*r){++made;
  if(refusals){--refusals;r->NumBytesToTrim=16u<<20;return E_OUTOFMEMORY;}
  made_handles+=r->NumAllocations;r->PagingFenceValue=7;return E_PENDING;}
struct D3DDDICB_EVICT { struct { UINT EvictOnlyIfNecessary; } Flags; UINT NumAllocations; const D3DKMT_HANDLE*AllocationList; ULONGLONG NumBytesToTrim; };
static void (*before_evict)();
static HRESULT evict_result=S_OK;
static HRESULT ev(HANDLE,const D3DDDICB_EVICT*e){if(before_evict)before_evict();++evicted;assert(e->Flags.EvictOnlyIfNecessary);
  last_evicted_count=e->NumAllocations;for(UINT i=0;i<e->NumAllocations;++i)last_evicted[i]=e->AllocationList[i];return evict_result;}
static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,UINT){}
struct CB { HRESULT(*pfnMakeResidentCb)(HANDLE,D3DDDI_MAKERESIDENT*); HRESULT(*pfnEvictCb)(HANDLE,const D3DDDICB_EVICT*); };
struct ADMISSION_UMD_DEVICE { UINT Magic; SLOT ScreenBuffers[16]; SRWLOCK ScreenBufferLock; BOOL DrawTerminal,ScreenClosing,BorrowedContext; HANDLE PagingQueue; CB*KernelCallbacks; struct{HANDLE handle;}RuntimeDevice; };
static void va_record(const void*,UINT,unsigned long long,D3DKMT_HANDLE,unsigned long long,unsigned long long,HRESULT){}
#define ADMISSION_UMD_WATCH_WINDOWS
#include "umd_va_watch.h"
@@FUNCS@@
int main(){
 CB cb={mk,ev}; ADMISSION_UMD_DEVICE d={}; d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.PagingQueue=(HANDLE)1; d.KernelCallbacks=&cb;
 for(unsigned i=0;i<6;++i){SLOT&s=d.ScreenBuffers[i];s.Active=1;s.Token=10+i;s.KernelAllocation=0x100+i;s.StagingAllocation=0x200+i;s.CanonicalGpuVa=0x10000*(i+1);s.Bytes=16u<<20;}
 uint64_t t01[2]={10,11}, t012[3]={10,11,12}; uint64_t fence=0;
 /* First submit: both slots become resident with one call. */
 assert(make_resident(&d,t01,2,&fence)==2 && fence==7 && made==1 && made_handles==2);
 assert(d.ScreenBuffers[0].CopyHeld && d.ScreenBuffers[1].CopyHeld);
 assert(evict(&d,t01,2)==1);
 assert(!d.ScreenBuffers[0].CopyHeld && d.ScreenBuffers[0].Resident && d.ScreenBuffers[1].Resident);
 /* Repeated submits of the same set: no MakeResident, no paging fence. */
 for(int k=0;k<100;++k){ fence=9; assert(make_resident(&d,t01,2,&fence)==1 && fence==0); assert(evict(&d,t01,2)==1); }
 assert(made==1 && made_handles==2 && evicted==0);
 /* A new slot joins: only it is made resident. */
 assert(make_resident(&d,t012,3,&fence)==2 && made==2 && made_handles==3 && d.ScreenBuffers[2].Resident);
 assert(evict(&d,t012,3)==1 && made==2);
 /* M2-f11: slot 13 joins while the budget is full. Slots 10-12 are idle
  * (last used at fences 5, 3, 9); 11 is the least recently used. */
 d.ScreenBuffers[0].LastUseFence=5; d.ScreenBuffers[1].LastUseFence=3; d.ScreenBuffers[2].LastUseFence=9;
 uint64_t t3[1]={13}; refusals=1; made=0; evicted=0;
 assert(make_resident(&d,t3,1,&fence)==2 && fence==7 && made==2 && evicted==1);
 assert(last_evicted_count==1 && last_evicted[0]==0x101);
 assert(!d.ScreenBuffers[1].Resident && d.ScreenBuffers[0].Resident && d.ScreenBuffers[2].Resident && d.ScreenBuffers[3].Resident);
 assert(evict(&d,t3,1)==1);
 /* A slot held by an in-flight submission is never trimmed; a trimmed slot
  * named again is made resident again. */
 d.ScreenBuffers[3].LastUseFence=12;
 d.ScreenBuffers[0].SubmissionHolds=1; refusals=1; made=0; evicted=0;
 uint64_t t4[2]={14,11};
 assert(make_resident(&d,t4,2,&fence)==2 && made==2 && evicted==1 && last_evicted[0]==0x102);
 assert(d.ScreenBuffers[0].Resident && d.ScreenBuffers[1].Resident && d.ScreenBuffers[4].Resident && !d.ScreenBuffers[2].Resident);
 assert(evict(&d,t4,2)==1);
 /* A budget that never fits: everything idle is trimmed, the request fails,
  * its copy holds are released and the loop stops. */
 refusals=100; made=0; evicted=0;
 uint64_t t5[1]={15};
 assert(make_resident(&d,t5,1,&fence)==0);
 assert(!d.ScreenBuffers[5].Resident && !d.ScreenBuffers[5].CopyHeld && !d.ScreenBuffers[5].SubmissionHolds);
 assert(d.ScreenBuffers[0].Resident);   /* still held in flight */
 assert(made<=ADMISSION_UMD_TRIM_ROUNDS+1u && evicted>=1);
 puts("EXP978 persistent residency: PASS");
}
'''


TRIM_RACE = r"""
static ADMISSION_UMD_DEVICE *racing_device;
static void submit_during_evict() {
  uint64_t token=10, fence=0;
  assert(racing_device->ScreenBufferLock==0);
  /* Schedule a second caller after trim's selection lock is released but
   * before EvictCb returns. Execute the production submission admission. */
  assert(make_resident(racing_device,&token,1,&fence)==0 &&
         "trim selected a slot that a concurrent submission can still hold");
  assert(!racing_device->ScreenBuffers[0].SubmissionHolds);
  UINT count=99;
  assert(trim_residency(racing_device,1,&count)==0 && count==0);
}
int main() {
  CB cb={mk,ev}; ADMISSION_UMD_DEVICE d={};
  d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.PagingQueue=(HANDLE)1;
  d.KernelCallbacks=&cb; racing_device=&d;
  SLOT &s=d.ScreenBuffers[0];
  s.Active=TRUE; s.Resident=TRUE; s.Token=10; s.KernelAllocation=0x100;
  s.StagingAllocation=0x200; s.CanonicalGpuVa=0x10000; s.Bytes=32u<<20;
  uint64_t token=10, fence=0;
  UINT count=0;
  /* The other ordering: a submission holds the slot before selection. */
  assert(make_resident(&d,&token,1,&fence)==1);
  assert(trim_residency(&d,1,&count)==0 && count==0);
  assert(evict(&d,&token,1));
  before_evict=submit_during_evict;
  /* A refused eviction releases the reservation without losing residency. */
  evict_result=E_FAIL;
  assert(trim_residency(&d,1,&count)==0 && count==0);
  assert(s.Resident && !s.Transition && !s.SubmissionHolds);
  assert(make_resident(&d,&token,1,&fence)==1 && made==0);
  assert(evict(&d,&token,1));
  /* A successful eviction releases the reservation with Resident cleared. */
  evict_result=S_OK;
  assert(trim_residency(&d,1,&count)==(32ull<<20) && count==1);
  assert(!s.Resident && !s.Transition && !s.SubmissionHolds);
  assert(make_resident(&d,&token,1,&fence)==2 && fence==7 && made==1);
  assert(s.Resident && s.SubmissionHolds==1 && s.CopyHeld);
  assert(evict(&d,&token,1));
  assert(evicted==2);
  puts("trim reservation interleaving: PASS");
}
"""


class PersistentResidency(unittest.TestCase):
    def test_trim_reserves_slots_until_evict_finishes(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A trim candidate must not acquire a submission hold between selection
        and EvictCb completion; failure must release the reservation, and a
        later submission must restore a successfully trimmed slot.
        """
        self.run_replay(BODY[:BODY.index('int main(){')] + TRIM_RACE,
                        'trim reservation interleaving: PASS')

    def test_d3d12_no_trim_preserves_idle_residency(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The D3D12 opt-in must prevent autonomous eviction even of idle internal
        BOs, allow VidMm budget arbitration, and leave other APIs/default intact.
        Executes production trim and MakeResident retry code with fake VidMm.
        """
        main = r'''int main() {
          CB cb={mk,ev}; ADMISSION_UMD_DEVICE d={};
          d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.PagingQueue=(HANDLE)1;
          d.KernelCallbacks=&cb;
          for(unsigned api=0;api<2;++api) {
            d.BorrowedContext=api;
            const char *values[]={nullptr,"0","1","yes","10",""};
            for(const char *value:values) {
              if(value) setenv("APPLE_AGX_D3D12_DISABLE_TRIM",value,1);
              else unsetenv("APPLE_AGX_D3D12_DISABLE_TRIM");
              memset(d.ScreenBuffers,0,sizeof(d.ScreenBuffers));
              for(unsigned i=0;i<2;++i) {
                SLOT &s=d.ScreenBuffers[i];s.Active=TRUE;s.Token=10+i;
                s.KernelAllocation=0x100+i;s.StagingAllocation=0x200+i;
                s.CanonicalGpuVa=0x10000*(i+1);s.Bytes=65536;
              }
              SLOT &idle=d.ScreenBuffers[0];idle.Resident=TRUE;
              made=evicted=0;refusals=1;
              uint64_t token=11,fence=0;
              const bool disabled=api && value && !strcmp(value,"1");
              int result=make_resident(&d,&token,1,&fence);
              if(disabled) {
                assert(result==2 && made==2 && evicted==0);
                assert(idle.Resident && !idle.Transition);
                assert(d.ScreenBuffers[1].Resident && d.ScreenBuffers[1].SubmissionHolds);
                assert(evict(&d,&token,1));
              } else {
                assert(result==2 && made==2 && evicted==1);
                assert(!idle.Resident);
                assert(evict(&d,&token,1));
              }
            }
          }
          unsetenv("APPLE_AGX_D3D12_DISABLE_TRIM");
          puts("D3D12 trim discriminator: PASS");
        }'''
        self.run_replay(BODY[:BODY.index('int main(){')] + main,
                        'D3D12 trim discriminator: PASS')

    def test_slots_stay_resident_across_submits(self):
        self.run_replay(BODY, 'EXP978 persistent residency: PASS')

    def run_replay(self, template, success):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in
                          ('watch_slots', 'find_slot', 'release_copies', 'allocation_handle',
                           'translate_handles', 'evict', 'trim_residency',
                           'residency_request', 'make_resident'))
        funcs = funcs.replace('auto *slot', 'SLOT *slot')
        body = template.replace('@@FUNCS@@', '#define ADMISSION_UMD_WATCH_WINDOWS\n#include "umd_va_watch.h"\n' + funcs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(body)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined',
                                    '-I', str(ROOT / 'drivers/apple-agx/render-admission/umd/include'),
                                    str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn(success, ran.stdout)


if __name__ == '__main__':
    unittest.main()
