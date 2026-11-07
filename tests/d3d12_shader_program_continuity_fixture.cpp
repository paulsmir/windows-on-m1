/* Original actual-module storage oracle; provider is finite test input only. */
#include <windows.h>
#include <d3d12umddi.h>
#include "agx_d3d12_shader_owned_core.h"
#include <array>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdio>
#include <cstdlib>
static int device_id;static unsigned checks,accepted,refused;static bool resolver_nested;
#include "agx_d3d12_zero_cache_library.h"
extern "C" const void *AgxD3d12ZeroCacheResolveDevice(D3D12DDI_HDEVICE h){resolver_nested|=agx_host_srw_depth!=0;return h.pDrvPrivate==&device_id?&device_id:nullptr;}
extern "C" const void *AgxD3d12ZeroCacheResolvePsoOwner(D3D12DDI_HPIPELINESTATE){std::abort();}
static void need(bool v,const char *why){++checks;if(!v){std::fprintf(stderr,"FAIL %s\n",why);std::abort();}}
static void bounds(const AgxShaderOwnedLayout &l){
 need(l.program_offset>=sizeof(AgxShaderOwned)&&l.program_offset<=l.total&&l.program_bytes<=l.total-l.program_offset,"owned program offset bounds");
 need(l.input_offset>=l.program_offset+l.program_bytes&&l.input_offset<=l.total&&l.input_count<=(l.total-l.input_offset)/sizeof(AgxShaderOwnedSignature),"input signature bounds");
 need(l.output_offset>=l.input_offset+l.input_count*sizeof(AgxShaderOwnedSignature)&&l.output_offset<=l.total&&l.output_count<=(l.total-l.output_offset)/sizeof(AgxShaderOwnedSignature),"output signature bounds");
 need(l.root_offset>=l.output_offset+l.output_count*sizeof(AgxShaderOwnedSignature)&&l.root_offset<=l.total&&l.root_bytes==l.total-l.root_offset,"root tail offset bounds");
}
int main(int argc,char **argv){
 need(argc==14,"one original DXIL plus12 real FXC PROGRAMs");
 static_assert(sizeof(AgxShaderOwned)==88,"retain DXIL layout and114467 baseline oracle");
 const unsigned stages[13]={5,1,0,2,0,5,5,5,3,4,0,0,5};
 D3D12DDI_HDEVICE device{&device_id};D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 empty{};alignas(64) unsigned char library_storage[128];
 need(AgxD3d12ZeroCacheCalcPrivatePipelineLibrarySize(device,&empty)<=sizeof(library_storage),"actual library private extent");
 D3D12DDI_HPIPELINELIBRARY library{library_storage};need(AgxD3d12ZeroCacheCreatePipelineLibrary(device,&empty,library)==S_OK,"actual library create");
 using S=AgxShaderStorageStatus;using K=AgxShaderProgramKind;
 need(AgxShaderOwnedGetProgramKind(nullptr)==K::Unknown,"null kind is unknown");AgxShaderOwned unknown{};need(AgxShaderOwnedGetProgramKind(&unknown)==K::Unknown,"unknown live record discriminator");
 unsigned legacy_files=0,deferred_files=0,dxil_files=0;
 for(int arg=1;arg<argc;++arg){std::ifstream f(argv[arg],std::ios::binary);std::vector<unsigned char> original((std::istreambuf_iterator<char>(f)),{});need(original.size()>=8,"real source PROGRAM");
  const unsigned stage=stages[arg-1];const bool supported=stage==0||stage==1||stage==5;
  const K kind=arg==1?K::Dxil:K::LegacyDxbc;
  if(arg==1)++dxil_files;else if(supported)++legacy_files;else ++deferred_files;
  for(unsigned poison:{0u,0xa5u,0xffu})for(bool use_library:{false,true}){
   auto code=original;code.insert(code.end(),16,0xeau);D3D12DDIARG_SIGNATURE_ENTRY in{},out{};in.Register=13;in.Mask=15;out.Register=7;out.Mask=3;
   D3D12DDIARG_STAGE_IO_SIGNATURES io{};io.pInputSignatureDeprecated=&in;io.NumInputSignatureEntries=1;io.pOutputSignatureDeprecated=&out;io.NumOutputSignatureEntries=1;
   std::array<unsigned char,31> root{};for(unsigned i=0;i<root.size();++i)root[i]=static_cast<unsigned char>(0x51u+i);
   D3D12DDIARG_CREATE_SHADER_0010 args{};args.pShaderCode=reinterpret_cast<const UINT *>(code.data());args.IOSignatures.Standard=&io;args.LibraryReference.hLibrary=use_library?library:D3D12DDI_HPIPELINELIBRARY{};args.LibraryReference.PipelineIndex=~0u;
   AgxShaderTrustedInput input{{code.data(),code.size()},{&in,sizeof(in)},{&out,sizeof(out)},{root.data(),root.size()}};AgxShaderOwnedLayout l{};
   alignas(64) std::array<unsigned char,8192> storage;storage.fill(static_cast<unsigned char>(poison));auto pristine=storage;AgxShaderOwned *owned=reinterpret_cast<AgxShaderOwned *>(1);
   const auto status=AgxShaderOwnedCalculate(device,&args,stage,&input,&l);
   if(!supported){need(status==S::Unsupported,"deferred stage internal domain refusal");need(AgxShaderOwnedCreate(device,&args,stage,&input,storage.data()+64,storage.size()-128,&owned)==S::Unsupported&&!owned,"deferred Create no publication");need(storage==pristine,"deferred no storage writes");++refused;continue;}
   need(status==S::Ok,"actual selected stage Calc");bounds(l);need(l.program_bytes==original.size(),"caller tail excluded exact PROGRAM extent");need(l.total+128<storage.size(),"finite storage capacity");
   need(AgxShaderOwnedCreate(device,&args,stage,&input,storage.data()+64,l.total-1,&owned)==S::StorageInvalid&&!owned,"short Create no publish");need(storage==pristine,"short Create no writes");
   need(AgxShaderOwnedCreate(device,&args,stage,&input,storage.data()+65,l.total,&owned)==S::StorageInvalid&&!owned,"unaligned Create no publish");need(storage==pristine,"unaligned Create no writes");
   need(AgxShaderOwnedCreate(device,&args,stage,&input,storage.data()+64,l.total,&owned)==S::Ok,"actual tagged Create");
   need(AgxShaderOwnedGetProgramKind(owned)==kind,"correct explicit stored program kind");need(!std::memcmp(&l,&owned->layout,sizeof(l))&&owned->stage==stage&&!owned->flags,"Calc/Create canonical tuple");
   auto *base=reinterpret_cast<unsigned char *>(owned);need(!std::memcmp(base+l.program_offset,original.data(),original.size()),"exact source PROGRAM bytes");need(!std::memcmp(base+l.root_offset,root.data(),root.size()),"root canonical bytes copied");
   const auto *si=reinterpret_cast<const AgxShaderOwnedSignature *>(base+l.input_offset);const auto *so=reinterpret_cast<const AgxShaderOwnedSignature *>(base+l.output_offset);need(si->reg==13&&si->mask==15&&so->reg==7&&so->mask==3,"old0010 signature fields owned");
   for(unsigned i=0;i<64;++i)need(storage[i]==poison,"left canary");for(SIZE_T i=64+l.total;i<storage.size();++i)need(storage[i]==poison,"right canary");
   std::vector<unsigned char> snapshot(base,base+l.total);std::memset(code.data(),0xcc,code.size());std::memset(&in,0xcc,sizeof(in));std::memset(&out,0xcc,sizeof(out));root.fill(0xcc);std::memset(&io,0xcc,sizeof(io));std::memset(&args,0xcc,sizeof(args));
   need(!std::memcmp(base,snapshot.data(),snapshot.size())&&AgxShaderOwnedGetProgramKind(owned)==kind,"tag/program/signatures/root survive all input overwrite");++accepted;
  }
 }
 need(legacy_files==9&&deferred_files==3&&dxil_files==1,"actual9accepted3deferred legacy file domain plusDXILcontrol");
 need(accepted==60&&refused==18,"finitepoison/library matrix");
 // Keep a real legacy owned object live across actual library destruction.
 std::ifstream keepfile(argv[2],std::ios::binary);std::vector<unsigned char> keepcode((std::istreambuf_iterator<char>(keepfile)),{});
 D3D12DDIARG_CREATE_SHADER_0010 keep_args{};keep_args.pShaderCode=reinterpret_cast<const UINT *>(keepcode.data());keep_args.LibraryReference.hLibrary=library;
 AgxShaderTrustedInput keep_input{{keepcode.data(),keepcode.size()},{nullptr,0},{nullptr,0},{nullptr,0}};
 alignas(64) std::array<unsigned char,8192> kept_storage{};AgxShaderOwned *kept=nullptr;
 need(AgxShaderOwnedCreate(device,&keep_args,1,&keep_input,kept_storage.data(),kept_storage.size(),&kept)==S::Ok,"legacy owned library control");
 std::vector<unsigned char> kept_snapshot(kept_storage.data(),kept_storage.data()+kept->layout.total);
 AgxD3d12ZeroCacheDestroyPipelineLibrary(device,library);
 need(!std::memcmp(kept_storage.data(),kept_snapshot.data(),kept_snapshot.size())&&AgxShaderOwnedGetProgramKind(kept)==K::LegacyDxbc,"legacy owned survives actual library destruction");
 keep_args.LibraryReference.hLibrary={};uint32_t badwords=0;std::memcpy(keepcode.data()+4,&badwords,4);
 AgxShaderOwnedLayout failed;std::memset(&failed,0xa5,sizeof(failed));
 need(AgxShaderOwnedCalculate(device,&keep_args,1,&keep_input,&failed)==S::Invalid,"legacy malformed wordcount mapped INVALID");
 AgxShaderOwnedLayout empty_layout{};need(!std::memcmp(&failed,&empty_layout,sizeof(failed)),"legacy frame failure clears layout");
 kept=reinterpret_cast<AgxShaderOwned *>(1);
 need(AgxShaderOwnedCreate(device,&keep_args,1,&keep_input,kept_storage.data(),kept_storage.size(),&kept)==S::Invalid&&!kept,"legacy malformed frame no publication");
 need(!std::memcmp(kept_storage.data(),kept_snapshot.data(),kept_snapshot.size()),"legacy failed create zero writes");
 need(!resolver_nested,"actual zeroCache resolver outside lock");
 std::printf("OWNED_PROGRAM_CONTINUITY_PASS checks=%u legacy_FXC_files=9 deferred_GS_HS_DS=3 DXIL_control=1 storage_accepts=%u internal_refusals=%u actual_modules=YES IR_validation=NO\n",checks,accepted,refused);
}
