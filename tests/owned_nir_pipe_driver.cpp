/* Original actual provider/core/frontends composition, explicit CPU storage. MIT */
#include <windows.h>
#ifdef _WIN32
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable:4201)
#endif
#include <d3d12umddi.h>
#ifdef _WIN32
#pragma warning(pop)
#endif
#include "agx_shader_input_provider.h"
#include "agx_d3d12_zero_cache_library.h"
#include "agx_owned_program_nir.h"
#include <array>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
using WrapperCreate=agx_owned_nir_pipe_status (*)(const void *,pipe_context *,agx_owned_nir_pipe_state *);
extern "C" void agx_owned_nir_pipe_fixture(nir_shader *,const dxbc_binding_map *,const uint32_t *,unsigned,const char *,WrapperCreate,const void *);
static unsigned wrapper_valid_calls,wrapper_null_calls,wrapper_snapshot_checks;
static void wrapper_check(bool p,const char *why){++wrapper_snapshot_checks;if(!p){std::fprintf(stderr,"WRAPPER_SNAPSHOT_FAIL %s\n",why);std::abort();}}
static_assert(std::is_same<decltype(&AgxOwnedNirCreatePipeState),agx_owned_nir_pipe_status (*)(const AgxOwnedNir *,pipe_context *,agx_owned_nir_pipe_state *) noexcept>::value,"actual wrapper typed noexcept signature");
extern "C" agx_owned_nir_pipe_status actual_wrapper(const void *opaque,pipe_context *pipe,agx_owned_nir_pipe_state *out){
 if(opaque)++wrapper_valid_calls;else ++wrapper_null_calls;
 return AgxOwnedNirCreatePipeState(static_cast<const AgxOwnedNir *>(opaque),pipe,out);
}
extern "C" void agx_owned_nir_pipe_fixture_finish(void);
static AgxCpuProvider *domain;static unsigned checks;
static void need(bool p,const char *why){++checks;if(!p){std::fprintf(stderr,"FAIL %s\n",why);std::abort();}}
extern "C" const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE h){
#ifndef _WIN32
 need(agx_host_srw_depth==0,"resolver outside library lock");
#endif
 auto id=AgxCpuResolvePinnedDeviceIdentity(domain,h);
#ifdef _WIN32
 need(id!=nullptr,"native actual resolver owns pinned device identity");
#endif
 return id;
}
extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE){std::abort();}
static void sink(void *,AgxCpuDevice,AgxCpuProviderResult)noexcept{std::abort();}
static std::vector<unsigned char> read(const char *p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
 need(argc==6,"mixedPROGRAM/legacyCS/VS/PS/goldvectors");
 static_assert(std::is_same<decltype(AgxOwnedNirBindings(nullptr)),const dxbc_binding_map*>::value,"map getter const-only");
 domain=AgxCpuProviderCreate();need(domain!=nullptr,"actual provider");int device_storage=0;AgxCpuDevice device{};
 need(AgxCpuRegisterDevice(domain,{&device_storage},sink,nullptr,&device)==AgxCpuProviderStatus::Ok,"actual device registration");
 unsigned accepted=0,refused=0;
 for(unsigned n=0;n<4;n++){
  const unsigned stage=n==2?1u:n==3?0u:5u;auto source=read(argv[n+1]);need(source.size()>=8,"real PROGRAM input");
  std::array<unsigned char,29> canonical;canonical.fill(static_cast<unsigned char>(0x41+n));const auto saved_root=canonical;int rawroot=0;AgxCpuRoot root{};
  need(AgxCpuRegisterRoot(domain,device,{&rawroot},{canonical.data(),canonical.size()},&root)==AgxCpuProviderStatus::Ok,"actual root owned registration");
  D3D12DDIARG_CREATE_SHADER_0010 args{};args.hRootSignature={&rawroot};args.pShaderCode=reinterpret_cast<const UINT*>(source.data());AgxCpuInputLease lease{};
  need(AgxCpuAcquireInput(domain,device,root,&args,stage,{source.data(),source.size()},{nullptr,0},{nullptr,0},&lease).status==AgxCpuProviderStatus::Ok,"actual borrowed input");
  AgxCpuOperation operation{};need(AgxCpuBeginOperation(domain,lease,&operation).status==AgxCpuProviderStatus::Ok,"actual pinned copy operation");
  alignas(64)std::array<unsigned char,8192> storage{};AgxShaderOwned *owned=nullptr;
  need(AgxCpuCopyPinned(domain,operation,storage.data(),storage.size(),&owned).status==AgxCpuProviderStatus::Ok&&owned,"actual core copied object");
  need(AgxCpuEndOperation(domain,&operation)==AgxCpuProviderStatus::Ok,"operation ends");
  need(AgxCpuReleaseInput(domain,lease)==AgxCpuProviderStatus::Ok,"legitimate input release");
  std::memset(source.data(),0xcc,source.size());canonical.fill(0xcc);std::memset(&args,0xcc,sizeof(args));
  AgxOwnedNirRequest request{owned,owned->layout.total,"main",4,{14,128,8,16}};AgxOwnedNir *result=nullptr;
  auto short_request=request;short_request.readable_owned_bytes--;need(AgxOwnedNirPrepare(&short_request,&result)==AGX_OWNED_NIR_INVALID&&!result,"short actual owned extent refuses before frontend");++refused;
  need(AgxOwnedNirPrepare(&request,&result)==AGX_OWNED_NIR_OK&&result,"actual ownedPROGRAM produces NIR");++accepted;
  const auto map=*AgxOwnedNirBindings(result);auto rootcopy=AgxOwnedNirRoot(result);need(rootcopy.bytes==saved_root.size()&&!std::memcmp(rootcopy.data,saved_root.data(),rootcopy.bytes),"canonical root independent from borrow");
  if(n==0){
   need(map.active&&map.count==5,"actual mixed 5key map");
   for(auto key:std::array<std::array<unsigned,4>,5>{{{{0,3,0,0}},{{1,2,0,0}},{{1,7,0,1}},{{2,1,0,0}},{{2,9,0,1}}}}){auto*b=dxbc_binding_find(&map,key[0],key[1],key[2]);need(b&&b->slot==key[3],"class/space/register deterministic slot");}
   const auto *wg=AgxOwnedNirWorkgroup(result);need(wg[0]==64&&wg[1]==1&&wg[2]==1,"actual workgroup64x1x1");
   auto bad=request;bad.entry="missing";bad.entry_bytes=7;AgxOwnedNir *failed=nullptr;need(AgxOwnedNirPrepare(&bad,&failed)!=AGX_OWNED_NIR_OK&&!failed,"unknown entry atomic rejection");++refused;
   bad=request;bad.limits[DXBC_BINDING_UAV]=1;need(AgxOwnedNirPrepare(&bad,&failed)==AGX_OWNED_NIR_UNSUPPORTED&&!failed,"mixed UAV capacity atomic refusal");++refused;
   need(!std::memcmp(AgxOwnedNirBindings(result),&map,sizeof(map)),"failed subsequent calls cannot mutate live map");
   AgxOwnedNir *again=nullptr;need(AgxOwnedNirPrepare(&request,&again)==AGX_OWNED_NIR_OK&&again,"valid again same thread after converter refusals");++accepted;AgxOwnedNirDestroy(&again);need(!again,"destroy clears result");
  }
  need(AgxCpuRetireRoot(domain,device,root)==AgxCpuProviderStatus::Ok,"root retirement after released caller inputs");
  std::memset(storage.data(),0xdd,storage.size());
  need(!std::memcmp(AgxOwnedNirBindings(result),&map,sizeof(map)),"map independent from destroyed source object storage");
  rootcopy=AgxOwnedNirRoot(result);need(rootcopy.bytes==saved_root.size()&&!std::memcmp(rootcopy.data,saved_root.data(),rootcopy.bytes),"rootcopy independent from retired root/source object");
  auto *original_nir=AgxOwnedNirShader(result);const auto *before_wg=AgxOwnedNirWorkgroup(result);std::array<uint32_t,3> saved_wg{{before_wg[0],before_wg[1],before_wg[2]}};
  agx_owned_nir_pipe_fixture(original_nir,AgxOwnedNirBindings(result),before_wg,stage,n==0?argv[5]:nullptr,actual_wrapper,result);
  wrapper_check(AgxOwnedNirShader(result)==original_nir,"original NIR pointer retained");
  const auto *after_wg=AgxOwnedNirWorkgroup(result);for(unsigned i=0;i<3;i++)wrapper_check(after_wg[i]==saved_wg[i],"actual private workgroup retained");
  const auto *after_map=AgxOwnedNirBindings(result);wrapper_check(after_map->active==map.active&&after_map->count==map.count,"actual private map header retained");
  for(unsigned i=0;i<DXBC_BINDING_MAP_MAX;i++){const auto &a=after_map->bindings[i];const auto &b=map.bindings[i];wrapper_check(a.kind==b.kind&&a.id==b.id&&a.space==b.space&&a.reg==b.reg&&a.slot==b.slot,"actual private map entries retained");}
  const auto after_root=AgxOwnedNirRoot(result);wrapper_check(after_root.bytes==saved_root.size()&&!std::memcmp(after_root.data,saved_root.data(),after_root.bytes),"actual owned root retained through wrapper");
  rootcopy=AgxOwnedNirRoot(result);if(rootcopy.bytes!=saved_root.size()||std::memcmp(rootcopy.data,saved_root.data(),rootcopy.bytes))std::abort();
  AgxOwnedNirDestroy(&result);need(!result,"NIR/map/root destroyed without source pointers");
 }
 agx_owned_nir_pipe_fixture_finish();
 wrapper_check(wrapper_valid_calls==20&&wrapper_null_calls==8,"source-expected five valid and two NULL wrapper calls per four actual objects");
 std::printf("C15_NIR_OPAQUE_WRAPPER_PASS valid_calls=%u null_calls=%u snapshot_checks=%u ACTUAL_CPP_WRAPPER_NO_ASAHI_BO\n",wrapper_valid_calls,wrapper_null_calls,wrapper_snapshot_checks);
 need(AgxCpuRetireDevice(domain,device)==AgxCpuProviderStatus::Ok,"device retirement after all CPU objects");AgxCpuQuiesceTicket q{};need(AgxCpuBeginQuiesce(domain,device,&q)==AgxCpuProviderStatus::Ok,"explicit sink gate close");need(AgxCpuTryQuiesce(domain,&q)==AgxCpuProviderStatus::Ok,"client quiescence");need(AgxCpuProviderDestroy(&domain)==AgxCpuProviderStatus::Ok&&!domain,"actual provider destroyed");
 std::printf("C15_OWNED_NIR_BRIDGE_PASS checks=%u accepted=%u refused=%u legacy_CS_VS_PS=3 DXIL_mixed=1 owned_NIR_map_root=YES NO_DDI_NO_BACKEND_NO_GPU\n",checks,accepted,refused);
}
