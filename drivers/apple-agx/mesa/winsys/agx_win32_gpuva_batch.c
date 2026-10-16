#include "gallium/drivers/asahi/agx_state.h"
#include "agx_win32_asahi_batch.h"
#include "agx_win32_asahi_bo.h"
#include "agx_device.h"
#include "apple_agx_g4_submit.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "util/os_time.h"
#include "util/format/u_format.h"

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
/* D3D-G1: a render batch whose draws launched compute work submits the v5
 * envelope (v3 + the compute preemption buffer) over the same command. */
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V5 Header;
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
} AGX_G4_PRIVATE_V5;
_Static_assert(offsetof(AGX_G4_PRIVATE_V5,Native)==
               sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V5),
               "G4 render-compute private header layout");
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
_Static_assert(sizeof(struct drm_asahi_cmd_compute)==
               sizeof(APPLE_AGX_G4_NATIVE_COMPUTE), "native compute layout");
_Static_assert(offsetof(struct drm_asahi_cmd_compute,cdm_ctrl_stream_base)==
               offsetof(APPLE_AGX_G4_NATIVE_COMPUTE,CdmCtrlStreamBase), "CDM VA");
_Static_assert(offsetof(struct drm_asahi_cmd_compute,sampler_heap)==
               offsetof(APPLE_AGX_G4_NATIVE_COMPUTE,SamplerHeap), "compute samplers");
_Static_assert(offsetof(struct drm_asahi_cmd_compute,helper)==
               offsetof(APPLE_AGX_G4_NATIVE_COMPUTE,Helper), "compute helper");
_Static_assert(offsetof(struct drm_asahi_cmd_compute,ts)==
               offsetof(APPLE_AGX_G4_NATIVE_COMPUTE,Timestamps), "compute timestamps");
_Static_assert(offsetof(APPLE_AGX_G4_COMPUTE_ENVELOPE,Native)==
               sizeof(APPLE_AGX_G4_PRIVATE_HEADER_COMPUTE), "compute envelope layout");
/* The UMD bridge marks at most this many written allocations per submission
 * (D3DDDI_MAX_WRITTEN_PRIMARIES in umd_gpuva_windows.c submit). */
#define AGX_G4_WRITTEN_LIMIT 16u

typedef struct {
  struct agx_bo *Command;
  /* G2: the CDM context-store buffer of a compute job, in this process's VM
   * (Asahi allocates one per job from the user VM, queue/compute.rs). */
  struct agx_bo *Preemption;
  APPLE_AGX_G4_PRIVATE_LEASE Lease;
  uint64_t Fence;
  unsigned Entered, ComputeEntered, Submitted, Retired, Rejected;
  /* GPU timestamps: where Mesa's timestamp buffer takes each value
   * (TimestampEnd 0: the job's start, 1: its end). */
  unsigned TimestampCount;
  uint32_t TimestampOffset[4];
  unsigned char TimestampEnd[4];
} AGX_G4_BATCH;

/* GPU timestamps (docs/superpowers/plans/2026-10-16-gpu-timestamps.md).
 * Asahi names Mesa's timestamp buffer by a bound handle and offset, and the
 * firmware writes it. Here the KMD keeps the firmware values (render: the
 * 3D work's start/end; compute: the requested block) and returns them by
 * escape (Ops.GpuTimestamps), so the G4 command names no Mesa handle -- a
 * standalone compute job carries APPLE_AGX_G4_TIMESTAMP_REQUEST -- and the
 * capsule keeps the offsets; AgxWin32AsahiBatchTimestamps writes the values
 * there after the batch retired, before Mesa reads them (agx_batch_cleanup:
 * Poll, Timestamps, Release). */
static void keep_timestamp(AGX_G4_BATCH *g,uint32_t handle,uint32_t offset,
                           unsigned char end) {
  if(!handle || g->TimestampCount==4u) return;
  g->TimestampOffset[g->TimestampCount]=offset;
  g->TimestampEnd[g->TimestampCount++]=end;
}
static void keep_compute_timestamps(AGX_G4_BATCH *g,APPLE_AGX_G4_TIMESTAMPS *ts,
                                    int request) {
  keep_timestamp(g,ts->StartHandle,ts->StartOffset,0u);
  keep_timestamp(g,ts->EndHandle,ts->EndOffset,1u);
  ts->StartHandle=request && ts->StartHandle ? APPLE_AGX_G4_TIMESTAMP_REQUEST : 0u;
  ts->EndHandle=request && ts->EndHandle ? APPLE_AGX_G4_TIMESTAMP_REQUEST : 0u;
  ts->StartOffset=ts->EndOffset=0u;
}
static void write_timestamps(struct agx_batch *batch,AGX_WIN32_ASAHI_BACKEND *b,
                             AGX_G4_BATCH *g) {
  uint64_t start=0,end=0,frequency=0,qpc=0;
  unsigned char *map;
  if(!g->TimestampCount || !g->Command || !g->Command->va ||
     !batch->ctx || !batch->ctx->timestamps || !b->Gpuva.Ops.GpuTimestamps ||
     !b->Gpuva.Ops.GpuTimestamps(b->Gpuva.Context,g->Command->va->addr,
                                 &start,&end,&frequency,&qpc)) return;
  map=(unsigned char *)agx_bo_map(batch->ctx->timestamps);
  if(!map) return;
  for(unsigned i=0;i<g->TimestampCount;++i) {
    const uint64_t value=g->TimestampEnd[i] ? end : (start ? start : end);
    if(g->TimestampOffset[i]>batch->ctx->timestamps->size-sizeof(value)) continue;
    memcpy(map+g->TimestampOffset[i],&value,sizeof(value));
  }
  b->TimestampFrequency=frequency;
  b->TimestampOffset=end-qpc;
}

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
/* MSAA-r1 receipt-only (EXP1299-EXP1305: draws into 4x targets write no
 * sample while clears and copies reach them): the render fields that carry
 * the sample count, for every multisampled render, as submitted. */
void (*AgxWin32RenderTraceHook)(const uint32_t *values, unsigned count);
/* EXP1317 receipt-only: every pixel shader whose indexable temporary Asahi
 * lowers to scratch (above 256 bytes) hung the GPU (agx_scratch_smoke n=24,
 * DEVICE_HUNG) with the SGX core topology reported. The fragment helper
 * fields of the render and the helper header Mesa built (agx_scratch.c;
 * libagx/helper.h agx_helper_header: u32 subgroups, u32 pad, then 88-byte
 * agx_helper_core entries {u64 blocklist, alloc_cur, alloc_max,
 * alloc_failed, pad, alloc_count[16]}) tell whether the helper ever ran:
 * site 1 at submission, site 2 when the context is destroyed. */
