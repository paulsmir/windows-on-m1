/* Original CPU-only state/lifetime oracle, no fault injection. SPDX-License-Identifier: MIT */
#include <windows.h>
#include <d3d12umddi.h>
#include "agx_shader_input_provider.h"
#include "agx_d3d12_zero_cache_library.h"
#include <array>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdio>
#include <cstdlib>
static AgxCpuProvider *provider;static unsigned checks;
static void need(bool x,const char*why){++checks;if(!x){std::fprintf(stderr,"FAIL %s\n",why);std::abort();}}
extern "C" const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE d){need(agx_host_srw_depth==0,"resolver outside library lock");return AgxCpuResolveDeviceIdentity(provider,d);}
extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE){std::abort();}
struct Sink {unsigned calls=0;AgxCpuDevice device{};AgxCpuProviderResult last{};AgxShaderOwned **publication=nullptr;};
static void failure(void*ctx,AgxCpuDevice device,AgxCpuProviderResult r)noexcept{
 auto*s=static_cast<Sink*>(ctx);need(agx_host_srw_depth==0,"sink outside library lock");
 need(!s->publication||!*s->publication,"failure observes no published output");++s->calls;s->device=device;s->last=r;
}
using P=AgxCpuProviderStatus;using S=AgxShaderStorageStatus;
static std::vector<unsigned char> read(const char*p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 need(argc==3,"actual DXIL/legacy PROGRAM inputs");auto dxil=read(argv[1]),legacy=read(argv[2]);need(dxil.size()==1680&&legacy.size()>=8,"actual fixture sizes");
 provider=AgxCpuProviderCreate();need(provider!=nullptr,"provider");int da=0,db=0,ra=0,rb=0;Sink sa,sb;AgxCpuDevice a{},b{};
 need(AgxCpuRegisterDevice(provider,{&da},failure,&sa,&a)==P::Ok,"deviceA");need(AgxCpuRegisterDevice(provider,{&db},failure,&sb,&b)==P::Ok,"deviceB");
 AgxCpuDevice duplicate{99};need(AgxCpuRegisterDevice(provider,{&da},failure,&sa,&duplicate)==P::Invalid&&!duplicate.value,"duplicate refused empty");
 std::array<unsigned char,16> canon{};for(unsigned n=0;n<canon.size();n++)canon[n]=n^0x6d;auto originalRoot=canon;
 AgxCpuRoot ar{},br{};need(AgxCpuRegisterRoot(provider,a,{&ra},{canon.data(),canon.size()},&ar)==P::Ok,"opaque root A");
 need(AgxCpuRegisterRoot(provider,b,{&rb},{nullptr,0},&br)==P::Ok,"empty opaque root B");canon.fill(0xcc);
 for(auto input:{dxil,legacy}){
  auto pristine=input;D3D12DDIARG_SIGNATURE_ENTRY signature{};signature.Register=7;signature.Mask=15;
  D3D12DDIARG_STAGE_IO_SIGNATURES io{};io.NumInputSignatureEntries=1;io.pInputSignatureDeprecated=&signature;
  D3D12DDIARG_CREATE_SHADER_0010 args{};args.hRootSignature={&ra};args.pShaderCode=reinterpret_cast<const UINT*>(input.data());args.IOSignatures.Standard=&io;
  AgxCpuInputLease lease{};need(AgxCpuAcquireInput(provider,a,ar,&args,5,{input.data(),input.size()},{&signature,sizeof(signature)},{nullptr,0},&lease).status==P::Ok,"typed explicit lease");
  /* Args/count descriptor are snapshots. Actual PROGRAM/signature arrays remain
   * untouched until release, as required by the lease's caller contract. */
  args.Flags=static_cast<D3D12DDI_CREATE_SHADER_FLAGS>(123);io.NumInputSignatureEntries=999;
  AgxShaderTrustedSpan root{};need(AgxCpuLeaseRootBytes(provider,lease,&root)==P::Ok&&root.bytes==originalRoot.size()&&!std::memcmp(root.data,originalRoot.data(),root.bytes),"root owns source bytes");
  AgxShaderOwnedLayout layout{};auto c=AgxCpuCalculate(provider,lease,&layout);need(c.status==P::Ok&&c.core_called&&c.core_status==S::Ok,"actual core Calculate");
  alignas(64)std::array<unsigned char,8192> storage;storage.fill(0xa5);need(layout.total+128<storage.size(),"storage extent");AgxShaderOwned*owned=nullptr;sa.publication=&owned;unsigned before=sa.calls;
  c=AgxCpuCopyOwned(provider,lease,storage.data()+64,layout.total-1,&owned);need(c.status==P::CoreFailure&&c.core_called&&c.core_status==S::StorageInvalid&&!owned,"exact core storage refusal");
  need(sa.calls==before+1&&sa.device.value==a.value&&!sb.calls,"per-device CPU sink only");
  c=AgxCpuCopyOwned(provider,lease,storage.data()+64,layout.total,&owned);need(c.status==P::Ok&&owned&&sa.calls==before+1,"publish only success");
  need(AgxShaderOwnedGetProgramKind(owned)==(input.size()==dxil.size()?AgxShaderProgramKind::Dxil:AgxShaderProgramKind::LegacyDxbc),"actual tagged core");
  auto*bytes=reinterpret_cast<unsigned char*>(owned);need(!std::memcmp(bytes+layout.program_offset,pristine.data(),layout.program_bytes),"owned actual PROGRAM");
  need(!std::memcmp(bytes+layout.root_offset,originalRoot.data(),originalRoot.size()),"owned canonical opaque bytes");
  auto*si=reinterpret_cast<AgxShaderOwnedSignature*>(bytes+layout.input_offset);need(si->reg==7&&si->mask==15&&layout.input_count==1,"snapshot count+actual signature");
  std::vector<unsigned char> snapshot(bytes,bytes+layout.total);need(AgxCpuReleaseInput(provider,lease)==P::Ok,"release input lease");
  input.assign(input.size(),0xcc);std::memset(&signature,0xcc,sizeof(signature));need(!std::memcmp(bytes,snapshot.data(),snapshot.size()),"owned survives after release/mutation");
  need(AgxCpuReleaseInput(provider,lease)==P::Invalid,"stale lease ID refused without pointer read");sa.publication=nullptr;
 }
 auto input=dxil;D3D12DDIARG_CREATE_SHADER_0010 args{};args.hRootSignature={&ra};args.pShaderCode=reinterpret_cast<const UINT*>(input.data());
 auto acquire=[&](AgxCpuDevice d,AgxCpuRoot r,AgxCpuInputLease*out){return AgxCpuAcquireInput(provider,d,r,&args,5,{input.data(),input.size()},{nullptr,0},{nullptr,0},out);};
 AgxCpuInputLease lease{},rejected{77};need(acquire(a,ar,&lease).status==P::Ok,"retirement lease");
 need(AgxCpuProviderDestroy(&provider)==P::Busy&&provider,"busy with valid lease");
 need(acquire(b,ar,&rejected).status==P::WrongDevice&&!rejected.value,"cross-device root refused");need(sb.calls==1&&sb.device.value==b.value,"foreign-root sink B");
 need(AgxCpuRetireRoot(provider,b,ar)==P::WrongDevice,"foreign retirement refused");need(AgxCpuRetireRoot(provider,a,ar)==P::Ok,"retire root");
 need(acquire(a,ar,&rejected).status==P::NotLiveRoot&&!rejected.value,"new acquisition of stale root refused");
 AgxShaderTrustedSpan root{};need(AgxCpuLeaseRootBytes(provider,lease,&root)==P::Ok&&!std::memcmp(root.data,originalRoot.data(),root.bytes),"existing root lease survives retirement");
 AgxShaderOwnedLayout layout{};need(AgxCpuCalculate(provider,lease,&layout).status==P::Ok,"existing root lease valid while device active");
 AgxCpuRoot ar2{};need(AgxCpuRegisterRoot(provider,a,{&ra},{canon.data(),canon.size()},&ar2)==P::Ok&&ar2.value!=ar.value,"new root generation same identity value");
 need(acquire(a,ar,&rejected).status==P::NotLiveRoot&&!rejected.value,"old root ID cannot alias new root");
 need(AgxCpuRetireDevice(provider,a)==P::Ok,"retire device with lease");need(!AgxCpuResolveDeviceIdentity(provider,{&da}),"retired device membership absent");
 need(acquire(a,ar2,&rejected).status==P::NotLiveDevice&&!rejected.value,"retired device refuses before input use");
 need(AgxCpuLeaseRootBytes(provider,lease,&root)==P::Ok&&!std::memcmp(root.data,originalRoot.data(),root.bytes),"root bytes pinned after device retirement");
 auto c=AgxCpuCalculate(provider,lease,&layout);need(c.status==P::RetiredDevice&&!c.core_called&&!layout.total,"retired lease cannot enter core");
 AgxShaderOwned*out=nullptr;sa.publication=&out;alignas(64)unsigned char storage[4096];c=AgxCpuCopyOwned(provider,lease,storage,sizeof(storage),&out);need(c.status==P::RetiredDevice&&!c.core_called&&!out,"retired no publication");sa.publication=nullptr;
 need(AgxCpuReleaseInput(provider,lease)==P::Ok,"last lease releases retired nodes");need(AgxCpuLeaseRootBytes(provider,lease,&root)==P::Invalid&&!root.data&&!root.bytes,"stale lease no dangling access");
 AgxCpuDevice a2{};need(AgxCpuRegisterDevice(provider,{&da},failure,&sa,&a2)==P::Ok&&a2.value!=a.value,"device ID no reuse");need(acquire(a,ar2,&rejected).status==P::NotLiveDevice,"old device ID cannot alias new device");
 need(AgxCpuProviderDestroy(&provider)==P::Ok&&!provider,"teardown remaining roots/devices");
 std::printf("PASS provider caller-serialized checks=%u exact-core legacy+DXIL/ownedopaque-root/leases/generations/sinks/publishlast noDDIpolicy\n",checks);
}
