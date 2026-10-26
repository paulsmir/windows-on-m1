"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

RC25 must eliminate the actual private VirtualAlloc, not merely report a direct
policy. Replay the whole allocation function with real wire validation and OS
callbacks, including T13 fallback, borrowed/GPU-only/internal cases and cleanup failures.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/apple-agx'

HARNESS = r'''
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
extern "C" {
#include "render_win32_transport.h"
#include "apple_agx_win32_device_info.h"
}
using UINT=unsigned;using BOOL=int;using BYTE=unsigned char;using SIZE_T=size_t;
using HRESULT=int;using D3DKMT_HANDLE=unsigned;
#define APPLE_AGX_GPUVA_WINSYS 1
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_FAIL ((int)0x80004005u)
#define E_OUTOFMEMORY ((int)0x8007000eu)
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
#define MAXUINT32 0xffffffffu
#define ADMISSION_UMD_DEVICE_MAGIC 55u
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 4u
#define MEM_COMMIT 1u
#define MEM_RESERVE 2u
#define MEM_WRITE_WATCH 4u
#define MEM_RELEASE 8u
#define PAGE_READWRITE 1u
#define D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE
#define D3DDDIFMT_A8 ADMISSION_WIN32_ALLOCATION_FORMAT_A8
#define ZeroMemory(p,n) memset(p,0,n)
#define ARRAYSIZE(a) (sizeof(a)/sizeof(a[0]))
#define UNREFERENCED_PARAMETER(x) ((void)(x))
struct D3DDDI_ALLOCATIONINFO {void *pPrivateDriverData;UINT PrivateDriverDataSize,VidPnSourceId,hAllocation;};
struct D3DDDICB_ALLOCATE {void *hResource;UINT NumAllocations;D3DDDI_ALLOCATIONINFO *pAllocationInfo;};
struct D3DDDICB_DEALLOCATE {UINT NumAllocations;const UINT *HandleList;};
struct CB {HRESULT (*pfnAllocateCb)(void*,D3DDDICB_ALLOCATE*);HRESULT (*pfnDeallocateCb)(void*,D3DDDICB_DEALLOCATE*);};
struct ADMISSION_UMD_SCREEN_BUFFER {
 APPLE_AGX_U64 Token,Serial,Bytes,Alignment;
 BOOL Active,Transition,Borrowed,WriteWatch,HeapStaging,SystemDirect;
 UINT KernelAllocation,StagingAllocation,ClassId,Flags;
 BYTE *PrivateStaging;
};
struct ADMISSION_UMD_DEVICE {
 UINT Magic;CB *KernelCallbacks;struct {void *handle;} RuntimeDevice;
 int ScreenBufferLock;BOOL ScreenClosing,DrawTerminal,HeapStaging,DirectCpuPool;
 APPLE_AGX_U64 NextScreenToken,NextScreenSerial;
 HRESULT LastScreenError;ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[4];
};
static void AcquireSRWLockShared(int *l){assert(!*l);*l=1;}
static void ReleaseSRWLockShared(int *l){assert(*l);*l=0;}
static void AcquireSRWLockExclusive(int *l){assert(!*l);*l=1;}
static void ReleaseSRWLockExclusive(int *l){assert(*l);*l=0;}
static AGX_WIN32_BUFFER_CLASS_INFO cls;
static const AGX_WIN32_BUFFER_CLASS_INFO *AdmissionUmdScreenClass(ADMISSION_UMD_DEVICE *,UINT id) {
 cls={};cls.ClassId=id;cls.MaximumBytes=256ull<<20;cls.MinimumAlignment=65536;cls.Flags=0x3f;return &cls;
}
static ADMISSION_UMD_SCREEN_BUFFER *AdmissionUmdScreenFreeSlot(ADMISSION_UMD_DEVICE *d){return &d->ScreenBuffers[0];}
static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,size_t){}
static unsigned allocs,frees,private_allocs,refusals;
static bool fail_allocate,fail_private,fail_release;
static ADMISSION_ALLOCATION_DESCRIPTION parsed;
static HRESULT allocate(void*,D3DDDICB_ALLOCATE *r) {
 ++allocs;assert(r->NumAllocations==1);
 UINT cls,flags;
 assert(AdmissionWin32AllocationCreateValidateEx(r->pAllocationInfo->pPrivateDriverData,
     r->pAllocationInfo->PrivateDriverDataSize,&parsed,&cls,&flags,256ull<<20)==AdmissionWin32TransportSuccess);
 r->pAllocationInfo->hAllocation=42;return fail_allocate?E_OUTOFMEMORY:S_OK;
}
static HRESULT deallocate(void*,D3DDDICB_DEALLOCATE *r){++frees;assert(r->NumAllocations==1 && *r->HandleList==42);return fail_release?E_FAIL:S_OK;}
static void *VirtualAlloc(void*,SIZE_T,UINT,UINT){++private_allocs;return fail_private?nullptr:calloc(1,1);}
static int VirtualFree(void *p,SIZE_T,UINT){free(p);return 1;}
static void AdmissionUmdScreenMemoryRefusal(ADMISSION_UMD_DEVICE*,HRESULT,APPLE_AGX_U64,UINT,UINT,APPLE_AGX_U64,UINT,UINT){++refusals;}
@@FUNCTION@@
int main() {
 CB cb={allocate,deallocate};
 for(unsigned mode=0;mode<3;++mode) for(unsigned kind=0;kind<4;++kind) {
  ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;d.KernelCallbacks=&cb;
  d.HeapStaging=mode!=0;d.DirectCpuPool=mode==2;
  UINT flags=kind==3?0x2cu:kind==2?0x6u:0xfu,borrowed=kind==1?7u:0u;
  allocs=frees=private_allocs=refusals=0;APPLE_AGX_U64 token=0;
  assert(AdmissionUmdScreenCreateClassBufferImpl(&d,kind==2?2:1,kind==2?65536:32ull<<20,65536,flags,&token,borrowed));
  auto &s=d.ScreenBuffers[0];bool staged=!borrowed && mode==1 && (kind==0 || kind==2);
  assert(token && allocs==1 && !frees && !refusals && private_allocs==unsigned(staged));
  assert(bool(s.PrivateStaging)==staged && bool(s.HeapStaging)==staged);
  assert(bool(s.SystemDirect)==(!borrowed && !staged));
  assert(s.KernelAllocation==42 && s.StagingAllocation==borrowed);
  assert(parsed.CpuVisible==unsigned(!borrowed && !staged && kind!=3));
  free(s.PrivateStaging);
 }
 // A direct allocation has no private-memory failure point at all.
 ADMISSION_UMD_DEVICE d={};d.Magic=ADMISSION_UMD_DEVICE_MAGIC;d.KernelCallbacks=&cb;
 d.HeapStaging=d.DirectCpuPool=TRUE;fail_private=true;APPLE_AGX_U64 token=0;
 private_allocs=0;
 assert(AdmissionUmdScreenCreateClassBufferImpl(&d,1,65536,65536,0xf,&token,0));
 assert(!private_allocs && !d.ScreenBuffers[0].PrivateStaging);
 // AllocateCb refusal with a returned handle is rolled back; a failed
 // rollback keeps the exact owner and terminal state, not a lost allocation.
 for(unsigned retained=0;retained<2;++retained) {
  d.ScreenBuffers[0]={};fail_allocate=true;fail_release=retained;allocs=frees=refusals=0;
  assert(!AdmissionUmdScreenCreateClassBufferImpl(&d,1,65536,65536,0xf,&token,0));
  assert(!token && allocs==1 && frees==1 && refusals==1 && !private_allocs);
  assert(bool(d.ScreenBuffers[0].KernelAllocation)==bool(retained));
  assert(bool(d.DrawTerminal)==bool(retained));
 }
}
'''


