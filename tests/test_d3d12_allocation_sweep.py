"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

A failed sampler BO/map or pool allocation must not be dereferenced, consume a
sampler index, or publish descriptors. Sweep the k-th allocation/map through
actual projected sampler functions and the existing query-constructor replay.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import function_body, project
from test_d3d12_query_allocation_failure import QueryAllocationFailure


class AllocationSweep(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='mem2-sweep-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.base = Path(cls.tmp.name)
        cls.out = project(cls.base/'out')
        if cls.out is None:
            raise unittest.SkipTest('pinned Mesa source unavailable')

    def run_c(self, name, code):
        src = self.base/(name+'.c')
        exe = self.base/name
        src.write_text(code)
        build = subprocess.run(['clang', '-std=c11', '-fsanitize=address,undefined',
                                str(src), '-o', str(exe)], capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)

    def test_sampler_heap_kth_failure(self):
        state = (self.out/'src/gallium/drivers/asahi/agx_state.c').read_text()
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define AGX_SAMPLER_HEAP_SIZE 8
#define AGX_SAMPLER_LENGTH 16
#define AGX_BO_WRITEBACK 8
struct agx_device {int unused;};
struct agx_bo {int unused;};
struct agx_sampler_packed {uint32_t words[4];};
struct agx_sampler_heap {struct agx_bo *bo; unsigned count;};
static unsigned kth, calls, failed;
static struct agx_bo bo;
static struct agx_sampler_packed storage[8];
static int fail(void) {++calls; if(calls==kth){failed=1;return 1;}return 0;}
static struct agx_bo *agx_bo_create(struct agx_device *d,size_t s,unsigned a,unsigned f,const char *l) {
 (void)d;(void)s;(void)a;(void)f;(void)l;return fail()?NULL:&bo;
}
static void *agx_bo_map(struct agx_bo *b) {assert(b);return fail()?NULL:storage;}
'''
        code += '\nstatic uint16_t agx_sampler_heap_add(struct agx_device *dev, struct agx_sampler_heap *heap, struct agx_sampler_packed *sampler)\n'+function_body(state,'agx_sampler_heap_add')
        code += r'''
int main(void) {
 for(kth=1;kth<=10;++kth) {
  struct agx_sampler_heap heap={0};struct agx_device dev={0};
  struct agx_sampler_packed desc={{1,2,3,4}};
  calls=failed=0;memset(storage,0,sizeof(storage));
  for(unsigned i=0;i<8;++i) {
   uint16_t index=agx_sampler_heap_add(&dev,&heap,&desc);
   if(failed) {assert(index>=AGX_SAMPLER_HEAP_SIZE && heap.count==i);break;}
   assert(index==i && heap.count==i+1 && storage[i].words[3]==4);
  }
  if(!failed) assert(heap.count==8);
 }
 return 0;
}
'''
        self.run_c('sampler', code)

    def test_query_replay_kth_allocation_and_map(self):
        QueryAllocationFailure.setUpClass()
        self.addCleanup(QueryAllocationFailure.doClassCleanups)
        code = QueryAllocationFailure.code
        code = code.replace('static unsigned mode,', 'static unsigned kth,calls;\nstatic int fail(void){return ++calls==kth;}\nstatic unsigned mode,')
        for condition in ('mode==1','mode==2','mode==3','mode==4'):
            code=code.replace(condition, 'fail()')
        code += r'''
int main(void) {
 for(kth=1;kth<32;++kth) {
  struct agx_device dev={0};struct agx_context ctx={{&dev},NULL};calls=0;
  for(unsigned i=0;i<8;++i) {
   struct agx_query *q=(void *)agx_create_query(&ctx.base,i%2?7:0,i);
   if(!q) break;
   if(q->bo) agx_bo_unreference(&dev,q->bo);
   query_free(q);
  }
  ralloc_free(ctx.oq);assert(!objects && !heaps && !bos);
 }
 return 0;
}
'''
        self.run_c('query',code)


if __name__=='__main__':
    unittest.main()
