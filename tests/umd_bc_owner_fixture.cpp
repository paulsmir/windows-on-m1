/* CPU oracle for actual owner replay. GPU contents are opaque to CPU maps;
 * the controlled blit callback models raw block copying, not hardware. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
using UINT=unsigned;using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define DXGI_ERROR_DEVICE_HUNG (-3)
#define SUCCEEDED(x) ((x)>=0)
struct AGX_D3D12_BUFFER {void *Resource;unsigned long long GpuVa,Bytes;};
struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingCopies;pipe_screen *Screen;pipe_resource *UploadStaging,*DownloadStaging;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *) {}};
static void AgxD3d12OwnerRecordError(AGX_D3D12_OWNER *,HRESULT) {}
static void receipt(const char *,HRESULT,UINT,UINT,UINT) {}
/* ACTUAL_COPY_RECORD */
static bool reserved_texture(pipe_resource *) {return false;}
static bool compressed_texture(AGX_D3D12_OWNER *,pipe_resource *) {return false;}
static HRESULT copy_reserved_texture(AGX_D3D12_OWNER *,const AGX_D3D12_TEXTURE_COPY *) {return E_INVALIDARG;}
struct Store {pipe_resource r={};std::vector<unsigned char> data;};
static unsigned creates,blits,maps,unmaps;static bool fail_create,fail_map;
static void AgxWin32AsahiResourceRelease(pipe_resource **r) {delete reinterpret_cast<Store *>(*r);*r=nullptr;}
static pipe_resource *create(pipe_screen *,const pipe_resource *t) {
 assert(!t->flags && t->usage==PIPE_USAGE_STAGING);
 assert(t->format==PIPE_FORMAT_R32G32_UINT || t->format==PIPE_FORMAT_R32G32B32A32_UINT);
 if(fail_create)return nullptr;
 Store *s=new Store;s->r=*t;s->data.resize(t->width0*t->height0*16);++creates;return &s->r;
}
static size_t offset(pipe_resource *r,unsigned level,unsigned x,unsigned y,unsigned z) {
 unsigned w=util_format_get_nblocksx(r->format,r->width0>>level ? r->width0>>level:1);
 unsigned h=util_format_get_nblocksy(r->format,r->height0>>level ? r->height0>>level:1);
 return ((z*h+y)*w+x)*util_format_get_blocksize(r->format);
}
static pipe_transfer tr[4];static unsigned next_tr;
static void *map(pipe_context *,pipe_resource *r,unsigned level,unsigned,const pipe_box *box,pipe_transfer **t) {
 assert(!(r->flags & PIPE_RESOURCE_FLAG_UNMAPPABLE));++maps;
 if(fail_map){*t=nullptr;return nullptr;}
 *t=&tr[next_tr++%4];**t={};
 (*t)->stride=r->target==PIPE_BUFFER?r->width0:r->width0*util_format_get_blocksize(r->format);
 return reinterpret_cast<Store *>(r)->data.data()+(r->target==PIPE_BUFFER?box->x:offset(r,level,box->x,box->y,box->z));
}
static void unmap(pipe_context *,pipe_transfer *) {++unmaps;}
static void blit(pipe_context *,const pipe_blit_info *b) {
 assert(b->mask==PIPE_MASK_RGBA && b->src.format==b->dst.format);
 assert(b->src.box.width==b->dst.box.width && b->src.box.height==b->dst.box.height);
 auto *s=reinterpret_cast<Store *>(b->src.resource),*d=reinterpret_cast<Store *>(b->dst.resource);
 for(int z=0;z<b->src.box.depth;z++)for(int y=0;y<b->src.box.height;y++) {
  size_t so=offset(&s->r,b->src.level,b->src.box.x,b->src.box.y+y,b->src.box.z+z);
  size_t dest=offset(&d->r,b->dst.level,b->dst.box.x,b->dst.box.y+y,b->dst.box.z+z);
  size_t bytes=b->src.box.width*util_format_get_blocksize(b->src.format);
  assert(so+bytes<=s->data.size() && dest+bytes<=d->data.size());
  memcpy(d->data.data()+dest,s->data.data()+so,bytes);
 }
 ++blits;
}
/* ACTUAL_OWNER_COPY */
int main() {
 pipe_screen screen={};screen.resource_create=create;
 pipe_context pipe={};pipe.texture_map=map;pipe.buffer_map=map;pipe.texture_unmap=unmap;pipe.buffer_unmap=unmap;pipe.blit=blit;
 AGX_D3D12_OWNER owner={&pipe,0,&screen};
 const pipe_format formats[]={PIPE_FORMAT_DXT1_RGBA,PIPE_FORMAT_DXT3_RGBA,PIPE_FORMAT_DXT5_RGBA,
  PIPE_FORMAT_RGTC1_UNORM,PIPE_FORMAT_RGTC2_UNORM,PIPE_FORMAT_BPTC_RGB_UFLOAT,PIPE_FORMAT_BPTC_RGBA_UNORM};
 for(auto format:formats)for(unsigned level:{0u,5u}) {
  Store gpu,buffer;gpu.r.target=PIPE_TEXTURE_2D_ARRAY;gpu.r.format=format;gpu.r.width0=gpu.r.height0=64;
  gpu.r.depth0=1;gpu.r.array_size=3;gpu.r.last_level=5;gpu.r.flags=PIPE_RESOURCE_FLAG_UNMAPPABLE;gpu.data.resize(16*16*3*16,0xaa);
  buffer.r.target=PIPE_BUFFER;buffer.r.width0=4096;buffer.data.resize(4096);
  AGX_D3D12_BUFFER g={&gpu.r,0,gpu.data.size()},b={&buffer.r,0,buffer.data.size()};
  unsigned bytes=util_format_get_blocksize(format),w=level?1:3,h=level?1:2;
  for(unsigned layer=1;layer<=2;layer++) {
   for(unsigned i=0;i<4096;i++)buffer.data[i]=(i*7+layer*13)&255;
   auto upload=buffer.data;auto expected=gpu.data;
   AGX_D3D12_TEXTURE_COPY c={};c.Texture=&g;c.Buffer=&b;c.Layer=layer;c.Level=level;c.Depth=1;
   c.X=level?0:8;c.Y=level?0:12;c.Width=w*4;c.Height=h*4;c.Pitch=256;c.Offset=32;
   unsigned old=blits;
   assert(AgxD3d12OwnerCopyTexture(&owner,&c)==S_OK && blits==old+1);
   for(unsigned y=0;y<h;y++)memcpy(expected.data()+offset(&gpu.r,level,c.X/4,c.Y/4+y,layer),upload.data()+32+y*256,w*bytes);
   assert(gpu.data==expected);
   std::fill(buffer.data.begin(),buffer.data.end(),0x5a);c.ToBuffer=1;c.Offset=64;
   auto download=buffer.data;
   assert(AgxD3d12OwnerCopyTexture(&owner,&c)==S_OK && blits==old+2);
   for(unsigned y=0;y<h;y++)memcpy(download.data()+64+y*256,upload.data()+32+y*256,w*bytes);
   assert(buffer.data==download);
   fail_map=true;auto saved=buffer.data;
   assert(AgxD3d12OwnerCopyTexture(&owner,&c)==DXGI_ERROR_DEVICE_HUNG && buffer.data==saved);
   fail_map=false;
  }
  AgxWin32AsahiResourceRelease(&owner.UploadStaging);AgxWin32AsahiResourceRelease(&owner.DownloadStaging);
  fail_create=true;AGX_D3D12_TEXTURE_COPY c={};c.Texture=&g;c.Buffer=&b;c.Width=c.Height=4;c.Depth=1;c.Pitch=256;
  assert(AgxD3d12OwnerCopyTexture(&owner,&c)==E_OUTOFMEMORY);fail_create=false;
 }
 assert(creates==28); /* upload/download cache reused across array layers */
 puts("BC owner cached staging: PASS");
}