class CpuPoolAllocation(unittest.TestCase):
    def test_actual_allocation_has_no_private_copy(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Opt-in allocation must never commit a duplicate CPU copy, including when private memory allocation would fail."""
        source = (DRIVER/'render-admission/umd/src/umd_win32_screen.c').read_text()
        code = HARNESS.replace('@@FUNCTION@@', function(source, 'AdmissionUmdScreenCreateClassBufferImpl'))
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp)/'allocation.cpp', Path(tmp)/'allocation'
            src.write_text(code)
            includes = ['-I', str(DRIVER/'shared/include'), '-I', str(DRIVER/'render-admission/include')]
            objects = []
            for unit in [DRIVER/'render-admission/src/render_win32_transport.c',
                         DRIVER/'render-admission/src/render_allocation.c', DRIVER/'shared/src/apple_agx_win32_abi.c']:
                obj = Path(tmp)/(unit.stem+'.o')
                compiled = subprocess.run(['clang', '-std=c11', '-include', str(ROOT/'tests/d3d12_wait_disabled_fixture.h'), '-fsanitize=address,undefined',
                                           *includes, '-c', str(unit), '-o', str(obj)], capture_output=True, text=True)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                objects.append(str(obj))
            cmd = ['clang++', '-std=c++17', '-include', str(ROOT/'tests/d3d12_wait_disabled_fixture.h'), '-fsanitize=address,undefined',
                   *includes, str(src), *objects, '-o', str(exe)]
            built = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
