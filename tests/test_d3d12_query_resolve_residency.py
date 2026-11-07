"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1945: GPU query resolve passes a raw source VA without a batch BO reference.
A newly reused Query BO can be trimmed before resolve and never made resident.
Execute the projected resolve with modeled batch/platform boundaries, the real
trim policy and GPUVA submit/retire implementation. Both dedicated queries and
the shared occlusion heap must be restored and held through the consuming job.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import project, function_body, ROOT

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "agx_win32_gpuva.h"
#include "umd_residency_trim.h"
enum pipe_query_type { PIPE_QUERY_OCCLUSION_COUNTER, PIPE_QUERY_OCCLUSION_PREDICATE,
 PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE, PIPE_QUERY_TIMESTAMP,
 PIPE_QUERY_TIME_ELAPSED, PIPE_QUERY_PIPELINE_STATISTICS_SINGLE,
 PIPE_QUERY_SO_OVERFLOW_PREDICATE, PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE };
enum pipe_query_value_type { PIPE_QUERY_TYPE_U32, PIPE_QUERY_TYPE_I32, PIPE_QUERY_TYPE_U64 };
enum pipe_query_flags { PIPE_QUERY_WAIT=1 };
enum query_copy_type { QUERY_COPY_NORMAL, QUERY_COPY_BOOL32, QUERY_COPY_BOOL64,
 QUERY_COPY_TIMESTAMP, QUERY_COPY_TIME_ELAPSED };
