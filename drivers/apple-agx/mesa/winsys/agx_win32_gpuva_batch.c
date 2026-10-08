#include "gallium/drivers/asahi/agx_state.h"
#include "agx_win32_asahi_batch.h"
#include "agx_win32_asahi_bo.h"
#include "agx_device.h"
#include "apple_agx_g4_submit.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef APPLE_AGX_GPUVA_WINSYS
#error This batch path is only for APPLE_AGX_GPUVA_WINSYS
#endif

/* Native command bytes are duplicated verbatim for KMD validation. Kernel
 * private ranges accompany the v3 envelope. KMD owns their allocation,
 * initialization and lifetime; the UMD requests release after its fence. */
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V3 Header;
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
} AGX_G4_PRIVATE;
_Static_assert(offsetof(AGX_G4_PRIVATE,Native)==
               sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V3),
               "G4 private command header layout");
_Static_assert(sizeof(struct drm_asahi_cmd_header)==
               sizeof(APPLE_AGX_G4_NATIVE_HEADER), "native command header");
_Static_assert(sizeof(struct drm_asahi_attachment)==
               sizeof(APPLE_AGX_G4_ATTACHMENT), "native attachment");
_Static_assert(sizeof(struct drm_asahi_cmd_render)==
               sizeof(APPLE_AGX_G4_NATIVE_RENDER), "native render layout");
_Static_assert(offsetof(struct drm_asahi_cmd_render,vdm_ctrl_stream_base)==
               offsetof(APPLE_AGX_G4_NATIVE_RENDER,VdmCtrlStreamBase), "VDM VA");
_Static_assert(offsetof(struct drm_asahi_cmd_render,vertex_helper)==
               offsetof(APPLE_AGX_G4_NATIVE_RENDER,VertexHelper), "vertex helper");
_Static_assert(offsetof(struct drm_asahi_cmd_render,isp_scissor_base)==
               offsetof(APPLE_AGX_G4_NATIVE_RENDER,IspScissorBase), "ISP VA");
_Static_assert(offsetof(struct drm_asahi_cmd_render,depth)==
               offsetof(APPLE_AGX_G4_NATIVE_RENDER,Depth), "ZLS VA");
_Static_assert(offsetof(struct drm_asahi_cmd_render,sampler_heap)==
               offsetof(APPLE_AGX_G4_NATIVE_RENDER,SamplerHeap), "sampler VA");
_Static_assert(offsetof(struct drm_asahi_cmd_render,bg)==
               offsetof(APPLE_AGX_G4_NATIVE_RENDER,Bg), "background USC");
_Static_assert(offsetof(struct drm_asahi_cmd_render,ts_vtx)==
               offsetof(APPLE_AGX_G4_NATIVE_RENDER,TimestampsVertex), "timestamps");

typedef struct {
  struct agx_bo *Command;
  APPLE_AGX_G4_PRIVATE_LEASE Lease;
  uint64_t Fence;
  unsigned Entered, Submitted, Retired, Rejected;
} AGX_G4_BATCH;

static AGX_WIN32_ASAHI_BACKEND *backend(struct agx_batch *batch) {
  return batch && batch->ctx ?
      agx_device(batch->ctx->base.screen)->windows_private : NULL;
}
static AGX_G4_BATCH *capsule(struct agx_batch *batch) {
  return batch ? (AGX_G4_BATCH *)batch->windows_batch : NULL;
}

int AgxWin32AsahiBatchConfigure(AGX_WIN32_ASAHI_BACKEND *b,
    const AGX_WIN32_ASAHI_BATCH_OPS *ops,void *owner) {
  (void)ops;
  (void)owner;
  return b && b->Native && b->GpuvaReady && !b->Failed;
}
void AgxWin32AsahiBatchTraceDraw(struct agx_context *ctx,
    struct agx_batch *batch,unsigned phase) {
  (void)ctx; (void)batch; (void)phase;
}
/* Diagnostic only (EXP870): report which check refused a batch. The hook is
 * installed by the D3D10 Windows layer and never changes the result. */
void (*AgxWin32BatchRefusalHook)(unsigned kind, unsigned site,
                                 unsigned detail0, unsigned detail1);
