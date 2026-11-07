/* SPDX-License-Identifier: MIT
 * Execute projected depth transfers. Only BO allocation, scheduling and GPU
 * execution are simulated; formats, AIL, staging and transfer bodies are real.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_range.h"
#include "util/bitset.h"
#include "asahi/layout/layout.h"

void debug_describe_resource(char *buf, const struct pipe_resource *r) {
   (void)r;buf[0]=0;
}

struct agx_resource {
   struct pipe_resource base;
   struct ail_layout layout;
   uint64_t modifier;
   void *bo;
   BITSET_DECLARE(data_valid, PIPE_MAX_TEXTURE_LEVELS);
   struct util_range valid_buffer_range;
};
struct agx_transfer {
   struct pipe_transfer base;
   struct { struct pipe_resource *rsrc; struct pipe_box box; } staging;
   void *map;
};
struct agx_context { struct pipe_context base; bool any_faults; };
#define agx_context(p) ((struct agx_context *)(p))
#define agx_resource(p) ((struct agx_resource *)(p))
#define agx_transfer(p) ((struct agx_transfer *)(p))
static struct agx_resource gpu;
static unsigned maps, blits, syncs, flushes, live, faults;
static bool fail_create, fail_map, pending_read;
static enum pipe_format want_staging;
static void AgxWin32AsahiMarkContextFault(struct agx_context *c, unsigned site) { assert(site); ++faults; c->any_faults=true; }
static void agx_prepare_for_map(struct agx_context *c, struct agx_resource *r, unsigned l, unsigned u, const struct pipe_box *b, bool staging) {
   (void)c;(void)r;(void)l;(void)u;(void)b;assert(staging);
}
static bool agx_resource_valid(struct agx_resource *r, unsigned level) { return BITSET_TEST(r->data_valid, level); }
static void *agx_bo_map(void *bo) { assert(bo != gpu.bo && !pending_read); ++maps; return fail_map ? NULL : bo; }
static void *agx_map_texture_cpu(struct agx_resource *r, unsigned l, unsigned z) {
   (void)r;(void)l;(void)z;assert(!"direct depth CPU map");return NULL;
}
static void agx_sync_writer(struct agx_context *c, struct agx_resource *r, const char *s) {
   (void)c;(void)s;assert(r != &gpu && pending_read);pending_read=false;++syncs;
}
static void agx_flush_readers(struct agx_context *c, struct agx_resource *r, const char *s) {
   (void)c;(void)s;assert(r != &gpu);++flushes;
}
static void destroy(struct pipe_screen *s, struct pipe_resource *r) {
   (void)s;assert(r != &gpu.base);free(agx_resource(r)->bo);free(r);--live;
}
static struct pipe_resource *create(struct pipe_screen *s, const struct pipe_resource *t) {
   assert(!(t->flags & PIPE_RESOURCE_FLAG_UNMAPPABLE));
   assert(t->format == want_staging && t->usage == PIPE_USAGE_STAGING);
   assert(t->bind == (PIPE_BIND_LINEAR | PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW));
   assert(t->target == PIPE_TEXTURE_2D_ARRAY && !t->last_level && t->array_size == 1);
   assert(t->width0 == 3 && t->height0 == 2);
   if (fail_create) return NULL;
   struct agx_resource *r=calloc(1,sizeof(*r));assert(r);r->base=*t;r->base.screen=s;
   pipe_reference_init(&r->base.reference,1);
   r->layout=(struct ail_layout){.width_px=t->width0,.height_px=t->height0,
      .depth_px=t->array_size,.levels=1,.sample_count_sa=1,.format=t->format,.tiling=AIL_TILING_LINEAR};
   ail_make_miptree(&r->layout);r->bo=calloc(1,r->layout.size_B);assert(r->bo);++live;
   return &r->base;
}
/* GPU copy callback uses a linear byte oracle for the GPU's opaque storage.
 * No CPU BO map is involved. The caller's actual blit formats/boxes select it.
 */
