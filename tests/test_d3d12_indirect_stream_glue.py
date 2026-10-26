"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The GPU stream must admit pipeline statistics, reject unsupported queries/GS/tess and invalid extents, flush prior
argument/count writers before graphics, reserve CDM capacity before entry,
and emit no VDM link on any allocation or launch failure. Execute production
admission/emission and the real kernel; mock only batch/allocator boundaries.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_indirect_stream import PROGRAM, WINSYS, local_tree
from test_d3d10_shader_fields_init import project

PRELUDE = PROGRAM[:PROGRAM.index('static void run(')].replace(
    '#include "agx_win32_indirect_stream.cl"',
    '#include "agx_win32_indirect_stream.cl"\n#undef NDEBUG\n#include <assert.h>')

GLUE = r'''
#include "gallium/include/pipe/p_defines.h"
struct pipe_resource {unsigned width0;};
struct agx_resource {struct pipe_resource base;uint64_t gpu;};
struct pipe_draw_info {unsigned mode,index_size;bool has_user_indices,primitive_restart;};
struct pipe_draw_indirect_info {struct pipe_resource *buffer,*indirect_draw_count;unsigned offset,draw_count,stride,indirect_draw_count_offset;};
struct shader {struct {unsigned system_values_read[16];} info;};
struct agx_device {unsigned chip;uint64_t shader_base;};
struct agx_pool {unsigned kind;};
struct agx_bo {unsigned unused;};
struct agx_ptr {void *cpu;uint64_t gpu;};
struct agx_encoder {void *bo;uint8_t *current,*end;struct agx_ptr windows_chunk;};
struct agx_batch;
struct agx_context {
 struct {struct agx_device *screen;} base;
 bool in_tess,active_draw_without_restart,any_faults,active_queries;
 struct {struct shader *shader;} stage[MESA_SHADER_STAGES];
 struct {unsigned num_targets;} streamout;
 void *occlusion_query,*time_elapsed,*cond_query,*tf_any_overflow;
 void *pipeline_statistics[12],*prims_generated[4],*tf_prims_generated[4],*tf_overflow[4];
 struct agx_batch *batch;unsigned windows_indirect_path;
};
struct agx_batch {struct agx_context *ctx;struct agx_pool pool,windows_stream_pool,windows_stream_usc_pool;struct agx_encoder cdm,vdm;};
static struct agx_device dev;
static unsigned allocations,fault,reads,launches,entered,leaves,writer_flushes,capacity_flushes,output_refs;
static struct agx_bo output_bos[2];
static unsigned query_refs;
static uint64_t agx_get_query_address(struct agx_batch *b,void *q){assert(b->ctx->active_queries&&q);++query_refs;if(fault==11)b->ctx->any_faults=true;return (uintptr_t)q;}
static bool passthrough;
static void *blocks[8];static uint8_t cdm[1024],vdm[256];
static struct agx_device *agx_device(struct agx_device *d){return d;}
static struct agx_resource *agx_resource(struct pipe_resource *r){return (void *)r;}
static uint64_t agx_map_gpu(struct agx_resource *r){return r->gpu;}
static unsigned agx_usc_addr(struct agx_device *d,uint64_t a){assert(a>=d->shader_base&&a-d->shader_base<=UINT32_MAX);return a-d->shader_base;}
static bool agx_needs_passthrough_gs(struct agx_context *c,const struct pipe_draw_info *i,void *in,bool *xfb){(void)c;(void)i;(void)in;(void)xfb;return passthrough;}
static bool agx_batch_is_compute(struct agx_batch *b){(void)b;return false;}
static void agx_flush_writer(struct agx_context *c,struct agx_resource *r,const char *s){assert(r&&s);++writer_flushes;if(fault==8)c->any_faults=true;}
static void agx_flush_batch_for_reason(struct agx_context *c,struct agx_batch *b,const char *s){assert(c==b->ctx&&s);++capacity_flushes;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned line){assert(line);c->any_faults=true;}
static struct agx_encoder agx_encoder_allocate(struct agx_batch *b,struct agx_device *d){assert(b&&d==&dev);if(fault==7)return (struct agx_encoder){0};return (struct agx_encoder){cdm,cdm,cdm+sizeof(cdm)};}
static void agx_batch_reads(struct agx_batch *b,struct agx_resource *r){assert(r);++reads;if(fault==9)b->ctx->any_faults=true;}
static struct agx_ptr agx_pool_alloc_aligned(struct agx_pool *pool,size_t size,unsigned alignment){
 ++allocations;if(allocations==fault)return (struct agx_ptr){0};
 assert(size&&allocations<8);void *p=NULL;assert(!posix_memalign(&p,alignment<8?8:alignment,size));
 blocks[allocations]=p;memset(p,0xcd,size);
 if(pool->kind==2)dev.shader_base=(uintptr_t)p-0x10000;
 return (struct agx_ptr){p,(uintptr_t)p};
}
static struct agx_ptr agx_pool_alloc_aligned_with_bo(struct agx_pool *pool,size_t size,unsigned alignment,struct agx_bo **bo){
 assert(pool->kind==1||pool->kind==2);
 if(pool->kind==1){assert(alignment==256);assert(size>=8+0x800);}
 struct agx_ptr p=agx_pool_alloc_aligned(pool,size,alignment);
 *bo=p.gpu?&output_bos[pool->kind-1]:NULL;p.cpu=NULL;return p;
}
static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo){assert(bo==&output_bos[output_refs++]);if(fault==10)b->ctx->any_faults=true;}
static uint64_t agx_pool_upload_aligned(struct agx_pool *pool,void *data,size_t size,unsigned alignment){struct agx_ptr p=agx_pool_alloc_aligned(pool,size,alignment);if(p.cpu)memcpy(p.cpu,data,size);return p.gpu;}
static bool AgxWin32AsahiBatchComputeEnter(struct agx_batch *b){assert(b);if(fault==4)return false;++entered;return true;}
static bool AgxWin32AsahiBatchComputeLeave(struct agx_batch *b){assert(b&&entered==1);++leaves;return fault!=6;}
static void launch(struct agx_batch *b,struct agx_grid grid,unsigned barrier,uint64_t args){
 assert(barrier==(AGX_BARRIER_ALL|AGX_PREGFX)&&entered==1);++launches;
 if(fault==5){b->ctx->any_faults=true;return;}
 for(unsigned i=0;i<grid.count[0];++i){cl_global_id.x=i;libagx_win32_indirect_stream((void *)(uintptr_t)args);}
}
#define libagx_win32_indirect_stream launch
#include "agx_win32_indirect_stream_emit.h"

static void init(struct agx_context *ctx,struct agx_batch *batch,struct shader *vs){
 *ctx=(struct agx_context){0};ctx->base.screen=&dev;ctx->batch=batch;ctx->active_queries=true;
 ctx->stage[MESA_SHADER_VERTEX].shader=vs;
 *batch=(struct agx_batch){.ctx=ctx,.windows_stream_pool={1},.windows_stream_usc_pool={2}};dev.chip=AGX_CHIP_G13G;
 batch->vdm=(struct agx_encoder){vdm,vdm,vdm+sizeof(vdm),{vdm,0x1234000000ull}};
}
int main(void){
 struct agx_context ctx;struct agx_batch batch;struct shader vs={0};
 struct pipe_draw_info info={.mode=MESA_PRIM_TRIANGLES,.index_size=4};
 init(&ctx,&batch,&vs);unsetenv("APPLE_AGX_D3D12_GPU_INDIRECT_STREAM");assert(agx_win32_stream_fallback(&ctx,&info,0,1)==7);
 setenv("APPLE_AGX_D3D12_GPU_INDIRECT_STREAM","1",1);assert(!agx_win32_stream_fallback(&ctx,&info,0,1));
 info.mode=MESA_PRIM_PATCHES;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==1);info.mode=MESA_PRIM_TRIANGLES;
 for(unsigned s=MESA_SHADER_TESS_CTRL;s<=MESA_SHADER_TESS_EVAL;++s){ctx.stage[s].shader=&vs;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==1);ctx.stage[s].shader=NULL;}
 ctx.stage[MESA_SHADER_GEOMETRY].shader=&vs;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==2);ctx.stage[MESA_SHADER_GEOMETRY].shader=NULL;
 ctx.streamout.num_targets=1;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==2);ctx.streamout.num_targets=0;
 for(unsigned i=0;i<12;++i){ctx.pipeline_statistics[i]=&vs;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==(i<=10?0:3));ctx.pipeline_statistics[i]=NULL;}
 ctx.pipeline_statistics[0]=&vs;info.primitive_restart=true;
 assert(agx_win32_stream_fallback(&ctx,&info,0,1)==3);
 info.primitive_restart=false;ctx.pipeline_statistics[0]=NULL;
 void **queries[]={&ctx.occlusion_query,&ctx.time_elapsed,&ctx.cond_query,&ctx.tf_any_overflow,&ctx.prims_generated[3],&ctx.tf_prims_generated[3],&ctx.tf_overflow[3]};
 for(unsigned i=0;i<ARRAY_SIZE(queries);++i){*queries[i]=&vs;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==3);*queries[i]=NULL;}
 vs.info.windows_uses_draw_id=true;
 assert(agx_win32_stream_fallback(&ctx,&info,0,1)==4);memset(&vs,0,sizeof(vs));
 assert(agx_win32_stream_fallback(&ctx,&info,1,1)==4);
 passthrough=true;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==5);passthrough=false;
 dev.chip=AGX_CHIP_G14G;assert(agx_win32_stream_fallback(&ctx,&info,0,1)==6);dev.chip=0;

 uint32_t value=2,args[32]={0};uint8_t tmpl[64]={0};
 struct agx_resource arguments={{sizeof(args)},(uintptr_t)args},counter={{4},(uintptr_t)&value};
 struct pipe_draw_indirect_info in={&arguments.base,&counter.base,4,3,32,0};
 assert(agx_win32_stream_inputs_valid(&info,&in));
 for(unsigned bad=0;bad<6;++bad){struct pipe_draw_indirect_info copy=in;
  if(bad==0)copy.offset=UINT32_MAX;if(bad==1)copy.stride=19;if(bad==2)copy.stride=22;
  if(bad==3)copy.draw_count=UINT32_MAX;if(bad==4)copy.indirect_draw_count_offset=1;if(bad==5)copy.draw_count=0;
  assert(!agx_win32_stream_inputs_valid(&info,&copy));}
 for(fault=0;fault<=10;++fault){
  init(&ctx,&batch,&vs);allocations=reads=launches=entered=leaves=writer_flushes=capacity_flushes=output_refs=0;
  agx_win32_stream_flush_inputs(&ctx,&in);assert(writer_flushes==(fault==8?1:2));
  if(fault==8)continue;
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};
  struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,true);
  uint32_t *out=(void *)(vdm+16);memset(out,0xff,32);
  uint8_t *end=agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)out);
  if(fault){assert(ctx.any_faults&&!end&&!ctx.windows_indirect_path&&out[0]==UINT32_MAX);}
  else{assert(!ctx.any_faults&&ctx.windows_indirect_path==3&&end==(uint8_t *)out+8);assert(allocations==3&&reads==2&&output_refs==2&&launches==1&&entered==leaves);assert((out[0]>>28)==8);
   const struct agx_win32_stream_params *p=blocks[3];
   assert(p->continuation==0x1234000018ull);
   const uint32_t *tail=(const void *)((const uint8_t *)blocks[1]+value*28);
   assert(tail[0]==0x80000012u&&tail[1]==0x34000018u);}
  if(entered)assert(leaves==1);
  for(unsigned i=0;i<8;++i){free(blocks[i]);blocks[i]=NULL;}
 }
 /* CPU emission performs exactly the same calls for 1 and 12000 records. */
 for(unsigned records=1;records<=12000;records+=11999){
  fault=0;init(&ctx,&batch,&vs);allocations=reads=launches=entered=leaves=output_refs=0;
  in.draw_count=records;value=records;
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};
  struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,true);
  uint32_t *words=(void *)(vdm+32);assert(agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)words));
  assert(allocations==3&&reads==2&&launches==1&&output_refs==2&&entered==leaves);
  for(unsigned i=0;i<8;++i){free(blocks[i]);blocks[i]=NULL;}
 }
 /* All five IA/VS/clipper counters use the query BO addresses; inactive
  * queries do not register or update any query. PS and absent-stage slots
  * remain untouched by this internal compute dispatch. */
 for(unsigned enabled=0;enabled<3;++enabled){
  fault=enabled==2?11:0;init(&ctx,&batch,&vs);ctx.active_queries=enabled!=0;
  allocations=reads=launches=entered=leaves=output_refs=query_refs=0;
  uint64_t stats[12]={0};for(unsigned k=0;k<11;++k)ctx.pipeline_statistics[k]=&stats[k];
  in.draw_count=3;value=2;args[1]=3;args[2]=2;args[9]=6;args[10]=3;
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12,MESA_PRIM_TRIANGLES};
  struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,false);
  uint32_t *words=(void *)(vdm+32);uint8_t *end=agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)words);
  if(enabled==2){assert(!end&&ctx.any_faults&&!launches);}
  else{assert(end&&!ctx.any_faults&&query_refs==(enabled?5:0));
   assert(stats[PIPE_STAT_QUERY_IA_VERTICES]==(enabled?24:0));
   assert(stats[PIPE_STAT_QUERY_VS_INVOCATIONS]==(enabled?24:0));
   assert(stats[PIPE_STAT_QUERY_IA_PRIMITIVES]==(enabled?8:0));
   assert(stats[PIPE_STAT_QUERY_C_PRIMITIVES]==(enabled?8:0));
   assert(stats[PIPE_STAT_QUERY_C_INVOCATIONS]==(enabled?8:0));
   assert(!stats[PIPE_STAT_QUERY_PS_INVOCATIONS]&&!stats[PIPE_STAT_QUERY_CS_INVOCATIONS]&&!stats[PIPE_STAT_QUERY_GS_INVOCATIONS]);}
  for(unsigned i=0;i<8;++i){free(blocks[i]);blocks[i]=NULL;}
 }
 fault=0;init(&ctx,&batch,&vs);batch.cdm=(struct agx_encoder){cdm,cdm+sizeof(cdm)-1,cdm+sizeof(cdm)};
 capacity_flushes=0;agx_win32_stream_flush_inputs(&ctx,&in);assert(capacity_flushes==1);
 /* Constant-time size check rejects a pipeline allocation wrap before allocating. */
 in.draw_count=UINT32_MAX;allocations=0;
 struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};
 uint32_t *out=(void *)(vdm+48);struct agx_draw d=agx_draw_indirect((uintptr_t)args);
 assert(!agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)out));
 assert(ctx.any_faults&&!allocations);
 return 0;
}
'''

class IndirectStreamGlue(unittest.TestCase):
    def test_production_fallback_hazard_capacity_and_failure_paths(self):
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)
            out=project(p/'native')
            state=(out/'src/gallium/drivers/asahi/agx_state.h').read_text()
            shader=state[state.index('struct agx_uncompiled_shader {'):]
            info=shader[shader.index('   struct {'):shader.index('   } info;')+10]
            glue=GLUE.replace('struct shader {struct {unsigned system_values_read[16];} info;};',
                              'struct shader {\n'+info+'\n};')
            (p/'test.c').write_text(PRELUDE+glue)
            cmd=['clang','-std=c11','-Werror','-Wno-unused-function','-Wno-c23-extensions',
                 '-fsanitize=address,undefined','-DNDEBUG','-Dstatic_assert=_Static_assert',
                 '-DHAVE___BUILTIN_CLZ','-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD']
            cmd+=['-I'+str(x) for x in (WINSYS,mesa/'src',mesa/'include',build/'src',
                                      mesa/'src/asahi/libagx',build/'src/asahi/genxml')]
            r=subprocess.run(cmd+[str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

if __name__=='__main__':unittest.main()
