/* Host boundaries, with the production batch finish/retirement and parser.
 * No external Mesa implementation is copied into this replay. */
#include "apple_agx_g4_submit.h"
#include "agx_win32_gpuva.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned APPLE_AGX_U32;

#define PIPE_MAX_COLOR_BUFS 8
#define PIPE_FORMAT_B8G8R8A8_UNORM 1
#define PIPE_FORMAT_B8G8R8A8_SRGB 2
#define PIPE_FORMAT_B8G8R8X8_UNORM 3
#define PIPE_FORMAT_B8G8R8X8_SRGB 4
#define PIPE_FORMAT_R8G8B8A8_UNORM 5
#define PIPE_FORMAT_R16G16B16A16_FLOAT 6
#define PIPE_FORMAT_R10G10B10A2_UNORM 7
#define PIPE_FORMAT_R8_UNORM 8
#define PIPE_FORMAT_A8_UNORM 9
#define PIPE_FORMAT_R8G8_UNORM 10
#define DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS 3u
#define DRM_ASAHI_BARRIER_NONE 0xffffu
#define drm_asahi_cmd_render _APPLE_AGX_G4_NATIVE_RENDER_HOST
struct drm_asahi_cmd_render { APPLE_AGX_G4_NATIVE_RENDER native; };
/* The production function accesses these lowercase UAPI fields only. */
#define samples native.Samples
#define sample_size_B native.SampleSizeBytes
struct drm_asahi_attachment { uint64_t pointer,size; uint32_t pad,flags; };
struct drm_asahi_cmd_header { uint16_t cmd_type,size,vdm_barrier,cdm_barrier; };
struct agx_va { uint64_t addr; };
struct agx_bo {
  struct agx_va *va;
  AGX_WIN32_GPUVA_BO view;
  unsigned handle, flags, refs, resident, uploaded;
  unsigned char cpu[512];
};
struct agx_resource {
  struct agx_bo *bo;
  struct { uint64_t size_B,level_offsets_B[1]; } layout;
  struct agx_resource *separate_stencil;
};
struct util_dynarray { void *data; unsigned size; };
#define util_dynarray_num_elements(a,t) ((a)->size / sizeof(t))
#define util_dynarray_foreach(a,t,i) \
  for(t *i=(t *)(a)->data; i < (t *)((char *)(a)->data+(a)->size); ++i)
