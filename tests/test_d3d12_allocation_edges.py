"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

The geometry/tessellation heap's resource allocation could fail before the
checked pool upload and still be dereferenced. Encoding failure must also
unwind entered capture/compute scopes instead of keeping stack pointers live.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import project, function_body


class AllocationEdges(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='mem2-edges-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.base=Path(cls.tmp.name)
        cls.out=project(cls.base/'out')
        if cls.out is None: raise unittest.SkipTest('Mesa unavailable')
        cls.state=(cls.out/'src/gallium/drivers/asahi/agx_state.c').read_text()

    def test_geometry_heap_kth_allocation_failure(self):
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define PIPE_BIND_GLOBAL 1
#define PIPE_USAGE_DEFAULT 2
struct agx_bo {struct {uint64_t addr;} *va;};
struct agx_resource {struct agx_bo *bo;};
struct agx_context {struct {void *screen;} base;struct agx_resource *heap;bool any_faults;};
struct agx_batch {struct agx_context *ctx;uint64_t heap;int pool;};
struct poly_heap {uint64_t base;unsigned size;};
#define agx_resource(r) (r)
static unsigned kth,calls,writes,uploads;
static int fail(void){return ++calls==kth;}
static struct agx_resource *pipe_buffer_create(void *s,int b,int u,unsigned n){
 assert(s && b==1 && u==2 && n==128*1024*1024);
 if(fail())return NULL;
 struct agx_resource *r=calloc(1,sizeof(*r));r->bo=calloc(1,sizeof(*r->bo));
 r->bo->va=calloc(1,sizeof(*r->bo->va));r->bo->va->addr=0x10000;return r;
}
static void agx_batch_writes(struct agx_batch *b,struct agx_resource *r,unsigned l){assert(b && r && !l);++writes;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned n){assert(n);c->any_faults=true;}
static uint64_t agx_pool_upload_aligned(int *p,const void *v,size_t n,unsigned a){
 assert(p && n==sizeof(struct poly_heap) && a==8);const struct poly_heap *h=v;assert(h->base==0x10000);++uploads;
 return fail()?0:0x20000;
}
'''
        code+='\nstatic uint64_t agx_batch_heap(struct agx_batch *batch)\n'+function_body(self.state,'agx_batch_heap')
        code+=r'''
int main(void){
 for(kth=1;kth<=3;++kth){
  struct agx_context ctx={{&ctx},NULL,false};struct agx_batch b={&ctx,0,0};calls=writes=uploads=0;
  uint64_t p=agx_batch_heap(&b);
  if(kth<3){assert(!p && !b.heap && ctx.any_faults);if(kth==1)assert(!ctx.heap && !writes && !uploads);}
  else {assert(p==0x20000 && b.heap==p && !ctx.any_faults && writes==1 && uploads==1);assert(agx_batch_heap(&b)==p && calls==2);}
  if(ctx.heap){free(ctx.heap->bo->va);free(ctx.heap->bo);free(ctx.heap);}
 }
 return 0;
}
'''
        src=self.base/'heap.c';exe=self.base/'heap';src.write_text(code)
        build=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
        self.assertEqual(build.returncode,0,build.stderr)
        run=subprocess.run([str(exe)],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr)

    def test_indirect_upload_failure_and_reference_ownership(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
struct pipe_resource {unsigned refs;};
struct pipe_draw_info {int unused;};
struct pipe_draw_start_count_bias {int unused;};
struct pipe_draw_indirect_info {unsigned draw_count,indirect_draw_count_offset;struct pipe_resource *indirect_draw_count;};
struct pipe_context {void *const_uploader;void (*draw_vbo)(struct pipe_context *,const struct pipe_draw_info *,unsigned,const struct pipe_draw_indirect_info *,const struct pipe_draw_start_count_bias *,unsigned);};
struct agx_context {struct pipe_context base;bool any_faults;};
#define agx_context(p) ((struct agx_context *)(p))
static unsigned mode,draws;
static struct pipe_resource uploaded={1},borrowed={1};
static void pipe_resource_reference(struct pipe_resource **dst,struct pipe_resource *src){if(*dst)--(*dst)->refs;*dst=src;if(src)++src->refs;}
static void u_upload_data_ref(void *u,unsigned o,unsigned n,unsigned a,const void *d,unsigned *out,struct pipe_resource **r){assert(u && !o && n==4 && a==4 && *(const unsigned *)d==7);*out=0;pipe_resource_reference(r,mode?NULL:&uploaded);}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned site){assert(site);c->any_faults=true;}
static void draw(struct pipe_context *p,const struct pipe_draw_info *i,unsigned id,const struct pipe_draw_indirect_info *ind,const struct pipe_draw_start_count_bias *d,unsigned n){assert(p && i && !id && d && n==1 && ind->indirect_draw_count==&uploaded);++draws;}
'''
        code += '\nstatic void util_draw_multi_upload_indirect(struct pipe_context *pctx, const struct pipe_draw_info *info, const struct pipe_draw_indirect_info *indirect, const struct pipe_draw_start_count_bias *draws)\n'+function_body(self.state,'util_draw_multi_upload_indirect')
        code += r'''
int main(void){for(mode=0;mode<2;++mode){struct agx_context ctx={{&ctx,draw},false};struct pipe_draw_info i={0};struct pipe_draw_start_count_bias d={0};struct pipe_draw_indirect_info ind={7,0,&borrowed};draws=0;util_draw_multi_upload_indirect(&ctx.base,&i,&ind,&d);assert(draws==!mode && ctx.any_faults==!!mode);assert(uploaded.refs==1 && borrowed.refs==1);}return 0;}
'''
        src=self.base/'indirect.c';exe=self.base/'indirect';src.write_text(code)
        build=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
        self.assertEqual(build.returncode,0,build.stderr)
        run=subprocess.run([str(exe)],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr)

    def test_capture_failures_unwind_before_return(self):
        """Invariant: inserted failure returns cannot bypass matching Leave/Finish."""
        gs=function_body(self.state,'agx_launch_gs_prerast')
        scoped=gs.split('AgxWin32AsahiBatchComputeEnter(batch)',1)[1]
        # The Enter refusal itself may return; all subsequent injected guards
        # must target the single matching Leave.
        scoped=scoped.split('agx_ensure_cmdbuf_has_space',1)[1]
        self.assertNotIn('return;',scoped.replace('return ;','return;'))
        self.assertIn('goto windows_compute_done;',scoped)
        self.assertLess(scoped.index('windows_compute_done:'),scoped.index('AgxWin32AsahiBatchComputeLeave'))
        enc=function_body(self.state,'agx_encode_state')
        failure=enc.split('windows_state_failed:',1)[1]
        self.assertLess(failure.index('PipelineFinish(nested_capture'),failure.index('PipelineFinish(&state_capture'))
        self.assertLess(failure.index('PipelineFinish(&state_capture'),failure.index('return NULL'))
        self.assertNotIn('return NULL',enc.split('windows_state_failed:',1)[0])

    def test_all_graphics_batch_consumers_check_acquisition(self):
        pipe=(self.out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
        for src,name in ((self.state,'agx_draw_patches'),(self.state,'agx_draw_vbo_windows_body'),
                         (self.state,'agx_legalize_feedback_loop_surf'),(pipe,'agx_invalidate_resource')):
            with self.subTest(name=name):
                body=function_body(src,name).split('struct agx_batch *batch = agx_get_batch(ctx);',1)[1]
                self.assertTrue(body.lstrip().startswith('if (!batch) return;'))


if __name__=='__main__':unittest.main()