void (*AgxWin32FirstFaultHook)(unsigned site, uintptr_t context,
                              unsigned flags,unsigned draws);
/* EXP990 receipt-only: the first VDM control-stream words as written by Mesa,
 * for comparison with the KMD's job-time view of the same GPU VA. */
void (*AgxWin32VdmTraceHook)(uint64_t va, const uint32_t *words,
                             unsigned count, unsigned draws);
void AgxWin32AsahiMarkBatchFault(struct agx_context *ctx,
                                struct agx_batch *batch,unsigned site) {
  if(!ctx) return;
  int first=!ctx->any_faults;
  unsigned flags=(unsigned)ctx->any_faults |
      ((unsigned)(batch!=NULL)<<1) |
      ((unsigned)(batch && batch->vdm.bo!=NULL)<<2) |
      ((unsigned)(batch && batch->initialized)<<3) |
      ((unsigned)(batch && batch->draws!=0)<<4) |
      ((unsigned)(batch && batch->cdm.bo!=NULL)<<5) |
      ((unsigned)(batch && batch->clear)<<6) |
      ((unsigned)(batch && ctx->batch==batch)<<7);
  unsigned draws=batch?batch->draws:0u;
  ctx->any_faults=true;
  if(first && AgxWin32FirstFaultHook)
    AgxWin32FirstFaultHook(site,(uintptr_t)&ctx->base,flags,draws);
}
void AgxWin32AsahiMarkContextFault(struct agx_context *ctx,unsigned site) {
  AgxWin32AsahiMarkBatchFault(ctx,ctx?ctx->batch:NULL,site);
}
static int batch_refuse(unsigned kind, unsigned site,
                        unsigned detail0, unsigned detail1) {
  if (AgxWin32BatchRefusalHook)
    AgxWin32BatchRefusalHook(kind, site, detail0, detail1);
  return 0;
}

int AgxWin32AsahiBatchBegin(struct agx_batch *batch) {
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  if(!b || !b->GpuvaReady || b->Failed || batch->windows_batch ||
     !batch->vdm.bo) return batch_refuse(1u, __LINE__, 0u, 0u);
  /* One monitored submission owns this process residency set. Drain earlier
   * active and submitted batches before opening another transaction. */
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *old=&batch->ctx->batches.slots[i];
    if(old==batch || !old->windows_batch) continue;
    if(BITSET_TEST(batch->ctx->batches.active,i))
      agx_flush_batch(batch->ctx,old);
    if(batch->ctx->any_faults || !AgxWin32AsahiBatchPoll(old,1000)) return batch_refuse(1u, __LINE__, 0u, 0u);
    agx_sync_batch(batch->ctx,old);
    if(old->windows_batch) return batch_refuse(1u, __LINE__, 0u, 0u);
  }
  if(b->Gpuva.Held) return batch_refuse(1u, __LINE__, 0u, 0u);
  AGX_G4_BATCH *g=calloc(1,sizeof(*g));
  if(!g) return batch_refuse(1u, __LINE__, 0u, 0u);
  batch->windows_batch=g;
  return 1;
}
int AgxWin32AsahiBatchEnter(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  if(!g || g->Submitted || g->Rejected || g->Entered) return 0;
  g->Entered=1;
  return 1;
}
int AgxWin32AsahiBatchLeave(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  if(!g || !g->Entered) return 0;
  g->Entered=0;
  return 1;
}
int AgxWin32AsahiBatchPrepareDraw(struct agx_batch *batch,
    const struct pipe_draw_info *info,const struct pipe_draw_start_count_bias *draw) {
  AGX_G4_BATCH *g=capsule(batch);
  return g && !g->Submitted && !g->Rejected && info && draw &&
         draw->count && info->instance_count;
}
int AgxWin32AsahiBatchDrawAllowed(struct agx_context *ctx,
    const struct pipe_draw_info *info,unsigned drawid,
    const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draws,unsigned count) {
  (void)drawid;
  return ctx && !ctx->any_faults && info && draws && count && !indirect &&
         info->instance_count && ctx->framebuffer.nr_cbufs &&
         ctx->framebuffer.cbufs[0].texture;
}
int AgxWin32AsahiBatchComputeEnter(struct agx_batch *batch) {
  (void)batch;
  return 0; /* no direct-VA CDM command parser is agreed with KMD yet */
}
int AgxWin32AsahiBatchComputeLeave(struct agx_batch *batch) {
  (void)batch;
  return 0;
}
int AgxWin32AsahiBatchComputeFinalize(struct agx_batch *batch,const void *end) {
  (void)batch; (void)end;
  return 0;
}

