#include "gallium/drivers/asahi/agx_state.h"
#include "agx_win32_asahi_batch.h"
#include "agx_usc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static AGX_WIN32_ASAHI_BACKEND *backend(struct agx_batch *b) {
  return b && b->ctx ? agx_device(b->ctx->base.screen)->windows_private : NULL;
}
static AGX_WIN32_ASAHI_BATCH *capsule(struct agx_batch *b) {
  return b ? b->windows_batch : NULL;
}
int AgxWin32AsahiBatchConfigure(AGX_WIN32_ASAHI_BACKEND *b,
    const AGX_WIN32_ASAHI_BATCH_OPS *ops,void *owner) {
  if(!b || !b->Native || !owner || !ops || !ops->Create || !ops->Submit ||
      !ops->Retire || !ops->Destroy || b->BatchOps || b->ActiveCapture) return 0;
  b->BatchOps=ops; b->BatchOwner=owner; return 1;
}
int AgxWin32AsahiBatchBegin(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  AGX_WIN32_RELOC_ALLOCATION id;
  if(!d || !d->BatchOps || d->Failed || d->ActiveCapture || b->windows_batch ||
      !b->vdm.bo || b->cdm.bo || !b->vdm.bo->_map ||
      !AgxWin32AsahiIdentity(d,b->vdm.bo,&id)) return 0;
  AGX_WIN32_ASAHI_BATCH *c=calloc(1,sizeof(*c));
  if(!c) return 0;
  c->Native=b; c->Ops=d->BatchOps; c->Owner=d->BatchOwner;
  c->Transaction=c->Ops->Create(c->Owner,&c->Request);
  if(!c->Transaction || !c->Request) { free(c); return 0; }
  b->windows_batch=c;
  if(AgxWin32AsahiCaptureBeginVersion(&c->Capture,d,id.Owner,id.Generation,c->Request,
      APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH)!=AgxRelocOk ||
      !AgxWin32AsahiCaptureActivate(&c->Capture) ||
      !AgxWin32AsahiEncoderRootBegin(d->Native,b->vdm.bo->_map,b->vdm.bo->va->addr,
        (APPLE_AGX_U32)b->vdm.bo->size,&c->Root)) {
    (void)AgxWin32AsahiBatchAbort(b); (void)AgxWin32AsahiBatchRelease(b); return 0;
  }
  /* Capture remains associated with its real native batch between calls. */
  c->Draw.StructBytes=sizeof(c->Draw);
  c->Draw.Format=AppleAgxWin32FormatBgra8Unorm;
  c->Draw.SurfaceWidth=b->key.width; c->Draw.SurfaceHeight=b->key.height;
  c->Draw.SurfacePitch=b->key.width*4;
  c->Draw.Topology=AppleAgxWin32TopologyTriangleList;
  c->Draw.VertexCount=3; c->Draw.InstanceCount=1;
  c->Draw.EncoderReference=c->Root.Scope.Reference;
#define ABSENT(n) c->Draw.n=APPLE_AGX_WIN32_OPTIONAL_REFERENCE
  ABSENT(VertexReference); ABSENT(IndexReference); ABSENT(ConstantReference);
  ABSENT(TextureReference); ABSENT(VertexShaderReference); ABSENT(FragmentShaderReference);
  ABSENT(VertexRodataReference); ABSENT(FragmentRodataReference); ABSENT(UscPipelineReference);
  ABSENT(DescriptorReference); ABSENT(ScissorReference); ABSENT(DepthBiasReference);
#undef ABSENT
  c->Draw.Reserved[0]=APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  return 1;
}
int AgxWin32AsahiBatchEnter(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b); AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  if(!c || !d || c->Submitted || c->Rejected || c->Entered || c->Retired ||
     !b->vdm.bo || b->vdm.bo->size>UINT32_MAX ||
     !AgxWin32AsahiEncoderRootEnter(d->Native,b->vdm.bo->_map,b->vdm.bo->va->addr,
       (APPLE_AGX_U32)b->vdm.bo->size,&c->Root)) return 0;
  c->Entered=1; return 1;
}
int AgxWin32AsahiBatchLeave(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);
  if(!c || !c->Entered || !AgxWin32AsahiEncoderRootLeave(&c->Root)) return 0;
  c->Entered=0; return 1;
}
int AgxWin32AsahiBatchDrawAllowed(struct agx_context *ctx,
    const struct pipe_draw_info *info,unsigned drawid,
    const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draws,unsigned count) {
  if(!ctx || ctx->any_faults || !info || !draws || count!=1 || drawid || indirect ||
      info->mode!=MESA_PRIM_TRIANGLES || info->index_size || info->instance_count!=1 ||
      info->start_instance || draws->start || draws->count!=3 || draws->index_bias ||
      ctx->framebuffer.nr_cbufs!=1 || ctx->framebuffer.zsbuf.texture ||
      !ctx->framebuffer.cbufs[0].texture || (ctx->batch && ctx->batch->draws)) return 0;
  struct agx_resource *rt=agx_resource(ctx->framebuffer.cbufs[0].texture);
  return rt->base.target==PIPE_TEXTURE_2D && rt->base.format==PIPE_FORMAT_B8G8R8A8_UNORM &&
      !rt->layout.compressed && rt->base.last_level==0 && rt->base.depth0==1 &&
      rt->base.array_size==1 && rt->base.nr_samples<=1;
}
static int find_root(AGX_WIN32_ASAHI_BATCH *c, uint64_t address,unsigned role,unsigned *index) {
  APPLE_AGX_U64 offset=0;
  return AgxWin32AsahiCaptureFind(&c->Capture,address,1,role,index,&offset) && !offset;
}
static int pipeline(AGX_WIN32_ASAHI_BATCH *c,uint64_t usc,uint32_t counts,
    APPLE_AGX_WIN32_NATIVE_PIPELINE_ROOT *out) {
  /* drm_asahi_bg_eot.usc is a tagged 32-bit USC coordinate. Capture owns
   * the original full construction address, relative to this native base. */
  uint64_t base=c->Capture.Backend->Native->shader_base;
  if(usc>UINT32_MAX || base>UINT64_MAX-(usc&~63ULL)) return 0;
  out->UscFlags=(unsigned)(usc&63); out->PackedCounts=counts;
  return find_root(c,base+(usc&~63ULL),AppleAgxWin32RoleUscPipeline,&out->UscReference);
}
static int batch_reject(AGX_WIN32_ASAHI_BATCH *c,unsigned line) {
  fprintf(stderr,"NATIVE_FINALIZE_REJECT: line=%u state=%u refs=%u relocs=%u finalized=%u\n",line,
      c?c->Capture.Capture.State:0,c?c->Capture.Capture.ReferenceCount:0,
      c?c->Capture.Capture.RelocationCount:0,c?c->Root.Finalized:0);
  return 0;
}
int AgxWin32AsahiBatchFinish(struct agx_batch *b,const struct drm_asahi_cmd_render *r) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b); AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  if(r) fprintf(stderr,"NATIVE_FINALIZE_INPUT: samples=%u layers=%u flags=%u depth=%llu stencil=%llu query=%llu sampler=%llu refs=%u relocs=%u\n",
      r->samples,r->layers,r->flags,(unsigned long long)r->depth.base,(unsigned long long)r->stencil.base,
      (unsigned long long)r->isp_oclqry_base,(unsigned long long)r->sampler_heap,
      c?c->Capture.Capture.ReferenceCount:0,c?c->Capture.Capture.RelocationCount:0);
  if(!c || !d || !r || d->Failed || b->ctx->any_faults || c->Submitted || c->Rejected ||
      b->draws!=1 || b->cdm.bo || b->vs_scratch || b->fs_scratch ||
      agx_tilebuffer_spills(&b->tilebuffer_layout) || r->samples!=1 || r->layers!=1 ||
      r->depth.base || r->stencil.base || r->isp_oclqry_base || r->sampler_heap ||
      (r->flags & ~(unsigned)DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES) ||
      r->ppp_multisamplectl>UINT32_MAX || r->vdm_ctrl_stream_base!=c->Root.Address)
    return batch_reject(c,__LINE__);
  if(c->Entered && !AgxWin32AsahiBatchLeave(b)) return batch_reject(c,__LINE__);
  if(!AgxWin32AsahiEncoderRootFinalize(&c->Root,b->vdm.current+69)) return batch_reject(c,__LINE__);
  struct agx_resource *rt=agx_resource(b->key.cbufs[0].texture);
  if(!find_root(c,agx_map_gpu(rt),AppleAgxWin32RoleRenderTarget,&c->Draw.DestinationReference)) return batch_reject(c,__LINE__);
  if(!b->scissor.size || AgxWin32AsahiCaptureAddress(&c->Capture,r->isp_scissor_base,
      b->scissor.size,AppleAgxWin32RoleScissor,AppleAgxWin32AccessRead,
      &c->Draw.ScissorReference)!=AgxRelocOk) return batch_reject(c,__LINE__);
  if(b->depth_bias.size && AgxWin32AsahiCaptureAddress(&c->Capture,r->isp_dbias_base,
      b->depth_bias.size,AppleAgxWin32RoleDepthBias,AppleAgxWin32AccessRead,
      &c->Draw.DepthBiasReference)!=AgxRelocOk) return batch_reject(c,__LINE__);
  c->Render.StructBytes=sizeof(c->Render);
  if(!pipeline(c,r->bg.usc,r->bg.rsrc_spec,&c->Render.Background) ||
     !pipeline(c,r->partial_bg.usc,r->partial_bg.rsrc_spec,&c->Render.PartialBackground) ||
     !pipeline(c,r->eot.usc,r->eot.rsrc_spec,&c->Render.EndOfTile) ||
      r->eot.usc!=r->partial_eot.usc || r->eot.rsrc_spec!=r->partial_eot.rsrc_spec) return batch_reject(c,__LINE__);
  c->Render.Samples=r->samples; c->Render.Layers=r->layers;
  c->Render.SampleSizeBytes=r->sample_size_B;
  c->Render.UtileWidth=r->utile_width_px; c->Render.UtileHeight=r->utile_height_px;
  c->Render.PppControl=r->ppp_ctrl; c->Render.PppMultisampleControl=(uint32_t)r->ppp_multisamplectl;
  c->Render.RenderFlags=(r->flags & DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES) ?
      APPLE_AGX_WIN32_NATIVE_RENDER_PROCESS_EMPTY_TILES : 0;
  /* Receipt aliases come from emitted typed edges, not pool positions. */
  for(unsigned i=0;i<c->Capture.Capture.RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *edge=&c->Capture.Capture.Relocations[i];
    if(edge->Kind==AppleAgxWin32RelocationVdmPipelineOffset32)
      c->Draw.UscPipelineReference=edge->TargetReference;
    if(edge->Kind==AppleAgxWin32RelocationPppPipelineOffset32)
      c->Draw.Reserved[0]=edge->TargetReference;
  }
  for(unsigned i=0;i<c->Capture.Capture.RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *edge=&c->Capture.Capture.Relocations[i];
    if(edge->Kind!=AppleAgxWin32RelocationUscShaderOffset32) continue;
    if(edge->DestinationReference==c->Draw.UscPipelineReference) c->Draw.VertexShaderReference=edge->TargetReference;
    if(edge->DestinationReference==c->Draw.Reserved[0]) c->Draw.FragmentShaderReference=edge->TargetReference;
  }
  if(!AgxWin32AsahiCaptureDeactivate(&c->Capture)) return batch_reject(c,__LINE__);
  int entered=0;
  c->Status=c->Ops->Submit(c->Owner,c->Transaction,&c->Capture.Capture,&c->Draw,&c->Render,&entered);
  fprintf(stderr,"NATIVE_ADAPTER_RESULT: status=%08x entered=%u\n",(unsigned)c->Status,entered);
  c->Submitted=entered!=0; c->Rejected=!entered;
  return c->Submitted;
}
int AgxWin32AsahiBatchPoll(struct agx_batch *b,APPLE_AGX_U32 timeout) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);
  if(!c) return 1; /* No transaction was ever created for this slot. */
  if(c->Retired || c->Rejected) return 1;
  if(!c->Submitted) return 0;
  c->Status=c->Ops->Retire(c->Owner,c->Transaction,timeout);
  if(c->Status<0) return 0;
  c->Retired=1; return 1;
}
int AgxWin32AsahiBatchAbort(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b); AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  if(!c) return 1;
  if(c->Submitted && !c->Retired) return 0;
  if(c->Entered && !AgxWin32AsahiBatchLeave(b)) return 0;
  if(d->ActiveCapture==&c->Capture && !AgxWin32AsahiCaptureDeactivate(&c->Capture)) return 0;
  if(c->Capture.Capture.State && AgxWin32RelocAbort(&c->Capture.Capture)!=AgxRelocOk) return 0;
  c->Rejected=1; return 1;
}
int AgxWin32AsahiBatchRelease(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);
  if(!c) return 1;
  if((!c->Retired && !c->Rejected) || c->Entered || c->Capture.Capture.State ||
      !c->Ops->Destroy(c->Owner,c->Transaction)) return 0;
  b->windows_batch=NULL; free(c); return 1;
}
