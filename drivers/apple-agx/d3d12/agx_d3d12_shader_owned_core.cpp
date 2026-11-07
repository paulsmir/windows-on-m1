/* Original CPU-only R1 shader storage. SPDX-License-Identifier: MIT */
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
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include "agx_d3d12_shader_owned_core.h"
#include "agx_d3d12_zero_cache_library.h"
#include "agx_dxil_program.h"
#include "agx_legacy_program_frame.h"
namespace {
constexpr uint64_t kOwnedMagic=0x4147585348445231ull; /* Existing DXIL format. */
constexpr uint64_t kOwnedLegacyMagic=0x4147585348445232ull;
bool append(SIZE_T &end,SIZE_T alignment,SIZE_T count,SIZE_T element,SIZE_T &offset){
 const auto max=std::numeric_limits<SIZE_T>::max();
 if(end>max-(alignment-1)||count>max/element)return false;
 const SIZE_T at=(end+alignment-1)&~(alignment-1),bytes=count*element;
 if(bytes>max-at)return false;offset=at;end=at+bytes;return true;
}
bool span(AgxShaderTrustedSpan s,const void *pointer,SIZE_T count,SIZE_T element){
 return s.data==pointer&&count<=s.bytes/element&&(!count||s.data);
}
bool overlap(const void *dst,SIZE_T bytes,AgxShaderTrustedSpan src){
 if(!src.bytes)return false;
 const auto max=std::numeric_limits<uintptr_t>::max();
 const auto a=reinterpret_cast<uintptr_t>(dst),b=reinterpret_cast<uintptr_t>(src.data);
 if(bytes>max-a||src.bytes>max-b)return true;
 return a<b+src.bytes&&b<a+bytes;
}
void signatures(unsigned char *base,SIZE_T offset,const D3D12DDIARG_SIGNATURE_ENTRY *in,SIZE_T count){
 auto *out=reinterpret_cast<AgxShaderOwnedSignature *>(base+offset);
 for(SIZE_T n=0;n<count;++n){
  // Storage was zeroed; padding never retains uninitialized WDK bytes.
  ::new(out+n) AgxShaderOwnedSignature{};
  out[n].system_value=static_cast<uint32_t>(in[n].SystemValue);out[n].reg=in[n].Register;
  out[n].component_type=static_cast<uint32_t>(in[n].RegisterComponentType);
  out[n].min_precision=static_cast<uint32_t>(in[n].MinPrecision);out[n].mask=in[n].Mask;
 }
}
}
static AgxShaderStorageStatus Calculate(D3D12DDI_HDEVICE device,
 const D3D12DDIARG_CREATE_SHADER_0010 *args,uint32_t stage,
 const AgxShaderTrustedInput *input,AgxShaderOwnedLayout *output,AgxShaderProgramKind *kind){
 if(!output)return AgxShaderStorageStatus::Invalid;
 std::memset(output,0,sizeof(*output));
 *kind=AgxShaderProgramKind::Unknown;
 if(!args||!input)return AgxShaderStorageStatus::Invalid;
 if(AgxD3d12ZeroCacheValidateLibraryReference(device,&args->LibraryReference)!=S_OK)
  return AgxShaderStorageStatus::LibraryRejected;
 if(stage!=0&&stage!=1&&stage!=5)return AgxShaderStorageStatus::Unsupported;
 if(args->Flags!=D3D12DDI_CREATE_SHADER_FLAG_NONE)return AgxShaderStorageStatus::Unsupported;
 if(input->program.data!=args->pShaderCode||!input->program.data||!input->program.bytes||
    (input->root.bytes&&!input->root.data))return AgxShaderStorageStatus::Invalid;
 if(input->program.bytes<8)return AgxShaderStorageStatus::Invalid;
 uint32_t version=0;std::memcpy(&version,input->program.data,sizeof(version));
 const unsigned model=version&0xffu;
 if(model!=0x40u&&model!=0x41u&&model!=0x50u&&model!=0x51u&&model!=0x60u)
  return AgxShaderStorageStatus::Unsupported;
 const auto *io=args->IOSignatures.Standard;
 const SIZE_T ni=io?io->NumInputSignatureEntries:0,no=io?io->NumOutputSignatureEntries:0;
 const auto *si=io?io->pInputSignatureDeprecated:nullptr;
 const auto *so=io?io->pOutputSignatureDeprecated:nullptr;
 if(!span(input->input_signature,si,ni,sizeof(*si))||!span(input->output_signature,so,no,sizeof(*so)))
  return AgxShaderStorageStatus::Invalid;
 SIZE_T program_bytes=0;AgxShaderProgramKind parsed_kind=AgxShaderProgramKind::Unknown;
 if(model==0x60u){
  agx_dxil_profile profile={uint32_t(1u<<stage),0x60,0x60};agx_dxil_program parsed={};
  auto status=agx_dxil_parse(input->program.data,input->program.bytes,AGX_DXIL_INPUT_PROGRAM,&profile,&parsed);
  if(status==AGX_DXIL_PARSE_INVALID)return AgxShaderStorageStatus::Invalid;
  if(status!=AGX_DXIL_PARSE_OK)return AgxShaderStorageStatus::Unsupported;
  program_bytes=parsed.program_bytes;parsed_kind=AgxShaderProgramKind::Dxil;
 }else{
  agx_legacy_frame_profile profile={uint32_t(1u<<stage),0xfu};agx_legacy_program_frame parsed={};
  auto status=agx_legacy_frame_program(input->program.data,input->program.bytes,&profile,&parsed);
  if(status==AGX_LEGACY_FRAME_INVALID)return AgxShaderStorageStatus::Invalid;
  if(status!=AGX_LEGACY_FRAME_OK)return AgxShaderStorageStatus::Unsupported;
  program_bytes=parsed.program_bytes;parsed_kind=AgxShaderProgramKind::LegacyDxbc;
 }
 AgxShaderOwnedLayout l={};l.total=sizeof(AgxShaderOwned);l.program_bytes=program_bytes;
 l.input_count=ni;l.output_count=no;l.root_bytes=input->root.bytes;
 if(!append(l.total,alignof(uint32_t),l.program_bytes,1,l.program_offset)||
    !append(l.total,alignof(AgxShaderOwnedSignature),ni,sizeof(AgxShaderOwnedSignature),l.input_offset)||
    !append(l.total,alignof(AgxShaderOwnedSignature),no,sizeof(AgxShaderOwnedSignature),l.output_offset)||
    !append(l.total,1,l.root_bytes,1,l.root_offset))return AgxShaderStorageStatus::Overflow;
 *output=l;*kind=parsed_kind;return AgxShaderStorageStatus::Ok;
}
AgxShaderStorageStatus AgxShaderOwnedCalculate(D3D12DDI_HDEVICE device,
 const D3D12DDIARG_CREATE_SHADER_0010 *args,uint32_t stage,
 const AgxShaderTrustedInput *input,AgxShaderOwnedLayout *output){
 AgxShaderProgramKind kind=AgxShaderProgramKind::Unknown;
 return Calculate(device,args,stage,input,output,&kind);
}
AgxShaderStorageStatus AgxShaderOwnedCreate(D3D12DDI_HDEVICE device,
 const D3D12DDIARG_CREATE_SHADER_0010 *args,uint32_t stage,
 const AgxShaderTrustedInput *input,void *storage,SIZE_T capacity,AgxShaderOwned **output){
 if(!output)return AgxShaderStorageStatus::Invalid;
 *output=nullptr;AgxShaderOwnedLayout layout={};
 AgxShaderProgramKind kind=AgxShaderProgramKind::Unknown;
 const auto status=Calculate(device,args,stage,input,&layout,&kind);
 if(status!=AgxShaderStorageStatus::Ok)return status;
 if(!storage||reinterpret_cast<uintptr_t>(storage)%alignof(AgxShaderOwned)||capacity<layout.total)
  return AgxShaderStorageStatus::StorageInvalid;
 // Descriptors/inputs must also survive storage initialization; rejecting alias
 // is necessary even if no owned byte range overlaps an incoming byte array.
 if(overlap(storage,layout.total,{args,sizeof(*args)})||overlap(storage,layout.total,{input,sizeof(*input)})||
    (args->IOSignatures.Standard&&overlap(storage,layout.total,{args->IOSignatures.Standard,sizeof(*args->IOSignatures.Standard)}))||
    overlap(storage,layout.total,input->program)||overlap(storage,layout.total,input->input_signature)||
    overlap(storage,layout.total,input->output_signature)||overlap(storage,layout.total,input->root))
  return AgxShaderStorageStatus::StorageInvalid;
 std::memset(storage,0,layout.total);
 auto *owned=::new(storage) AgxShaderOwned{};owned->stage=stage;owned->flags=static_cast<uint32_t>(args->Flags);owned->layout=layout;
 auto *base=static_cast<unsigned char *>(storage);std::memcpy(base+layout.program_offset,input->program.data,layout.program_bytes);
 const auto *io=args->IOSignatures.Standard;
 signatures(base,layout.input_offset,io?io->pInputSignatureDeprecated:nullptr,layout.input_count);
 signatures(base,layout.output_offset,io?io->pOutputSignatureDeprecated:nullptr,layout.output_count);
 if(layout.root_bytes)std::memcpy(base+layout.root_offset,input->root.data,layout.root_bytes);
 owned->magic=kind==AgxShaderProgramKind::Dxil?kOwnedMagic:kOwnedLegacyMagic;*output=owned;return AgxShaderStorageStatus::Ok;
}
static_assert(std::is_trivially_destructible<AgxShaderOwned>::value,"CPU private storage has no external cleanup");

AgxShaderProgramKind AgxShaderOwnedGetProgramKind(const AgxShaderOwned *owned){
 if(!owned)return AgxShaderProgramKind::Unknown;
 if(owned->magic==kOwnedMagic)return AgxShaderProgramKind::Dxil;
 if(owned->magic==kOwnedLegacyMagic)return AgxShaderProgramKind::LegacyDxbc;
 return AgxShaderProgramKind::Unknown;
}
