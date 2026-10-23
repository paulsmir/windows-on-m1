#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
using UINT=unsigned;using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define DXGI_ERROR_DEVICE_HUNG ((HRESULT)0x887a0006u)
#define SUCCEEDED(x) ((x)>=0)
struct AGX_D3D12_BUFFER {void *Resource;unsigned long long GpuVa,Bytes;};
struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingCopies;pipe_screen *Screen;pipe_resource *DownloadStaging;};
static void AgxWin32AsahiResourceRelease(pipe_resource **resource) {*resource=nullptr;}
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *) {}}; /* the owner lock (EXP1824) is not a fixture boundary */
/* ACTUAL_COPY_RECORD */
/* C53 T2D: no reserved textures here (their staging path: the HW2 self-test) */
static bool reserved_texture(pipe_resource *) { return false; }
static HRESULT copy_reserved_texture(AGX_D3D12_OWNER *, const AGX_D3D12_TEXTURE_COPY *) { return E_INVALIDARG; }
/* C70: the fixture's textures are never compressed, so uploads keep the map path */
static bool compressed_texture(AGX_D3D12_OWNER *, struct pipe_resource *) { return false; }
static HRESULT staged_upload(AGX_D3D12_OWNER *, const AGX_D3D12_TEXTURE_COPY *, struct pipe_resource *,
                             struct pipe_resource *, const struct pipe_box &, const struct pipe_box &, unsigned,
                             unsigned long long) { return E_INVALIDARG; }
static unsigned char volume[2][2048],buffer_bytes[8192];
static pipe_transfer tex_transfer,buf_transfer;
static unsigned maps=0, unmaps=0, map_failure=0;
/* C29: a BC1 map is a staging copy of whole 4x4 blocks (agx_transfer_map
 * stride = blocks across the box * 8) */
static bool bc_mode;static pipe_box bc_box;static unsigned char bc_blocks[3][256];
/* C80 (EXP1899N): an imported presentation surface (PIPE_BIND_SHARED) is
 * read back through a GPU copy into the owner's linear staging; a CPU map
 * of the surface itself is counted (each one downloaded the whole surface) */
