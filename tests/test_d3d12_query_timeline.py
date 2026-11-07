"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Query reuse currently synchronizes the GPU before its CPU reset, and timestamp
resource resolves fall back to a blocking CPU read. Replay the projected query
functions with queued (not completed) GPU operations: old storage must survive
reuse, and resolve must retain sources and register its destination without a
CPU result read. Allocation failures must preserve the live query.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import ROOT, project, function_body


class QueryTimeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='rc40-query-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.base = Path(cls.tmp.name)
        cls.out = project(cls.base/'out')
        if cls.out is None:
            raise unittest.SkipTest('reference Mesa tree not available')
        cls.source = (cls.out/'src/gallium/drivers/asahi/agx_query.c').read_text()

    def run_replay(self, definitions, main):
        prelude = (ROOT/'tests/d3d12_query_timeline_fixture.h').read_text()
        code = prelude
        for ret, name, args in definitions:
            code += '\nstatic '+ret+' '+name+'('+args+')\n'+function_body(self.source, name)+'\n'
        path = self.base/'replay.c'
        path.write_text(code+main)
        exe = self.base/'replay'
        run = subprocess.run(['clang','-std=c11','-DAPPLE_AGX_GPUVA_WINSYS','-fsanitize=address,undefined',
            '-I'+str(ROOT/'drivers/apple-agx/mesa/winsys'),str(path),'-o',str(exe)],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr)
        run = subprocess.run([str(exe)],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr)

    def test_reuse_versions_storage_without_waiting(self):
        self.run_replay([
            ('bool','agx_begin_query','struct pipe_context *pctx, struct pipe_query *pquery'),
        ], r'''
int main(void) {
 struct agx_context ctx={0}; struct agx_query q={0};
 q.type=PIPE_QUERY_PIPELINE_STATISTICS_SINGLE; q.index=0;
 for(unsigned i=0;i<128;i++)q.writer_generation[i]=UINT64_MAX;
 q.bo=new_bo(); q.ptr=(struct agx_ptr){q.bo->data,(uint64_t)(uintptr_t)q.bo->data};
 q.writer_generation[0]=1; ctx.batches.generation[0]=1;
 uint64_t *old=q.ptr.cpu; *old=0x100000001ull;
 assert(agx_begin_query(&ctx.base,(void *)&q));
 assert(waits==0); assert(*(uint64_t *)q.ptr.cpu==0);
 assert(q.ptr.cpu!=old && *old==0x100000001ull);
 assert(ctx.pipeline_statistics[0]==&q);
 ctx.batches.generation[0]++;
 AgxWin32AsahiQueryCleanup(&ctx);
 assert(!ctx.windows_retired_queries);
 agx_bo_unreference(NULL,q.bo);
 assert(!bos); return 0;
}
''')

    def test_timestamp_resolve_is_queued_with_source_and_destination_hazards(self):
        self.run_replay([
            ('bool','is_occlusion','struct agx_query *query'),
            ('enum query_copy_type','classify_query_type','enum pipe_query_type type'),
            ('unsigned','result_type_size','enum pipe_query_value_type result_type'),
            ('bool','agx_get_query_result_resource_gpu',
             'struct agx_context *ctx, struct agx_query *query, enum pipe_query_flags flags, enum pipe_query_value_type result_type, int index, struct pipe_resource *prsrc, unsigned offset'),
        ],r'''
int main(void) {
 struct agx_context ctx={0}; struct agx_query q={0}; struct pipe_resource dst={0};
 ctx.batch.ctx=&ctx; q.type=PIPE_QUERY_TIMESTAMP; q.windows_timestamp_seed=19;
 q.windows_timestamp_count=2;
 q.windows_timestamp_bos[0]=new_bo(); q.windows_timestamp_bos[1]=new_bo();
 q.bo=new_bo(); q.ptr=(struct agx_ptr){q.bo->data,(uint64_t)(uintptr_t)q.bo->data};
 *(uint64_t *)q.ptr.cpu=99999; /* CPU cleanup storage is not the GPU seed */
 assert(agx_get_query_result_resource_gpu(&ctx,&q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,&dst,24));
 assert(!waits && flushes==1 && dispatches==1 && writes==1);
 assert(ctx.batch.refs==2 && dst.words[3]==0); /* GPU has not run */
 q.windows_timestamp_bos[0]->data[0]=0x100000002ull;
 q.windows_timestamp_bos[1]->data[0]=0x100000009ull;
 run_gpu(); assert(dst.words[3]==0x100000009ull);
 for(unsigned i=0;i<2;i++) agx_bo_unreference(NULL,q.windows_timestamp_bos[i]);
 agx_bo_unreference(NULL,q.bo); release_batch(&ctx.batch); assert(!bos); return 0;
}
''')

    def test_resolve_reader_keeps_old_storage_on_rebegin_and_allocation_failure(self):
        self.run_replay([
            ('bool','is_occlusion','struct agx_query *query'),
            ('enum query_copy_type','classify_query_type','enum pipe_query_type type'),
            ('unsigned','result_type_size','enum pipe_query_value_type result_type'),
            ('bool','agx_get_query_result_resource_gpu',
             'struct agx_context *ctx, struct agx_query *query, enum pipe_query_flags flags, enum pipe_query_value_type result_type, int index, struct pipe_resource *prsrc, unsigned offset'),
            ('bool','agx_begin_query','struct pipe_context *pctx, struct pipe_query *pquery'),
        ],r'''
int main(void) {
 struct agx_context ctx={0};struct pipe_resource dst={0};ctx.batch.ctx=&ctx;ctx.batch.index=1;
 struct agx_query *q=(void *)agx_create_query(&ctx.base,PIPE_QUERY_PIPELINE_STATISTICS_SINGLE,0);
 uint64_t *old=q->ptr.cpu;*old=UINT64_MAX;ctx.batches.generation[1]=5;
 assert(agx_get_query_result_resource_gpu(&ctx,q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,&dst,24));
 assert(q->writer_generation[1]==5);assert(!waits);
 allocation_failure=true;assert(!agx_begin_query(&ctx.base,(void *)q));
 assert(q->ptr.cpu==old && *old==UINT64_MAX && !ctx.pipeline_statistics[0]);
 allocation_failure=false;assert(agx_begin_query(&ctx.base,(void *)q));
 assert(q->ptr.cpu!=old && *old==UINT64_MAX && !waits);
 run_gpu();assert(dst.words[3]==UINT64_MAX); /* no 32-bit truncation */
 ctx.batches.generation[1]++;AgxWin32AsahiQueryCleanup(&ctx);
 release_batch(&ctx.batch);agx_destroy_query(&ctx.base,(void *)q);assert(!bos);return 0;
}
''')

    def test_timestamp_end_reuse_cannot_change_already_queued_snapshot(self):
        self.run_replay([
            ('bool','is_occlusion','struct agx_query *query'),
            ('enum query_copy_type','classify_query_type','enum pipe_query_type type'),
            ('unsigned','result_type_size','enum pipe_query_value_type result_type'),
            ('bool','agx_get_query_result_resource_gpu',
             'struct agx_context *ctx, struct agx_query *query, enum pipe_query_flags flags, enum pipe_query_value_type result_type, int index, struct pipe_resource *prsrc, unsigned offset'),
            ('bool','agx_end_query','struct pipe_context *pctx, struct pipe_query *pquery'),
        ],r'''
int main(void) {
 struct agx_context ctx={0};struct pipe_resource dst={0};ctx.batch.ctx=&ctx;
 struct agx_query *q=(void *)agx_create_query(&ctx.base,PIPE_QUERY_TIMESTAMP,0);
 struct agx_bo *first=new_bo(),*second=new_bo();attach_source=first;clock_seed=17;
 assert(agx_end_query(&ctx.base,(void *)q));
 assert(agx_get_query_result_resource_gpu(&ctx,q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,&dst,24));
 attach_source=second;clock_seed=9999;
 assert(agx_end_query(&ctx.base,(void *)q));assert(!waits);
 first->data[0]=91;second->data[0]=10001;run_gpu();assert(dst.words[3]==91);
 ctx.batches.generation[0]++;AgxWin32AsahiQueryCleanup(&ctx);release_batch(&ctx.batch);
 agx_destroy_query(&ctx.base,(void *)q);agx_bo_unreference(NULL,first);agx_bo_unreference(NULL,second);
 assert(!bos);return 0;
}
''')

    def test_failed_gpu_resolve_never_enters_cpu_fallback(self):
        self.run_replay([
            ('bool','is_occlusion','struct agx_query *query'),
            ('enum query_copy_type','classify_query_type','enum pipe_query_type type'),
            ('unsigned','result_type_size','enum pipe_query_value_type result_type'),
            ('bool','agx_get_query_result_resource_gpu',
             'struct agx_context *ctx, struct agx_query *query, enum pipe_query_flags flags, enum pipe_query_value_type result_type, int index, struct pipe_resource *prsrc, unsigned offset'),
            ('void','agx_get_query_result_resource',
             'struct pipe_context *pipe, struct pipe_query *q, enum pipe_query_flags flags, enum pipe_query_value_type result_type, int index, struct pipe_resource *resource, unsigned offset'),
        ],r'''
int main(void) {
 for(unsigned failure=0;failure<2;failure++) {
  struct agx_context ctx={0};struct pipe_resource dst={0};ctx.batch.ctx=&ctx;
  struct agx_query *q=(void *)agx_create_query(&ctx.base,PIPE_QUERY_TIMESTAMP,0);
  q->windows_timestamp_count=1;q->windows_timestamp_bos[0]=new_bo();
  compute_failure=failure==0;upload_failure=failure==1;
  agx_get_query_result_resource(&ctx.base,(void *)q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,&dst,24);
  assert(ctx.any_faults && !waits && !dispatches && !writes);
  release_batch(&ctx.batch);agx_destroy_query(&ctx.base,(void *)q);assert(!bos);
 }
 return 0;
}
''')


if __name__=='__main__': unittest.main()
