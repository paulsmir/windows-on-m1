#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define AGX_MAX_BATCHES 128
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define AGX_DIRTY_QUERY 1
#define AGX_BARRIER_ALL 0
#define STATIC_ASSERT(x) _Static_assert(x,#x)
#define AGX_WIN32_QUERY_TIMESTAMP_MAX_SOURCES 128
#define PIPE_QUERY_OCCLUSION_COUNTER 0
#define PIPE_QUERY_OCCLUSION_PREDICATE 1
#define PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE 2
#define PIPE_QUERY_PRIMITIVES_GENERATED 3
#define PIPE_QUERY_PRIMITIVES_EMITTED 4
#define PIPE_QUERY_SO_OVERFLOW_PREDICATE 5
#define PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE 6
#define PIPE_QUERY_TIME_ELAPSED 7
#define PIPE_QUERY_TIMESTAMP 8
#define PIPE_QUERY_PIPELINE_STATISTICS_SINGLE 9
enum pipe_query_type { QUERY_UNUSED=100 };
#define UNREACHABLE(x) abort()
enum pipe_query_value_type {PIPE_QUERY_TYPE_I32,PIPE_QUERY_TYPE_U32,PIPE_QUERY_TYPE_I64,PIPE_QUERY_TYPE_U64};
enum pipe_query_flags { PIPE_QUERY_WAIT=1 };
enum query_copy_type {QUERY_COPY_NORMAL,QUERY_COPY_BOOL32,QUERY_COPY_BOOL64,QUERY_COPY_TIMESTAMP,QUERY_COPY_TIME_ELAPSED};
struct agx_device {int unused;};struct pipe_context {struct agx_device *screen;}; struct pipe_query {int unused;};
struct agx_bo {uint64_t data[2]; unsigned refs; struct {uint64_t addr;} storage; struct {uint64_t addr;} *va;};
struct agx_ptr {void *cpu;uint64_t gpu;};
struct agx_query {unsigned type,index;uint64_t writer_generation[128];struct agx_bo *bo;struct agx_ptr ptr;
 struct agx_query *windows_next; unsigned windows_timestamp_count; uint64_t windows_timestamp_seed;
 struct agx_bo *windows_timestamp_bos[128];};
struct agx_context;
struct agx_batch {struct agx_context *ctx;unsigned refs, index; struct agx_bo *held[256]; struct {int unused;} pool;};
struct agx_context {struct pipe_context base; unsigned dirty; bool any_faults;
 struct {uint64_t generation[128];} batches;
 struct agx_query *occlusion_query,*prims_generated[4],*tf_prims_generated[4],*tf_overflow[4],*tf_any_overflow,*time_elapsed,*pipeline_statistics[16],*windows_retired_queries;
 struct {struct agx_bo *bo;} *oq; struct agx_batch batch;};
