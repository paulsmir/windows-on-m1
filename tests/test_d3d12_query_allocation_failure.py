"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

EXP1928 faults at the first BO dereference in agx_create_query after terminal
MakeResident failure. Query creation must return NULL on allocation/map failure,
release private BOs, and never consume a shared occlusion slot without a map.
Compile the real projected constructors, with only allocation/map dependencies
replaced by fault injection, under ASan/UBSan.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define AGX_MAX_OCCLUSION_QUERIES 32
#define AGX_BO_WRITEBACK 8
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define BITSET_DECLARE(n,s) uint32_t n[1]
#define BITSET_ONES(a) ((a)[0]=UINT32_MAX)
#define BITSET_FFS(a) __builtin_ffs((a)[0])
#define BITSET_CLEAR(a,i) ((a)[0]&=~(1u<<(i)))
#define PIPE_QUERY_OCCLUSION_COUNTER 0
#define PIPE_QUERY_OCCLUSION_PREDICATE 1
#define PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE 2
struct agx_device { int unused; };
struct pipe_context { struct agx_device *screen; };
struct pipe_query { int unused; };
struct agx_va { uint64_t addr; };
struct agx_bo { struct agx_va *va; unsigned refs; };
struct agx_ptr { void *cpu; uint64_t gpu; };
struct agx_query { unsigned type, index; uint64_t writer_generation[128];
  struct agx_bo *bo; struct agx_ptr ptr; };
struct agx_oq_heap { struct agx_device *dev; struct agx_bo *bo;
  BITSET_DECLARE(available, AGX_MAX_OCCLUSION_QUERIES); };
struct agx_context { struct pipe_context base; struct agx_oq_heap *oq; };
#define agx_context(p) ((struct agx_context *)(p))
#define agx_device(p) (p)
static unsigned mode, objects, heaps, bos, creates, maps, releases;
static uint64_t storage[AGX_MAX_OCCLUSION_QUERIES];
static struct agx_va va={0x120000};
static void (*heap_destructor)(void *);
static void *query_calloc(size_t n,size_t s) {
  if(mode==1) return NULL;
  void *p=calloc(n,s);assert(p);++objects;return p;
}
static void query_free(void *p) {if(p) {assert(objects);--objects;free(p);}}
static void *heap_alloc(size_t size) {
  if(mode==4) return NULL;
  void *p=calloc(1,size);assert(p);++heaps;return p;
}
#define rzalloc(ctx,type) ((type *)heap_alloc(sizeof(type)))
static void ralloc_set_destructor(void *p,void (*d)(void *)) {
  assert(p);heap_destructor=d;
}
static void ralloc_free(void *p) {
  if(p) {assert(heaps);heap_destructor(p);--heaps;free(p);}
}
static struct agx_bo *agx_bo_create(struct agx_device *d,unsigned size,
                                  unsigned align,unsigned flags,const char *label) {
  assert(d && !align && flags==AGX_BO_WRITEBACK);++creates;
  assert((size==16 && !strcmp(label,"Query")) ||
         (size==sizeof(storage) && !strcmp(label,"Occlusion query heap")));
  if(mode==2) return NULL; /* also the Windows failed-backend result */
  struct agx_bo *bo=calloc(1,sizeof(*bo));assert(bo);
  bo->va=&va;bo->refs=1;++bos;return bo;
}
static void *agx_bo_map(struct agx_bo *bo) {
  assert(bo && bo->refs);++maps;return mode==3?NULL:storage;
}
static void agx_bo_unreference(struct agx_device *d,struct agx_bo *bo) {
  if(!bo) return;
  assert(d && bo->refs==1 && bos);--bos;++releases;free(bo);
}
#define calloc query_calloc
#define free query_free
'''


class QueryAllocationFailure(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='agx-query-allocation-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.base = Path(cls.tmp.name)
        out = project(cls.base/'out')
        if out is None:
            raise unittest.SkipTest('reference Mesa tree not available')
        query = (out/'src/gallium/drivers/asahi/agx_query.c').read_text()
        cls.code = PRELUDE
        for decl, name, args in (
                ('bool', 'is_occlusion', 'struct agx_query *query'),
                ('void', 'agx_destroy_oq_heap', 'void *heap_'),
                ('struct agx_oq_heap *', 'agx_alloc_oq_heap', 'struct agx_context *ctx'),
                ('struct agx_oq_heap *', 'agx_get_oq_heap', 'struct agx_context *ctx'),
                ('struct agx_ptr', 'agx_alloc_oq', 'struct agx_context *ctx'),
                ('struct pipe_query *', 'agx_create_query',
                 'struct pipe_context *ctx, unsigned query_type, unsigned index')):
            cls.code += f'\nstatic {decl} {name}({args})\n' + function_body(query, name)

    def run_c(self, name, main, modes):
        src = self.base/(name+'.c')
        exe = self.base/name
        src.write_text(self.code + main)
        build = subprocess.run(['clang', '-std=c11', '-fsanitize=address,undefined',
                                str(src), '-o', str(exe)], capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stderr)
        for mode in modes:
            with self.subTest(mode=mode):
                run = subprocess.run([str(exe), str(mode)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stderr)

    def test_dedicated_query_allocation_and_mapping_failure(self):
        self.run_c('dedicated', r'''
