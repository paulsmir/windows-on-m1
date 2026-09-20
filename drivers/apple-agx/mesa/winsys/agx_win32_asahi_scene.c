#include "gallium/drivers/asahi/agx_state.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "drm-uapi/drm_fourcc.h"
#include "agx_win32_asahi_scene.h"
#include "agx_win32_asahi_bo.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pipe_screen *AgxWin32AsahiScreenCreate(AGX_WIN32_ASAHI_BACKEND *,AGX_WIN32_SCREEN *,
    const AGX_WIN32_ASAHI_OWNER_OPS *,void *,const AGX_WIN32_ASAHI_BATCH_OPS *,
    const struct drm_asahi_params_global *);

static int context_idle(struct pipe_context *ctx) {
  struct agx_context *native=ctx?agx_context(ctx):NULL;
  if(!native) return 0;
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i)
    if(native->batches.slots[i].windows_batch || BITSET_TEST(native->batches.active,i) ||
       BITSET_TEST(native->batches.submitted,i)) return 0;
  return 1;
}
static void tracked_context_destroy(struct pipe_context *ctx) {
  AGX_WIN32_ASAHI_BACKEND *backend=ctx && ctx->screen?
      agx_device(ctx->screen)->windows_private:NULL;
  if(!backend || !backend->ContextDestroy || !backend->ContextCount || !context_idle(ctx)) return;
  void (*destroy)(struct pipe_context *)=backend->ContextDestroy;
  ctx->destroy=destroy;
  destroy(ctx);
  --backend->ContextCount;
}
static struct pipe_context *tracked_context_create(struct pipe_screen *screen,void *owner,unsigned flags) {
  AGX_WIN32_ASAHI_BACKEND *backend=screen?agx_device(screen)->windows_private:NULL;
  if(!backend || backend->Closing || !backend->ContextCreate || backend->ContextCount==UINT32_MAX) return NULL;
  struct pipe_context *ctx=backend->ContextCreate(screen,owner,flags);
  if(!ctx || !ctx->destroy) return NULL;
  if(backend->ContextDestroy && backend->ContextDestroy!=ctx->destroy) {
    ctx->destroy(ctx);return NULL;
  }
  backend->ContextDestroy=ctx->destroy;
  ctx->destroy=tracked_context_destroy;
  ++backend->ContextCount;
  return ctx;
}

struct pipe_screen *AgxWin32AsahiScreenCreateForWindows(AGX_WIN32_ASAHI_BACKEND *backend,
    AGX_WIN32_SCREEN *windows,const AGX_WIN32_ASAHI_OWNER_OPS *owners,void *owner,
    const AGX_WIN32_ASAHI_BATCH_OPS *batches) {
  if(!backend || !windows || !windows->Active || !windows->Generation ||
      !AgxWin32DeviceInfoValid(&windows->Info) || !owners || !owner || !batches) return NULL;
  struct drm_asahi_params_global params={0};
  params.gpu_generation=windows->Info.GpuGeneration;
  params.gpu_variant='G';
  /* KMD lifecycle.c exposes the compiled G13G/16-KiB contract, not measured
   * topology. Pinned Asahi hw/t8103.rs (77cb8f24c238...) defines G13G, one die,
   * max_num_clusters=1. This adapter implements that one-cluster model only.
   * Core masks, revision and timestamp frequency are not invented. */
  params.num_clusters_total=1;
  params.num_dies=1;
  params.features=0; /* Optional soft faults are deliberately not enabled. */
  if(windows->Info.PageBytes!=AIL_PAGESIZE) return NULL;
  struct pipe_screen *screen=AgxWin32AsahiScreenCreate(backend,windows,owners,owner,batches,&params);
  if(screen) {
    backend->ContextCreate=screen->context_create;
    backend->ContextDestroy=NULL;
    backend->ContextCount=0;
    backend->Closing=0;
    screen->context_create=tracked_context_create;
  }
  return screen;
}

struct pipe_screen *AgxWin32AsahiScreenRecover(AGX_WIN32_ASAHI_BACKEND *backend) {
  struct agx_screen *screen=backend && backend->Native ?
      (struct agx_screen *)((unsigned char *)backend->Native-
          offsetof(struct agx_screen,dev)) : NULL;
  return screen ? &screen->pscreen : NULL;
}