struct pipe_resource {uint64_t words[16];};
#define agx_resource pipe_resource
#define pipe_resource(p) (p)
#define agx_context(p) ((struct agx_context *)(p))
#define agx_device(p) (p)
static unsigned waits,bos,flushes,writes,dispatches; static bool allocation_failure, compute_failure, upload_failure;
static struct agx_bo *new_bo(void) {struct agx_bo *b=calloc(1,sizeof(*b));assert(b);b->refs=1; b->va=(void *)&b->storage;b->va->addr=(uint64_t)(uintptr_t)b->data;bos++;return b;}
static void agx_bo_unreference(void *dev,struct agx_bo *b) {(void)dev;if(b && !--b->refs) {bos--;free(b);}}
static void sync_query_writers(struct agx_context *c,struct agx_query *q,const char *s) {(void)c;(void)q;(void)s;waits++;}
static void flush_query_writers(struct agx_context *c,struct agx_query *q,const char *s) {(void)c;(void)q;(void)s;flushes++;}
static const char *util_str_query_type(unsigned t,bool b) {(void)t;(void)b;return "query";}
static bool is_query_busy(struct agx_context *c,struct agx_query *q) {for(unsigned i=0;i<128;i++)if(q->writer_generation[i]==c->batches.generation[i])return true;return false;}
static struct pipe_query *agx_create_query(struct pipe_context *p,unsigned t,unsigned idx) {(void)p;if(allocation_failure)return NULL;struct agx_query *q=calloc(1,sizeof(*q));q->type=t;q->index=idx;for(unsigned i=0;i<128;i++)q->writer_generation[i]=UINT64_MAX;q->bo=new_bo();q->ptr=(struct agx_ptr){q->bo->data,q->bo->va->addr};return (void *)q;}
static void agx_destroy_query(struct pipe_context *p,struct pipe_query *pq) {(void)p;struct agx_query *q=(void *)pq;for(unsigned i=0;i<q->windows_timestamp_count;i++)agx_bo_unreference(NULL,q->windows_timestamp_bos[i]);agx_bo_unreference(NULL,q->bo);free(q);}
static void AgxWin32AsahiMarkContextFault(struct agx_context *ctx,unsigned site) {(void)site;ctx->any_faults=true;}
static struct agx_batch *agx_get_compute_batch(struct agx_context *c) {if(compute_failure){c->any_faults=true;return NULL;}return &c->batch;}
static void agx_batch_init_state(struct agx_batch *b) {(void)b;}
static void agx_dirty_all(struct agx_context *c) {(void)c;}
static void agx_batch_add_bo(struct agx_batch *b,struct agx_bo *bo) {assert(bo);for(unsigned i=0;i<b->refs;i++)if(b->held[i]==bo)return;b->held[b->refs++]=bo;bo->refs++;}
static void agx_add_query_to_batch(struct agx_batch *b,struct agx_query *q) {agx_batch_add_bo(b,q->bo);q->writer_generation[b->index]=b->ctx->batches.generation[b->index];}
static void agx_batch_writes_range(struct agx_batch *b,struct pipe_resource *p,unsigned off,unsigned n) {(void)b;assert(p && off==24 && n==8);writes++;}
static uint64_t agx_map_gpu(struct pipe_resource *p) {return (uint64_t)(uintptr_t)p->words;}
static void agx_get_query_result_resource_cpu(struct agx_context *c,struct agx_query *q,enum pipe_query_flags f,enum pipe_query_value_type t,int i,struct pipe_resource *r,unsigned o) {(void)c;(void)q;(void)f;(void)t;(void)i;(void)r;(void)o;waits++;}
static unsigned agx_1d(unsigned n) {return n;}
static uint64_t uploaded[128], gpu_seed,gpu_dst,ordinary_src;static unsigned gpu_count,ordinary_bool;
struct agx_pool {int unused;};
static uint64_t agx_pool_upload_aligned(void *pool,const void *p,size_t n,unsigned alignment) {(void)pool;if(upload_failure)return 0;assert(n<=sizeof(uploaded)&&alignment==8);memcpy(uploaded,p,n);return (uint64_t)(uintptr_t)uploaded;}
static void libagx_win32_query_timestamp(struct agx_batch *b,unsigned grid,unsigned barrier,uint64_t addresses,unsigned count,uint64_t seed,uint64_t dst) {(void)b;(void)barrier;assert(grid==1&&addresses==(uint64_t)(uintptr_t)uploaded);gpu_count=count;gpu_seed=seed;gpu_dst=dst;dispatches++;}
static void libagx_copy_query_gl(struct agx_batch *b,unsigned grid,unsigned barrier,uint64_t src,uint64_t dst,unsigned type,unsigned bool_size) {(void)b;(void)grid;(void)barrier;assert(type==PIPE_QUERY_TYPE_U64);ordinary_src=src;gpu_dst=dst;ordinary_bool=bool_size;dispatches++;}
static uint64_t clock_seed;
static struct agx_bo *attach_source;
static uint64_t agx_get_gpu_timestamp(struct agx_device *d) {(void)d;return clock_seed;}
static void agx_add_timestamp_end_query(struct agx_context *ctx,struct agx_query *q) {if(attach_source){q->windows_timestamp_bos[q->windows_timestamp_count++]=attach_source;attach_source->refs++;q->writer_generation[0]=ctx->batches.generation[0];}}
static void release_batch(struct agx_batch *b) {for(unsigned i=0;i<b->refs;i++)agx_bo_unreference(NULL,b->held[i]);b->refs=0;}
static void run_gpu(void) {if(ordinary_src){uint64_t v=*(uint64_t *)(uintptr_t)ordinary_src;if(ordinary_bool==4)v=(uint32_t)v;if(ordinary_bool)v=!!v;*(uint64_t *)(uintptr_t)gpu_dst=v;return;}uint64_t value=gpu_seed;for(unsigned i=0;i<gpu_count;i++){uint64_t next=*(uint64_t *)(uintptr_t)uploaded[i];if(next>value)value=next;}*(uint64_t *)(uintptr_t)gpu_dst=value;}
#if __has_include("agx_win32_query_lifecycle.h")
#include "agx_win32_query_lifecycle.h"
#else
static void AgxWin32AsahiQueryCleanup(struct agx_context *c) {(void)c;}
#endif