struct agx_pool { struct util_dynarray bos; };
typedef struct {
  AGX_WIN32_GPUVA_SPACE Gpuva;
  void *Native;
  unsigned GpuvaReady, Failed;
} AGX_WIN32_ASAHI_BACKEND;
struct agx_screen { struct agx_bo *rodata; };
/* EXP1069: Asahi's per-device zero and scratch pages (agx_open_device). */
struct agx_device { struct agx_bo *zero_bo, *scratch_bo; };
struct agx_context { struct { struct agx_screen *screen; } base; };
struct agx_batch {
  struct agx_context *ctx;
  void *windows_batch;
  struct { struct agx_bo *bo; } vdm,cdm;
  unsigned draws,clear;
  struct { unsigned bit_count, count; unsigned handles[8]; } bo_list;
  struct agx_pool pool,pipeline_pool;
  struct {
    unsigned nr_cbufs;
    struct { struct agx_resource *texture; unsigned format; }
      cbufs[PIPE_MAX_COLOR_BUFS],zsbuf;
  } key;
};
#define AGX_BATCH_FOREACH_BO_HANDLE(b,h) \
  for(unsigned hi=0;hi<(b)->bo_list.count && ((h)=(int)(b)->bo_list.handles[hi],1);++hi)
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V3 Header;
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
} AGX_G4_PRIVATE;
typedef struct {
  struct agx_bo *Command;
  APPLE_AGX_G4_PRIVATE_LEASE Lease;
  uint64_t Fence;
  unsigned Entered, Submitted, Retired, Rejected;
} AGX_G4_BATCH;
static AGX_WIN32_ASAHI_BACKEND owner;
static struct agx_bo bos[16];
static struct agx_va vas[16];
static unsigned resident_count, submit_count, eviction_count, release_count;
static unsigned reject_submit;
static APPLE_AGX_G4_FAILURE failure;
static AGX_WIN32_ASAHI_BACKEND *backend(struct agx_batch *b) { (void)b;return &owner; }
static AGX_G4_BATCH *capsule(struct agx_batch *b) { return b->windows_batch; }
static struct agx_screen *agx_screen(struct agx_screen *s) { return s; }
static struct agx_device native_device;
static struct agx_device *agx_device(struct agx_screen *s) { (void)s;return &native_device; }
static struct agx_resource *agx_resource(struct agx_resource *r) { return r; }
static uint64_t agx_map_gpu(struct agx_resource *r) { return r->bo->va->addr; }
static const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(
    AGX_WIN32_ASAHI_BACKEND *b,struct agx_bo *bo) {
  (void)b;return bo && bo->refs ? &bo->view : NULL;
}
static struct agx_bo *AgxWin32AsahiLookupBo(void *native,unsigned h) {
  (void)native;return h<16 && bos[h].refs ? &bos[h] : NULL;
}
static struct agx_bo *agx_bo_create(void *native,unsigned bytes,unsigned align,
                                   unsigned flags,const char *label) {
  (void)native;(void)align;(void)flags;(void)label;
  assert(bytes<=sizeof(bos[1].cpu));++bos[1].refs;return &bos[1];
}
static unsigned char *agx_bo_map(struct agx_bo *bo) { return bo->cpu; }
static void agx_bo_unreference(void *native,struct agx_bo *bo) {
  (void)native;assert(bo->refs && !bo->resident);--bo->refs;
}
static int AgxWin32AsahiBatchLeave(struct agx_batch *b) { capsule(b)->Entered=0;return 1; }
static struct drm_asahi_cmd_header agx_cmd_header(bool compute,unsigned a,unsigned b) {
  assert(!compute && !a && !b);
  return (struct drm_asahi_cmd_header){APPLE_AGX_G4_RENDER,240,0,0};
}
static int private_escape(void *ctx,APPLE_AGX_G3_PRIVATE_REQUEST *r) {
  (void)ctx;
  if(r->Operation==APPLE_AGX_G3_PRIVATE_RELEASE) {++release_count;return 1;}
  APPLE_AGX_G4_NATIVE_RENDER render={.WidthPx=r->Width,.HeightPx=r->Height,
    .Layers=r->Layers,.Samples=r->Samples,.UtileWidthPx=r->UtileWidth,
    .UtileHeightPx=r->UtileHeight};
  unsigned sizes[9];assert(AppleAgxG4ProcessRequiredBytes(&render,sizes));
  uint64_t va=0x20000000;
  for(unsigned i=0;i<9;++i) {
    r->Ranges[i]=(APPLE_AGX_G4_PROCESS_RANGE){va,sizes[i],0};va+=sizes[i];
  }
  r->ManagerId=r->ManagerGeneration=r->SceneId=r->SceneGeneration=1;
  return 1;
}
static int make_resident(void *ctx,const uint64_t *tokens,unsigned count,uint64_t *fence) {
  (void)ctx;resident_count=count;
  for(unsigned i=0;i<count;++i) { assert(tokens[i]<16);bos[tokens[i]].resident=1; }
  *fence=10;return 2;
}
static int wait_paging(void *ctx,uint64_t fence) { (void)ctx;assert(fence==10);return 1; }
static int mapped(void *ctx,unsigned long long va,unsigned bytes,int write,
    APPLE_AGX_G4_ACCESS_KIND kind,unsigned ordinal) {
  (void)ctx;(void)write;(void)ordinal;
  if(kind==AppleAgxG4AccessProcess) return 1;
  for(unsigned i=1;i<16;++i)
    if(bos[i].resident && bos[i].uploaded && va>=bos[i].view.Va &&
       va-bos[i].view.Va<bos[i].view.Bytes &&
       bytes<=bos[i].view.Bytes-(va-bos[i].view.Va)) return 1;
  return 0;
}
static int submit(void *ctx,const uint64_t *written,unsigned written_count,
    uint64_t va,uint32_t bytes,const void *packet,uint32_t packet_bytes,uint64_t *fence) {
  (void)ctx;++submit_count;assert(written_count==1 && written[0]==4);
  /* Model transfer_held: only canonical allocations in the residency set
   * have their CPU staging contents uploaded before KMD submission. */
  for(unsigned i=1;i<16;++i) if(bos[i].resident) bos[i].uploaded=1;
  APPLE_AGX_G4_SUBMIT_VIEW view;
  int result=AppleAgxG4ParseSubmitEx(packet,packet_bytes,packet_bytes,va,bytes,
      mapped,NULL,&view,&failure);
  if(result!=AppleAgxG4ParseOk) {
    fprintf(stderr,"R148 parse=%d ordinal=%u VA=%llx resident_count=%u\n",
      result,failure.Ordinal,failure.Va,resident_count);return 0;
  }
  if(reject_submit) return 0;
  *fence=20;return 1;
}
static int wait_render(void *ctx,uint64_t fence) { (void)ctx;assert(fence==20);return 1; }
static int evict(void *ctx,const uint64_t *tokens,unsigned count) {
  (void)ctx;++eviction_count;
  for(unsigned i=0;i<count;++i) {assert(bos[tokens[i]].resident);bos[tokens[i]].resident=0;}
  return 1;
}
#include "g4_mesa_pool_functions.inc"

/* scenario: normal, empty pools, null pool entry, invalid identity,
 * general-pool capacity overflow, pipeline-pool capacity overflow. */
