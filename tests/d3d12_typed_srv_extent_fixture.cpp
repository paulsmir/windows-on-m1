/* SPDX-License-Identifier: MIT */
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "dxbc_resources.h"
#include "agx_d3d12_view_mapping.h"
using UINT=unsigned;using UINT64=unsigned long long;using DXGI_FORMAT=unsigned;
using HRESULT=int;
#define FALSE 0
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
static constexpr unsigned AGX_D3D12_CBV_SLOTS=14,AGX_D3D12_SRV_SLOTS=128,AGX_D3D12_UAV_SLOTS=8;
enum {DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R8_UINT,DXGI_FORMAT_R16_UINT,DXGI_FORMAT_R32_UINT};
struct AGX_D3D12_BUFFER {void *Resource;UINT64 GpuVa,Bytes;};
struct AGX_D3D12_CBV {const AGX_D3D12_BUFFER *Buffer;UINT64 Offset;const void *Data;unsigned Bytes;};
struct AGX_D3D12_UAV {const AGX_D3D12_BUFFER *Buffer;UINT64 Offset,Bytes;DXGI_FORMAT Format;int Texture;UINT Level,FirstLayer,Layers;const AGX_D3D12_BUFFER *Counter;UINT64 CounterOffset;};
#ifndef DXBC_NIR_UAV_COUNTER_SLOT
#define DXBC_NIR_UAV_COUNTER_SLOT 8 /* C33: dxbc_to_nir.h */
#endif
/* OWNER_SRV */
static pipe_format FormatTranslate(DXGI_FORMAT f,int) {
 return f==DXGI_FORMAT_R8_UINT?PIPE_FORMAT_R8_UINT:f==DXGI_FORMAT_R16_UINT?PIPE_FORMAT_R16_UINT:
        f==DXGI_FORMAT_R32_UINT?PIPE_FORMAT_R32_UINT:PIPE_FORMAT_NONE;
}
static bool uav_image(const AGX_D3D12_UAV *,pipe_image_view *) {return false;}
/* PRODUCTION */
static pipe_sampler_view captured;
static pipe_sampler_view *capture(pipe_context *,pipe_resource *,const pipe_sampler_view *view) {captured=*view;return &captured;}
static void cb(pipe_context *,mesa_shader_stage,unsigned,const pipe_constant_buffer *) {}
static void sv(pipe_context *,mesa_shader_stage,unsigned,unsigned,unsigned,pipe_sampler_view **) {}
static void sb(pipe_context *,mesa_shader_stage,unsigned,unsigned,const pipe_shader_buffer *,unsigned) {}
static void si(pipe_context *,mesa_shader_stage,unsigned,unsigned,unsigned,const pipe_image_view *) {}
int main(int argc,char **argv) {
 assert(argc==2);unsigned component=static_cast<unsigned>(std::atoi(argv[1]));
 const bool untyped=component==0;
 DXGI_FORMAT f=component==1?DXGI_FORMAT_R8_UINT:component==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT;
 unsigned expected=untyped?12:3*component;
 pipe_resource resource={};resource.target=PIPE_BUFFER;
 AGX_D3D12_BUFFER buffer={&resource,0,65536};
 AGX_D3D12_SRV srvs[AGX_D3D12_SRV_SLOTS]={};
 srvs[0].Buffer=&buffer;srvs[0].Offset=64;srvs[0].Bytes=untyped?15:expected;
 srvs[0].Format=untyped?DXGI_FORMAT_UNKNOWN:f;
 srvs[0].ComponentMapping=AGX_D3D12_COMPONENT_MAPPING_IDENTITY;
 AGX_D3D12_CBV cbvs[AGX_D3D12_CBV_SLOTS]={};
 AGX_D3D12_UAV uavs[AGX_D3D12_UAV_SLOTS]={};
 uint8_t dimensions[DXBC_RESOURCE_SLOTS]={};pipe_sampler_view *views[AGX_D3D12_SRV_SLOTS]={};unsigned view_count=0;
 pipe_context pipe={};pipe.create_sampler_view=capture;pipe.set_constant_buffer=cb;
 pipe.set_sampler_views=sv;pipe.set_shader_buffers=sb;pipe.set_shader_images=si;
 assert(bind_stage(&pipe,MESA_SHADER_COMPUTE,cbvs,srvs,uavs,views,&view_count,dimensions,nullptr)==S_OK);
 assert(view_count==1 && captured.target==PIPE_BUFFER && captured.u.buf.offset==64);
 unsigned elements=agx_texture_buffer_size_el(captured.format,captured.u.buf.size);
 std::printf("typed component_bytes=%u captured_bytes=%u expected_bytes=%u actual_Asahi_elements=%u expected_elements=3 trailing_index2_addressable=%u\n",
 component,captured.u.buf.size,expected,elements,elements>2);std::fflush(stdout);
 assert(captured.u.buf.size==expected);
 assert(elements==3 && elements>2);
}
