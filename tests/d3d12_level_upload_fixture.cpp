// rc103: the actual owner replay of CopyTextureRegion uploads (level_upload,
// staged_upload, AgxD3d12OwnerCopyTexture from agx_d3d12_windows.cpp) over a
// CPU model. The model GPU runs a recorded compute copy only when the owner
// drains (AgxD3d12OwnerSubmitAndWait) or at the end, reading the ring span
// at that time: a span the CPU rewrote before its copy ran corrupts the
// level, so the ring-reuse rule is checked, not assumed.
#define owner_watch_resource(...) ((void)0)
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
using UINT=unsigned;using HRESULT=int;using LONG=long;using DWORD=unsigned long;using SIZE_T=size_t;
#define S_OK 0
#define S_FALSE 1
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define DXGI_ERROR_DEVICE_HUNG (-3)
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define TRUE 1
#define MemoryBarrier() ((void)0)
struct AGX_D3D12_BUFFER {void *Resource;unsigned long long GpuVa,Bytes;void *Cpu;};
/* LEVEL_RING_HEADER */
struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingCopies;pipe_screen *Screen;pipe_resource *UploadStaging,*DownloadStaging;
 AGX_D3D12_BUFFER LevelRing;unsigned long long LevelRingUsed;void *LevelScratch;unsigned long long LevelScratchBytes;void *LevelLinear;unsigned long long LevelLinearBytes;
 bool LevelRingFailed;UINT LevelUploads,LevelRingDrains;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *) {}};
static void AgxD3d12OwnerRecordError(AGX_D3D12_OWNER *,HRESULT) {}
static void receipt(const char *,HRESULT,UINT,UINT,UINT) {}
static void receipt(const char *,HRESULT,UINT,UINT,UINT,UINT,UINT) {}
static unsigned GetEnvironmentVariableA(const char *name,char *out,unsigned size) {
 const char *v=getenv(name);if(!v)return 0;const unsigned n=static_cast<unsigned>(strlen(v));
 if(n<size){memcpy(out,v,n+1);} return n;
}
static LONG InterlockedExchange(volatile LONG *target,LONG value) {const LONG old=*target;*target=value;return old;}
static void *GetProcessHeap() {return nullptr;}
static void *HeapAlloc(void *,unsigned,SIZE_T bytes) {return malloc(bytes);}
static int HeapFree(void *,unsigned,void *p) {free(p);return 1;}
/* ACTUAL_COPY_RECORD */
static bool reserved_texture(pipe_resource *) {return false;}
static bool compressed_texture(AGX_D3D12_OWNER *,pipe_resource *) {return false;}
static HRESULT copy_reserved_texture(AGX_D3D12_OWNER *,const AGX_D3D12_TEXTURE_COPY *) {return E_INVALIDARG;}

/* Model resources. A texture's level is "tiled" by reversing its block rows,
 * so a copy that skipped AgxWin32AsahiTileLevel shows. */
