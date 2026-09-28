"""Derive Windows real-DDI ABI fixture from the checked-in focused regression."""
import importlib.util
from pathlib import Path
root=Path(__file__).resolve().parents[3]
base=Path(__file__).resolve().parent/'windows-replay';base.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('r148',root/'tests/test_r148_shared_retirement.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Save:
 def __enter__(self):return str(base)
 def __exit__(self,*args):return False
m.tempfile.TemporaryDirectory=Save
m.SharedRetirement().test_frontend_flush_releases_only_native_retired_shared_resources()
s=(base/'replay.cpp').read_text();start=s.index('using BOOL=');end=s.index('#ifndef APPLE_AGX_UMD_RESOURCE_LIFETIME_H')
s=s[:start]+'''#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#include <d3d10umddi.h>
#pragma warning(pop)
'''+s[end:]
s=s.replace('struct D3D10DDI_HDEVICE {void*pDrvPrivate;}; struct D3D10DDI_HRESOURCE {void*pDrvPrivate;};','')
s=s.replace('static Device*CastDevice(Device*x){return x;}','static Device*CastDevice(D3D10DDI_HDEVICE x){return (Device*)x.pDrvPrivate;}')
s=s.replace('static void SetError(Device*,HRESULT)','static void SetError(D3D10DDI_HDEVICE,HRESULT)')
s=s.replace('static void FrontendFlush(Device*hDevice)','static void APIENTRY ActualFrontendFlush(D3D10DDI_HDEVICE hDevice)')
s=s.replace('static void pipeflush(','''static void FrontendFlush(Device*x){
 D3D10DDI_HDEVICE h={};h.pDrvPrivate=x;
 D3D10DDI_DEVICEFUNCS functions={};functions.pfnFlush=ActualFrontendFlush;
 functions.pfnFlush(h);
}
static void pipeflush(''')
s=s.replace('static HRESULT deallocate(', 'static HRESULT APIENTRY deallocate(').replace('static void report(', 'static void APIENTRY report(')
s=s.replace(' r->Resource.KernelAllocation=h;', ' if(!r) abort();\n r->Resource.KernelAllocation=h;').replace(' r->Resource.Retirement->RuntimeResource=', ' if(!r->Resource.Retirement) abort();\n r->Resource.Retirement->RuntimeResource=')
(base/'windows-replay.cpp').write_text(s)
