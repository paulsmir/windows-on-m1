"""EXP1124: releasing a borrowed surface must not force VidMm to page it out.

DWM opens each application surface as a borrowed Direct screen buffer and
keeps one persistent residency reference (EXP978).  Releasing the buffer
called EvictCb with EvictOnlyIfNecessary clear, which tells VidMm the
allocation is finished: VidMm copied every 2.4 MiB Notepad surface to system
memory (LOCAL_TO_SYSTEM, 18 paging passes) ~35 ms after creation and a few
ms before dxgkrnl destroyed it, ~37 times per second (EXP1123/EXP1124
census + Wom1AllocLife1123).  D3DDDI_EVICT_FLAGS: EvictOnlyIfNecessary
drops the reference and lets VidMm evict only under memory pressure.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c"


def function(text, name):
    m = re.search(r"static [A-Za-z0-9_ *]+?\b" + name + r"\([^;{]*\)\s*\{", text)
    if not m:
        raise AssertionError("missing " + name)
    start = text.index("{", m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}"); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG, APPLE_AGX_U64; typedef void *HANDLE;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define FAILED(x) ((x)<0)
#define APPLE_AGX_GPUVA_WINSYS 1
#define MEM_RELEASE 0x8000u
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 4u
#define ZeroMemory(p,n) memset((p),0,(n))
typedef int SRWLOCK;
static void AcquireSRWLockExclusive(SRWLOCK*){} static void ReleaseSRWLockExclusive(SRWLOCK*){}
static int VirtualFree(void*,size_t,unsigned){return 1;}
struct ADMISSION_UMD_SCREEN_BUFFER { BOOL Active,Mapped,Transition,CopyHeld,Direct,Resident,Borrowed,SystemDirect;
  APPLE_AGX_U64 Token; void *NativeBo; UINT SourceHolds,SubmissionHolds;
  D3DKMT_HANDLE KernelAllocation,StagingAllocation; ULONGLONG CanonicalGpuVa; unsigned char *PrivateStaging; };
struct D3DDDI_EVICT_FLAGS { union { struct { UINT EvictOnlyIfNecessary:1; UINT NotWrittenTo:1; UINT Reserved:30; }; UINT Value; }; };
struct D3DDDICB_EVICT { D3DDDI_EVICT_FLAGS Flags; UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; ULONGLONG NumBytesToTrim; };
struct D3DDDICB_DEALLOCATE { HANDLE hResource; UINT NumAllocations; const D3DKMT_HANDLE *HandleList; };
static unsigned evicts, onlyIfNecessary, notWrittenTo, deallocations;
static HRESULT ev(HANDLE,const D3DDDICB_EVICT*e){++evicts;onlyIfNecessary+=e->Flags.EvictOnlyIfNecessary;notWrittenTo+=e->Flags.NotWrittenTo;return S_OK;}
static HRESULT de(HANDLE,const D3DDDICB_DEALLOCATE*){++deallocations;return S_OK;}
struct CB { HRESULT(*pfnEvictCb)(HANDLE,const D3DDDICB_EVICT*); HRESULT(*pfnDeallocateCb)(HANDLE,const D3DDDICB_DEALLOCATE*); };
struct ADMISSION_UMD_DEVICE { ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[4]; SRWLOCK ScreenBufferLock; BOOL DrawTerminal;
  CB *KernelCallbacks; struct { HANDLE handle; } RuntimeDevice; HRESULT LastScreenError; };
static void AdmissionUmdVaRecordDeallocate(const void*,ULONGLONG,D3DKMT_HANDLE,ULONGLONG,HRESULT){}
@@FUNCS@@
int main(){
  CB cb={ev,de}; ADMISSION_UMD_DEVICE d={}; d.KernelCallbacks=&cb;
  ADMISSION_UMD_SCREEN_BUFFER &b=d.ScreenBuffers[0];
  b.Active=1;b.Token=7;b.Direct=1;b.Borrowed=1;b.Resident=1;b.KernelAllocation=0x40;
  assert(AdmissionUmdScreenDestroyBuffer(&d,7)==1);
  /* One reference drop, no forced page-out, contents kept for the owner. */
  assert(evicts==1 && onlyIfNecessary==1 && notWrittenTo==0);
  /* A borrowed Direct allocation is never deallocated by the borrower. */
  assert(deallocations==0 && !d.ScreenBuffers[0].Active);
  return 0;
}
'''


class BorrowedReleaseEvictTests(unittest.TestCase):
    def test_release_drops_residency_without_forcing_eviction(self):
        text = SRC.read_text()
        funcs = function(text, "AdmissionUmdScreenFind") + "\n" + \
            function(text, "AdmissionUmdScreenDestroyBuffer")
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "t.cpp"; binary = Path(tmp) / "t"
            source.write_text(BODY.replace("@@FUNCS@@", funcs))
            subprocess.run(["clang++", "-std=c++17", "-w", str(source), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