struct pipe_context *AgxWin32AsahiContextCreate(struct pipe_screen *screen,void *owner) {
  return screen && screen->context_create ? screen->context_create(screen,owner,0) : NULL;
}
struct pipe_resource *AgxWin32AsahiImportLinearColor32(
    struct pipe_screen *screen,const AGX_WIN32_SCREEN_BUFFER *buffer,
    APPLE_AGX_U32 width,APPLE_AGX_U32 height,APPLE_AGX_U32 pitch,
    APPLE_AGX_U64 bytes,AGX_WIN32_ASAHI_LINEAR_FORMAT format) {
  AGX_WIN32_ASAHI_BACKEND *backend=screen?
      agx_device(screen)->windows_private:NULL;
  if(!screen || !buffer || !width || !height ||
     pitch!=(APPLE_AGX_U64)width*4ULL || bytes!=(APPLE_AGX_U64)pitch*height ||
     !backend || buffer->Transport.Bytes!=bytes ||
     buffer->Transport.Generation!=backend->Buffers.Generation)
    return NULL;
  enum pipe_format pipe_format;
  switch(format) {
  case AgxWin32AsahiLinearFormatBgra8Unorm:
    pipe_format=PIPE_FORMAT_B8G8R8A8_UNORM;break;
  case AgxWin32AsahiLinearFormatRgba8Unorm:
    pipe_format=PIPE_FORMAT_R8G8B8A8_UNORM;break;
  default: return NULL;
  }
  struct pipe_resource info={0};
  info.target=PIPE_TEXTURE_2D;info.format=pipe_format;
  info.width0=width;info.height0=height;info.depth0=1;info.array_size=1;
  info.nr_samples=1;info.nr_storage_samples=1;
  info.bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW;
  info.usage=PIPE_USAGE_DEFAULT;
  struct agx_resource *resource=calloc(1,sizeof(*resource));
  if(!resource) return NULL;
  resource->base=info;resource->base.screen=screen;
  resource->modifier=DRM_FORMAT_MOD_LINEAR;
  resource->layout=(struct ail_layout){
      .tiling=AIL_TILING_LINEAR,.format=pipe_format,
      .width_px=width,.height_px=height,.depth_px=1,.sample_count_sa=1,
      .levels=1,.renderable=true,.linear_stride_B=pitch};
  pipe_reference_init(&resource->base.reference,1);
  ail_make_miptree(&resource->layout);
  if(resource->layout.size_B!=bytes ||
     ail_get_linear_stride_B(&resource->layout,0)!=pitch ||
     resource->layout.level_offsets_B[0]!=0) {
    free(resource);return NULL;
  }
  struct agx_bo *imported=AgxWin32AsahiImportBo(backend,buffer,"Display primary");
  if(!imported) {free(resource);return NULL;}
  resource->bo=imported;
  if(backend->Failed) {
    agx_bo_unreference(agx_device(screen),imported);free(resource);return NULL;
  }
  return &resource->base;
}
void AgxWin32AsahiResourceRelease(struct pipe_resource **resource) {
  if(resource) pipe_resource_reference(resource,NULL);
}
int AgxWin32AsahiResourceIdentity(
    struct pipe_resource *resource,AGX_WIN32_RELOC_ALLOCATION *identity) {
  if(identity) memset(identity,0,sizeof(*identity));
  if(!resource || !resource->screen || !identity) return 0;
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(resource->screen)->windows_private;
  struct agx_resource *native=agx_resource(resource);
  return backend && native->bo &&
      AgxWin32AsahiIdentity(backend,native->bo,identity);
}
struct pipe_resource *AgxWin32AsahiCreateUncompressedDepthStencil(
    struct pipe_screen *screen,const struct pipe_resource *templ) {
  if(!screen||!templ||!screen->resource_create_with_modifiers||
     (templ->format!=PIPE_FORMAT_Z24_UNORM_S8_UINT &&
      templ->format!=PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) return NULL;
  const uint64_t modifier=DRM_FORMAT_MOD_APPLE_GPU_TILED;
  struct pipe_resource depth_info=*templ,stencil_info=*templ;
  depth_info.format=PIPE_FORMAT_Z32_FLOAT;
  stencil_info.format=PIPE_FORMAT_S8_UINT;
  struct pipe_resource *depth=screen->resource_create_with_modifiers(
      screen,&depth_info,&modifier,1);
  if(!depth) return NULL;
  struct pipe_resource *stencil=screen->resource_create_with_modifiers(
      screen,&stencil_info,&modifier,1);
  if(!stencil) { screen->resource_destroy(screen,depth); return NULL; }
  struct agx_resource *native_depth=agx_resource(depth);
  native_depth->base.format=templ->format;
  native_depth->separate_stencil=agx_resource(stencil);
  return depth;
}
int AgxWin32AsahiContextDestroy(struct pipe_context *ctx) {
  if(!ctx || !ctx->destroy) return 0;
  if(!context_idle(ctx)) return 0;
  ctx->destroy(ctx);
  return 1;
}
int AgxWin32AsahiContextRetire(struct pipe_context *ctx,APPLE_AGX_U32 timeout) {
  if(!ctx) return 0;
  struct agx_context *native=agx_context(ctx);
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *batch=&native->batches.slots[i];
    if(batch->windows_batch) {
      if(!AgxWin32AsahiBatchPoll(batch,timeout)) return 0;
      agx_sync_batch(native,batch);
    }
    if(batch->windows_batch || BITSET_TEST(native->batches.active,i) ||
       BITSET_TEST(native->batches.submitted,i)) return 0;
  }
  return 1;
}
void AgxWin32AsahiContextDiagnostic(struct pipe_context *ctx,
    APPLE_AGX_U32 state[16],APPLE_AGX_U32 bindings[16]) {
  memset(state,0,16*sizeof(*state));memset(bindings,0,16*sizeof(*bindings));
  if(!ctx) return;
  struct agx_context *n=agx_context(ctx);
  AGX_WIN32_ASAHI_BACKEND *d=agx_device(ctx->screen)->windows_private;
  struct agx_batch *b=n->batch;
  AGX_WIN32_ASAHI_BATCH *c=b?b->windows_batch:NULL;
  state[0]=n->any_faults;state[1]=d?d->Failed:0;state[2]=b!=NULL;
  state[3]=b?b->draws:0;state[4]=b?b->initialized:0;
  state[5]=c?c->Capture.Capture.State:0;state[6]=c?c->Root.Scope.Failed:0;
  state[7]=c?c->Entered:0;state[8]=c?c->Submitted:0;state[9]=c?c->Rejected:0;
  state[10]=c?c->Capture.Capture.ReferenceCount:0;
  state[11]=c?c->Capture.Capture.RelocationCount:0;
  state[12]=d?d->LiveBos:0;state[13]=n->framebuffer.nr_cbufs;
  state[14]=n->framebuffer.cbufs[0].format;state[15]=b?b->sampler_heap.count:0;
  bindings[0]=n->rast!=NULL;bindings[1]=n->rast?n->rast->depth_bias:0;
  bindings[2]=n->attributes!=NULL;
  bindings[3]=n->stage[MESA_SHADER_VERTEX].shader!=NULL;
  bindings[4]=n->stage[MESA_SHADER_FRAGMENT].shader!=NULL;
  bindings[5]=(n->stage[MESA_SHADER_GEOMETRY].shader?1u:0u)|
      (n->stage[MESA_SHADER_TESS_CTRL].shader?2u:0u)|
      (n->stage[MESA_SHADER_TESS_EVAL].shader?4u:0u)|
      (n->cond_query?8u:0u)|(n->occlusion_query?16u:0u)|(n->time_elapsed?32u:0u);
  bindings[6]=n->stage[MESA_SHADER_VERTEX].texture_count;
  bindings[7]=n->stage[MESA_SHADER_FRAGMENT].texture_count;
  bindings[8]=n->stage[MESA_SHADER_VERTEX].sampler_count;
  bindings[9]=n->stage[MESA_SHADER_FRAGMENT].sampler_count;
  bindings[10]=n->stage[MESA_SHADER_VERTEX].cb_mask;
  bindings[11]=n->stage[MESA_SHADER_FRAGMENT].cb_mask;
  bindings[12]=n->stage[MESA_SHADER_GEOMETRY].cb_mask;
  for(unsigned i=0;i<MESA_SHADER_STAGES;++i) {
    bindings[13]|=n->stage[i].image_mask;
    bindings[14]|=n->stage[i].ssbo_mask;
    if(n->stage[i].custom_borders) bindings[15]|=1u<<i;
  }
}
int AgxWin32AsahiContextDrawReceipt(struct pipe_context *ctx) {
  if(!ctx) return 0;
  struct agx_context *native=agx_context(ctx);
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(ctx->screen)->windows_private;
  struct agx_batch *batch=native->batch;
  return batch && batch->draws==1 && batch->windows_batch && backend &&
      !backend->Failed && !native->any_faults;
}
int AgxWin32AsahiSetStreamOutputTargetOffsetForTest(
  struct pipe_stream_output_target *base,APPLE_AGX_U32 value) {
  struct agx_streamout_target *target=base?agx_so_target(base):NULL;
  struct agx_resource *offset=target&&target->offset?agx_resource(target->offset):NULL;
  void *map=offset&&offset->bo?agx_bo_map(offset->bo):NULL;
  if(!target||!offset||!offset->bo||!map)
    return 0;
  memcpy(map,&value,sizeof(value));
  util_range_add(&offset->base,&offset->valid_buffer_range,0,sizeof(value));
  return 1;
}
int AgxWin32AsahiContextFaulted(struct pipe_context *ctx) {
  return !ctx || agx_context(ctx)->any_faults;
}
int AgxWin32AsahiContextFlushForPresent(struct pipe_context *ctx) {
  if(!ctx) return 0;
  struct agx_context *native=agx_context(ctx);
  struct agx_batch *batch=native->batch;
  int drawn=0;
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *slot=&native->batches.slots[i];
    if(slot->windows_batch && slot->draws) { drawn=1; break; }
  }
  if(drawn) {
    ctx->flush(ctx,NULL,0);
    return !native->any_faults;
  }
  if(!batch) return !native->any_faults;
  if(!AgxWin32AsahiBatchAbort(batch)) return 0;
  agx_batch_reset(native,batch);
  return !native->any_faults && native->batch==NULL;
}
int AgxWin32AsahiScreenDestroy(struct pipe_screen *screen) {
  if(!screen || !screen->destroy) return 0;
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(screen)->windows_private;
  if(!backend || backend->Native!=agx_device(screen) || backend->ContextCount) return 0;
  screen->destroy(screen);
  if(backend->Native) return 0;
  backend->ContextCreate=NULL;backend->ContextDestroy=NULL;backend->Closing=0;
  return 1;
}