#define AGX_BARRIER_ALL 0
struct agx_bo { unsigned id; };
struct pipe_resource { struct agx_bo *bo; };
struct agx_resource { struct agx_bo *bo; };
struct agx_ptr { void *cpu; uint64_t gpu; };
struct agx_query { enum pipe_query_type type; struct agx_bo *bo; struct agx_ptr ptr; };
struct agx_oq_heap { struct agx_bo *bo; };
struct agx_context;
struct agx_batch { struct agx_context *ctx; unsigned mask; };
struct agx_context { struct agx_oq_heap *oq; struct agx_batch batch; bool any_faults; };
static AGX_WIN32_GPUVA_BO maps[4];
static ADMISSION_UMD_TRIM_SLOT slots[4];
static unsigned source, dispatched, paging_waits, restored;
static uint64_t next_fence;
static void flush_query_writers(struct agx_context *c,struct agx_query *q,const char *s) {(void)c;(void)q;(void)s;}
static const char *util_str_query_type(unsigned t,bool b) {(void)t;(void)b;return "query";}
static struct agx_resource *agx_resource(struct pipe_resource *p) {return (void *)p;}
static struct agx_batch *agx_get_compute_batch(struct agx_context *c) {return &c->batch;}
static void agx_batch_init_state(struct agx_batch *b) {(void)b;}
static void agx_dirty_all(struct agx_context *c) {(void)c;}
static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo) {assert(bo);b->mask|=1u<<bo->id;}
static void agx_batch_writes_range(struct agx_batch *b,struct agx_resource *r,unsigned o,unsigned n) {
 assert(o==8 && n==8);agx_batch_add_bo(b,r->bo);
}
static uint64_t agx_map_gpu(struct agx_resource *r) {return maps[r->bo->id].Va;}
static unsigned agx_1d(unsigned x) {return x;}
static void libagx_copy_query_gl(struct agx_batch *b,unsigned x,unsigned barrier,uint64_t src,
 uint64_t dst,enum pipe_query_value_type t,unsigned bool_size) {
 (void)b;(void)barrier;(void)bool_size;assert(x==1 && t==PIPE_QUERY_TYPE_U64);
 assert(src==maps[source].Va && dst==maps[3].Va+8);++dispatched;
}
static unsigned trim(void) {
 unsigned chosen[4];unsigned long long bytes;
 unsigned n=AdmissionUmdTrimChoose(slots,4,1ull<<30,chosen,&bytes);
 for(unsigned i=0;i<n;++i) slots[chosen[i]].Resident=0;
 return n;
}
static int resident(void *ctx,const uint64_t *tokens,unsigned n,uint64_t *fence) {
 (void)ctx;
 for(unsigned i=0;i<n;++i) {
  unsigned k=(unsigned)tokens[i]-1;assert(k<4);
  restored+=!slots[k].Resident;slots[k].Resident=1;++slots[k].SubmissionHolds;
 }
 *fence=7;return 2;
}
static int wait_paging(void *ctx,uint64_t fence) {(void)ctx;assert(fence==7);++paging_waits;return 1;}
static int submit(void *ctx,const uint64_t *written,unsigned n,uint64_t va,uint32_t bytes,
 const void *data,uint32_t len,uint64_t *fence) {
 (void)ctx;(void)data;assert(n==1 && written[0]==4 && va==maps[0].Va && bytes && len);
 assert(paging_waits);
 /* The actual raw-address consumer must have backing, not just a live BO. */
 assert(slots[source].Resident && slots[source].SubmissionHolds);
 assert(slots[3].Resident && slots[3].SubmissionHolds);
 trim();assert(slots[source].Resident && slots[3].Resident);
 *fence=++next_fence;return 1;
}
static int wait_render(void *ctx,uint64_t fence) {(void)ctx;assert(fence==next_fence);return 1;}
static int release(void *ctx,const uint64_t *tokens,unsigned n) {
 (void)ctx;for(unsigned i=0;i<n;++i) {unsigned k=(unsigned)tokens[i]-1;
 assert(slots[k].SubmissionHolds);--slots[k].SubmissionHolds;}return 1;
}
'''
MAIN = r'''
int main(int argc,char **argv) {
 assert(argc==3);unsigned type=atoi(argv[1]),early=atoi(argv[2]);
 struct agx_bo bos[4]={{0},{1},{2},{3}};struct agx_oq_heap oq={&bos[2]};
 struct agx_context ctx={0};ctx.oq=&oq;ctx.batch.ctx=&ctx;
 source=type<=PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE?2:1;
 for(unsigned i=0;i<4;++i) {
  maps[i]=(AGX_WIN32_GPUVA_BO){i+1,0x100000ull+i*0x10000ull,0x10000,1};
  slots[i].Active=slots[i].Resident=1;slots[i].KernelAllocation=i+1;slots[i].Bytes=0x10000;
 }
 struct agx_query q={type,source==2?NULL:&bos[1],{NULL,maps[source].Va}};
 struct pipe_resource dst={&bos[3]};
 AGX_WIN32_GPUVA_SPACE space={0};space.Ops.MakeResident=resident;space.Ops.WaitPaging=wait_paging;
 space.Ops.Submit=submit;space.Ops.WaitRender=wait_render;space.Ops.Evict=release;
 for(unsigned pass=0;pass<2;++pass) {
  ctx.batch.mask=1; // command BO is always referenced by the winsys
  if(early) {trim();assert(!slots[source].Resident);}
  assert(agx_get_query_result_resource_gpu(&ctx,&q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,&dst,8));
  trim();assert(!slots[source].Resident); // encoded, not yet submitted
  const AGX_WIN32_GPUVA_BO *refs[4],*written[]={&maps[3]};unsigned n=0;
  for(unsigned i=0;i<4;++i)if(ctx.batch.mask&(1u<<i))refs[n++]=&maps[i];
  uint64_t fence=0;unsigned private_data=1;
  assert(AgxWin32GpuvaSubmit(&space,refs,n,&maps[0],4,written,1,&private_data,4,&fence));
  assert(AgxWin32GpuvaHoldingFence(&space,maps[source].Allocation)==fence);
  assert(AgxWin32GpuvaRetire(&space,fence));
  assert(!slots[source].SubmissionHolds);trim();assert(!slots[source].Resident);
 }
 assert(dispatched==2 && paging_waits==2 && restored>=6);
 // CPU fallback paths must not launch a GPU query copy.
 q.type=PIPE_QUERY_TIMESTAMP;
 assert(!agx_get_query_result_resource_gpu(&ctx,&q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,&dst,8));
 q.type=PIPE_QUERY_TIME_ELAPSED;
 assert(!agx_get_query_result_resource_gpu(&ctx,&q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,&dst,8));
 assert(!agx_get_query_result_resource_gpu(&ctx,&q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,-1,&dst,8));
 assert(dispatched==2);return 0;
}
'''

class QueryResolveResidency(unittest.TestCase):
    def test_trim_before_query_resolve_restores_source_until_retirement(self):
        with tempfile.TemporaryDirectory(prefix='query-residency-') as td:
            p=Path(td);out=project(p/'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            text=(out/'src/gallium/drivers/asahi/agx_query.c').read_text()
            code=PRELUDE
            for ret,name,args in (
                ('bool','is_occlusion','struct agx_query *query'),
                ('enum query_copy_type','classify_query_type','enum pipe_query_type type'),
                ('unsigned','result_type_size','enum pipe_query_value_type result_type'),
                ('bool','agx_get_query_result_resource_gpu',
                 'struct agx_context *ctx, struct agx_query *query, enum pipe_query_flags flags, enum pipe_query_value_type result_type, int index, struct pipe_resource *prsrc, unsigned offset')):
                code+=f'\nstatic {ret} {name}({args})\n'+function_body(text,name)
            src=p/'replay.c';src.write_text(code+MAIN)
            winsys=ROOT/'drivers/apple-agx/mesa/winsys'
            build=subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',
                '-I'+str(winsys),'-I'+str(ROOT/'drivers/apple-agx/shared/include'),
                '-I'+str(ROOT/'drivers/apple-agx/render-admission/umd/include'),
                str(src),str(winsys/'agx_win32_gpuva.c'),'-o',str(p/'replay')],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            for kind in (0,1,2,5,6,7):
                for early in (0,1):
                    with self.subTest(kind=kind,trim_before_recording=early):
                        run=subprocess.run([str(p/'replay'),str(kind),str(early)],capture_output=True,text=True)
                        self.assertEqual(run.returncode,0,run.stderr)

if __name__=='__main__':
    unittest.main()
