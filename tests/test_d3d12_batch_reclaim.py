"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Completed batches retain reusable BOs until BatchBegin, after new pools and
encoders have already allocated. That avoidable cache miss can enter paging
while a newer render is pending. Exercise the actual projected selection and
cleanup functions and the real BO cache, with completion/allocation callbacks.
Pending/active batches and their indirect results must survive early reclaim.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from g3_vidmm_replay import body
from test_d3d10_shader_fields_init import ROOT, project
from test_g4_bo_cache import BODY, HDR, SRC, function


class BatchReclaim(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='q21-reclaim-')
        cls.out = project(Path(cls.temp.name) / 'native')
        if cls.out is None:
            cls.temp.cleanup()
            raise unittest.SkipTest('reference Mesa tree not available')
        cls.native = (cls.out / 'src/gallium/drivers/asahi/agx_batch.c').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def compile_run(self, code, cxx=False):
        with tempfile.TemporaryDirectory(prefix='q21-model-') as td:
            p = Path(td)
            src = p / ('model.cpp' if cxx else 'model.c')
            src.write_text(code)
            build = subprocess.run([
                'clang++' if cxx else 'clang', '-std=c++17' if cxx else '-std=c11',
                '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined',
                str(src), '-o', str(p / 'model')], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(p / 'model')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_completed_storage_is_available_before_new_batch_allocates(self):
        # This existing fixture supplies the real cache's full storage/hold
        # types and a deterministic allocator, not a replacement cache policy.
        prefix = BODY[:BODY.index('int main(void)')]
        prefix = prefix.replace('int Failed, Closing;',
                                'int Failed, Closing; unsigned ReclaimBeforeBatch;')
        limits = '\n'.join(l for l in HDR.read_text().splitlines()
                           if l.startswith('#define AGX_WIN32_BO_CACHE_'))
        funcs = '\n'.join(function(SRC.read_text(), n) for n in (
            'cache_storage_bytes', 'cache_in_flight', 'cache_evict_oldest',
            'cache_put', 'cache_take', 'cache_flush'))
        code = prefix.replace('@@LIMITS@@', limits).replace('@@FUNCS@@', funcs)
        code += r'''
#include <stdbool.h>
#define APPLE_AGX_GPUVA_WINSYS 1
#define AGX_MAX_BATCHES 8u
#define AgxWaitBatchSlot 1u
static unsigned AgxWin32WaitBegin(unsigned site){return site;}
static void AgxWin32WaitEnd(unsigned token){(void)token;}
#define BITSET_TEST(s,i) (((s)[0] >> (i)) & 1u)
#define foreach_active(c,i) for ((i)=0;(i)<AGX_MAX_BATCHES;++(i)) if (BITSET_TEST((c)->batches.active,i))
#define foreach_submitted(c,i) for ((i)=0;(i)<AGX_MAX_BATCHES;++(i)) if (BITSET_TEST((c)->batches.submitted,i))
struct pipe_framebuffer_state {unsigned kind;};
struct agx_device {void *windows_private;};
struct agx_context;
struct agx_batch {
 struct agx_context *ctx;
 struct pipe_framebuffer_state key;
 unsigned seqnum, index, completed, poll_failure, cleanup_failure;
 uint64_t expected_vertices, resolved_vertices;
 struct windows_bo *storage;
};
struct agx_context {
 struct {struct agx_device *screen;} base;
 struct {struct agx_batch slots[AGX_MAX_BATCHES]; unsigned active[1],submitted[1],seqnum;} batches;
};
static unsigned paging_requests, polls, syncs;
static struct agx_device *agx_device(struct agx_device *d){return d;}
static bool util_framebuffer_state_equal(const struct pipe_framebuffer_state *a,
                                         const struct pipe_framebuffer_state *b){return a->kind==b->kind;}
static bool agx_batch_is_submitted(struct agx_batch *b){return BITSET_TEST(b->ctx->batches.submitted,b->index);}
static bool agx_batch_is_active(struct agx_batch *b){return BITSET_TEST(b->ctx->batches.active,b->index);}
static int AgxWin32AsahiBatchPoll(struct agx_batch *b,unsigned timeout){
 assert(timeout==0);++polls;return b->completed && !b->poll_failure;
}
static void agx_batch_cleanup(struct agx_context *c,struct agx_batch *b,bool reset){
 assert(c==b->ctx && b->completed && !reset && agx_batch_is_submitted(b));
 if(b->cleanup_failure)return;
 /* The real cleanup snapshots query results before returning pool storage. */
 b->resolved_vertices=b->expected_vertices;
 if(b->storage){assert(cache_put(b->storage));b->storage=NULL;}
 c->batches.submitted[0]&=~(1u<<b->index);
}
int agx_cleanup_batches(struct agx_context *ctx);
static void agx_batch_init(struct agx_context *c,const struct pipe_framebuffer_state *key,
                           struct agx_batch *b){
 AGX_WIN32_ASAHI_BACKEND *backend=c->base.screen->windows_private;
 b->ctx=c;b->key=*key;b->seqnum=++c->batches.seqnum;
 b->storage=cache_take(backend,0x40000,0,1);
 if(!b->storage){++paging_requests;b->storage=mk(backend,0x40000,0,1,100+made_count);}
 c->batches.active[0]|=1u<<b->index;
 /* Existing BatchBegin cleanup occurs AFTER pool/encoder allocation. */
 while(agx_cleanup_batches(c)>=0){}
}
/* PF1 (agx_win32_expand_ahead.h): no batch is sealed in this model */
#define AGX_WIN32_FOLD_CAUSE_EVICT 3u
static void agx_win32_fold_cause(struct agx_batch *b,unsigned k){(void)b;(void)k;}
static void agx_sync_batch_for_reason(struct agx_context *c,struct agx_batch *b,const char *s){
 (void)c;(void)b;(void)s;++syncs;
}
''' + body(self.native, 'agx_cleanup_batches') + '\n' + body(
            self.native, 'agx_get_batch_for_framebuffer') + r'''
static void init(struct agx_context *c,struct agx_device *d){
 memset(c,0,sizeof(*c));c->base.screen=d;
 for(unsigned i=0;i<AGX_MAX_BATCHES;++i){c->batches.slots[i].ctx=c;c->batches.slots[i].index=i;}
 paging_requests=polls=syncs=0;
}
static void submitted(struct agx_context *c,unsigned i,unsigned done){
 struct agx_batch *b=&c->batches.slots[i];b->completed=done;
 b->storage=mk(c->base.screen->windows_private,0x40000,0,1,10+i);
 b->expected_vertices=4ull*(262144+i);c->batches.submitted[0]|=1u<<i;
}
int main(void){
 for(unsigned kind=0;kind<2;++kind)for(unsigned enabled=0;enabled<2;++enabled){
  AGX_WIN32_ASAHI_BACKEND backend={0};backend.ReclaimBeforeBatch=enabled;
  struct agx_device d={&backend};struct agx_context c;init(&c,&d);
  /* A completed warm batch and a newer, still-pending counted render. */
  submitted(&c,0,1);submitted(&c,1,0);
  struct windows_bo *warm=c.batches.slots[0].storage,*pending=c.batches.slots[1].storage;
  uint64_t held=pending->Gpuva.Allocation;backend.Gpuva.Held=&held;backend.Gpuva.HeldCount=1;
  struct pipe_framebuffer_state key={kind};
  struct agx_batch *next=agx_get_batch_for_framebuffer(&c,&key);assert(next);
  if(enabled){
   assert(!paging_requests && "completed BO must be reclaimed BEFORE new allocation");
   assert(next->storage==warm);
  }else assert(paging_requests==1);
  assert(!syncs && agx_batch_is_submitted(&c.batches.slots[1]));
  assert(c.batches.slots[1].storage==pending && !c.batches.slots[1].resolved_vertices);
  assert(c.batches.slots[0].resolved_vertices==4ull*262144);
  /* A matching active framebuffer is returned without polling/cleanup. */
  unsigned old_polls=polls;assert(agx_get_batch_for_framebuffer(&c,&key)==next);
  assert(polls==old_polls);
  c.batches.slots[1].completed=1;backend.Gpuva.Held=NULL;backend.Gpuva.HeldCount=0;
  while(agx_cleanup_batches(&c)>=0){}
  assert(c.batches.slots[1].resolved_vertices==4ull*262145);
  free_made();
 }
 /* No completed work, unknown completion and failed cleanup cannot provide
  * storage. They preserve the normal allocation path; never spin or sync. */
 for(unsigned failure=0;failure<3;++failure){
  AGX_WIN32_ASAHI_BACKEND backend={0};backend.ReclaimBeforeBatch=1;
  struct agx_device d={&backend};struct agx_context c;init(&c,&d);
  submitted(&c,0,failure!=0);c.batches.slots[0].poll_failure=failure==1;
  c.batches.slots[0].cleanup_failure=failure==2;
  struct windows_bo *kept=c.batches.slots[0].storage;
  struct pipe_framebuffer_state key={1};assert(agx_get_batch_for_framebuffer(&c,&key));
  assert(paging_requests==1 && !syncs && polls<=2*AGX_MAX_BATCHES);
  assert(agx_batch_is_submitted(&c.batches.slots[0]) && c.batches.slots[0].storage==kept);
  assert(!c.batches.slots[0].resolved_vertices);free_made();
 }
 /* Clean all completed batches, including a completed slot after a pending
  * one. No batch can be reclaimed twice or still be active when reused. */
 AGX_WIN32_ASAHI_BACKEND backend={0};backend.ReclaimBeforeBatch=1;
 struct agx_device d={&backend};struct agx_context c;init(&c,&d);
 submitted(&c,0,1);submitted(&c,1,0);submitted(&c,2,1);
 struct pipe_framebuffer_state key={0};assert(agx_get_batch_for_framebuffer(&c,&key));
 assert(!paging_requests && backend.CacheCount==1 && c.batches.submitted[0]==2);
 assert(c.batches.slots[0].resolved_vertices && c.batches.slots[2].resolved_vertices);
 assert(!syncs);free_made();puts("Q21 early reclaim: PASS");
}
'''
        # The actual projected initializer still has late cleanup in Begin;
        # this guards the model's allocation-before-Begin setup against drift.
        init = body(self.native, 'agx_batch_init')
        self.assertLess(init.index('agx_pool_init('), init.index('AgxWin32AsahiBatchBegin('))
        self.assertLess(init.index('agx_encoder_allocate('), init.index('AgxWin32AsahiBatchBegin('))
        self.compile_run(code)

    def test_exact_owner_switch_and_initialization(self):
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        configure = body(owner, 'configure_batch_reclaim')
        self.assertIn('configure_batch_reclaim(owner);', owner)
        code = r'''
#include <assert.h>
#include <string.h>
struct AGX_D3D12_OWNER {struct {unsigned ReclaimBeforeBatch;} Backend;};
static const char *setting;
static unsigned GetEnvironmentVariableA(const char *name,char *out,unsigned capacity){
 assert(!strcmp(name,"APPLE_AGX_D3D12_RECLAIM_BEFORE_BATCH"));
 if(!setting)return 0;
 unsigned n=(unsigned)strlen(setting);if(n>=capacity)return n+1;
 memcpy(out,setting,n+1);return n;
}
''' + configure + r'''
int main(){
 const char *cases[]={nullptr,"","0","1","01","true","1 "," 1","-1"};
 for(const char *s:cases){setting=s;AGX_D3D12_OWNER o={};configure_batch_reclaim(&o);
  assert(o.Backend.ReclaimBeforeBatch==(s && !strcmp(s,"1")));}
}
'''
        self.compile_run(code, cxx=True)


if __name__ == '__main__':
    unittest.main()