void (*AgxWin32ScratchTraceHook)(const uint32_t *values, unsigned count);
void AgxWin32AsahiScratchReceipt(struct agx_context *ctx, unsigned site,
                                 const struct drm_asahi_cmd_render *render) {
  uint32_t values[15]={0};
  const unsigned char *hdr;
  if(!AgxWin32ScratchTraceHook || !ctx || !ctx->scratch_fs.buf || !ctx->scratch_fs.header)
    return;
  hdr=(const unsigned char *)ctx->scratch_fs.header;
  values[0]=site;
  memcpy(&values[1],hdr,4);
  values[2]=render ? render->fragment_helper.binary : 0u;
  values[3]=render ? (uint32_t)render->fragment_helper.data : (uint32_t)ctx->scratch_fs.buf->va->addr;
  values[4]=render ? (uint32_t)(render->fragment_helper.data>>32) : (uint32_t)(ctx->scratch_fs.buf->va->addr>>32);
  values[5]=render ? render->fragment_helper.cfg : ctx->scratch_fs.size_dwords;
  for(unsigned core=0;core<8u;++core) {
    const unsigned char *c=hdr+8u+88u*core;
    uint32_t cur,max,failed,sum=0,count;
    memcpy(&cur,c+8,4); memcpy(&max,c+12,4); memcpy(&failed,c+16,4);
    for(unsigned i=0;i<16u;++i) { memcpy(&count,c+24+4*i,4); sum+=count; }
    values[6+core]=(cur&0xffu)|((max&0xffu)<<8)|((failed>0xffu?0xffu:failed)<<16)|
        ((sum>0xffu?0xffu:sum)<<24);
  }
  memcpy(&values[14],hdr+8u+88u*1u,4); /* core 1 block list, low word */
  AgxWin32ScratchTraceHook(values,15u);
}
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

/* The batch of this context whose submission completes at `fence`. */
static struct agx_batch *submitted_at(struct agx_batch *batch,uint64_t fence) {
  for(unsigned i=0;fence && i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *old=&batch->ctx->batches.slots[i];
    AGX_G4_BATCH *g=capsule(old);
    if(old==batch || !g || !g->Submitted || g->Retired || g->Rejected ||
       g->Fence!=fence) continue;
    return old;
  }
  return NULL;
}
/* EXP1071: several open batches may be flushed back to back (agx_flush_all).
 * EXP1093: several submissions may be in flight (EXP1090: 26 % of DWM's
 * wall time waited for the previous one before each submission; EXP1203:
 * AGX_GPUVA_IN_FLIGHT). Make room by retiring (waiting for) the oldest one
 * only; the newer ones stay in flight while this batch submits.
 * EXP1161: under WGL several GL contexts share one screen, its G4 context
 * and fence timeline, so a set may belong to another context's batch; it is
 * retired through the space by its fence, as a CPU map does (native_map),
 * and that batch later retires by the same fence. */
static int retire_held(struct agx_batch *batch,AGX_WIN32_ASAHI_BACKEND *b) {
  /* EXP1203: room is needed only when every older slot is taken. */
  if(b->Gpuva.OlderCount<AGX_GPUVA_IN_FLIGHT-1u) return 1;
  uint64_t oldest=b->Gpuva.Older[0].Fence;
  struct agx_batch *old=submitted_at(batch,oldest);
  if(!old) return AgxWin32GpuvaRetire(&b->Gpuva,oldest) &&
                  b->Gpuva.OlderCount<AGX_GPUVA_IN_FLIGHT-1u;
  return AgxWin32AsahiBatchPoll(old,1000) &&
         b->Gpuva.OlderCount<AGX_GPUVA_IN_FLIGHT-1u;
}

int AgxWin32AsahiBatchBegin(struct agx_batch *batch) {
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  if(batch && batch->ctx) AgxWin32AsahiPublishPages(agx_device(batch->ctx->base.screen));
  /* G2: a compute batch (agx_get_compute_batch) owns a CDM encoder instead. */
  if(!b || !b->GpuvaReady || b->Failed || batch->windows_batch ||
     !(agx_batch_is_compute(batch) ? batch->cdm.bo && !batch->vdm.bo :
                                     batch->vdm.bo != NULL))
    return batch_refuse(1u, __LINE__, 0u, 0u);
  /* EXP1071: other active batches stay open, as in Linux Mesa; Asahi's
   * hazard tracking flushes one when a dependency, a CPU access or a Gallium
   * flush needs it. (Flushing them here caused 66% of DWM's submissions in
   * EXP1070.) A completed submitted batch is retired and cleaned up here.
   * EXP1082: one still running on the GPU stays submitted -- Mesa counts it
   * as a user of its resources until agx_sync_batch -- and the next
   * submission retires it (retire_held), so this batch is built meanwhile. */
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *old=&batch->ctx->batches.slots[i];
    if(old==batch || !old->windows_batch ||
       BITSET_TEST(batch->ctx->batches.active,i)) continue;
    AGX_G4_BATCH *og=capsule(old);
    if(og->Submitted && og->Fence && !og->Retired && !og->Rejected &&
       !AgxWin32GpuvaComplete(&b->Gpuva,og->Fence)) {
      AgxWin32PerfNote("Begin keep: in flight");
      continue;
    }
    AgxWin32PerfNote("Begin drain: retire submitted");
    if(batch->ctx->any_faults || !AgxWin32AsahiBatchPoll(old,1000)) return batch_refuse(1u, __LINE__, 0u, 0u);
    agx_sync_batch(batch->ctx,old);
    if(old->windows_batch) return batch_refuse(1u, __LINE__, 0u, 0u);
  }
  AGX_G4_BATCH *g=calloc(1,sizeof(*g));
  if(!g) return batch_refuse(1u, __LINE__, 0u, 0u);
  batch->windows_batch=g;
  return 1;
}
/* EXP1166: the Asahi draw path re-enters draw_vbo on the current batch
 * (util_draw_multi for num_draws > 1, unrolled/emulated indirect draws,
 * transform-feedback draws, primitive-restart emulation), so Entered is a
 * depth: each draw enters and leaves one level. A one-level flag refused
 * every nested draw and faulted the GL context (CS 1.6 froze).
 * EXP1164 receipt-only: a refused Enter reports reject-batch kind 7 (1 no
 * capsule, 2 submitted, 4 rejected; fence, draws). */
