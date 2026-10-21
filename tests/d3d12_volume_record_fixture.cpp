#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
using LONG=int;using BOOL=int;using UINT=unsigned;using UINT64=unsigned long long;using VOID=void;
#define APIENTRY
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_NOTIMPL (-2)
enum {D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RT_BUFFER=1,D3D12DDI_RT_TEXTURE2D=3,D3D12DDI_RT_TEXTURE3D=4};
enum {D3D12DDI_RL_SELECT_SUBRESOURCE=1,D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED=2};
enum {kCommandTextureCopy=4,kCommandTextureRegionCopy=7};
struct AgxD3d12Heap {void *native;UINT64 gpuva,bytes;};
struct AgxD3d12Resource {AgxD3d12Heap *heap;UINT64 gpuva,bytes;UINT type,format,width,height,layers,levels;};
struct D3D12DDIARG_BUFFER_PLACEMENT {struct {struct {struct {void *pDrvPrivate;} hResource;UINT64 Offset;} UMD;} BaseAddress;};
struct D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT {UINT Format,PhysicalWidth,PhysicalHeight,PhysicalDepth,Pitch,SlicePitch;};
struct D3D12DDIARG_PLACED_RESOURCE {UINT Layout;const void *pLayout;};
struct D3D12DDI_BOX {LONG Left,Top,Front,Right,Bottom,Back;};
/* ACTUAL_RECORD_TYPES */
struct AgxD3d12Command {UINT kind;AgxD3d12TextureCopy texture_copy;AgxD3d12TextureRegionCopy region_copy;};
struct AgxD3d12CommandList {unsigned commands;int error;AgxD3d12Command recorded;};
struct D3D12DDI_HCOMMANDLIST {void *pDrvPrivate;};
static const UINT kListCommands=16;
static AgxD3d12Command *AppendCommand(AgxD3d12CommandList *l){++l->commands;l->recorded={};return &l->recorded;}
static void ListError(AgxD3d12CommandList *l,int error){l->error=error;}
static void AdmissionUmdDiagnostic(const char *,int,const UINT *,UINT) {}
static UINT AgxD3d12FormatBlockBytes(UINT) {return 4;}
static UINT AgxD3d12FormatBlock(UINT f,unsigned *w,unsigned *h) {const bool bc=f==71u;*w=*h=bc?4u:1u;return bc?8u:4u;}
/* ACTUAL_RECORDING */
int main() {
 AgxD3d12Heap th={reinterpret_cast<void *>(1),0x10000,131072},bh={reinterpret_cast<void *>(2),0x30000,65536};
 AgxD3d12Resource t={&th,0x10000,131072,D3D12DDI_RT_TEXTURE3D,42,4,4,8,2};
 AgxD3d12Resource b={&bh,0x30000,65536,D3D12DDI_RT_BUFFER,0,65536,1,1,1};
 D3D12DDIARG_BUFFER_PLACEMENT tex={},buf={};tex.BaseAddress.UMD.hResource.pDrvPrivate=&t;tex.BaseAddress.UMD.Offset=1;
 buf.BaseAddress.UMD.hResource.pDrvPrivate=&b;buf.BaseAddress.UMD.Offset=64;
 D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT fp={42,2,2,4,256,512};
 D3D12DDIARG_PLACED_RESOURCE tl={D3D12DDI_RL_SELECT_SUBRESOURCE,nullptr},bl={D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED,&fp};
 AgxD3d12CommandList l={};D3D12DDI_HCOMMANDLIST handle={&l};
 CopyTextureRegion(handle,&tex,tl,0,0,0,&buf,bl,nullptr);
 assert(!l.error &&l.commands==1 &&l.recorded.kind==kCommandTextureCopy);
 auto c=l.recorded.texture_copy;assert(c.level==1 &&c.layer==0 &&c.depth==4 &&c.width==2 &&c.height==2 &&c.offset==64 &&c.pitch==256&&c.slice_pitch==512);
 D3D12DDI_BOX box={0,0,1,2,2,3};l={};CopyTextureRegion(handle,&tex,tl,0,0,1,&buf,bl,&box);
 assert(!l.error&&l.commands==1);c=l.recorded.texture_copy;assert(c.layer==1 &&c.depth==2 &&c.offset==576);
 l={};CopyTextureRegion(handle,&buf,bl,0,0,0,&tex,tl,nullptr);
 assert(!l.error &&l.recorded.texture_copy.to_buffer &&l.recorded.texture_copy.depth==4);
 // A subresource equal to mip count must not alias mip0/arraylayer1.
 tex.BaseAddress.UMD.Offset=2;l={};CopyTextureRegion(handle,&tex,tl,0,0,0,&buf,bl,nullptr);assert(l.error&& !l.commands);
 tex.BaseAddress.UMD.Offset=1;fp.SlicePitch=256;l={};CopyTextureRegion(handle,&tex,tl,0,0,0,&buf,bl,nullptr);assert(l.error&&!l.commands);
 fp.SlicePitch=512;buf.BaseAddress.UMD.Offset=65535;l={};CopyTextureRegion(handle,&tex,tl,0,0,0,&buf,bl,nullptr);assert(l.error&&!l.commands);
 buf.BaseAddress.UMD.Offset=64;l={};CopyTextureRegion(handle,&tex,tl,0xffffffffu,0,0,&buf,bl,nullptr);assert(l.error&&!l.commands);
 D3D12DDIARG_BUFFER_PLACEMENT other=tex;l={};CopyTextureRegion(handle,&tex,tl,0,0,0,&other,tl,&box);
 assert(!l.error&&l.recorded.kind==kCommandTextureRegionCopy&&l.recorded.region_copy.src_layer==1&&l.recorded.region_copy.layers==2);
 box.Left=-1;l={};CopyTextureRegion(handle,&tex,tl,0,0,0,&buf,bl,&box);assert(l.error&&!l.commands);
 puts("PASS volume actualDDI copy subresources/pitches/Z/bounds");
}
