#include <cstdint>
#include <initializer_list>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
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
static LONG InterlockedCompareExchange(volatile LONG *p,LONG exchange,LONG comparand) {
 LONG expected=comparand;
 __atomic_compare_exchange_n(p,&expected,exchange,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 return expected;
}
static LONG InterlockedExchange(volatile LONG *p,LONG value) {
 return __atomic_exchange_n(p,value,__ATOMIC_SEQ_CST);
}
static void *InterlockedCompareExchangePointer(void *volatile *p,void *exchange,void *comparand) {
 void *expected=comparand;
 __atomic_compare_exchange_n(p,&expected,exchange,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 return expected;
}
static void *InterlockedExchangePointer(void *volatile *p,void *value) {
 return __atomic_exchange_n(p,value,__ATOMIC_SEQ_CST);
}
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
struct AgxD3d12Device;
static AGX_D3D12_OWNER *EnsureOwner(AgxD3d12Device *);
static struct { const void *callbacks; struct {void *handle;} runtime; } adapter_object;
static AGX_D3D12_OWNER fresh_owner, race_owner_a{1}, race_owner_b{2};
static unsigned owner_calls;
static HRESULT owner_result=S_OK;
static AGX_D3D12_OWNER_ARGS captured;
static bool race_mode, release_first, release_second;
static bool reenter_mode;
static unsigned nested_calls;
static AGX_D3D12_OWNER *nested_owner;
static unsigned race_entries;
static std::mutex race_mutex;
static std::condition_variable race_cv;
static HRESULT AgxD3d12OwnerCreate(const AGX_D3D12_OWNER_ARGS *a,AGX_D3D12_OWNER **out) {
 if(race_mode){
  std::unique_lock<std::mutex> lock(race_mutex);
  const unsigned slot=++race_entries;++owner_calls;race_cv.notify_all();
  if(!race_cv.wait_for(lock,std::chrono::seconds(5),[&]{return slot==1?release_first:release_second;}))
   return E_INVALIDARG;
  *out=slot==1?&race_owner_a:&race_owner_b;return S_OK;
 }
 ++owner_calls; captured=*a;
 if(reenter_mode&&owner_calls==1){
  ++nested_calls;
  nested_owner=EnsureOwner(static_cast<AgxD3d12Device *>(a->ErrorContext));
 }
 *out=SUCCEEDED(owner_result)?&fresh_owner:nullptr; return owner_result;
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
static void PublishDeviceIdentity(AgxD3d12Device *) {}
/* C53 HW1: the self-test runs only with APPLE_AGX_D3D12_RESERVED_SELFTEST=1;
 * this environment has none, so CreateDevice must never call it. */
static unsigned selftest_calls;
static unsigned long GetEnvironmentVariableA(const char *, char *, unsigned long) { return 0; }
static HRESULT AgxD3d12OwnerReservedSelfTest(AGX_D3D12_OWNER *) { ++selftest_calls; return S_OK; }
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
 if(std::strcmp(argv[1],"prewarm")==0){
  std::memset(&device,0xa5,sizeof device);
  const unsigned before=owner_calls;
  check(CreateDevice({},&a)==S_OK,"prewarm device creation");
  check(owner_calls==before+1&&device.owner==&fresh_owner&&!device.owner_failed,
        "CreateDevice did not publish one native owner");
  check(EnsureOwner(&device)==&fresh_owner&&owner_calls==before+1,
        "post-CreateDevice owner was not cached");
  check(selftest_calls==0,"reserved self-test ran without its opt-in");
  return 0;
 }
 if(std::strcmp(argv[1],"reenter")==0){
  std::memset(&device,0xa5,sizeof device);
  reenter_mode=true;
  check(CreateDevice({},&a)==S_OK,"reentrant device creation");
  check(owner_calls==1&&nested_calls==1&&nested_owner==nullptr&&
        device.owner==&fresh_owner&&!device.owner_failed,
        "reentrant owner create published another owner");
  return 0;
 }
 if(std::strcmp(argv[1],"race")==0){
  std::memset(&device,0xa5,sizeof device);
  check(CreateDevice({},&a)==S_OK,"race device creation");
  if(device.owner){
   const unsigned before=owner_calls;
   AGX_D3D12_OWNER *first=nullptr,*second=nullptr;
   std::thread ta([&]{first=EnsureOwner(&device);});
   std::thread tb([&]{second=EnsureOwner(&device);});
   ta.join();tb.join();
   std::printf("ACTUAL_OWNER_RACE calls=%u second_entered=0 first=%p second=%p published=%p\n",
               owner_calls,first,second,device.owner);
   check(owner_calls==before&&first==second&&device.owner==first,
         "cached owner changed under concurrent use");
   return 0;
  }
  race_mode=true;AGX_D3D12_OWNER *first=nullptr,*second=nullptr;
  std::thread ta([&]{first=EnsureOwner(&device);});
  {std::unique_lock<std::mutex> lock(race_mutex);
   check(race_cv.wait_for(lock,std::chrono::seconds(2),[]{return race_entries>=1;}),
         "first owner create entered");}
  std::thread tb([&]{second=EnsureOwner(&device);});
  bool second_entered=false;
  {std::unique_lock<std::mutex> lock(race_mutex);
   second_entered=race_cv.wait_for(lock,std::chrono::seconds(1),[]{return race_entries>=2;});
   release_first=true;race_cv.notify_all();}
  ta.join();
  {std::lock_guard<std::mutex> lock(race_mutex);release_second=true;race_cv.notify_all();}
  tb.join();
  std::printf("ACTUAL_OWNER_RACE calls=%u second_entered=%u first=%p second=%p published=%p\n",
              owner_calls,second_entered,first,second,device.owner);
  check(owner_calls==1&&first==second&&device.owner==first,
        "duplicate owner construction/publication");
  return 0;
 }
 for(unsigned poison: {0xffu,0xa5u}) {
  std::memset(&device,poison,sizeof device);
  owner_result=std::strcmp(argv[1],"failure")==0?E_INVALIDARG:S_OK;
  const unsigned before=owner_calls;
  check(CreateDevice({},&a)==S_OK,"CreateDevice failed");
  check(device.runtime.handle==a.hRTDevice.handle && device.callbacks==&um && device.kernel==&kt,"runtime callbacks lost");
  check(device.paging_queue==0 && device.paging_fence==nullptr,"stale paging state");
  auto *expected=SUCCEEDED(owner_result)?&fresh_owner:nullptr;
  check(owner_calls==before+1&&device.owner==expected&&
        device.owner_failed==FAILED(owner_result),"CreateDevice owner result not cached");
  check(device.shader_provider==&test_provider &&
        device.shader_provider_device.value==provider_registrations,
        "stale shader provider registration");
  check(device.heap_count==0,"stale heap count");
  for(auto *heap:device.heaps)check(heap==nullptr,"stale heap pointer");
  check(device.generation!=0,"generation missing");
  auto *owner=EnsureOwner(&device);
  check(owner_calls==before+1,"cached native owner created again");
  check(captured.KernelCallbacks==&kt && captured.RuntimeDevice==a.hRTDevice.handle && captured.ErrorContext==&device && captured.Generation==device.generation,"owner creation provenance mismatch");
  check(owner==(SUCCEEDED(owner_result)?&fresh_owner:nullptr),"stale or failed owner returned");
  check(EnsureOwner(&device)==owner && owner_calls==before+1,"cached owner result not reused");
 }
 return 0;
}