static pipe_resource staging_res;static unsigned char present_px[4*32],staging_px[8*1024];
static unsigned shared_maps=0,staging_creates=0,staging_copies=0;
static pipe_resource *create_staging(pipe_screen *,const pipe_resource *templ) {++staging_creates;staging_res=*templ;return &staging_res;}
static void copy_region(pipe_context *,pipe_resource *dst,unsigned level,unsigned dx,unsigned dy,unsigned dz,
                        pipe_resource *src,unsigned src_level,const pipe_box *box) {
 assert(dst==&staging_res && !level && !dz && !src_level && (src->bind & PIPE_BIND_SHARED) && box->depth==1);
 ++staging_copies;
 for(int y=0;y<box->height;++y)std::memcpy(staging_px+(dy+y)*1024+dx*4,present_px+(box->y+y)*32+box->x*4,box->width*4);
}
static void *map_texture(pipe_context *,pipe_resource *res,unsigned level,unsigned,const pipe_box *box,pipe_transfer **t) {
 ++maps;tex_transfer={};*t=map_failure==2?nullptr:&tex_transfer;
 if(map_failure==1)return nullptr;
 if(res==&staging_res){tex_transfer.stride=1024;return staging_px+box->y*1024+box->x*4;}
 if(res->bind & PIPE_BIND_SHARED){++shared_maps;tex_transfer.stride=32;return present_px+box->y*32+box->x*4;}
 if(bc_mode){bc_box=*box;tex_transfer.stride=(box->width+3)/4*8;tex_transfer.layer_stride=tex_transfer.stride*((box->height+3)/4);return bc_blocks[level];}
 tex_transfer.stride=64;tex_transfer.layer_stride=512;
 return volume[level]+box->z*512+box->y*64+box->x*4;
}
static void *map_buffer(pipe_context *,pipe_resource *,unsigned,unsigned,const pipe_box *box,pipe_transfer **t) {
 *t=map_failure==4?nullptr:&buf_transfer;return map_failure==3?nullptr:buffer_bytes+box->x;
}
static void unmap(pipe_context *,pipe_transfer *t) { assert(t);++unmaps; }
static void receipt(const char *,HRESULT,UINT,UINT,UINT) {}
/* ACTUAL_OWNER_COPY */
int main() {
 pipe_context pipe={};pipe.texture_map=map_texture;pipe.buffer_map=map_buffer;pipe.texture_unmap=unmap;pipe.buffer_unmap=unmap;
 AGX_D3D12_OWNER owner={&pipe,0};pipe_resource tex={},buf={};tex.target=PIPE_TEXTURE_3D;tex.format=PIPE_FORMAT_R32_UINT;
 tex.width0=tex.height0=tex.depth0=4;tex.array_size=1;tex.last_level=1;buf.target=PIPE_BUFFER;
 AGX_D3D12_BUFFER texture={&tex,0,sizeof(volume)},buffer={&buf,0,sizeof(buffer_bytes)};
 std::memset(volume,0x11,sizeof(volume));std::memset(buffer_bytes,0xcd,sizeof(buffer_bytes));
 for(unsigned z=0;z<2;z++)for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++) {
  unsigned v=101+100*z+10*y+x;std::memcpy(buffer_bytes+64+z*256+y*32+x*4,&v,4);
 }
 AGX_D3D12_TEXTURE_COPY c={};c.Texture=&texture;c.Buffer=&buffer;c.Offset=64;c.Layer=1;c.X=c.Y=1;
 c.Width=c.Height=c.Depth=2;c.Pitch=32;c.SlicePitch=256;
 HRESULT hr=AgxD3d12OwnerCopyTexture(&owner,&c);std::printf("volume upload result=%d maps=%u\n",hr,maps);std::fflush(stdout);assert(hr==S_OK);
 for(unsigned z=0;z<4;z++)for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++) {
  unsigned actual;std::memcpy(&actual,volume[0]+z*512+y*64+x*4,4);
  unsigned expect=z>=1&&z<3&&y>=1&&y<3&&x>=1&&x<3?101+100*(z-1)+10*(y-1)+(x-1):0x11111111;
  assert(actual==expect);
 }
 // Download the same region with a different pitch and slice stride; poison
 // outside the precise destination footprint must remain untouched.
 std::memset(buffer_bytes,0x5a,sizeof(buffer_bytes));c.ToBuffer=1;c.Offset=128;c.Pitch=48;c.SlicePitch=320;
 assert(AgxD3d12OwnerCopyTexture(&owner,&c)==S_OK);
 for(unsigned z=0;z<2;z++)for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++) {
  unsigned actual;std::memcpy(&actual,buffer_bytes+128+z*320+y*48+x*4,4);assert(actual==101+100*z+10*y+x);
 }
 assert(buffer_bytes[127]==0x5a && buffer_bytes[136]==0x5a && buffer_bytes[448-1]==0x5a && buffer_bytes[504]==0x5a);
 c.Level=1;c.Layer=1;c.X=c.Y=0;c.Depth=1;c.ToBuffer=0;c.Pitch=32;c.SlicePitch=256;c.Offset=64;
 assert(AgxD3d12OwnerCopyTexture(&owner,&c)==S_OK);
 unsigned before=maps;c.Layer=2;assert(AgxD3d12OwnerCopyTexture(&owner,&c)==E_INVALIDARG && maps==before);
 c.Layer=1;c.X=0xffffffffu;assert(AgxD3d12OwnerCopyTexture(&owner,&c)==E_INVALIDARG && maps==before);
 c.X=0;c.Offset=sizeof(buffer_bytes)-1;assert(AgxD3d12OwnerCopyTexture(&owner,&c)==E_INVALIDARG && maps==before);
 // C29: BC1 8x8 with three mips -- block rows at the footprint pitch, and a
 // 4x4 block footprint into the 2x2 mip maps only the mip's own extent.
 pipe_resource bc={};bc.target=PIPE_TEXTURE_2D;bc.format=PIPE_FORMAT_DXT1_RGBA;bc.width0=bc.height0=8;bc.depth0=1;bc.array_size=1;bc.last_level=2;
 AGX_D3D12_BUFFER bctex={&bc,0,65536};bc_mode=true;
 for(unsigned i=0;i<sizeof(buffer_bytes);++i)buffer_bytes[i]=static_cast<unsigned char>(i*7+3);
 AGX_D3D12_TEXTURE_COPY b={};b.Texture=&bctex;b.Buffer=&buffer;b.Width=b.Height=8;b.Depth=1;b.Pitch=256;
 assert(AgxD3d12OwnerCopyTexture(&owner,&b)==S_OK && bc_box.width==8 && bc_box.height==8);
 assert(!std::memcmp(bc_blocks[0],buffer_bytes,16) && !std::memcmp(bc_blocks[0]+16,buffer_bytes+256,16));
 b.Level=2;b.Width=b.Height=4;b.Offset=1024;
 assert(AgxD3d12OwnerCopyTexture(&owner,&b)==S_OK && bc_box.width==2 && bc_box.height==2);
 assert(!std::memcmp(bc_blocks[2],buffer_bytes+1024,8));
 /* C21H (EXP1907): the same 4x4 footprint into a BC1 Texture3D's 2x2 mip
  * (the volume bounds count whole blocks too) */
 bc.target=PIPE_TEXTURE_3D;bc.depth0=4;
 assert(AgxD3d12OwnerCopyTexture(&owner,&b)==S_OK && bc_box.width==2 && bc_box.height==2);
 bc.target=PIPE_TEXTURE_2D;bc.depth0=1;
 std::memset(buffer_bytes,0x5a,sizeof(buffer_bytes));b.Level=0;b.Width=b.Height=8;b.Offset=512;b.ToBuffer=1;
 assert(AgxD3d12OwnerCopyTexture(&owner,&b)==S_OK && !std::memcmp(buffer_bytes+512,bc_blocks[0],16) &&
        !std::memcmp(buffer_bytes+768,bc_blocks[0]+16,16) && buffer_bytes[528]==0x5a && buffer_bytes[784]==0x5a);
 before=maps;b.ToBuffer=0;b.X=2;b.Width=4;assert(AgxD3d12OwnerCopyTexture(&owner,&b)==E_INVALIDARG && maps==before);
 b.X=0;b.Width=12;assert(AgxD3d12OwnerCopyTexture(&owner,&b)==E_INVALIDARG && maps==before);
 b.Width=8;b.Pitch=8;assert(AgxD3d12OwnerCopyTexture(&owner,&b)==E_INVALIDARG && maps==before);
 bc_mode=false;
 // WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Failed read/write maps
 // must not copy bytes, dereference a missing transfer, or report success.
 c.X=0;c.Offset=64;
 for(map_failure=1;map_failure<=4;++map_failure) {
   unsigned char saved[sizeof(volume)];std::memcpy(saved,volume,sizeof(volume));
   unmaps=0;
   assert(AgxD3d12OwnerCopyTexture(&owner,&c)==DXGI_ERROR_DEVICE_HUNG);
   assert(!std::memcmp(saved,volume,sizeof(volume)));
   assert(unmaps==(map_failure==3?2u:1u));
 }
 map_failure=0;
 // C80: a 5x3 readback of an 8x4 BGRA presentation surface at (2,1)
 pipe_screen screen={};screen.resource_create=create_staging;owner.Screen=&screen;pipe.resource_copy_region=copy_region;
 pipe_resource pres={};pres.target=PIPE_TEXTURE_2D;pres.format=PIPE_FORMAT_B8G8R8A8_UNORM;pres.width0=8;pres.height0=4;
 pres.depth0=1;pres.array_size=1;pres.bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW|PIPE_BIND_SHARED;
 AGX_D3D12_BUFFER prestex={&pres,0,sizeof(present_px)};
 for(unsigned i=0;i<sizeof(present_px);++i)present_px[i]=static_cast<unsigned char>(i*13+1);
 std::memset(buffer_bytes,0x5a,sizeof(buffer_bytes));
 AGX_D3D12_TEXTURE_COPY r={};r.Texture=&prestex;r.Buffer=&buffer;r.ToBuffer=1;r.X=2;r.Y=1;r.Width=5;r.Height=3;r.Depth=1;
 r.Pitch=256;r.Offset=512;
 for(unsigned pass=0;pass<2;++pass) {
  assert(AgxD3d12OwnerCopyTexture(&owner,&r)==S_OK);
  std::printf("presentation readback pass %u shared_maps=%u staging_copies=%u creates=%u\n",pass,shared_maps,staging_copies,staging_creates);
  std::fflush(stdout);
  assert(!shared_maps && staging_copies==pass+1 && staging_creates==1);
  for(unsigned y=0;y<3;++y) {
   assert(!std::memcmp(buffer_bytes+512+y*256,present_px+(1+y)*32+8,20));
   assert(buffer_bytes[512+y*256-1]==0x5a && buffer_bytes[512+y*256+20]==0x5a);
  }
 }
 /* a larger box than the cached staging makes a new one */
 r.X=0;r.Y=0;r.Width=8;r.Height=4;assert(AgxD3d12OwnerCopyTexture(&owner,&r)==S_OK && !shared_maps);
 assert(!std::memcmp(buffer_bytes+512+3*256,present_px+3*32,32));
 // WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? C80 staging readback
 // must preserve the failed-map contract without mapping the shared surface.
 for(map_failure=1;map_failure<=4;++map_failure) {
   unsigned char saved[sizeof(buffer_bytes)];std::memcpy(saved,buffer_bytes,sizeof(buffer_bytes));
   unmaps=0;
   assert(AgxD3d12OwnerCopyTexture(&owner,&r)==DXGI_ERROR_DEVICE_HUNG);
   assert(!std::memcmp(saved,buffer_bytes,sizeof(buffer_bytes)) && !shared_maps);
   assert(unmaps==(map_failure==3?2u:1u));
 }
 map_failure=0;
 puts("PASS volume real-copy pitches/mip bounds/untouched planes; C29 BC1 block rows/edge mip/refusals; C80 presentation readback via GPU staging");
}
