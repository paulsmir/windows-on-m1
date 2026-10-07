"""EXP1000: a failed copy QUERY re-establishes residency once, then retries.

EXP997: explorer's failing QUERY (predicate57, PTE present but invalid for
1 s) named a reused BO mapped and made resident 36 s earlier, with only 172 MB
of 1 GiB local memory in use. WDDM guarantees a device's residency list only
while its contexts are scheduled (Residency overview), and a completed
MakeResident paging fence; the UMD takes its persistent reference once, so a
later demotion of an idle process is never undone before the CPU-time copy.
Invariants:
- QUERY failure -> MakeResident(allocation), wait its paging fence, retry once;
- the extra residency reference is always dropped with one Evict;
- a successful first QUERY makes no residency call;
- MakeResident failure leaves the QUERY failed and takes no reference.
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
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG;
#define S_OK 0
#define E_PENDING ((HRESULT)0x8000000Au)
#define E_FAIL ((HRESULT)0x80004005u)
#define SUCCEEDED(h) ((h)>=0)
#define FAILED(h) ((h)<0)
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define APPLE_AGX_G3_COPY_QUERY 0u
struct D3DDDI_MAKERESIDENT { D3DKMT_HANDLE hPagingQueue; UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; const UINT *PriorityList; ULONGLONG PagingFenceValue; };
struct D3DDDICB_EVICT { UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; };
struct APPLE_AGX_G3_COPY_REQUEST { UINT Operation; ULONGLONG Offset; UINT TransferBytes; ULONGLONG ProcessGeneration, MappingGeneration; };
struct HANDLE_ { void *handle; };
struct CB { HRESULT (*pfnMakeResidentCb)(void*, D3DDDI_MAKERESIDENT*); HRESULT (*pfnEvictCb)(void*, D3DDDICB_EVICT*); };
struct ADMISSION_UMD_DEVICE { CB *KernelCallbacks; HANDLE_ RuntimeDevice; D3DKMT_HANDLE PagingQueue; };
static int queries, valid_after, made, evicted, waits; static HRESULT make_hr=E_PENDING;
static int copy_escape(ADMISSION_UMD_DEVICE*, APPLE_AGX_G3_COPY_REQUEST *q) {
  ++queries; if (queries > valid_after) { q->ProcessGeneration = q->MappingGeneration = 1; return 1; } return 0; }
static HRESULT make(void*, D3DDDI_MAKERESIDENT *r) { ++made; assert(r->NumAllocations==1 && r->AllocationList[0]==0x40); r->PagingFenceValue = 7; return make_hr; }
static HRESULT evict(void*, D3DDDICB_EVICT *e) { ++evicted; assert(e->NumAllocations==1 && e->AllocationList[0]==0x40); return S_OK; }
static int wait_paging(void*, uint64_t f) { ++waits; return f == 7; }
static void va_record(const void*, UINT, ULONGLONG, D3DKMT_HANDLE, ULONGLONG, ULONGLONG, HRESULT) {}
static void AdmissionUmdDiagnostic(const char*, HRESULT, const UINT*, UINT) {}
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
@@FUNCS@@
static void reset(int after){queries=0;valid_after=after;made=evicted=waits=0;make_hr=E_PENDING;}
int main(){
  CB cb={make,evict}; ADMISSION_UMD_DEVICE d={&cb,{(void*)1},3}; APPLE_AGX_G3_COPY_REQUEST q={};
  bool held=false;
  reset(0); assert(query_canonical(&d,0x40,&q,&held) && queries==1 && made==0 && !held);
  reset(1); held=false;
  if(!query_canonical(&d,0x40,&q,&held)){puts("failed QUERY not retried after residency refresh");return 1;}
  assert(queries==2 && made==1 && waits==1 && held);
  residency_release(&d,0x40,&held); assert(evicted==1 && !held);
  residency_release(&d,0x40,&held); assert(evicted==1);
  reset(5); held=false; assert(!query_canonical(&d,0x40,&q,&held) && queries==2 && held);
  residency_release(&d,0x40,&held); assert(evicted==1);
  reset(5); make_hr=E_FAIL; held=false; assert(!query_canonical(&d,0x40,&q,&held) && queries==1 && !held);
  puts("EXP1000 query residency refresh: PASS");
}
'''


class QueryResidencyRefresh(unittest.TestCase):
    def test_failed_query_refreshes_residency_once(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in ('residency_refresh', 'residency_release', 'query_canonical'))
        self.assertIn('query_canonical', funcs, 'QUERY residency refresh is missing')
        funcs = re.sub(r'#if defined\(APPLE_AGX_EXP907_FRAME_RECEIPT\).*?#endif\n', '', funcs, flags=re.S)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', funcs))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1000 query residency refresh: PASS', ran.stdout)

    def test_transfer_slot_uses_refresh_and_releases(self):
        text = SRC.read_text()
        body = text[text.index('static int transfer_slot('):text.index('static int transfer_held(')]
        self.assertIn('query_canonical(', body)
        self.assertLess(body.index('residency_release('), body.index('HeapFree(GetProcessHeap(),0,payload);return success;'))


if __name__ == '__main__':
    unittest.main()
