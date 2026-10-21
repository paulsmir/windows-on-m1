/* SPDX-License-Identifier: MIT */
#include <cassert>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <limits>
#include "dxbc_resources.h"
#include <cstring>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/format/u_formats.h"
#if __has_include("agx_d3d12_view_mapping.h")
#include "agx_d3d12_view_mapping.h"
#endif
using UINT=uint32_t; using UINT64=uint64_t; using BYTE=unsigned char;
using VOID=void; using HRESULT=int; using DXGI_FORMAT=unsigned;
#define APIENTRY
#define FALSE 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define S_OK 0
static constexpr UINT kSrvSlots=128,AGX_D3D12_SRV_SLOTS=128,AGX_D3D12_CBV_SLOTS=14,AGX_D3D12_UAV_SLOTS=8;
enum {DXGI_FORMAT_UNKNOWN=0,DXGI_FORMAT_R32_UINT=1,DXGI_FORMAT_R8G8B8A8_UNORM=2,DXGI_FORMAT_R32_TYPELESS=3,DXGI_FORMAT_R32_SINT=4};
enum {D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RD_TEXTURE1D=2,D3D12DDI_RT_BUFFER=1,D3D12DDI_RT_TEXTURE2D=3,D3D12DDI_RT_TEXTURE3D=4,D3D12DDI_RD_BUFFER=1,D3D12DDI_RD_TEXTURE2D=3,D3D12DDI_RD_TEXTURE3D=4,D3D12DDI_RD_TEXTURECUBE=5};
enum {D3D12DDI_BUFFER_SRV_FLAG_RAW=1,kDescriptorSrvBuffer=2,kDescriptorSrvTexture=5};
using D3D12DDI_HDEVICE=void*;
struct D3D12DDI_CPU_DESCRIPTOR_HANDLE {uintptr_t ptr;};
struct AGX_D3D12_BUFFER {void *Resource; UINT64 GpuVa,Bytes;};
struct AgxD3d12Heap {pipe_resource *native; UINT64 gpuva,bytes;};
struct AgxD3d12Resource {AgxD3d12Heap *heap; UINT64 gpuva,bytes; UINT type; DXGI_FORMAT format; UINT width,height,layers,levels;};
struct D3D12DDIARG_TEX2D_SHADER_RESOURCE_VIEW_0002 {UINT MostDetailedMip,FirstArraySlice,MipLevels,ArraySize,PlaneSlice;float ResourceMinLODClamp;};
struct D3D12DDIARG_TEXCUBE_SHADER_RESOURCE_VIEW {UINT MostDetailedMip,MipLevels,First2DArrayFace,NumCubes;float ResourceMinLODClamp;};
struct D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 {
 struct {void *pDrvPrivate;} hDrvResource;
 DXGI_FORMAT Format; UINT ResourceDimension,Shader4ComponentMapping;
 struct {UINT MostDetailedMip,FirstArraySlice,MipLevels,ArraySize;float ResourceMinLODClamp;} Tex1D;
 D3D12DDIARG_TEX2D_SHADER_RESOURCE_VIEW_0002 Tex2D;
 D3D12DDIARG_TEXCUBE_SHADER_RESOURCE_VIEW TexCube;
 struct {UINT MostDetailedMip,MipLevels;float ResourceMinLODClamp;} Tex3D;
 struct {UINT64 FirstElement;UINT NumElements,StructureByteStride,Flags;} Buffer;
};
/* PRODUCTION_OWNER_SRV */
struct AGX_D3D12_CBV {const AGX_D3D12_BUFFER *Buffer;UINT64 Offset;const void *Data;UINT Bytes;};
struct AGX_D3D12_UAV {const AGX_D3D12_BUFFER *Buffer;UINT64 Offset,Bytes;DXGI_FORMAT Format;int Texture;UINT Level,FirstLayer,Layers;};
struct AgxD3d12Descriptor;
struct Bindings {const AgxD3d12Descriptor *srv_descriptor[128];AgxD3d12Heap *srv_heap[128];UINT64 srv_offset[128];};
struct OwnerBindings {AGX_D3D12_BUFFER srv_buffers[128];AGX_D3D12_SRV srvs[128];};
static AGX_D3D12_BUFFER HeapBuffer(AgxD3d12Heap *h) {return {h->native,h->gpuva,h->bytes};}
static UINT last_reason;
static void ViewReceipt(const char *,UINT reason,UINT,UINT) {last_reason=reason;}
static UINT AgxD3d12FormatBlockBytes(DXGI_FORMAT) {return 4;}
static UINT AgxD3d12FormatBlock(DXGI_FORMAT,unsigned *w,unsigned *h) {*w=*h=1;return 4;}
static pipe_format FormatTranslate(DXGI_FORMAT f,int) {return f==DXGI_FORMAT_R32_UINT?PIPE_FORMAT_R32_UINT:f==DXGI_FORMAT_R32_SINT?PIPE_FORMAT_R32_SINT:PIPE_FORMAT_R8G8B8A8_UNORM;}
static bool uav_image(const AGX_D3D12_UAV *,pipe_image_view *) {return false;}
/* PRODUCTION_VIEW_MAPPING */
static_assert(sizeof(AgxD3d12Descriptor)<=64,"descriptor stride must fit");
static pipe_sampler_view captured;
static unsigned capture_calls;
static pipe_sampler_view *capture(pipe_context *,pipe_resource *,const pipe_sampler_view *view) {++capture_calls;captured=*view;return &captured;}
static void cb(pipe_context *,mesa_shader_stage,unsigned,const pipe_constant_buffer *) {}
static void sv(pipe_context *,mesa_shader_stage,unsigned,unsigned,unsigned,pipe_sampler_view **) {}
static void sb(pipe_context *,mesa_shader_stage,unsigned,unsigned,const pipe_shader_buffer *,unsigned) {}
static void si(pipe_context *,mesa_shader_stage,unsigned,unsigned,unsigned,const pipe_image_view *) {}
static UINT encode(UINT a,UINT b,UINT c,UINT d) {return 0x1000u|a|(b<<3)|(c<<6)|(d<<9);}
static uint8_t shader_dimensions[DXBC_RESOURCE_SLOTS];
static HRESULT bind(OwnerBindings &o) {
 pipe_context pipe={};pipe.create_sampler_view=capture;pipe.set_constant_buffer=cb;
 pipe.set_sampler_views=sv;pipe.set_shader_buffers=sb;pipe.set_shader_images=si;
 AGX_D3D12_CBV cbs[14]={};AGX_D3D12_UAV uavs[8]={};pipe_sampler_view *views[128]={};unsigned count;
 return bind_stage(&pipe,MESA_SHADER_FRAGMENT,cbs,o.srvs,uavs,views,&count /* DIMENSIONS_ARGUMENT */);
}
int main(int argc,char **argv) {
 assert(argc==2);pipe_resource resource={};resource.target=PIPE_TEXTURE_2D;resource.width0=resource.height0=2;resource.array_size=1;
 AgxD3d12Heap heap={&resource,0x10000,4096};AgxD3d12Resource r={&heap,0x10000,4096,D3D12DDI_RT_TEXTURE2D,DXGI_FORMAT_R8G8B8A8_UNORM,2,2,1,1};
 D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 arg={};arg.hDrvResource.pDrvPrivate=&r;arg.Format=r.format;
 arg.ResourceDimension=D3D12DDI_RD_TEXTURE2D;arg.Tex2D.MipLevels=arg.Tex2D.ArraySize=1;
 AgxD3d12Descriptor descriptor={};Bindings b={};OwnerBindings o={};
 const unsigned selectors[]={PIPE_SWIZZLE_X,PIPE_SWIZZLE_Y,PIPE_SWIZZLE_Z,PIPE_SWIZZLE_W,PIPE_SWIZZLE_0,PIPE_SWIZZLE_1};
 if(!std::strcmp(argv[1],"mapping")) {
  for(unsigned typed=0;typed<2;++typed) for(unsigned integer=0;integer<3;++integer) {
   arg.Format=integer==2?DXGI_FORMAT_R32_SINT:integer==1?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R8G8B8A8_UNORM;
   r.type=typed?D3D12DDI_RT_BUFFER:D3D12DDI_RT_TEXTURE2D;resource.target=typed?PIPE_BUFFER:PIPE_TEXTURE_2D;
   arg.ResourceDimension=typed?D3D12DDI_RD_BUFFER:D3D12DDI_RD_TEXTURE2D;arg.Buffer.NumElements=16;
   for(unsigned component=0;component<4;++component) for(unsigned selector=0;selector<6;++selector) {
    UINT map[4]={0,1,2,3};map[component]=selector;arg.Shader4ComponentMapping=encode(map[0],map[1],map[2],map[3]);
    CreateShaderResourceView(nullptr,&arg,{reinterpret_cast<uintptr_t>(&descriptor)});assert(!last_reason);
    AgxD3d12Descriptor copy;std::memcpy(&copy,&descriptor,sizeof(copy));b.srv_descriptor[7]=&copy;o={};Replay(b,&o);
    assert(bind(o)==S_OK);
    assert(captured.swizzle_r==selectors[map[0]] && captured.swizzle_g==selectors[map[1]] &&
      captured.swizzle_b==selectors[map[2]] && captured.swizzle_a==selectors[map[3]]);
    unsigned char view_swizzle[4]={(unsigned char)captured.swizzle_r,(unsigned char)captured.swizzle_g,
      (unsigned char)captured.swizzle_b,(unsigned char)captured.swizzle_a};
    // Use Mesa's actual type-aware constant and format/view composition routines.
    for(unsigned bgra=0;bgra<2;++bgra) {
     unsigned char format_swizzle[4]={(unsigned char)(bgra?2:0),1,(unsigned char)(bgra?0:2),3},composed[4];
     util_format_compose_swizzles(format_swizzle,view_swizzle,composed);
     pipe_color_union input={},output={};
     if(integer) {input.ui[0]=17;input.ui[1]=53;input.ui[2]=109;input.ui[3]=197;}
     else {input.f[0]=17.0f/255;input.f[1]=53.0f/255;input.f[2]=109.0f/255;input.f[3]=197.0f/255;}
     util_format_apply_color_swizzle(&output,&input,composed,integer);
     for(unsigned c=0;c<4;++c) {
      unsigned select=map[c];
      if(integer) assert(output.ui[c]==(select<4?input.ui[format_swizzle[select]]:select==5?1u:0u));
      else assert(output.f[c]==(select<4?input.f[format_swizzle[select]]:select==5?1.0f:0.0f));
     }
    }
   }
  }
 } else if(!std::strcmp(argv[1],"refusal")) {
  UINT bad[]={0,encode(6,1,2,3),encode(0,7,2,3),encode(0,1,2,3)|0x2000u};
  for(UINT mapping:bad) {arg.Shader4ComponentMapping=mapping;CreateShaderResourceView(nullptr,&arg,{reinterpret_cast<uintptr_t>(&descriptor)});assert(last_reason && !descriptor.kind);}
  r.type=D3D12DDI_RT_BUFFER;resource.target=PIPE_BUFFER;arg.ResourceDimension=D3D12DDI_RD_BUFFER;
  arg.Buffer.NumElements=16;
  for(unsigned raw=0;raw<2;++raw) {
   arg.Buffer.Flags=raw?D3D12DDI_BUFFER_SRV_FLAG_RAW:0;arg.Buffer.StructureByteStride=raw?0:16;
   arg.Format=raw?DXGI_FORMAT_R32_TYPELESS:DXGI_FORMAT_UNKNOWN;arg.Shader4ComponentMapping=encode(2,0,4,5);
   CreateShaderResourceView(nullptr,&arg,{reinterpret_cast<uintptr_t>(&descriptor)});assert(last_reason && !descriptor.kind);
   arg.Shader4ComponentMapping=encode(0,1,2,3);
   CreateShaderResourceView(nullptr,&arg,{reinterpret_cast<uintptr_t>(&descriptor)});assert(!last_reason && descriptor.kind);
   b.srv_descriptor[7]=&descriptor;o={};Replay(b,&o);
   o.srvs[7].ComponentMapping=encode(2,0,4,5);assert(bind(o)==E_INVALIDARG);
   o.srvs[7].ComponentMapping=0;assert(bind(o)==E_INVALIDARG);
  }
 } else if(!std::strcmp(argv[1],"cube")) {
  r.width=r.height=4;r.layers=12;r.levels=2;
  resource.width0=resource.height0=4;resource.target=PIPE_TEXTURE_2D_ARRAY;resource.array_size=12;resource.last_level=1;
  arg.ResourceDimension=D3D12DDI_RD_TEXTURECUBE;arg.Shader4ComponentMapping=encode(0,1,2,3);
  arg.TexCube={0,1,0,1,0.0f};
  auto replay_cube=[&]() {CreateShaderResourceView(nullptr,&arg,{reinterpret_cast<uintptr_t>(&descriptor)});
    if(last_reason) return false; AgxD3d12Descriptor copy=descriptor;b.srv_descriptor[7]=&copy;o={};Replay(b,&o);return true;};
  shader_dimensions[7]=DXBC_SRV_CUBE;
  assert(replay_cube());assert(bind(o)==S_OK && captured.target==PIPE_TEXTURE_CUBE);
  shader_dimensions[7]=DXBC_SRV_CUBE_ARRAY;
  assert(replay_cube());assert(bind(o)==S_OK && captured.target==PIPE_TEXTURE_CUBE_ARRAY);
  arg.TexCube={1,1,6,1,0.0f};
  assert(replay_cube());assert(bind(o)==S_OK && captured.target==PIPE_TEXTURE_CUBE_ARRAY);
  assert(captured.u.tex.first_level==1 && captured.u.tex.last_level==1 && captured.u.tex.first_layer==6 && captured.u.tex.last_layer==11);
  arg.TexCube={0,UINT_MAX,0,2,0.0f};
  assert(replay_cube());assert(bind(o)==S_OK && captured.u.tex.last_layer==11 && captured.u.tex.last_level==1);
  o.srvs[7].Texture=0;assert(bind(o)==E_INVALIDARG);o.srvs[7].Texture=1;
  shader_dimensions[7]=DXBC_SRV_CUBE;assert(bind(o)==E_INVALIDARG);
  shader_dimensions[7]=DXBC_SRV_OTHER;assert(bind(o)==E_INVALIDARG);
  shader_dimensions[7]=DXBC_SRV_UNUSED;unsigned before=capture_calls;assert(bind(o)==S_OK && capture_calls==before);
  shader_dimensions[7]=DXBC_SRV_CUBE_ARRAY;
  arg.TexCube={0,1,1,1,0.0f};assert(replay_cube());assert(bind(o)==S_OK && captured.u.tex.first_layer==1 && captured.u.tex.last_layer==6);
  for(unsigned bad=0;bad<10;++bad) {
   arg.TexCube={0,1,0,1,0.0f};r.width=4;
   switch(bad) {
    case 0:arg.TexCube.NumCubes=0;break;
    case 1:arg.TexCube.NumCubes=UINT_MAX;break;
    case 2:arg.TexCube.First2DArrayFace=12;break;
    case 3:arg.TexCube.First2DArrayFace=7;break;
    case 4:arg.TexCube.MostDetailedMip=2;break;
    case 5:arg.TexCube.MipLevels=0;break;
    case 6:arg.TexCube.MipLevels=3;break;
    case 7:arg.TexCube.ResourceMinLODClamp=1.0f;break;
    case 8:arg.TexCube.ResourceMinLODClamp=std::numeric_limits<float>::quiet_NaN();break;
    case 9:r.width=8;break;
   }
   assert(!replay_cube() && !descriptor.kind);
  }
 } else {
  resource.target=PIPE_BUFFER;b.srv_heap[7]=&heap;Replay(b,&o);assert(bind(o)==S_OK);
  assert(captured.swizzle_r==PIPE_SWIZZLE_X && captured.swizzle_g==PIPE_SWIZZLE_Y &&
    captured.swizzle_b==PIPE_SWIZZLE_Z && captured.swizzle_a==PIPE_SWIZZLE_W);
  assert(o.srvs[7].ComponentMapping==encode(0,1,2,3));
 }
 std::printf("PASS view mapping descriptor=%zu\n",sizeof(AgxD3d12Descriptor));
}