static AGX_WIN32_ASAHI_BACKEND *scene_backend(const AGX_WIN32_ASAHI_SCENE *s) {
  return s && s->Screen && s->Context && s->Context->screen==s->Screen ?
      agx_device(s->Screen)->windows_private : NULL;
}
/* One source of truth for real native shader/clear inputs and CPU pixel
 * expectations. The canonical pack is never injected into a GPU command. */
static const float scene_background[4]={0.05f,0.05f,0.05f,1.0f};
static const float scene_foreground[4]={0.9f,0.2f,0.1f,1.0f};
static APPLE_AGX_U32 scene_bgra8(const float color[4]) {
  unsigned char pixel[4];
  util_format_pack_rgba(PIPE_FORMAT_B8G8R8A8_UNORM,pixel,color,1);
  return (APPLE_AGX_U32)pixel[0] | ((APPLE_AGX_U32)pixel[1]<<8) |
      ((APPLE_AGX_U32)pixel[2]<<16) | ((APPLE_AGX_U32)pixel[3]<<24);
}
static nir_shader *scene_shader(mesa_shader_stage stage) {
  nir_builder b=nir_builder_init_simple_shader(stage,&agx_nir_options,"Windows native scene");
  nir_variable *out=nir_variable_create(b.shader,nir_var_shader_out,glsl_vec4_type(),"out");
  out->data.location=stage==MESA_SHADER_VERTEX?VARYING_SLOT_POS:FRAG_RESULT_DATA0;
  out->data.driver_location=0;
  if(stage==MESA_SHADER_VERTEX) {
    nir_variable *in=nir_variable_create(b.shader,nir_var_shader_in,glsl_vec4_type(),"position");
    in->data.location=VERT_ATTRIB_GENERIC0;in->data.driver_location=0;
    nir_store_var(&b,out,nir_load_var(&b,in),0xf);
  } else nir_store_var(&b,out,nir_imm_vec4(&b,scene_foreground[0],scene_foreground[1],scene_foreground[2],scene_foreground[3]),0xf);
  nir_shader_gather_info(b.shader,nir_shader_get_entrypoint(b.shader));
  return b.shader;
}
int AgxWin32AsahiSceneInit(AGX_WIN32_ASAHI_SCENE *s,struct pipe_screen *screen,struct pipe_context *ctx) {
  if(!s || s->Phase!=AgxAsahiSceneEmpty || !screen || !ctx || ctx->screen!=screen) return 0;
  struct agx_context *native=agx_context(ctx);
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(screen)->windows_private;
  if(!backend || backend->Native!=agx_device(screen) || backend->Failed || backend->Closing || !backend->BatchOps ||
     backend->ActiveCapture || backend->ActiveEmission || native->any_faults) return 0;
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i)
    if(native->batches.slots[i].windows_batch || BITSET_TEST(native->batches.active,i) ||
       BITSET_TEST(native->batches.submitted,i)) return 0;
  s->Screen=screen;s->Context=ctx;s->Phase=AgxAsahiScenePreparing;
  struct pipe_resource rt_info={0},vb_info={0};
  rt_info.target=PIPE_TEXTURE_2D;rt_info.format=PIPE_FORMAT_B8G8R8A8_UNORM;
  rt_info.width0=16;rt_info.height0=16;rt_info.depth0=1;rt_info.array_size=1;
  rt_info.bind=PIPE_BIND_RENDER_TARGET;rt_info.usage=PIPE_USAGE_DEFAULT;
  vb_info.target=PIPE_BUFFER;vb_info.format=PIPE_FORMAT_R8_UNORM;
  vb_info.width0=48;vb_info.height0=vb_info.depth0=vb_info.array_size=1;
  vb_info.bind=PIPE_BIND_VERTEX_BUFFER;vb_info.usage=PIPE_USAGE_DEFAULT;
  const uint64_t modifier=DRM_FORMAT_MOD_APPLE_GPU_TILED;
  s->Target=screen->resource_create_with_modifiers(screen,&rt_info,&modifier,1);
  s->VertexBuffer=screen->resource_create(screen,&vb_info);
  if(!s->Target || !s->VertexBuffer) goto rejected;
  struct agx_resource *target=agx_resource(s->Target);
  if(!AgxWin32AsahiIdentity(backend,target->bo,&s->Receipt.TargetIdentity)) goto rejected;
  s->Receipt.TargetConstructionAddress=agx_map_gpu(target);
  s->Receipt.TargetBytes=target->layout.size_B;
  s->Receipt.TargetLayerStride=target->layout.layer_stride_B;
  s->Receipt.Width=16;s->Receipt.Height=16;
  s->Receipt.ExpectedBackgroundBgra8=scene_bgra8(scene_background);
  s->Receipt.ExpectedForegroundBgra8=scene_bgra8(scene_foreground);
  s->Receipt.Tiling=target->layout.tiling;s->Receipt.Format=AppleAgxWin32FormatBgra8Unorm;
  s->Receipt.BootGeneration=backend->Buffers.Screen->Info.BootGeneration;
  s->Receipt.PageBytes=backend->Buffers.Screen->Info.PageBytes;
  const float vertices[12]={-1,-1,0,1,1,-1,0,1,0,1,0,1};
  ctx->buffer_subdata(ctx,s->VertexBuffer,PIPE_MAP_WRITE,0,sizeof(vertices),vertices);
  struct pipe_framebuffer_state fb={0};fb.width=16;fb.height=16;fb.nr_cbufs=1;
  fb.cbufs[0].texture=s->Target;fb.cbufs[0].format=PIPE_FORMAT_B8G8R8A8_UNORM;
  ctx->set_framebuffer_state(ctx,&fb);
  struct pipe_shader_state vs={0},fs={0};vs.type=fs.type=PIPE_SHADER_IR_NIR;
  vs.ir.nir=scene_shader(MESA_SHADER_VERTEX);fs.ir.nir=scene_shader(MESA_SHADER_FRAGMENT);
  s->VertexShader=ctx->create_vs_state(ctx,&vs);s->FragmentShader=ctx->create_fs_state(ctx,&fs);
  /* Native agx_create_shader_state returns before consuming NIR if its initial
   * state allocation fails; a successful call consumes/frees its input. */
  if(!s->VertexShader) ralloc_free(vs.ir.nir);
  if(!s->FragmentShader) ralloc_free(fs.ir.nir);
  if(!s->VertexShader || !s->FragmentShader) goto rejected;
  ctx->bind_vs_state(ctx,s->VertexShader);ctx->bind_fs_state(ctx,s->FragmentShader);
  struct pipe_blend_state blend={0};blend.rt[0].colormask=0xf;
  s->Blend=ctx->create_blend_state(ctx,&blend);if(!s->Blend) goto rejected;
  ctx->bind_blend_state(ctx,s->Blend);
  struct pipe_rasterizer_state raster={0};
  raster.fill_front=raster.fill_back=PIPE_POLYGON_MODE_FILL;
  raster.depth_clip_near=raster.depth_clip_far=true;raster.half_pixel_center=true;
  raster.point_size=1;raster.line_width=1;
  s->Rasterizer=ctx->create_rasterizer_state(ctx,&raster);if(!s->Rasterizer) goto rejected;
  ctx->bind_rasterizer_state(ctx,s->Rasterizer);
  struct pipe_depth_stencil_alpha_state depth={0};
  s->Depth=ctx->create_depth_stencil_alpha_state(ctx,&depth);if(!s->Depth) goto rejected;
  ctx->bind_depth_stencil_alpha_state(ctx,s->Depth);
  struct pipe_vertex_element element={0};element.src_format=PIPE_FORMAT_R32G32B32A32_FLOAT;element.src_stride=16;
  s->Elements=ctx->create_vertex_elements_state(ctx,1,&element);if(!s->Elements) goto rejected;
  ctx->bind_vertex_elements_state(ctx,s->Elements);
  struct pipe_vertex_buffer vertex_buffer={0};vertex_buffer.buffer.resource=s->VertexBuffer;
  ctx->set_vertex_buffers(ctx,1,&vertex_buffer);
  struct pipe_viewport_state vp={0};vp.scale[0]=vp.scale[1]=8;vp.scale[2]=1;
  vp.translate[0]=vp.translate[1]=8;ctx->set_viewport_states(ctx,0,1,&vp);
  struct pipe_scissor_state scissor={0};scissor.maxx=scissor.maxy=16;ctx->set_scissor_states(ctx,0,1,&scissor);
  if(backend->Failed || native->any_faults) goto rejected;
  s->Phase=AgxAsahiScenePrepared;return 1;