struct Store {pipe_resource r={};std::vector<unsigned char> data;bool ineligible=false;};
static unsigned creates,blits,maps,ring_creates,drains,tiles,copies_recorded;
static bool fail_ring,fail_copy;
static std::vector<pipe_resource *> rings;
static void AgxWin32AsahiResourceRelease(pipe_resource **r) {delete reinterpret_cast<Store *>(*r);*r=nullptr;}
static unsigned blocks_x(const pipe_resource *r,unsigned level) {return util_format_get_nblocksx(r->format,r->width0>>level?r->width0>>level:1);}
static unsigned blocks_y(const pipe_resource *r,unsigned level) {return util_format_get_nblocksy(r->format,r->height0>>level?r->height0>>level:1);}
static size_t level_bytes(const pipe_resource *r,unsigned level) {return (size_t)blocks_x(r,level)*blocks_y(r,level)*util_format_get_blocksize(r->format);}
static size_t level_offset(const pipe_resource *r,unsigned level,unsigned layer) {
 size_t layer_bytes=0;for(unsigned l=0;l<=r->last_level;l++)layer_bytes+=level_bytes(r,l);
 size_t at=layer*layer_bytes;for(unsigned l=0;l<level;l++)at+=level_bytes(r,l);return at;
}
static size_t offset(pipe_resource *r,unsigned level,unsigned x,unsigned y,unsigned z) {
 return level_offset(r,level,z)+((size_t)y*blocks_x(r,level)+x)*util_format_get_blocksize(r->format);
}
static pipe_resource *create(pipe_screen *,const pipe_resource *t) {
 assert(!t->flags && t->usage==PIPE_USAGE_STAGING);++creates;
 Store *s=new Store;s->r=*t;s->data.resize((size_t)t->width0*t->height0*16);return &s->r;
}
static pipe_transfer tr[4];static unsigned next_tr;
static void *map(pipe_context *,pipe_resource *r,unsigned level,unsigned usage,const pipe_box *box,pipe_transfer **t) {
 assert(!(r->flags & PIPE_RESOURCE_FLAG_UNMAPPABLE));++maps;
 *t=&tr[next_tr++%4];**t={};
 (*t)->stride=r->target==PIPE_BUFFER?r->width0:blocks_x(r,level)*util_format_get_blocksize(r->format);
 (void)usage;
 const unsigned bw=r->target==PIPE_BUFFER?1u:util_format_get_blockwidth(r->format),bh=r->target==PIPE_BUFFER?1u:util_format_get_blockheight(r->format);
 return reinterpret_cast<Store *>(r)->data.data()+(r->target==PIPE_BUFFER?box->x:offset(r,level,box->x/bw,box->y/bh,box->z));
}
static void unmap(pipe_context *,pipe_transfer *) {}
static void blit(pipe_context *,const pipe_blit_info *b) {
 auto *s=reinterpret_cast<Store *>(b->src.resource),*d=reinterpret_cast<Store *>(b->dst.resource);
 for(int y=0;y<b->src.box.height;y++) {
  const size_t row=b->src.box.width*util_format_get_blocksize(b->src.format);
  memcpy(d->data.data()+offset(&d->r,b->dst.level,b->dst.box.x,b->dst.box.y+y,b->dst.box.z),
         s->data.data()+offset(&s->r,b->src.level,b->src.box.x,b->src.box.y+y,b->src.box.z),row);
 }
 ++blits;
}
/* Native layout stand-ins (agx_win32_asahi_level_upload.h contracts). */
static uint64_t AgxWin32AsahiLevelUploadBytes(const pipe_resource *t,unsigned level,unsigned layer,unsigned w,unsigned h) {
 const Store *s=reinterpret_cast<const Store *>(t);
 if(s->ineligible || level>t->last_level || layer>=t->array_size) return 0;
 if(w!=(t->width0>>level?t->width0>>level:1u) || h!=(t->height0>>level?t->height0>>level:1u)) return 0;
 return level_bytes(t,level);
}
static int AgxWin32AsahiTileLevel(const pipe_resource *t,unsigned level,void *dst,uint64_t bytes,const void *linear,unsigned pitch) {
 assert(bytes==level_bytes(t,level));++tiles;
 const unsigned rows=blocks_y(t,level);const size_t row=(size_t)blocks_x(t,level)*util_format_get_blocksize(t->format);
 assert(pitch>=row);
 for(unsigned y=0;y<rows;y++) memcpy(static_cast<unsigned char *>(dst)+(rows-1-y)*row,static_cast<const unsigned char *>(linear)+(size_t)y*pitch,row);
 return 1;
}
struct Pending {Store *texture;unsigned level,layer;pipe_resource *src;uint64_t offset,bytes;};
static std::vector<Pending> gpu;
static void run_gpu() {
 for(const Pending &p:gpu) {
  Store *src=reinterpret_cast<Store *>(p.src);
  assert(p.offset+p.bytes<=src->data.size());
  const unsigned rows=blocks_y(&p.texture->r,p.level);const size_t row=p.bytes/rows;
  for(unsigned y=0;y<rows;y++) /* the model GPU untiles what the CPU tiled */
   memcpy(p.texture->data.data()+level_offset(&p.texture->r,p.level,p.layer)+(size_t)y*row,src->data.data()+p.offset+(rows-1-y)*row,row);
 }
 gpu.clear();
}
static int AgxWin32AsahiCopyToLevel(pipe_context *,pipe_resource *t,unsigned level,unsigned layer,pipe_resource *src,uint64_t offset,uint64_t bytes) {
 assert(!rings.empty() && src==rings.back());
 assert(offset%AGX_D3D12_LEVEL_RING_ALIGN==0 && offset+bytes<=AGX_D3D12_LEVEL_RING_BYTES && bytes==level_bytes(t,level));
 for(const Pending &p:gpu) /* no recorded copy may still read the span */
  assert(p.src!=src || offset>=p.offset+p.bytes || offset+bytes<=p.offset);
 if(fail_copy) return 0;
 gpu.push_back({reinterpret_cast<Store *>(t),level,layer,src,offset,bytes});++copies_recorded;return 1;
}
static HRESULT AgxD3d12OwnerCreateBuffer(AGX_D3D12_OWNER *,unsigned long long bytes,int cpu,AGX_D3D12_BUFFER *b) {
 assert(cpu && bytes==AGX_D3D12_LEVEL_RING_BYTES);++ring_creates;
 if(fail_ring) return E_OUTOFMEMORY;
 Store *s=new Store;s->r.target=PIPE_BUFFER;s->r.width0=static_cast<unsigned>(bytes);s->data.resize(bytes,0xcc);
 rings.push_back(&s->r);b->Resource=&s->r;b->Cpu=s->data.data();b->Bytes=bytes;return S_OK;
}
static void AgxD3d12OwnerDestroyBuffer(AGX_D3D12_OWNER *,AGX_D3D12_BUFFER *b) {
 pipe_resource *r=static_cast<pipe_resource *>(b->Resource);AgxWin32AsahiResourceRelease(&r);memset(b,0,sizeof(*b));
}
static HRESULT AgxD3d12OwnerSubmitAndWait(AGX_D3D12_OWNER *) {++drains;run_gpu();return S_OK;}
/* ACTUAL_OWNER_COPY */
static Store *texture(unsigned size,unsigned levels,unsigned layers,unsigned flags) {
 Store *s=new Store;s->r.target=layers>1?PIPE_TEXTURE_2D_ARRAY:PIPE_TEXTURE_2D;s->r.format=PIPE_FORMAT_BPTC_RGBA_UNORM;
 s->r.width0=s->r.height0=size;s->r.depth0=1;s->r.array_size=static_cast<uint16_t>(layers);s->r.last_level=static_cast<uint8_t>(levels-1);
 s->r.flags=flags;s->data.resize(level_offset(&s->r,0,layers),0xaa);return s;
}
/* One CopyTextureRegion of `level` (whole level unless x/w say otherwise)
 * from an upload buffer whose block rows carry a pattern. */
