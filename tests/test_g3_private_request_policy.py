"""The actual UMD private request builder must not demand global GPU idle."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from tests.test_g3_frame_arm_software_entry import function

SHIM = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
using UINT=uint32_t;using HRESULT=int32_t;using HANDLE=void*;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define ADMISSION_UMD_DEVICE_MAGIC 1
#define ADMISSION_UMD_ADAPTER_MAGIC 2
#define APPLE_AGX_G3_PRIVATE_RELEASE 3
struct APPLE_AGX_G3_PRIVATE_REQUEST {UINT Operation,Width,Height,UtileWidth,UtileHeight,Layers,Samples;};
struct EscapeFlags {union {struct {UINT HardwareAccess:1,Other:31;};UINT Value;};};
struct D3DDDICB_ESCAPE {HANDLE hDevice,hContext;EscapeFlags Flags;void *pPrivateDriverData;UINT PrivateDriverDataSize;};
struct RuntimeHandle {HANDLE handle;};
struct Adapter {UINT Magic;RuntimeHandle RuntimeAdapter;};
struct Callbacks {HRESULT (*pfnEscapeCb)(HANDLE,D3DDDICB_ESCAPE*);};
struct ADMISSION_UMD_DEVICE {UINT Magic;bool ScreenClosing;Adapter *Adapter;RuntimeHandle RuntimeDevice;HANDLE KernelContext;Callbacks *KernelCallbacks;};
static unsigned calls=0,lastFlags=~0u;
static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,size_t){}
static HRESULT capture(HANDLE adapter,D3DDDICB_ESCAPE *q){
 assert(adapter && q->hDevice && q->hContext);
 assert(q->pPrivateDriverData && q->PrivateDriverDataSize==sizeof(APPLE_AGX_G3_PRIVATE_REQUEST));
 ++calls;lastFlags=q->Flags.Value;return 0;
}
'''
END = r'''
int main(){
 int adapterToken=0,deviceToken=0,contextToken=0;
 Adapter a{ADMISSION_UMD_ADAPTER_MAGIC,{&adapterToken}};Callbacks cb{capture};
 ADMISSION_UMD_DEVICE d{ADMISSION_UMD_DEVICE_MAGIC,false,&a,{&deviceToken},&contextToken,&cb};
 APPLE_AGX_G3_PRIVATE_REQUEST q{};
 for(UINT operation=1;operation<=3;++operation){
  q.Operation=operation;assert(private_escape(&d,&q));assert(lastFlags==0);
 }
 assert(calls==3);d.ScreenClosing=true;assert(!private_escape(&d,&q));assert(calls==3);
}
'''

class PrivateRequestPolicyTests(unittest.TestCase):
    def test_actual_umd_acquire_prepare_release_use_software_entry(self):
        body = function('drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c', 'private_escape')
        with tempfile.TemporaryDirectory(prefix='private-request-') as temp:
            source=Path(temp)/'test.cpp';binary=Path(temp)/'test'
            source.write_text(SHIM+body+END)
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined',str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)