static int add_bo(AGX_WIN32_ASAHI_BACKEND *b,
    const AGX_WIN32_GPUVA_BO **refs,unsigned *count,unsigned limit,
    struct agx_bo *bo) {
  if(!bo) return 1;
  const AGX_WIN32_GPUVA_BO *view=AgxWin32AsahiGpuvaBo(b,bo);
  if(!view) return 0;
  for(unsigned i=0;i<*count;++i)
    if(refs[i]->Allocation==view->Allocation) return 1;
  if(*count==limit) return 0;
  refs[(*count)++]=view;
  return 1;
}

static int append_native(AGX_G4_PRIVATE *packet, const void *data,
                         size_t bytes) {
  if(!packet || !data || !bytes ||
     bytes>sizeof(packet->Native)-packet->Header.V2.Base.CommandBytes) return 0;
  memcpy(packet->Native+packet->Header.V2.Base.CommandBytes,data,bytes);
  packet->Header.V2.Base.CommandBytes+=(uint32_t)bytes;
  return 1;
}

static int append_attachments(struct agx_batch *batch,AGX_G4_PRIVATE *packet) {
  struct drm_asahi_attachment attachments[PIPE_MAX_COLOR_BUFS+2]={0};
  unsigned count=0;
  for(unsigned i=0;i<batch->key.nr_cbufs;++i) {
    if(!batch->key.cbufs[i].texture) continue;
    struct agx_resource *r=agx_resource(batch->key.cbufs[i].texture);
    if(!r || !r->bo || r->layout.size_B<r->layout.level_offsets_B[0]) return 0;
    attachments[count].size=r->layout.size_B-r->layout.level_offsets_B[0];
    attachments[count].pointer=agx_map_gpu(r);
    ++count;
  }
  if(batch->key.zsbuf.texture) {
    struct agx_resource *r=agx_resource(batch->key.zsbuf.texture);
    if(!r || !r->bo || r->layout.size_B<r->layout.level_offsets_B[0]) return 0;
    attachments[count].size=r->layout.size_B-r->layout.level_offsets_B[0];
    attachments[count].pointer=agx_map_gpu(r);
    ++count;
    if(r->separate_stencil) {
      struct agx_resource *s=agx_resource(r->separate_stencil);
      if(!s || !s->bo || s->layout.size_B<s->layout.level_offsets_B[0])
        return 0;
      attachments[count].size=s->layout.size_B-s->layout.level_offsets_B[0];
      attachments[count].pointer=agx_map_gpu(s);
      ++count;
    }
  }
  if(!count) return 1;
  struct drm_asahi_cmd_header header={0};
  header.cmd_type=DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS;
  header.size=count*sizeof(attachments[0]);
  header.vdm_barrier=DRM_ASAHI_BARRIER_NONE;
  header.cdm_barrier=DRM_ASAHI_BARRIER_NONE;
  return append_native(packet,&header,sizeof(header)) &&
         append_native(packet,attachments,count*sizeof(attachments[0]));
}

static int prepare_process_buffers(AGX_WIN32_ASAHI_BACKEND *b,AGX_G4_BATCH *g,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    APPLE_AGX_G4_PROCESS_RANGE ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT]) {
  APPLE_AGX_G3_PRIVATE_REQUEST request={0};
  if(!b || !g || !render || g->Lease.SceneId || !b->Gpuva.Ops.PrivateEscape)
    return 0;
  request.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;
  request.Version=APPLE_AGX_G3_PRIVATE_VERSION;request.Bytes=sizeof(request);
  request.Operation=APPLE_AGX_G3_PRIVATE_ACQUIRE;
  request.Width=render->WidthPx;request.Height=render->HeightPx;
  request.UtileWidth=render->UtileWidthPx;request.UtileHeight=render->UtileHeightPx;
  request.Layers=render->Layers;request.Samples=render->Samples;
  if(!b->Gpuva.Ops.PrivateEscape(b->Gpuva.Context,&request)) return 0;
  g->Lease=(APPLE_AGX_G4_PRIVATE_LEASE){request.ManagerId,request.ManagerGeneration,
      request.SceneId,request.SceneGeneration};
  memcpy(ranges,request.Ranges,sizeof(request.Ranges));
  return g->Lease.ManagerId && g->Lease.ManagerGeneration &&
      g->Lease.SceneId && g->Lease.SceneGeneration;
}


