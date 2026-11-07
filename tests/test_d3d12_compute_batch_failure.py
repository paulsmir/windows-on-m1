"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

EXP1927: MakeResident OOM faults the context; the next compute batch has no
encoder, but its caller writes the initial CDM barrier (ARM64 RVA 0x48184).
A failed acquisition must return NULL, and all callers must stop before using
that result. Compile the projected getter and its caller admission regions,
plus the complete native heap tracking functions, under ASan/UBSan.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, function_body, project

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define AGX_COMPUTE_BATCH_WIDTH 65535u
#define PIPE_BUFFER 1
struct pipe_context { void *screen; };
struct pipe_resource { void *screen; unsigned target, width0, last_level; };
struct agx_resource { struct pipe_resource base; };
struct agx_bo { unsigned size; };
struct agx_encoder { struct agx_bo *bo; unsigned char *current, *end; };
struct pipe_framebuffer_state { unsigned width; };
struct agx_context;
struct agx_batch { struct agx_context *ctx; struct agx_encoder cdm, vdm;
  bool initialized, incoherent_writes; };
struct agx_context { struct pipe_context base; bool any_faults; struct agx_batch *batch; };
#define agx_context(p) ((struct agx_context *)(p))
#define agx_resource(p) ((struct agx_resource *)(p))
static struct agx_batch candidate;
static struct agx_bo bo = {64};
static unsigned char encoder[64];
static unsigned acquisitions, dirty, initializations, reads, writes, fault_site, mode;
static void agx_dirty_all(struct agx_context *c) {(void)c; ++dirty;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c, unsigned site) {
  c->any_faults = true; fault_site = site;
}
static struct agx_batch *agx_get_batch_for_framebuffer(struct agx_context *c,
  const struct pipe_framebuffer_state *key) {
  assert(key->width == AGX_COMPUTE_BATCH_WIDTH); ++acquisitions;
  candidate.ctx=c; candidate.cdm=(struct agx_encoder){&bo,encoder,encoder+64};
  if(mode==1) c->any_faults=true;
  if(mode==2) return NULL;
  if(mode==3) candidate.cdm.bo=NULL;
  if(mode==4) candidate.cdm.current=NULL;
  c->batch=&candidate;return &candidate;
}
static struct agx_batch *agx_get_batch(struct agx_context *c) {
  struct agx_batch *b=agx_get_batch_for_framebuffer(c,&(struct pipe_framebuffer_state){65535});
  if(b) b->vdm=b->cdm;
  return b;
}
static void agx_batch_init_state(struct agx_batch *b) {
  assert(b && !b->ctx->any_faults && b->cdm.bo && b->cdm.current);
  ++initializations;b->initialized=true; b->cdm.current[0]=42;
}
static void agx_batch_reads(struct agx_batch *b,struct agx_resource *r) {
  assert(b && b->initialized && r && !b->ctx->any_faults);++reads;
  if(mode==5) b->ctx->any_faults=true;
}
static void agx_batch_writes(struct agx_batch *b,struct agx_resource *r,unsigned level) {
  assert(b && b->initialized && r && level<=r->base.last_level && !b->ctx->any_faults);++writes;
  if(mode==5) b->ctx->any_faults=true;
}
static void agx_batch_writes_range(struct agx_batch *b,struct agx_resource *r,unsigned off,unsigned size) {
  assert(!off && size==r->base.width0);agx_batch_writes(b,r,0);
}
'''


def native_function(text, name):
    # Native bridge declarations use a same-line opening brace.
    start = text.rfind('\n', 0, text.index(name + '(')) + 1
    end = text.index('\n}', start) + 2
    return text[start:end]


class ComputeBatchFailure(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='agx-compute-failure-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.base = Path(cls.tmp.name)
        cls.out = project(cls.base/'out')
        if cls.out is None:
            raise unittest.SkipTest('reference Mesa tree not available')
        batch = (cls.out/'src/gallium/drivers/asahi/agx_batch.c').read_text()
        cls.getter = ('static struct agx_batch *agx_get_compute_batch(struct agx_context *ctx)\n'
                      + function_body(batch, 'agx_get_compute_batch'))

    def run_c(self, name, code, modes):
        src = self.base/(name+'.c'); exe = self.base/name
        src.write_text(code)
        built = subprocess.run(['clang', '-std=c11', '-fsanitize=address,undefined',
                                str(src), '-o', str(exe)], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr)
        for mode in modes:
            with self.subTest(mode=mode):
                ran = subprocess.run([str(exe), str(mode)], capture_output=True, text=True)
                self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_failed_acquisition_is_not_a_usable_batch(self):
        self.run_c('get', PRELUDE + self.getter + r'''