int AgxWin32AsahiBatchEnter(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  if(!g || g->Submitted || g->Rejected)
    return batch_refuse(7u,(unsigned)(!g)|((unsigned)(g && g->Submitted)<<1)|
        ((unsigned)(g && g->Rejected)<<2),
        g ? (unsigned)g->Fence : 0u, batch ? batch->draws : 0u);
  ++g->Entered;
  return 1;
}
/* EXP1167: agx_draw_vbo ends with flushes (scissor/depth-bias or sampler
 * heap overflow) that submit the batch after the draw is encoded; Finish
 * leaves every level, so that draw's Leave succeeds. Any other unbalanced
 * Leave is refused (reject-batch kind 8: 1 no capsule, 4 rejected,
 * 8 not entered; fence, draws). */
int AgxWin32AsahiBatchLeave(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  if(g && g->Submitted && !g->Rejected) return 1;
  if(!g || g->Rejected || !g->Entered)
    return batch_refuse(8u,(unsigned)(!g)|((unsigned)(g && g->Rejected)<<2)|
        ((unsigned)(g && !g->Entered)<<3),
        g ? (unsigned)g->Fence : 0u, batch ? batch->draws : 0u);
  --g->Entered;
  return 1;
}
/* EXP1308: an indirect draw takes its counts from its argument buffer (or a
 * stream-output count); Asahi's own indirect draws pass no direct draws. */
static int indirect_arguments(const struct pipe_draw_indirect_info *indirect) {
  return indirect->buffer!=NULL || indirect->count_from_stream_output!=NULL;
}
int AgxWin32AsahiBatchPrepareDraw(struct agx_batch *batch,
    const struct pipe_draw_info *info,const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draw) {
  AGX_G4_BATCH *g=capsule(batch);
  return g && !g->Submitted && !g->Rejected && info &&
         (indirect ? indirect_arguments(indirect) :
                     draw && draw->count && info->instance_count);
}
int AgxWin32AsahiBatchDrawAllowed(struct agx_context *ctx,
    const struct pipe_draw_info *info,unsigned drawid,
    const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draws,unsigned count) {
  (void)drawid;
  /* EXP1288: or no colour buffer and a depth buffer -- the depth-only pass
   * the batch gate and the KMD admit (D3D-G7). D3D-G11: or neither, on a
   * framebuffer the frontend sized (stream output, UAV-only pixel shaders). */
  return ctx && !ctx->any_faults && info &&
         (indirect ? indirect_arguments(indirect) :
                     draws && count && info->instance_count) &&
         (ctx->framebuffer.nr_cbufs ? ctx->framebuffer.cbufs[0].texture != NULL :
          ctx->framebuffer.zsbuf.texture != NULL ||
          (ctx->framebuffer.width && ctx->framebuffer.height));
}
/* EXP1063: a draw DrawAllowed refuses has touched no batch state, so it is
 * dropped instead of poisoning the context (EXP1062: Notepad lost every
 * later flush after one such draw). The first refusals are reported
 * (reject-batch kind 6): reason bits 1 no draw, 2 indirect without
 * arguments (EXP1308; before it every indirect draw), 4 no instances,
 * 8 neither a color nor a depth buffer on a zero-sized framebuffer
 * (D3D-G11), 16 color buffer 0 unbound;
 * nr_cbufs | depth << 8; vertex count. */
int AgxWin32AsahiBatchDrawRefused(struct agx_context *ctx,
    const struct pipe_draw_info *info,
    const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draws,unsigned count) {
  static unsigned reported;
  unsigned reason=0u;
  if(!ctx) return 1;
  if(indirect) {
    if(!indirect_arguments(indirect)) reason|=2u;
  } else {
    if(!info || !draws || !count) reason|=1u;
    if(info && !info->instance_count) reason|=4u;
  }
  if(!ctx->framebuffer.nr_cbufs) {
    if(!ctx->framebuffer.zsbuf.texture &&
       (!ctx->framebuffer.width || !ctx->framebuffer.height)) reason|=8u;
  } else if(!ctx->framebuffer.cbufs[0].texture) reason|=16u;
  if(reported<32u) {
    ++reported;
    (void)batch_refuse(6u,reason,(unsigned)ctx->framebuffer.nr_cbufs|
        ((unsigned)(ctx->framebuffer.zsbuf.texture!=NULL)<<8),
        draws && count ? draws[0].count : 0u);
  }
  return 0;
}
/* G2: a compute-only batch submits one AGX4 compute envelope (KMD admits it
 * as a standalone RunCompute job). */
static int compute_only(const struct agx_batch *batch) {
  return batch && agx_batch_is_compute(batch) && batch->cdm.bo && !batch->vdm.bo;
}
/* D3D-G1: the CDM work of a render batch (geometry shader prerasterization,
 * tessellation, IA statistics, restart unrolling, internal kernels) is one
 * compute command the render waits on, in one v5 envelope (Mesa
 * agx_batch_submit: COMPUTE, attachments, RENDER with barriers NONE/1). */
static int render_compute(const struct agx_batch *batch) {
  return batch && !agx_batch_is_compute(batch) && batch->cdm.bo && batch->vdm.bo;
}
int AgxWin32AsahiBatchComputeEnter(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  int scope=compute_only(batch) || render_compute(batch);
  if(!scope || !g || g->Submitted || g->Rejected)
    return batch_refuse(10u,(unsigned)!scope|((unsigned)!g<<1),
        g ? (unsigned)g->Fence : 0u, 0u);
  ++g->ComputeEntered;
  return 1;
}
int AgxWin32AsahiBatchComputeLeave(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  if(!g || g->Rejected || !g->ComputeEntered) return batch_refuse(10u,4u,0u,0u);
  --g->ComputeEntered;
  return 1;
}
/* The stream ends in the encoder itself: the end Mesa reports is an offset
 * into that buffer (agx_flush_compute), meaningless once the stream has
 * continued in a pool chunk, so such a batch is refused. */
