"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Sampler heap insertion/map failure must never publish a usable handle; failed
encoder or shadow maps must stop submission. Execute the actual native routines
with k-th fallible-operation injection, under ASan/UBSan.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'


class WinsysFailures(unittest.TestCase):
    def replay(self, code):
        with tempfile.TemporaryDirectory(prefix='mem2-winsys-') as directory:
            src = Path(directory)/'replay.c'
            exe = Path(directory)/'replay'
            src.write_text(code)
            compiled = subprocess.run(['clang', '-std=c11', '-fsanitize=address,undefined',
                                      str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_sampler_kth_failure_never_publishes_invalid_handle(self):
        source = (WINSYS/'agx_win32_asahi_scene.c').read_text()
        self.replay(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define AGX_SAMPLER_HEAP_SIZE 4u
#define APPLE_AGX_U16 uint16_t
struct agx_bo { int unused; };
struct agx_sampler_packed { unsigned value; };
struct heap { struct agx_bo *bo; unsigned count; };
struct agx_batch { struct heap sampler_heap; };
struct agx_device { int unused; };
struct pipe_context { void *screen; };
struct agx_context { struct pipe_context base; bool any_faults; };
#define agx_context(p) ((struct agx_context *)(p))
static struct agx_device dev;
static struct agx_device *agx_device(void *p) { (void)p;return &dev; }
static struct agx_batch batch;
static struct agx_bo bo;
static struct agx_sampler_packed stored[4];
static unsigned kth, calls, faults;
static int fail(void) { return ++calls==kth; }
static struct agx_batch *heap_batch(struct agx_context *ctx,int compute) {
 (void)compute;return ctx->any_faults?NULL:&batch;
}
static void AgxWin32AsahiMarkContextFault(struct agx_context *ctx,unsigned site) {
 assert(site);ctx->any_faults=true;++faults;
}
static void agx_flush_batch_for_reason(struct agx_context *ctx,struct agx_batch *b,const char *reason) {
 (void)ctx;(void)reason;b->sampler_heap=(struct heap){0};
}
static void *agx_bo_map(struct agx_bo *b) { assert(b);return fail()?NULL:stored; }
static unsigned agx_sampler_heap_add(struct agx_device *d,struct heap *h,struct agx_sampler_packed *s) {
 assert(d && h->count<4);
 if(fail()) return UINT16_MAX;
 h->bo=&bo;stored[h->count]=*s;return h->count++;
}
''' + body(source, 'AgxWin32AsahiBatchSamplerHandles') + r'''
int main(void) {
 for(unsigned existing=0;existing<2;++existing) {
  for(kth=0;kth<5;++kth) {
   struct agx_context ctx={0};calls=faults=0;
   batch.sampler_heap=(struct heap){existing?&bo:NULL,existing};
   stored[0].value=99;
   const struct agx_sampler_packed packed[2]={{17},{18}};
   uint16_t handles[2]={UINT16_MAX,UINT16_MAX};
   int ok=AgxWin32AsahiBatchSamplerHandles(&ctx.base,0,packed,sizeof(packed[0]),2,handles);
   if(kth && calls>=kth) {
    assert(!ok && faults==1 && ctx.any_faults);
    for(unsigned i=0;i<2;++i) assert(handles[i]==UINT16_MAX ||
      (handles[i]>=28 && handles[i]<28+batch.sampler_heap.count));
   } else assert(ok && !faults && handles[0]==28+existing && handles[1]==29+existing);
  }
 }
}
''')

    def test_encoder_map_failures_reject_publication(self):
        bo = (WINSYS/'agx_win32_asahi_bo.c').read_text()
        batch = (WINSYS/'agx_win32_gpuva_batch.c').read_text()
        self.replay(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define AGX_WIN32_BO_SHADOW 0x100
#define AGX_WIN32_ENCODER_TAIL 16
struct agx_bo { unsigned flags; void *_map; size_t size; };
struct windows_bo { struct agx_bo Base; void *Shadow; struct {void *CpuAddress;} Backing; };
struct list { struct agx_bo **data; unsigned count; };
struct agx_pool { struct agx_bo *transient_bo; size_t transient_offset; struct list bos; };
struct agx_encoder { struct agx_bo *bo; void *current; };
struct agx_batch {struct agx_encoder vdm,cdm;struct agx_pool pool,pipeline_pool;};
#define util_dynarray_foreach(a,t,v) for(t *v=(a)->data; (a)->count && v<(a)->data+(a)->count; ++v)
static unsigned kth,calls;
static void *agx_bo_map(struct agx_bo *b) {return ++calls==kth?NULL:b->_map;}
''' + body(bo, 'AgxWin32AsahiShadowPublish') + body(batch, 'pool_extent') + body(batch, 'publish_shadows') + r'''
int main(void) {
 unsigned char src[64],dst[64];memset(src,42,sizeof(src));
 struct windows_bo bo={{AGX_WIN32_BO_SHADOW,src,64},src,{dst}};
 struct agx_batch batch={0};batch.vdm=(struct agx_encoder){&bo.Base,src+16};
 for(kth=0;kth<3;++kth) {
  calls=0;memset(dst,0,sizeof(dst));int ok=publish_shadows(&batch);
  assert(ok==(kth!=1));assert(dst[0]==(ok?42:0));
 }
 bo.Base._map=NULL;memset(dst,0,sizeof(dst));
 /* An untouched preallocated pool slab has no recorded data to publish. */
 assert(AgxWin32AsahiShadowPublish(&bo.Base,64) && dst[0]==0);
 bo.Base.flags=0;assert(AgxWin32AsahiShadowPublish(&bo.Base,64));
}
''')

    def test_buffer_copy_stops_after_failed_hazard_sync(self):
        source = (WINSYS/'agx_win32_asahi_scene.c').read_text()
        self.replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define PIPE_BUFFER 1
#define APPLE_AGX_U64 uint64_t
#define BITSET_SET(a,b) ((a)[b]=1)
struct agx_bo {unsigned char data[16];};
struct pipe_context {void *screen;};
struct agx_context {struct pipe_context base;bool any_faults;};
struct pipe_resource {void *screen;unsigned target,width0;};
struct agx_resource {struct pipe_resource base;struct agx_bo *bo;unsigned valid_buffer_range,data_valid[1];};
#define agx_context(p) ((struct agx_context *)(p))
#define agx_resource(p) ((struct agx_resource *)(p))
static unsigned kth,calls,maps;
static void agx_sync_writer(struct agx_context *ctx,struct agx_resource *r,const char *why){
 (void)r;(void)why;assert(!ctx->any_faults);if(++calls==kth)ctx->any_faults=true;
}
static void agx_sync_readers(struct agx_context *ctx,struct agx_resource *r,const char *why){agx_sync_writer(ctx,r,why);}
static void *agx_bo_map(struct agx_bo *b){++maps;return b->data;}
static void util_range_add(struct pipe_resource *r,unsigned *range,unsigned first,unsigned end){
 (void)r;assert(first==0 && end==16);*range=1;
}
''' + body(source, 'AgxWin32AsahiBufferCopy') + r'''
int main(void){
 for(kth=0;kth<=2;++kth){
  struct agx_context ctx={{(void *)1},false};
  struct agx_bo src={{42}},dst={{0}};
  struct agx_resource s={{(void *)1,PIPE_BUFFER,16},&src,0,{0}},d={{(void *)1,PIPE_BUFFER,16},&dst,0,{0}};
  calls=maps=0;int ok=AgxWin32AsahiBufferCopy(&ctx.base,&d.base,0,&s.base,0,16);
  assert(ok==!kth && maps==(kth?0u:2u));
  assert(dst.data[0]==(kth?0:42) && d.data_valid[0]==!kth);
 }
}
''')

    def test_image_legalization_failure_never_packs_descriptor(self):
        source = (WINSYS/'agx_win32_asahi_scene.c').read_text()
        self.replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define APPLE_AGX_U64 uint64_t
#define PIPE_MAX_SHADER_IMAGES 16
#define MESA_SHADER_COMPUTE 5
struct pipe_resource {void *screen;};
struct pipe_image_view {struct pipe_resource *resource;};
struct pipe_context {void *screen;void (*set_shader_images)(struct pipe_context *,unsigned,unsigned,unsigned,unsigned,const struct pipe_image_view *);};
struct agx_context {struct pipe_context base;bool any_faults;};
#define agx_context(p) ((struct agx_context *)(p))
static unsigned mode,binds,unbinds,packs;
static void set_images(struct pipe_context *ctx,unsigned stage,unsigned slot,unsigned count,unsigned unbind,const struct pipe_image_view *v){
 assert(stage==5 && slot==15);
 if(count){assert(v);++binds;if(mode==1)agx_context(ctx)->any_faults=true;}
 else {assert(unbind==1 && !v);++unbinds;}
}
static void agx_win32_pack_image_view(void *t,void *p,const struct pipe_image_view *v){assert(v);++packs;*(unsigned *)t=*(unsigned *)p=42;}
static uint64_t AgxWin32AsahiResourceLayoutKey(struct pipe_resource *r){assert(r);return 77;}
''' + body(source, 'AgxWin32AsahiImageDescriptors') + r'''
int main(void){
 for(mode=0;mode<=2;++mode){
  struct agx_context ctx={{(void *)1,set_images},mode==2};
  struct pipe_resource r={(void *)1};struct pipe_image_view v={&r};
  unsigned texture=0,pbe=0;uint64_t key=0;binds=unbinds=packs=0;
  int ok=AgxWin32AsahiImageDescriptors(&ctx.base,&v,&texture,&pbe,&key);
  assert(ok==(mode==0) && packs==(mode==0));
  assert(binds==(mode!=2) && unbinds==binds);
  assert(key==(mode==0?77u:0u) && texture==(mode==0?42u:0u));
 }
}
''')


if __name__ == '__main__':
    unittest.main()
