/* Original deterministic CPU phase oracle; actual core and zero-cache only.
 * No backend/compiler, fault injection or invalid-memory probes. SPDX: MIT */
#include <windows.h>
#include <d3d12umddi.h>
#include "agx_shader_input_provider.h"
#include "agx_d3d12_zero_cache_library.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>
using P=AgxCpuProviderStatus;
static AgxCpuProvider *provider;
static std::atomic<unsigned> checks{0};
static thread_local const void *expected_identity=nullptr;
static void need(bool value,const char *why){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::abort();}}
static void pause_resolver();
extern "C" const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE h){
 need(agx_host_srw_depth==0,"resolver outside actual zero-cache registry lock");
 pause_resolver();
 auto identity=AgxCpuResolvePinnedDeviceIdentity(provider,h);
 need(identity!=nullptr,"actual core/library call owns explicit device scope");
 if(expected_identity)need(identity==expected_identity,"resolver keeps pinned device generation");
 return identity;
}
extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE){std::abort();}
struct Signal {std::mutex mutex;std::condition_variable cv;bool set=false;
 void send(){std::lock_guard<std::mutex> lock(mutex);set=true;cv.notify_all();}
 void wait(){std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return set;});}};
struct ResolverPause {Signal *entered,*released;bool used=false;};
static thread_local ResolverPause *resolver_pause=nullptr;
static void pause_resolver(){if(resolver_pause&&!resolver_pause->used){resolver_pause->used=true;resolver_pause->entered->send();resolver_pause->released->wait();}}
struct Rendezvous {std::mutex mutex;std::condition_variable cv;unsigned arrivals=0;
 void wait(){std::unique_lock<std::mutex> lock(mutex);++arrivals;cv.notify_all();cv.wait(lock,[&]{return arrivals==2;});}};
struct Sink {D3D12DDI_HDEVICE raw{};const void *identity=nullptr;int extra_root=0;
 std::atomic<unsigned> calls{0};bool block_first=false;Signal entered,released;};