/* EXP1029: the native batch encodes the colour format in its own PBE/EOT
 * state and the KMD treats attachments as format-agnostic pointer ranges.
 * EXP1028: RGBA8 render targets (shell UI) were refused here. Accept the
 * formats the Asahi batch path encodes whose tile-buffer sample fits the
 * 8/16-byte native layout checked below. */
static int gpuva_color_format_supported(unsigned format) {
  switch(format) {
  case PIPE_FORMAT_B8G8R8A8_UNORM: case PIPE_FORMAT_B8G8R8A8_SRGB:
  case PIPE_FORMAT_B8G8R8X8_UNORM: case PIPE_FORMAT_B8G8R8X8_SRGB:
  case PIPE_FORMAT_R8G8B8A8_UNORM: case PIPE_FORMAT_R16G16B16A16_FLOAT:
  case PIPE_FORMAT_R10G10B10A2_UNORM:
    return 1;
  default:
    return 0;
  }
}

static int batch_has_render_work(const struct agx_batch *batch) {
  return batch && (batch->draws || batch->clear);
}

int AgxWin32AsahiBatchFinish(struct agx_batch *batch,
                             const struct drm_asahi_cmd_render *render) {
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  AGX_G4_BATCH *g=capsule(batch);
  const AGX_WIN32_GPUVA_BO **refs=NULL;
  const AGX_WIN32_GPUVA_BO *written[PIPE_MAX_COLOR_BUFS+2]={0};
  unsigned written_count=0;
  unsigned count=0,limit;
  size_t pool_count, pipeline_count;
  AGX_G4_PRIVATE packet={0};
  APPLE_AGX_G4_PROCESS_RANGE ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT]={{0}};
  APPLE_AGX_G4_NATIVE_RENDER native_render;
  unsigned char *cpu;
  struct drm_asahi_cmd_header command_header;
  unsigned fail_site=0u;
  if(!b || !g || !render || b->Failed || b->Gpuva.Terminal ||
     !batch_has_render_work(batch) || batch->cdm.bo || g->Submitted || g->Rejected ||
     !batch->vdm.bo ||
     batch->key.nr_cbufs!=1 || !batch->key.cbufs[0].texture ||
     !gpuva_color_format_supported(batch->key.cbufs[0].format) ||
     batch->key.zsbuf.texture || render->samples!=1 ||
     (render->sample_size_B!=8 && render->sample_size_B!=16) ||
     batch->bo_list.bit_count>UINT32_MAX-(PIPE_MAX_COLOR_BUFS+17))
    return batch_refuse(2u, __LINE__,
        (batch->key.nr_cbufs & 0xffu) | ((unsigned)(batch->key.zsbuf.texture!=NULL) << 8) |
        ((unsigned)(batch->cdm.bo!=NULL) << 9) | ((unsigned)(batch->draws==0) << 10) |
        ((unsigned)(b && b->Failed) << 11) | ((unsigned)(b && b->Gpuva.Terminal) << 12) |
        ((unsigned)(render ? render->samples : 0) << 16),
        (unsigned)(batch->key.cbufs[0].texture ? batch->key.cbufs[0].format : 0xffffu) |
        ((render ? render->sample_size_B : 0u) << 16));
  if(g->Entered && !AgxWin32AsahiBatchLeave(batch)) { fail_site=__LINE__; goto fail; }
  /* Mesa pools own their slabs separately from the batch handle bitset.
   * Every slab must join the canonical residency/copy transaction, including
   * earlier slabs after rollover and the low-VA pipeline pool. */
  pool_count=util_dynarray_num_elements(&batch->pool.bos,struct agx_bo *);
  pipeline_count=util_dynarray_num_elements(&batch->pipeline_pool.bos,struct agx_bo *);
  limit=batch->bo_list.bit_count+PIPE_MAX_COLOR_BUFS+17;
  if(pool_count>UINT32_MAX-limit) { fail_site=__LINE__; goto fail; }
  limit+=(unsigned)pool_count;
  if(pipeline_count>UINT32_MAX-limit) { fail_site=__LINE__; goto fail; }
  limit+=(unsigned)pipeline_count;
  memcpy(&native_render,render,sizeof(native_render));
  /* The first G13 scene constructor uses one cluster. Asahi selects this
   * firmware path with the UAPI NO_VERTEX_CLUSTERING bit. */
  native_render.Flags|=1u<<2;
  if(!prepare_process_buffers(b,g,&native_render,ranges)) { fail_site=__LINE__; goto fail; }