int main(int argc,char **argv) {
 assert(argc==2);mode=atoi(argv[1]);struct agx_context ctx={0};
 if(mode==6) ctx.any_faults=true;
 struct agx_batch *b=agx_get_compute_batch(&ctx);
 if(mode==0) {assert(b==&candidate && !ctx.any_faults && acquisitions==1);agx_batch_init_state(b);}
 else {
   assert(!b && ctx.any_faults && !initializations);
   if(mode==6) assert(!acquisitions && !dirty);
   else assert(acquisitions==1);
   if(mode==1 || mode==3 || mode==4) assert(ctx.batch==&candidate);
   if(mode>=2 && mode<=4) assert(fault_site);
 }
}
''', [0, 1, 2, 3, 4, 6])

    def test_projected_callers_stop_before_first_batch_use(self):
        # Execute the actual admission region, from acquisition through the
        # first encoder use. The downstream compiler/packer is a sentinel;
        # nothing in the test inserts a failure guard. Inventory all calls so
        # a new unhandled consumer cannot silently bypass this replay.
        # agx_blit.c: agx_resource_copy_region and rc103's AgxWin32AsahiCopyToLevel
        expected = {'agx_blit.c': 2, 'agx_pipe.c': 2, 'agx_query.c': 1, 'agx_state.c': 1}
        for filename, count in expected.items():
            text = (self.out/'src/gallium/drivers/asahi'/filename).read_text()
            starts = list(re.finditer(r'struct agx_batch \*batch = agx_get_compute_batch\([^;]+;', text))
            self.assertEqual(len(starts), count)
            for index, match in enumerate(starts):
                # launch_grid uses indirect/query state before init_state;
                # test its entry up to that first batch-dependent work.
                stop = text.index('uint64_t indirect = 0;', match.end()) if filename == 'agx_state.c' else text.index('agx_batch_init_state(batch);', match.end())
                admission = text[match.start():stop]
                returns_bool = filename == 'agx_query.c'
                # rc103: the level copy reports a refusal as 0 (its caller blits)
                returns_int = filename == 'agx_blit.c' and 'AgxWin32AsahiCopyToLevel' in text[:match.start()][-4000:]
                decl = 'static bool caller' if returns_bool else 'static int caller' if returns_int else 'static void caller'
                tail = 'return true;' if returns_bool else 'return 1;' if returns_int else ''
                invoke = ('assert(caller(&ctx) == (mode == 0));' if returns_bool or returns_int else 'caller(&ctx);')
                self.run_c(filename+str(index), PRELUDE + self.getter + '\n' + decl + r'''(struct agx_context *ctx) {
 struct pipe_context *pctx=&ctx->base,*pipe=&ctx->base;
''' + admission + 'agx_batch_init_state(batch);' + tail + r'''
}
int main(int argc,char **argv) {
 assert(argc==2);mode=atoi(argv[1]);struct agx_context ctx={0};
 if(mode==6) ctx.any_faults=true;
''' + invoke + r'''
 assert(initializations==(mode==0?1u:0u));
}
''', [0, 1, 2, 3, 4, 6])

    def test_heap_tracking_stops_on_acquisition_or_hazard_failure(self):
        text = (ROOT/'drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c').read_text()
        functions = '\n'.join(native_function(text, name) for name in
                              ('heap_batch', 'AgxWin32AsahiBatchReadResources', 'AgxWin32AsahiBatchWriteResources'))
        self.run_c('heap', PRELUDE + self.getter + functions + r'''
