/* Test-only negotiation probe. Never installed, never returns a usable device.
 * It advertises candidate interfaces solely to observe the real runtime ABI. */
#include <windows.h>
#include <wingdi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#ifdef AGX_RUNTIME_PROBE_DRIVER
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)

static int ProbeAdapter;
static void receipt(const char *format,...) {
  char path[MAX_PATH];FILE *file=NULL;va_list ap;
  if(!GetEnvironmentVariableA("AGX_RUNTIME_PROBE_LOG",path,MAX_PATH)) return;
  if(fopen_s(&file,path,"a") || !file) return;
  va_start(ap,format);vfprintf(file,format,ap);va_end(ap);
  fputc('\n',file);fclose(file);
}
void AgxRuntimeProbeKmtReceipt(const char *name) { receipt("SoftwareKMT %s",name); }
static SIZE_T APIENTRY size_device(D3D10DDI_HADAPTER adapter,
    const D3D10DDIARG_CALCPRIVATEDEVICESIZE *args) {
  (void)adapter;
  receipt("CalcPrivateDeviceSize interface=%08x version=%08x",
      args?args->Interface:0,args?args->Version:0);
  return 64;
}
static HRESULT APIENTRY create_device(D3D10DDI_HADAPTER adapter,
    D3D10DDIARG_CREATEDEVICE *args) {
  (void)adapter;
  if(!args) return E_INVALIDARG;
  receipt("CreateDevice interface=%08x version=%08x flags=%08x dxgi11=%u base_bytes=%llu extended_bytes=%llu resolve_offset=%llu table=%p",
      args->Interface,args->Version,args->Flags,
      (UINT)IS_DXGI1_1_BASE_FUNCTIONS(args->Interface,args->Version),
      (unsigned long long)sizeof(DXGI_DDI_BASE_FUNCTIONS),
      (unsigned long long)sizeof(DXGI1_1_DDI_BASE_FUNCTIONS),
      (unsigned long long)offsetof(DXGI1_1_DDI_BASE_FUNCTIONS,pfnResolveSharedResource),
      args->DXGIBaseDDI.pDXGIDDIBaseFunctions);
  receipt("IntentionalCreateFailure hr=80004005; no table written; post-create format queries NOT MEASURED");
  return E_FAIL;
}
static HRESULT APIENTRY close_adapter(D3D10DDI_HADAPTER adapter) {
  (void)adapter;receipt("CloseAdapter");return S_OK;
}
static HRESULT APIENTRY versions(D3D10DDI_HADAPTER adapter,UINT32 *count,UINT64 *values) {
  (void)adapter;
  if(!count) return E_INVALIDARG;
  receipt("GetSupportedVersions capacity=%u values=%u",*count,values!=NULL);
  if(!values) { *count=2;return S_OK; }
  if(*count<2) return E_INVALIDARG;
  values[0]=D3D10_0_DDI_SUPPORTED;values[1]=D3D10_0_x_DDI_SUPPORTED;
  *count=2;return S_OK;
}
static HRESULT APIENTRY caps(D3D10DDI_HADAPTER adapter,const D3D10_2DDIARG_GETCAPS *args) {
  (void)adapter;
  if(!args || !args->pData) return E_INVALIDARG;
  receipt("GetCaps type=%u bytes=%u",args->Type,args->DataSize);
  if(args->Type==D3D11DDICAPS_THREADING && args->DataSize==sizeof(D3D11DDI_THREADING_CAPS)) {
    ((D3D11DDI_THREADING_CAPS *)args->pData)->Caps=0;return S_OK;
  }
  if(args->Type==D3D11DDICAPS_3DPIPELINESUPPORT && args->DataSize==sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS)) {
    ((D3D11DDI_3DPIPELINESUPPORT_CAPS *)args->pData)->Caps=
        D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0);
    return S_OK;
  }
  return E_NOTIMPL;
}
__declspec(dllexport) HRESULT APIENTRY OpenAdapter10(D3D10DDIARG_OPENADAPTER *args) {
  if(!args || !args->pAdapterFuncs) return E_INVALIDARG;
  receipt("OpenAdapter10 interface=%08x version=%08x",args->Interface,args->Version);
  args->hAdapter.pDrvPrivate=&ProbeAdapter;
  args->pAdapterFuncs->pfnCalcPrivateDeviceSize=size_device;
  args->pAdapterFuncs->pfnCreateDevice=create_device;
  args->pAdapterFuncs->pfnCloseAdapter=close_adapter;
  return S_OK;
}
__declspec(dllexport) HRESULT APIENTRY OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *args) {
  if(!args || !args->pAdapterFuncs_2) return E_INVALIDARG;
  receipt("OpenAdapter10_2 interface=%08x version=%08x",args->Interface,args->Version);
  args->hAdapter.pDrvPrivate=&ProbeAdapter;
  args->pAdapterFuncs_2->pfnCalcPrivateDeviceSize=size_device;
  args->pAdapterFuncs_2->pfnCreateDevice=create_device;
  args->pAdapterFuncs_2->pfnCloseAdapter=close_adapter;
  args->pAdapterFuncs_2->pfnGetSupportedVersions=versions;
  args->pAdapterFuncs_2->pfnGetCaps=caps;
  return S_OK;
}
#else
#include <d3d11.h>
int wmain(int argc,wchar_t **argv) {
  HMODULE module;ID3D11Device *device=NULL;ID3D11DeviceContext *context=NULL;
  D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL chosen=(D3D_FEATURE_LEVEL)0;
  if(argc!=2) return 2;
  module=LoadLibraryW(argv[1]);
  if(!module) { printf("LoadLibrary error=%lu\n",GetLastError());return 3; }
  HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_SOFTWARE,module,0xa9u,
      requested,1,D3D11_SDK_VERSION,&device,&chosen,&context);
  printf("SoftwareProbe hr=%08lx selected_fl=%08x flags=000000a9\n",(ULONG)hr,(UINT)chosen);
  if(context) context->lpVtbl->Release(context);
  if(device) device->lpVtbl->Release(device);
  FreeLibrary(module);
  /* The receipt, not this exit code, establishes whether CreateDevice ran. */
  return 0;
}
#endif