static void failure(void *context,AgxCpuDevice device,AgxCpuProviderResult)noexcept{
 auto&s=*static_cast<Sink*>(context);const unsigned number=++s.calls;
 need(agx_host_srw_depth==0,"sink outside zero-cache lock");
 need(AgxCpuResolvePinnedDeviceIdentity(provider,s.raw)==s.identity,"report owns scoped generation pin");
 AgxCpuRoot out{99};
 need(AgxCpuRegisterRoot(provider,device,{&s.extra_root},{nullptr,0},&out)==P::Reentrant&&!out.value,
      "sink mutation refused without recursion or publication");
 if(s.block_first&&number==1){s.entered.send();s.released.wait();}
}
static std::vector<unsigned char> read(const char *path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static const void *identity(AgxCpuDevice device,D3D12DDI_HDEVICE raw){
 AgxCpuDeviceScope scope{};need(AgxCpuBeginDeviceScope(provider,device,&scope)==P::Ok,"explicit device scope");
 auto value=AgxCpuResolvePinnedDeviceIdentity(provider,raw);need(value!=nullptr,"pinned identity");
 need(AgxCpuEndDeviceScope(provider,&scope)==P::Ok&&!scope.value,"scope ends same-thread LIFO");return value;
}
static AgxCpuInputLease lease(AgxCpuDevice device,AgxCpuRoot root,D3D12DDI_HROOTSIGNATURE raw,const std::vector<unsigned char>&program){
 D3D12DDIARG_CREATE_SHADER_0010 args{};args.hRootSignature=raw;args.pShaderCode=reinterpret_cast<const UINT*>(program.data());
 AgxCpuInputLease out{};need(AgxCpuAcquireInput(provider,device,root,&args,5,{program.data(),program.size()},{nullptr,0},{nullptr,0},&out).status==P::Ok,"actual borrowed input lease");return out;
}
int main(int argc,char **argv){
 need(argc==3,"actual DXIL and legacy CS PROGRAM inputs");auto dxil=read(argv[1]),legacy=read(argv[2]);need(dxil.size()==1680&&legacy.size()>=8,"real finite PROGRAMs");
 provider=AgxCpuProviderCreate();need(provider!=nullptr,"provider");
 int da=0,db=0,ra=0,rb=0;Sink sa,sb,sa2;sa.raw={&da};sb.raw={&db};sa2.raw={&da};AgxCpuDevice a{},b{},a2{};
 need(AgxCpuRegisterDevice(provider,{&da},failure,&sa,&a)==P::Ok,"device A");
 need(AgxCpuRegisterDevice(provider,{&db},failure,&sb,&b)==P::Ok,"device B");
 sa.identity=identity(a,{&da});sb.identity=identity(b,{&db});
 need(!AgxCpuResolvePinnedDeviceIdentity(provider,{&da}),"new resolver refuses unscoped lookup");
 std::array<unsigned char,16> canonicalA{},canonicalB{};canonicalA.fill(0x31);canonicalB.fill(0x72);AgxCpuRoot ar{},br{};
 need(AgxCpuRegisterRoot(provider,a,{&ra},{canonicalA.data(),canonicalA.size()},&ar)==P::Ok,"root A");
 need(AgxCpuRegisterRoot(provider,b,{&rb},{canonicalB.data(),canonicalB.size()},&br)==P::Ok,"root B");
 // Actual zero-cache helpers are unchanged; this NEW client explicitly brackets them.
 AgxCpuDeviceScope lib_scope{};need(AgxCpuBeginDeviceScope(provider,a,&lib_scope)==P::Ok,"library scope");expected_identity=sa.identity;
 D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 empty{};alignas(64)unsigned char library_storage[128];
 need(AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize({&da},&empty)<=sizeof(library_storage),"actual library private size");
 D3D12DDI_HPIPELINELIBRARY library{library_storage};need(AgxD3d12ZeroCacheCreatePipelineLibrary({&da},&empty,library)==S_OK,"actual library create in scope");
 unsigned char canary=0xa5;need(!AgxD3d12ZeroCacheCalcSerializedLibrarySize({&da},library),"zero-cache extent");
 need(AgxD3d12ZeroCacheSerializeLibrary({&da},library,&canary)==S_OK&&canary==0xa5,"actual serialize zero writes");
 AgxD3d12ZeroCacheDestroyPipelineLibrary({&da},library);expected_identity=nullptr;
 need(AgxCpuEndDeviceScope(provider,&lib_scope)==P::Ok,"library children destroyed before retirement");
 auto la=lease(a,ar,{&ra},dxil),lb=lease(b,br,{&rb},legacy);AgxCpuOperation oa{},ob{};
 // Scheduling latch inside the REAL resolver called by the REAL core: no
 // substituted core algorithm or injected failure. Other actual core progresses.
 Signal core_entered,core_released;AgxCpuOperation stalled{};
 need(AgxCpuBeginOperation(provider,la,&stalled).status==P::Ok,"stalled actual core operation");
 std::thread core_thread([&]{ResolverPause pause{&core_entered,&core_released};resolver_pause=&pause;expected_identity=sa.identity;
  AgxShaderOwnedLayout layout{};need(AgxCpuCalculatePinned(provider,stalled,&layout).status==P::Ok,"actual core resumes after resolver latch");resolver_pause=nullptr;expected_identity=nullptr;});
 core_entered.wait();need(AgxCpuEndOperation(provider,&stalled)==P::Busy,"End is Busy during actual core callback");
 AgxShaderOwnedLayout independent{};expected_identity=sb.identity;
 need(AgxCpuCalculate(provider,lb,&independent).status==P::Ok,"other actual core completes while first resolver waits");expected_identity=nullptr;
 core_released.send();core_thread.join();need(AgxCpuEndOperation(provider,&stalled)==P::Ok,"stalled core pins released");

 need(AgxCpuBeginOperation(provider,la,&oa).status==P::Ok&&AgxCpuBeginOperation(provider,lb,&ob).status==P::Ok,"independent Ready operations");
 need(AgxCpuReleaseInput(provider,la)==P::Busy,"Ready operation retains caller borrow");
 Rendezvous both;AgxShaderOwnedLayout layoutA{},layoutB{};
 std::thread ta([&]{expected_identity=sa.identity;both.wait();need(AgxCpuCalculatePinned(provider,oa,&layoutA).status==P::Ok,"actual parallel core A");expected_identity=nullptr;});
 std::thread tb([&]{expected_identity=sb.identity;both.wait();need(AgxCpuCalculatePinned(provider,ob,&layoutB).status==P::Ok,"actual parallel core B");expected_identity=nullptr;});
 ta.join();tb.join();need(layoutA.total>sizeof(AgxShaderOwned)&&layoutB.total>sizeof(AgxShaderOwned),"real layouts");
 need(AgxCpuEndOperation(provider,&oa)==P::Ok&&AgxCpuEndOperation(provider,&ob)==P::Ok,"Finished pins released");
 // Reuse root handle while an old lease still owns immutable bytes.
 need(AgxCpuRetireRoot(provider,a,ar)==P::Ok,"retire root A");AgxCpuRoot ar2{};
 need(AgxCpuRegisterRoot(provider,a,{&ra},{canonicalB.data(),canonicalB.size()},&ar2)==P::Ok&&ar2.value!=ar.value,"root generation reuse");
 AgxCpuInputLease stale{};D3D12DDIARG_CREATE_SHADER_0010 args{};args.hRootSignature={&ra};args.pShaderCode=reinterpret_cast<const UINT*>(dxil.data());
 need(AgxCpuAcquireInput(provider,a,ar,&args,5,{dxil.data(),dxil.size()},{nullptr,0},{nullptr,0},&stale).status==P::NotLiveRoot&&!stale.value,"stale root ID refuses new acquisition");
 need(AgxCpuBeginOperation(provider,la,&oa).status==P::Ok,"old admitted root bytes survive retirement");
 AgxShaderTrustedSpan root{};need(AgxCpuOperationRootBytes(provider,oa,&root)==P::Ok&&root.bytes==canonicalA.size()&&!std::memcmp(root.data,canonicalA.data(),root.bytes),"operation owns old root A bytes");
 alignas(64)std::array<unsigned char,8192> ownedA{};AgxShaderOwned *copiedA=nullptr;expected_identity=sa.identity;
 need(AgxCpuCopyPinned(provider,oa,ownedA.data(),ownedA.size(),&copiedA).status==P::Ok&&copiedA,"actual owned copy");expected_identity=nullptr;
 std::vector<unsigned char> saved(ownedA.data(),ownedA.data()+copiedA->layout.total);
 need(AgxCpuEndOperation(provider,&oa)==P::Ok,"copy operation released");
 // Block an actual failure sink, not a replacement core. Other registry work progresses.
 need(AgxCpuBeginOperation(provider,lb,&ob).status==P::Ok,"reporting operation");sb.block_first=true;
 alignas(64)std::array<unsigned char,8192> outputB{};AgxShaderOwned *failedOwned=nullptr;AgxCpuProviderResult failureB{};
 std::thread reporting([&]{expected_identity=sb.identity;failureB=AgxCpuCopyPinned(provider,ob,outputB.data(),1,&failedOwned);expected_identity=nullptr;});
 sb.entered.wait();
 need(AgxCpuEndOperation(provider,&ob)==P::Busy&&AgxCpuReleaseInput(provider,lb)==P::Busy,"Running includes blocked sink, input remains pinned");
 int extra=0;AgxCpuRoot extra_root{};need(AgxCpuRegisterRoot(provider,b,{&extra},{nullptr,0},&extra_root)==P::Ok,"registry progresses while sink waits outside lock");
 need(AgxCpuRetireRoot(provider,b,extra_root)==P::Ok,"unrelated root retirement progresses");
 need(AgxCpuProviderDestroy(&provider)==P::Busy&&provider,"no domain destruction through live operation");
 need(AgxCpuRetireDevice(provider,b)==P::Ok,"retire B leaves sink admission open");unsigned before=sb.calls.load();AgxCpuOperation refused{99};
 need(AgxCpuBeginOperation(provider,lb,&refused).status==P::RetiredDevice&&!refused.value&&sb.calls==before+1,"known lease retired failure still reports before quiescence");
 AgxCpuQuiesceTicket qb{};need(AgxCpuBeginQuiesce(provider,b,&qb)==P::Ok,"explicit close sink gate");
 need(AgxCpuTryQuiesce(provider,&qb)==P::Busy&&qb.value,"in-flight core/report ticket drains first");
 before=sb.calls.load();AgxShaderOwnedLayout rejected{};need(AgxCpuCalculate(provider,lb,&rejected).status==P::RetiredDevice&&sb.calls==before,"no new sink ticket after gate closing");
 sb.released.send();reporting.join();need(failureB.status==P::CoreFailure&&failureB.core_status==AgxShaderStorageStatus::StorageInvalid&&!failedOwned,"actual reported core refusal unpublished");
 need(AgxCpuEndOperation(provider,&ob)==P::Ok,"drain reporting operation");need(AgxCpuTryQuiesce(provider,&qb)==P::Ok&&!qb.value,"explicit quiescence complete");
 before=sb.calls.load();need(AgxCpuCalculate(provider,lb,&rejected).status==P::RetiredDevice&&sb.calls==before,"no callbacks after successful quiescence");
 need(AgxCpuReleaseInput(provider,lb)==P::Ok,"retired memory releases after quiescence");
 // Pinned resolver generation survives device-handle reuse and nested scopes.
 need(AgxCpuBeginOperation(provider,la,&oa).status==P::Ok,"admit old A before device retirement");
 AgxCpuDeviceScope old_scope{},new_scope{};need(AgxCpuBeginDeviceScope(provider,a,&old_scope)==P::Ok,"old A explicit scope");
 need(AgxCpuRetireDevice(provider,a)==P::Ok,"retire A");need(AgxCpuRegisterDevice(provider,{&da},failure,&sa2,&a2)==P::Ok&&a2.value!=a.value,"device handle reuse new generation");
 need(AgxCpuResolvePinnedDeviceIdentity(provider,{&da})==sa.identity,"old scope is not current raw-handle lookup");
 need(AgxCpuBeginDeviceScope(provider,a2,&new_scope)==P::Ok,"new A scope");sa2.identity=AgxCpuResolvePinnedDeviceIdentity(provider,{&da});
 need(sa2.identity&&sa2.identity!=sa.identity,"new generation distinct identity");
 need(AgxCpuEndDeviceScope(provider,&old_scope)==P::Invalid,"scope LIFO enforced");
 need(AgxCpuEndDeviceScope(provider,&new_scope)==P::Ok&&AgxCpuResolvePinnedDeviceIdentity(provider,{&da})==sa.identity,"restore old pinned generation");
 before=sa.calls.load();refused={99};need(AgxCpuBeginOperation(provider,la,&refused).status==P::RetiredDevice&&!refused.value&&sa.calls==before+1,"retired reporting uses pinned old generation despite live handle reuse");
 expected_identity=sa.identity;need(AgxCpuCalculatePinned(provider,oa,&layoutA).status==P::Ok,"admitted actual core finishes after device closing");expected_identity=nullptr;
 need(AgxCpuCalculatePinned(provider,oa,&rejected).status==P::Invalid&&!rejected.total,"one-use operation refuses second execution");
 need(AgxCpuEndOperation(provider,&oa)==P::Ok&&AgxCpuEndDeviceScope(provider,&old_scope)==P::Ok,"end old operation/scope");
 AgxCpuQuiesceTicket qa{};need(AgxCpuBeginQuiesce(provider,a,&qa)==P::Ok&&AgxCpuTryQuiesce(provider,&qa)==P::Ok,"old device quiescence despite retained root lease");
 need(AgxCpuReleaseInput(provider,la)==P::Ok,"old A input release");dxil.assign(dxil.size(),0xcc);canonicalA.fill(0xcc);
 need(!std::memcmp(ownedA.data(),saved.data(),saved.size()),"owned data survives borrow release/source overwrite");
 need(!AgxCpuResolvePinnedDeviceIdentity(provider,{&da}),"new resolver still refuses unscoped lookup");
 need(AgxCpuProviderDestroy(&provider)==P::Ok&&!provider,"client joins all threads before final raw-pointer domain destruction");
 std::printf("PASS concurrent-provider checks=%u actual_core=YES actual_zero_cache=YES legacy_lookup_separate=YES operations_retire_reuse_sink_quiescence=YES noDDIpolicy\n",checks.load());
}
