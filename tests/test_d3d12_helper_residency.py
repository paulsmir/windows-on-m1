"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Spill commands publish the cached helper executable VA without a consuming
batch reference. Execute the projected three publication blocks and require
that an evicted helper joins residency, including on cache reuse.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import project

class HelperResidency(unittest.TestCase):
    def test_all_helper_consumers_retain_executable(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);out=project(p/'out')
            s=(out/'src/gallium/drivers/asahi/agx_pipe.c').read_text()
            blocks=[]
            for field in ('c->vertex_helper.binary','c->fragment_helper.binary','cmdbuf->helper.binary'):
                start=s.index('      '+field+' = agx_helper_program(')
                end=s.index('\n   }',start)
                blocks.append(s[start:end])
            pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define LIBAGX_HELPER 1
struct agx_bo {bool resident;};
struct agx_precompiled_shader {struct agx_bo *bo;};
struct agx_bg_eot_cache {struct agx_precompiled_shader shader;};
struct agx_context {struct agx_bg_eot_cache bg_eot;bool fault;};
struct agx_batch {struct agx_context *ctx;struct agx_bo *ref;};
struct command {struct {uint64_t binary;} vertex_helper,fragment_helper,helper;};
static bool fail;
static uint64_t agx_helper_program(struct agx_bg_eot_cache *c){return fail?0:0x4001;}
static struct agx_precompiled_shader *agx_get_precompiled(struct agx_bg_eot_cache *c,unsigned p){assert(p==LIBAGX_HELPER);return &c->shader;}
static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo){b->ref=bo;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned s){c->fault=true;}
'''
            code=pre+'\n'.join('static void consume%d(struct agx_batch *batch,struct command *c,struct command *cmdbuf){\n%s\n}'%(i,b) for i,b in enumerate(blocks))
            code+=r'''
int main(){
 void (*consumers[])(struct agx_batch *,struct command *,struct command *)={consume0,consume1,consume2};
 for(unsigned i=0;i<3;++i){
  struct agx_bo bo={false};struct agx_context ctx={{{&bo}},false};
  for(unsigned reuse=0;reuse<2;++reuse){
  bo.resident=false;ctx.fault=false;
  struct agx_batch batch={&ctx,0};struct command cmd={0};
  fail=false;consumers[i](&batch,&cmd,&cmd);
  assert(batch.ref==&bo); // submission restores precisely the referenced BO
  batch.ref->resident=true;assert(bo.resident && !ctx.fault);
  batch.ref=0;fail=true;consumers[i](&batch,&cmd,&cmd);assert(ctx.fault && !batch.ref);
 }}
}
'''
            (p/'test.c').write_text(code)
            z=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
