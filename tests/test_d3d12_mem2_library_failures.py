"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Scratch growth must clear released state on allocation/map failure; linker and
internal shader uploads must return failure before writing or caching unusable
objects. Compile projected production constructors (and allocation admission
regions for linker/precompiled packing) with k-th failure injection and sanitizers.
"""
import importlib.util
from pathlib import Path
import shutil
import unittest

from test_d3d10_shader_fields_init import ROOT, function_body, local_tree
import test_d3d12_mem2_winsys_failures as winsys_tests


class LibraryFailures(unittest.TestCase):
    replay = winsys_tests.WinsysFailures.replay

    @classmethod
    def setUpClass(cls):
        import tempfile
        cls.tmp = tempfile.TemporaryDirectory(prefix='mem2-library-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.out = Path(cls.tmp.name)
        reference = local_tree('reference', 'mesa')
        if reference is None:
            raise unittest.SkipTest('reference Mesa unavailable')
        names = ('agx_scratch.c', 'agx_linker.c', 'agx_bg_eot.c')
        target = cls.out/'src/asahi/lib'
        target.mkdir(parents=True)
        for name in names:
            shutil.copyfile(reference/'src/asahi/lib'/name, target/name)
        helper = ROOT/'drivers/apple-agx/mesa/scripts/native-asahi-library-failure.py'
        spec = importlib.util.spec_from_file_location('library_failure', helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        cls.overlays = {}
        module.project_sources(cls.out, ROOT, cls.overlays)
        cls.sources = {name: (target/name).read_text() for name in names}

    def test_scratch_growth_kth_failure_clears_released_state(self):
        source = self.sources['agx_scratch.c']
        start = source.index('#define AGX_ADDR_SHIFT')
        stop = source.index('\nvoid\nagx_scratch_debug_pre')
        self.replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define AGX_DBG_SCRATCH 1
#define AGX_BO_WRITEBACK 1
#define AGX_SPILL_SIZE_BUCKETS 16
#define AGX_MAX_CORE_ID 8
#define DIV_ROUND_UP(x,y) (((x)+(y)-1)/(y))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define align(x,a) (((x)+(a)-1)&~((a)-1))
#define BITFIELD_BIT(x) (1u<<(x))
#define BITFIELD_MASK(x) ((1u<<(x))-1)
static unsigned util_logbase2(unsigned x) {return 31-__builtin_clz(x);}
static unsigned util_logbase2_ceil(unsigned x) {return x<=1?0:32-__builtin_clz(x-1);}
static unsigned util_next_power_of_two(unsigned x) {return 1u<<util_logbase2_ceil(x);}
struct agx_va {uint64_t addr;};
struct agx_bo {struct agx_va *va;size_t size;void *map;};
struct agx_device {unsigned debug;struct {unsigned num_cores_per_cluster,num_clusters_total,core_masks[1];} params;};
struct agx_helper_block {uint32_t blocks[4];};
struct agx_helper_header {unsigned subgroups;struct {uint64_t blocklist;} cores[8];};
struct agx_scratch {struct agx_device *dev;struct agx_bo *buf;struct agx_helper_header *header;unsigned size_dwords,subgroups,num_cores,max_core_id;};
static unsigned kth,calls,live;
static struct agx_va va={0x100000};
static struct agx_bo *agx_bo_create(struct agx_device *d,size_t bytes,unsigned align,unsigned flags,const char *name) {
 (void)d;(void)align;(void)flags;(void)name;if(++calls==kth)return NULL;
 struct agx_bo *b=calloc(1,sizeof(*b));assert(b);b->map=calloc(1,bytes);assert(b->map);
 b->va=&va;b->size=bytes;++live;return b;
}
static void *agx_bo_map(struct agx_bo *b) {assert(b);return ++calls==kth?NULL:b->map;}
static void agx_bo_unreference(struct agx_device *d,struct agx_bo *b) {
 (void)d;if(b){assert(live);--live;free(b->map);free(b);}
}
''' + source[start:stop] + r'''
int main(void) {
 for(unsigned k=1;k<=3;++k) {
  struct agx_device dev={.params={1,1,{1}}};
  struct agx_scratch scratch={.dev=&dev,.num_cores=1};
  kth=calls=0;agx_scratch_alloc(&scratch,8,1);assert(scratch.buf && scratch.header && live==1);
  kth=k;calls=0;agx_scratch_alloc(&scratch,64,2);
  if(k<=2) assert(!scratch.buf && !scratch.header && !scratch.size_dwords && !scratch.subgroups && !live);
  else assert(scratch.buf && scratch.header && live==1);
  kth=0;agx_scratch_alloc(&scratch,64,2);assert(scratch.buf && scratch.header && live==1);
  agx_bo_unreference(&dev,scratch.buf);assert(!live);
 }
}
''')

    def test_internal_shader_compile_and_cache_failures(self):
        source = self.sources['agx_bg_eot.c']
        compile_body = function_body(source, 'agx_compile_bg_eot_shader')
        get_body = function_body(source, 'agx_get_bg_eot_shader')
        self.replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define AGX_EOT_STORE 2
#define NIR_PASS(...) ((void)0)
typedef struct {int unused;} nir_shader;
struct agx_shader_key {int unused;};struct agx_tilebuffer_layout {int unused;};
struct agx_bg_eot_key {unsigned op[2];};
struct agx_bo {int unused;};
struct shader_info {unsigned binary_size;};
struct agx_bg_eot_shader {struct shader_info info;uint64_t ptr;struct agx_bo *bo;struct agx_bg_eot_key key;};
struct agx_shader_part {struct shader_info info;void *binary;};
struct agx_bg_eot_cache {void *ht;int pool;};struct hash_entry {void *data;};
static unsigned kth,calls,live,compiled,inserted,built_bg,built_eot;
static void *alloc(size_t size){if(++calls==kth)return NULL;++live;return calloc(1,size);}
#define rzalloc(ctx,t) ((t *)alloc(sizeof(t)))
static void ralloc_free(void *p){if(p){assert(live);--live;free(p);}}
static void agx_nir_lower_texture(nir_shader *s){assert(s);}
static void agx_preprocess_nir(nir_shader *s){assert(s);}
static void agx_compile_shader_nir(nir_shader *s,struct agx_shader_key *k,struct agx_shader_part *b){
 (void)k;assert(s);++compiled;b->info.binary_size=16;b->binary=malloc(16);
}
static uint64_t agx_pool_upload_aligned_with_bo(int *pool,void *binary,unsigned size,unsigned align,struct agx_bo **bo){
 (void)pool;assert(binary && size==16 && align==128);static struct agx_bo storage;
 *bo=NULL;if(++calls==kth)return 0;*bo=&storage;return 0x100000;
}
static struct hash_entry *_mesa_hash_table_search(void *h,struct agx_bg_eot_key *k){assert(h && k);return NULL;}
static struct hash_entry *_mesa_hash_table_insert(void *h,struct agx_bg_eot_key *k,void *v){
 assert(h && k && v);if(++calls==kth)return NULL;++inserted;return (void *)1;
}
static struct agx_bg_eot_shader *agx_compile_bg_eot_shader(struct agx_bg_eot_cache *cache,nir_shader *shader,struct agx_shader_key *key,struct agx_tilebuffer_layout *tib)
''' + compile_body + r'''
static struct agx_bg_eot_shader *build(struct agx_bg_eot_cache *c){
 nir_shader *s=calloc(1,sizeof(*s));++live;return agx_compile_bg_eot_shader(c,s,NULL,NULL);
}
static struct agx_bg_eot_shader *agx_build_end_of_tile_shader(struct agx_bg_eot_cache *c,struct agx_bg_eot_key *k){(void)k;++built_eot;return build(c);}
static struct agx_bg_eot_shader *agx_build_background_shader(struct agx_bg_eot_cache *c,struct agx_bg_eot_key *k){(void)k;++built_bg;return build(c);}
static struct agx_bg_eot_shader *agx_get_bg_eot_shader(struct agx_bg_eot_cache *cache,struct agx_bg_eot_key *key)
''' + get_body + r'''
int main(void){
 for(unsigned eot=0;eot<2;++eot) for(kth=0;kth<=3;++kth){
  calls=compiled=inserted=built_bg=built_eot=0;
  struct agx_bg_eot_cache cache={(void *)1,0};struct agx_bg_eot_key key={{eot?AGX_EOT_STORE:0,0}};
  struct agx_bg_eot_shader *r=agx_get_bg_eot_shader(&cache,&key);
  if(kth) assert(!r && !inserted && !live);
  else {assert(r && inserted==1 && live==1);ralloc_free(r);}
  assert(built_eot==eot && built_bg==!eot);
 }
}
''')

    def test_linker_bo_and_map_failure_before_binary_write(self):
        source = self.sources['agx_linker.c']
        admission = function_body(source, 'agx_fast_link').split('   size_t offset = 0;')[0]
        constants = source[source.index('static const uint8_t sample_loop_header'):source.index('\nvoid\nagx_fast_link')]
        self.replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#define AGX_BO_EXEC 1
#define AGX_BO_LOW_VA 2
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX2(a,b) ((a)>(b)?(a):(b))
struct agx_device {int unused;};struct agx_bo {char data[256];};
struct agx_shader_part {struct {unsigned main_size,nr_gprs,scratch_size;bool reads_tib,writes_sample_mask,disable_tri_merging,uses_base_param,uses_txf,tag_write_disable;} info;};
struct agx_linked_shader {struct agx_bo *bo;bool uses_base_param,uses_txf;};
static unsigned kth,calls,live,admitted;
static struct agx_bo *agx_bo_create(struct agx_device *d,size_t bytes,unsigned align,unsigned flags,const char *name){
 (void)d;(void)name;assert(bytes && !align && flags==3);if(++calls==kth)return NULL;
 ++live;return calloc(1,sizeof(struct agx_bo));
}
static void *agx_bo_map(struct agx_bo *b){assert(b);return ++calls==kth?NULL:b->data;}
static void agx_bo_unreference(struct agx_device *d,struct agx_bo *b){(void)d;if(b){assert(live);--live;free(b);}}
''' + constants + r'''
static void agx_fast_link(struct agx_linked_shader *linked,struct agx_device *dev,
 bool fragment,struct agx_shader_part *main,struct agx_shader_part *prolog,
 struct agx_shader_part *epilog,unsigned nr_samples_shaded)
''' + admission + r'''
 assert(linked_map);++admitted;
}
int main(void){
 for(kth=0;kth<=2;++kth){
  calls=admitted=0;struct agx_device d={0};struct agx_linked_shader linked={0};
  struct agx_shader_part main={.info={.main_size=16}};
  agx_fast_link(&linked,&d,false,&main,NULL,NULL,0);
  if(kth) assert(!linked.bo && !admitted && !live);
  else {assert(linked.bo && admitted==1 && live==1);agx_bo_unreference(&d,linked.bo);}
 }
}
''')

    def test_precompiled_allocation_upload_and_helper_failure(self):
        source = self.sources['agx_bg_eot.c']
        admission = function_body(source, 'agx_get_precompiled_locked').split('   /* Bake launch */')[0]
        helper = function_body(source, 'agx_helper_program')
        self.replay(r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define LIBAGX_HELPER 0
struct agx_bo {int unused;};
struct agx_precompiled_shader {struct {unsigned workgroup;} b;uint64_t ptr;struct agx_bo *bo;};
struct agx_precompiled_kernel_info {unsigned main_offset,workgroup_size[3],binary_size;};
struct agx_device {const uint32_t *libagx_programs[1];};
struct agx_bg_eot_cache {int lock,pool;void *ht;struct agx_device *dev;struct agx_precompiled_shader *precomp[1];};
static unsigned kth,calls,live,admitted;
static void *alloc(size_t bytes){if(++calls==kth)return NULL;++live;return calloc(1,bytes);}
#define ralloc(ctx,t) ((t *)alloc(sizeof(t)))
static void ralloc_free(void *p){if(p){assert(live);--live;free(p);}}
static void simple_mtx_assert_locked(int *l){(void)l;}
static unsigned agx_workgroup(unsigned x,unsigned y,unsigned z){return x*y*z;}
static uint64_t agx_pool_upload_aligned_with_bo(int *pool,const void *binary,unsigned size,unsigned align,struct agx_bo **bo){
 (void)pool;assert(binary && size==16 && align==128);static struct agx_bo storage;
 *bo=NULL;if(++calls==kth)return 0;*bo=&storage;return 0x100000;
}
static struct agx_precompiled_shader *agx_get_precompiled_locked(struct agx_bg_eot_cache *cache,unsigned program)
''' + admission + r'''
 ++admitted;cache->precomp[program]=p;return p;
}
static struct agx_precompiled_shader *agx_get_precompiled(struct agx_bg_eot_cache *cache,unsigned program){
 return agx_get_precompiled_locked(cache,program);
}
static uint64_t agx_helper_program(struct agx_bg_eot_cache *cache)
''' + helper + r'''
int main(void){
 struct agx_precompiled_kernel_info info={.workgroup_size={1,1,1},.binary_size=16};
 struct agx_device dev={{(void *)&info}};
 for(kth=0;kth<=2;++kth){
  calls=admitted=0;struct agx_bg_eot_cache cache={.ht=(void *)1,.dev=&dev};
  uint64_t helper=agx_helper_program(&cache);
  if(kth) assert(!helper && !cache.precomp[0] && !live && !admitted);
  else {assert(helper==0x100001 && cache.precomp[0] && live==1 && admitted==1);ralloc_free(cache.precomp[0]);}
 }
}
''')


if __name__ == '__main__':
    unittest.main()
