"""EXP1004: make a slot resident before mapping its GPU VA.

EXP1001 leaf ring: VidMm wrote the PTEs of a fresh BO as invalid at Map time
(allocation not yet resident; the persistent MakeResident came later, at the
first submission) and never re-sent valid entries; EXP1003's scheduled touch
did not repair them either. Mapping an already-resident allocation lets VidMm
write valid PTEs as part of the Map paging operation.
Invariants:
- map_va issues MakeResident (and waits its paging fence) before
  MapGpuVirtualAddress for a slot without its persistent residency reference;
- the reference is recorded (slot Resident) so make_resident never takes a
  second persistent one; an already-resident slot gets no extra call;
- a MakeResident failure does not prevent the mapping (previous behaviour).
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
#include <cstring>
#include <string>
#include "umd_va_alias.h"
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_PENDING ((HRESULT)0x8000000Au)
#define E_FAIL ((HRESULT)0x80004005u)
#define SUCCEEDED(h) ((h)>=0)
#define FAILED(h) ((h)<0)
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 4u
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) ADMISSION_UMD_SCREEN_BUFFER_LIMIT
#define ADMISSION_UMD_DEVICE_MAGIC 0x55u
#define AGX_GPUVA_MAP_WRITE 1u
#define AGX_GPUVA_MAP_EXECUTE 2u
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
static void AcquireSRWLockShared(SRWLOCK*){} static void ReleaseSRWLockShared(SRWLOCK*){}
struct D3DDDI_MAKERESIDENT { D3DKMT_HANDLE hPagingQueue; UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; const UINT *PriorityList; ULONGLONG PagingFenceValue; };
struct PROT { UINT Write:1; UINT Execute:1; };
struct D3DDDI_MAPGPUVIRTUALADDRESS { D3DKMT_HANDLE hPagingQueue; ULONGLONG BaseAddress; D3DKMT_HANDLE hAllocation; ULONGLONG OffsetInPages, SizeInPages; PROT Protection; ULONGLONG VirtualAddress, PagingFenceValue; };
struct SLOT { uint64_t Token; BOOL Active,Transition,Resident; D3DKMT_HANDLE KernelAllocation; uint64_t CanonicalGpuVa; };
typedef SLOT ADMISSION_UMD_SCREEN_BUFFER;
struct HANDLE_ { void *handle; };
struct CB { HRESULT (*pfnMakeResidentCb)(void*, D3DDDI_MAKERESIDENT*); HRESULT (*pfnMapGpuVirtualAddressCb)(void*, D3DDDI_MAPGPUVIRTUALADDRESS*); };
struct ADMISSION_UMD_DEVICE { UINT Magic; SLOT ScreenBuffers[4]; SRWLOCK ScreenBufferLock; CB *KernelCallbacks; HANDLE_ RuntimeDevice; D3DKMT_HANDLE PagingQueue; BOOL ScreenClosing,DrawTerminal; uint64_t PagingWaitToken; ADMISSION_UMD_VA_ALIAS VaAliases[4]; UINT VaAliasCount, VaAliasCapacity; };
static int alias_reserve(ADMISSION_UMD_DEVICE *d, unsigned need) { d->VaAliasCapacity=4; return need<=4; }
static std::string order; static HRESULT make_hr=E_PENDING;
static HRESULT make(void*, D3DDDI_MAKERESIDENT *r){ order+="R"; r->PagingFenceValue=9; return make_hr; }
static HRESULT map(void*, D3DDDI_MAPGPUVIRTUALADDRESS *m){ order+="M"; m->VirtualAddress=m->BaseAddress; m->PagingFenceValue=10; return E_PENDING; }
static int wait_paging(void*, uint64_t f){ order+="W"; return f==9 || f==10; }
static int wait_paging_at(ADMISSION_UMD_DEVICE *d, uint64_t f, UINT, uint64_t){ return wait_paging(d,f); }
static void va_record(const void*, UINT, ULONGLONG, D3DKMT_HANDLE, ULONGLONG, ULONGLONG, HRESULT) {}
static void AdmissionUmdDiagnostic(const char*, HRESULT, const UINT*, UINT) {}
/* residency_request (M2-f11) is one pfnMakeResidentCb call whenever VidMm
 * does not refuse for budget, which this replay never does; the trim loop has
 * its own replay (test_umd_persistent_residency). */
static HRESULT residency_request(ADMISSION_UMD_DEVICE *device, D3DDDI_MAKERESIDENT *request,
    D3DKMT_HANDLE *handles, UINT count, UINT *priorities) {
  memset(request, 0, sizeof(*request));
  request->hPagingQueue = device->PagingQueue; request->NumAllocations = count;
  request->AllocationList = handles; request->PriorityList = priorities;
  return device->KernelCallbacks->pfnMakeResidentCb(device->RuntimeDevice.handle, request);
}
@@FUNCS@@
int main(){
  CB cb={make,map}; ADMISSION_UMD_DEVICE d={}; d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.KernelCallbacks=&cb; d.PagingQueue=3;
  d.ScreenBuffers[0]={7,1,0,0,0x40,0};
  uint64_t fence=0;
  assert(map_va(&d,7,0x100000,16,AGX_GPUVA_MAP_WRITE,&fence)==2);
  if(order!="RWM"){printf("expected resident-then-map, got %s\n",order.c_str());return 1;}
  assert(d.ScreenBuffers[0].Resident && fence==10);
  order.clear(); assert(map_va(&d,7,0x200000,16,0,&fence)==2 && order=="M");
  d.ScreenBuffers[1]={8,1,0,0,0x41,0}; make_hr=E_FAIL; order.clear();
  assert(map_va(&d,8,0x300000,16,0,&fence)==2 && order=="RM" && !d.ScreenBuffers[1].Resident);
  puts("EXP1004 resident before map: PASS");
}
'''


class ResidentBeforeMap(unittest.TestCase):
    def test_accepted_residency_must_complete_before_mapping(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A failed/missing paging fence must not become a Resident fast path:
        neither canonical nor alias mapping may proceed or retry GPU use.
        A successful request carrying a fence must also wait for it.
        """
        body = BODY[:BODY.index('int main()')]
        body = body.replace('static HRESULT make_hr=E_PENDING;',
            'static HRESULT make_hr=E_PENDING; static uint64_t returned_fence=9; static bool fail_wait=false;')
        body = body.replace('r->PagingFenceValue=9;', 'r->PagingFenceValue=returned_fence;')
        body = body.replace('return f==9 || f==10;', 'return !fail_wait && (f==9 || f==10);')
        body += r'''
int main() {
  for (unsigned alias=0; alias<2; ++alias)
  for (unsigned scenario=0; scenario<5; ++scenario) {
    CB cb={make,map}; ADMISSION_UMD_DEVICE d={};
    d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.KernelCallbacks=&cb; d.PagingQueue=3;
    d.ScreenBuffers[0]={7,1,0,0,0x40,0};
    make_hr=scenario>=2 ? S_OK : E_PENDING;
    returned_fence=scenario==1 || scenario==4 ? 0 : 9;
    fail_wait=scenario==0 || scenario==2;
    order.clear(); uint64_t fence=0;
    const int result=alias ? map_va_at(&d,7,0x100000,16,16,AGX_GPUVA_MAP_WRITE,&fence)
                           : map_va(&d,7,0x100000,16,AGX_GPUVA_MAP_WRITE,&fence);
    if(scenario<3) {
      assert(result==3 && "accepted residency without completion must retain uncertain state");
      assert(order==(scenario==1 ? "R":"RW") && "must not map after paging failure");
      assert(d.DrawTerminal && d.ScreenBuffers[0].Resident);
      assert(!d.ScreenBuffers[0].CanonicalGpuVa && !d.VaAliasCount && !fence);
      order.clear();
      assert(!map_va(&d,7,0x100000,16,0,&fence) && order.empty());
      assert(!map_va_at(&d,7,0x200000,0,16,0,&fence) && order.empty());
    } else {
      assert(result==2 && !d.DrawTerminal && d.ScreenBuffers[0].Resident);
      assert(order==(scenario==3 ? "RWM":"RM"));
    }
  }
  puts("EXP1004 resident before map: PASS");
}
'''
        self.run_replay(body)

    def test_map_after_residency(self):
        self.run_replay(BODY)

    def run_replay(self, body):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in ('find_slot', 'allocation_handle', 'resident_before_map', 'map_va', 'map_va_at'))
        self.assertIn('resident_before_map', funcs, 'resident-before-map is missing')
        funcs = funcs.replace('auto *slot', 'SLOT *slot')
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(body.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', '-I', str(SRC.parent), str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1004 resident before map: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
