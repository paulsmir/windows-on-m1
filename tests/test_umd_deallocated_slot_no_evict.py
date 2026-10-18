"""EXP1131: a slot must not evict an allocation the runtime already freed.

Closing Settings made DWM tear down and rebuild its D3D device (~1 s black
screen).  EXP1130 ETW: AgxD3d10WindowsPresentationDestroy deallocated the
presentation resource (AdmissionUmdDeallocateResource -> DeallocateCB), then
collect_presentations dropped the native BO, whose Direct screen slot still
held the dead handle as Resident.  AdmissionUmdScreenDestroyBuffer called
EvictCb on it and dxgkrnl marked the device as removed
(DxgkEvictInternal -> VidSchMarkDeviceAsError -> VidSchiNotifyDeviceRemoved);
DWM's composition thread then ran CD3DDevice::ProcessDeviceLost.  Destroying
an allocation already removes it from the device residency list, so the slot
must forget the handle when the runtime resource is deallocated.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCREEN = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c"
RUNTIME = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"


def function(text, name, required=True):
    m = re.search(r"^(?:static )?[A-Za-z0-9_ *]+?\b" + name + r"\([^;{]*\)\s*\{",
                  text, re.M)
    if not m:
        if required:
            raise AssertionError("missing " + name)
        return ""
    start = text.index("{", m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}"); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
typedef int HRESULT; typedef unsigned UINT; typedef unsigned D3DKMT_HANDLE; typedef int BOOL;
typedef unsigned long long ULONGLONG, APPLE_AGX_U64; typedef void *HANDLE; typedef void VOID;
typedef uintptr_t ULONG_PTR;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define FAILED(x) ((x)<0)
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define APPLE_AGX_GPUVA_WINSYS 1
#define MEM_RELEASE 0x8000u
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 4u
#define ZeroMemory(p,n) memset((p),0,(n))
typedef int SRWLOCK;
static int locked;
static void AcquireSRWLockExclusive(SRWLOCK*){assert(!locked);locked=1;}
static void ReleaseSRWLockExclusive(SRWLOCK*){assert(locked);locked=0;}
static int VirtualFree(void*,size_t,unsigned){return 1;}
struct ADMISSION_UMD_SCREEN_BUFFER { BOOL Active,Mapped,Transition,CopyHeld,Direct,Resident,Borrowed,SystemDirect;
  APPLE_AGX_U64 Token; void *NativeBo; UINT SourceHolds,SubmissionHolds;
  D3DKMT_HANDLE KernelAllocation,StagingAllocation; ULONGLONG CanonicalGpuVa; unsigned char *PrivateStaging; };
struct D3DDDI_EVICT_FLAGS { union { struct { UINT EvictOnlyIfNecessary:1; UINT NotWrittenTo:1; UINT Reserved:30; }; UINT Value; }; };
struct D3DDDICB_EVICT { D3DDDI_EVICT_FLAGS Flags; UINT NumAllocations; const D3DKMT_HANDLE *AllocationList; ULONGLONG NumBytesToTrim; };
struct D3DDDICB_DEALLOCATE { HANDLE hResource; UINT NumAllocations; const D3DKMT_HANDLE *HandleList; };
/* dxgkrnl model: a deallocated runtime resource kills its allocation handle;
 * an Evict naming a dead handle marks the device as removed. */
static D3DKMT_HANDLE dead; static unsigned evicts, dead_evicts, deallocations;
static HRESULT ev(HANDLE,const D3DDDICB_EVICT*e){++evicts;
  for(UINT i=0;i<e->NumAllocations;++i) if(e->AllocationList[i]==dead) ++dead_evicts;
  return dead_evicts ? (HRESULT)0x887a0005u : S_OK;}
static HRESULT de(HANDLE,const D3DDDICB_DEALLOCATE*d){++deallocations;
  if(d->hResource==(HANDLE)0x5000) dead=0x40; return S_OK;}
struct CB { HRESULT(*pfnEvictCb)(HANDLE,const D3DDDICB_EVICT*); HRESULT(*pfnDeallocateCb)(HANDLE,const D3DDDICB_DEALLOCATE*); };
struct ADMISSION_UMD_DEVICE { ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[4]; SRWLOCK ScreenBufferLock; BOOL DrawTerminal;
  CB *KernelCallbacks; struct { HANDLE handle; } RuntimeDevice; HRESULT LastScreenError; };
struct ADMISSION_UMD_RETIREMENT { UINT Origin; BOOL Primary,Shared; D3DKMT_HANDLE KernelResource,KernelAllocation; HANDLE RuntimeResource; };
static void AdmissionUmdVaRecordDeallocate(const void*,ULONGLONG,D3DKMT_HANDLE,ULONGLONG,HRESULT){}
static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,UINT){}
@@SCREEN@@
@@RUNTIME@@
int main(){
  CB cb={ev,de}; ADMISSION_UMD_DEVICE d={}; d.KernelCallbacks=&cb;
  /* EXP1130 order: the presentation resource is deallocated first, the
   * native BO's Direct slot (persistently resident) is destroyed after. */
  ADMISSION_UMD_SCREEN_BUFFER &b=d.ScreenBuffers[0];
  b.Active=1;b.Token=7;b.Direct=1;b.Resident=1;b.KernelAllocation=0x40;
  ADMISSION_UMD_RETIREMENT r={}; r.Origin=2u;r.Primary=1;r.KernelAllocation=0x40;
  r.RuntimeResource=(HANDLE)0x5000;
  assert(AdmissionUmdDeallocateResource(&d,&r)==S_OK && dead==0x40);
  assert(AdmissionUmdScreenDestroyBuffer(&d,7)==1);
  assert(dead_evicts==0 && evicts==0);
  assert(!d.ScreenBuffers[0].Active && d.LastScreenError==S_OK);
  /* The usual order is unchanged: a live slot drops its own reference once. */
  ADMISSION_UMD_SCREEN_BUFFER &c=d.ScreenBuffers[1];
  c.Active=1;c.Token=9;c.Direct=1;c.Borrowed=1;c.Resident=1;c.KernelAllocation=0x80;
  assert(AdmissionUmdScreenDestroyBuffer(&d,9)==1);
  assert(evicts==1 && dead_evicts==0 && deallocations==1);
  return 0;
}
'''


class DeallocatedSlotNoEvictTests(unittest.TestCase):
    def test_deallocated_runtime_resource_is_never_evicted_by_its_slot(self):
        screen = SCREEN.read_text()
        runtime = RUNTIME.read_text()
        screen_funcs = "\n".join([
            function(screen, "AdmissionUmdScreenFind"),
            function(screen, "AdmissionUmdScreenForgetAllocation", False),
            function(screen, "AdmissionUmdScreenDestroyBuffer")])
        runtime_funcs = function(runtime, "AdmissionUmdDeallocateResource")
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "t.cpp"; binary = Path(tmp) / "t"
            source.write_text(BODY.replace("@@SCREEN@@", screen_funcs)
                              .replace("@@RUNTIME@@", runtime_funcs))
            subprocess.run(["clang++", "-std=c++17", "-w", str(source), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
