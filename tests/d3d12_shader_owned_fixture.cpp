/* Original actual-module tests; host shim is NOT a Windows ABI model. */
#include <windows.h>
#include <d3d12umddi.h>
#include "agx_d3d12_shader_owned_core.h"
#include "agx_d3d12_zero_cache_library.h"
#include <array>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
static int a,b;static bool alive=true;static unsigned checks;static bool nested=false;
extern "C" const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE h){nested|=agx_host_srw_depth!=0;return h.pDrvPrivate==&a&&alive?&a:h.pDrvPrivate==&b?&b:nullptr;}
extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE){std::abort();}
static void require(bool v,const char *why){++checks;if(!v){std::fprintf(stderr,"FAIL %s\n",why);std::abort();}}
int main(int argc,char **argv){
 require(argc==2,"PROGRAM fixture argument");std::ifstream f(argv[1],std::ios::binary);
 std::vector<unsigned char> original((std::istreambuf_iterator<char>(f)),{});require(original.size()==1680,"actual1397 PROGRAM size");
 D3D12DDI_HDEVICE device{&a};D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 empty{};
 alignas(64) unsigned char lib[128];const auto libsize=AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(device,&empty);
 require(libsize&&libsize<=sizeof(lib),"actual library storage fits");D3D12DDI_HPIPELINELIBRARY library{lib};
 require(AgxD3d12ZeroCacheCreatePipelineLibrary(device,&empty,library)==S_OK,"actual library create");
 using S=AgxShaderStorageStatus;
 for(unsigned stage:{0u,1u,5u})for(unsigned poison:{0u,0xa5u,0xffu})for(bool useLibrary:{false,true}){
  auto code=original;uint32_t version=(stage<<16)|0x60u;std::memcpy(code.data(),&version,4);
  D3D12DDIARG_SIGNATURE_ENTRY in{},out{};in.Register=17;in.Mask=15;out.Register=3;out.Mask=7;
  D3D12DDIARG_STAGE_IO_SIGNATURES io{};io.pInputSignatureDeprecated=&in;io.NumInputSignatureEntries=1;io.pOutputSignatureDeprecated=&out;io.NumOutputSignatureEntries=1;
  std::array<unsigned char,37> root{};for(unsigned i=0;i<root.size();++i)root[i]=i^0x6d;auto rootcopy=root;
  D3D12DDIARG_CREATE_SHADER_0010 args{};args.pShaderCode=reinterpret_cast<const UINT *>(code.data());args.IOSignatures.Standard=&io;
  args.LibraryReference.hLibrary=useLibrary?library:D3D12DDI_HPIPELINELIBRARY{};args.LibraryReference.PipelineIndex=~0u;
  AgxShaderTrustedInput input{{code.data(),code.size()},{&in,sizeof(in)},{&out,sizeof(out)},{root.data(),root.size()}};
  AgxShaderOwnedLayout layout{};require(AgxShaderOwnedCalculate(device,&args,stage,&input,&layout)==S::Ok,"actual Calculate");
  alignas(64) std::array<unsigned char,8192> storage;storage.fill(poison);AgxShaderOwned *owned=reinterpret_cast<AgxShaderOwned *>(1);
  require(layout.total+128<storage.size(),"exact private storage fits");
  require(AgxShaderOwnedCreate(device,&args,stage,&input,storage.data()+64,layout.total-1,&owned)==S::StorageInvalid&&!owned,"short storage no publication");
  require(AgxShaderOwnedCreate(device,&args,stage,&input,storage.data()+65,layout.total,&owned)==S::StorageInvalid&&!owned,"unaligned storage no publication");
  require(AgxShaderOwnedCreate(device,&args,stage,&input,storage.data()+64,layout.total,&owned)==S::Ok,"actual Create");
  auto *base=reinterpret_cast<unsigned char *>(owned);require(owned->stage==stage&&!owned->flags&&owned->magic,"owned header");
  require(!std::memcmp(&layout,&owned->layout,sizeof(layout)),"Calc/Create layout agreement");
  require(!std::memcmp(base+layout.program_offset,code.data(),code.size()),"exact PROGRAM bytes copied");
  require(!std::memcmp(base+layout.root_offset,root.data(),root.size()),"canonical root bytes copied");
  const auto *si=reinterpret_cast<AgxShaderOwnedSignature *>(base+layout.input_offset);
  const auto *so=reinterpret_cast<AgxShaderOwnedSignature *>(base+layout.output_offset);
  require(si->reg==17&&si->mask==15&&so->reg==3&&so->mask==7,"signature semantic fields copied");
  for(unsigned i=0;i<64;++i)require(storage[i]==poison,"left canary");
  for(SIZE_T i=64+layout.total;i<storage.size();++i)require(storage[i]==poison,"right canary");
  std::vector<unsigned char> snapshot(base,base+layout.total);std::memset(code.data(),0xcc,code.size());std::memset(&in,0xcc,sizeof(in));std::memset(&out,0xcc,sizeof(out));root.fill(0xcc);std::memset(&io,0xcc,sizeof(io));std::memset(&args,0xcc,sizeof(args));
  require(!std::memcmp(base,snapshot.data(),snapshot.size()),"owned snapshot survives all incoming overwrites");
 }
 // Original negative cases exercise actual module/validator, never a substitute.
 auto code=original;D3D12DDIARG_CREATE_SHADER_0010 args{};args.pShaderCode=reinterpret_cast<const UINT *>(code.data());
 AgxShaderTrustedInput input{{code.data(),code.size()},{nullptr,0},{nullptr,0},{nullptr,0}};AgxShaderOwnedLayout l{};
 auto calc=[&](){return AgxShaderOwnedCalculate(device,&args,5,&input,&l);};
 require(calc()==S::Ok,"null optional signature / empty canonical root");
 args.LibraryReference.hLibrary={reinterpret_cast<void *>(1)};require(calc()==S::LibraryRejected,"unknown library unread");
 args.LibraryReference.hLibrary=library;require(AgxShaderOwnedCalculate({&b},&args,5,&input,&l)==S::LibraryRejected,"foreign library");
 alignas(64) std::array<unsigned char,8192> retained{};AgxShaderOwned *saved=nullptr;
 require(AgxShaderOwnedCreate(device,&args,5,&input,retained.data(),retained.size(),&saved)==S::Ok,"library ordinary owned construction");
 std::vector<unsigned char> savedbytes(retained.data(),retained.data()+saved->layout.total);
 AgxD3d12ZeroCacheDestroyPipelineLibrary(device,library);require(calc()==S::LibraryRejected,"destroyed library");
 require(!std::memcmp(retained.data(),savedbytes.data(),savedbytes.size()),"owned shader survives actual library destruction");
 args.LibraryReference.hLibrary={};alive=false;require(calc()==S::LibraryRejected,"dead device ordinary reference");alive=true;
 args.Flags=static_cast<D3D12DDI_CREATE_SHADER_FLAGS>(1);require(calc()==S::Unsupported,"unimplemented tracing internal status");args.Flags=D3D12DDI_CREATE_SHADER_FLAG_NONE;
 require(AgxShaderOwnedCalculate(device,&args,2,&input,&l)==S::Unsupported,"deferred stage internal status");
 input.program.bytes=7;require(calc()==S::Invalid,"explicit short PROGRAM span");input.program.bytes=code.size();
 uint32_t bad=0x00050061;std::memcpy(code.data(),&bad,4);require(calc()==S::Unsupported,"unsupported model internal status");code=original;args.pShaderCode=reinterpret_cast<const UINT *>(code.data());input.program.data=code.data();
 input.root={code.data(),std::numeric_limits<SIZE_T>::max()};require(calc()==S::Overflow,"checked total extent overflow without root read");input.root={nullptr,0};
 D3D12DDIARG_STAGE_IO_SIGNATURES io{};io.NumInputSignatureEntries=~0u;args.IOSignatures.Standard=&io;require(calc()==S::Invalid,"huge signature count inaccessible span rejected before traversal");args.IOSignatures.Standard=nullptr;
 input.program.data=nullptr;require(calc()==S::Invalid,"trusted span must match actual code");
 alignas(64) std::array<unsigned char,8192> alias{};std::memcpy(alias.data()+64,original.data(),original.size());auto before_alias=alias;
 args={};args.pShaderCode=reinterpret_cast<const UINT *>(alias.data()+64);input={{alias.data()+64,original.size()},{nullptr,0},{nullptr,0},{nullptr,0}};
 saved=reinterpret_cast<AgxShaderOwned *>(1);
 require(AgxShaderOwnedCreate(device,&args,5,&input,alias.data(),alias.size(),&saved)==S::StorageInvalid&&!saved,"overlapping PROGRAM/destination rejected");
 require(alias==before_alias,"alias rejection makes zero writes");
 std::memset(&l,0xa5,sizeof(l));input.program.bytes=7;
 require(AgxShaderOwnedCalculate(device,&args,5,&input,&l)==S::Invalid,"failure clears layout");
 AgxShaderOwnedLayout zero{};require(!std::memcmp(&l,&zero,sizeof(l)),"no partial output layout");
 require(!nested,"actual library resolver outside SRW");
 std::printf("SHADER_OWNED_CORE_PASS checks=%u actual_modules=YES native_ABI=NO framing_only=YES no_registration=YES\n",checks);
}