#ifdef _MSC_VER
  if(AgxWin32VdmTraceHook && batch->vdm.bo) {
    const struct agx_bo *vbo=batch->vdm.bo;
    const unsigned char *map=(const unsigned char *)agx_bo_map((struct agx_bo *)vbo);
    uint64_t base=vbo->va ? vbo->va->addr : 0;
    if(map && base && render->vdm_ctrl_stream_base>=base &&
       render->vdm_ctrl_stream_base+52<=base+vbo->size)
      AgxWin32VdmTraceHook(render->vdm_ctrl_stream_base,
          (const uint32_t *)(map+(render->vdm_ctrl_stream_base-base)),13u,
          (unsigned)batch->draws);
  }
#endif
  if(!append_attachments(batch,&packet)) { fail_site=__LINE__; goto fail; }
  command_header=agx_cmd_header(false,0,0);
  if(!append_native(&packet,&command_header,sizeof(command_header)) ||
     !append_native(&packet,&native_render,sizeof(native_render))) { fail_site=__LINE__; goto fail; }
  g->Command=agx_bo_create(b->Native,packet.Header.V2.Base.CommandBytes,
                           0,0,"VA command");
  if(!g->Command) { fail_site=__LINE__; goto fail; }
  cpu=agx_bo_map(g->Command);
  if(!cpu || !AgxWin32AsahiGpuvaBo(b,g->Command)) { fail_site=__LINE__; goto fail; }
  memcpy(cpu,packet.Native,packet.Header.V2.Base.CommandBytes);
  if(!AppleAgxG4ComposeHeaderV3(&packet.Header,&native_render,
      g->Command->va->addr,packet.Header.V2.Base.CommandBytes,
      APPLE_AGX_G4_COLOR_BGRA8,ranges,&g->Lease)) { fail_site=__LINE__; goto fail; }
  refs=calloc(limit,sizeof(*refs));
  if(!refs) { fail_site=__LINE__; goto fail; }
  if(!add_bo(b,refs,&count,limit,g->Command) ||
     !add_bo(b,refs,&count,limit,batch->vdm.bo) ||
     !add_bo(b,refs,&count,limit,agx_screen(batch->ctx->base.screen)->rodata))
    { fail_site=__LINE__; goto fail; }
  for(unsigned i=0;i<batch->key.nr_cbufs;++i) {
    if(batch->key.cbufs[i].texture) {
      struct agx_bo *color=agx_resource(batch->key.cbufs[i].texture)->bo;
      if(!add_bo(b,refs,&count,limit,color)) { fail_site=__LINE__; goto fail; }
      const AGX_WIN32_GPUVA_BO *mapped=AgxWin32AsahiGpuvaBo(b,color);
      if(!mapped) { fail_site=__LINE__; goto fail; }
      unsigned duplicate=0;
      for(unsigned j=0;j<written_count;++j)
        if(written[j]==mapped) duplicate=1;
      if(!duplicate) written[written_count++]=mapped;
    }
  }
  if(batch->key.zsbuf.texture) {
    struct agx_resource *depth=agx_resource(batch->key.zsbuf.texture);
    if(!add_bo(b,refs,&count,limit,depth->bo) ||
       (depth->separate_stencil &&
        !add_bo(b,refs,&count,limit,
          agx_resource(depth->separate_stencil)->bo))) { fail_site=__LINE__; goto fail; }
    /* EXP985: depth/stencil are written too; only written BOs are
     * downloaded back to their CPU staging after the submission. */
    struct agx_bo *zs[2]={depth->bo,depth->separate_stencil?
        agx_resource(depth->separate_stencil)->bo:NULL};
    for(unsigned k=0;k<2;++k) if(zs[k]) {
      const AGX_WIN32_GPUVA_BO *mapped=AgxWin32AsahiGpuvaBo(b,zs[k]);
      if(!mapped) { fail_site=__LINE__; goto fail; }
      unsigned duplicate=0;
      for(unsigned j=0;j<written_count;++j)
        if(written[j]==mapped) duplicate=1;
      if(!duplicate) written[written_count++]=mapped;
    }
  }
  int handle;
  AGX_BATCH_FOREACH_BO_HANDLE(batch,handle) {
    struct agx_bo *referenced=AgxWin32AsahiLookupBo(b->Native,handle);
    if(!referenced || !add_bo(b,refs,&count,limit,referenced))
      { fail_site=__LINE__; goto fail; }
  }
  util_dynarray_foreach(&batch->pool.bos,struct agx_bo *,bo) {
    if(!*bo || !add_bo(b,refs,&count,limit,*bo)) { fail_site=__LINE__; goto fail; }
  }
  util_dynarray_foreach(&batch->pipeline_pool.bos,struct agx_bo *,bo) {
    if(!*bo || !add_bo(b,refs,&count,limit,*bo)) { fail_site=__LINE__; goto fail; }
  }
  if(!AgxWin32GpuvaSubmit(&b->Gpuva,refs,count,
      AgxWin32AsahiGpuvaBo(b,g->Command),packet.Header.V2.Base.CommandBytes,
      written,written_count,
      &packet,packet.Header.V2.Base.HeaderBytes+packet.Header.V2.Base.CommandBytes,
      &g->Fence)) { fail_site=__LINE__; goto fail; }
  g->Submitted=1;
  free(refs);
  return 1;
