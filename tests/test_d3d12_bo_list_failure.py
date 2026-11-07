"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

A failed batch BO-bitset resize must preserve the old allocation and count,
fault the context, and take no reference for a BO that cannot be recorded.
Execute actual projected state.h bodies with a k-th realloc failure sweep;
the original primary-source bodies provide the sanitizer RED control.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, local_tree, project


PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdio>
using BITSET_WORD=uint64_t;
#define unlikely(x) (x)
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define ALIGN_POT(x,p) (((x)+(p)-1)&~((p)-1))
#define BITSET_TEST(a,b) (((a)[(b)/64]>>((b)%64))&1)
#define BITSET_SET(a,b) ((a)[(b)/64]|=uint64_t(1)<<((b)%64))
struct agx_context {bool any_faults;};
struct agx_bo {unsigned handle,references;};
struct agx_batch {agx_context *ctx;struct {BITSET_WORD *set;unsigned bit_count;} bo_list;};
static unsigned calls,fail_at;
static unsigned util_next_power_of_two(unsigned x) {unsigned n=1;while(n<x)n*=2;return n;}
static void agx_bo_reference(agx_bo *bo) {++bo->references;}
static BITSET_WORD *resize_words(agx_context *,BITSET_WORD *old,unsigned old_count,unsigned new_count) {
 assert(new_count>old_count);++calls;
 if(calls==fail_at)return nullptr;
 auto *next=(BITSET_WORD*)calloc(new_count,sizeof(BITSET_WORD));assert(next);
 memcpy(next,old,old_count*sizeof(BITSET_WORD));free(old);return next;
}
#define rerzalloc(ctx,old,type,old_count,new_count) resize_words(ctx,old,old_count,new_count)
@@FUNCTIONS@@
int main() {
 // Three growth points plus an all-success control; both API layers execute.
 for(unsigned kth=1;kth<=4;++kth)for(unsigned use_wrapper=0;use_wrapper<2;++use_wrapper) {
  calls=0;fail_at=kth;agx_context ctx={};agx_batch batch={};batch.ctx=&ctx;
  batch.bo_list.bit_count=64;batch.bo_list.set=(BITSET_WORD*)calloc(1,sizeof(BITSET_WORD));assert(batch.bo_list.set);
  agx_bo bos[]={{5,1},{64,1},{130,1},{4096,1}};
  agx_batch_add_bo(&batch,&bos[0]);assert(bos[0].references==2);
  for(unsigned i=1;i<4;++i) {
   auto *prior=batch.bo_list.set;unsigned bits=batch.bo_list.bit_count;
   std::vector<BITSET_WORD> snapshot(prior,prior+bits/64);
   if(use_wrapper)agx_batch_add_bo(&batch,&bos[i]);else agx_batch_add_bo_internal(&batch,&bos[i]);
   if(i==kth) {
    assert(ctx.any_faults && calls==kth);
    assert(batch.bo_list.set==prior && batch.bo_list.bit_count==bits);
    assert(!memcmp(prior,snapshot.data(),snapshot.size()*sizeof(BITSET_WORD)));
    assert(bos[i].references==1 && !agx_batch_uses_bo(&batch,&bos[i]));
    for(unsigned j=0;j<i;++j)assert(bos[j].references==2 && agx_batch_uses_bo(&batch,&bos[j]));
    // Re-adding an already-owned BO must neither lose it nor reference it twice.
    agx_batch_add_bo(&batch,&bos[0]);assert(bos[0].references==2 && agx_batch_uses_bo(&batch,&bos[0]));
    break;
   }
   assert(!ctx.any_faults && batch.bo_list.bit_count>bits && bos[i].references==2);
   for(unsigned j=0;j<=i;++j)assert(agx_batch_uses_bo(&batch,&bos[j]));
   agx_batch_add_bo(&batch,&bos[i]);assert(bos[i].references==2 && calls==i);
  }
  if(kth==4)assert(!ctx.any_faults && calls==3);
  free(batch.bo_list.set);
 }
 puts("batch BO list kth-resize sweep: PASS");
}
'''


def bodies(text):
    result='static bool agx_batch_uses_bo(struct agx_batch *batch, struct agx_bo *bo)\n'
    result+=function_body(text,'agx_batch_uses_bo')
    for name in ('agx_batch_add_bo_internal','agx_batch_add_bo'):
        result+='\nstatic void '+name+'(struct agx_batch *batch, struct agx_bo *bo)\n'
        result+=function_body(text,name)
    return result


class BoListFailure(unittest.TestCase):
    def test_projected_bitset_resize_failure_preserves_ownership(self):
        with tempfile.TemporaryDirectory(prefix='mem2-bo-list-') as tmp:
            out=project(Path(tmp)/'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            rel='src/gallium/drivers/asahi/agx_state.h'
            primary=local_tree('reference','mesa')/rel
            for name,text in [('projected',(out/rel).read_text()),('original',primary.read_text())]:
                src=Path(tmp)/(name+'.cpp');exe=Path(tmp)/name
                src.write_text(PRELUDE.replace('@@FUNCTIONS@@',bodies(text)))
                b=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-Wall','-Wextra',str(src),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(b.returncode,0,b.stderr)
                r=subprocess.run([str(exe)],capture_output=True,text=True)
                if name=='projected':
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                    self.assertIn('kth-resize sweep: PASS',r.stdout)
                else:
                    self.assertNotEqual(r.returncode,0,'original unchecked resize must fail')
                    self.assertTrue('runtime error:' in r.stderr or 'AddressSanitizer' in r.stderr,r.stderr)
