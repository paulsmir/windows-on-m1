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
void AgxWin32AsahiBatchTraceDraw(struct agx_context *ctx,struct agx_batch *b,
                                unsigned phase) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);
  AGX_WIN32_ASAHI_BACKEND *d=ctx?agx_device(ctx->base.screen)->windows_private:NULL;
  if(phase==3u && ctx && b && c && d && b->draws &&
      !ctx->any_faults && !d->Failed) return;
  fprintf(stderr,"NATIVE_DRAW_BOUNDARY: phase=%u faults=%u backend=%u draws=%u current_draws=%u same=%u initialized=%u capture=%u root_failed=%u entered=%u vs_samplers=%u fs_samplers=%u\n",
      phase,ctx?(unsigned)ctx->any_faults:0u,d?(unsigned)d->Failed:0u,
      b?b->draws:0u,ctx&&ctx->batch?ctx->batch->draws:0u,
      ctx?(unsigned)(ctx->batch==b):0u,b?(unsigned)b->initialized:0u,
      c?c->Capture.Capture.State:0u,c?(unsigned)c->Root.Scope.Failed:0u,
      c?(unsigned)c->Entered:0u,
      ctx?ctx->stage[MESA_SHADER_VERTEX].sampler_count:0u,
      ctx?ctx->stage[MESA_SHADER_FRAGMENT].sampler_count:0u);
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
  const int has_geometry=b->ctx->stage[MESA_SHADER_GEOMETRY].shader!=NULL;
  const int has_depth=b->key.zsbuf.texture!=NULL;
  const int has_texture=b->ctx->stage[MESA_SHADER_FRAGMENT].texture_count!=0;
  if(!has_geometry) {
    b->uniforms.tables[AGX_SYSVAL_TABLE_GRID]=0;
    b->uniforms.tables[AGX_SYSVAL_TABLE_GS]=0;
    b->uniforms.vertex_params=0;
    b->uniforms.geometry_params=0;
    b->uniforms.vertex_outputs=0;
  }
  if((has_depth && has_texture) || (has_geometry && (has_depth||has_texture))) {
    (void)AgxWin32AsahiBatchAbort(b); (void)AgxWin32AsahiBatchRelease(b);
    return 0;
  }
  APPLE_AGX_U16 version=has_geometry ?
      APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH : has_depth ?
      APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH :
      has_texture ? APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH :
                    APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
  if(AgxWin32AsahiCaptureBeginVersion(&c->Capture,d,id.Owner,id.Generation,
      c->Request,version)!=AgxRelocOk ||
      !AgxWin32AsahiCaptureActivate(&c->Capture) ||
      !AgxWin32AsahiEncoderRootBegin(d->Native,b->vdm.bo->_map,b->vdm.bo->va->addr,
        (APPLE_AGX_U32)b->vdm.bo->size,&c->Root)) {
    (void)AgxWin32AsahiBatchAbort(b); (void)AgxWin32AsahiBatchRelease(b); return 0;
  }
  /* Capture remains associated with its real native batch between calls. */
  c->Draw.StructBytes=sizeof(c->Draw);
  switch(b->key.cbufs[0].format) {
  case PIPE_FORMAT_B8G8R8A8_UNORM:
    c->Draw.Format=AppleAgxWin32FormatBgra8Unorm; break;
  case PIPE_FORMAT_B8G8R8A8_SRGB:
    c->Draw.Format=AppleAgxWin32FormatBgra8Srgb; break;
  case PIPE_FORMAT_B8G8R8X8_UNORM:
    c->Draw.Format=AppleAgxWin32FormatBgrx8Unorm; break;
  case PIPE_FORMAT_B8G8R8X8_SRGB:
    c->Draw.Format=AppleAgxWin32FormatBgrx8Srgb; break;
  case PIPE_FORMAT_R8G8B8A8_UNORM:
    c->Draw.Format=AppleAgxWin32FormatRgba8Unorm; break;
  case PIPE_FORMAT_R16G16B16A16_FLOAT:
    c->Draw.Format=AppleAgxWin32FormatRgba16Float; break;
  case PIPE_FORMAT_R8_UNORM:
    c->Draw.Format=AppleAgxWin32FormatR8Unorm; break;
  case PIPE_FORMAT_R16_FLOAT:
    c->Draw.Format=AppleAgxWin32FormatR16Float; break;
  case PIPE_FORMAT_R32G32B32A32_FLOAT:
    c->Draw.Format=AppleAgxWin32FormatRgba32Float; break;
  case PIPE_FORMAT_R10G10B10A2_UNORM:
    c->Draw.Format=AppleAgxWin32FormatRgb10A2Unorm; break;
  case PIPE_FORMAT_R11G11B10_FLOAT:
    c->Draw.Format=AppleAgxWin32FormatR11G11B10Float; break;
  case PIPE_FORMAT_B5G6R5_UNORM:
    c->Draw.Format=AppleAgxWin32FormatBgr565Unorm; break;
  default:
    (void)AgxWin32AsahiBatchAbort(b); (void)AgxWin32AsahiBatchRelease(b);
    return 0;
  }
  c->Draw.SurfaceWidth=b->key.width; c->Draw.SurfaceHeight=b->key.height;
  c->Draw.SurfacePitch=b->key.width*
      AppleAgxWin32FormatBytesPerPixel(c->Draw.Format);
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
  if(has_depth) {
    c->Render.DepthCompressionReference=APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
    c->Render.StencilReference=APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
    c->Render.StencilCompressionReference=APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  }
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
int AgxWin32AsahiBatchComputeEnter(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  if(!c||!d||c->Capture.Capture.CommandVersion!=
      APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH||
      c->ComputeEntered||!b->cdm.bo||!b->cdm.bo->_map)
    return 0;
  if(c->Entered && !AgxWin32AsahiBatchLeave(b)) return 0;
  if((!c->ComputePrepared && !AgxWin32AsahiComputeEncoderRootBegin(
      d->Native,b->cdm.bo->_map,b->cdm.bo->va->addr,
      (APPLE_AGX_U32)b->cdm.bo->size,&c->ComputeRoot)) ||
     !AgxWin32AsahiEncoderRootEnter(d->Native,b->cdm.bo->_map,
      b->cdm.bo->va->addr,(APPLE_AGX_U32)b->cdm.bo->size,&c->ComputeRoot))
    return 0;
  c->ComputeEntered=1;return 1;
}
int AgxWin32AsahiBatchComputeLeave(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  if(!c||!d||!c->ComputeEntered||
     !AgxWin32AsahiEncoderRootLeave(&c->ComputeRoot)) return 0;
  c->ComputeEntered=0;c->ComputePrepared=1;
  if(!AgxWin32AsahiEncoderRootEnter(d->Native,b->vdm.bo->_map,b->vdm.bo->va->addr,
      (APPLE_AGX_U32)b->vdm.bo->size,&c->Root)) return 0;
  c->Entered=1;return 1;
}
int AgxWin32AsahiBatchComputeFinalize(struct agx_batch *b,const void *end) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  if(!c||!c->ComputePrepared||c->ComputeEntered||!end||
     !d) return 0;
  int restore_vdm=c->Entered!=0;
  if(restore_vdm && !AgxWin32AsahiBatchLeave(b)) return 0;
  if(!AgxWin32AsahiEncoderRootFinalize(&c->ComputeRoot,end)) {
    if(restore_vdm && AgxWin32AsahiEncoderRootEnter(d->Native,
        b->vdm.bo->_map,b->vdm.bo->va->addr,(APPLE_AGX_U32)b->vdm.bo->size,
        &c->Root)) c->Entered=1;
    return 0;
  }
  c->Render.ComputeEncoderReference=c->ComputeRoot.Scope.Reference;
  c->Render.ComputeEncoderBytes=c->ComputeRoot.CompletedEnd;
  if(restore_vdm) {
    if(!AgxWin32AsahiEncoderRootEnter(d->Native,b->vdm.bo->_map,
        b->vdm.bo->va->addr,(APPLE_AGX_U32)b->vdm.bo->size,&c->Root)) return 0;
    c->Entered=1;
  }
  return c->Render.ComputeEncoderBytes!=0u;
}
int AgxWin32AsahiBatchDrawAllowed(struct agx_context *ctx,
    const struct pipe_draw_info *info,unsigned drawid,
    const struct pipe_draw_indirect_info *indirect,
    const struct pipe_draw_start_count_bias *draws,unsigned count) {
  if(!ctx || ctx->any_faults || !info || !draws || count!=1 || drawid || indirect ||
      info->mode!=MESA_PRIM_TRIANGLES || (info->index_size && info->index_size!=2) ||
      (ctx->stage[MESA_SHADER_FRAGMENT].texture_count && info->index_size) ||
      info->primitive_restart || info->instance_count!=1 ||
      info->start_instance || draws->start || draws->count!=3 || draws->index_bias ||
      ctx->framebuffer.nr_cbufs!=1 ||
      !ctx->framebuffer.cbufs[0].texture || (ctx->batch && ctx->batch->draws)) return 0;
  struct agx_resource *rt=agx_resource(ctx->framebuffer.cbufs[0].texture);
  int valid=rt->base.target==PIPE_TEXTURE_2D &&
      (rt->base.format==PIPE_FORMAT_B8G8R8A8_UNORM ||
       rt->base.format==PIPE_FORMAT_B8G8R8A8_SRGB ||
       rt->base.format==PIPE_FORMAT_B8G8R8X8_UNORM ||
       rt->base.format==PIPE_FORMAT_B8G8R8X8_SRGB ||
       rt->base.format==PIPE_FORMAT_R8G8B8A8_UNORM ||
       rt->base.format==PIPE_FORMAT_R16G16B16A16_FLOAT ||
       rt->base.format==PIPE_FORMAT_R8_UNORM ||
       rt->base.format==PIPE_FORMAT_R16_FLOAT ||
       rt->base.format==PIPE_FORMAT_R32G32B32A32_FLOAT ||
       rt->base.format==PIPE_FORMAT_R10G10B10A2_UNORM ||
       rt->base.format==PIPE_FORMAT_R11G11B10_FLOAT ||
       rt->base.format==PIPE_FORMAT_B5G6R5_UNORM) &&
      !rt->layout.compressed && rt->base.last_level==0 && rt->base.depth0==1 &&
      rt->base.array_size==1 && rt->base.nr_samples<=1;
  if(valid && ctx->framebuffer.zsbuf.texture) {
    struct pipe_surface *zs=&ctx->framebuffer.zsbuf;
    struct agx_resource *depth=agx_resource(zs->texture);
    valid=!ctx->stage[MESA_SHADER_FRAGMENT].texture_count && !info->index_size &&
        (zs->format==PIPE_FORMAT_Z32_FLOAT ||
         zs->format==PIPE_FORMAT_Z16_UNORM ||
         zs->format==PIPE_FORMAT_Z24_UNORM_S8_UINT ||
         zs->format==PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) && !zs->level &&
        !zs->first_layer && !zs->last_layer &&
        depth->base.target==PIPE_TEXTURE_2D &&
        (depth->base.format==PIPE_FORMAT_Z32_FLOAT ||
         depth->base.format==PIPE_FORMAT_Z16_UNORM ||
         depth->base.format==PIPE_FORMAT_Z24_UNORM_S8_UINT ||
         depth->base.format==PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) &&
        !depth->layout.compressed &&
        depth->base.last_level==0 && depth->base.depth0==1 &&
        depth->base.array_size==1 && depth->base.nr_samples<=1 && depth->bo;
    if(valid && (depth->base.format==PIPE_FORMAT_Z24_UNORM_S8_UINT ||
                 depth->base.format==PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) {
      struct agx_resource *stencil=depth->separate_stencil?
          agx_resource(depth->separate_stencil):NULL;
      valid=depth->layout.format==PIPE_FORMAT_Z32_FLOAT && stencil &&
          stencil->layout.format==PIPE_FORMAT_S8_UINT &&
          !stencil->layout.compressed && stencil->base.last_level==0 &&
          stencil->base.depth0==1 && stencil->base.array_size==1 &&
          stencil->base.nr_samples<=1 && stencil->bo;
    }
  }
  if(valid && info->index_size) {
    struct pipe_resource *resource=info->index.resource;
    struct agx_resource *index=resource?agx_resource(resource):NULL;
    valid=resource && resource->target==PIPE_BUFFER && resource->width0==8 &&
        (resource->bind&PIPE_BIND_INDEX_BUFFER) && index->bo;
  }
  return valid;
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
  (void)c; (void)line;
  return 0;
}
int AgxWin32AsahiBatchFinish(struct agx_batch *b,const struct drm_asahi_cmd_render *r) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b); AGX_WIN32_ASAHI_BACKEND *d=backend(b);
  if(!c || !d || !r || d->Failed || b->ctx->any_faults || c->Submitted || c->Rejected ||
      b->draws!=1 || b->vs_scratch || b->fs_scratch ||
      agx_tilebuffer_spills(&b->tilebuffer_layout) || r->samples!=1 || r->layers!=1 ||
      r->isp_oclqry_base ||
      r->sampler_heap || r->sampler_count ||
      (r->flags & ~((unsigned)DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES |
                    (unsigned)DRM_ASAHI_RENDER_DBIAS_IS_INT)) ||
      r->ppp_multisamplectl>UINT32_MAX || r->vdm_ctrl_stream_base!=c->Root.Address)
    return batch_reject(c,__LINE__);
  if((c->Capture.Capture.CommandVersion==APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH)!=
       (b->cdm.bo!=NULL) || (b->cdm.bo && !c->Render.ComputeEncoderBytes))
    return batch_reject(c,__LINE__);
  if(c->Entered && !AgxWin32AsahiBatchLeave(b)) return batch_reject(c,__LINE__);
  if(!AgxWin32AsahiEncoderRootFinalize(&c->Root,b->vdm.current+69)) return batch_reject(c,__LINE__);
  struct agx_resource *rt=agx_resource(b->key.cbufs[0].texture);
  if(!find_root(c,agx_map_gpu(rt),AppleAgxWin32RoleRenderTarget,&c->Draw.DestinationReference)) return batch_reject(c,__LINE__);
  if(c->Capture.Capture.CommandVersion==APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH) {
    struct pipe_surface *zs=&b->key.zsbuf;
    struct agx_resource *depth=zs->texture?agx_resource(zs->texture):NULL;
    struct agx_resource *stencil=depth&&depth->separate_stencil?
        agx_resource(depth->separate_stencil):NULL;
    int has_stencil=depth&&(depth->base.format==PIPE_FORMAT_Z24_UNORM_S8_UINT ||
        depth->base.format==PIPE_FORMAT_Z32_FLOAT_S8X24_UINT);
    uint64_t depth_address=depth ? agx_map_texture_gpu(depth,0)+
        ail_get_level_offset_B(&depth->layout,0) : 0;
    if(!depth || depth->layout.compressed || !r->depth.base ||
       (has_stencil && (depth->layout.format!=PIPE_FORMAT_Z32_FLOAT ||
                        !stencil || stencil->layout.format!=PIPE_FORMAT_S8_UINT)) ||
       (((r->flags & DRM_ASAHI_RENDER_DBIAS_IS_INT)!=0) !=
        (depth->base.format==PIPE_FORMAT_Z16_UNORM)) ||
       r->depth.base!=depth_address || r->depth.comp_base || r->depth.comp_stride ||
       !depth->layout.layer_stride_B || depth->layout.layer_stride_B>UINT32_MAX ||
       AgxWin32AsahiCaptureAddress(&c->Capture,r->depth.base,
          (APPLE_AGX_U32)depth->layout.layer_stride_B,
          AppleAgxWin32RoleDepthAttachment,
          AppleAgxWin32AccessRead|AppleAgxWin32AccessWrite,
          &c->Render.DepthReference)!=AgxRelocOk)
      return batch_reject(c,__LINE__);
    if(has_stencil) {
      uint64_t stencil_address=stencil?agx_map_texture_gpu(stencil,0)+
          ail_get_level_offset_B(&stencil->layout,0):0;
      if(!stencil || stencil->layout.compressed || !r->stencil.base ||
         r->stencil.base!=stencil_address || r->stencil.comp_base ||
         r->stencil.comp_stride || !stencil->layout.layer_stride_B ||
         stencil->layout.layer_stride_B>UINT32_MAX ||
         AgxWin32AsahiCaptureAddress(&c->Capture,r->stencil.base,
            (APPLE_AGX_U32)stencil->layout.layer_stride_B,
            AppleAgxWin32RoleDepthAttachment,
            AppleAgxWin32AccessRead|AppleAgxWin32AccessWrite,
            &c->Render.StencilReference)!=AgxRelocOk)
        return batch_reject(c,__LINE__);
      c->Render.StencilStride=r->stencil.stride;
      c->Render.StencilCompressionStride=0;
    } else if(r->stencil.base || r->stencil.comp_base || r->stencil.comp_stride) {
      return batch_reject(c,__LINE__);
    }
    c->Render.DepthStride=r->depth.stride;
    c->Render.DepthCompressionStride=0;
    c->Render.ZlsControl=r->zls_ctrl;
    c->Render.IspZlsPixels=r->isp_zls_pixels;
    c->Render.IspBgobjDepth=r->isp_bgobjdepth;
    c->Render.IspBgobjValues=r->isp_bgobjvals;
  } else if(r->depth.base || r->depth.comp_base || r->depth.comp_stride ||
            r->stencil.base || r->stencil.comp_base || r->stencil.comp_stride ||
            r->zls_ctrl || r->isp_zls_pixels ||
            r->isp_bgobjdepth || r->isp_bgobjvals!=0x300u) {
    return batch_reject(c,__LINE__);
  }
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
  if(r->flags & DRM_ASAHI_RENDER_DBIAS_IS_INT)
    c->Render.RenderFlags|=APPLE_AGX_WIN32_NATIVE_RENDER_DEPTH_BIAS_IS_INT;
  /* Receipt aliases come from emitted typed edges, not pool positions. */
  for(unsigned i=0;i<c->Capture.Capture.RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *edge=&c->Capture.Capture.Relocations[i];
    if(edge->Kind==AppleAgxWin32RelocationVdmPipelineOffset32)
      c->Draw.UscPipelineReference=edge->TargetReference;
    if(edge->Kind==AppleAgxWin32RelocationPppPipelineOffset32)
      c->Draw.Reserved[0]=edge->TargetReference;
    if(edge->Kind==AppleAgxWin32RelocationVdmIndexBufferAddress40)
      c->Draw.IndexReference=edge->TargetReference;
    if(edge->Kind==AppleAgxWin32RelocationTextureAddress40 &&
       c->Capture.Capture.References[edge->TargetReference].Role==AppleAgxWin32RoleTexture) {
      if(c->Draw.TextureReference!=APPLE_AGX_WIN32_OPTIONAL_REFERENCE &&
         c->Draw.TextureReference!=edge->TargetReference) return batch_reject(c,__LINE__);
      c->Draw.TextureReference=edge->TargetReference;
    }
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
  if(c->ComputeEntered && !AgxWin32AsahiEncoderRootLeave(&c->ComputeRoot)) return 0;
  c->ComputeEntered=0;
  if(c->Entered && !AgxWin32AsahiBatchLeave(b)) return 0;
  if(d->ActiveCapture==&c->Capture && !AgxWin32AsahiCaptureDeactivate(&c->Capture)) return 0;
  if(c->Capture.Capture.State && AgxWin32RelocAbort(&c->Capture.Capture)!=AgxRelocOk) return 0;
  c->Rejected=1; return 1;
}
int AgxWin32AsahiBatchRelease(struct agx_batch *b) {
  AGX_WIN32_ASAHI_BATCH *c=capsule(b);
  if(!c) return 1;
  if((!c->Retired && !c->Rejected) || c->Entered || c->ComputeEntered ||
      c->Capture.Capture.State ||
      !c->Ops->Destroy(c->Owner,c->Transaction)) return 0;
  b->windows_batch=NULL; free(c); return 1;
}