int AgxWin32AsahiBatchComputeFinalize(struct agx_batch *batch,const void *end) {
  AGX_G4_BATCH *g=capsule(batch);
  const unsigned char *map=compute_only(batch) || render_compute(batch) ?
      (const unsigned char *)agx_bo_map(batch->cdm.bo) : NULL;
  if(!g || g->Submitted || g->Rejected || g->ComputeEntered || !map ||
     (const unsigned char *)end<map ||
     (size_t)((const unsigned char *)end-map)+4u>batch->cdm.bo->size)
    return batch_refuse(10u,8u,g ? (unsigned)g->Fence : 0u,0u);
  return 1;
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
  if(!count) {
    /* D3D-G11: a render without colour and depth/stencil (stream output,
     * UAV-only pixel shaders) names the device scratch page -- the sink
     * Asahi gives every unbound output -- as the KMD's attachment 0. */
    const struct agx_bo *sink=agx_device(batch->ctx->base.screen)->scratch_bo;
    if(!sink || !sink->va || !sink->va->addr || !sink->size) return 0;
    attachments[0].pointer=sink->va->addr;
    attachments[0].size=sink->size;
    count=1;
  }
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
 * EXP1028: RGBA8 render targets (shell UI) were refused here.
 * D3D-G10a: every format the Asahi screen renders whose block size has a
 * KMD colour class (1, 2, 4, 8 or 16 bytes) -- the bring-up list of ten
 * formats refused D3D render targets the screen supports. */
static int gpuva_color_format_supported(const struct agx_batch *batch, unsigned format) {
  unsigned bytes = util_format_get_blocksize(format);
  return format != PIPE_FORMAT_NONE &&
      (bytes == 1u || bytes == 2u || bytes == 4u || bytes == 8u || bytes == 16u) &&
      AgxWin32AsahiRenderableFormat(batch, format);
}

static int gpuva_color_formats_supported(const struct agx_batch *batch) {
  for(unsigned i=0;i<batch->key.nr_cbufs;++i)
    if(batch->key.cbufs[i].texture &&
       !gpuva_color_format_supported(batch, batch->key.cbufs[i].format)) return 0;
  return 1;
}

/* D3D-G10a: the screen's render-target answer for a batch's format. */
int AgxWin32AsahiRenderableFormat(const struct agx_batch *batch, unsigned format) {
  struct pipe_screen *screen = batch && batch->ctx ? batch->ctx->base.screen : NULL;
  return screen && screen->is_format_supported(screen, (enum pipe_format)format,
      PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_RENDER_TARGET);
}

/* D3D-G10a: the KMD sizes attachment 0 by its format's block size. */
static unsigned gpuva_color_class(unsigned format) {
  switch(util_format_get_blocksize(format)) {
  case 1: return APPLE_AGX_G4_COLOR_1BYTE;
  case 2: return APPLE_AGX_G4_COLOR_2BYTE;
  case 8: return APPLE_AGX_G4_COLOR_8BYTE;
  case 16: return APPLE_AGX_G4_COLOR_16BYTE;
  default: return APPLE_AGX_G4_COLOR_BGRA8;
  }
}

static int batch_has_render_work(const struct agx_batch *batch) {
  return batch && (batch->draws || batch->clear);
}

/* EXP1207: the batch pools and the encoder are recorded into cached shadows
 * (AGX_WIN32_BO_SHADOW). Copy what this batch wrote into the allocations
 * before the submission names them: a pool's transient slab up to its offset,
 * earlier slabs whole; the encoder up to its current pointer plus the
 * terminator agx_flush_render writes past it, or whole when the stream
 * continued in a pool chunk (agx_ensure_cmdbuf_has_space). */
static size_t pool_extent(const struct agx_pool *pool,const struct agx_bo *bo) {
  return bo==pool->transient_bo ? pool->transient_offset : bo->size;
}
static int publish_shadows(struct agx_batch *batch) {
  /* The batch's encoders: VDM for a render batch, CDM for a compute one,
   * both for a render batch that launched compute work (D3D-G1);
   * agx_flush_compute writes its terminator at the current pointer. */
  struct agx_encoder *encoders[2]={&batch->vdm,&batch->cdm};
  for(unsigned i=0;i<2u;++i) {
    struct agx_bo *stream=encoders[i]->bo;
    if(!stream) continue;
    uintptr_t map=(uintptr_t)agx_bo_map(stream),current=(uintptr_t)encoders[i]->current;
    size_t used=stream->size;
    if(map && current>=map && current-map<=stream->size)
      used=(size_t)(current-map)+AGX_WIN32_ENCODER_TAIL;
    if(!AgxWin32AsahiShadowPublish(stream,used)) return 0;
  }
  util_dynarray_foreach(&batch->pool.bos,struct agx_bo *,bo)
    if(!AgxWin32AsahiShadowPublish(*bo,pool_extent(&batch->pool,*bo))) return 0;
  util_dynarray_foreach(&batch->pipeline_pool.bos,struct agx_bo *,bo)
    if(!AgxWin32AsahiShadowPublish(*bo,pool_extent(&batch->pipeline_pool,*bo))) return 0;
  return 1;
}

/* D3D-G1: `compute` is the batch's CDM command when its draws launched
 * compute work (exactly when the batch has a CDM encoder); the render then
 * waits on it in the same submission. */
int AgxWin32AsahiBatchFinish(struct agx_batch *batch,
                             const struct drm_asahi_cmd_render *render,
                             const struct drm_asahi_cmd_compute *compute) {
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  AGX_G4_BATCH *g=capsule(batch);
  const AGX_WIN32_GPUVA_BO **refs=NULL;
  const AGX_WIN32_GPUVA_BO *written[AGX_G4_WRITTEN_LIMIT]={0};
  unsigned written_count=0;
  unsigned count=0,limit;
  size_t pool_count, pipeline_count;
  AGX_G4_PRIVATE packet={0};
  AGX_G4_PRIVATE_V5 *with_compute=NULL;
  const void *private_data=&packet;
  unsigned private_bytes;
  /* EXP1339: a helper program (compute scratch) is carried by the compute
   * command; Mesa adds the scratch buffer to the batch (agx_flush_compute).
   * Its timestamps come back as the render's (keep_compute_timestamps). */
  const int compute_refused=compute &&
      (!render_compute(batch) || g==NULL || g->ComputeEntered || compute->flags);
  APPLE_AGX_G4_NATIVE_COMPUTE compute_command;
  APPLE_AGX_G4_PROCESS_RANGE ranges[APPLE_AGX_G4_PROCESS_RANGE_COUNT]={{0}};
  APPLE_AGX_G4_NATIVE_RENDER native_render;
  unsigned char *cpu;
  struct drm_asahi_cmd_header command_header;
  unsigned fail_site=0u;
  /* References besides the bo_list bitset and the colour buffers: command,
   * VDM, rodata, zero and scratch pages (EXP1069), depth/stencil, the CDM
   * stream, compute preemption and sampler heap (D3D-G1), and slack.
   * The entry guard and the reference limit must use the same count. */
  enum { fixed_refs=19u };
  if(!b || !g || !render || b->Failed || b->Gpuva.Terminal ||
     !batch_has_render_work(batch) || (compute!=NULL)!=(batch->cdm.bo!=NULL) ||
     compute_refused || g->Submitted || g->Rejected ||
     !batch->vdm.bo ||
     /* D3D-G2b: up to PIPE_MAX_COLOR_BUFS colour targets; the first is
      * the KMD's bound output, every bound one a supported format.
      * D3D-G7: or none, when a depth buffer is bound (depth-only pass).
      * D3D-G11: or neither (the scratch page is attachment 0). */
     (batch->key.nr_cbufs!=0 &&
      (batch->key.nr_cbufs>PIPE_MAX_COLOR_BUFS ||
       !batch->key.cbufs[0].texture || !gpuva_color_formats_supported(batch))) ||
     (batch->key.zsbuf.texture && !b->ZlsAttachments) ||
     /* D3D-G5: the KMD encodes 1, 2 and 4 samples (Asahi render.rs). */
     (render->samples!=1 && render->samples!=2 && render->samples!=4) ||
     /* EXP1319: a fragment helper (scratch) is carried by the render.
      * EXP1338: so is a vertex-stage helper -- the G4 builder encodes
      * RunVertex uses_scratch (DRM_ASAHI_RENDER_VERTEX_SCRATCH). */
     /* D3D-G6: any 8-byte multiple whose utile fits the tile buffer. */
     !AppleAgxG4TileBufferValid(render->sample_size_B,render->samples,
         render->utile_width_px,render->utile_height_px) ||
     batch->bo_list.bit_count>UINT32_MAX-(PIPE_MAX_COLOR_BUFS+fixed_refs))
    return batch_refuse(2u, __LINE__,
        (batch->key.nr_cbufs & 0xffu) | ((unsigned)(batch->key.zsbuf.texture!=NULL) << 8) |
        ((unsigned)(batch->cdm.bo!=NULL) << 9) | ((unsigned)(batch->draws==0) << 10) |
        ((unsigned)(b && b->Failed) << 11) | ((unsigned)(b && b->Gpuva.Terminal) << 12) |
        ((unsigned)compute_refused << 13) | ((unsigned)(compute!=NULL) << 14) |
        ((unsigned)(render ? render->samples : 0) << 16),
        (unsigned)(batch->key.cbufs[0].texture ? batch->key.cbufs[0].format : 0xffffu) |
        ((render ? render->sample_size_B : 0u) << 16));
  /* A flush inside a (nested) draw leaves every level. */
  g->Entered=0;
  /* Retire (and return the scene lease of) the previous completed
   * submission before this batch acquires its own. */
  if(!retire_held(batch,b)) { fail_site=__LINE__; goto fail; }
  /* Mesa pools own their slabs separately from the batch handle bitset.
   * Every slab must join the canonical residency/copy transaction, including
   * earlier slabs after rollover and the low-VA pipeline pool. */
  pool_count=util_dynarray_num_elements(&batch->pool.bos,struct agx_bo *);
  pipeline_count=util_dynarray_num_elements(&batch->pipeline_pool.bos,struct agx_bo *);
  limit=batch->bo_list.bit_count+PIPE_MAX_COLOR_BUFS+fixed_refs;
  if(pool_count>UINT32_MAX-limit) { fail_site=__LINE__; goto fail; }
  limit+=(unsigned)pool_count;
  if(pipeline_count>UINT32_MAX-limit) { fail_site=__LINE__; goto fail; }
  limit+=(unsigned)pipeline_count;
  memcpy(&native_render,render,sizeof(native_render));
  g->TimestampCount=0u;
  keep_timestamp(g,native_render.TimestampsVertex.StartHandle,
                 native_render.TimestampsVertex.StartOffset,0u);
  keep_timestamp(g,native_render.TimestampsFragment.EndHandle,
                 native_render.TimestampsFragment.EndOffset,1u);
  memset(&native_render.TimestampsVertex,0,sizeof(native_render.TimestampsVertex));
  memset(&native_render.TimestampsFragment,0,sizeof(native_render.TimestampsFragment));
  /* The first G13 scene constructor uses one cluster. Asahi selects this
   * firmware path with the UAPI NO_VERTEX_CLUSTERING bit. */
  native_render.Flags|=1u<<2;
  if(!prepare_process_buffers(b,g,&native_render,ranges)) { fail_site=__LINE__; goto fail; }
#ifdef _MSC_VER
  if(AgxWin32RenderTraceHook && native_render.Samples>1) {
    const uint32_t values[14]={native_render.Samples,native_render.SampleSizeBytes,
        native_render.UtileWidthPx|((uint32_t)native_render.UtileHeightPx<<8),
        native_render.WidthPx|((uint32_t)native_render.HeightPx<<16),
        (uint32_t)native_render.PppMultisampleCtrl,
        (uint32_t)(native_render.PppMultisampleCtrl>>32),native_render.PppCtrl,
        native_render.Flags,(uint32_t)native_render.ZlsCtrl,native_render.Bg.Usc,
        native_render.Eot.Usc,native_render.IspBgObjVals,native_render.IspZlsPixels,
        (uint32_t)batch->draws};
    AgxWin32RenderTraceHook(values,14u);
  }
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
  if(compute) {
    /* The compute command waits on the previous submission on both queues
     * (agx_cmd_header(true, 0, 0)); the render waits on it below. */
    command_header=agx_cmd_header(true,0,0);
    memcpy(&compute_command,compute,sizeof(compute_command));
    keep_compute_timestamps(g,&compute_command.Timestamps,0);
    if(!append_native(&packet,&command_header,sizeof(command_header)) ||
       !append_native(&packet,&compute_command,sizeof(compute_command))) { fail_site=__LINE__; goto fail; }
  }
  if(!append_attachments(batch,&packet)) { fail_site=__LINE__; goto fail; }
  command_header=compute ? agx_cmd_header(false,DRM_ASAHI_BARRIER_NONE,1) :
                           agx_cmd_header(false,0,0);
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
      batch->key.nr_cbufs ? gpuva_color_class(batch->key.cbufs[0].format) :
          APPLE_AGX_G4_COLOR_NONE,ranges,&g->Lease)) {
    fail_site=__LINE__; goto fail;
  }
  private_bytes=packet.Header.V2.Base.HeaderBytes+packet.Header.V2.Base.CommandBytes;
  if(compute) {
    APPLE_AGX_G4_PROCESS_RANGE preemption;
    /* 64 KiB (the BO granule) covers the G13 context-store size the KMD
     * requires (APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES), as for G2 compute. */
    g->Preemption=agx_bo_create(b->Native,0x10000u,0,0,"Compute preemption");
    with_compute=calloc(1,sizeof(*with_compute));
    if(!g->Preemption || !AgxWin32AsahiGpuvaBo(b,g->Preemption) || !with_compute)
      { fail_site=__LINE__; goto fail; }
    preemption.Va=g->Preemption->va->addr;
    preemption.Bytes=(unsigned)g->Preemption->size;
    preemption.Reserved=0u;
    if(!AppleAgxG4ComposeHeaderV5(&with_compute->Header,&packet.Header,
        g->Command->va->addr,packet.Header.V2.Base.CommandBytes,&preemption))
      { fail_site=__LINE__; goto fail; }
    memcpy(with_compute->Native,packet.Native,packet.Header.V2.Base.CommandBytes);
    private_data=with_compute;
    private_bytes=with_compute->Header.V3.V2.Base.HeaderBytes+
        packet.Header.V2.Base.CommandBytes;
  }
  refs=calloc(limit,sizeof(*refs));
  if(!refs) { fail_site=__LINE__; goto fail; }
  if(!add_bo(b,refs,&count,limit,g->Command) ||
     !add_bo(b,refs,&count,limit,batch->vdm.bo) ||
     !add_bo(b,refs,&count,limit,agx_screen(batch->ctx->base.screen)->rodata) ||
     /* EXP1069: descriptors may name the device's zero/scratch pages. */
     !add_bo(b,refs,&count,limit,agx_device(batch->ctx->base.screen)->zero_bo) ||
     !add_bo(b,refs,&count,limit,agx_device(batch->ctx->base.screen)->scratch_bo) ||
     (compute && (!add_bo(b,refs,&count,limit,batch->cdm.bo) ||
                  !add_bo(b,refs,&count,limit,g->Preemption) ||
                  !add_bo(b,refs,&count,limit,batch->sampler_heap.bo))))
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
    /* D3D-G1: the compute command writes buffers too (stream output,
     * geometry and tessellation outputs): every BO this batch is the Mesa
     * writer of, as for a G2 compute batch, so the UMD refreshes their CPU
     * copies after the job. */
    if(compute && agx_writer_get(batch->ctx,(unsigned)handle)==batch) {
      const AGX_WIN32_GPUVA_BO *mapped=AgxWin32AsahiGpuvaBo(b,referenced);
      unsigned duplicate=0;
      if(!mapped) { fail_site=__LINE__; goto fail; }
      for(unsigned j=0;j<written_count;++j)
        if(written[j]==mapped) duplicate=1;
      if(!duplicate) {
        if(written_count==AGX_G4_WRITTEN_LIMIT) { fail_site=__LINE__; goto fail; }
        written[written_count++]=mapped;
      }
    }
  }
  util_dynarray_foreach(&batch->pool.bos,struct agx_bo *,bo) {
    if(!*bo || !add_bo(b,refs,&count,limit,*bo)) { fail_site=__LINE__; goto fail; }
  }
  util_dynarray_foreach(&batch->pipeline_pool.bos,struct agx_bo *,bo) {
    if(!*bo || !add_bo(b,refs,&count,limit,*bo)) { fail_site=__LINE__; goto fail; }
  }
  if(!publish_shadows(batch)) { fail_site=__LINE__; goto fail; }
  if(!AgxWin32GpuvaSubmit(&b->Gpuva,refs,count,
      AgxWin32AsahiGpuvaBo(b,g->Command),packet.Header.V2.Base.CommandBytes,
      written,written_count,private_data,private_bytes,
      &g->Fence)) { fail_site=__LINE__; goto fail; }
  g->Submitted=1;
  if(g->Fence>b->NewestFence) b->NewestFence=g->Fence;
  free(with_compute);
  free(refs);
  return 1;