static size_t offset(struct agx_resource *r, unsigned level, unsigned x, unsigned y, unsigned z) {
   unsigned bs=util_format_get_blocksize(r->layout.format);
   if (r==&gpu) return ((z*u_minify(r->base.height0,level)+y)*u_minify(r->base.width0,level)+x)*bs;
   assert(!level);return z*r->layout.layer_stride_B+y*ail_get_linear_stride_B(&r->layout,0)+x*bs;
}
static void agx_blit(struct pipe_context *p, const struct pipe_blit_info *b) {
   (void)p;assert(b->src.format==want_staging && b->dst.format==want_staging);
   assert(b->mask==PIPE_MASK_RGBA && b->filter==PIPE_TEX_FILTER_NEAREST);
   assert(b->src.box.width==3 && b->src.box.height==2 && b->src.box.depth==1);
   assert(b->dst.box.width==3 && b->dst.box.height==2 && b->dst.box.depth==1);
   struct agx_resource *src=agx_resource(b->src.resource), *dst=agx_resource(b->dst.resource);
   const struct pipe_box *g=src==&gpu?&b->src.box:&b->dst.box;
   const struct pipe_box *s=src==&gpu?&b->dst.box:&b->src.box;
   assert(g->x==2 && g->y==3 && g->z==1 && !s->x && !s->y && !s->z);
   for(unsigned y=0;y<2;y++) {
      size_t so=offset(src,b->src.level,b->src.box.x,b->src.box.y+y,b->src.box.z);
      size_t dest=offset(dst,b->dst.level,b->dst.box.x,b->dst.box.y+y,b->dst.box.z);
      memcpy((char *)dst->bo+dest,(char *)src->bo+so,3*util_format_get_blocksize(want_staging));
   }
   if(src==&gpu) pending_read=true;
   ++blits;
}
/* ACTUAL_TRANSFER_SOURCE */
int main(void) {
   struct pipe_screen screen={.resource_create=create,.resource_destroy=destroy};
   struct agx_context ctx={.base.screen=&screen};
   const enum pipe_format formats[]={PIPE_FORMAT_Z16_UNORM,PIPE_FORMAT_Z32_FLOAT};
   const enum pipe_format colors[]={PIPE_FORMAT_R16_UNORM,PIPE_FORMAT_R32_FLOAT};
   for(unsigned f=0;f<2;f++) for(unsigned compressed=0;compressed<2;compressed++) for(unsigned level=0;level<=3;level+=3) {
      memset(&gpu,0,sizeof(gpu));want_staging=colors[f];
      gpu.base=(struct pipe_resource){.screen=&screen,.target=PIPE_TEXTURE_2D_ARRAY,
         .format=formats[f],.width0=64,.height0=64,.depth0=1,.array_size=2,.last_level=3,
         .bind=PIPE_BIND_DEPTH_STENCIL|PIPE_BIND_SAMPLER_VIEW,.flags=PIPE_RESOURCE_FLAG_UNMAPPABLE};
      pipe_reference_init(&gpu.base.reference,1);
      gpu.layout=(struct ail_layout){.width_px=64,.height_px=64,.depth_px=2,.levels=4,
         .sample_count_sa=1,.format=formats[f],.tiling=AIL_TILING_GPU,.renderable=true,.compressed=compressed};
      ail_make_miptree(&gpu.layout);gpu.modifier=1;BITSET_SET(gpu.data_valid,level);
      /* Distinguish compressed base, uncompressed base, and small mip tail. */
      assert(ail_is_level_logically_compressed(&gpu.layout,level)==(compressed && !level));
      unsigned bs=util_format_get_blocksize(formats[f]);size_t bytes=64*64*2*bs;
      gpu.bo=malloc(bytes);assert(gpu.bo);for(size_t i=0;i<bytes;i++)((unsigned char *)gpu.bo)[i]=(i*13+7)&255;
      unsigned char *expected=malloc(bytes);assert(expected);memcpy(expected,gpu.bo,bytes);
      struct pipe_box box={.x=2,.y=3,.z=1,.width=3,.height=2,.depth=1};struct pipe_transfer *t=NULL;
      ctx.any_faults=false;maps=blits=syncs=flushes=faults=0;
      unsigned char *map=agx_transfer_map(&ctx.base,&gpu.base,level,PIPE_MAP_READ|PIPE_MAP_WRITE,&box,&t);
      assert(map && t && maps==1 && blits==1 && syncs==1 && !flushes && live==1);
      for(unsigned y=0;y<2;y++) for(unsigned x=0;x<3*bs;x++) {
         size_t pos=offset(&gpu,level,2,3+y,1)+x;
         assert(map[y*t->stride+x]==expected[pos]);map[y*t->stride+x]^=0xa5;expected[pos]^=0xa5;
      }
      agx_transfer_unmap(&ctx.base,t);
      assert(blits==2 && flushes==1 && !live && gpu.base.reference.count==1);
      assert(!memcmp(expected,gpu.bo,bytes));
      /* Failed staging allocation/map must not publish a transfer or leak. */
      for(unsigned fail=0;fail<2;fail++) {
         ctx.any_faults=false;fail_create=fail==0;fail_map=fail==1;t=(void *)1;
         assert(!agx_transfer_map(&ctx.base,&gpu.base,level,PIPE_MAP_READ,&box,&t));
         assert(!t && !live && gpu.base.reference.count==1 && faults==fail+1);
      }
      ctx.any_faults=false;fail_create=fail_map=false;
      unsigned before=maps;
      const unsigned modes[]={PIPE_MAP_DIRECTLY,PIPE_MAP_PERSISTENT,PIPE_MAP_COHERENT};
      for(unsigned i=0;i<3;i++) {
         t=(void *)1;assert(!agx_transfer_map(&ctx.base,&gpu.base,level,PIPE_MAP_READ|modes[i],&box,&t));
         assert(!t && maps==before && !live);
      }
      free(expected);free(gpu.bo);
   }
   puts("gpu-only Z16/Z32 depth staging: PASS");
}
