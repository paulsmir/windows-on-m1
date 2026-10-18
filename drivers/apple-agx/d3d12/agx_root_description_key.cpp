/* Original UNLINKED exact-field0001 key. SPDX-License-Identifier: MIT */
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
#include "agx_root_description_key.h"
#include <cstring>
#include <limits>
namespace {
static_assert(sizeof(FLOAT)==4&&sizeof(uint32_t)==4,"exact32-bit sampler scalar words");
bool extent(AgxRootKeySpan s){
 if(!s.data)return !s.bytes;
 return s.bytes<=std::numeric_limits<uintptr_t>::max()-reinterpret_cast<uintptr_t>(s.data);
}
template<class T>bool covers(AgxRootKeySpan s,SIZE_T count,const T*pointer){
 return extent(s)&&s.data==pointer&&count<=s.bytes/sizeof(T)&&
  (!count||(pointer&&reinterpret_cast<uintptr_t>(pointer)%alignof(T)==0));
}
bool add(SIZE_T&total,SIZE_T count,SIZE_T unit){
 const auto max=std::numeric_limits<SIZE_T>::max();
 if(count>max/unit||count*unit>max-total)return false;total+=count*unit;return true;
}
template<class T>uint32_t raw32(const T&value){static_assert(sizeof(T)==4,"raw scalar word");uint32_t word;std::memcpy(&word,&value,4);return word;}
struct Shape {const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001 *args;const D3D12DDI_ROOT_SIGNATURE *desc;
 const AgxRootKeySpan *tables;SIZE_T bytes;};
AgxRootKeyStatus preflight(const AgxRootKeyInput*in,Shape*out){
 if(!in)return AgxRootKeyStatus::InvalidSpan;
 const auto*args=static_cast<const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001*>(in->args.data);
 if(!covers(in->args,1,args))return AgxRootKeyStatus::InvalidSpan;
 const auto*desc=static_cast<const D3D12DDI_ROOT_SIGNATURE*>(in->description.data);
 if(!covers(in->description,1,args->pRootSignature)||desc!=args->pRootSignature)return AgxRootKeyStatus::InvalidSpan;
 const auto*tables=static_cast<const AgxRootKeySpan*>(in->table_spans.data);
 if(!covers(in->parameters,desc->NumParameters,desc->pRootParameters)||
    !covers(in->samplers,desc->NumStaticSamplers,desc->pStaticSamplers)||
    !covers(in->table_spans,desc->NumParameters,tables))return AgxRootKeyStatus::InvalidSpan;
 SIZE_T size=32;
 if(!add(size,desc->NumParameters,8)||!add(size,desc->NumStaticSamplers,52))return AgxRootKeyStatus::SizeOverflow;
 for(UINT n=0;n<desc->NumParameters;++n){
  const auto&p=desc->pRootParameters[n];
  switch(raw32(p.ParameterType)){
  case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:{
   const auto&t=p.DescriptorTable;
   if(!covers(tables[n],t.NumDescriptorRanges,t.pDescriptorRanges))return AgxRootKeyStatus::InvalidSpan;
   if(!add(size,1,4)||!add(size,t.NumDescriptorRanges,20))return AgxRootKeyStatus::SizeOverflow;break;
  }
  case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:if(!add(size,1,12))return AgxRootKeyStatus::SizeOverflow;break;
  case D3D12DDI_ROOT_PARAMETER_TYPE_CBV:case D3D12DDI_ROOT_PARAMETER_TYPE_SRV:case D3D12DDI_ROOT_PARAMETER_TYPE_UAV:
   if(!add(size,1,8))return AgxRootKeyStatus::SizeOverflow;break;
  default:return AgxRootKeyStatus::UnsupportedUnion;
  }
 }
 *out={args,desc,tables,size};return AgxRootKeyStatus::Ok;
}
bool overlaps(AgxRootKeySpan a,AgxRootKeySpan b){
 if(!a.bytes||!b.bytes)return false;
 const auto x=reinterpret_cast<uintptr_t>(a.data),y=reinterpret_cast<uintptr_t>(b.data);
 return x<y+b.bytes&&y<x+a.bytes;
}
struct Writer {unsigned char*p;void word(uint32_t n){p[0]=static_cast<unsigned char>(n);p[1]=static_cast<unsigned char>(n>>8);p[2]=static_cast<unsigned char>(n>>16);p[3]=static_cast<unsigned char>(n>>24);p+=4;}};
}
AgxRootKeyStatus AgxRootKeyCalculate(const AgxRootKeyInput*in,SIZE_T*out){
 if(!out)return AgxRootKeyStatus::InvalidSpan;*out=0;Shape s{};auto status=preflight(in,&s);
 if(status==AgxRootKeyStatus::Ok)*out=s.bytes;return status;
}
AgxRootKeyStatus AgxRootKeyEncode(const AgxRootKeyInput*in,void*dst,SIZE_T capacity,AgxRootKeyView*out){
 if(!out)return AgxRootKeyStatus::InvalidSpan;*out={};Shape s{};const auto status=preflight(in,&s);
 if(status!=AgxRootKeyStatus::Ok)return status;
 if(!dst||capacity<s.bytes||!extent({dst,capacity}))return AgxRootKeyStatus::DestinationInvalid;
 const AgxRootKeySpan destination{dst,s.bytes};
 if(overlaps(destination,{in,sizeof(*in)})||overlaps(destination,in->args)||overlaps(destination,in->description)||
    overlaps(destination,in->parameters)||overlaps(destination,in->samplers)||overlaps(destination,in->table_spans))return AgxRootKeyStatus::DestinationInvalid;
 for(UINT n=0;n<s.desc->NumParameters;++n)if(raw32(s.desc->pRootParameters[n].ParameterType)==D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE&&overlaps(destination,s.tables[n]))return AgxRootKeyStatus::DestinationInvalid;
 auto*start=static_cast<unsigned char*>(dst);std::memcpy(start,"AGXRKEY1",8);Writer w{start+8};
 w.word(1);w.word(1);w.word(s.args->NodeMask);w.word(raw32(s.desc->Flags));w.word(s.desc->NumParameters);w.word(s.desc->NumStaticSamplers);
 for(UINT n=0;n<s.desc->NumParameters;++n){
  const auto&p=s.desc->pRootParameters[n];w.word(raw32(p.ParameterType));w.word(raw32(p.ShaderVisibility));
  switch(raw32(p.ParameterType)){
  case D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
   w.word(p.DescriptorTable.NumDescriptorRanges);
   for(UINT r=0;r<p.DescriptorTable.NumDescriptorRanges;++r){const auto&t=p.DescriptorTable.pDescriptorRanges[r];w.word(raw32(t.RangeType));w.word(t.NumDescriptors);w.word(t.BaseShaderRegister);w.word(t.RegisterSpace);w.word(t.OffsetInDescriptorsFromTableStart);}break;
  case D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:w.word(p.Constants.ShaderRegister);w.word(p.Constants.RegisterSpace);w.word(p.Constants.Num32BitValues);break;
  default:w.word(p.Descriptor.ShaderRegister);w.word(p.Descriptor.RegisterSpace);break; /* kinds preflighted */
  }
 }
 for(UINT n=0;n<s.desc->NumStaticSamplers;++n){const auto&t=s.desc->pStaticSamplers[n];
  w.word(raw32(t.Filter));w.word(raw32(t.AddressU));w.word(raw32(t.AddressV));w.word(raw32(t.AddressW));w.word(raw32(t.MipLODBias));w.word(t.MaxAnisotropy);w.word(raw32(t.ComparisonFunc));w.word(raw32(t.BorderColor));w.word(raw32(t.MinLOD));w.word(raw32(t.MaxLOD));w.word(t.ShaderRegister);w.word(t.RegisterSpace);w.word(raw32(t.ShaderVisibility));
 }
 *out={start,s.bytes};return AgxRootKeyStatus::Ok;
}
