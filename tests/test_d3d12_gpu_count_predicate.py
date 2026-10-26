"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Counted indirect replay must keep GPU-produced count/arguments off the CPU,
retain scratch through submission/recursive batch rollover, order its kernel
before consumers, and fail closed on allocation/dispatch/submission failure.
Execute the production glue and the pinned MIT libagx kernel (only in temp
files); mocked boundaries are BO/batch submission, not the predication logic.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import project, ROOT
from test_d3d12_owner_allocation_failure import function

HELPER = ROOT/'drivers/apple-agx/mesa/winsys/agx_win32_counted_indirect.h'
MESA = ROOT/'.local/reference/mesa'

PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define PIPE_BUFFER 0
#define AGX_BARRIER_ALL 1
#define AGX_PREGFX 2
#define global
#define constant const
#define KERNEL(x)
typedef unsigned uint;
static struct {unsigned x;} cl_global_id;
@@KERNEL@@
#undef global
#undef constant
struct agx_device {int unused;};
struct agx_va {uint64_t addr;};
struct agx_bo {struct agx_va *va; unsigned refs;};
struct pipe_resource {unsigned target,width0;void *screen;};
struct agx_resource {struct pipe_resource base;struct agx_bo *bo;};
struct pipe_draw_info {unsigned index_size;};
struct pipe_draw_start_count_bias {unsigned unused;};
struct pipe_draw_indirect_info {struct pipe_resource *buffer,*indirect_draw_count;unsigned offset,stride,draw_count,indirect_draw_count_offset;void *count_from_stream_output;};
struct pipe_context {void *screen;void (*draw_vbo)(struct pipe_context *,const struct pipe_draw_info *,unsigned,const struct pipe_draw_indirect_info *,const struct pipe_draw_start_count_bias *,unsigned);};
struct agx_context {struct pipe_context base;bool any_faults;int (*windows_draw_bindings)(void *);void *windows_draw_bindings_data;};
struct agx_pool {unsigned unused;};
struct agx_batch {struct agx_context *ctx;struct agx_pool pool;};
struct agx_ptr {void *cpu;uint64_t gpu;};
static unsigned source[100000],counter[8],scratch[100000];
static struct agx_va input_va,count_va,scratch_va;
static struct agx_bo input_bo={&input_va,1},count_bo={&count_va,1},scratch_bo={&scratch_va,0};
static struct agx_resource input,count_resource;
static struct agx_batch batch;
static unsigned fault,reads,launches,flushes,encoded,notes,active,max_count,words,original_stride;
static bool retained,submitted;
static struct agx_device dev;
static struct agx_resource *agx_resource(struct pipe_resource *r){return (struct agx_resource *)r;}
static struct agx_device *agx_device(void *s){(void)s;return &dev;}
static uint64_t agx_map_gpu(struct agx_resource *r){return r->bo->va->addr;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *ctx,unsigned site){assert(site);ctx->any_faults=true;}
static struct agx_batch *agx_get_compute_batch(struct agx_context *ctx){assert(!ctx->windows_draw_bindings);if(fault==1){ctx->any_faults=true;return NULL;}active=1;batch.ctx=ctx;return &batch;}
static void agx_batch_reads(struct agx_batch *b,struct agx_resource *r){assert(b==&batch);assert(r==&input||r==&count_resource);++reads;if(fault==2)b->ctx->any_faults=true;}
static void agx_batch_init_state(struct agx_batch *b){assert(b==&batch);if(fault==3)b->ctx->any_faults=true;}
static struct agx_ptr agx_pool_alloc_aligned_with_bo(struct agx_pool *p,size_t size,unsigned alignment,struct agx_bo **out){assert(p==&batch.pool&&alignment==4);assert(size==max_count*words*4);if(fault==4)return (struct agx_ptr){0};scratch_bo.refs=1;*out=&scratch_bo;return (struct agx_ptr){scratch,scratch_va.addr};}
static void agx_bo_reference(struct agx_bo *bo){assert(bo==&scratch_bo&&bo->refs);++bo->refs;retained=true;}
static void agx_bo_unreference(struct agx_device *d,struct agx_bo *bo){assert(d==&dev&&bo==&scratch_bo&&bo->refs);--bo->refs;retained=false;}
static unsigned agx_1d(unsigned n){return n;}
static void submit(struct agx_batch *b){assert(b==&batch&&retained&&scratch_bo.refs==2);++flushes;submitted=true;active=0;--scratch_bo.refs;if(fault==6)b->ctx->any_faults=true;}
static void launch(struct agx_batch *b,unsigned n,unsigned barrier,uint64_t out,uint64_t in,uint64_t count,unsigned stride,bool indexed){
 assert(reads==2&&retained&&!b->ctx->windows_draw_bindings);
 assert(barrier==(AGX_BARRIER_ALL|AGX_PREGFX));assert(n==max_count&&stride==original_stride/4);
 ++launches;if(fault==5){b->ctx->any_faults=true;return;}
 for(unsigned i=0;i<n;++i){cl_global_id.x=i;libagx_predicate_indirect((uint32_t *)(uintptr_t)out,(const uint32_t *)(uintptr_t)in,(const uint32_t *)(uintptr_t)count,stride,indexed);}
 if(fault==7)submit(b); /* CDM capacity flush inside launch */
}
#define libagx_predicate_indirect launch
static bool agx_batch_is_active(struct agx_batch *b){assert(b==&batch);return active;}
static void agx_flush_batch_for_reason(struct agx_context *c,struct agx_batch *b,const char *s){assert(c==b->ctx&&s);submit(b);}
static void perf_note(struct agx_context *ctx,const char *s){assert(ctx&&!ctx->any_faults&&submitted&&s);++notes;}
#define perf_debug_ctx perf_note
static int bind(void *p){(void)p;return 1;}
static void draw(struct pipe_context *p,const struct pipe_draw_info *info,unsigned id,const struct pipe_draw_indirect_info *in,const struct pipe_draw_start_count_bias *r,unsigned n){
 (void)info;(void)r;assert(id==0&&n==1&&submitted&&retained&&scratch_bo.refs==1);
 struct agx_context *ctx=(struct agx_context *)p;assert(ctx->windows_draw_bindings==bind);
 assert(!in->indirect_draw_count&&!in->indirect_draw_count_offset&&in->draw_count==max_count&&in->stride==words*4);
 struct agx_resource *out=agx_resource(in->buffer);assert(out->bo==&scratch_bo&&!out->base.screen);
 for(unsigned i=0;i<max_count;++i){
  /* Each native subdraw can acquire/retire another batch: the local pin stays. */
  ++scratch_bo.refs;
  for(unsigned j=0;j<words;++j)assert(scratch[in->offset/4+i*words+j]==(i<counter[3]?source[7+i*(original_stride/4)+j]:0));
  --scratch_bo.refs;++encoded;
 }
}
'''

PROGRAM = r'''
static void run(unsigned index,unsigned value,unsigned failure,unsigned records){
 memset(scratch,0xcd,sizeof(scratch));for(unsigned i=0;i<100000;++i)source[i]=i*17+1;
 counter[3]=value;words=index?5:4;max_count=records;original_stride=32;fault=failure;
 reads=launches=flushes=encoded=notes=active=0;retained=submitted=false;scratch_bo.refs=0;
 input_va.addr=(uintptr_t)source;count_va.addr=(uintptr_t)counter;scratch_va.addr=(uintptr_t)scratch;
 input=(struct agx_resource){{PIPE_BUFFER,sizeof(source),(void *)1},&input_bo};
 count_resource=(struct agx_resource){{PIPE_BUFFER,sizeof(counter),(void *)1},&count_bo};
 struct agx_context ctx={{NULL,draw},false,bind,NULL};
 struct pipe_draw_info info={index};struct pipe_draw_start_count_bias range={0};
 struct pipe_draw_indirect_info indirect={&input.base,&count_resource.base,28,original_stride,max_count,12,NULL};
 agx_win32_draw_indirect_count(&ctx,&info,0,&indirect,&range);
 assert(ctx.windows_draw_bindings==bind&&!retained);
 if(failure&&failure!=7){assert(ctx.any_faults&&!encoded&&!notes);}
 else {assert(!ctx.any_faults&&launches==1&&flushes==1&&encoded==max_count&&notes==1&&scratch_bo.refs==0);}
 /* Aborted batches own their original pool reference until cleanup. */
 scratch_bo.refs=0;
}
int main(void){
 unsigned values[]={0,1,32,33,99,UINT32_MAX};
 for(unsigned index=0;index<=4;index+=4)for(unsigned v=0;v<6;++v)for(unsigned f=0;f<8;++f)run(index,values[v],f,33);
 for(unsigned index=0;index<=4;index+=4){run(index,0,0,1);run(index,1,0,1);run(index,1,0,12000);}
 for(unsigned bad=0;bad<7;++bad){
  run(4,3,0,33);encoded=reads=launches=0;
  struct agx_context ctx={{NULL,draw},false,bind,NULL};struct pipe_draw_info info={4};
  struct pipe_draw_indirect_info in={&input.base,&count_resource.base,28,32,33,12,NULL};
  if(bad==0)in.offset=UINT32_MAX;
  if(bad==1)in.indirect_draw_count_offset=UINT32_MAX;
  if(bad==2)in.stride=19;
  if(bad==3)in.draw_count=UINT32_MAX;
  if(bad==4)in.stride=30;
  if(bad==5)in.offset=27;
  if(bad==6)in.indirect_draw_count_offset=11;
  agx_win32_draw_indirect_count(&ctx,&info,0,&in,NULL);
  assert(ctx.any_faults&&!encoded&&!reads&&!launches&&ctx.windows_draw_bindings==bind);
 }
 struct agx_context ctx={{NULL,draw},false,bind,NULL};struct pipe_draw_info info={4};
 struct pipe_draw_indirect_info empty={0};encoded=reads=launches=0;
 agx_win32_draw_indirect_count(&ctx,&info,0,&empty,NULL);
 assert(!ctx.any_faults&&!encoded&&!reads&&!launches&&ctx.windows_draw_bindings==bind);
 return 0;
}
''' 

class GpuCountPredicate(unittest.TestCase):
    def test_real_kernel_and_batch_lifetime(self):
        self.assertTrue(HELPER.exists(), 'GPU predication helper is missing')
        kernel=(MESA/'src/asahi/libagx/draws.cl').read_text()
        kernel=kernel[:kernel.index('#include')]+function(kernel,'KERNEL(32)\nlibagx_predicate_indirect(')
        kernel=kernel.replace('KERNEL(32)\n','KERNEL(32)\nvoid\n')
        helper=HELPER.read_text()
        # The production include supplies types/extern declarations, modeled above.
        helper='\n'.join(s for s in helper.splitlines() if not s.startswith('#include'))
        code=PRELUDE.replace('@@KERNEL@@',kernel)+helper+PROGRAM
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(code)
            result=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_projection_routes_before_graphics_batch_entry(self):
        with tempfile.TemporaryDirectory() as td:
            out=project(Path(td)/'out')
            self.assertIsNotNone(out)
            state=(out/'src/gallium/drivers/asahi/agx_state.c').read_text()
            wrapper=function(state[state.rindex('static void\nagx_draw_vbo('):], 'static void\nagx_draw_vbo(')
            self.assertIn('agx_win32_draw_indirect_count(',wrapper)
            self.assertLess(wrapper.index('agx_win32_draw_indirect_count('),wrapper.index('AgxWin32AsahiBatchEnter('))
            self.assertLess(wrapper.index('agx_win32_draw_indirect_count('),wrapper.index('agx_legalize_feedback_loops('))
            self.assertIn('info->mode != MESA_PRIM_PATCHES',wrapper)
            self.assertIn('#include "agx_win32_counted_indirect.h"',state)
            self.assertIn('CPU counted indirect: patches',wrapper)
            # Actual generated library has both precompiled specializations.
            library=(out/'src/asahi/lib/libagx_shaders.h').read_text()
            self.assertIn('#define libagx_predicate_indirect(',library)

if __name__=='__main__':unittest.main()
