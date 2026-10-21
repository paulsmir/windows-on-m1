#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "asahi/layout/layout.h"
#include "agx_win32_asahi_layout.h"
using UINT=unsigned;using UINT64=unsigned long long;using DXGI_FORMAT=unsigned;using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_NOTIMPL (-2)
#define E_OUTOFMEMORY (-3)
#define D3D12DDI_TL_UNDEFINED 0
using D3D12DDI_TEXTURE_LAYOUT=unsigned;using UINT8=unsigned char;
/* VOLUME_LAYOUT_RECORD */
#define D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET 1
#define D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE 16
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define FALSE 0
#define TRUE 1
#define AGX_D3D12_BIND_RENDER_TARGET 1u
#define AGX_D3D12_BIND_SAMPLER 2u
#define AGX_D3D12_BIND_DEPTH_STENCIL 4u
#define APPLE_AGX_D3D12_NATIVE_OWNER 1
#define APIENTRY
using VOID=void;
enum {DXGI_FORMAT_R8G8B8A8_UNORM=28,DXGI_FORMAT_R32_UINT=42};
enum {D3D12DDI_RT_TEXTURE2D=3,D3D12DDI_RT_TEXTURE3D=4,D3D12DDI_RT_TEXTURE1D=2};
static constexpr UINT64 kHeapPage=0x10000;
struct AGX_D3D12_BUFFER {void *Resource;UINT64 GpuVa,Bytes;};
struct AGX_D3D12_OWNER {pipe_screen *Screen;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *) {}}; /* the owner lock (EXP1824) is not a fixture boundary */
struct D3D12DDIARG_CREATERESOURCE_0003 {unsigned ResourceType;struct {UINT Count,Quality;} SampleDesc;UINT64 Width;UINT Height,DepthOrArraySize,MipLevels;DXGI_FORMAT Format;unsigned Flags,Layout;};
static pipe_format FormatTranslate(DXGI_FORMAT format,int) {return format==28?PIPE_FORMAT_R8G8B8A8_UNORM:PIPE_FORMAT_R32_UINT;}
static UINT query_calls=0,create_calls=0;
static pipe_resource queried,created;
static int query_ok=1;
extern "C" int AgxWin32AsahiQueryUncompressedResourceLayout(const pipe_resource *t,AGX_WIN32_ASAHI_RESOURCE_LAYOUT *r) {
 ++query_calls;queried=*t;std::memset(r,0,sizeof(*r));if(!query_ok)return 0;
 r->LevelOffsets[1]=128;r->LevelOffsets[2]=256;r->LevelOffsets[3]=384;r->LayerStrideBytes=16384;r->SizeBytes=16384ull*t->depth0;r->AllocatedLevels=4;r->Modifier=0x1234;r->RequestedLevels=t->last_level+1;r->Flags=AGX_WIN32_ASAHI_LAYOUT_MIPMAPPED_Z;
 return 1;
}
static void receipt(const char *,HRESULT,UINT,UINT,UINT) {}
static UINT64 AgxWin32AsahiBufferGpuAddress(pipe_resource *) {return 0x1000000;}
static void AgxWin32AsahiResourceRelease(pipe_resource **r) {*r=nullptr;}
static pipe_resource created_resource;
static bool supported(pipe_screen *,pipe_format,pipe_texture_target,unsigned,unsigned,unsigned) {return true;}
static pipe_resource *create(pipe_screen *,const pipe_resource *t,const uint64_t *m,int count) {
 assert(count==1 && m[0]==0x1234);++create_calls;created=*t;created_resource=*t;return &created_resource;
}

/* ACTUAL_QUERY */
/* ACTUAL_TEXTURE_BYTES */
int main() {
 D3D12DDIARG_CREATERESOURCE_0003 r={};r.ResourceType=D3D12DDI_RT_TEXTURE3D;r.SampleDesc.Count=1;
 r.Width=r.Height=4;r.DepthOrArraySize=8;r.MipLevels=2;r.Format=42;
 UINT64 bytes=0;assert(TextureBytes(&r)==0x20000 && query_calls==1 && !create_calls);
 assert(AgxD3d12QueryTexture3D(42,4,4,8,2,0,&bytes,nullptr)==S_OK && bytes==0x20000);
 assert(queried.target==PIPE_TEXTURE_3D && queried.width0==4 && queried.height0==4 && queried.depth0==8 && queried.array_size==1 && queried.last_level==1);
 assert(!(queried.bind&PIPE_BIND_SHADER_IMAGE));
 unsigned calls=query_calls;
 for(unsigned bad=0;bad<9;bad++) {auto v=r;switch(bad){case 0:v.Width=64;break;case 1:v.Height=8;break;case 2:v.DepthOrArraySize=0;break;case 3:v.MipLevels=3;break;case 4:v.SampleDesc.Count=2;break;case 5:v.Layout=1;break;case 6:v.Format=99;break;case 7:v.Width=v.Height=3;break;case 8:v.Flags=2;break;} assert(!TextureBytes(&v));}
 assert(calls==query_calls);
 AGX_D3D12_VOLUME_LAYOUT layout={};assert(AgxD3d12QueryTexture3DLayout(42,4,4,8,2,0,&layout)==S_OK);
 assert(layout.Bytes==0x20000 && layout.NativeBytes==0x20000 && layout.DepthStride==16384 && layout.MipOffsets[1]==128);
 query_ok=0;assert(AgxD3d12QueryTexture3DLayout(42,4,4,8,2,0,&layout)==E_NOTIMPL && !layout.Bytes);query_ok=1;
 pipe_screen screen={};screen.is_format_supported=supported;screen.resource_create_with_modifiers=create;
 AGX_D3D12_OWNER owner={&screen};AGX_D3D12_BUFFER texture={};
 assert(AgxD3d12OwnerCreateTexture3D(&owner,42,4,4,8,2,0,0x20000,&texture)==S_OK && create_calls==1);
 assert(AgxD3d12OwnerCreateTexture3D(&owner,42,4,4,8,2,0,0x10000,&texture)==E_NOTIMPL && create_calls==1);
 puts("PASS bounded volume pure size/metadata/create guards");
}