int main(int argc,char **argv) {
 assert(argc==2);mode=atoi(argv[1]);struct agx_device dev={0};
 struct agx_context ctx={{&dev},NULL};
 struct agx_query *q=(void *)agx_create_query(&ctx.base,7,9);
 if(mode==0) {
   assert(q && q->type==7 && q->index==9 && q->bo && q->ptr.cpu==storage && q->ptr.gpu==0x120000);
   for(unsigned i=0;i<128;++i) assert(q->writer_generation[i]==UINT64_MAX);
   agx_bo_unreference(&dev,q->bo);query_free(q);
 } else {
   assert(!q && !objects && !bos);
   if(mode==1) assert(!creates);
   if(mode==2) assert(creates==1 && !maps && !releases);
   if(mode==3) assert(creates==1 && maps==1 && releases==1);
 }
 assert(!objects && !bos && !heaps);return 0;
}
''', range(4))

    def test_occlusion_failures_preserve_heap_and_slot_ownership(self):
        self.run_c('occlusion', r'''
int main(int argc,char **argv) {
 assert(argc==2);mode=atoi(argv[1]);struct agx_device dev={0};
 struct agx_context ctx={{&dev},NULL};
 struct agx_query *q=(void *)agx_create_query(&ctx.base,0,0);
 if(mode) {
   assert(!q && !objects);
   if(mode==3) {
     assert(ctx.oq && ctx.oq->available[0]==UINT32_MAX && bos==1);
   } else {assert(!ctx.oq && !bos && !heaps);}
 }
 mode=0;
 if(!q) q=(void *)agx_create_query(&ctx.base,0,0);
 assert(q && q->ptr.cpu==storage && q->ptr.gpu==0x120000);
 assert(ctx.oq->available[0]==0xfffffffe);
 /* A failed map on an existing heap with a nonzero offset cannot publish
  * pointer arithmetic on NULL or consume another slot. */
 mode=3;
 struct agx_query *other=(void *)agx_create_query(&ctx.base,1,0);
 assert(!other && ctx.oq->available[0]==0xfffffffe && objects==1);
 mode=0;other=(void *)agx_create_query(&ctx.base,2,0);
 assert(other && other->ptr.cpu==storage+1 && other->ptr.gpu==0x120008);
 assert(ctx.oq->available[0]==0xfffffffc);
 ctx.oq->available[0]=0; /* exhausted heap: no new allocation/map */
 unsigned before=maps;
 assert(!agx_create_query(&ctx.base,0,0) && maps==before && objects==2);
 query_free(other);query_free(q);ralloc_free(ctx.oq);
 assert(!objects && !heaps && !bos && releases==1);return 0;
}
''', range(5))


if __name__ == '__main__':
    unittest.main()
