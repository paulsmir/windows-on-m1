"""Replay the production frontend Flush and shared-retirement collector on host."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

def function(text, name):
    m = re.search(r'(?:static )?(?:BOOL|VOID|HRESULT) (?:APIENTRY )?'+name+r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start+1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]

class SharedRetirement(unittest.TestCase):
    def test_frontend_flush_releases_only_native_retired_shared_resources(self):
        winsys=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp').read_text()
        umd=ROOT/'drivers/apple-agx/render-admission/umd'
        lifetime=(umd/'src/umd_resource_lifetime.c').read_text()
        lifetime='\n'.join(x for x in lifetime.splitlines() if not x.startswith('#include'))
        header=(umd/'include/umd_resource_lifetime.h').read_text().replace('#include <windows.h>','')
        frontend=(ROOT/'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py').read_text()
        flush=re.search(r"replace_function_body\('src/gallium/frontends/d3d10umd/Device.cpp','Flush','''(.*?)'''\)",frontend,re.S).group(1)
        body=r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
using BOOL=int;using VOID=void;using ULONG=unsigned;using UINT=unsigned;using HRESULT=int32_t;using HANDLE=void*;using SRWLOCK=int;
#define AdmissionUmdMeasureNativeFlushStage 5u
static void AgxD3d10WindowsPresentMeasure(UINT,HRESULT,const UINT*,UINT){}
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_FAIL ((HRESULT)0x80004005u)
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define E_NOTIMPL ((HRESULT)0x80004001u)
#define HEAP_ZERO_MEMORY 1
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
static void InitializeSRWLock(SRWLOCK*p){*p=0;}
static void AcquireSRWLockExclusive(SRWLOCK*){}
static void ReleaseSRWLockExclusive(SRWLOCK*){}
static void* GetProcessHeap(){return nullptr;}
static void* HeapAlloc(void*,unsigned,size_t n){return calloc(1,n);}
static void HeapFree(void*,unsigned,void*p){free(p);}
@@HEADER@@
@@LIFETIME@@
struct ADMISSION_UMD_RESOURCE { ADMISSION_UMD_RETIREMENT* Retirement; unsigned KernelAllocation; };
struct ADMISSION_UMD_ASAHI_BATCH {struct {HRESULT RenderStatus,PostStatus;} Submission;};
struct ADMISSION_UMD_DEVICE {ADMISSION_UMD_RETIREMENT_QUEUE Retirement;void*NativeBatchTransaction;BOOL DrawTerminal;HRESULT LastScreenError,LastRetirementError;};
struct D3D10DDI_HDEVICE {void*pDrvPrivate;}; struct D3D10DDI_HRESOURCE {void*pDrvPrivate;};
static ADMISSION_UMD_DEVICE*AdmissionUmdDeviceFromHandle(D3D10DDI_HDEVICE x){return (ADMISSION_UMD_DEVICE*)x.pDrvPrivate;}
static ADMISSION_UMD_RESOURCE*AdmissionUmdResourceFromHandle(D3D10DDI_HRESOURCE x){return (ADMISSION_UMD_RESOURCE*)x.pDrvPrivate;}
static unsigned errors,closes,registered;static BOOL fail_deallocate;
static void AdmissionUmdSetError(ADMISSION_UMD_DEVICE*,HRESULT){++errors;}
@@DESTROY@@
struct AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE {AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE*Next;ADMISSION_UMD_RESOURCE Resource;};
struct AGX_D3D10_WINDOWS_DEVICE {int Stage;struct{int Failed;}Backend;ADMISSION_UMD_DEVICE Runtime;AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE*PendingPresentations;};
#define AgxD3d10DeviceReady 7
static void AgxWin32AsahiCollect(void*){}
static BOOL AdmissionUmdScreenAllocationRegistered(ADMISSION_UMD_DEVICE*,unsigned h){return h==registered;}
@@COLLECT@@
static void AgxD3d10WindowsDiagnosticState(AGX_D3D10_WINDOWS_DEVICE*,const char*){}
@@STATUS@@
@@DEFERRED@@
static HRESULT AgxD3d10WindowsQueryCollect(AGX_D3D10_WINDOWS_DEVICE*){return S_OK;}
struct Pipe{void(*flush)(Pipe*,void*,unsigned);};
struct Device{AGX_D3D10_WINDOWS_DEVICE*windows;Pipe*pipe;};
static Device*CastDevice(Device*x){return x;}
static void SetError(Device*,HRESULT){++errors;}
static void FrontendFlush(Device*hDevice){@@FLUSH@@}
static void pipeflush(Pipe*,void*,unsigned){}
static HRESULT deallocate(void*,HANDLE){if(fail_deallocate)return E_FAIL;++closes;return S_OK;}
static void report(void*c,HRESULT h){((ADMISSION_UMD_DEVICE*)c)->LastRetirementError=h;}
static void enqueue(AGX_D3D10_WINDOWS_DEVICE*d,unsigned h){
 auto*r=(AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE*)calloc(1,sizeof(AGX_D3D10_WINDOWS_PRESENTATION_RESOURCE));
 r->Resource.KernelAllocation=h;r->Resource.Retirement=AdmissionUmdRetirementCreate();
 r->Resource.Retirement->RuntimeResource=(void*)(uintptr_t)h;r->Resource.Retirement->Shared=TRUE;
 r->Next=d->PendingPresentations;d->PendingPresentations=r;
}
int main(){
 (void)&collect_presentations;
 AGX_D3D10_WINDOWS_DEVICE d={};d.Stage=AgxD3d10DeviceReady;
 AdmissionUmdRetirementInitialize(&d.Runtime.Retirement,&d.Runtime,deallocate,report);
 Pipe p={pipeflush};Device frontend={&d,&p};
 // Native BO still owns shared allocation: no callback, no queue consumption.
 ADMISSION_UMD_ASAHI_BATCH pending={};d.Runtime.NativeBatchTransaction=&pending;
 enqueue(&d,17);registered=17;FrontendFlush(&frontend);assert(closes==0&&d.PendingPresentations);
 // Completion removes BO, empty Flush must collect and deallocate shared alias.
 d.Runtime.NativeBatchTransaction=nullptr;registered=0;FrontendFlush(&frontend);assert(closes==1&&!d.PendingPresentations&&d.Runtime.Retirement.Count==0);
 // Repeated create/destroy on a stable device must not grow retirement queue.
 for(unsigned i=0;i<64;++i){enqueue(&d,100+i);FrontendFlush(&frontend);assert(d.Runtime.Retirement.Count==0);}
 assert(closes==65);
 // Callback failure retains exact record; retry closes exactly once.
 enqueue(&d,500);fail_deallocate=TRUE;FrontendFlush(&frontend);
 assert(errors==1&&closes==65&&d.Runtime.Retirement.Count==1);
 fail_deallocate=FALSE;FrontendFlush(&frontend);assert(closes==66&&d.Runtime.Retirement.Count==0);
 // Failed/uncertain submission cannot be papered over by cleanup.
 enqueue(&d,600);d.Runtime.DrawTerminal=TRUE;FrontendFlush(&frontend);assert(closes==66&&d.PendingPresentations);
 d.Runtime.DrawTerminal=FALSE;FrontendFlush(&frontend);assert(closes==67);
 puts("R148 shared retirement: PASS");
}
'''
        for tag,value in [('HEADER',header),('LIFETIME',lifetime),('DESTROY',function((umd/'src/umd.c').read_text(),'AdmissionUmdDestroyResource')),('COLLECT',function(winsys,'collect_presentations')),('STATUS',function(winsys,'AgxD3d10WindowsFlushStatus')),('DEFERRED',function(winsys,'AgxD3d10WindowsFlushDeferredResources')),('FLUSH',flush)]:
            body=body.replace("@@"+tag+"@@",value)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'replay.cpp';binary=Path(tmp)/'replay';src.write_text(body)
            built=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',str(src),'-o',str(binary)],text=True,capture_output=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(binary)],text=True,capture_output=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
            self.assertIn('R148 shared retirement: PASS',ran.stdout)