fail:
  free(with_compute);
  free(refs);
  (void)batch_refuse(3u, fail_site, b->Gpuva.LastFailure, b->Gpuva.LastDetail);
  g->Rejected=1;
  /* Native submission also marks the context faulted. Publish that failure
   * to Windows FlushStatus even when residency rollback completed safely.
   * Keep Gpuva.Terminal separate: a rejected batch can still be released. */
  AGX_WIN32_ASAHI_FAIL(b, 4u);
  return 0;
}

/* G2: submit a compute-only batch as one AGX4 compute envelope: the native
 * DRM_ASAHI_CMD_COMPUTE command as the DMA buffer, its copy behind the
 * compute header in the private data, and a per-job context-store buffer.
 * Residency is the render rule (command, stream, device pages, bo_list,
 * pools); the written set is every BO this batch is the Mesa writer of, so
 * the UMD refreshes their CPU copies after the job. A helper program
 * (compute scratch, EXP1339) is carried by the command, its buffer by the
 * bo_list; timestamps are a KMD request (keep_compute_timestamps). */
int AgxWin32AsahiBatchFinishCompute(struct agx_batch *batch,
                                    const struct drm_asahi_cmd_compute *compute) {
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  AGX_G4_BATCH *g=capsule(batch);
  const AGX_WIN32_GPUVA_BO **refs=NULL;
  const AGX_WIN32_GPUVA_BO *written[AGX_G4_WRITTEN_LIMIT]={0};
  unsigned written_count=0,count=0,limit;
  size_t pool_count,pipeline_count;
  APPLE_AGX_G4_COMPUTE_ENVELOPE envelope;
  APPLE_AGX_G4_PROCESS_RANGE preemption;
  struct drm_asahi_cmd_header command_header;
  const unsigned command_bytes=
      (unsigned)(sizeof(envelope.Native)+sizeof(envelope.Compute));
  unsigned char *cpu;
  unsigned fail_site=0u;
  int handle;
  /* Command, stream, preemption, rodata, zero and scratch pages, sampler
   * heap and slack. */
  enum { fixed_refs=8u };
  if(!b || !g || !compute || b->Failed || b->Gpuva.Terminal ||
     !compute_only(batch) || g->Submitted || g->Rejected || g->ComputeEntered ||
     compute->flags || batch->bo_list.bit_count>UINT32_MAX-fixed_refs)
    return batch_refuse(9u, __LINE__,
        (unsigned)!compute_only(batch)|((unsigned)(compute && compute->flags)<<1)|
        ((unsigned)(compute && compute->ts.start.handle)<<2)|
        ((unsigned)(b && b->Failed)<<3)|((unsigned)(b && b->Gpuva.Terminal)<<4),
        compute ? compute->sampler_count : 0u);
  g->Entered=0;
  if(!retire_held(batch,b)) { fail_site=__LINE__; goto fail; }
  pool_count=util_dynarray_num_elements(&batch->pool.bos,struct agx_bo *);
  pipeline_count=util_dynarray_num_elements(&batch->pipeline_pool.bos,struct agx_bo *);
  limit=batch->bo_list.bit_count+fixed_refs;
  if(pool_count>UINT32_MAX-limit) { fail_site=__LINE__; goto fail; }
  limit+=(unsigned)pool_count;
  if(pipeline_count>UINT32_MAX-limit) { fail_site=__LINE__; goto fail; }
  limit+=(unsigned)pipeline_count;
  /* 64 KiB (the BO granule) covers the G13 context-store size the KMD
   * requires (APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES). */
  g->Preemption=agx_bo_create(b->Native,0x10000u,0,0,"Compute preemption");
  g->Command=agx_bo_create(b->Native,command_bytes,0,0,"VA compute command");
  if(!g->Preemption || !g->Command || !AgxWin32AsahiGpuvaBo(b,g->Preemption) ||
     !AgxWin32AsahiGpuvaBo(b,g->Command)) { fail_site=__LINE__; goto fail; }
  memset(&envelope,0,sizeof(envelope));
  command_header=agx_cmd_header(true,0,0);
  memcpy(&envelope.Native,&command_header,sizeof(envelope.Native));
  memcpy(&envelope.Compute,compute,sizeof(envelope.Compute));
  g->TimestampCount=0u;
  keep_compute_timestamps(g,&envelope.Compute.Timestamps,1);
  preemption.Va=g->Preemption->va->addr;
  preemption.Bytes=(unsigned)g->Preemption->size;
  preemption.Reserved=0u;
  cpu=agx_bo_map(g->Command);
  if(!cpu || !AppleAgxG4ComposeHeaderCompute(&envelope.Header,
      g->Command->va->addr,command_bytes,&preemption)) { fail_site=__LINE__; goto fail; }
  memcpy(cpu,&envelope.Native,command_bytes);
  refs=calloc(limit,sizeof(*refs));
  if(!refs) { fail_site=__LINE__; goto fail; }
  if(!add_bo(b,refs,&count,limit,g->Command) ||
     !add_bo(b,refs,&count,limit,batch->cdm.bo) ||
     !add_bo(b,refs,&count,limit,g->Preemption) ||
     !add_bo(b,refs,&count,limit,agx_screen(batch->ctx->base.screen)->rodata) ||
     !add_bo(b,refs,&count,limit,agx_device(batch->ctx->base.screen)->zero_bo) ||
     !add_bo(b,refs,&count,limit,agx_device(batch->ctx->base.screen)->scratch_bo) ||
     !add_bo(b,refs,&count,limit,batch->sampler_heap.bo))
    { fail_site=__LINE__; goto fail; }
  AGX_BATCH_FOREACH_BO_HANDLE(batch,handle) {
    struct agx_bo *referenced=AgxWin32AsahiLookupBo(b->Native,handle);
    if(!referenced || !add_bo(b,refs,&count,limit,referenced))
      { fail_site=__LINE__; goto fail; }
    if(agx_writer_get(batch->ctx,(unsigned)handle)==batch) {
      const AGX_WIN32_GPUVA_BO *mapped=AgxWin32AsahiGpuvaBo(b,referenced);
      unsigned duplicate=0;
      if(!mapped) { fail_site=__LINE__; goto fail; }
      for(unsigned j=0;j<written_count;++j)
        if(written[j]==mapped) duplicate=1;
      if(!duplicate) {
        if(written_count==AGX_G4_WRITTEN_LIMIT) { fail_site=__LINE__; goto fail; }
        written[written_count++]=mapped;
      }
    }
  }
  util_dynarray_foreach(&batch->pool.bos,struct agx_bo *,bo) {
    if(!*bo || !add_bo(b,refs,&count,limit,*bo)) { fail_site=__LINE__; goto fail; }
  }
  util_dynarray_foreach(&batch->pipeline_pool.bos,struct agx_bo *,bo) {
    if(!*bo || !add_bo(b,refs,&count,limit,*bo)) { fail_site=__LINE__; goto fail; }
  }
  if(!publish_shadows(batch)) { fail_site=__LINE__; goto fail; }
  if(!AgxWin32GpuvaSubmit(&b->Gpuva,refs,count,
      AgxWin32AsahiGpuvaBo(b,g->Command),command_bytes,written,written_count,
      &envelope,(unsigned)sizeof(envelope),&g->Fence)) { fail_site=__LINE__; goto fail; }
  g->Submitted=1;
  if(g->Fence>b->NewestFence) b->NewestFence=g->Fence;
  free(refs);
  return 1;
fail:
  free(refs);
  (void)batch_refuse(3u, fail_site, b->Gpuva.LastFailure, b->Gpuva.LastDetail);
  g->Rejected=1;
  AGX_WIN32_ASAHI_FAIL(b, 4u);
  return 0;
}

