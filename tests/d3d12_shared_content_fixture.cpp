// Diagnostics are outside this replay; RC5 tests their production bodies.
#define owner_watch_resource(...) ((void)0)
/* Execute production private-data decode and copy translation. Gallium callbacks
 * model storage only: a native Direct BO map is a cached CPU shadow, while a
 * resource_copy_region addresses the shared allocation. No GPU is simulated. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <memory>
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#define DXGI_FORMAT_R8G8B8A8_UNORM 28u
#define DXGI_FORMAT_B8G8R8A8_UNORM 87u
#define DXGI_FORMAT_B8G8R8A8_UNORM_SRGB 91u
#define D3DKMDT_GDISURFACE_TEXTURE 2u
#define D3DDDIFMT_A8R8G8B8 21u
#define D3DDDIFMT_A8B8G8R8 32u
#include "agx_d3d12_shared_open.h"
#include "umd_primary_backbuffer.h"
using UINT=unsigned; using HRESULT=int; using BOOLEAN=int;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define D3D10DDIRESOURCE_TEXTURE2D 3u
#define D3D10_DDI_USAGE_DEFAULT 0u
#define DXGI_FORMAT_A8_UNORM 65u
#define D3D10_DDI_BIND_SHADER_RESOURCE 8u
#define D3D10_DDI_BIND_RENDER_TARGET 32u
#define D3D10_DDI_BIND_PRESENT 128u
#define D3D10_DDI_RESOURCE_MISC_SHARED 2u
#define D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT 8u
#define D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE 0x20000u
struct Mip {unsigned TexelWidth,TexelHeight,TexelDepth;};
struct Primary {struct {struct {unsigned Numerator,Denominator;} RefreshRate;} ModeDesc;};
struct D3D11DDIARG_CREATERESOURCE {
 Mip *pMipInfoList; unsigned ResourceDimension,Format,MipLevels,ArraySize,BindFlags;
 unsigned Usage,MapFlags,MiscFlags; struct {unsigned Count,Quality;} SampleDesc;
 void *pInitialDataUP; Primary *pPrimaryDesc;
};
#include "direct_flip_contract.h"
/* ACTUAL_DESCRIBE */
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define DXGI_ERROR_DEVICE_HUNG ((HRESULT)0x887a0006u)
#define SUCCEEDED(x) ((x)>=0)
struct AGX_D3D12_BUFFER {void *Resource; unsigned long long GpuVa,Bytes;};
struct AGX_D3D12_OWNER {pipe_context *Context; unsigned PendingCopies; pipe_screen *Screen; pipe_resource *UploadStaging,*DownloadStaging;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER*) {}};
static void AgxD3d12OwnerRecordError(AGX_D3D12_OWNER *, HRESULT) {} // receipt sink
/* ACTUAL_COPY_RECORD */
static bool reserved_texture(pipe_resource*) {return false;}
static HRESULT copy_reserved_texture(AGX_D3D12_OWNER*,const AGX_D3D12_TEXTURE_COPY*) {return E_INVALIDARG;}
static bool compressed_texture(AGX_D3D12_OWNER*,pipe_resource*) {return false;}
static void AgxWin32AsahiResourceRelease(pipe_resource **r) {*r=nullptr;}
static void receipt(const char*,HRESULT,UINT,UINT,UINT) {}
static void receipt(const char*,HRESULT,UINT,UINT,UINT,UINT,UINT) {}
using LONG=long;
static unsigned GetEnvironmentVariableA(const char *name,char *out,unsigned size) {
 const char *v=getenv(name);if(!v)return 0;const unsigned n=static_cast<unsigned>(strlen(v));
 if(n<size){memcpy(out,v,n+1);} return n;
}
static LONG InterlockedExchange(volatile LONG *target,LONG value) {const LONG old=*target;*target=value;return old;}
struct Storage {
 pipe_resource resource={};
 std::shared_ptr<std::vector<unsigned char>> allocation;
 std::vector<unsigned char> shadow;
 unsigned stride;
 pipe_transfer transfer={};
};
static std::vector<std::unique_ptr<Storage>> storage;
static unsigned shared_maps, copies, map_failure, staging_usage;
static bool create_failure;
static Storage &memory(pipe_resource *r) {
 for(auto &s:storage) if(&s->resource==r)return *s;
 assert(false);return *storage[0];
}
static Storage &make_storage(unsigned width,unsigned height,unsigned stride,unsigned bind,pipe_format format,
                             std::shared_ptr<std::vector<unsigned char>> allocation={}) {
 auto s=std::make_unique<Storage>();s->stride=stride;
 s->resource.target=PIPE_TEXTURE_2D;s->resource.format=format;s->resource.width0=width;s->resource.height0=height;
 s->resource.depth0=s->resource.array_size=s->resource.nr_samples=s->resource.nr_storage_samples=1;
 s->resource.bind=bind;
 s->allocation=allocation?allocation:std::make_shared<std::vector<unsigned char>>(stride*height,0);
 auto &result=*s;storage.push_back(std::move(s));return result;
}
static pipe_resource *create_staging(pipe_screen*,const pipe_resource *p) {
 if(create_failure)return nullptr;
 return &make_storage(p->width0,p->height0,p->width0*4,p->bind,p->format).resource;
}
static void *texture_map(pipe_context*,pipe_resource *r,unsigned level,unsigned usage,const pipe_box *box,pipe_transfer **t) {
 assert(level==0);auto &s=memory(r);s.transfer.stride=s.stride;s.transfer.layer_stride=s.stride*r->height0;
 *t=map_failure==2?nullptr:&s.transfer;
 if(map_failure==1)return nullptr;
 auto *bytes=s.allocation.get();
 if(r->bind&PIPE_BIND_SHARED) {
  ++shared_maps;
  if(s.shadow.empty())s.shadow=*s.allocation; // agx_bo_map caches the native map
  bytes=&s.shadow;
 } else staging_usage=usage; // the owner's cached upload staging
 return bytes->data()+box->y*s.stride+box->x*4;
}
static void *buffer_map(pipe_context*,pipe_resource *r,unsigned,unsigned,const pipe_box *box,pipe_transfer **t) {
 auto &s=memory(r);*t=map_failure==4?nullptr:&s.transfer;
 return map_failure==3?nullptr:s.allocation->data()+box->x;
}
static void unmap(pipe_context*,pipe_transfer*) {} // transfer unmap does not unmap the BO
static void copy_region(pipe_context*,pipe_resource *dst,unsigned dl,unsigned x,unsigned y,unsigned z,
                        pipe_resource *src,unsigned sl,const pipe_box *box) {
 assert(!dl && !sl && !z && !box->z && box->depth==1);++copies;
 auto &d=memory(dst);auto &s=memory(src);
 for(int row=0;row<box->height;++row)
  memcpy(d.allocation->data()+(y+row)*d.stride+x*4,
         s.allocation->data()+(box->y+row)*s.stride+box->x*4,box->width*4);
}
/* ACTUAL_OWNER_COPY */
int main() {
 pipe_context pipe={};pipe.texture_map=texture_map;pipe.buffer_map=buffer_map;
 pipe.texture_unmap=unmap;pipe.buffer_unmap=unmap;pipe.resource_copy_region=copy_region;
 pipe_screen screen={};screen.resource_create=create_staging;
 for(unsigned format:{28u,87u,91u})for(unsigned width:{37u,17u,65u}) {
  // The actual serialized resource/allocation contract crosses an unaligned byte buffer.
  Mip mip={width,29,1};D3D11DDIARG_CREATERESOURCE create={};
  create.pMipInfoList=&mip;create.ResourceDimension=3;create.Format=format;
  create.MipLevels=create.ArraySize=create.SampleDesc.Count=1;create.BindFlags=32|8|128;
  const auto *CreateResource=&create;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE description={};
  assert(AdmissionUmdDescribePrimary(CreateResource,&description));
  ADMISSION_PRESENT_RESOURCE_DATA resourceData={};const UINT MetadataFlags=0;
  const UINT CreateFlags=0;ADMISSION_UMD_PRIMARY_MAPPING primary={};
  struct {const void *pPrivateDriverData;unsigned PrivateDriverDataSize;} allocate={},allocationInfo={};
/* ACTUAL_SERIALIZE */
  const auto &a=description.Allocation;const auto &r=resourceData;
  unsigned char aw[sizeof(a)+1],rw[sizeof(r)+1];
  memcpy(aw+1,allocationInfo.pPrivateDriverData,allocationInfo.PrivateDriverDataSize);
  memcpy(rw+1,allocate.pPrivateDriverData,allocate.PrivateDriverDataSize);
  AgxD3d12SharedDescription decoded={};
  assert(AgxD3d12DecodeShared(rw+1,sizeof(r),aw+1,sizeof(a),&decoded));
  const auto pf=format==28?PIPE_FORMAT_R8G8B8A8_UNORM:PIPE_FORMAT_B8G8R8A8_UNORM;
  auto &producer=make_storage(width,29,a.Pitch,PIPE_BIND_SHARED,pf);
  auto &consumer=make_storage(decoded.Width,decoded.Height,decoded.Pitch,PIPE_BIND_SHARED,pf,producer.allocation);
  assert(&producer.resource!=&consumer.resource);
  auto &upload=make_storage(4096,1,16384,0,pf);upload.resource.target=PIPE_BUFFER;
  auto &readback=make_storage(4096,1,16384,0,pf);readback.resource.target=PIPE_BUFFER;
  AGX_D3D12_BUFFER src={&producer.resource,0,a.Size},opened={&consumer.resource,0,decoded.Bytes};
  AGX_D3D12_BUFFER up={&upload.resource,0,16384},down={&readback.resource,0,16384};
  AGX_D3D12_OWNER owner={&pipe,0,&screen},reader={&pipe,0,&screen};
  AGX_D3D12_TEXTURE_COPY c={};c.Texture=&src;c.Buffer=&up;c.Width=width;c.Height=29;c.Depth=1;c.Pitch=512;c.Offset=128;
  for(unsigned pass=0;pass<2;++pass) {
   // Second write is a subrect; untouched texels and row padding must survive.
   if(pass){c.X=1;c.Y=2;c.Width=width-2;c.Height=25;}
   std::vector<unsigned char> expected=*producer.allocation;
   for(unsigned y=0;y<c.Height;++y)for(unsigned x=0;x<c.Width*4;++x) {
    const auto v=static_cast<unsigned char>(x*7+y*11+pass*83);
    (*upload.allocation)[c.Offset+y*c.Pitch+x]=v;
    expected[(c.Y+y)*a.Pitch+c.X*4+x]=v;
   }
   assert(AgxD3d12OwnerCopyTexture(&owner,&c)==S_OK);
   // A completed empty batch cannot publish a private CPU shadow. Require the
   // translated copy to reach shared backing, and to participate in submission.
   unsigned bad=0;for(unsigned y=0;y<29;++y)for(unsigned x=0;x<width*4;++x)
    bad+=(*consumer.allocation)[y*decoded.Pitch+x]!=expected[y*a.Pitch+x];
   printf("shared width=%u format=%u pass=%u mismatches=%u pending=%u\n",width,format,pass,bad,owner.PendingCopies);fflush(stdout);
   assert(!bad && owner.PendingCopies==pass+1 && !shared_maps);
   assert(*producer.allocation==expected);
   // RC85: the upload rewrites the box it reads back; its staging (256 KiB here) maps discard
   // unless APPLE_AGX_D3D12_STAGED_DISCARD=0
   assert((staging_usage&PIPE_MAP_WRITE) &&
          !!(staging_usage&PIPE_MAP_DISCARD_WHOLE_RESOURCE)==!getenv("APPLE_AGX_D3D12_STAGED_DISCARD"));
   std::fill(readback.allocation->begin(),readback.allocation->end(),0xcd);
   auto d=c;d.Texture=&opened;d.Buffer=&down;d.ToBuffer=1;d.X=d.Y=0;d.Width=width;d.Height=29;
   assert(AgxD3d12OwnerCopyTexture(&reader,&d)==S_OK);
   for(unsigned y=0;y<29;++y) {
    assert(!memcmp(readback.allocation->data()+d.Offset+y*d.Pitch,expected.data()+y*a.Pitch,width*4));
    assert((*readback.allocation)[d.Offset+y*d.Pitch-1]==0xcd);
    assert((*readback.allocation)[d.Offset+y*d.Pitch+width*4]==0xcd);
   }
  }
  for(map_failure=1;map_failure<=4;++map_failure) {
   const auto expected=*producer.allocation;const auto count=copies,pending=owner.PendingCopies;
   assert(AgxD3d12OwnerCopyTexture(&owner,&c)==DXGI_ERROR_DEVICE_HUNG);
   assert(*producer.allocation==expected && copies==count && owner.PendingCopies==pending);
  }
  map_failure=0;
  {
   // RC85: above Asahi's 6 MiB copy-shadow bound the staging keeps the plain (waiting) map
   auto &large=make_storage(2048,1024,2048*4,PIPE_BIND_SAMPLER_VIEW,pf);
   owner.UploadStaging=&large.resource;staging_usage=0;
   assert(AgxD3d12OwnerCopyTexture(&owner,&c)==S_OK);
   assert((staging_usage&PIPE_MAP_WRITE) && !(staging_usage&PIPE_MAP_DISCARD_WHOLE_RESOURCE));
  }
  owner.UploadStaging=nullptr;create_failure=true;
  assert(AgxD3d12OwnerCopyTexture(&owner,&c)==E_OUTOFMEMORY);create_failure=false;
 }
 puts("PASS shared private-data/layout/content round trip; two writes, subrects, map failures");
}
