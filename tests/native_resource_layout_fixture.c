/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_range.h"
#include "util/u_inlines.h"
#include "util/bitset.h"
#include "asahi/layout/layout.h"
#include "agx_win32_asahi_layout.h"
#define AGX_DBG_NOCOMPRESS (1u<<0)
#define AGX_DBG_NOWC (1u<<1)
#define AGX_BO_WRITEBACK (1u<<0)
#define AGX_BO_SHAREABLE (1u<<1)
struct agx_device {unsigned debug;};
struct agx_screen {struct pipe_screen base;struct agx_device dev;};
static unsigned device_accesses,explicit_cases;
static struct agx_device *agx_device(struct pipe_screen *screen) {++device_accesses;assert(screen);return &((struct agx_screen *)screen)->dev;}
struct agx_bo {uint64_t size,rounded_size;};
static int native_bo_shape(size_t *request,unsigned *requested_alignment) {
   struct backend_ready {bool GpuvaReady;} state={true};
   struct backend_ready *b=&state;
   size_t bytes=*request;unsigned align=*requested_alignment;
/* ACTUAL_BO_SHAPE */
   *request=bytes;*requested_alignment=align;return 1;
}
/* ACTUAL_RESOURCE_STRUCT */
static unsigned bo_calls,case_count,compressed_count,rejected_count,linear_count,volume_count,padded_count;
static struct agx_bo *agx_bo_create(struct agx_device *d,uint64_t size,unsigned align,unsigned flags,const char *label) {
   (void)d;(void)align;(void)flags;(void)label;++bo_calls;
   size_t physical=size;
   assert(native_bo_shape(&physical,&align));
   struct agx_bo *bo=calloc(1,sizeof(*bo));if(bo) {bo->size=size;bo->rounded_size=physical;}return bo;
}
static uint64_t policy_debug_hash;
static unsigned policy_debug_count;
static void policy_debug_message(const void *resource,const char *message) {
   (void)resource;++policy_debug_count;
   while(*message) policy_debug_hash=policy_debug_hash*33u+(unsigned char)*message++;
}
#define rsrc_debug(...) policy_debug_message(__VA_ARGS__)
#define agx_resource_debug(...) ((void)0)
static bool drm_find_modifier(uint64_t modifier,const uint64_t *list,int count) {
   for(int i=0;i<count;++i) if(list[i]==modifier) return true;return false;
}
/* ACTUAL_SOURCE */
static void check(struct agx_screen *screen,struct pipe_resource *templ,const uint64_t *mods,int count) {
   ++case_count;
   AGX_WIN32_ASAHI_RESOURCE_LAYOUT query;
   struct pipe_resource saved=*templ;
   unsigned before=bo_calls;
   memset(&query,0xff,sizeof(query));
   int queried=AgxWin32AsahiQueryResourceLayout(&screen->base,templ,mods,count,&query);
   assert(bo_calls==before);assert(!memcmp(templ,&saved,sizeof(saved)));
   policy_debug_hash=0;policy_debug_count=0;
   struct agx_resource *reference=(struct agx_resource *)reference_create(&screen->base,templ,mods,count);
   uint64_t reference_debug=policy_debug_hash;unsigned reference_debug_count=policy_debug_count;
   policy_debug_hash=0;policy_debug_count=0;
   struct agx_resource *actual=(struct agx_resource *)agx_resource_create_with_modifiers(&screen->base,templ,mods,count);
   assert(reference_debug==policy_debug_hash && reference_debug_count==policy_debug_count);
   assert(!!reference==!!actual && queried==!!reference);
   if(!reference) {++rejected_count;AGX_WIN32_ASAHI_RESOURCE_LAYOUT zero={0};assert(!memcmp(&query,&zero,sizeof(query)));return;}
   compressed_count+=actual->layout.compressed;
   linear_count+=actual->layout.tiling==AIL_TILING_LINEAR;
   volume_count+=actual->layout.mipmapped_z;
   assert(query.Tiling==(uint32_t)actual->layout.tiling);
   uint32_t flags=(actual->layout.mipmapped_z?AGX_WIN32_ASAHI_LAYOUT_MIPMAPPED_Z:0) |
      (actual->layout.compressed?AGX_WIN32_ASAHI_LAYOUT_COMPRESSED:0) |
      (actual->layout.page_aligned_layers?AGX_WIN32_ASAHI_LAYOUT_PAGE_ALIGNED_LAYERS:0) |
      (actual->layout.writeable_image?AGX_WIN32_ASAHI_LAYOUT_WRITEABLE_IMAGE:0) |
      (actual->layout.renderable?AGX_WIN32_ASAHI_LAYOUT_RENDERABLE:0);
   assert(query.Flags==flags);
   assert(query.SizeBytes==actual->bo->size && query.SizeBytes==reference->bo->size);
   size_t rounded=query.SizeBytes;unsigned alignment=0;
   assert(native_bo_shape(&rounded,&alignment));
   assert(rounded==actual->bo->rounded_size && rounded==reference->bo->rounded_size);
   padded_count+=rounded!=query.SizeBytes;
   assert(!memcmp(&actual->layout,&reference->layout,sizeof(actual->layout)));
   assert(query.Modifier==actual->modifier && query.LayerStrideBytes==actual->layout.layer_stride_B);
   assert(query.MetadataOffsetBytes==actual->layout.metadata_offset_B);
   assert(query.CompressionLayerStrideBytes==actual->layout.compression_layer_stride_B);
   assert(query.SparseTableSizeBytes==actual->layout.sparse_table_size_B);
   assert(query.AllocatedLevels==actual->layout.levels && query.RequestedLevels==templ->last_level+1);
   assert(query.EffectiveBind==actual->base.bind);
   for(unsigned i=0;i<AGX_WIN32_ASAHI_LAYOUT_LEVELS;i++) {
      assert(query.LevelOffsets[i]==actual->layout.level_offsets_B[i]);
      assert(query.CompressedLevelOffsets[i]==actual->layout.level_offsets_compressed_B[i]);
   }
   free(reference->bo);free(reference);free(actual->bo);free(actual);
}
static void check_explicit(struct agx_screen *screen,const struct pipe_resource *t,uint64_t modifier) {
   AGX_WIN32_ASAHI_RESOURCE_LAYOUT expected,query;
   int regular=AgxWin32AsahiQueryResourceLayout(&screen->base,t,&modifier,1,&expected);
   unsigned before_bo=bo_calls,before_device=device_accesses,before_debug=policy_debug_count;
   uint64_t before_debug_hash=policy_debug_hash;
   memset(&query,0xff,sizeof(query));
   int pure=agx_query_uncompressed_resource_layout(t,modifier,&query);
   assert(bo_calls==before_bo && device_accesses==before_device);
   assert(policy_debug_count==before_debug && policy_debug_hash==before_debug_hash);
   if(modifier==DRM_FORMAT_MOD_APPLE_GPU_TILED && regular) {
      assert(pure && !memcmp(&expected,&query,sizeof(query)));
      AGX_WIN32_ASAHI_RESOURCE_LAYOUT public_query;
      assert(AgxWin32AsahiQueryUncompressedResourceLayout(t,&public_query));
      assert(!memcmp(&public_query,&query,sizeof(query)));
      assert(bo_calls==before_bo && device_accesses==before_device);
   } else {
      AGX_WIN32_ASAHI_RESOURCE_LAYOUT zero={0};assert(!pure && !memcmp(&query,&zero,sizeof(query)));
   }
   ++explicit_cases;
}
int main(int argc,char **argv) {
   (void)argv;
   struct agx_screen screen={0};struct pipe_resource t={0};
   t.target=PIPE_TEXTURE_2D_ARRAY;t.format=PIPE_FORMAT_R8G8B8A8_UNORM;
   t.width0=t.height0=4;t.depth0=1;t.array_size=64;t.last_level=1;t.bind=PIPE_BIND_SAMPLER_VIEW;
   check(&screen,&t,NULL,0); /* exact existing metadata-counterexample shape */
   t.target=PIPE_TEXTURE_3D;t.depth0=64;t.array_size=1;
   check(&screen,&t,NULL,0); /* depth-dominant expanded mip chain */
   t.last_level=0;check(&screen,&t,NULL,0);
   const uint64_t uncompressed[]={DRM_FORMAT_MOD_APPLE_GPU_TILED};
   const uint64_t compressed[]={DRM_FORMAT_MOD_APPLE_GPU_TILED_COMPRESSED};
   const uint64_t invalid[]={DRM_FORMAT_MOD_INVALID};
   t.width0=128;t.height0=32;t.depth0=8;t.last_level=2;
   check(&screen,&t,NULL,0);
   check(&screen,&t,uncompressed,1);
   check(&screen,&t,compressed,1);
   check(&screen,&t,invalid,1);
   check(&screen,&t,uncompressed,0);
   screen.dev.debug=AGX_DBG_NOCOMPRESS;
   check(&screen,&t,NULL,0);
   check(&screen,&t,compressed,1); /* debug policy must match native refusal */
   screen.dev.debug=0;
   t.target=PIPE_TEXTURE_2D_ARRAY;t.depth0=1;t.array_size=6;t.width0=96;t.height0=37;t.last_level=3;
   check(&screen,&t,NULL,0);
   check(&screen,&t,uncompressed,1);
   t.target=PIPE_TEXTURE_2D;t.array_size=1;t.last_level=0;
   t.usage=PIPE_USAGE_STAGING;check(&screen,&t,NULL,0);
   t.usage=PIPE_USAGE_DEFAULT;t.bind|=PIPE_BIND_SHARED;check(&screen,&t,NULL,0);
   t.bind=PIPE_BIND_SAMPLER_VIEW;t.width0=16384;t.height0=16384;t.array_size=8;
   t.target=PIPE_TEXTURE_2D_ARRAY;check(&screen,&t,uncompressed,1); /* native >=4GiB gate, no BO */
   t.target=PIPE_BUFFER;t.width0=257;t.height0=t.depth0=t.array_size=1;
   t.format=PIPE_FORMAT_R8_UINT;t.bind=PIPE_BIND_SHADER_BUFFER;
   check(&screen,&t,NULL,0);
   if(argc>1) {
      struct pipe_resource volume={0};volume.target=PIPE_TEXTURE_3D;volume.format=PIPE_FORMAT_R8G8B8A8_UNORM;
      volume.width0=128;volume.height0=32;volume.depth0=8;volume.array_size=1;volume.last_level=2;volume.bind=PIPE_BIND_SAMPLER_VIEW;
      for(unsigned debug=0;debug<2;++debug) {
         screen.dev.debug=debug?AGX_DBG_NOCOMPRESS:0;
         for(unsigned dim=0;dim<2;++dim) {
            volume.target=dim?PIPE_TEXTURE_3D:PIPE_TEXTURE_2D_ARRAY;
            volume.depth0=dim?8:1;volume.array_size=dim?1:8;
            check(&screen,&volume,uncompressed,1);
            check_explicit(&screen,&volume,uncompressed[0]);
            check_explicit(&screen,&volume,compressed[0]);
            check_explicit(&screen,&volume,invalid[0]);
            check_explicit(&screen,&volume,DRM_FORMAT_MOD_LINEAR);
         }
      }
      screen.dev.debug=0;volume.target=PIPE_TEXTURE_2D_ARRAY;volume.width0=volume.height0=16384;volume.depth0=1;volume.array_size=8;volume.last_level=0;
      check_explicit(&screen,&volume,uncompressed[0]);
      check(&screen,&t,uncompressed,1);check_explicit(&screen,&t,uncompressed[0]); /* buffer cannot be tiled */
      volume.width0=volume.height0=8;volume.array_size=1;volume.bind|=PIPE_BIND_LINEAR;
      check(&screen,&volume,uncompressed,1);check_explicit(&screen,&volume,uncompressed[0]);
      assert(explicit_cases==19);
   }
   AGX_WIN32_ASAHI_RESOURCE_LAYOUT result;
   unsigned before=bo_calls;
   memset(&result,0xff,sizeof(result));
   assert(!AgxWin32AsahiQueryResourceLayout(NULL,&t,NULL,0,&result));
   AGX_WIN32_ASAHI_RESOURCE_LAYOUT zero={0};assert(!memcmp(&result,&zero,sizeof(result)));
   assert(!AgxWin32AsahiQueryResourceLayout(&screen.base,NULL,NULL,0,&result));
   assert(!AgxWin32AsahiQueryResourceLayout(&screen.base,&t,NULL,1,&result));
   assert(!AgxWin32AsahiQueryResourceLayout(&screen.base,&t,NULL,-1,&result));
   assert(!AgxWin32AsahiQueryResourceLayout(&screen.base,&t,NULL,0,NULL));
   assert(bo_calls==before);
   unsigned before_device=device_accesses;
   assert(!AgxWin32AsahiQueryUncompressedResourceLayout(NULL,&result));
   assert(!memcmp(&result,&zero,sizeof(result)));
   assert(!AgxWin32AsahiQueryUncompressedResourceLayout(&t,NULL));
   assert(bo_calls==before && device_accesses==before_device);
   assert(compressed_count>0 && linear_count>0 && volume_count>0 && rejected_count>0 && padded_count>0);
   printf("PASS native layout cases=%u compressed=%u linear=%u volume=%u rejected=%u allocator-padded=%u query-bo=0 scratch=%zu result=%zu screen-free=%u\n",
      case_count,compressed_count,linear_count,volume_count,rejected_count,padded_count,sizeof(struct agx_resource),sizeof(result),explicit_cases);
   return 0;
}