/* EXP1071: the private scene storage is needed only while the GPU runs the
 * job. Return it as soon as the submission retires: with several open
 * batches flushed back to back, holding every lease until cleanup exhausted
 * the process range (D3DERR_OUTOFVIDEOMEMORY in apps). */
static int release_lease(AGX_WIN32_ASAHI_BACKEND *b,AGX_G4_BATCH *g) {
  APPLE_AGX_G3_PRIVATE_REQUEST request={0};
  if(!g->Lease.SceneId) return 1;
  request.Magic=APPLE_AGX_G3_PRIVATE_MAGIC;request.Version=APPLE_AGX_G3_PRIVATE_VERSION;
  request.Bytes=sizeof(request);request.Operation=APPLE_AGX_G3_PRIVATE_RELEASE;
  request.ManagerId=g->Lease.ManagerId;request.ManagerGeneration=g->Lease.ManagerGeneration;
  request.SceneId=g->Lease.SceneId;request.SceneGeneration=g->Lease.SceneGeneration;
  if(!b->Gpuva.Ops.PrivateEscape ||
     !b->Gpuva.Ops.PrivateEscape(b->Gpuva.Context,&request)) return 0;
  memset(&g->Lease,0,sizeof(g->Lease));
  return 1;
}
/* EXP1202 receipt-only: time per phase of the native draw body (phase 0
 * starts the clock once the batch is taken; 1-5 add the time since the
 * previous mark). Game thread only; read and reset by the ICD receipt. */
