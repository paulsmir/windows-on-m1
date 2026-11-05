"""Reproduce EXP497 runtime-token dereference in the actual Windows callbacks."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class OpenAllocationTests(unittest.TestCase):
    def test_runtime_handle_resolution_and_rollback(self):
        source = (RENDER / 'src/allocation_windows.c').read_text()
        callbacks = source[source.index('_Use_decl_annotations_ NTSTATUS AdmissionDdiOpenAllocation'):]
        shim = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "render_allocation.h"
#include "render_objects.h"
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((int32_t)0xc000000d)
#define STATUS_INVALID_HANDLE ((int32_t)0xc0000008)
#define STATUS_INVALID_DEVICE_STATE ((int32_t)0xc0000184)
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0
#define ADMISSION_OPEN_ALLOCATION_MAGIC 0x4f504152u
#define DXGK_HANDLE_ALLOCATION 1
#define AdmissionOpenAllocationTraceBegin(Context,Device,Args) \
 ((void)(Context),(void)(Device),(void)(Args),FALSE)
#define AdmissionOpenAllocationTraceResult(Context,Enabled,Guard,Status) \
 ((void)(Context),(void)(Enabled),(void)(Guard),(void)(Status))
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define RtlCompareMemory(a,b,n) (memcmp((a),(b),(n))==0?(n):0)
#define ExAllocatePool2(flags,n,tag) allocate(n)
#define ExFreePoolWithTag(p,tag) free(p)
#define CONTAINING_RECORD(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef int32_t NTSTATUS,LONG;
typedef void *HANDLE;
typedef unsigned int UINT,ULONG,D3DKMT_HANDLE;
typedef unsigned char BOOLEAN;
typedef struct {D3DKMT_HANDLE hObject;UINT Type;union {UINT Value;struct {
 UINT DeviceSpecific:1;UINT Reserved:31;};} Flags;} DXGKARGCB_GETHANDLEDATA;
typedef void *DXGKARG_RELEASE_HANDLE;
typedef struct {DXGKARG_RELEASE_HANDLE ReleaseHandle;UINT Type;}
 DXGKARGCB_RELEASEHANDLEDATA;
typedef struct {ADMISSION_OBJECT_ADAPTER ObjectAdapter;BOOLEAN InterfaceValid;
 struct {void *(*DxgkCbGetHandleData)(const DXGKARGCB_GETHANDLEDATA *);
 void *(*DxgkCbAcquireHandleData)(const DXGKARGCB_GETHANDLEDATA *,DXGKARG_RELEASE_HANDLE *);
 void (*DxgkCbReleaseHandleData)(DXGKARGCB_RELEASEHANDLEDATA);} Interface;
} ADMISSION_CONTEXT;
typedef struct {ADMISSION_OBJECT_DEVICE Object;volatile LONG Win32Generation;} ADMISSION_DEVICE;
typedef struct {ADMISSION_ALLOCATION_OBJECT Object;} ADMISSION_ALLOCATION_HANDLE;
typedef struct {ULONG Magic;ADMISSION_DEVICE *Device;D3DKMT_HANDLE RuntimeAllocation;
 ADMISSION_ALLOCATION_OBJECT *Allocation;BOOLEAN ReadOnly;ULONG Win32Generation;} ADMISSION_OPEN_ALLOCATION;
typedef struct {D3DKMT_HANDLE hAllocation;void *pPrivateDriverData;
 UINT PrivateDriverDataSize;HANDLE hDeviceSpecificAllocation;} DXGK_OPENALLOCATIONINFO;
typedef struct {UINT NumAllocations;DXGK_OPENALLOCATIONINFO *pOpenAllocation;
 const void *pPrivateDriverData;UINT PrivateDriverSize;union {UINT Value;struct {
 UINT Create:1;UINT ReadOnly:1;UINT Reserved:30;};} Flags;UINT SubresourceIndex;
} DXGKARG_OPENALLOCATION;
typedef struct {UINT NumAllocations;HANDLE *pOpenHandleList;} DXGKARG_CLOSEALLOCATION;
static ADMISSION_ALLOCATION_HANDLE backing;
static unsigned calls,releases,refs;
static int failAllocation;
static void *allocate(size_t n) {return failAllocation?NULL:malloc(n);}
static LONG InterlockedCompareExchange(volatile LONG *target,LONG exchange,LONG compare) {
 LONG original=*target;if(original==compare)*target=exchange;return original;
}
static int referenceToken;
enum {AdmissionOpenAllocationGuardAccepted=0,
 AdmissionOpenAllocationGuardArgs=2,AdmissionOpenAllocationGuardInterface=3,
 AdmissionOpenAllocationGuardPrivate=4,AdmissionOpenAllocationGuardAcquire=5,
 AdmissionOpenAllocationGuardDescription=6,AdmissionOpenAllocationGuardPool=7};
static void *legacy(const DXGKARGCB_GETHANDLEDATA *q) {
 (void)q;return NULL; /* EXP498 current Windows rejects WDDM1-only resolver. */
}
static void *resolve(const DXGKARGCB_GETHANDLEDATA *q,DXGKARG_RELEASE_HANDLE *releaseHandle) {
 ++calls;
 assert(q->Type==1 && q->Flags.Value==0);
 *releaseHandle=NULL;
 if(q->hObject!=0x400001c0u)return NULL;
 assert(refs==0);refs=1;*releaseHandle=&referenceToken;return &backing;
}
static void release(DXGKARGCB_RELEASEHANDLEDATA q) {
 assert(q.Type==1 && q.ReleaseHandle==&referenceToken && refs==1);
 refs=0;++releases;
}
'''
        cases = r'''
int main(void) {
 ADMISSION_CONTEXT adapter={0};ADMISSION_DEVICE device={0};
 ADMISSION_ALLOCATION_DESCRIPTION d;
 assert(AdmissionAllocationDescribe(2560,1600,4,1,21,0,&d));
 assert(AdmissionAllocationCreate(&d,&backing.Object));
 adapter.InterfaceValid=TRUE;adapter.Interface.DxgkCbGetHandleData=legacy;
 adapter.Interface.DxgkCbAcquireHandleData=resolve;
 adapter.Interface.DxgkCbReleaseHandleData=release;
 device.Object.Magic=ADMISSION_OBJECT_DEVICE_MAGIC;
 device.Object.Adapter=&adapter.ObjectAdapter;
 device.Win32Generation=7;
 DXGK_OPENALLOCATIONINFO info[2]={{0x400001c0u,&d,48,NULL},{0x400001c4u,&d,48,NULL}};
 DXGKARG_OPENALLOCATION a={1,info,NULL,0,{.ReadOnly=1},0};
 assert(AdmissionDdiOpenAllocation(&device,&a)==0);
 ADMISSION_OPEN_ALLOCATION *opened=info[0].hDeviceSpecificAllocation;
 assert(opened && opened->Allocation==&backing.Object && opened->Device==&device);
 assert(opened->RuntimeAllocation==0x400001c0u && opened->ReadOnly);
 assert(opened->Win32Generation==7u);
 assert(calls==1 && releases==1 && refs==0);
 assert(backing.Object.OpenCount==1 && device.Object.AllocationCount==1);
 HANDLE handles[1]={opened};DXGKARG_CLOSEALLOCATION closeArgs={1,handles};
 assert(AdmissionDdiCloseAllocation(&device,&closeArgs)==0);
 assert(backing.Object.OpenCount==0 && device.Object.AllocationCount==0);
 info[0].hDeviceSpecificAllocation=NULL;a.NumAllocations=2;calls=0;
 assert(AdmissionDdiOpenAllocation(&device,&a)==STATUS_INVALID_HANDLE);
 assert(calls==2 && info[0].hDeviceSpecificAllocation==NULL && info[1].hDeviceSpecificAllocation==NULL);
 assert(releases==2 && refs==0);
 assert(backing.Object.OpenCount==0 && device.Object.AllocationCount==0);
 a.NumAllocations=1;adapter.Interface.DxgkCbAcquireHandleData=NULL;
 assert(AdmissionDdiOpenAllocation(&device,&a)!=0);
 assert(backing.Object.OpenCount==0 && device.Object.AllocationCount==0);
 adapter.Interface.DxgkCbAcquireHandleData=resolve;
 adapter.Interface.DxgkCbReleaseHandleData=NULL;
 assert(AdmissionDdiOpenAllocation(&device,&a)!=0);
 adapter.Interface.DxgkCbReleaseHandleData=release;adapter.InterfaceValid=FALSE;
 assert(AdmissionDdiOpenAllocation(&device,&a)!=0);
 adapter.InterfaceValid=TRUE;d.Format=22;
 assert(AdmissionDdiOpenAllocation(&device,&a)==STATUS_INVALID_PARAMETER);
 assert(info[0].hDeviceSpecificAllocation==NULL && backing.Object.OpenCount==0);
 assert(device.Object.AllocationCount==0);
 assert(releases==3 && refs==0);
 d.Format=21;failAllocation=1;
 assert(AdmissionDdiOpenAllocation(&device,&a)!=0);
 assert(releases==4 && refs==0 && backing.Object.OpenCount==0);
 assert(device.Object.AllocationCount==0 && info[0].hDeviceSpecificAllocation==NULL);
 failAllocation=0;backing.Object.Magic=0;
 assert(AdmissionDdiOpenAllocation(&device,&a)==STATUS_INVALID_HANDLE);
 assert(releases==5 && refs==0 && device.Object.AllocationCount==0);
 backing.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;
 assert(AdmissionAllocationDestroy(&backing.Object));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'open.c'
            program.write_text(shim + callbacks + cases)
            binary = Path(tmp) / 'open'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-Wno-int-to-pointer-cast',
                            '-fsanitize=address,undefined', '-I', str(RENDER / 'include'),
                            str(program), str(RENDER / 'src/render_allocation.c'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