rejected:
  s->Phase=AgxAsahiSceneRejected;return 0;
}
int AgxWin32AsahiSceneDraw(AGX_WIN32_ASAHI_SCENE *s) {
  AGX_WIN32_ASAHI_BACKEND *backend=scene_backend(s);
  if(!backend || backend->Closing || s->Phase!=AgxAsahiScenePrepared) return 0;
  struct pipe_context *ctx=s->Context;
  struct agx_context *native=agx_context(ctx);
  union pipe_color_union clear;
  memcpy(clear.f,scene_background,sizeof(scene_background));
  ctx->clear(ctx,PIPE_CLEAR_COLOR0,0xf,0,NULL,&clear,0,0);
  s->Batch=native->batch;
  if(backend->Failed || native->any_faults || !s->Batch) goto rejected;
  struct pipe_draw_info info={0};info.mode=MESA_PRIM_TRIANGLES;info.instance_count=1;
  struct pipe_draw_start_count_bias draw={0};draw.count=3;
  ctx->draw_vbo(ctx,&info,0,NULL,&draw,1);
  if(backend->Failed || native->any_faults || native->batch!=s->Batch ||
      s->Batch->draws!=1 || !s->Batch->windows_batch) goto rejected;
  s->Phase=AgxAsahiSceneDrawn;return 1;
rejected:
  s->Phase=AgxAsahiSceneRejected;return 0;
}
int AgxWin32AsahiSceneSubmit(AGX_WIN32_ASAHI_SCENE *s) {
  AGX_WIN32_ASAHI_BACKEND *backend=scene_backend(s);
  if(!backend || backend->Closing || s->Phase!=AgxAsahiSceneDrawn || !s->Batch) return 0;
  s->Context->flush(s->Context,NULL,0);
  AGX_WIN32_ASAHI_BATCH *c=s->Batch->windows_batch;
  if(!c || !c->Submitted) {s->Phase=AgxAsahiSceneRejected;s->SubmitStatus=c?c->Status:-1;return 0;}
  /* Explicit Windows retirement keeps this capsule stable through flush even
   * if the ordered event was signalled before the callback returned. */
  s->Receipt.Request=c->Request;
  s->Receipt.CommandVersion=c->Capture.Capture.CommandVersion;
  s->Receipt.References=c->Capture.Capture.ReferenceCount;
  s->Receipt.Relocations=c->Capture.Capture.RelocationCount;
  s->Receipt.EncoderBytes=c->Capture.Capture.References[c->Draw.EncoderReference].Bytes;
  s->Receipt.NativeRoots=c->Render;
  s->SubmitStatus=c->Status;s->Phase=AgxAsahiSceneSubmitted;
  return s->SubmitStatus>=0;
}
int AgxWin32AsahiSceneRetire(AGX_WIN32_ASAHI_SCENE *s,APPLE_AGX_U32 timeout) {
  if(!scene_backend(s)) return 0;
  if(s->Phase==AgxAsahiSceneRetired) return 1;
  if(s->Phase!=AgxAsahiSceneSubmitted || !s->Batch || !s->Receipt.Request) return 0;
  AGX_WIN32_ASAHI_BATCH *c=s->Batch->windows_batch;
  if(!c || !c->Submitted || c->Request!=s->Receipt.Request) return 0;
  int complete=AgxWin32AsahiBatchPoll(s->Batch,timeout);
  s->RetireStatus=c->Status;
  if(!complete) return 0;
  /* Poll proved the same transaction retired. The normal native cleanup now
   * releases its pools/writer records; it cannot enqueue or replay a draw. */
  agx_sync_batch(agx_context(s->Context),s->Batch);
  if(s->Batch->windows_batch || agx_batch_is_active(s->Batch) || agx_batch_is_submitted(s->Batch)) return 0;
  s->Batch=NULL;s->Phase=AgxAsahiSceneRetired;return 1;
}
int AgxWin32AsahiSceneCleanup(AGX_WIN32_ASAHI_SCENE *s,APPLE_AGX_U32 timeout) {
  if(!s) return 0;
  if(s->Phase==AgxAsahiSceneReleased) return 1;
  if(s->Phase==AgxAsahiSceneEmpty) {s->Phase=AgxAsahiSceneReleased;return 1;}
  if(!scene_backend(s)) return 0;
  if(s->Phase==AgxAsahiSceneSubmitted && !AgxWin32AsahiSceneRetire(s,timeout)) return 0;
  struct agx_context *native=agx_context(s->Context);
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *b=&native->batches.slots[i];
    AGX_WIN32_ASAHI_BATCH *c=b->windows_batch;
    if(c && c->Submitted && !c->Retired) return 0;
    if(c || BITSET_TEST(native->batches.active,i) || BITSET_TEST(native->batches.submitted,i))
      agx_batch_reset(native,b);
    if(b->windows_batch || BITSET_TEST(native->batches.active,i) || BITSET_TEST(native->batches.submitted,i)) return 0;
  }
  struct pipe_context *ctx=s->Context;
  ctx->bind_vs_state(ctx,NULL);ctx->bind_fs_state(ctx,NULL);
  if(s->VertexShader) ctx->delete_vs_state(ctx,s->VertexShader);
  if(s->FragmentShader) ctx->delete_fs_state(ctx,s->FragmentShader);
  ctx->bind_vertex_elements_state(ctx,NULL);if(s->Elements) ctx->delete_vertex_elements_state(ctx,s->Elements);
  ctx->bind_blend_state(ctx,NULL);if(s->Blend) ctx->delete_blend_state(ctx,s->Blend);
  ctx->bind_rasterizer_state(ctx,NULL);if(s->Rasterizer) ctx->delete_rasterizer_state(ctx,s->Rasterizer);
  ctx->bind_depth_stencil_alpha_state(ctx,NULL);if(s->Depth) ctx->delete_depth_stencil_alpha_state(ctx,s->Depth);
  ctx->set_vertex_buffers(ctx,0,NULL);
  struct pipe_framebuffer_state fb={0};ctx->set_framebuffer_state(ctx,&fb);
  pipe_resource_reference(&s->VertexBuffer,NULL);pipe_resource_reference(&s->Target,NULL);
  s->VertexShader=s->FragmentShader=s->Elements=s->Blend=s->Rasterizer=s->Depth=NULL;
  s->Batch=NULL;s->Phase=AgxAsahiSceneReleased;return 1;
}
