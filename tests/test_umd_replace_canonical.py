"""EXP1011: replace a canonical allocation whose MakeResident VidMm skipped.

EXP1009 DxgKrnl ETW: for ~1% of fresh canonical allocations VidMm completed
MakeResident as a no-op (no fault/placement/page-in) and the Map wrote no
PTEs; EXP1003/1004/1006/1010 showed touching, re-mapping and cycling the same
allocation never repairs it. A replacement allocation at the same GPU VA does.
Invariants:
- order: allocate fresh -> MakeResident(fresh) -> wait -> Map(fresh, same VA);
- only after a successful map the slot owns the fresh handle, is Resident,
  its staging/chunk records are invalidated and the old handle is freed;
- any failure frees the fresh handle and leaves the slot untouched;
- direct and borrowed (shared) slots are never replaced.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
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
typedef unsigned long long ULONGLONG; typedef uint64_t APPLE_AGX_U64; typedef unsigned APPLE_AGX_U32;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_PENDING ((HRESULT)0x8000000Au)
#define E_FAIL ((HRESULT)0x80004005u)
#define SUCCEEDED(h) ((h)>=0)
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
enum { AppleAgxWin32BufferGpuWrite = 4u };
enum { AgxWin32BufferClassShader = 3u };
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
struct D3DDDI_MAKERESIDENT { D3DKMT_HANDLE hPagingQueue; UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; const UINT *PriorityList; ULONGLONG PagingFenceValue; };
struct PROT { UINT Write:1; UINT Execute:1; };
struct D3DDDI_MAPGPUVIRTUALADDRESS { D3DKMT_HANDLE hPagingQueue; ULONGLONG BaseAddress; D3DKMT_HANDLE hAllocation; ULONGLONG OffsetInPages, SizeInPages; PROT Protection; ULONGLONG VirtualAddress, PagingFenceValue; };
struct SYNC { BOOL Valid; }; struct CHUNKS { BOOL Valid; };
struct SLOT { uint64_t Token; BOOL Direct,Borrowed,Resident; UINT ClassId,Flags; uint64_t Bytes;
  D3DKMT_HANDLE KernelAllocation; uint64_t CanonicalGpuVa; SYNC Sync; CHUNKS Chunks; };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct HANDLE_ { void *handle; };
struct CB { HRESULT (*pfnMakeResidentCb)(void*, D3DDDI_MAKERESIDENT*); HRESULT (*pfnMapGpuVirtualAddressCb)(void*, D3DDDI_MAPGPUVIRTUALADDRESS*); };
struct ADMISSION_UMD_DEVICE { SRWLOCK ScreenBufferLock; CB *KernelCallbacks; HANDLE_ RuntimeDevice; D3DKMT_HANDLE PagingQueue; };
static std::string order; static HRESULT new_hr=S_OK, make_hr=E_PENDING; static uint64_t map_va_out_delta;
static D3DKMT_HANDLE freed[4]; static int nfreed;
static HRESULT AdmissionUmdScreenNewCanonical(ADMISSION_UMD_DEVICE*, APPLE_AGX_U32, APPLE_AGX_U32, APPLE_AGX_U64 b, D3DKMT_HANDLE *a){
  order+="N"; assert(b); if(new_hr<0) return new_hr; *a=0x99; return S_OK; }
static HRESULT AdmissionUmdScreenFreeAllocation(ADMISSION_UMD_DEVICE*, D3DKMT_HANDLE a){ order+="F"; freed[nfreed++]=a; return S_OK; }
static HRESULT make(void*, D3DDDI_MAKERESIDENT *r){ order+="R"; assert(r->NumAllocations==1 && r->AllocationList[0]==0x99); r->PagingFenceValue=9; return make_hr; }
static HRESULT map(void*, D3DDDI_MAPGPUVIRTUALADDRESS *m){ order+="M"; assert(m->hAllocation==0x99);
  m->VirtualAddress=m->BaseAddress+map_va_out_delta; m->PagingFenceValue=10; return E_PENDING; }
static int wait_paging(ADMISSION_UMD_DEVICE*, uint64_t f){ order+="W"; return f==9 || f==10; }
static void va_record(const void*, UINT, ULONGLONG, D3DKMT_HANDLE, ULONGLONG, ULONGLONG, HRESULT) {}
static void AdmissionUmdVaRecordDeallocate(ADMISSION_UMD_DEVICE*, uint64_t, D3DKMT_HANDLE, uint64_t, HRESULT) {}
static void AdmissionUmdStagingInvalidate(SYNC *s){ s->Valid=FALSE; }
static void AdmissionUmdStagingChunksInvalidate(CHUNKS *c){ c->Valid=FALSE; }
static void AdmissionUmdDiagnostic(const char*, HRESULT, const UINT*, UINT) {}
@@FUNCS@@
static SLOT fresh_slot(){ SLOT s={}; s.Token=7; s.Flags=AppleAgxWin32BufferGpuWrite; s.Bytes=0x30000;
  s.KernelAllocation=0x40; s.CanonicalGpuVa=0x1000000; s.Sync.Valid=s.Chunks.Valid=TRUE; return s; }
int main(){
  CB cb={make,map}; ADMISSION_UMD_DEVICE d={}; d.KernelCallbacks=&cb; d.PagingQueue=3;
  SLOT s=fresh_slot();
  assert(replace_canonical(&d,&s)==1);
  if(order!="NRWMWF"){printf("expected alloc,resident,wait,map,wait,free-old; got %s\n",order.c_str());return 1;}
  assert(s.KernelAllocation==0x99 && s.Resident && !s.Sync.Valid && !s.Chunks.Valid && nfreed==1 && freed[0]==0x40);
  /* Map at a different VA: fresh freed, slot untouched. */
  s=fresh_slot(); order.clear(); nfreed=0; map_va_out_delta=0x10000;
  assert(replace_canonical(&d,&s)==0 && order=="NRWMF" && freed[0]==0x99);
  assert(s.KernelAllocation==0x40 && !s.Resident && s.Sync.Valid && s.Chunks.Valid);
  map_va_out_delta=0;
  /* MakeResident failure: no map, fresh freed. */
  s=fresh_slot(); order.clear(); nfreed=0; make_hr=E_FAIL;
  assert(replace_canonical(&d,&s)==0 && order=="NRF" && freed[0]==0x99 && s.KernelAllocation==0x40);
  make_hr=E_PENDING;
  /* Allocation failure: nothing to free. */
  s=fresh_slot(); order.clear(); nfreed=0; new_hr=E_FAIL;
  assert(replace_canonical(&d,&s)==0 && order=="N" && nfreed==0); new_hr=S_OK;
  /* Direct and borrowed slots are never replaced. */
  s=fresh_slot(); s.Direct=1; order.clear(); assert(replace_canonical(&d,&s)==0 && order.empty());
  s=fresh_slot(); s.Borrowed=1; assert(replace_canonical(&d,&s)==0 && order.empty());
  puts("EXP1011 replace canonical: PASS");
}
'''


class ReplaceCanonical(unittest.TestCase):
    def test_replace_order_and_rollback(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in ('map_canonical_as', 'replace_canonical'))
        self.assertIn('replace_canonical', funcs, 'canonical replacement is missing')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1011 replace canonical: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
