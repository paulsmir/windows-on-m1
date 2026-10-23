#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
using UINT=unsigned;using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define SUCCEEDED(x) ((x)>=0)
struct AGX_D3D12_BUFFER {void *Resource;unsigned long long GpuVa,Bytes;};
struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingCopies;};
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
static unsigned maps=0;
/* C29: a BC1 map is a staging copy of whole 4x4 blocks (agx_transfer_map
 * stride = blocks across the box * 8) */
static bool bc_mode;static pipe_box bc_box;static unsigned char bc_blocks[3][256];
static void *map_texture(pipe_context *,pipe_resource *,unsigned level,unsigned,const pipe_box *box,pipe_transfer **t) {
 ++maps;tex_transfer={};*t=&tex_transfer;
 if(bc_mode){bc_box=*box;tex_transfer.stride=(box->width+3)/4*8;tex_transfer.layer_stride=tex_transfer.stride*((box->height+3)/4);return bc_blocks[level];}
 tex_transfer.stride=64;tex_transfer.layer_stride=512;
 return volume[level]+box->z*512+box->y*64+box->x*4;
}
static void *map_buffer(pipe_context *,pipe_resource *,unsigned,unsigned,const pipe_box *box,pipe_transfer **t) {
 *t=&buf_transfer;return buffer_bytes+box->x;
}
static void unmap(pipe_context *,pipe_transfer *) {}
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
 puts("PASS volume real-copy pitches/mip bounds/untouched planes; C29 BC1 block rows/edge mip/refusals");
}
