"""Replay the real frame-receipt path without requesting GPU-idle entry.

The receipt only mutates CPU bookkeeping. Its UMD request must not select
Windows level-two hardware synchronization, and the KMD must accept that
software entry without weakening process/context/device validation.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(path, name):
    text = (ROOT / path).read_text()
    pos = text.index(name + '(')
    start = text.rfind('\n', 0, pos) + 1
    brace = text.index('{', pos)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


SHIM = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
using UINT=uint32_t; using ULONG=uint32_t; using ULONGLONG=uint64_t;
using ULONG_PTR=uintptr_t; using D3DKMT_HANDLE=uint32_t;
using BOOL=bool; using BOOLEAN=bool; using HANDLE=void*; using PVOID=void*;
using HRESULT=int32_t; using NTSTATUS=int32_t;
using LONG=int32_t;
#define TRUE true
#define FALSE false
#define _Use_decl_annotations_
#define PASSIVE_LEVEL 0
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xc0000008)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xc0000184)
#define SUCCEEDED(x) ((x)>=0)
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define CONTAINING_RECORD(p,t,f) ((t*)((char*)(p)-offsetof(t,f)))
#define RtlCopyMemory std::memcpy
#define RtlZeroMemory(p,n) std::memset((p),0,(n))
#define ADMISSION_DWM_FRAME_ARM_MAGIC 0x314d5241u
#define ADMISSION_DWM_FRAME_VERSION 1u
#define ADMISSION_DWM_FRAME_CAPACITY 2u
#define ADMISSION_UMD_SCREEN_BUFFER_LIMIT 2u
#define ADMISSION_DWM_SOURCE_MAP_VERSION 2u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define APPLE_AGX_GPUVA_G3_VALID 1u
#define APPLE_AGX_GPUVA_G3_WRITE 2u
#define MAXULONGLONG UINT64_MAX
struct APPLE_AGX_GPUVA_G3_LOGICAL_PTE { ULONGLONG Allocation,AllocationOffset,GuestIpa; UINT SegmentId,Flags; };
struct ADMISSION_DWM_SOURCE_MAP_RECEIPT {
 UINT Version,Bytes,OsProcessId,PteFound; ULONGLONG GraphProcessId,Allocation,CanonicalGpuVa,RootIpa;
 ULONGLONG MappingGeneration,PteAllocation,PteAllocationOffset,PteGuestIpa,ResolvedGuestIpa;
 ULONGLONG SelectedHostPhysicalAddress,SelectedPrimaryAddress,SelectedSurfaceBytes;
 UINT SegmentId,PteFlags,SourceReceiptState,Ordinal,InSelectedRange,InSelectedRangeCount;
};
struct ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT { UINT Unused; };
struct SourceReceipt { ULONGLONG SelectedHostPhysicalAddress,PrimaryAddress; UINT Stride,Height; };
struct Flags { union { struct { UINT HardwareAccess:1, other:31; }; UINT Value; }; };
struct ADMISSION_DWM_FRAME_ARM {
 UINT Magic,Version,Bytes,OsProcessId; ULONGLONG Allocation,CanonicalGpuVa;
};
struct ADMISSION_DWM_FRAME_ENTRY {
 UINT OsProcessId; ULONGLONG GraphProcessId,Allocation,CanonicalGpuVa,Context;
 UINT Sequence;
};
struct Probe { ADMISSION_DWM_FRAME_ENTRY Entries[2]; UINT ArmedCount,Dropped; };
struct ADMISSION_CONTEXT { bool Started; void *GpuvaG3State; int ObjectAdapter; Probe DwmFrameProbe;
 void *PhysicalDeviceObject; LONG DwmSourceMapRecordCount,SourceAddressReceiptState,DwmSourceMapInRangeCount;
 SourceReceipt SourceAddressReceipt; };
struct DeviceObject { void *Adapter; };
struct ADMISSION_DEVICE { DeviceObject Object; };
struct ADMISSION_RENDER_CONTEXT {
 bool Win32Transport; struct {DeviceObject *Device;} Object;
 ADMISSION_RENDER_CONTEXT *GpuvaG3NextContext;
 ULONGLONG GpuvaG3RootIpa,GpuvaG3MappingGeneration;
};
struct ADMISSION_G3_PROCESS { ADMISSION_RENDER_CONTEXT *Contexts; struct {ULONGLONG ProcessId;} Graph; };
struct ADMISSION_G3_STATE { int Lock; ADMISSION_G3_PROCESS *Process; };
struct DXGKARG_ESCAPE {
 Flags Flags; void *pPrivateDriverData; UINT PrivateDriverDataSize;
 HANDLE hKmdProcessHandle,hContext,hDevice;
};
struct D3DDDICB_ESCAPE {HANDLE hDevice,hContext; struct Flags Flags; void *pPrivateDriverData; UINT PrivateDriverDataSize;};
struct RuntimeHandle { HANDLE handle; };
struct ADMISSION_UMD_ADAPTER {RuntimeHandle RuntimeAdapter;};
struct Callbacks { HRESULT (*pfnEscapeCb)(HANDLE,D3DDDICB_ESCAPE*); };
struct ScreenBuffer { bool Active; UINT KernelAllocation; ULONGLONG CanonicalGpuVa; };
struct ADMISSION_UMD_DEVICE {
 int ScreenBufferLock; ScreenBuffer ScreenBuffers[2]; bool DwmFrameArmAttempted;
 UINT DwmFrameLastAllocation; ULONGLONG DwmFrameLastVa; UINT DwmFrameArmAttempts;
 Callbacks *KernelCallbacks; ADMISSION_UMD_ADAPTER *Adapter;
 RuntimeHandle RuntimeDevice; HANDLE KernelContext;
};
static bool isDwm=true,claim=true,hardwareBusy=true;
static LONG gDwmSystemLeafSnapshotClaimed=1;
static unsigned calls=0,blocked=0,lastFlags=~0u,irql=0;
static ADMISSION_CONTEXT adapter{}; static ADMISSION_G3_STATE state{};
static ADMISSION_G3_PROCESS process{}; static ADMISSION_RENDER_CONTEXT context{};
static ADMISSION_DEVICE kd{};
static bool frame_process_is_dwm(){return isDwm;}
static UINT GetCurrentProcessId(){return 42;}
static HANDLE PsGetCurrentProcessId(){return (HANDLE)(uintptr_t)42;}
static ULONG HandleToULong(HANDLE p){return (ULONG)(uintptr_t)p;}
static unsigned KeGetCurrentIrql(){return irql;}
static void AcquireSRWLockShared(int*){}
static void ReleaseSRWLockShared(int*){}
static void ExAcquireFastMutex(int *p){assert(*p==0);*p=1;}
static void ExReleaseFastMutex(int *p){assert(*p==1);*p=0;}
static void KeMemoryBarrier(){}
static bool AdmissionDwmFrameClaim(ADMISSION_CONTEXT*){return claim;}
static void AdmissionDwmFrameRelease(ADMISSION_CONTEXT*){}
static ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(ADMISSION_G3_STATE *s,HANDLE h){return h==s->Process?s->Process:nullptr;}
static void AdmissionUmdDiagnostic(const char*,HRESULT,UINT*,size_t){}
static const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *AdmissionG3CopyPte(ADMISSION_G3_PROCESS *,ULONGLONG){return nullptr;}
static LONG InterlockedCompareExchange(LONG *p,LONG value,LONG expected){LONG old=*p;if(old==expected)*p=value;return old;}
static LONG InterlockedIncrement(LONG *p){return ++*p;}
static void AdmissionG3SnapshotDwmSystemLeaves(ADMISSION_G3_PROCESS *,UINT,ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT *){}
static void AdmissionG3WriteDwmSystemLeaves(ADMISSION_CONTEXT *,const ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT *){}
static void AdmissionRecordDwmSourceMap(void *,const ADMISSION_DWM_SOURCE_MAP_RECEIPT *,UINT){}
'''

END = r'''
static HRESULT escape_callback(HANDLE handle,D3DDDICB_ESCAPE *q) {
 ++calls;lastFlags=q->Flags.Value;
 // Model only the documented admission requirement: hardware entry requires
 // GPU idle. The actual KMD handler and CPU receipt mutation run below.
 if(q->Flags.HardwareAccess && hardwareBusy){++blocked;return -1;}
 DXGKARG_ESCAPE a{};a.Flags.Value=q->Flags.Value;a.pPrivateDriverData=q->pPrivateDriverData;
 a.PrivateDriverDataSize=q->PrivateDriverDataSize;a.hKmdProcessHandle=&process;
 a.hContext=q->hContext;a.hDevice=q->hDevice;
 return AdmissionGpuvaG3FrameArmEscape((ADMISSION_CONTEXT*)handle,&a);
}
int main(int argc,char **argv) {
 assert(argc==2);adapter.Started=true;adapter.GpuvaG3State=&state;state.Process=&process;
 process.Contexts=&context;process.Graph.ProcessId=99;context.Win32Transport=true;
 context.Object.Device=&kd.Object;kd.Object.Adapter=&adapter.ObjectAdapter;
 if(argv[1][0]=='u') {
  Callbacks cb{escape_callback};ADMISSION_UMD_ADAPTER a{{&adapter}};
  ADMISSION_UMD_DEVICE d{};d.KernelCallbacks=&cb;d.Adapter=&a;
  d.RuntimeDevice.handle=&kd;d.KernelContext=&context;
  assert(AdmissionUmdGpuvaFrameArm(&d,0x80,0x10000)==0x10000);
  assert(lastFlags==0 && blocked==0);assert(d.DwmFrameArmAttempted);
  assert(adapter.DwmFrameProbe.Entries[0].Allocation==0x80);
  assert(adapter.DwmFrameProbe.Entries[0].GraphProcessId==99);
  assert(adapter.DwmFrameProbe.Entries[0].CanonicalGpuVa==0x10000);
  unsigned before=calls;AdmissionUmdGpuvaFrameArm(&d,0x80,0x10000);assert(calls==before);
  isDwm=false;AdmissionUmdGpuvaFrameArm(&d,0x81,0x20000);assert(calls==before);
 } else {
  ADMISSION_DWM_FRAME_ARM q{ADMISSION_DWM_FRAME_ARM_MAGIC,ADMISSION_DWM_FRAME_VERSION,
    sizeof(q),42,0x80,0x10000};
  DXGKARG_ESCAPE a{};a.pPrivateDriverData=&q;a.PrivateDriverDataSize=sizeof(q);
  a.hKmdProcessHandle=&process;a.hContext=&context;a.hDevice=&kd;
  assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_SUCCESS);
  assert(adapter.DwmFrameProbe.ArmedCount==1 && state.Lock==0);
  auto saved=adapter.DwmFrameProbe.Entries[0];
  q.OsProcessId=43;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_PARAMETER);q.OsProcessId=42;
  q.CanonicalGpuVa=0x10001;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_PARAMETER);q.CanonicalGpuVa=0x10000;
  a.Flags.Value=2;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_PARAMETER);
  a.Flags.Value=8;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_PARAMETER);a.Flags.Value=0;
  a.hContext=nullptr;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_HANDLE);a.hContext=&context;
  a.hDevice=nullptr;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_HANDLE);a.hDevice=&kd;
  a.hKmdProcessHandle=nullptr;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_HANDLE);a.hKmdProcessHandle=&process;
  irql=1;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_PARAMETER);irql=0;
  assert(std::memcmp(&saved,&adapter.DwmFrameProbe.Entries[0],sizeof(saved))==0);
  a.Flags.Value=1;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_SUCCESS);
  a.Flags.Value=0;claim=false;assert(AdmissionGpuvaG3FrameArmEscape(&adapter,&a)==STATUS_INVALID_HANDLE);
  assert(state.Lock==0 && blocked==0);
 }
 puts("frame_arm_software_entry: PASS");
}
'''


class FrameArmSoftwareTests(unittest.TestCase):
    def replay(self, mode):
        functions = [
            function('drivers/apple-agx/render-admission/src/dwm_ddi_probe_windows.c', 'AdmissionDwmFrameArmWindows'),
            function('drivers/apple-agx/render-admission/src/gpuva_g3_windows.c', 'AdmissionGpuvaG3FrameArmEscape'),
            function('drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c', 'AdmissionUmdGpuvaFrameArm'),
        ]
        with tempfile.TemporaryDirectory(prefix='frame-arm-replay-') as temp:
            source = Path(temp) / 'replay.cpp'
            source.write_text(SHIM + '\n'.join(functions) + END)
            binary = Path(temp) / 'replay'
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary), mode], check=True, timeout=10)

    def test_umd_does_not_request_gpu_idle(self):
        self.replay('umd')

    def test_kmd_software_entry_preserves_owner_validation(self):
        self.replay('kmd')