static HRESULT upload(AGX_D3D12_OWNER *owner,Store *t,unsigned level,unsigned layer,std::vector<unsigned char> &expected,
                      unsigned x=0,unsigned width=0) {
 const unsigned bx=blocks_x(&t->r,level),by=blocks_y(&t->r,level);const unsigned pitch=(bx*16+255)/256*256;
 Store buffer;buffer.r.target=PIPE_BUFFER;buffer.r.width0=pitch*by+64;buffer.data.resize(buffer.r.width0);
 for(size_t i=0;i<buffer.data.size();i++)buffer.data[i]=static_cast<unsigned char>(i*31+level*7+layer*3+t->r.width0);
 AGX_D3D12_BUFFER g={&t->r,0,t->data.size(),nullptr},b={&buffer.r,0,buffer.data.size(),nullptr};
 AGX_D3D12_TEXTURE_COPY c={};c.Texture=&g;c.Buffer=&b;c.Level=level;c.Layer=layer;c.Depth=1;c.Offset=64;c.Pitch=pitch;
 const unsigned w=width?width:(t->r.width0>>level?t->r.width0>>level:1u),h=t->r.height0>>level?t->r.height0>>level:1u;
 c.X=x;c.Width=(w+3)/4*4;c.Height=(h+3)/4*4;
 for(unsigned y=0;y<by;y++)memcpy(expected.data()+offset(&t->r,level,x/4,y,layer),buffer.data.data()+64+(size_t)y*pitch,(c.Width/4)*16);
 return AgxD3d12OwnerCopyTexture(owner,&c);
}
int main(int argc,char **argv) {
 assert(argc==2);const bool on=!strcmp(argv[1],"on");
 pipe_screen screen={};screen.resource_create=create;
 pipe_context pipe={};pipe.texture_map=map;pipe.buffer_map=map;pipe.texture_unmap=unmap;pipe.buffer_unmap=unmap;pipe.blit=blit;
 AGX_D3D12_OWNER owner={};owner.Context=&pipe;owner.Screen=&screen;
 /* 40 GPU-only 1024x1024 BC7 textures per 16 MiB of ring (rc112: 64 MiB on
  * 64-bit): level 0 is 1 MiB */
 const unsigned count=40u*static_cast<unsigned>(AGX_D3D12_LEVEL_RING_BYTES>>24);
 std::vector<Store *> textures;std::vector<std::vector<unsigned char>> expected;
 for(unsigned i=0;i<count;i++){textures.push_back(texture(1024,11,1,PIPE_RESOURCE_FLAG_UNMAPPABLE));expected.push_back(textures.back()->data);}
 for(unsigned i=0;i<count;i++)for(unsigned level=0;level<11;level++)assert(upload(&owner,textures[i],level,0,expected[i])==S_OK);
 Store *array=texture(64,7,3,PIPE_RESOURCE_FLAG_UNMAPPABLE);auto array_expected=array->data;
 for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<7;level++)assert(upload(&owner,array,level,layer,array_expected)==S_OK);
 run_gpu();
 for(unsigned i=0;i<count;i++)assert(textures[i]->data==expected[i]);
 assert(array->data==array_expected);
 if(on) {
  /* one compute copy per level and no blit; count x 1.33 MiB of levels drain
   * the ring 3 times; the ring and the staging texture are created once */
  assert(blits==0 && copies_recorded==count*11+21 && tiles==copies_recorded && owner.LevelUploads==copies_recorded);
  unsigned long long used=0;unsigned expect=0;
  auto place=[&](size_t bytes){if(used+bytes>AGX_D3D12_LEVEL_RING_BYTES){++expect;used=0;}used=(used+bytes+255)/256*256;};
  for(unsigned i=0;i<count;i++)for(unsigned level=0;level<11;level++)place(level_bytes(&textures[i]->r,level));
  for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<7;level++)place(level_bytes(&array->r,level));
  assert(expect==3 && drains==expect && owner.LevelRingDrains==expect && ring_creates==1 && creates==0);
  assert(owner.PendingCopies==copies_recorded);
 } else {
  assert(blits==count*11+21 && copies_recorded==0 && tiles==0 && ring_creates==0 && drains==0);
 }
 /* a partial level, an ineligible layout and a mappable texture keep their paths */
 const unsigned before=blits,recorded=copies_recorded;
 Store *partial=texture(256,1,1,PIPE_RESOURCE_FLAG_UNMAPPABLE);auto partial_expected=partial->data;
 assert(upload(&owner,partial,0,0,partial_expected,64,128)==S_OK && blits==before+1);
 Store *compressed=texture(256,1,1,PIPE_RESOURCE_FLAG_UNMAPPABLE);compressed->ineligible=true;auto compressed_expected=compressed->data;
 assert(upload(&owner,compressed,0,0,compressed_expected)==S_OK && blits==before+2);
 Store *mappable=texture(256,1,1,0);auto mappable_expected=mappable->data;
 assert(upload(&owner,mappable,0,0,mappable_expected)==S_OK && blits==before+2);
 run_gpu();
 assert(partial->data==partial_expected && compressed->data==compressed_expected && mappable->data==mappable_expected);
 assert(copies_recorded==recorded);
 if(on) {
  /* a refused compute copy blits and leaves the ring's used bytes as they were */
  const unsigned long long used=owner.LevelRingUsed;fail_copy=true;
  Store *refused=texture(256,1,1,PIPE_RESOURCE_FLAG_UNMAPPABLE);auto refused_expected=refused->data;
  assert(upload(&owner,refused,0,0,refused_expected)==S_OK && blits==before+3 && owner.LevelRingUsed==used);
  assert(refused->data==refused_expected);fail_copy=false;
  /* a failed ring creation blits and is not retried */
  AGX_D3D12_OWNER fresh={};fresh.Context=&pipe;fresh.Screen=&screen;fail_ring=true;
  Store *first=texture(256,1,1,PIPE_RESOURCE_FLAG_UNMAPPABLE);auto first_expected=first->data;
  assert(upload(&fresh,first,0,0,first_expected)==S_OK && upload(&fresh,first,0,0,first_expected)==S_OK);
  assert(ring_creates==2 && fresh.LevelRingFailed && blits==before+5 && first->data==first_expected);
  if(fresh.UploadStaging)AgxWin32AsahiResourceRelease(&fresh.UploadStaging);
 }
 if(owner.LevelRing.Resource)AgxD3d12OwnerDestroyBuffer(&owner,&owner.LevelRing);
 free(owner.LevelScratch);free(owner.LevelLinear);
 if(owner.UploadStaging)AgxWin32AsahiResourceRelease(&owner.UploadStaging);
 printf("level upload %s: PASS blits %u copies %u drains %u\n",argv[1],blits,copies_recorded,drains);
}