static void run(unsigned fail,unsigned many,unsigned scenario) {
  memset(&owner,0,sizeof(owner));memset(bos,0,sizeof(bos));
  resident_count=submit_count=eviction_count=release_count=0;reject_submit=fail;
  for(unsigned i=1;i<16;++i) {
    vas[i].addr=0x100000ULL*i;bos[i].va=&vas[i];bos[i].handle=i;
    bos[i].view=(AGX_WIN32_GPUVA_BO){i,vas[i].addr,0x40000,1};
    bos[i].refs=i==1?0:1;
  }
  /* Source-shaped scissor suballocation, not a special acceptance value. */
  vas[5].addr=many?0x15000000:0x270000;bos[5].view.Va=vas[5].addr;
  owner.GpuvaReady=1;
  owner.Gpuva.Ops=(AGX_WIN32_GPUVA_OPS){.MakeResident=make_resident,
    .WaitPaging=wait_paging,.Submit=submit,.WaitRender=wait_render,
    .Evict=evict,.PrivateEscape=private_escape};
  struct agx_bo *pool_bos[4]={&bos[5],&bos[6],&bos[3],&bos[7]};
  struct agx_bo *pipeline_bos[3]={&bos[8],&bos[9],&bos[5]};
  bos[8].flags=bos[9].flags=1; /* Preserve low-VA pool creation intent. */
  struct agx_screen screen={&bos[3]};struct agx_context ctx={{&screen}};
  native_device=(struct agx_device){&bos[10],&bos[11]};
  struct agx_resource color={.bo=&bos[4],.layout={.size_B=65536}};
  struct agx_batch batch={.ctx=&ctx,.vdm={&bos[2]},.draws=1,
    .bo_list={.bit_count=16,.count=1,.handles={3}},
    .pool={.bos={pool_bos,(many?4u:1u)*sizeof(pool_bos[0])}},
    .pipeline_pool={.bos={pipeline_bos,(many?3u:1u)*sizeof(pipeline_bos[0])}},
    .key={.nr_cbufs=1,.cbufs={{&color,PIPE_FORMAT_B8G8R8A8_UNORM}}}};
  batch.windows_batch=calloc(1,sizeof(AGX_G4_BATCH));assert(batch.windows_batch);
  APPLE_AGX_G4_NATIVE_RENDER render={.WidthPx=31,.HeightPx=17,.Layers=1,
    .Samples=1,.SampleSizeBytes=8,.UtileWidthPx=16,.UtileHeightPx=16,
    .VdmCtrlStreamBase=bos[2].view.Va,
    .IspScissorBase=bos[5].view.Va+(many?0x12340:0xe40)};
  if(scenario==1) {
    batch.pool.bos.size=batch.pipeline_pool.bos.size=0;
    render.IspScissorBase=bos[2].view.Va+128;
  } else if(scenario==2) {
    pipeline_bos[0]=NULL;
  } else if(scenario==3) {
    bos[5].refs=0;
  } else if(scenario>=4) {
    batch.bo_list.bit_count=UINT32_MAX-(PIPE_MAX_COLOR_BUFS+19)-1;
    batch.pool.bos.size=(scenario==4?2u:1u)*sizeof(pool_bos[0]);
  }
  /* Set aliases at the actual ABI offsets, checked by the wrapper below. */
  struct drm_asahi_cmd_render native;memcpy(&native,&render,sizeof(render));
  int result=AgxWin32AsahiBatchFinish(&batch,&native);
  if(scenario>=2) {
    assert(!result && !submit_count && !resident_count && !eviction_count);
    assert(!owner.Gpuva.Held && !owner.Gpuva.Terminal);
    assert(AgxWin32AsahiBatchRelease(&batch));
    assert(!batch.windows_batch && !bos[1].refs);
    assert(release_count==(scenario>=4?0u:1u));
    for(unsigned i=2;i<16;++i) assert(!bos[i].resident);
    return;
  }
  if(!fail && !result) {
    assert(failure.Ordinal==12 && failure.Va==render.IspScissorBase);
    fprintf(stderr,"R148 missing batch-pool residency/upload (expected admitted submit)\n");
  }
  assert(result==!fail);
  /* The zero and scratch pages (bos 10, 11) are resident in every batch. */
  assert(submit_count==1 && resident_count==(scenario==1?6u:many?11u:8u));
  assert(bos[10].uploaded && bos[11].uploaded);
  if(scenario!=1) {
    assert(bos[5].uploaded && bos[8].uploaded && bos[8].flags==1);
    if(many) assert(bos[6].uploaded && bos[7].uploaded && bos[9].uploaded);
  }
  if(!fail) {assert(owner.Gpuva.Held);assert(AgxWin32AsahiBatchPoll(&batch,1000));}
  assert(!owner.Gpuva.Held && eviction_count==1);
  assert(AgxWin32AsahiBatchRelease(&batch));
  assert(!batch.windows_batch && release_count==1 && !bos[1].refs);
  for(unsigned i=2;i<16;++i) assert(bos[i].refs==1 && !bos[i].resident);
}
int main(void) {
  run(0,0,0);run(0,1,0);run(1,1,0);run(0,0,1);
  run(0,0,2);run(0,0,3);run(0,0,4);run(0,0,5);
  puts("R148 production batch pool residency/upload/rollback: PASS");return 0;
}
