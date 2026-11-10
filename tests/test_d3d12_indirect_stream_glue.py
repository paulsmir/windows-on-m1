"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The GPU stream must admit pipeline statistics, reject unsupported queries/GS/tess and invalid extents, flush prior
argument/count writers before graphics, reserve CDM capacity before entry,
and emit no VDM link on any allocation or launch failure. rc79: the grid is
MaxCount + 1 unless APPLE_AGX_D3D12_STREAM_THREADS names a valid smaller
bound, and the bounded grid produces the same records and statistics. rc89:
outputs, grid, terminator and output checks follow the capacity the scratch
policy returns (not MaxCount), the kernel gets the feedback words, and the
feedback BO is ensured before graphics entry unless
APPLE_AGX_D3D12_STREAM_CAPACITY=0. Execute production
admission/emission and the real kernel; mock only batch/allocator boundaries.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_indirect_stream import PROGRAM, WINSYS, local_tree
from test_d3d10_shader_fields_init import project, ROOT

PRELUDE = PROGRAM[:PROGRAM.index('static void run(')].replace(
    '#include "agx_win32_indirect_stream.cl"',
    '#include "agx_win32_indirect_stream.cl"\n#undef NDEBUG\n#include <assert.h>')

GLUE = r'''
#include "gallium/include/pipe/p_defines.h"
struct pipe_resource {unsigned width0;};
struct agx_resource {struct pipe_resource base;uint64_t gpu;struct agx_bo *bo;};
struct pipe_draw_info {unsigned mode,index_size;bool has_user_indices,primitive_restart;};
struct pipe_draw_indirect_info {struct pipe_resource *buffer,*indirect_draw_count;unsigned offset,draw_count,stride,indirect_draw_count_offset;};
struct shader {struct {unsigned system_values_read[16];} info;};
struct agx_device {unsigned chip;uint64_t shader_base;};
struct agx_pool {unsigned kind;};
struct agx_va {uint64_t addr,size_B;};
struct agx_bo {struct agx_va *va;size_t size;};
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
 struct agx_bo *windows_stream_feedback;
 /* PF1 (agx_win32_expand_ahead.h) */
 unsigned windows_indirect_fold,windows_fold_counts[20];
};
struct agx_batch {struct agx_context *ctx;struct agx_pool pool,windows_stream_pool,windows_stream_usc_pool;struct agx_encoder cdm,vdm;
 /* IE1a (agx_win32_expand_ahead.h); test_active/test_submitted model the batch bitsets */
 struct agx_batch *windows_expand,*windows_expand_owner;unsigned windows_expand_dispatches;bool test_active,test_submitted;
 /* PF1 (agx_win32_expand_ahead.h) */
 bool windows_expand_fold;unsigned windows_fold_marks,windows_fold_cause;};
#define AGX_MAX_BATCHES 128
static bool agx_batch_is_active(struct agx_batch *b){return b->test_active;}
static bool agx_batch_is_submitted(struct agx_batch *b){return b->test_submitted;}
static void agx_batch_reset(struct agx_context *c,struct agx_batch *b);
static void agx_flush_batch(struct agx_context *c,struct agx_batch *b);
static struct agx_batch *agx_get_batch(struct agx_context *c);
static struct agx_device dev;
static unsigned allocations,fault,reads,launches,entered,leaves,writer_flushes,capacity_flushes,output_refs;
static struct agx_bo output_bos[2];
static struct agx_va output_vas[2];
static unsigned bad_extent;
static unsigned query_refs;
static uint64_t agx_get_query_address(struct agx_batch *b,void *q){assert(b->ctx->active_queries&&q);++query_refs;if(fault==11)b->ctx->any_faults=true;return (uintptr_t)q;}
static bool passthrough;
static void *blocks[64];static uint8_t cdm[1024],vdm[4096];
static struct agx_device *agx_device(struct agx_device *d){return d;}
static struct agx_resource *agx_resource(struct pipe_resource *r){return (void *)r;}
static uint64_t agx_map_gpu(struct agx_resource *r){return r->gpu;}
static unsigned agx_usc_addr(struct agx_device *d,uint64_t a){assert(a>=d->shader_base&&a-d->shader_base<=UINT32_MAX);return a-d->shader_base;}
static bool agx_needs_passthrough_gs(struct agx_context *c,const struct pipe_draw_info *i,void *in,bool *xfb){(void)c;(void)i;(void)in;(void)xfb;return passthrough;}
static bool agx_batch_is_compute(struct agx_batch *b){(void)b;return false;}
static void agx_flush_writer(struct agx_context *c,struct agx_resource *r,const char *s){assert(r&&s);++writer_flushes;if(fault==8)c->any_faults=true;}
static void agx_flush_batch_for_reason(struct agx_context *c,struct agx_batch *b,const char *s){assert(c==b->ctx&&s);++capacity_flushes;if(b->windows_expand_owner)agx_flush_batch(c,b);}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned line){assert(line);c->any_faults=true;}
static struct agx_encoder agx_encoder_allocate(struct agx_batch *b,struct agx_device *d){assert(b&&d==&dev);if(fault==7)return (struct agx_encoder){0};return (struct agx_encoder){cdm,cdm,cdm+sizeof(cdm)};}
static void agx_batch_reads(struct agx_batch *b,struct agx_resource *r){assert(r);++reads;if(fault==9)b->ctx->any_faults=true;}
static struct agx_ptr agx_pool_alloc_aligned(struct agx_pool *pool,size_t size,unsigned alignment){
 ++allocations;if(allocations==fault)return (struct agx_ptr){0};
 assert(size&&allocations<64);void *p=NULL;assert(!posix_memalign(&p,alignment<8?8:alignment,size));
 blocks[allocations]=p;memset(p,0xcd,size);
 if(pool->kind==2)dev.shader_base=(uintptr_t)p-0x10000;
 return (struct agx_ptr){p,(uintptr_t)p};
}
static struct agx_ptr agx_pool_alloc_aligned_with_bo(struct agx_pool *pool,size_t size,unsigned alignment,struct agx_bo **bo){
 assert(pool->kind==1||pool->kind==2);
 if(pool->kind==1){assert(alignment==256);assert(size>=8+0x800);}
 struct agx_ptr p=agx_pool_alloc_aligned(pool,size,alignment);
 unsigned n=pool->kind-1;
 *bo=p.gpu?&output_bos[n]:NULL;
 if(p.gpu){
  p.gpu=0x1200000000ull+n*0x100000;
  output_vas[n]=(struct agx_va){p.gpu,size};
  output_bos[n]=(struct agx_bo){&output_vas[n],size};
  if(pool->kind==2)dev.shader_base=p.gpu-0x10000;
  if(bad_extent==1&&n==0)--output_bos[n].size;
  if(bad_extent==2&&n==1)--output_vas[n].size_B;
  if(bad_extent==3&&n==0)p.gpu=output_vas[n].addr=(1ull<<39)-256;
  if(bad_extent==4&&n==0)p.gpu+=4;
  if(bad_extent==5&&n==1)p.gpu+=4;
  if(bad_extent==6&&n==0)output_vas[n].addr=p.gpu+256;
  if(bad_extent==7&&n==1)output_bos[n].va=NULL;
  if(bad_extent==8&&n==1)dev.shader_base=p.gpu-(1ull<<32)+64;
 }
 p.cpu=NULL;return p;
}
static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo){assert(bo==&output_bos[output_refs++ % 2]);if(fault==10)b->ctx->any_faults=true;}
static uint64_t agx_pool_upload_aligned(struct agx_pool *pool,const void *data,size_t size,unsigned alignment){struct agx_ptr p=agx_pool_alloc_aligned(pool,size,alignment);if(p.cpu)memcpy(p.cpu,data,size);return p.gpu;}
static bool AgxWin32AsahiBatchComputeEnter(struct agx_batch *b){assert(b);if(fault==4)return false;++entered;return true;}
static bool AgxWin32AsahiBatchComputeLeave(struct agx_batch *b){assert(b&&entered==1);++leaves;return fault!=6;}
static unsigned last_grid;
static void launch(struct agx_batch *b,struct agx_grid grid,unsigned barrier,uint64_t args){
 assert(barrier==(AGX_BARRIER_ALL|AGX_PREGFX)&&entered==1);++launches;last_grid=grid.count[0];
 if(fault==5){b->ctx->any_faults=true;return;}
 struct agx_win32_stream_params params=*(struct agx_win32_stream_params *)(uintptr_t)args;
 params.stream=(uintptr_t)blocks[allocations-2];params.pipelines=(uintptr_t)blocks[allocations-1];
 for(unsigned i=0;i<grid.count[0];++i){cl_global_id.x=i;libagx_win32_indirect_stream(&params);}
}
/* rc89: the capacity policy and feedback words are agx_win32_stream_scratch.h's;
 * test_d3d12_rc89_stream_capacity.py runs them. Here the policy returns
 * forced_capacity when feedback exists. */
#define AGX_WIN32_STREAM_FB_MAX_COUNT 0u
#define AGX_WIN32_STREAM_FB_OVERFLOWS 1u
#define AGX_WIN32_STREAM_FB_DISPATCHES 2u
#define AGX_WIN32_STREAM_FB_WORDS 4u
static unsigned forced_capacity,feedback_ensures,below_notes;static uint32_t feedback_words[4];
static void AgxWin32PerfNote(const char *f,...){assert(f);if(strstr(f,"capacity below"))++below_notes;}
static void agx_win32_stream_feedback_ensure(struct agx_context *c){assert(c);++feedback_ensures;}
static uint64_t agx_win32_stream_feedback(struct agx_batch *b){assert(b);return forced_capacity?(uintptr_t)feedback_words:0;}
static unsigned agx_win32_stream_capacity(const struct agx_context *c,unsigned max,uint64_t fb){assert(c);return fb&&forced_capacity<max?forced_capacity:max;}
/* RC71: production takes each output from agx_win32_stream_scratch
 * (agx_state.h); its reuse is tested in test_rc71_stream_scratch.py. */
static struct agx_ptr agx_win32_stream_scratch(struct agx_batch *b,unsigned kind,size_t size,struct agx_bo **bo){
 return agx_pool_alloc_aligned_with_bo(kind?&b->windows_stream_usc_pool:&b->windows_stream_pool,size,kind?64:256,bo);}
#define libagx_win32_indirect_stream launch
/* IE1a: off unless APPLE_AGX_D3D12_EXPAND_AHEAD=1; the projected batch
 * functions are modelled here (test_d3d12_indirect_stream_projection.py). */
#include "agx_win32_expand_ahead.h"
static unsigned get_batches,expand_opens,template_copies,batch_resets;
static struct agx_batch *expand_next,*flush_log[16];static unsigned flushes,aborts,fail_expand_flush;
static struct agx_batch *agx_get_batch(struct agx_context *c){++get_batches;return c->batch;}
struct agx_batch *agx_win32_expand_open(struct agx_context *c,struct agx_batch *r){
 assert(c==r->ctx);++expand_opens;if(!expand_next)return NULL;
 expand_next->windows_expand_owner=r;r->windows_expand=expand_next;return expand_next;}
const void *agx_win32_expand_template_cpu(struct agx_batch *r,uint64_t va,size_t n){
 assert(r&&!r->windows_expand_owner&&va&&n);++template_copies;return (const void *)(uintptr_t)va;}
/* PF1: the projected agx_batch.c primitives (open compute batch, recorded
 * writers, rc105 scan, output adoption) as fixed answers */
static struct agx_batch *fold_wb,*fold_target_writer,*fold_writer[2];static struct pipe_resource *fold_written[2];
static bool fold_render_writes;static unsigned fold_adopts;
struct agx_batch *agx_win32_fold_compute(struct agx_context *c){(void)c;return fold_wb&&fold_wb->test_active&&!fold_wb->test_submitted?fold_wb:NULL;}
bool agx_win32_fold_writer(struct agx_context *c,struct agx_batch *wb,struct pipe_resource *r){(void)c;
 for(unsigned i=0;i<2;++i)if(r&&wb&&r==fold_written[i]&&wb==fold_writer[i])return true;return false;}
bool agx_win32_fold_target(struct agx_context *c,struct agx_batch *wb){(void)c;return wb&&wb==fold_target_writer;}
bool agx_win32_fold_render_writes(struct agx_context *c,struct agx_batch *wb,struct agx_batch *r){(void)c;(void)wb;(void)r;return fold_render_writes;}
void agx_win32_fold_adopt(struct agx_batch *wb,struct agx_batch *r){assert(wb&&r&&wb!=r);++fold_adopts;}
static void agx_batch_reset(struct agx_context *c,struct agx_batch *b){
 (void)c;assert(b->test_active);++batch_resets;b->test_active=false;agx_win32_expand_cleanup(c,b);}
/* The projected agx_flush_batch: its rc105 block starts with the hook; a
 * faulted context aborts the batch (never submits it); a failed expansion
 * submission faults the context (1) or, modelled defect, stays open (2). */
static void agx_flush_batch(struct agx_context *c,struct agx_batch *b){
 if(!b->test_active||b->test_submitted)return;
 if(agx_win32_expand_flush_first(c,b))return;
 if(c->any_faults){++aborts;b->test_active=false;return;}
 if(b->windows_expand_owner&&fail_expand_flush==1){c->any_faults=true;++aborts;b->test_active=false;return;}
 if(b->windows_expand_owner&&fail_expand_flush==2)return;
 assert(flushes<16);flush_log[flushes++]=b;
 b->test_active=false;b->test_submitted=true;}
#include "agx_win32_indirect_stream_emit.h"

/* RC69: the complete native VS block agx_encode_state captured for the template. */
static void native_vs(struct agx_win32_indirect_stream *s){
 uint32_t *o=s->vs_state;
 agx_push(o,VDM_STATE,cfg){cfg.vertex_shader_word_0_present=true;cfg.vertex_shader_word_1_present=true;
  cfg.vertex_outputs_present=true;cfg.vertex_unknown_present=true;}
 o[0]=0x1111u;o[1]=0x2222u;o[2]=0x0808u;o[3]=0x2u;o[4]=0;s->vs_state_valid=true;
}
static void init(struct agx_context *ctx,struct agx_batch *batch,struct shader *vs){
 *ctx=(struct agx_context){0};ctx->base.screen=&dev;ctx->batch=batch;ctx->active_queries=true;
 ctx->stage[MESA_SHADER_VERTEX].shader=vs;
 *batch=(struct agx_batch){.ctx=ctx,.windows_stream_pool={1},.windows_stream_usc_pool={2}};dev.chip=AGX_CHIP_G13G;
 batch->vdm=(struct agx_encoder){vdm,vdm,vdm+sizeof(vdm),{vdm,0x1234000000ull}};
}
int main(void){
 struct agx_context ctx;struct agx_batch batch;struct shader vs={0};
 struct pipe_draw_info info={.mode=MESA_PRIM_TRIANGLES,.index_size=4};
 init(&ctx,&batch,&vs);unsetenv("APPLE_AGX_D3D12_GPU_INDIRECT_STREAM");assert(!agx_win32_stream_fallback(&ctx,&info,0,1));
 setenv("APPLE_AGX_D3D12_GPU_INDIRECT_STREAM","0",1);assert(agx_win32_stream_fallback(&ctx,&info,0,1)==7);
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
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};native_vs(&stream);
  struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,true);
  uint32_t *out=(void *)(vdm+16);memset(out,0xff,32);
  uint8_t *end=agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)out);
  if(fault){assert(ctx.any_faults&&!end&&!ctx.windows_indirect_path&&out[0]==UINT32_MAX);}
  else{assert(!ctx.any_faults&&ctx.windows_indirect_path==3&&end==(uint8_t *)out+8);assert(allocations==3&&reads==2&&output_refs==2&&launches==1&&entered==leaves);assert((out[0]>>28)==8);
   const struct agx_win32_stream_params *p=blocks[3];
   assert(p->continuation==0x1234000018ull);
   const uint32_t *tail=(const void *)((const uint8_t *)blocks[1]+value*agx_win32_stream_record_bytes(4));
   assert(tail[0]==0x80000012u&&tail[1]==0x34000018u);}
  if(entered)assert(leaves==1);
  for(unsigned i=0;i<8;++i){free(blocks[i]);blocks[i]=NULL;}
 }
 /* RC43: full GPU output ownership/VA and continuation fetch bounds must
  * be checked before dispatch; a bad allocation must never emit a link. */
 for(bad_extent=1;bad_extent<=11;++bad_extent){
  fault=0;init(&ctx,&batch,&vs);allocations=reads=launches=entered=leaves=output_refs=0;
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};native_vs(&stream);
  struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,false);
  if(bad_extent==9)batch.vdm.end=vdm+24;
  if(bad_extent==10)batch.vdm.windows_chunk.gpu=(1ull<<39)-16;
  if(bad_extent==11)batch.vdm.windows_chunk.gpu+=1;
  uint32_t *out=(void *)(vdm+16);memset(out,0xff,32);
  uint8_t *end=agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)out);
  assert(ctx.any_faults&&!end&&!launches&&!entered&&!ctx.windows_indirect_path&&out[0]==UINT32_MAX);
  for(unsigned i=0;i<8;++i){free(blocks[i]);blocks[i]=NULL;}
 }
 bad_extent=0;
 /* RC69: never repeat a missing, word-1-only or unpadded VS block per record. */
 for(unsigned bad=0;bad<4;++bad){
  fault=0;init(&ctx,&batch,&vs);allocations=reads=launches=entered=leaves=output_refs=0;
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};native_vs(&stream);
  if(bad==0)stream.vs_state_valid=false;
  if(bad==1)stream.vs_state[0]&=~2u;  /* word 0 absent */
  if(bad==2)stream.vs_state[0]|=1u;   /* restart index shifts the words */
  if(bad==3)stream.vs_state[5]=1u;    /* pad is not zero */
  struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,true);
  uint32_t *out=(void *)(vdm+16);memset(out,0xff,32);
  assert(!agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)out));
  assert(ctx.any_faults&&!allocations&&!launches&&!entered&&out[0]==UINT32_MAX);
 }
 /* CPU emission performs exactly the same calls for 1 and 12000 records. */
 for(unsigned records=1;records<=12000;records+=11999){
  fault=0;init(&ctx,&batch,&vs);allocations=reads=launches=entered=leaves=output_refs=0;
  in.draw_count=records;value=records;
  /* RC69: the kernel copies every executed record's draw parameters. */
  uint32_t *record_args=calloc(records,32);assert(record_args);
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};native_vs(&stream);
  struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)record_args+4,0x1000,256,AGX_INDEX_SIZE_U32,true);
  uint32_t *words=(void *)(vdm+32);assert(agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,(void *)words));
  assert(allocations==3&&reads==2&&launches==1&&output_refs==2&&entered==leaves);
  for(unsigned i=0;i<8;++i){free(blocks[i]);blocks[i]=NULL;}
  free(record_args);
 }
 /* All five IA/VS/clipper counters use the query BO addresses; inactive
  * queries do not register or update any query. PS and absent-stage slots
  * remain untouched by this internal compute dispatch. */
 for(unsigned enabled=0;enabled<3;++enabled){
  fault=enabled==2?11:0;init(&ctx,&batch,&vs);ctx.active_queries=enabled!=0;
  allocations=reads=launches=entered=leaves=output_refs=query_refs=0;
  uint64_t stats[12]={0};for(unsigned k=0;k<11;++k)ctx.pipeline_statistics[k]=&stats[k];
  in.draw_count=3;value=2;args[1]=3;args[2]=2;args[9]=6;args[10]=3;
  struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12,MESA_PRIM_TRIANGLES};native_vs(&stream);
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

 /* RC57: execute real generator/emitter for GM-sized arguments, with five
  * statistics counters, optional count, and seven streams in one batch.
  * Keep each allocation alive until all continuations have been checked.
  * rc79: also with APPLE_AGX_D3D12_STREAM_THREADS=1024, whose bounded grid
  * must give the same records, links and statistics. */
 const unsigned maxima[]={2400,12000,60000};
 for(unsigned bounded=0;bounded<2;++bounded)
 for(unsigned mi=0;mi<3;++mi)for(unsigned counted=0;counted<2;++counted){
  if(bounded)setenv("APPLE_AGX_D3D12_STREAM_THREADS","1024",1);
  else unsetenv("APPLE_AGX_D3D12_STREAM_THREADS");
  unsigned records=maxima[mi];uint32_t *large=calloc(records,20);assert(large);
  for(unsigned i=0;i<records;++i){large[5*i]=6;large[5*i+1]=1;large[5*i+3]=i;}
  struct agx_resource input={{records*20},(uintptr_t)large};
  struct pipe_draw_indirect_info many={&input.base,counted?&counter.base:NULL,0,records,20,0};
  value=records;assert(agx_win32_stream_inputs_valid(&info,&many));
  fault=0;init(&ctx,&batch,&vs);allocations=reads=launches=output_refs=query_refs=0;
  uint64_t stats[12]={0};for(unsigned k=0;k<11;++k)ctx.pipeline_statistics[k]=&stats[k];
  for(unsigned call=0;call<7;++call){
   entered=leaves=0;
   struct agx_win32_indirect_stream stream={&many,(uintptr_t)tmpl,sizeof(tmpl),8,12,MESA_PRIM_TRIANGLES};native_vs(&stream);
   struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)large,0x1000,256,AGX_INDEX_SIZE_U32,false);
   uint8_t *out=vdm+32+call*8;
   assert(agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,out)==out+8);
   assert(!ctx.any_faults&&ctx.windows_indirect_path==3&&entered==leaves);
   const struct agx_win32_stream_params *p=blocks[3*call+3];
   assert(p->max_count==records&&p->continuation==0x1234000028ull+call*8);
   assert(p->threads==(bounded?1024u:0u)&&last_grid==(bounded?1024u:records+1u));
   const uint32_t *tail=(void *)((uint8_t *)blocks[3*call+1]+records*agx_win32_stream_record_bytes(4));
   assert(tail[0]==0x80000012u&&tail[1]==0x34000028u+call*8);
  }
  assert(allocations==21&&launches==7&&output_refs==14&&query_refs==35);
  assert(stats[PIPE_STAT_QUERY_IA_VERTICES]==42ull*records);
  assert(stats[PIPE_STAT_QUERY_VS_INVOCATIONS]==42ull*records);
  assert(stats[PIPE_STAT_QUERY_IA_PRIMITIVES]==14ull*records);
  assert(stats[PIPE_STAT_QUERY_C_INVOCATIONS]==14ull*records);
  assert(stats[PIPE_STAT_QUERY_C_PRIMITIVES]==14ull*records);
  for(unsigned i=0;i<64;++i){free(blocks[i]);blocks[i]=NULL;}
  free(large);
 }
 unsetenv("APPLE_AGX_D3D12_STREAM_THREADS");
 /* rc89: GravityMark-shaped draws (MaxCount 60000) with outputs of 16
  * records: sized, launched, linked and checked by capacity, never by
  * MaxCount, for GPU counts below, at and above the capacity. The mock
  * pool's ASan-sized blocks catch any write past the outputs. */
 { const unsigned gpu_counts[]={0,3,16,17,150,60000};
   for(unsigned bounded=0;bounded<2;++bounded)
   for(unsigned k=0;k<ARRAY_SIZE(gpu_counts);++k){
    if(bounded)setenv("APPLE_AGX_D3D12_STREAM_THREADS","32",1);else unsetenv("APPLE_AGX_D3D12_STREAM_THREADS");
    const unsigned records=60000;uint32_t *large=calloc(records,20);assert(large);
    for(unsigned i=0;i<records;++i){large[5*i]=6;large[5*i+1]=1;large[5*i+3]=i;}
    struct agx_resource input={{records*20},(uintptr_t)large};
    struct pipe_draw_indirect_info many={&input.base,&counter.base,0,records,20,0};
    value=gpu_counts[k];forced_capacity=16;memset(feedback_words,0,sizeof(feedback_words));below_notes=0;
    fault=0;init(&ctx,&batch,&vs);allocations=reads=launches=entered=leaves=output_refs=0;
    struct agx_win32_indirect_stream stream={&many,(uintptr_t)tmpl,sizeof(tmpl),8,12,MESA_PRIM_TRIANGLES};native_vs(&stream);
    struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)large,0x1000,256,AGX_INDEX_SIZE_U32,false);
    uint8_t *out=vdm+32;
    assert(agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,out)==out+8);
    assert(!ctx.any_faults&&ctx.windows_indirect_path==3&&launches==1&&below_notes==1);
    const struct agx_win32_stream_params *p=blocks[3];
    assert(p->max_count==records&&p->capacity==16&&p->feedback==(uintptr_t)feedback_words);
    assert(output_vas[0].size_B==16u*agx_win32_stream_record_bytes(4)+AGX_VDM_STREAM_LINK_LENGTH+AGX_WIN32_STREAM_FETCH_PADDING);
    assert(output_vas[1].size_B==16u*agx_win32_stream_usc_stride(sizeof(tmpl),true));
    assert(p->threads==0&&last_grid==17u); /* 32 >= 16 + 1 */
    unsigned written=value<16?value:16;
    const uint32_t *tail=(void *)((uint8_t *)blocks[1]+written*agx_win32_stream_record_bytes(4));
    assert(tail[0]==0x80000012u&&tail[1]==0x34000028u);
    assert(feedback_words[0]==value&&feedback_words[1]==(value>16)&&feedback_words[2]==1);
    for(unsigned i=0;i<64;++i){free(blocks[i]);blocks[i]=NULL;}
    free(large);
   }
   unsetenv("APPLE_AGX_D3D12_STREAM_THREADS");
   /* A bounded grid is a bound below capacity + 1, not below MaxCount + 1. */
   forced_capacity=4096;setenv("APPLE_AGX_D3D12_STREAM_THREADS","1024",1);
   { const unsigned records=60000;uint32_t *large=calloc(records,20);assert(large);
     struct agx_resource input={{records*20},(uintptr_t)large};
     struct pipe_draw_indirect_info many={&input.base,&counter.base,0,records,20,0};
     value=5000;fault=0;init(&ctx,&batch,&vs);allocations=reads=launches=entered=leaves=output_refs=0;
     struct agx_win32_indirect_stream stream={&many,(uintptr_t)tmpl,sizeof(tmpl),8,12,MESA_PRIM_TRIANGLES};native_vs(&stream);
     struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)large,0x1000,256,AGX_INDEX_SIZE_U32,false);
     assert(agx_win32_emit_indirect_stream(&batch,&stream,d,AGX_PRIMITIVE_TRIANGLES,vdm+32));
     const struct agx_win32_stream_params *p=blocks[3];
     assert(p->capacity==4096&&p->threads==1024&&last_grid==1024);
     for(unsigned i=0;i<64;++i){free(blocks[i]);blocks[i]=NULL;}
     free(large); }
   unsetenv("APPLE_AGX_D3D12_STREAM_THREADS");forced_capacity=0;
   /* The feedback BO is ensured before graphics entry unless disabled. */
   feedback_ensures=0;unsetenv("APPLE_AGX_D3D12_STREAM_CAPACITY");fault=0;init(&ctx,&batch,&vs);
   agx_win32_stream_flush_inputs(&ctx,&in);assert(feedback_ensures==1);
   setenv("APPLE_AGX_D3D12_STREAM_CAPACITY","0",1);agx_win32_stream_flush_inputs(&ctx,&in);assert(feedback_ensures==1);
   setenv("APPLE_AGX_D3D12_STREAM_CAPACITY","1",1);agx_win32_stream_flush_inputs(&ctx,&in);assert(feedback_ensures==2);
   unsetenv("APPLE_AGX_D3D12_STREAM_CAPACITY"); }
 /* rc79: only a decimal 32..65536 below MaxCount + 1 bounds the grid. */
 { const char *bad[]={"","abc","16","31","65537","99999999999","1024x"};
   for(unsigned k=0;k<ARRAY_SIZE(bad);++k){setenv("APPLE_AGX_D3D12_STREAM_THREADS",bad[k],1);
     assert(agx_win32_stream_threads(60000)==0);}
   setenv("APPLE_AGX_D3D12_STREAM_THREADS","32",1);assert(agx_win32_stream_threads(60000)==32);
   setenv("APPLE_AGX_D3D12_STREAM_THREADS","65536",1);assert(agx_win32_stream_threads(65536)==65536);
   assert(agx_win32_stream_threads(65535)==0);
   setenv("APPLE_AGX_D3D12_STREAM_THREADS","1024",1);assert(agx_win32_stream_threads(1023)==0);
   assert(agx_win32_stream_threads(1024)==1024);
   unsetenv("APPLE_AGX_D3D12_STREAM_THREADS");assert(agx_win32_stream_threads(60000)==0); }
 fault=0;init(&ctx,&batch,&vs);batch.cdm=(struct agx_encoder){cdm,cdm+sizeof(cdm)-1,cdm+sizeof(cdm)};
 capacity_flushes=0;agx_win32_stream_flush_inputs(&ctx,&in);assert(capacity_flushes==1);
 /* Constant-time size check rejects a pipeline allocation wrap before allocating. */
 in.draw_count=UINT32_MAX;allocations=0;
 struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12};native_vs(&stream);
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


# PF1 (APPLE_AGX_D3D12_EXPAND_FOLD, agx_win32_expand_ahead.h): the glue
# harness with the IE1a recording stubs, plus a writer map that the mock
# hazard paths (agx_flush_writer, agx_batch_reads) flush through the
# production deferral, and a log of the work each batch receives.
FOLD_STUBS = {
    'static void agx_flush_writer(struct agx_context *c,struct agx_resource *r,const char *s){assert(r&&s);++writer_flushes;if(fault==8)c->any_faults=true;}':
    'static struct agx_batch *writer_of(struct agx_resource *r);\n'
    'static bool fold_defers(struct agx_context *c,struct agx_batch *w,struct agx_batch *r,unsigned cause);\n'
    'static void note_work(struct agx_batch *b,char kind);\n'
    'static const void *last_args,*last_stream,*last_usc;\n'
    'static void agx_flush_writer(struct agx_context *c,struct agx_resource *r,const char *s){assert(r&&s);++writer_flushes;\n'
    ' struct agx_batch *w=writer_of(r);if(w&&w->test_active&&!w->test_submitted&&!fold_defers(c,w,NULL,5u))agx_flush_batch(c,w);}',
    'static void agx_batch_reads(struct agx_batch *b,struct agx_resource *r){assert(r);++reads;if(fault==9)b->ctx->any_faults=true;}':
    'static void agx_batch_reads(struct agx_batch *b,struct agx_resource *r){assert(r);++reads;\n'
    ' struct agx_batch *w=writer_of(r);if(w&&w!=b&&w->test_active&&!w->test_submitted&&!fold_defers(b->ctx,w,b,6u))agx_flush_batch(b->ctx,w);}',
    'launch_batch=b;launch_barrier=barrier;++launches;':
    'launch_batch=b;launch_barrier=barrier;++launches;note_work(b,\'K\');last_args=(const void *)(uintptr_t)args;\n'
    ' last_stream=blocks[allocations-2];last_usc=blocks[allocations-1];',
    'static struct agx_batch *agx_get_batch(struct agx_context *c){++get_batches;return c->batch;}':
    'static struct agx_batch *render_next;\n'
    'static struct agx_batch *agx_get_batch(struct agx_context *c){++get_batches;if(render_next)c->batch=render_next;return c->batch;}',
}
FOLD_MAIN = r"""/* PF1 harness main, appended to the glue harness prefix. Built with
 * -DPF1_BASELINE against the base commit's headers it prints the switch-off
 * lines only, which must equal this tree's. */
#ifdef PF1_BASELINE
static bool fold_defers(struct agx_context *c,struct agx_batch *w,struct agx_batch *r,unsigned cause){(void)c;(void)w;(void)r;(void)cause;return false;}
#else
static bool fold_defers(struct agx_context *c,struct agx_batch *w,struct agx_batch *r,unsigned cause){return agx_win32_fold_defers(c,w,r,cause);}
#endif
static uint8_t ecdm[3][1024];
static struct agx_bo argbo,cntbo,fbbo;
static uint32_t *fixed_words;
#define args fixed_words
#define value fixed_words[64]
static uint8_t tmpl[64];
static struct agx_resource arguments,counter;
static struct pipe_draw_indirect_info in;
static struct agx_batch *work_batch[32];static char work_kind[32];static unsigned works;
static void note_work(struct agx_batch *b,char kind){assert(works<32);work_batch[works]=b;work_kind[works++]=kind;}
static struct agx_batch *writer_of(struct agx_resource *r){
 for(unsigned i=0;i<2;++i)if(&r->base==fold_written[i])return fold_writer[i];return NULL;}
static char last_work(struct agx_batch *b){char k=0;for(unsigned i=0;i<works;++i)if(work_batch[i]==b)k=work_kind[i];return k;}
static unsigned work_count(struct agx_batch *b,char kind){unsigned n=0;for(unsigned i=0;i<works;++i)n+=work_batch[i]==b&&work_kind[i]==kind;return n;}
static uint64_t fnv(const void *p,size_t n,uint64_t h){const uint8_t *b=p;for(size_t i=0;i<n;++i){h^=b[i];h*=0x100000001b3ull;}return h;}
static struct agx_context ctx;static struct agx_batch r,wb,wb2;static struct shader vs;
static void reset_counts(void){
 allocations=reads=launches=entered=leaves=writer_flushes=capacity_flushes=output_refs=0;
 get_batches=expand_opens=template_copies=batch_resets=flushes=aborts=fail_expand_flush=0;
 launch_batch=scratch_batch=NULL;launch_barrier=0;works=0;fold_adopts=0;last_args=last_stream=last_usc=NULL;
 for(unsigned i=0;i<64;++i){free(blocks[i]);blocks[i]=NULL;}
}
static void expansion(struct agx_batch *e,unsigned slab){
 *e=(struct agx_batch){.ctx=&ctx,.test_active=true};
 e->cdm=(struct agx_encoder){&ecdm[slab],ecdm[slab],ecdm[slab]+sizeof(ecdm[slab])};
}
/* R open (a render batch), Wb the open compute batch, the last command a
 * dispatch (ctx->batch = Wb) that wrote the arguments. */
static void setup(void){
 reset_counts();init(&ctx,&r,&vs);r.test_active=true;ctx.windows_stream_feedback=&fbbo;
 expansion(&wb,0);fold_wb=&wb;render_next=&r;ctx.batch=&wb;
 fold_written[0]=&arguments.base;fold_writer[0]=&wb;fold_written[1]=&counter.base;fold_writer[1]=NULL;
 fold_target_writer=NULL;fold_render_writes=false;note_work(&wb,'A');
}
/* agx_get_compute_batch (its PF1 hook) and one application dispatch */
static struct agx_batch *app_dispatch(struct agx_batch *fresh,unsigned slab){
#ifndef PF1_BASELINE
 agx_win32_fold_dispatch(&ctx);
#endif
 struct agx_batch *c=agx_win32_fold_compute(&ctx);
 if(!c){expansion(fresh,slab);fold_wb=c=fresh;}
 ctx.batch=c;note_work(c,'A');return c;
}
static uint8_t *draw(uint8_t *out){
 entered=leaves=0;
 struct agx_win32_indirect_stream stream={&in,(uintptr_t)tmpl,sizeof(tmpl),8,12,MESA_PRIM_TRIANGLES};native_vs(&stream);
 struct agx_draw d=agx_draw_indexed_indirect((uintptr_t)args+4,0x1000,256,AGX_INDEX_SIZE_U32,true);
 return agx_win32_emit_indirect_stream(&r,&stream,d,AGX_PRIMITIVE_TRIANGLES,out);
}
static const char *name(const struct agx_batch *b){return b==&r?"R":b==&wb?"Wb":b==&wb2?"Wb2":"?";}
/* the emitted parameters (template address and host pointers aside), VDM
 * link, generated records and USC clones of the last stream draw */
static uint64_t emitted(const uint8_t *out){
 struct agx_win32_stream_params q=*(const struct agx_win32_stream_params *)last_args;
 q.usc_template=0;q.feedback=0;
 uint64_t h=fnv(&q,sizeof(q),0xcbf29ce484222325ull);
 h=fnv(out,8,h);
 h=fnv(last_stream,value*agx_win32_stream_record_bytes(4)+AGX_VDM_STREAM_LINK_LENGTH,h);
 return fnv(last_usc,value*agx_win32_stream_usc_stride(sizeof(tmpl),true),h);
}
static void flush_line(const char *tag,unsigned k){
 printf("%s %u flushes",tag,k);for(unsigned i=0;i<flushes;++i)printf(" %s",name(flush_log[i]));
 /* DRAW (9): the seal skipped the argument flush; R's flush submits Wb */
 printf(" writer_flushes %u reads %u launch %s barrier %x\n",k==9?0u:writer_flushes,reads,name(launch_batch),launch_barrier);
}
#ifndef PF1_BASELINE
#define COUNT(slot) ctx.windows_fold_counts[slot]
#endif

/* Test 1: Wb is submitted before R and the kernels are Wb's last work. */
static void order(bool on){
 setup();
 agx_win32_stream_flush_inputs(&ctx,&in);
 assert(!ctx.any_faults&&(!on||ctx.batch==&r));
 if(!on){assert(flushes==1&&flush_log[0]==&wb&&writer_flushes==2);}
#ifndef PF1_BASELINE
 else{assert(!flushes&&writer_flushes==1&&r.windows_expand==&wb&&wb.windows_expand_owner==&r);
  assert(wb.windows_expand_fold&&COUNT(AGX_WIN32_FOLD_SEALS)==1);}
#endif
 for(unsigned call=0;call<2;++call){
  if(call)agx_win32_stream_flush_inputs(&ctx,&in);
  uint8_t *out=vdm+32+8*call;assert(draw(out)==out+8&&!ctx.any_faults);
 }
 if(!on){assert(launch_batch==&r&&launch_barrier==(AGX_BARRIER_ALL|AGX_PREGFX)&&r.cdm.bo&&work_count(&r,'K')==2);}
#ifndef PF1_BASELINE
 else{
  assert(launch_batch==&wb&&launch_barrier==AGX_BARRIER_ALL&&!r.cdm.bo);
  assert(!flushes&&COUNT(AGX_WIN32_FOLD_SEALS)==1&&COUNT(AGX_WIN32_FOLD_KERNELS)==2);
  assert(COUNT(AGX_WIN32_FOLD_DEFERRED)==2&&ctx.windows_indirect_fold==1);  /* R's argument reads */
  assert(wb.windows_expand_dispatches==2&&template_copies==2);
  /* the fold marks the pair for the D3D12 owner */
  assert(agx_win32_fold_kind(&r)==AGX_WIN32_FOLD_KIND_FLAG&&agx_win32_fold_kind(&wb)==AGX_WIN32_FOLD_KIND_FLAG);
  assert(agx_win32_fold_follows(&wb,&r)&&!agx_win32_fold_follows(&r,&wb));
 }
#endif
 agx_flush_batch(&ctx,&r);
 assert(flushes==2&&flush_log[0]==&wb&&flush_log[1]==&r&&!ctx.any_faults);
 if(on){assert(last_work(&wb)=='K'&&work_count(&wb,'K')==2&&work_count(&wb,'A')==1&&work_batch[0]==&wb);
  for(unsigned i=1;i<works;++i)assert(work_kind[i]=='K');}
#ifndef PF1_BASELINE
 if(on)assert(COUNT(AGX_WIN32_FOLD_CAUSED+AGX_WIN32_FOLD_CAUSE_RENDER)==1&&!r.windows_expand&&!wb.windows_expand_fold);
#endif
 puts("ORDER PASS");
}

/* Test 2: an application Dispatch after the seal never joins Wb. */
static void seal(bool on){
 setup();
 agx_win32_stream_flush_inputs(&ctx,&in);assert(draw(vdm+32));
 struct agx_batch *c=app_dispatch(&wb2,1);
 assert(c==&wb2&&wb.test_submitted&&last_work(&wb)==(on?'K':'A')&&work_count(&wb,'A')==1);
 assert(flushes==1&&flush_log[0]==&wb&&wb2.test_active&&!wb2.test_submitted);
#ifndef PF1_BASELINE
 if(on)assert(COUNT(AGX_WIN32_FOLD_CAUSED+AGX_WIN32_FOLD_CAUSE_DISPATCH)==1&&!r.windows_expand);
#endif
 /* R's next counted draw: Wb2 wrote nothing it reads, so it keeps the prelude */
 agx_win32_stream_flush_inputs(&ctx,&in);assert(draw(vdm+40)&&launch_batch==&r);
 assert(launch_barrier==(AGX_BARRIER_ALL|AGX_PREGFX));
#ifndef PF1_BASELINE
 if(on)assert(ctx.windows_indirect_fold==(0x100u|AGX_WIN32_FOLD_REFUSE_NOT_WRITER));
#endif
 agx_flush_batch(&ctx,&r);
 assert(flushes==2&&flush_log[1]==&r&&wb2.test_active&&!wb2.test_submitted); /* the dispatch stays after R */
 puts("SEAL PASS");
}

/* Test 3: a refused fold (or the switch off) emits today's in-render form;
 * a fold emits the same parameters and stream from Wb. */
static void fallback(bool on){
 for(unsigned k=0;k<=AGX_WIN32_FOLD_COUNTS_REFUSALS;++k){
  if(k==AGX_WIN32_FOLD_OTHER_RENDER)continue;   /* own scenario below */
  setup();
  uint64_t *stat=(uint64_t *)(fixed_words+128);*stat=0;
  if(k==1)ctx.pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES]=stat;   /* QUERY */
  if(k==2){fold_wb=NULL;fold_writer[0]=NULL;}                        /* NO_COMPUTE */
  if(k==3)fold_writer[0]=NULL;                                        /* NOT_WRITER */
  if(k==4)r.cdm=(struct agx_encoder){cdm,cdm,cdm+sizeof(cdm)};       /* PRELUDE */
  if(k==5)wb.cdm.current=wb.cdm.end-8;                                /* CAPACITY */
  if(k==7)fold_render_writes=true;                                    /* ORDER */
  if(k==8){render_next=NULL;ctx.batch=NULL;}                          /* LOST: no render */
  agx_win32_stream_flush_inputs(&ctx,&in);
  if(k==9)wb.cdm.current=wb.cdm.end-8;                                /* DRAW: no room left */
  uint8_t *out=vdm+32;assert(draw(out)==out+8&&!ctx.any_faults);
  if(k){assert(launch_batch==&r&&r.cdm.bo&&((const struct agx_win32_stream_params *)last_args)->usc_template==(uintptr_t)tmpl);}
  else if(on){assert(launch_batch==&wb&&!memcmp((const void *)(uintptr_t)((const struct agx_win32_stream_params *)last_args)->usc_template,tmpl,sizeof(tmpl)));}
#ifndef PF1_BASELINE
  if(on&&k){assert(COUNT(AGX_WIN32_FOLD_REFUSED+k)==1&&ctx.windows_indirect_fold==(0x100u|k));
   for(unsigned i=1;i<=AGX_WIN32_FOLD_REFUSE_DRAW;++i)assert(i==k||!COUNT(AGX_WIN32_FOLD_REFUSED+i));}
  if(!on)for(unsigned i=0;i<AGX_WIN32_FOLD_COUNTS;++i)assert(!ctx.windows_fold_counts[i]&&!ctx.windows_indirect_fold);
#endif
  agx_flush_batch(&ctx,&r);
  printf("%s %u %016llx\n",k?"PRELUDE":"FOLDED",k,(unsigned long long)emitted(out));
  if(k)flush_line("FLUSH",k);
  ctx.pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES]=NULL;
 }
 puts("FALLBACK PASS");
}

#ifndef PF1_BASELINE
/* Test 4: R reset with a sealed Wb leaves Wb submittable with its own work;
 * a Wb without kernels is submitted, never reset. Test: another render. */
static void reset(void){
 setup();
 agx_win32_stream_flush_inputs(&ctx,&in);assert(draw(vdm+32)&&launch_batch==&wb);
 agx_batch_reset(&ctx,&r);
 assert(batch_resets==1&&!r.test_active&&wb.test_active&&!wb.test_submitted);
 assert(!wb.windows_expand_fold&&!wb.windows_expand_owner&&fold_adopts==1);
 assert(COUNT(AGX_WIN32_FOLD_CAUSED+AGX_WIN32_FOLD_CAUSE_RESET)==1);
 agx_flush_batch(&ctx,&wb);
 assert(flushes==1&&flush_log[0]==&wb&&wb.test_submitted&&batch_resets==1);
 assert(work_count(&wb,'A')==1&&work_count(&wb,'K')==1);
 /* sealed, but its render's draw took the prelude: Wb keeps its work */
 setup();
 agx_win32_stream_flush_inputs(&ctx,&in);wb.cdm.current=wb.cdm.end-8;assert(draw(vdm+32)&&launch_batch==&r);
 assert(!wb.windows_expand_dispatches);
 agx_flush_batch(&ctx,&r);
 assert(!batch_resets&&flushes==2&&flush_log[0]==&wb&&flush_log[1]==&r&&wb.test_submitted);
 /* sealed for R: another render's counted draw flushes Wb (Wb before both) */
 setup();
 agx_win32_stream_flush_inputs(&ctx,&in);assert(draw(vdm+32)&&launch_batch==&wb);
 struct agx_batch r2=r;r2.windows_expand=NULL;r2.windows_fold_marks=0;r2.cdm=(struct agx_encoder){0};
 render_next=&r2;
 agx_win32_stream_flush_inputs(&ctx,&in);
 assert(COUNT(AGX_WIN32_FOLD_REFUSED+AGX_WIN32_FOLD_REFUSE_OTHER_RENDER)==1);
 assert(flushes==1&&flush_log[0]==&wb&&!r.windows_expand&&COUNT(AGX_WIN32_FOLD_CAUSED+AGX_WIN32_FOLD_CAUSE_RESOURCE)==1);
 /* the dispatch hook leaves an unsealed compute batch alone */
 setup();ctx.batch=&wb;app_dispatch(&wb2,1);assert(!flushes&&work_count(&wb,'A')==2);
 puts("RESET PASS");
}
#endif

int main(int argc,char **argv){
 const bool on=argc>2&&!strcmp(argv[1],"on");
 const char *what=argc>2?argv[2]:"";
 if(on)setenv("APPLE_AGX_D3D12_EXPAND_FOLD","1",1);else unsetenv("APPLE_AGX_D3D12_EXPAND_FOLD");
 unsetenv("APPLE_AGX_D3D12_EXPAND_AHEAD");
#ifndef PF1_BASELINE
 assert(agx_win32_expand_fold_enabled()==on&&!agx_win32_expand_ahead_enabled());
 /* read once */
 setenv("APPLE_AGX_D3D12_EXPAND_FOLD",on?"0":"1",1);assert(agx_win32_expand_fold_enabled()==on);
#endif
 void *page=mmap((void *)0x200000000000ull,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(page==(void *)0x200000000000ull);fixed_words=page;value=3;
 for(unsigned i=0;i<sizeof(tmpl);++i)tmpl[i]=(uint8_t)(i*7u+1u);
 arguments=(struct agx_resource){{128},(uintptr_t)args,&argbo};
 counter=(struct agx_resource){{4},(uintptr_t)&value,&cntbo};
 in=(struct pipe_draw_indirect_info){&arguments.base,&counter.base,4,3,32,0};
 for(unsigned i=0;i<3;++i){args[1+8*i]=3;args[2+8*i]=1;args[4+8*i]=i;}
 forced_capacity=16;
 if(!strcmp(what,"order"))order(on);
 else if(!strcmp(what,"seal"))seal(on);
 else if(!strcmp(what,"fallback"))fallback(on);
#ifndef PF1_BASELINE
 else if(!strcmp(what,"reset")&&on)reset();
#endif
 else return 2;
 reset_counts();
 return 0;
}
"""


class ExpandFold(unittest.TestCase):
    """PF1 (APPLE_AGX_D3D12_EXPAND_FOLD): production admission, emission,
    seal and flush hooks with the real kernel; batch primitives mocked."""

    @classmethod
    def build(cls, td, headers=None, baseline=False):
        from test_d3d12_indirect_stream_projection import IE1A_STUBS
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        if not mesa or not build:
            raise unittest.SkipTest('reference Mesa tree not available')
        out=project(td/('native-base' if baseline else 'native'))
        state=(out/'src/gallium/drivers/asahi/agx_state.h').read_text()
        shader=state[state.index('struct agx_uncompiled_shader {'):]
        info=shader[shader.index('   struct {'):shader.index('   } info;')+10]
        prefix=GLUE[:GLUE.index('int main(void){')].replace(
            'struct shader {struct {unsigned system_values_read[16];} info;};','struct shader {\n'+info+'\n};')
        for stubs in (IE1A_STUBS, FOLD_STUBS):
            for old,new in stubs.items():
                assert prefix.count(old)==1,old
                prefix=prefix.replace(old,new)
        main=FOLD_MAIN.replace('AGX_WIN32_FOLD_COUNTS_REFUSALS','9u').replace(
            'AGX_WIN32_FOLD_OTHER_RENDER','6u')
        source=td/('baseline.c' if baseline else 'fold.c')
        source.write_text(PRELUDE+prefix+'#include <sys/mman.h>\n'+main)
        exe=source.with_suffix('')
        cmd=['clang','-std=c11','-Werror','-Wno-unused-function','-Wno-c23-extensions',
             '-fsanitize=address,undefined','-DNDEBUG','-Dstatic_assert=_Static_assert',
             '-DHAVE___BUILTIN_CLZ','-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD']
        if baseline: cmd.append('-DPF1_BASELINE')
        dirs=([headers] if headers else [])+[WINSYS,mesa/'src',mesa/'include',build/'src',
                                             mesa/'src/asahi/libagx',build/'src/asahi/genxml']
        cmd+=['-I'+str(x) for x in dirs]
        return subprocess.run(cmd+[str(source),'-o',str(exe)],capture_output=True,text=True),exe

    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='pf1-fold-')
        cls.td=Path(cls.tmp.name)
        cls.built,cls.exe=cls.build(cls.td)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_mode(self, mode, what, exe=None):
        self.assertEqual(self.built.returncode,0,self.built.stderr[-6000:])
        r=subprocess.run([str(exe or self.exe),mode,what],capture_output=True,text=True)
        self.assertEqual(r.returncode,0,mode+' '+what+': '+r.stdout+r.stderr[-3000:])
        return r.stdout

    def test_order_compute_batch_submits_first_with_the_kernel_last(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A render that runs before the compute batch holding its stream kernel
        executes last frame's (or garbage) records -- the rc105 flicker class.
        Wb must be submitted strictly before R, with the folded kernels as its
        last work, R getting no CDM, both batches marked for the D3D12 owner,
        and R's own argument reads deferred instead of flushing Wb."""
        for mode in ('off','on'):
            self.assertIn('ORDER PASS',self.run_mode(mode,'order'))

    def test_seal_dispatch_after_the_render_never_joins_the_compute_batch(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A Dispatch recorded after the render's counted draw that lands in Wb
        would run before the render (Wb is submitted first): compute work
        reordered across the render. A new dispatch must first submit the
        sealed Wb, go to a new batch that stays behind R, and R's later
        counted draws keep the in-render prelude."""
        for mode in ('off','on'):
            self.assertIn('SEAL PASS',self.run_mode(mode,'seal'))

    def test_fallback_equals_the_in_render_form(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A silent change of the default path: with the switch off, or a fold
        refused for any reason, the parameters, VDM link, generated records,
        USC clones, barrier, launching batch and flush sequence must equal
        today's in-render prelude; a taken fold must generate the same
        parameters and stream from Wb."""
        off=self.run_mode('off','fallback').splitlines()
        on=self.run_mode('on','fallback').splitlines()
        self.assertEqual(off[-1],'FALLBACK PASS')
        self.assertEqual(len([l for l in off if l.startswith('PRELUDE ')]),8)
        self.assertEqual(off,on)

    def test_switch_off_is_byte_identical_to_the_base_commit(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Any PF1 hunk that leaks into the default path: the same harness built
        against the base commit's emitter and IE1a header (e81d8dbfe, before
        PF1) must print exactly the switch-off lines of this tree."""
        base='e81d8dbfe'
        heads=self.td/'base-headers';heads.mkdir(exist_ok=True)
        for name in ('agx_win32_indirect_stream_emit.h','agx_win32_expand_ahead.h'):
            r=subprocess.run(['git','-C',str(ROOT),'show',base+':drivers/apple-agx/mesa/winsys/'+name],
                             capture_output=True)
            if r.returncode:
                self.skipTest('base commit not in this repository')
            (heads/name).write_bytes(r.stdout)
        built,exe=self.build(self.td,heads,baseline=True)
        self.assertEqual(built.returncode,0,built.stderr[-6000:])
        for what in ('fallback','order','seal'):
            self.assertEqual(self.run_mode('off',what,exe),self.run_mode('off',what),what)

    def test_reset_keeps_the_application_compute_work(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Lost application compute work: IE1a aborts its expansion batch with
        its render, but Wb holds the application's own dispatches. A render
        reset must only unlink Wb (giving it R's stream outputs to retire),
        a Wb without kernels must be submitted, never reset, and a second
        render's counted draw must submit Wb rather than share it."""
        self.assertIn('RESET PASS',self.run_mode('on','reset'))


DEPENDENCY = r"""
#include <assert.h>
#include <stdio.h>
#include "agx_d3d12_job_barriers.h"
enum { DRAW = 5, DISPATCH = 2 };
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
static int A, B, T;
static void batch(unsigned kind) { AgxD3d12JobCrossBatch(&j, kind, 1); AgxD3d12JobBarrierBatch(&j, kind, values); }
int main(void) {
  /* a render after a compute batch, TA-independent by every range rule */
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  AgxD3d12JobBarrierBegin(&j, DISPATCH);
  AgxD3d12JobBarrierDispatchRange(&j, &A, 0, 64, 1u | AGX_D3D12_JOB_SRC_VIEW);
  AgxD3d12JobBarrierEnd(&j);
  batch(2u);
  AgxD3d12JobBarrierBegin(&j, DRAW);
  AgxD3d12JobBarrierDrawRange(&j, &T, 0, 4096, AGX_D3D12_JOB_SRC_TARGET);
  AgxD3d12JobBarrierEnd(&j);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0u) == 0u);
  assert(AgxD3d12JobRenderTaRefusal(&j, AGX_D3D12_JOB_FOLDED) == AGX_D3D12_JOB_REFUSAL_FOLDED);
  assert(AgxD3d12JobRenderTaRefusal(&j, 1u | AGX_D3D12_JOB_FOLDED) == AGX_D3D12_JOB_REFUSAL_FOLDED);
  assert(AgxD3d12JobRenderTaRefusal(&j, 1u) == 9u);
  batch(1u);
  /* a compute batch after that render: independent by the OV2 rule, the
   * ranges rule and the cross-execution apply */
  AgxD3d12JobBarrierBegin(&j, DISPATCH);
  AgxD3d12JobBarrierDispatchRange(&j, &B, 0, 64, 1u | AGX_D3D12_JOB_SRC_VIEW);
  AgxD3d12JobBarrierEnd(&j);
  assert(AgxD3d12JobBarrierRefusal(&j) == 0u && AgxD3d12JobBarrierRefusalRanges(&j) == 0u);
  assert(AgxD3d12JobCrossApply(1u, 1u, 0u, 1u) == 1u);
  assert(AgxD3d12JobFoldRefusal(0u) == 0u && AgxD3d12JobFoldRefusal(1u) == 0u);
  assert(AgxD3d12JobFoldRefusal(AGX_D3D12_JOB_FOLDED) == AGX_D3D12_JOB_REFUSAL_FOLDED);
  /* the winsys flag is the owner's bit << 8 */
  assert(((0x200u >> 8) & AGX_D3D12_JOB_FOLDED) == AGX_D3D12_JOB_FOLDED);
  puts("DEPENDENCY PASS");
  return 0;
}
"""


class ExpandFoldDependency(unittest.TestCase):
    def test_folded_pair_refuses_every_lead_and_overlap(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A folded render reads a VDM stream that the preceding compute batch
        writes; no D3D12 range shows it. If any verdict let that render's TA
        start before the compute (OV5) or let the compute batch lead (OV2
        same-execution, ranges rule, N4c cross lead), the render would run
        on a stream not yet written. The mark must refuse both, whatever the
        ranges say, and must be wired from the winsys hooks to the owner's
        final verdicts."""
        d3d12=ROOT/'drivers/apple-agx/d3d12'
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'dep.c').write_text(DEPENDENCY)
            r=subprocess.run(['clang','-std=c11','-Wall','-Werror','-I'+str(d3d12),str(p/'dep.c'),'-o',str(p/'dep')],
                             capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'dep')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        batch=(WINSYS/'agx_win32_gpuva_batch.c').read_text()
        self.assertIn('AgxWin32RenderTaHook(agx_win32_fold_kind(batch) >> 8) ?',batch)
        self.assertIn('AgxWin32BatchKindHook(1u | (batch->cdm.bo ? 0x100u : 0u) | agx_win32_fold_kind(batch));',batch)
        self.assertIn('AgxWin32BatchKindHook(2u | agx_win32_fold_kind(batch)) &&',batch)
        header=(WINSYS/'agx_win32_expand_ahead.h').read_text()
        self.assertIn('#define AGX_WIN32_FOLD_KIND_FLAG 0x200u',header)
        owner=(WINSYS/'agx_d3d12_windows.cpp').read_text()
        hook=owner[owner.index('static unsigned owner_batch_kind(unsigned kind) {'):]
        hook=hook[:hook.index('static void owner_batch_deps(')]
        self.assertIn('const unsigned folded = (kind >> 8) & AGX_D3D12_JOB_FOLDED;',hook)
        # the fold refusal comes after every verdict that may set independent
        refusal=hook.index('if (AgxD3d12JobFoldRefusal(folded)) {')
        for verdict in ('independent = (owner_ov2_ranges() ? ranged : reason) == 0u ? 1u : 0u;',
                        'independent = AgxD3d12JobCrossApply('):
            self.assertLess(hook.index(verdict),refusal)
        self.assertEqual(hook.count('independent = '),hook[:refusal].count('independent = ')+1)
        self.assertIn('return independent;',hook[refusal:])
        ta=owner[owner.index('static unsigned owner_render_ta(unsigned prelude) {'):]
        self.assertIn('ta = AgxD3d12JobRenderTaRefusal(&owner_job_barriers, prelude) == 0u ? 1u : 0u;',
                      ta[:ta.index('\n}\n')])


if __name__=='__main__':unittest.main()