int AgxWin32AsahiDrawPhasesOn; /* EXP1206: set by the ICD for diagnostics */
static uint64_t draw_phase_ns[6],draw_phase_last;
static APPLE_AGX_U32 draw_phase_draws;
void AgxWin32AsahiDrawPhase(unsigned phase) {
  uint64_t now=(uint64_t)os_time_get_nano();
  if(!phase) { draw_phase_last=now; ++draw_phase_draws; return; }
  if(phase<6u && draw_phase_last) draw_phase_ns[phase]+=now-draw_phase_last;
  draw_phase_last=now;
}
void AgxWin32AsahiDrawPhases(APPLE_AGX_U32 out[6]) {
  out[0]=draw_phase_draws; draw_phase_draws=0;
  for(unsigned i=1;i<6u;++i) { out[i]=(APPLE_AGX_U32)(draw_phase_ns[i]/1000u); draw_phase_ns[i]=0; }
}
/* EXP1195: pipe fences over the device's render fence timeline. The GPU
 * completes submissions in order, so the newest accepted submission's
 * completion fence covers every earlier one. Signalled reads the monitored
 * fence value (QueryRender) without waiting; Wait blocks on it. A device
 * without a usable GPUVA space never signals and fails its waits. */
APPLE_AGX_U64 AgxWin32AsahiNewestFence(struct agx_context *ctx) {
  AGX_WIN32_ASAHI_BACKEND *b=ctx?agx_device(ctx->base.screen)->windows_private:NULL;
  return b?b->NewestFence:0u;
}
int AgxWin32AsahiFenceSignalled(struct agx_device *dev,APPLE_AGX_U64 fence) {
  AGX_WIN32_ASAHI_BACKEND *b=dev?dev->windows_private:NULL;
  if(!fence) return 1;
  if(!b || !b->GpuvaReady || b->Gpuva.Terminal || !b->Gpuva.Ops.QueryRender) return 0;
  return b->Gpuva.Ops.QueryRender(b->Gpuva.Context,fence)!=0;
}
int AgxWin32AsahiFenceWait(struct agx_device *dev,APPLE_AGX_U64 fence) {
  AGX_WIN32_ASAHI_BACKEND *b=dev?dev->windows_private:NULL;
  if(!fence) return 1;
  if(!b || !b->GpuvaReady || b->Gpuva.Terminal || !b->Gpuva.Ops.WaitRender) return 0;
  return b->Gpuva.Ops.WaitRender(b->Gpuva.Context,fence)!=0;
}
int AgxWin32AsahiBatchPoll(struct agx_batch *batch,APPLE_AGX_U32 timeout) {
  AGX_G4_BATCH *g=capsule(batch);
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  (void)timeout;
  if(!g || g->Retired || g->Rejected) return 1;
  if(!b || !g->Submitted || !g->Fence) return 0;
  /* EXP1082: a CPU map may already have retired this submission's residency
   * set (native_map); then only its fence is waited for. */
  if(AgxWin32GpuvaFenceHeld(&b->Gpuva,g->Fence)) {
    if(!AgxWin32GpuvaRetire(&b->Gpuva,g->Fence)) return 0;
  } else if(b->Gpuva.Terminal || !b->Gpuva.Ops.WaitRender ||
            !b->Gpuva.Ops.WaitRender(b->Gpuva.Context,g->Fence)) return 0;
  g->Retired=1;
  (void)release_lease(b,g); /* a failed release is retried by Release */
  return 1;
}
int AgxWin32AsahiBatchTimestamps(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  if(g && b && g->Retired) write_timestamps(batch,b,g);
  return 1;
}
int AgxWin32AsahiBatchAbort(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  if(!g) return 1;
  if(g->Submitted && !g->Retired) return 0;
  g->Entered=0;
  g->ComputeEntered=0;
  g->Rejected=1;
  return 1;
}
/* EXP1083 receipt-only: capsule state of one batch slot. */
void AgxWin32AsahiBatchDiagnostic(struct agx_batch *batch,APPLE_AGX_U32 out[2]) {
  AGX_G4_BATCH *g=capsule(batch);
  out[0]=(APPLE_AGX_U32)(g!=NULL)|((APPLE_AGX_U32)(g && g->Submitted)<<1)|
      ((APPLE_AGX_U32)(g && g->Retired)<<2)|((APPLE_AGX_U32)(g && g->Rejected)<<3)|
      ((APPLE_AGX_U32)(g && g->Entered)<<4)|((APPLE_AGX_U32)(g && g->Lease.SceneId)<<5);
  out[1]=g?(APPLE_AGX_U32)g->Fence:0u;
}
int AgxWin32AsahiBatchRelease(struct agx_batch *batch) {
  AGX_G4_BATCH *g=capsule(batch);
  AGX_WIN32_ASAHI_BACKEND *b=backend(batch);
  if(!g) return 1;
  if(!b || b->Gpuva.Terminal || (!g->Retired && !g->Rejected) ||
     g->ComputeEntered || g->Entered)
    return 0;
  if(!release_lease(b,g)) return 0;
  if(g->Command) agx_bo_unreference(b->Native,g->Command);
  if(g->Preemption) agx_bo_unreference(b->Native,g->Preemption);
  batch->windows_batch=NULL;
  free(g);
  return 1;
}
