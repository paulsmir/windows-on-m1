"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

Actual projected sampler/uniform pool consumers must stop before CPU writes or
GPU descriptor publication when either half of a pool allocation is unusable.
Sweep the k-th dependency failure; a test-only removed-guard build demonstrates
sensitivity to the original unchecked consumers. This is a host source replay,
not a Windows callback or complete GPU workload test.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using mesa_shader_stage=unsigned;
#define AGX_SAMPLER_LENGTH 16
#define AGX_BORDER_LENGTH 16
#define AGX_SYSVAL_TABLE_ROOT 0
#define MESA_PRIM_POINTS 1
#define STATIC_ASSERT static_assert
struct agx_sampler_packed {uint8_t bytes[16];};
struct agx_sampler_state {agx_sampler_packed desc;uint8_t border[16];bool uses_custom_border;};
struct agx_stage {bool custom_borders;unsigned sampler_count;agx_sampler_state *samplers[2];};
struct rast {struct {bool clip_halfz;unsigned sprite_coord_enable;} base;};
struct agx_context {bool any_faults;unsigned sample_mask;struct rast *rast;agx_stage stage[1];};
struct agx_pool {};
struct agx_draw_uniforms {uint64_t tables[1];unsigned sample_mask,clip_z_coeff,sprite_mask;};
struct agx_batch {agx_context *ctx;agx_pool pool;unsigned sampler_count[1];uint64_t samplers[1];agx_draw_uniforms uniforms;unsigned reduced_prim;};
struct agx_compiled_shader {};
struct agx_ptr {void *cpu;uint64_t gpu;};
static uint8_t storage[2][256];
static unsigned allocations,fail_at,fail_kind,faults,packs;
static agx_ptr agx_pool_alloc_aligned(agx_pool *,size_t bytes,unsigned) {
 assert(allocations<2 && bytes<=256);unsigned i=allocations++;
 if(allocations==fail_at) {
   // Exercise fully NULL plus inconsistent CPU-only and GPU-only results.
   return {fail_kind==1?storage[i]:nullptr,fail_kind==2?uint64_t(0x1000+i*256):0};
 }
 return {storage[i],uint64_t(0x1000+i*256)};
}
static unsigned sampler_count(agx_context *c,unsigned stage) {return c->stage[stage].sampler_count+1;}
static unsigned _mesa_float_to_half(double) {return 0x3800;}
static void agx_pack_txf_sampler(void *p) {++packs;memset(p,0x11,16);}
static void AgxWin32AsahiMarkContextFault(agx_context *c,unsigned reason) {assert(reason>>16==5);c->any_faults=true;++faults;}
@@FUNCTIONS@@
int main() {
 for(unsigned kind=0;kind<3;++kind)for(unsigned kth=1;kth<=3;++kth) {
  memset(storage,0xcd,sizeof(storage));allocations=faults=packs=0;fail_at=kth;fail_kind=kind;
  agx_sampler_state sampler={};memset(&sampler.desc,0x47,sizeof(sampler.desc));
  agx_context ctx={};ctx.sample_mask=0xf;ctx.stage[0].sampler_count=1;ctx.stage[0].samplers[0]=&sampler;
  agx_batch batch={};batch.ctx=&ctx;batch.samplers[0]=0xfeed;batch.sampler_count[0]=0x99;
  memset(&batch.uniforms,0xab,sizeof(batch.uniforms));const auto prior=batch.uniforms;
  agx_upload_samplers(&batch,nullptr,0);
  if(kth==1) {
   assert(ctx.any_faults && faults==1 && allocations==1 && !packs);
   assert(batch.samplers[0]==0xfeed && batch.sampler_count[0]==0x99);
   assert(!memcmp(&prior,&batch.uniforms,sizeof(prior)));
   for(unsigned char c:storage[0])assert(c==0xcd);
   continue;
  }
  assert(!ctx.any_faults && packs==1 && batch.samplers[0]==0x1000 && batch.sampler_count[0]==2);
  agx_upload_uniforms(&batch);
  if(kth==2) {
   assert(ctx.any_faults && faults==1 && allocations==2);
   assert(!memcmp(&prior,&batch.uniforms,sizeof(prior)));
   for(unsigned char c:storage[1])assert(c==0xcd);
  } else {
   assert(!ctx.any_faults && !faults && allocations==2);
   assert(batch.uniforms.tables[0]==0x1100 && batch.uniforms.sample_mask==0xf);
   assert(!memcmp(storage[1],&batch.uniforms,sizeof(batch.uniforms)));
  }
 }
 puts("pool consumer kth-allocation sweep: PASS");
}
'''


class PoolFailure(unittest.TestCase):
    def test_projected_consumers_kth_allocation_failure(self):
        with tempfile.TemporaryDirectory(prefix='mem2-pool-failure-') as tmp:
            out=project(Path(tmp)/'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            base=out/'src/gallium/drivers/asahi'
            funcs='static void agx_upload_samplers(struct agx_batch *batch, struct agx_compiled_shader *cs, mesa_shader_stage stage)\n'
            funcs+=function_body((base/'agx_state.c').read_text(),'agx_upload_samplers')
            funcs+='\nstatic void agx_upload_uniforms(struct agx_batch *batch)\n'
            funcs+=function_body((base/'agx_uniforms.c').read_text(),'agx_upload_uniforms')
            guard=r'\n +if \(!(?:T|root_ptr)\.cpu \|\| !(?:T|root_ptr)\.gpu\) \{\n +AgxWin32AsahiMarkContextFault\([^\n]+\n +return\s*;\n +\}'
            mutant,count=re.subn(guard,'',funcs)
            self.assertEqual(count,2,'both actual projected guards must be exercised')
            for name,body in [('guarded',funcs),('removed_guard',mutant)]:
                src=Path(tmp)/(name+'.cpp');exe=Path(tmp)/name
                src.write_text(PRELUDE.replace('@@FUNCTIONS@@',body))
                b=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-Wall','-Wextra',str(src),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(b.returncode,0,b.stderr)
                r=subprocess.run([str(exe)],capture_output=True,text=True)
                if name=='guarded':
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                    self.assertIn('kth-allocation sweep: PASS',r.stdout)
                else:
                    self.assertNotEqual(r.returncode,0,'removed-guard mutant must fail')
                    self.assertTrue('runtime error:' in r.stderr or 'AddressSanitizer' in r.stderr,r.stderr)
