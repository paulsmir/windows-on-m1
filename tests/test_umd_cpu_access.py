"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

A cached D3D12 MapHeap must record pointer exposure without inventing LockCb
calls; nested UnmapHeap must not erase the persistent native CPU map. Failure
snapshots must retain every large candidate's identity/range and occur once,
including with tracing in refusals-only mode (measure-* is already admitted).
Execute the production receipt, bookkeeping, owner bridge and MapHeap bodies.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_deallocated_slot_no_evict import function

ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / 'drivers/apple-agx/render-admission/umd'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

FIXTURE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include "umd_cpu_access.h"
typedef uint64_t APPLE_AGX_U64;
typedef unsigned UINT, D3DKMT_HANDLE;
typedef int HRESULT, BOOL, SRWLOCK;
typedef void VOID; typedef void *PVOID;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_INVALIDARG ((int)0x80070057)
#define E_FAIL ((int)0x80004005)
#define SUCCEEDED(h) ((h)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
#define ADMISSION_UMD_DEVICE_MAGIC 0x55u
#define APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES 4u
#define AdmissionDrawSealed 2u
#define DXGI_ERROR_DEVICE_REMOVED ((int)0x887a0005)
#define DXGI_ERROR_DEVICE_HUNG ((int)0x887a0006)
#define FAILED(h) ((h)<0)
#define APPLE_AGX_GPUVA_WINSYS 1
#define APPLE_AGX_D3D12_NATIVE_OWNER 1
#define ARRAYSIZE(a) (sizeof(a)/sizeof(a[0]))
#define ADMISSION_UMD_SCREEN_BUFFER_SCAN(d) 80u
struct D3DDDICB_LOCK { UINT hAllocation; struct { UINT LockEntire; } Flags; void *pData; };
struct D3DDDICB_UNLOCK { UINT NumAllocations; UINT *phAllocations; };
struct CB { HRESULT (*pfnLockCb)(void *,D3DDDICB_LOCK *); HRESULT (*pfnUnlockCb)(void *,D3DDDICB_UNLOCK *); };
struct AGX_WIN32_RELOC_ALLOCATION { uint64_t Token,Serial,Bytes; };
struct ADMISSION_UMD_DRAW_SUBMISSION {
  UINT Phase,Count; AGX_WIN32_RELOC_ALLOCATION Identities[4];
  struct {UINT hAllocation;} Allocations[4];
};
struct ADMISSION_UMD_SCREEN_BUFFER {
  BOOL Active, Mapped, Borrowed, Transition, Direct, SystemDirect;
  uint64_t Serial; UINT LockedAccess, StagingAllocation, FailedCpuLock; int Sync;
  int (*NativeMapRelease)(const void *,const void *,int);
  void *LockedBase, *NativeBo, *PrivateStaging;
  APPLE_AGX_U64 Token, Bytes, CanonicalGpuVa;
  D3DKMT_HANDLE KernelAllocation;
  ADMISSION_UMD_CPU_ACCESS CpuAccess;
};
struct ADMISSION_UMD_DEVICE {
  UINT Magic; CB *KernelCallbacks; struct {void *handle;} RuntimeDevice;
  ADMISSION_UMD_DRAW_SUBMISSION *DrawSubmission; HRESULT LastScreenError;
  ADMISSION_UMD_SCREEN_BUFFER ScreenBuffers[80];
  SRWLOCK ScreenBufferLock;
  UINT CpuAccessSnapshots;
  APPLE_AGX_U64 OwnerCookie;
  BOOL DrawTerminal;
};
static void AcquireSRWLockExclusive(SRWLOCK *l) { assert(!*l); *l=1; }
static void ReleaseSRWLockExclusive(SRWLOCK *l) { assert(*l==1); *l=0; }
static std::vector<std::array<UINT,16>> rows;
static unsigned flushed;
static void AdmissionUmdDiagnostic(const char *stage,HRESULT,const UINT *v,UINT n) {
  assert(strcmp(stage,"measure-bo-cpu-access")==0 && n==16);
  std::array<UINT,16> row; memcpy(row.data(),v,sizeof(row));
  if(((row[0]>>8)&0xffu)==4u) return; /* reason 4: live rows (EXP1918U3), not part of the snapshot contract */
  rows.push_back(row);
}
static void AdmissionUmdDiagnosticFlush() { ++flushed; }
static void AdmissionUmdStagingInvalidate(int *) {}
@@SCREEN@@
struct pipe_resource { uint64_t Token; };
static int AgxWin32AsahiResourceIdentity(pipe_resource *r,AGX_WIN32_RELOC_ALLOCATION *i) {
  i->Token=r->Token;return r->Token!=0;
}
struct AGX_D3D12_BUFFER { void *Resource; unsigned long long GpuVa; void *Cpu; unsigned long long Bytes; };
struct AGX_D3D12_OWNER { ADMISSION_UMD_DEVICE Runtime; };
struct OwnerLock { explicit OwnerLock(AGX_D3D12_OWNER *) {} };
@@OWNER@@
struct AgxD3d12Device { void *owner; CB *kernel; struct { void *handle; } runtime; };
struct AgxD3d12Heap { void *volume, *native, *cpu; UINT allocation, locks; BOOL cpu_visible; uint64_t gpuva, bytes; };
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HHEAP { void *pDrvPrivate; };
static AgxD3d12Device *DeviceOf(D3D12DDI_HDEVICE h) { return (AgxD3d12Device *)h.pDrvPrivate; }
@@DDI@@
static int release_map(const void *bo,const void *,int commit) {return !commit || bo!=(void *)1;}
static HRESULT batch_unlock(void *,D3DDDICB_UNLOCK *u) {assert(u->NumAllocations==2);return S_OK;}
int main() {
  /* A successful batch UnlockCb retires every lock even if native map
   * publication then fails on the first BO. */
  ADMISSION_UMD_DEVICE batch={};ADMISSION_UMD_DRAW_SUBMISSION submission={};CB callbacks={nullptr,batch_unlock};
  batch.Magic=ADMISSION_UMD_DEVICE_MAGIC;batch.KernelCallbacks=&callbacks;batch.DrawSubmission=&submission;
  submission.Phase=AdmissionDrawSealed;submission.Count=2;
  for(unsigned i=0;i<2;++i) {
    auto &s=batch.ScreenBuffers[i];s.Active=1;s.Mapped=1;s.SystemDirect=1;s.Token=i+1;
    s.Serial=9;s.Bytes=65ull<<20;s.KernelAllocation=i+100;s.NativeBo=(void *)(uintptr_t)(i+1);
    s.LockedBase=(void *)0x1000;s.NativeMapRelease=release_map;s.CpuAccess.ActiveLocks=1;
    submission.Identities[i]={s.Token,s.Serial,s.Bytes};submission.Allocations[i].hAllocation=s.KernelAllocation;
  }
  assert(AdmissionUmdScreenPrepareSubmissionMaps(&batch,&submission)==E_FAIL && batch.DrawTerminal);
  assert(!batch.ScreenBuffers[0].CpuAccess.ActiveLocks && !batch.ScreenBuffers[1].CpuAccess.ActiveLocks);
  AGX_D3D12_OWNER o={};auto &d=o.Runtime;
  d.OwnerCookie=0x123456789abcdef0ull;
  auto &s=d.ScreenBuffers[0];s.Active=1;s.Token=0x100000002ull;s.Bytes=65ull<<20;
  s.KernelAllocation=0x42;s.CanonicalGpuVa=0x23456780000ull;s.SystemDirect=1;
  s.Mapped=1;s.LockedBase=(void *)0x1000;s.NativeBo=(void *)1;
  AdmissionUmdScreenCpuAccessEvent(&d,s.Token,AdmissionCpuLock);
  AdmissionUmdScreenCpuAccessEvent(&d,s.Token,AdmissionCpuMap);
  pipe_resource r={s.Token};AGX_D3D12_BUFFER b={&r,s.CanonicalGpuVa,s.LockedBase,s.Bytes};
  owner_cpu_pointer(&o,&b,AdmissionCpuOwnerPointer);
  AgxD3d12Device dev={&o,nullptr,{nullptr}};
  AgxD3d12Heap heap={};heap.native=&r;heap.cpu=s.LockedBase;heap.bytes=s.Bytes;
  D3D12DDI_HDEVICE dh={&dev};D3D12DDI_HHEAP hh={&heap};void *out=nullptr;
  assert(MapHeap(dh,hh,&out)==S_OK && out==s.LockedBase);
  assert(MapHeap(dh,hh,&out)==S_OK && s.CpuAccess.HeapDepth==2);
  UnmapHeap(dh,hh);UnmapHeap(dh,hh);UnmapHeap(dh,hh);
  assert(s.CpuAccess.HeapMaps==2 && !s.CpuAccess.HeapDepth);
  assert(s.Mapped && s.LockedBase && s.CpuAccess.ActiveLocks==1);
  assert(s.CpuAccess.Maps==1 && s.CpuAccess.Locks==1 && s.CpuAccess.OwnerPointers==1);
  b.Cpu=nullptr;owner_cpu_pointer(&o,&b,AdmissionCpuOwnerPointer);
  AdmissionUmdScreenCpuAccessEvent(&d,999,AdmissionCpuLock);
  assert(s.CpuAccess.OwnerPointers==1 && s.CpuAccess.Locks==1);
  /* Every >64 MiB BO survives a snapshot, even beyond typical top-N limits. */
  for(unsigned i=1;i<70;++i) { auto &x=d.ScreenBuffers[i];x.Active=1;x.Token=i+7;
    x.Bytes=(65ull+i)<<20;x.KernelAllocation=0x100+i;x.CanonicalGpuVa=0x400000000ull+(uint64_t)i*0x10000000; }
  d.ScreenBuffers[70].Active=1;d.ScreenBuffers[70].Bytes=16ull<<20;
  d.ScreenBuffers[71].Active=1;d.ScreenBuffers[71].Bytes=(16ull<<20)+1;
  d.ScreenBuffers[72].Bytes=128ull<<20; /* inactive */
  AgxD3d12OwnerRecordError(&o,S_OK);assert(rows.empty());
  AgxD3d12OwnerRecordError(&o,DXGI_ERROR_DEVICE_REMOVED);assert(rows.size()==71);
  auto row=rows[0];
  assert(row[0]==0x201 && row[1]==0x9abcdef0 && row[2]==0x12345678);
  assert(row[3]==2 && row[4]==1 && row[5]==0x42);
  assert(row[6]==0x56780000 && row[7]==0x234 && row[8]==0x4100000 && row[9]==0);
  assert(row[10]==1 && row[11]==1 && row[12]==1 && row[13]==2 && row[14]==0);
  assert(row[15]==(1|2|4|8|32|512));
  for(unsigned i=1;i<70;++i) assert(rows[i][5]==0x100+i);
  AgxD3d12OwnerRecordError(&o,DXGI_ERROR_DEVICE_HUNG);assert(rows.size()==71);
  AdmissionUmdScreenCpuAccessSnapshot(&d,S_OK,3);assert(rows.size()==142);
  AdmissionUmdScreenCpuAccessSnapshot(&d,S_OK,3);assert(rows.size()==142);
  assert(!d.ScreenBufferLock && flushed);
  /* Real temporary locks do not become published maps; failed callbacks
   * do not dispatch success events. Unlock cannot underflow or wrap. */
  AdmissionUmdScreenCpuAccessEvent(&d,s.Token,AdmissionCpuUnlock);
  AdmissionUmdScreenCpuAccessEvent(&d,s.Token,AdmissionCpuUnlock);
  AdmissionUmdScreenCpuAccessEvent(&d,s.Token,AdmissionCpuLock);
  AdmissionUmdScreenCpuAccessEvent(&d,s.Token,AdmissionCpuUnlock);
  assert(s.CpuAccess.Locks==2 && !s.CpuAccess.ActiveLocks && s.CpuAccess.Maps==1);
  s.CpuAccess.Locks=~0u;AdmissionUmdScreenCpuAccessEvent(&d,s.Token,AdmissionCpuLock);
  assert(s.CpuAccess.Locks==~0u);
  AdmissionUmdScreenCpuAccessReceipt(&d,&s,S_OK,1);assert(rows.back()[15]&1024);
  s={};s.Active=1;s.Bytes=65ull<<20;s.Token=90;s.KernelAllocation=0x42;
  AdmissionUmdScreenCpuAccessReceipt(&d,&s,S_OK,1);
  for(unsigned i=10;i<16;++i) assert(rows.back()[i]==0); /* reused handle, fresh lifetime */
}
'''

class CpuAccess(unittest.TestCase):
    def test_cached_heap_maps_and_bounded_receipts(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Cached MapHeap must not invent locks; failure receipts must include every large BO with intact identity and counters, including batch-unlock failure tails."""
        screen = (UMD / 'src/umd_win32_screen.c').read_text()
        funcs = '\n'.join(function(screen, n) for n in (
            'AdmissionUmdScreenFind', 'AdmissionUmdScreenCpuAllocation',
            'AdmissionUmdScreenPrepareSubmissionMaps', 'AdmissionUmdScreenCpuAccessReceipt',
            'AdmissionUmdScreenCpuAccessEvent', 'AdmissionUmdScreenCpuAccessSnapshot'))
        owner = OWNER.read_text().replace('extern "C" ', '')
        owner_funcs = '\n'.join(function(owner, n) for n in (
            'AgxD3d12OwnerRecordError', 'owner_cpu_pointer', 'AgxD3d12OwnerRecordHeapMap'))
        ddi = DDI.read_text()
        ddi_funcs = '\n'.join(function(ddi, n) for n in ('HeapBuffer', 'MapHeap', 'UnmapHeap'))
        source = FIXTURE.replace('@@SCREEN@@', funcs).replace('@@OWNER@@', owner_funcs).replace('@@DDI@@', ddi_funcs)
        with tempfile.TemporaryDirectory(prefix='t7g-cpu-access-') as tmp:
            src, exe = Path(tmp)/'test.cpp', Path(tmp)/'test'
            src.write_text(source)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', '-I', str(UMD/'include'), str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)

    def test_receipt_entrypoints_link_from_cpp_callers(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        GPUVA includes umd_internal.h directly as C++, while screen wraps it
        in extern C. Both callers must link to the same receipt entrypoints.
        """
        header = (UMD/'src/umd_internal.h').read_text()
        start = header.index('/* Receipt-only. Call without ScreenBufferLock held.')
        declarations = header[start:header.index('#ifdef APPLE_AGX_GPUVA_WINSYS', start)]
        types = 'typedef void VOID; typedef int HRESULT; typedef unsigned UINT; typedef unsigned long long APPLE_AGX_U64; struct ADMISSION_UMD_DEVICE { int unused; };\n'
        with tempfile.TemporaryDirectory(prefix='t7g-linkage-') as tmp:
            path = Path(tmp)
            (path/'receipt.h').write_text(types+declarations)
            (path/'screen.cpp').write_text('extern "C" {\n#include "receipt.h"\n}\n'
                'VOID AdmissionUmdScreenCpuAccessSnapshot(ADMISSION_UMD_DEVICE *, HRESULT, UINT) {}\n'
                'VOID AdmissionUmdScreenCpuAccessEvent(ADMISSION_UMD_DEVICE *, APPLE_AGX_U64, UINT) {}\n')
            (path/'gpuva.cpp').write_text('#include "receipt.h"\nint main() { '
                'AdmissionUmdScreenCpuAccessSnapshot(nullptr,0,2); AdmissionUmdScreenCpuAccessEvent(nullptr,0,0); }\n')
            run = subprocess.run(['clang++', '-std=c++17', str(path/'screen.cpp'), str(path/'gpuva.cpp'),
                                  '-o', str(path/'test')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