int main(int argc,char **argv) {
 assert(argc==2);unsigned test=atoi(argv[1]); mode=test%7;
 bool write=(test/7)%2,compute=test/14;
 struct agx_context ctx={0};ctx.base.screen=&ctx;
 struct pipe_resource a={&ctx,PIPE_BUFFER,64,0}, b={&ctx,2,64,2},foreign={0};
 struct pipe_resource *r[]={NULL,&foreign,&a,&b};
 if(mode==6) ctx.any_faults=true;
 if(write) AgxWin32AsahiBatchWriteResources(&ctx.base,r,4,compute);
 else AgxWin32AsahiBatchReadResources(&ctx.base,r,4,compute);
 if(mode==0) assert(initializations==1 && reads==(write?0u:2u) && writes==(write?4u:0u));
 else if(mode==5) assert(ctx.any_faults && reads+writes==1);
 else assert(!initializations && !reads && !writes);
}
''', range(28))


    def test_query_failure_does_not_fall_back_to_cpu(self):
        text = (self.out/'src/gallium/drivers/asahi/agx_query.c').read_text()
        body = function_body(text, 'agx_get_query_result_resource')
        self.run_c('query-fallback', PRELUDE + r"""
struct pipe_query { unsigned unused; };
struct agx_query { unsigned unused; };
enum query_flags { QUERY_FLAGS_ZERO };
enum value_type { VALUE_TYPE_ZERO };
static unsigned cpu_copies;
static bool agx_get_query_result_resource_gpu(struct agx_context *ctx,
 struct agx_query *q,enum query_flags f,enum value_type t,int i,struct pipe_resource *r,unsigned o) {
 (void)q;(void)f;(void)t;(void)i;(void)r;(void)o;
 if(mode==1) ctx->any_faults=true;
 return mode==2;
}
static void agx_get_query_result_resource_cpu(struct agx_context *ctx,
 struct agx_query *q,enum query_flags f,enum value_type t,int i,struct pipe_resource *r,unsigned o) {
 (void)ctx;(void)q;(void)f;(void)t;(void)i;(void)r;(void)o;++cpu_copies;
}
static void copy(struct pipe_context *pipe,struct pipe_query *q,enum query_flags flags,
 enum value_type result_type,int index,struct pipe_resource *resource,unsigned offset)
""" + body + r"""
int main(int argc,char **argv) {
 assert(argc==2);mode=atoi(argv[1]);struct agx_context ctx={0};
 copy(&ctx.base,NULL,0,0,0,NULL,0);
 assert(cpu_copies==(mode==0?1u:0u));
}
""", range(3))

    def test_sampler_failure_before_and_after_flush(self):
        text = (ROOT/'drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c').read_text()
        functions = '\n'.join(native_function(text, name) for name in
                              ('heap_batch', 'AgxWin32AsahiBatchSamplerHandles'))
        # Complete native helpers; real sampler addition/dedup loops. External
        # allocation and flush only are stubbed to inject acquisition failures.
        prelude = PRELUDE.replace('bool initialized, incoherent_writes;',
            'struct {struct agx_bo *bo; unsigned count;} sampler_heap; bool initialized, incoherent_writes;')
        self.run_c('samplers', prelude + self.getter + r"""
#define AGX_SAMPLER_HEAP_SIZE 4
#define APPLE_AGX_U16 uint16_t
struct agx_device { unsigned unused; };
struct agx_sampler_packed { unsigned value; };
static unsigned heap_sampler_headroom(const struct agx_context *ctx) {(void)ctx;return 0;}
static struct agx_sampler_packed stored[4];
static unsigned additions, flushes;
static struct agx_device *agx_device(void *s) {(void)s;return NULL;}
static void *agx_bo_map(struct agx_bo *b) {(void)b;return stored;}
static unsigned agx_sampler_heap_add(struct agx_device *d,void *h,struct agx_sampler_packed *s) {
 (void)d;(void)h;++additions;unsigned i=candidate.sampler_heap.count++;
 assert(i<4);stored[i]=*s;candidate.sampler_heap.bo=&bo;return i;
}
static void agx_flush_batch_for_reason(struct agx_context *ctx,struct agx_batch *b,const char *s) {
 (void)s;assert(b==&candidate);++flushes;
 candidate.sampler_heap.count=0;candidate.sampler_heap.bo=NULL;
 if(mode==5) ctx->any_faults=true;
}
""" + functions + r"""
int main(int argc,char **argv) {
 assert(argc==2);unsigned test=atoi(argv[1]);mode=test%8;unsigned compute=test/8;
 struct agx_context ctx={0};uint16_t handles[2]={0xffff,0xffff};
 struct agx_sampler_packed packed[2]={{17},{17}};
 if(mode==6) ctx.any_faults=true;
 if(mode==5 || mode==7) candidate.sampler_heap.count=4;
 int ok=AgxWin32AsahiBatchSamplerHandles(&ctx.base,compute,packed,sizeof(packed[0]),2,handles);
 if(mode==0 || mode==7) assert(ok && additions==1 && handles[0]==28 && handles[1]==28);
 else assert(!ok && !additions && handles[0]==0xffff && handles[1]==0xffff);
 assert(flushes==((mode==5 || mode==7)?1u:0u));
}
""", range(16))



if __name__ == '__main__':
    unittest.main()