fail:
  free(refs);
  (void)batch_refuse(3u, fail_site, b->Gpuva.LastFailure, b->Gpuva.LastDetail);
  g->Rejected=1;
  /* Native submission also marks the context faulted. Publish that failure
   * to Windows FlushStatus even when residency rollback completed safely.
   * Keep Gpuva.Terminal separate: a rejected batch can still be released. */
  b->Failed=1;
  return 0;
}

int AgxWin32AsahiBatchPoll(struct agx_batch *batch,APPLE_AGX_U32 timeout) {
  AGX_G4_BATCH *g=capsule(batch);
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  (void)timeout;
  if(!g || g->Retired || g->Rejected) return 1;
  if(!b || !g->Submitted || !g->Fence) return 0;
  if(!AgxWin32GpuvaRetire(&b->Gpuva,g->Fence)) return 0;
  g->Retired=1;
  return 1;
}
int AgxWin32AsahiBatchAbort(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  if(!g) return 1;
  if(g->Submitted && !g->Retired) return 0;
  g->Entered=0;
  g->Rejected=1;
  return 1;
}
int AgxWin32AsahiBatchRelease(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  if(!g) return 1;
  if(!b || b->Gpuva.Terminal || (!g->Retired && !g->Rejected) || g->Entered)
    return 0;
  if(g->Lease.SceneId) {
    APPLE_AGX_G3_PRIVATE_REQUEST request={0};
    request.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;request.Version=APPLE_AGX_G3_PRIVATE_VERSION;
    request.Bytes=sizeof(request);request.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
    request.ManagerId=g->Lease.ManagerId;request.ManagerGeneration=g->Lease.ManagerGeneration;
    request.SceneId=g->Lease.SceneId;request.SceneGeneration=g->Lease.SceneGeneration;
    if(!b->Gpuva.Ops.PrivateEscape ||
       !b->Gpuva.Ops.PrivateEscape(b->Gpuva.Context,&request)) return 0;
  }
  if(g->Command) agx_bo_unreference(b->Native,g->Command);
  batch->windows_batch=NULL;
  free(g);
  return 1;
}
