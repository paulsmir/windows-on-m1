"""EXP975: hRTResource dies when DestroyResource(D3D10) returns.

The D3D10-DDI runtime frees its resource object as soon as the driver's
DestroyResource returns (EXP975 PageHeap dump: DeallocateCB dereferenced a freed
hResource from AdmissionUmdDeallocateResource). Any pfnDeallocateCb that names
the runtime resource must therefore run inside DestroyResource; only HandleList
deallocations may be deferred to Flush.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_r148_shared_retirement import function

ROOT = Path(__file__).resolve().parents[1]

BODY = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
using BOOL=int;using VOID=void;using ULONG=unsigned;using UINT=unsigned;using HRESULT=int32_t;using HANDLE=void*;using SRWLOCK=int;
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
inline void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,UINT){}
@@HEADER@@
@@LIFETIME@@
struct ADMISSION_UMD_RESOURCE { ADMISSION_UMD_RETIREMENT* Retirement; unsigned KernelAllocation; };
struct ADMISSION_UMD_DEVICE {ADMISSION_UMD_RETIREMENT_QUEUE Retirement;HRESULT LastRetirementError;};
struct D3D10DDI_HDEVICE {void*pDrvPrivate;}; struct D3D10DDI_HRESOURCE {void*pDrvPrivate;};
static ADMISSION_UMD_DEVICE*AdmissionUmdDeviceFromHandle(D3D10DDI_HDEVICE x){return (ADMISSION_UMD_DEVICE*)x.pDrvPrivate;}
static ADMISSION_UMD_RESOURCE*AdmissionUmdResourceFromHandle(D3D10DDI_HRESOURCE x){return (ADMISSION_UMD_RESOURCE*)x.pDrvPrivate;}
static unsigned errors,closes,stale;
static void AdmissionUmdSetError(ADMISSION_UMD_DEVICE*,HRESULT){++errors;}
/* Runtime resource objects: alive until the runtime's DestroyResource returns. */
static bool runtime_alive[8];
static HRESULT deallocate(void*,const ADMISSION_UMD_RETIREMENT*r){
 /* Mirrors AdmissionUmdDeallocateResource: only this shape uses HandleList. */
 bool names_runtime=!(r->Origin==1u&&!r->Primary&&!r->Shared&&r->KernelResource==0u);
 uintptr_t h=(uintptr_t)r->RuntimeResource;
 if(names_runtime && h<8 && !runtime_alive[h]) ++stale;
 ++closes;return S_OK;}
static void report(void*c,HRESULT h){((ADMISSION_UMD_DEVICE*)c)->LastRetirementError=h;}
@@RELEASE@@
@@DESTROY@@
/* What the runtime does: call DestroyResource, then free its own object. */
static void runtime_destroy(ADMISSION_UMD_DEVICE*d,ADMISSION_UMD_RESOURCE*res,unsigned h){
 D3D10DDI_HDEVICE dh={d};D3D10DDI_HRESOURCE rh={res};
 AdmissionUmdDestroyResource(dh,rh);
 runtime_alive[h]=false;
}
static ADMISSION_UMD_RESOURCE make(unsigned h,ULONG origin,BOOL primary,BOOL shared,ULONG km){
 ADMISSION_UMD_RESOURCE r={};r.KernelAllocation=0x40000000u+h;
 r.Retirement=AdmissionUmdRetirementCreate();
 r.Retirement->RuntimeResource=(void*)(uintptr_t)h;r.Retirement->Origin=origin;
 r.Retirement->Primary=primary;r.Retirement->Shared=shared;
 r.Retirement->KernelResource=km;r.Retirement->KernelAllocation=r.KernelAllocation;
 runtime_alive[h]=true;return r;
}
int main(){
 ADMISSION_UMD_DEVICE d={};
 AdmissionUmdRetirementInitialize(&d.Retirement,&d,deallocate,report);
 /* Opened shared surface (EXP974/975 crash), created shared, KM-resource and
  * shared-primary retirements all name hRTResource. */
 ADMISSION_UMD_RESOURCE opened=make(1,2u,FALSE,TRUE,0x80004bc0u);
 ADMISSION_UMD_RESOURCE shared=make(2,1u,FALSE,TRUE,0x80004c00u);
 ADMISSION_UMD_RESOURCE kmres=make(3,1u,FALSE,FALSE,0x80004c40u);
 ADMISSION_UMD_RESOURCE primary=make(4,1u,TRUE,TRUE,0x80004c80u);
 runtime_destroy(&d,&opened,1);runtime_destroy(&d,&shared,2);
 runtime_destroy(&d,&kmres,3);runtime_destroy(&d,&primary,4);
 assert(AdmissionUmdRetirementDrain(&d.Retirement));
 if(stale){printf("stale hRTResource deallocations: %u\n",stale);return 1;}
 assert(closes==4&&d.Retirement.Count==0&&errors==0);
 /* A HandleList-only allocation never names hRTResource: deferral stays legal. */
 ADMISSION_UMD_RESOURCE plain=make(5,1u,FALSE,FALSE,0u);
 runtime_destroy(&d,&plain,5);
 assert(closes==4&&d.Retirement.Count==1);
 assert(AdmissionUmdRetirementDrain(&d.Retirement)&&closes==5&&d.Retirement.Count==0);
 /* Presentation destroy releases the runtime part immediately; the later
  * deferred native destroy must not deallocate again. */
 ADMISSION_UMD_RESOURCE late=make(6,2u,FALSE,TRUE,0x80004cc0u);
 D3D10DDI_HDEVICE dh={&d};D3D10DDI_HRESOURCE rh={&late};
 assert(AdmissionUmdReleaseRuntimeResource(dh,rh)==S_OK&&closes==6&&!late.Retirement);
 runtime_alive[6]=false;
 AdmissionUmdDestroyResource(dh,rh);assert(AdmissionUmdRetirementDrain(&d.Retirement));
 if(!(closes==6&&stale==0&&errors==0)){printf("closes=%u stale=%u errors=%u\n",closes,stale,errors);return 1;}
 puts("EXP975 runtime resource release: PASS");
}
'''


class RuntimeResourceRelease(unittest.TestCase):
    def test_runtime_resource_is_never_used_after_destroy_returns(self):
        umd = ROOT / 'drivers/apple-agx/render-admission/umd'
        source = (umd / 'src/umd.c').read_text()
        lifetime = '\n'.join(x for x in (umd / 'src/umd_resource_lifetime.c').read_text().splitlines()
                             if not x.startswith('#include'))
        header = (umd / 'include/umd_resource_lifetime.h').read_text().replace('#include <windows.h>', '')
        needs = function(source.replace('static BOOL AdmissionUmdRetirementNeedsRuntimeResource',
                                        'BOOL AdmissionUmdRetirementNeedsRuntimeResource'),
                         'AdmissionUmdRetirementNeedsRuntimeResource')
        release = function(source, 'AdmissionUmdReleaseRuntimeResource')
        body = BODY
        for tag, value in [('HEADER', header), ('LIFETIME', lifetime),
                           ('RELEASE', needs + '\n' + release),
                           ('DESTROY', function(source, 'AdmissionUmdDestroyResource'))]:
            body = body.replace('@@' + tag + '@@', value)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'
            binary = Path(tmp) / 'replay'
            src.write_text(body)
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', str(src), '-o', str(binary)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP975 runtime resource release: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
