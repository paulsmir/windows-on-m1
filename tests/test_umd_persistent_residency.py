"""EXP978: UMD residency is a persistent per-slot reference.

EXP977 ETW showed VidMm moving ~10.7 GB local->system and ~10.5 GB back in 30 s:
the UMD called pfnMakeResidentCb before every submit and pfnEvictCb after it,
so idle trimming paged the whole working set out and in at the frame rate.
Invariant: a slot takes exactly one MakeResident reference in its lifetime;
ending a submission only releases its copy holds; later submissions call
MakeResident only for slots that are not resident yet.
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
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 16u
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) ADMISSION_UMD_SCREEN_BUFFER_LIMIT
#define ADMISSION_UMD_DEVICE_MAGIC 0x55u
static void* GetProcessHeap(){return nullptr;}
static void* HeapAlloc(void*,unsigned,size_t n){return calloc(1,n);}
static void HeapFree(void*,unsigned,void*p){free(p);}
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
static void AcquireSRWLockShared(SRWLOCK*){} static void ReleaseSRWLockShared(SRWLOCK*){}
struct D3DDDI_MAKERESIDENT { HANDLE hPagingQueue; UINT NumAllocations; const D3DKMT_HANDLE*AllocationList; const UINT*PriorityList; ULONGLONG PagingFenceValue; };
struct SLOT { uint64_t Token; BOOL SystemDirect,Active,Transition,CopyHeld,Direct,Mapped,Resident; UINT SourceHolds,SubmissionHolds;
  D3DKMT_HANDLE KernelAllocation,StagingAllocation; unsigned char *PrivateStaging; uint64_t CanonicalGpuVa; void*NativeBo; void*NativeMapRelease; };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
static unsigned made, evicted, made_handles;
static HRESULT mk(HANDLE,D3DDDI_MAKERESIDENT*r){++made;made_handles+=r->NumAllocations;r->PagingFenceValue=7;return E_PENDING;}
struct D3DDDICB_EVICT { UINT Flags; UINT NumAllocations; const D3DKMT_HANDLE*AllocationList; ULONGLONG NumBytesToTrim; };
static HRESULT ev(HANDLE,const D3DDDICB_EVICT*){++evicted;return S_OK;}
struct CB { HRESULT(*pfnMakeResidentCb)(HANDLE,D3DDDI_MAKERESIDENT*); HRESULT(*pfnEvictCb)(HANDLE,const D3DDDICB_EVICT*); };
struct ADMISSION_UMD_DEVICE { UINT Magic; SLOT ScreenBuffers[16]; SRWLOCK ScreenBufferLock; BOOL DrawTerminal,ScreenClosing; HANDLE PagingQueue; CB*KernelCallbacks; struct{HANDLE handle;}RuntimeDevice; };
static void va_record(const void*,UINT,unsigned long long,D3DKMT_HANDLE,unsigned long long,unsigned long long,HRESULT){}
@@FUNCS@@
int main(){
 CB cb={mk,ev}; ADMISSION_UMD_DEVICE d={}; d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.PagingQueue=(HANDLE)1; d.KernelCallbacks=&cb;
 for(unsigned i=0;i<3;++i){SLOT&s=d.ScreenBuffers[i];s.Active=1;s.Token=10+i;s.KernelAllocation=0x100+i;s.StagingAllocation=0x200+i;s.CanonicalGpuVa=0x10000*(i+1);}
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
 puts("EXP978 persistent residency: PASS");
}
'''


class PersistentResidency(unittest.TestCase):
    def test_slots_stay_resident_across_submits(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in
                          ('find_slot', 'release_copies', 'allocation_handle',
                           'translate_handles', 'evict', 'make_resident'))
        funcs = funcs.replace('auto *slot', 'SLOT *slot')
        body = BODY.replace('@@FUNCS@@', funcs)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(body)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP978 persistent residency: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
