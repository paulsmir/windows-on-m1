#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <cstdio>
#include <cstdlib>
// Host stand-in for unused Windows private-state lock; native ABI gate is separate.
struct SRWLOCK { uintptr_t value; };
static void InitializeSRWLock(SRWLOCK *p) { p->value = 0; }
using UINT=unsigned; using ULONG=unsigned; using BOOL=int; using LONG=int;
using UINT64=uint64_t; using HRESULT=int32_t; using D3DKMT_HANDLE=unsigned;
#define APIENTRY
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
static constexpr HRESULT S_OK=0, E_INVALIDARG=static_cast<HRESULT>(0x80070057u),
                         E_OUTOFMEMORY=static_cast<HRESULT>(0x8007000eu);
struct D3D12DDI_HRTDEVICE { void *handle; };
struct D3D12DDI_HADAPTER {};
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 { void (*pfnSetErrorCb)(D3D12DDI_HRTDEVICE,HRESULT); };
struct D3DDDI_DEVICECALLBACKS {};
struct D3D12DDIARG_CREATEDEVICE_0003 {
 D3D12DDI_HRTDEVICE hRTDevice; UINT Interface,Version;
 const D3DDDI_DEVICECALLBACKS *pKTCallbacks;
 D3D12DDI_HDEVICE hDrvDevice;
 const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *p12UMCallbacks;
 UINT Flags;
};
static ULONG GetCurrentProcessId() { return 11168; }
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static volatile LONG queue_generation;
static const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *callbacks;
static D3D12DDI_HRTDEVICE runtime_device;
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,UINT) {}
struct AGX_D3D12_OWNER { UINT serial; };
struct AGX_D3D12_OWNER_ARGS {
 const void *AdapterCallbacks; void *RuntimeAdapter;
 const D3DDDI_DEVICECALLBACKS *KernelCallbacks; void *RuntimeDevice;
 void (*Error)(void *,HRESULT); void *ErrorContext; ULONG Generation;
};
static struct { const void *callbacks; struct {void *handle;} runtime; } adapter_object;
static AGX_D3D12_OWNER fresh_owner;
static unsigned owner_calls;
static HRESULT owner_result=S_OK;
static AGX_D3D12_OWNER_ARGS captured;
static HRESULT AgxD3d12OwnerCreate(const AGX_D3D12_OWNER_ARGS *a,AGX_D3D12_OWNER **out) {
 ++owner_calls; captured=*a; *out=SUCCEEDED(owner_result)?&fresh_owner:nullptr; return owner_result;
}
/* Only the provider's CreateDevice registration contract is stubbed; the
 * Device/CreateDevice/EnsureOwner code under test is extracted unchanged. */
struct AgxCpuProvider {};
struct AgxCpuDevice { uint64_t value; };
enum class AgxCpuProviderStatus { Ok, ProjectOOM, Exhausted };
static AgxCpuProvider test_provider;
static unsigned provider_registrations;
static AgxCpuProvider *AgxCpuProviderCreate() { return &test_provider; }
static AgxCpuProviderStatus AgxCpuRegisterDevice(AgxCpuProvider *,D3D12DDI_HDEVICE,
                                                 void *,void *,AgxCpuDevice *out) {
 ++provider_registrations;out->value=provider_registrations;return AgxCpuProviderStatus::Ok;
}
static AgxCpuProviderStatus AgxCpuProviderDestroy(AgxCpuProvider **p) {
 *p=nullptr;return AgxCpuProviderStatus::Ok;
}
struct AgxD3d12Device;
static void PublishDeviceIdentity(AgxD3d12Device *) {}
/* PRODUCTION_DEVICE */
static void check(bool value,const char *why) { if(!value){ std::fprintf(stderr,"%s\n",why); std::exit(1); } }
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 um={};
 const D3DDDI_DEVICECALLBACKS kt={};
 AgxD3d12Device device;
 D3D12DDIARG_CREATEDEVICE_0003 a={};
 a.hRTDevice.handle=&a; a.hDrvDevice.pDrvPrivate=&device; a.p12UMCallbacks=&um; a.pKTCallbacks=&kt;
 if(std::strcmp(argv[1],"invalid")==0){
  std::memset(&device,0xa5,sizeof device); auto previous=device;
  check(CreateDevice({},nullptr)==E_INVALIDARG,"null args accepted");
  a.hDrvDevice.pDrvPrivate=nullptr;
  check(CreateDevice({},&a)==E_INVALIDARG,"null private storage accepted");
  check(std::memcmp(&device,&previous,sizeof device)==0,"invalid call mutated private storage"); return 0;
 }
 for(unsigned poison: {0xffu,0xa5u}) {
  std::memset(&device,poison,sizeof device);
  check(CreateDevice({},&a)==S_OK,"CreateDevice failed");
  check(device.runtime.handle==a.hRTDevice.handle && device.callbacks==&um && device.kernel==&kt,"runtime callbacks lost");
  check(device.paging_queue==0 && device.paging_fence==nullptr,"stale paging state");
  check(device.owner==nullptr && !device.owner_failed,"stale lazy owner");
  check(device.shader_provider==&test_provider &&
        device.shader_provider_device.value==provider_registrations,
        "stale shader provider registration");
  check(device.heap_count==0,"stale heap count");
  for(auto *heap:device.heaps)check(heap==nullptr,"stale heap pointer");
  check(device.generation!=0,"generation missing");
  unsigned before=owner_calls;
  owner_result=std::strcmp(argv[1],"failure")==0?E_INVALIDARG:S_OK;
  auto *owner=EnsureOwner(&device);
  check(owner_calls==before+1,"fresh lazy owner not created exactly once");
  check(captured.KernelCallbacks==&kt && captured.RuntimeDevice==a.hRTDevice.handle && captured.ErrorContext==&device && captured.Generation==device.generation,"owner creation provenance mismatch");
  check(owner==(SUCCEEDED(owner_result)?&fresh_owner:nullptr),"stale or failed owner returned");
  check(EnsureOwner(&device)==owner && owner_calls==before+1,"cached owner result not reused");
 }
 return 0;
}
