/* Host allocation/GPU boundaries; query lifetime policy is projected source. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define global
#include "compiler/libcl/libcl.h"
#undef KERNEL
#define KERNEL(n) static void
#define libagx_win32_query_timestamp execute_timestamp_kernel
#include "agx_win32_query_timestamp.cl"
#undef libagx_win32_query_timestamp
#define AGX_MAX_BATCHES 128
#define AGX_MAX_OCCLUSION_QUERIES 32
#define AGX_BO_WRITEBACK 1
#define AGX_DIRTY_QUERY 1
#define AGX_BARRIER_ALL 0
#define STATIC_ASSERT(x) _Static_assert(x,#x)
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define UNREACHABLE(x) abort()
#define BITSET_SET(a,i) ((a)[(i)/32] |= 1u<<((i)%32))
#define BITSET_CLEAR(a,i) ((a)[(i)/32] &= ~(1u<<((i)%32)))
#define BITSET_TEST(a,i) (((a)[(i)/32]>>((i)%32))&1)
#define BITSET_ONES(a) memset(a,0xff,sizeof(a))
#define BITSET_FFS(a) __builtin_ffs((a)[0])
#define foreach_active(c,i) for(i=0;i<AGX_MAX_BATCHES;i++) if(BITSET_TEST((c)->batches.active,i))
#define foreach_submitted(c,i) for(i=0;i<AGX_MAX_BATCHES;i++) if(BITSET_TEST((c)->batches.submitted,i))
enum pipe_query_type {PIPE_QUERY_OCCLUSION_COUNTER,PIPE_QUERY_OCCLUSION_PREDICATE,
 PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE,PIPE_QUERY_PRIMITIVES_GENERATED,
 PIPE_QUERY_PRIMITIVES_EMITTED,PIPE_QUERY_SO_OVERFLOW_PREDICATE,
 PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE,PIPE_QUERY_TIME_ELAPSED,PIPE_QUERY_TIMESTAMP,
 PIPE_QUERY_PIPELINE_STATISTICS_SINGLE};
enum pipe_query_value_type {PIPE_QUERY_TYPE_I32,PIPE_QUERY_TYPE_U32,PIPE_QUERY_TYPE_I64,PIPE_QUERY_TYPE_U64};
enum pipe_query_flags {PIPE_QUERY_WAIT=1};
enum query_copy_type {QUERY_COPY_NORMAL,QUERY_COPY_BOOL32,QUERY_COPY_BOOL64,QUERY_COPY_TIMESTAMP,QUERY_COPY_TIME_ELAPSED};
struct agx_device {unsigned unused;};
struct pipe_context {struct agx_device *screen;};
struct pipe_query {unsigned unused;};
struct agx_va {uint64_t addr;};
struct agx_bo {unsigned refs;struct agx_va address,*va;uint64_t *data;};
struct agx_ptr {void *cpu;uint64_t gpu;};
struct agx_oq_heap {struct agx_device *dev;struct agx_bo *bo;unsigned available[1];};
struct agx_query {unsigned type,index;uint64_t writer_generation[AGX_MAX_BATCHES];
 struct agx_bo *bo;struct agx_ptr ptr;struct agx_query *windows_next;
 uint64_t windows_timestamp_seed;unsigned windows_timestamp_count;
 struct agx_bo *windows_timestamp_bos[AGX_MAX_BATCHES];};
struct agx_pool {void *uploads[16];unsigned count;};
struct util_dynarray {struct agx_ptr *data;unsigned count;};
struct agx_context;
struct agx_batch {struct agx_context *ctx;unsigned index;bool initialized;
 struct agx_bo *publication;unsigned refs;struct agx_bo *held[256];
 struct agx_pool pool;struct util_dynarray timestamps;};
struct agx_context {struct pipe_context base;unsigned dirty;bool any_faults;
 struct {uint64_t generation[AGX_MAX_BATCHES];struct agx_batch slots[AGX_MAX_BATCHES];
 unsigned active[4],submitted[4];} batches;
 struct agx_oq_heap *oq;unsigned consumer;
 struct agx_query *occlusion_query,*prims_generated[4],*tf_prims_generated[4],
 *tf_overflow[4],*tf_any_overflow,*time_elapsed,*pipeline_statistics[16],*windows_retired_queries;};
struct pipe_resource {uint64_t words[4];};
#define agx_resource pipe_resource
#define pipe_resource(p) (p)
#define agx_context(p) ((struct agx_context *)(p))
#define agx_device(p) (p)
#define rzalloc(ctx,type) ((type *)calloc(1,sizeof(type)))
#define ralloc_set_destructor(p,f) ((void)(p),(void)(f))
#define ralloc_free(p) free(p)
static unsigned live_bos,flushes,writes;
static uint64_t clock_seed;
static struct agx_bo *agx_bo_create(struct agx_device *d,size_t bytes,unsigned align,unsigned flags,const char *name) {
 (void)d;(void)align;(void)flags;(void)name;
 struct agx_bo *b=calloc(1,sizeof(*b));assert(b);b->data=calloc(1,bytes);assert(b->data);
 b->refs=1;b->va=&b->address;b->va->addr=(uintptr_t)b->data;live_bos++;return b;
}
static void *agx_bo_map(struct agx_bo *b) {return b->data;}
static void agx_bo_reference(struct agx_bo *b) {assert(b&&b->refs);b->refs++;}
static void agx_bo_unreference(struct agx_device *d,struct agx_bo *b) {
 (void)d;if(!b)return;assert(b->refs);if(!--b->refs){free(b->data);free(b);live_bos--;}
}
static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo) {
 assert(bo);for(unsigned i=0;i<b->refs;i++)if(b->held[i]==bo)return;
 assert(b->refs<ARRAY_SIZE(b->held));b->held[b->refs++]=bo;agx_bo_reference(bo);
}
static unsigned agx_batch_idx(struct agx_batch *b) {return b->index;}
static struct agx_ptr *grow(struct util_dynarray *a) {
 a->data=realloc(a->data,(a->count+1)*sizeof(*a->data));assert(a->data);
 return &a->data[a->count++];
}
#define util_dynarray_grow(a,type,n) ((type *)grow(a))
#define util_dynarray_foreach(a,type,it) for(unsigned ai=0;ai<(a)->count;ai++) for(type *it=&(a)->data[ai];it;it=NULL)
static struct agx_bo *AgxWin32AsahiBatchTimestampBo(struct agx_batch *b) {assert(b->initialized);return b->publication;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned site) {(void)site;c->any_faults=true;}
static uint64_t agx_get_gpu_timestamp(struct agx_device *d) {(void)d;return clock_seed;}
static const char *util_str_query_type(unsigned t,bool b) {(void)t;(void)b;return "query";}
static void agx_flush_batch_for_reason(struct agx_context *c,struct agx_batch *b,const char *s) {
 (void)s;flushes++;BITSET_CLEAR(c->batches.active,b->index);BITSET_SET(c->batches.submitted,b->index);
}
static struct agx_batch *agx_get_compute_batch(struct agx_context *c) {return &c->batches.slots[c->consumer];}
static void agx_batch_init_state(struct agx_batch *b) {b->initialized=true;}
static void agx_dirty_all(struct agx_context *c) {(void)c;}
static uint64_t agx_map_gpu(struct pipe_resource *p) {return (uintptr_t)p->words;}
static unsigned agx_1d(unsigned n) {return n;}
static void agx_batch_writes_range(struct agx_batch *b,struct pipe_resource *p,unsigned off,unsigned bytes) {
 (void)b;assert(p&&off==8&&bytes==8);writes++;
}
static uint64_t agx_pool_upload_aligned(struct agx_pool *pool,const void *src,size_t bytes,unsigned align) {
 assert(align==8&&pool->count<ARRAY_SIZE(pool->uploads));void *p=malloc(bytes);assert(p);
 memcpy(p,src,bytes);pool->uploads[pool->count++]=p;return (uintptr_t)p;
}
static struct {bool timestamp;uint64_t sources,seed,destination;unsigned count,bool_size;} commands[16];
static unsigned queued;
static void libagx_win32_query_timestamp(struct agx_batch *b,unsigned grid,unsigned barrier,uint64_t src,unsigned count,uint64_t seed,uint64_t dst) {
 (void)b;(void)barrier;assert(grid==1&&queued<16);
 commands[queued].timestamp=true;commands[queued].sources=src;commands[queued].count=count;
 commands[queued].seed=seed;commands[queued++].destination=dst;
}
static void libagx_copy_query_gl(struct agx_batch *b,unsigned grid,unsigned barrier,uint64_t src,uint64_t dst,unsigned type,unsigned bool_size) {
 (void)b;(void)barrier;assert(grid==1&&type==PIPE_QUERY_TYPE_U64&&queued<16);
 commands[queued].timestamp=false;commands[queued].sources=src;
 commands[queued].bool_size=bool_size;commands[queued++].destination=dst;
}
static void execute_commands(void) {
 for(unsigned i=0;i<queued;i++) {
  if(commands[i].timestamp) execute_timestamp_kernel(commands[i].sources,commands[i].count,commands[i].seed,commands[i].destination);
  else {uint64_t v=*(uint64_t *)(uintptr_t)commands[i].sources;
   if(commands[i].bool_size==4)v=(uint32_t)v;if(commands[i].bool_size)v=!!v;
   *(uint64_t *)(uintptr_t)commands[i].destination=v;}
 }queued=0;
}
static void agx_win32_query_release_snapshot(struct agx_context *,struct agx_query *);
static void agx_add_query_to_batch(struct agx_batch *,struct agx_query *);
void agx_batch_add_timestamp_query(struct agx_batch *,struct agx_query *);
void agx_add_timestamp_end_query(struct agx_context *,struct agx_query *);

@@PRODUCTION@@

static void init_context(struct agx_context *c) {
 memset(c,0,sizeof(*c));c->consumer=3;
 for(unsigned i=0;i<AGX_MAX_BATCHES;i++) {c->batches.generation[i]=1;c->batches.slots[i].ctx=c;c->batches.slots[i].index=i;}
}
static struct agx_batch *open_batch(struct agx_context *c,unsigned i,bool submitted,bool initialized) {
 struct agx_batch *b=&c->batches.slots[i];assert(!b->publication&&!b->refs&&!b->timestamps.count);
 b->initialized=initialized;
 if(initialized)b->publication=agx_bo_create(NULL,8,0,0,"GPU publication");
 if(submitted)BITSET_SET(c->batches.submitted,i);else BITSET_SET(c->batches.active,i);
 return b;
}
static void retire(struct agx_batch *b,uint64_t end) {
 if(b->publication)b->publication->data[0]=end; /* GPU/KMD completion boundary */
 agx_finish_batch_queries(b,1,end);
 BITSET_CLEAR(b->ctx->batches.active,b->index);BITSET_CLEAR(b->ctx->batches.submitted,b->index);
 for(unsigned i=0;i<b->refs;i++)agx_bo_unreference(NULL,b->held[i]);b->refs=0;
 agx_bo_unreference(NULL,b->publication);b->publication=NULL;b->initialized=false;
 free(b->timestamps.data);b->timestamps=(struct util_dynarray){0};
 for(unsigned i=0;i<b->pool.count;i++)free(b->pool.uploads[i]);b->pool.count=0;
}
static bool resolve(struct agx_context *c,struct agx_query *q,struct pipe_resource *dst) {
 return agx_get_query_result_resource_gpu(c,q,PIPE_QUERY_WAIT,PIPE_QUERY_TYPE_U64,0,dst,8);
}
static void timestamp_generations(void) {
 struct agx_context c;init_context(&c);struct pipe_resource dst={0};
 struct agx_batch *a=open_batch(&c,0,false,true),*b=open_batch(&c,1,true,true);
 struct agx_batch *empty=open_batch(&c,2,false,false);
 struct agx_bo *first=a->publication,*second=b->publication;
 struct agx_query *q=(void *)agx_create_query(&c.base,PIPE_QUERY_TIMESTAMP,0);
 clock_seed=17;assert(agx_end_query(&c.base,(void *)q)&&!c.any_faults);
 assert(q->windows_timestamp_count==2&&first->refs==2&&second->refs==2);
 assert(q->writer_generation[0]==1&&q->writer_generation[1]==1&&q->writer_generation[2]==1);
 struct agx_batch *consumer=open_batch(&c,3,false,true);
 assert(resolve(&c,q,&dst)&&!dst.words[1]&&first->refs==3&&second->refs==3);
 retire(a,91);retire(b,85);retire(empty,0);
 assert(first->refs==2&&second->refs==2&&!is_query_busy(&c,q));
 /* Reuse the producer's slot/VA metadata before the old resolve executes. */
 a=open_batch(&c,0,false,true);assert(a->publication!=first);
 clock_seed=9999;assert(agx_end_query(&c.base,(void *)q));
 assert(q->windows_timestamp_seed==9999&&q->windows_timestamp_count==2);
 assert(first->refs==1&&second->refs==1); /* only the old resolve retains these */
 agx_destroy_query(&c.base,(void *)q);assert(c.windows_retired_queries);
 retire(a,10001);assert(c.windows_retired_queries);
 execute_commands();assert(dst.words[1]==91); /* old seed and immutable addresses */
 retire(consumer,10002);assert(!c.windows_retired_queries&&!live_bos);
 /* No active or submitted batches: End and resolve must preserve the seed. */
 q=(void *)agx_create_query(&c.base,PIPE_QUERY_TIMESTAMP,0);clock_seed=0x100000001ull;
 assert(agx_end_query(&c.base,(void *)q)&&!q->windows_timestamp_count);
 consumer=open_batch(&c,3,false,true);assert(resolve(&c,q,&dst));
 agx_destroy_query(&c.base,(void *)q);execute_commands();assert(dst.words[1]==0x100000001ull);
 retire(consumer,2);assert(!live_bos);
}
static void occlusion_generations(bool reuse) {
 struct agx_context c;init_context(&c);struct pipe_resource dst={0};
 struct agx_query *q=(void *)agx_create_query(&c.base,PIPE_QUERY_OCCLUSION_COUNTER,0);
 assert(q&&agx_begin_query(&c.base,(void *)q));unsigned old=agx_oq_index(&c,q);
 uint64_t *old_value=q->ptr.cpu;
 struct agx_batch *writer=open_batch(&c,0,false,true);agx_add_query_to_batch(writer,q);
 *old_value=51;assert(agx_end_query(&c.base,(void *)q));
 struct agx_batch *reader=open_batch(&c,3,false,true);assert(resolve(&c,q,&dst));
 retire(writer,7);assert(is_query_busy(&c,q));
 if(reuse) {assert(agx_begin_query(&c.base,(void *)q));assert(agx_oq_index(&c,q)!=old);}
 agx_destroy_query(&c.base,(void *)q);
 assert(c.windows_retired_queries&&!BITSET_TEST(c.oq->available,old));
 struct agx_query *other=(void *)agx_create_query(&c.base,PIPE_QUERY_OCCLUSION_COUNTER,0);
 assert(agx_oq_index(&c,other)!=old);assert(agx_begin_query(&c.base,(void *)other));
 assert(*old_value==51);agx_destroy_query(&c.base,(void *)other);
 execute_commands();assert(dst.words[1]==51);
 retire(reader,8);assert(!c.windows_retired_queries&&BITSET_TEST(c.oq->available,old));
 other=(void *)agx_create_query(&c.base,PIPE_QUERY_OCCLUSION_COUNTER,0);
 assert(agx_oq_index(&c,other)==old);agx_destroy_query(&c.base,(void *)other);
 agx_destroy_oq_heap(c.oq);free(c.oq);assert(!live_bos);
}
int main(void) {
 timestamp_generations();occlusion_generations(false);occlusion_generations(true);
 assert(!queued&&!live_bos);puts("Actual timestamp/OQ generation lifetimes: PASS");return 0;
}
