"""EXP1010: cycle a slot's residency to zero before re-requesting it.

EXP1009 DxgKrnl trace: for failing slots VidMm completed MakeResident in
~40 us with no NOT_RESIDENT fault, placement, fill or page-in, while working
slots of the same process got all of them. An extra reference (EXP1000)
changed nothing. Invariant: the persistent reference is evicted first (to
zero), then MakeResident is issued, the slot's Resident flag tracks the
reference, and a pending paging fence is waited.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

SRC = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_PENDING ((HRESULT)0x8000000Au)
#define E_FAIL ((HRESULT)0x80004005u)
#define FAILED(h) ((h)<0)
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
struct D3DDDI_MAKERESIDENT { D3DKMT_HANDLE hPagingQueue; UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; const UINT *PriorityList; ULONGLONG PagingFenceValue; };
struct D3DDDICB_EVICT { UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; };
struct SLOT { uint64_t Token; D3DKMT_HANDLE KernelAllocation; BOOL Resident; };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct HANDLE_ { void *handle; };
struct CB { HRESULT (*pfnEvictCb)(void*, D3DDDICB_EVICT*); HRESULT (*pfnMakeResidentCb)(void*, D3DDDI_MAKERESIDENT*); };
struct ADMISSION_UMD_DEVICE { CB *KernelCallbacks; HANDLE_ RuntimeDevice; D3DKMT_HANDLE PagingQueue; SRWLOCK ScreenBufferLock; };
static std::string order; static HRESULT evict_hr=S_OK;
static HRESULT evict(void*, D3DDDICB_EVICT *e){ order+="E"; assert(e->NumAllocations==1 && e->AllocationList[0]==0x40); return evict_hr; }
static HRESULT make(void*, D3DDDI_MAKERESIDENT *r){ order+="R"; r->PagingFenceValue=9; return E_PENDING; }
static int wait_paging(void*, uint64_t f){ order+="W"; return f==9; }
static void va_record(const void*, UINT, ULONGLONG, D3DKMT_HANDLE, ULONGLONG, ULONGLONG, HRESULT) {}
static void AdmissionUmdDiagnostic(const char*, HRESULT, const UINT*, UINT) {}
@@FUNC@@
int main(){
  CB cb={evict,make}; ADMISSION_UMD_DEVICE d={&cb,{(void*)1},3,0};
  SLOT s={1,0x40,TRUE};
  assert(cycle_residency(&d,&s)==1);
  if(order!="ERW"){printf("expected evict, make resident, wait; got %s\n",order.c_str());return 1;}
  assert(s.Resident);
  order.clear(); s.Resident=FALSE; assert(cycle_residency(&d,&s)==1 && order=="RW" && s.Resident);
  order.clear(); evict_hr=E_FAIL; assert(cycle_residency(&d,&s)==0 && order=="E" && s.Resident);
  puts("EXP1010 residency cycle: PASS");
}
'''


class ResidencyCycle(unittest.TestCase):
    def test_cycle(self):
        func = function(SRC.read_text(), 'cycle_residency')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'r.cpp'; exe = Path(tmp) / 'r'
            src.write_text(BODY.replace('@@FUNC@@', func))
            b = subprocess.run(['clang++', '-std=c++17', '-Wall', '-fsanitize=address,undefined', str(src), '-o', str(exe)], text=True, capture_output=True)
            self.assertEqual(b.returncode, 0, b.stdout + b.stderr)
            r = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn('EXP1010 residency cycle: PASS', r.stdout)


if __name__ == '__main__':
    unittest.main()
