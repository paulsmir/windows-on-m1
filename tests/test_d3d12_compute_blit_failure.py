"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Compute blits used NULL sampler/view/shader/key allocation results and failed to
restore saved state on OOM. The real projected save/blit/restore bodies run under
ASan/UBSan with each allocation failing in turn. Saved references must return to
their original counts; only fully built shaders may enter the cache or launch.
"""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import project

ROOT=Path(__file__).resolve().parents[1]
SCRIPT=ROOT/'drivers/apple-agx/mesa/scripts/native-asahi-blit-failure.py'
SOURCE='src/gallium/drivers/asahi/agx_blit.c'


def function(text,name):
    start=text.index('\n'+name+'(')
    decl=text.rfind('\n',0,start-1)+1
    return text[decl:text.index('\n}',start)+2]


PRELUDE=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define PIPE_IMAGE_ACCESS_WRITE 1
#define PIPE_IMAGE_ACCESS_DRIVER_INTERNAL 2
#define PIPE_TEX_WRAP_CLAMP_TO_EDGE 1
#define PIPE_FUNC_ALWAYS 0
#define PIPE_TEXTURE_2D_ARRAY 1
#define PIPE_TEXTURE_2D 2
#define PIPE_SWIZZLE_X 0
#define PIPE_SWIZZLE_Y 1
#define PIPE_SWIZZLE_Z 2
#define PIPE_SWIZZLE_W 3
#define MESA_SHADER_COMPUTE 0
#define BITFIELD_BIT(i) (1u<<(i))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define align(a,b) (((a)+(b)-1)&~((b)-1))
static unsigned nth,attempts,live,launches;
static void *allocate(size_t size) {if(++attempts==nth)return NULL;void *p=calloc(1,size);assert(p);++live;return p;}
static void release(void *p){if(p){assert(live);--live;free(p);}}
struct pipe_resource {unsigned refs,format,width0,height0;bool temporary;};
struct pipe_sampler_view {unsigned refs,format,target,swizzle_r,swizzle_g,swizzle_b,swizzle_a;bool temporary;struct {struct {unsigned first_layer,last_layer,first_level,last_level;} tex;} u;};
struct agx_sampler_view {struct pipe_sampler_view base;};
struct pipe_constant_buffer {struct pipe_resource *buffer;unsigned buffer_size,buffer_offset;const void *user_buffer;};
struct pipe_image_view {struct pipe_resource *resource;unsigned access,shader_access,format;struct {struct {unsigned level,first_layer,last_layer;bool single_layer_view;} tex;} u;};
struct pipe_sampler_state {unsigned wrap_s,wrap_t,wrap_r,min_img_filter,mag_img_filter,compare_func;bool seamless_cube_map;float max_lod;};
struct pipe_grid_info {unsigned block[3],last_block[3],grid[3];};
struct pipe_box {unsigned x,y,z,width,height,depth;};
struct pipe_blit_info {struct {struct pipe_resource *resource;unsigned format,level;struct pipe_box box;} src,dst;unsigned filter;};
struct asahi_blit_key {unsigned src_format,dst_format;bool array,aligned;};
struct hash_entry {void *data;};
struct hash_table {struct hash_entry *entry;void *key;};
struct pipe_context {
 void (*set_constant_buffer)(struct pipe_context *,int,unsigned,const struct pipe_constant_buffer *);
 void (*set_shader_images)(struct pipe_context *,int,unsigned,unsigned,unsigned,const struct pipe_image_view *);
 void *(*create_sampler_state)(struct pipe_context *,const struct pipe_sampler_state *);
 void (*bind_sampler_states)(struct pipe_context *,int,unsigned,unsigned,void **);
 struct pipe_sampler_view *(*create_sampler_view)(struct pipe_context *,struct pipe_resource *,const struct pipe_sampler_view *);
 void (*set_sampler_views)(struct pipe_context *,int,unsigned,unsigned,unsigned,struct pipe_sampler_view **);
 void (*sampler_view_release)(struct pipe_context *,struct pipe_sampler_view *);
 void (*bind_compute_state)(struct pipe_context *,void *);
 void (*delete_compute_state)(struct pipe_context *,void *);
 void (*launch_grid)(struct pipe_context *,const struct pipe_grid_info *);
};
struct asahi_blitter {
 bool active,has_saved_image;
 struct pipe_constant_buffer saved_cb;struct pipe_image_view saved_image;
 struct pipe_sampler_view *saved_sampler_view;
 unsigned saved_num_sampler_states;void *saved_sampler_states[4],*saved_cs,*sampler[2];
 struct hash_table *blit_cs;
};
struct agx_stage {struct pipe_constant_buffer cb[1];unsigned image_mask,sampler_count;struct pipe_image_view images[1];struct agx_sampler_view *textures[1];void *samplers[4],*shader;};
struct agx_context {struct pipe_context base;struct asahi_blitter compute_blitter;struct agx_stage stage[1];bool any_faults;};
#define agx_context(p) ((struct agx_context *)(p))
static void pipe_resource_reference(struct pipe_resource **dst,struct pipe_resource *src) {
 if(src)++src->refs;
 if(*dst){assert((*dst)->refs);if(!--(*dst)->refs&&(*dst)->temporary)release(*dst);}*dst=src;
}
static void pipe_sampler_view_reference(struct pipe_sampler_view **dst,struct pipe_sampler_view *src) {
 if(src)++src->refs;
 if(*dst){assert((*dst)->refs);if(!--(*dst)->refs&&(*dst)->temporary)release(*dst);}*dst=src;
}
static void set_cb(struct pipe_context *p,int stage,unsigned index,const struct pipe_constant_buffer *cb) {
 (void)stage;(void)index;struct agx_context *c=agx_context(p);struct pipe_constant_buffer *d=&c->stage[0].cb[0];
 struct pipe_resource *saved=d->buffer;pipe_resource_reference(&saved,cb?cb->buffer:NULL);
 if(cb)*d=*cb;else memset(d,0,sizeof(*d));d->buffer=saved;
 if(d->user_buffer&&!d->buffer){d->buffer=allocate(sizeof(*d->buffer));if(!d->buffer)c->any_faults=true;else {d->buffer->refs=1;d->buffer->temporary=true;}}
}
static void set_image(struct pipe_context *p,int stage,unsigned start,unsigned count,unsigned trailing,const struct pipe_image_view *v) {
 (void)stage;(void)start;(void)trailing;struct agx_stage *s=&agx_context(p)->stage[0];
 pipe_resource_reference(&s->images[0].resource,count?v->resource:NULL);s->image_mask=count?1:0;
}
static void *create_sampler(struct pipe_context *p,const struct pipe_sampler_state *s){(void)p;(void)s;return allocate(8);}
static void bind_sampler(struct pipe_context *p,int stage,unsigned start,unsigned count,void **states){(void)stage;(void)start;struct agx_stage *s=&agx_context(p)->stage[0];for(unsigned i=0;i<count;++i)s->samplers[i]=states[i];s->sampler_count=0;for(unsigned i=0;i<4;++i)if(s->samplers[i])s->sampler_count=i+1;}
static struct pipe_sampler_view *create_view(struct pipe_context *p,struct pipe_resource *r,const struct pipe_sampler_view *v){(void)p;(void)r;(void)v;struct pipe_sampler_view *s=allocate(sizeof(*s));if(s){s->refs=1;s->temporary=true;}return s;}
static void set_view(struct pipe_context *p,int stage,unsigned start,unsigned count,unsigned trailing,struct pipe_sampler_view **views){(void)stage;(void)start;(void)trailing;pipe_sampler_view_reference((struct pipe_sampler_view **)&agx_context(p)->stage[0].textures[0],count?views[0]:NULL);}
static void release_view(struct pipe_context *p,struct pipe_sampler_view *v){(void)p;assert(v);pipe_sampler_view_reference(&v,NULL);}
static void bind_cs(struct pipe_context *p,void *s){agx_context(p)->stage[0].shader=s;}
static void delete_cs(struct pipe_context *p,void *s){(void)p;release(s);}
static void launch(struct pipe_context *p,const struct pipe_grid_info *g){(void)g;struct agx_context *c=agx_context(p);assert(!c->any_faults&&c->stage[0].shader&&c->stage[0].textures[0]&&c->stage[0].samplers[0]);++launches;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned site){assert(site);c->any_faults=true;}
static void *asahi_blit_compute_shader(struct pipe_context *p,struct asahi_blit_key *k){(void)p;(void)k;return allocate(8);}
static struct hash_entry *_mesa_hash_table_search(struct hash_table *h,const void *key){assert(h&&key);return h->entry;}
static struct hash_entry *_mesa_hash_table_insert(struct hash_table *h,void *key,void *value){assert(h&&key&&value);struct hash_entry *e=allocate(sizeof(*e));if(e){e->data=value;h->entry=e;h->key=key;}return e;}
static void *ralloc_memdup(void *parent,const void *src,size_t size){assert(parent);void *p=allocate(size);if(p)memcpy(p,src,size);return p;}
static void ralloc_free(void *p){release(p);}
static unsigned u_minify(unsigned n,unsigned level){return n>>level?n>>level:1;}
static bool util_format_is_compressed(unsigned f){(void)f;return false;}
static unsigned util_format_get_nblocksx(unsigned f,unsigned n){(void)f;return n;}
static unsigned util_format_get_nblocksy(unsigned f,unsigned n){(void)f;return n;}
static unsigned fui(float f){unsigned u;memcpy(&u,&f,4);return u;}
static bool asahi_compute_blit_supported(const struct pipe_blit_info *i){(void)i;return true;}
static void u_sampler_view_default_template(struct pipe_sampler_view *v,struct pipe_resource *r,unsigned f){(void)v;(void)r;(void)f;}
'''

MAIN=r'''
int main(void) {
 for(unsigned empty=0;empty<2;++empty)for(nth=0;nth<=7;++nth) {
  attempts=live=launches=0;
  struct agx_context c={0};struct hash_table cache={0};c.compute_blitter.blit_cs=&cache;
  c.base=(struct pipe_context){set_cb,set_image,create_sampler,bind_sampler,create_view,set_view,release_view,bind_cs,delete_cs,launch};
  struct pipe_resource old_cb={1,0,32,32,false},old_image={1,0,32,32,false};
  struct agx_sampler_view old_view={.base={.refs=1}};int old_sampler,old_cs;
  if(!empty) {
   c.stage[0].cb[0].buffer=&old_cb;c.stage[0].images[0].resource=&old_image;c.stage[0].image_mask=1;
   c.stage[0].textures[0]=&old_view;c.stage[0].samplers[0]=&old_sampler;c.stage[0].sampler_count=1;c.stage[0].shader=&old_cs;
  }
  struct pipe_resource src={1,0,32,32,false},dst={1,0,32,32,false};
  struct pipe_blit_info info={.src={&src,0,0,{0,0,0,8,8,1}},.dst={&dst,0,0,{0,0,0,8,8,1}}};
  asahi_compute_blit(&c.base,&info,&c.compute_blitter);
  bool fail=nth>=1&&nth<=6;
  assert(c.any_faults==fail&&launches==(fail?0u:1u)&&!c.compute_blitter.active);
  assert(c.stage[0].cb[0].buffer==(empty?NULL:&old_cb));
  assert(c.stage[0].images[0].resource==(empty?NULL:&old_image));
  assert(c.stage[0].textures[0]==(empty?NULL:&old_view));
  assert(c.stage[0].samplers[0]==(empty?NULL:&old_sampler));
  assert(c.stage[0].shader==(empty?NULL:&old_cs));
  assert(old_cb.refs==1&&old_image.refs==1&&old_view.base.refs==1&&src.refs==1&&dst.refs==1);
  assert(!c.compute_blitter.saved_cb.buffer&&!c.compute_blitter.saved_image.resource&&!c.compute_blitter.saved_sampler_view);
  if(cache.entry){release(cache.entry->data);release(cache.entry);release(cache.key);}
  release(c.compute_blitter.sampler[0]);assert(!live);
 }
}
'''

class ComputeBlitFailure(unittest.TestCase):
    def test_kth_allocation_restores_state_and_ownership(self):
        source=ROOT/'.local/reference/mesa'/SOURCE
        if not source.exists():self.skipTest('pinned Mesa unavailable')
        with tempfile.TemporaryDirectory(prefix='compute-blit-oom-') as td:
            out=Path(td)
            projected=project(out/'projected')
            if projected is None:self.skipTest('pinned projection inputs unavailable')
            target=projected/SOURCE
            text=target.read_text();code=PRELUDE+'\n'.join(function(text,n) for n in ('asahi_compute_save','asahi_compute_restore','asahi_compute_blit'))+MAIN
            src=out/'test.c';exe=out/'test';src.write_text(code)
            p=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)

if __name__=='__main__':unittest.main()
