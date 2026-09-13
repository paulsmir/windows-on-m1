#include "gallium/drivers/asahi/agx_state.h"
#include "compiler/nir/nir_builder.h"
#include "asahi/lib/agx_linker.h"
#include "util/u_inlines.h"
#include "drm-uapi/drm_fourcc.h"
#include "agx_win32_asahi_batch.h"
#include <stdio.h>
#include <string.h>

struct pipe_screen *AgxWin32AsahiScreenCreate(AGX_WIN32_ASAHI_BACKEND *,AGX_WIN32_SCREEN *,
    const AGX_WIN32_ASAHI_OWNER_OPS *,void *,const AGX_WIN32_ASAHI_BATCH_OPS *,
    const struct drm_asahi_params_global *);
static nir_shader *runtime_shader(mesa_shader_stage stage) {
  nir_builder b=nir_builder_init_simple_shader(stage,&agx_nir_options,"Windows native lifecycle");
  nir_variable *out=nir_variable_create(b.shader,nir_var_shader_out,glsl_vec4_type(),"out");
  out->data.location=stage==MESA_SHADER_VERTEX?VARYING_SLOT_POS:FRAG_RESULT_DATA0;
  out->data.driver_location=0;
  if(stage==MESA_SHADER_VERTEX) {
    nir_variable *in=nir_variable_create(b.shader,nir_var_shader_in,glsl_vec4_type(),"position");
    in->data.location=VERT_ATTRIB_GENERIC0; in->data.driver_location=0;
    nir_store_var(&b,out,nir_load_var(&b,in),0xf);
  } else nir_store_var(&b,out,nir_imm_vec4(&b,0.9f,0.2f,0.1f,1.0f),0xf);
  nir_shader_gather_info(b.shader,nir_shader_get_entrypoint(b.shader));
  return b.shader;
}
/* Kernel/runtime callbacks are controlled by the existing UMD test. Everything
 * above that boundary is the actual native producer, including NIR compilation. */
unsigned AgxWin32AsahiRuntimeTest(AGX_WIN32_SCREEN *windows,
    const AGX_WIN32_ASAHI_OWNER_OPS *owners,void *owner,AGX_WIN32_ASAHI_BACKEND *backend,
    const AGX_WIN32_ASAHI_BATCH_OPS *submit,void (*checkpoint)(void *,unsigned)) {
  unsigned errors=0;
  struct pipe_screen *screen=NULL;
  struct pipe_context *ctx=NULL;
  struct agx_context *native=NULL;
  struct pipe_resource *rt=NULL,*vb=NULL;
  void *vs=NULL,*fs=NULL,*bs=NULL,*rs=NULL,*ds=NULL,*ve=NULL;
  struct pipe_framebuffer_state fb={0};
  unsigned marker_signalled=0,retained=0;
#define RUNTIME_CHECK(x) do { if(!(x)) {++errors;fprintf(stderr,"NATIVE_RUNTIME line=%u %s\n",(unsigned)__LINE__,#x);} } while(0)
  /* Catch signed enum-bitfield extraction before the same key reaches NIR.
   * All legal standard factors/functions, including inverted factors and MAX. */
  const enum pipe_blendfactor factors[]={
    PIPE_BLENDFACTOR_ONE,PIPE_BLENDFACTOR_SRC_COLOR,PIPE_BLENDFACTOR_SRC_ALPHA,
    PIPE_BLENDFACTOR_DST_ALPHA,PIPE_BLENDFACTOR_DST_COLOR,PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE,
    PIPE_BLENDFACTOR_CONST_COLOR,PIPE_BLENDFACTOR_CONST_ALPHA,PIPE_BLENDFACTOR_SRC1_COLOR,
    PIPE_BLENDFACTOR_SRC1_ALPHA,PIPE_BLENDFACTOR_ZERO,PIPE_BLENDFACTOR_INV_SRC_COLOR,
    PIPE_BLENDFACTOR_INV_SRC_ALPHA,PIPE_BLENDFACTOR_INV_DST_ALPHA,PIPE_BLENDFACTOR_INV_DST_COLOR,
    PIPE_BLENDFACTOR_INV_CONST_COLOR,PIPE_BLENDFACTOR_INV_CONST_ALPHA,
    PIPE_BLENDFACTOR_INV_SRC1_COLOR,PIPE_BLENDFACTOR_INV_SRC1_ALPHA};
  for(unsigned f=PIPE_BLEND_ADD;f<=PIPE_BLEND_MAX;++f) {
    for(unsigned i=0;i<ARRAY_SIZE(factors);++i) {
      unsigned v=(unsigned)factors[i];
      unsigned word=agx_pack_blend_standard((enum pipe_blend_func)f,factors[i],factors[i],
          (enum pipe_blend_func)f,factors[i],factors[i]);
      struct agx_blend_standard b=agx_unpack_blend_standard(word);
      RUNTIME_CHECK(sizeof(b)==4 && word==(f|(v<<3)|(v<<8)|(f<<13)|(v<<16)|(v<<21)));
      RUNTIME_CHECK(b.rgb_func==f && b.alpha_func==f && b.rgb_src_factor==v &&
          b.rgb_dst_factor==v && b.alpha_src_factor==v && b.alpha_dst_factor==v);
    }
  }
  if(errors) return errors;
  /* Controlled platform input for this offline execution; not a live ADT or
   * hardware inventory. The constructor rejects other GPU generations. */
  struct drm_asahi_params_global params={0};
  params.gpu_generation=13; params.num_clusters_total=1;
  screen=AgxWin32AsahiScreenCreate(backend,windows,owners,owner,submit,&params);
  RUNTIME_CHECK(screen); if(!screen) goto cleanup;
  ctx=screen->context_create(screen,owner,0);
  RUNTIME_CHECK(ctx); if(!ctx) goto cleanup;
  native=agx_context(ctx);
  struct pipe_resource rt_info={0},vb_info={0};
  rt_info.target=PIPE_TEXTURE_2D;rt_info.format=PIPE_FORMAT_B8G8R8A8_UNORM;
  rt_info.width0=16;rt_info.height0=16;rt_info.depth0=1;rt_info.array_size=1;
  rt_info.bind=PIPE_BIND_RENDER_TARGET;rt_info.usage=PIPE_USAGE_DEFAULT;
  vb_info.target=PIPE_BUFFER;vb_info.format=PIPE_FORMAT_R8_UNORM;
  vb_info.width0=48;vb_info.height0=vb_info.depth0=vb_info.array_size=1;
  vb_info.bind=PIPE_BIND_VERTEX_BUFFER;vb_info.usage=PIPE_USAGE_DEFAULT;
  const uint64_t modifier=DRM_FORMAT_MOD_APPLE_GPU_TILED;
  rt=screen->resource_create_with_modifiers(screen,&rt_info,&modifier,1);
  vb=screen->resource_create(screen,&vb_info);
  RUNTIME_CHECK(rt && vb); if(!rt || !vb) goto cleanup;
  printf("NATIVE_RUNTIME_TARGET: bytes=%llu tiling=%u stride=%llu\n",
      (unsigned long long)agx_resource(rt)->layout.size_B,
      (unsigned)agx_resource(rt)->layout.tiling,
      (unsigned long long)agx_resource(rt)->layout.layer_stride_B);
  const float vertices[12]={-1,-1,0,1,1,-1,0,1,0,1,0,1};
  ctx->buffer_subdata(ctx,vb,PIPE_MAP_WRITE,0,sizeof(vertices),vertices);
  fb.width=16;fb.height=16;fb.nr_cbufs=1;
  fb.cbufs[0].texture=rt;fb.cbufs[0].format=PIPE_FORMAT_B8G8R8A8_UNORM;
  ctx->set_framebuffer_state(ctx,&fb);
  struct pipe_shader_state vs_info={0},fs_info={0};
  vs_info.type=fs_info.type=PIPE_SHADER_IR_NIR;
  vs_info.ir.nir=runtime_shader(MESA_SHADER_VERTEX);fs_info.ir.nir=runtime_shader(MESA_SHADER_FRAGMENT);
  vs=ctx->create_vs_state(ctx,&vs_info);fs=ctx->create_fs_state(ctx,&fs_info);
  RUNTIME_CHECK(vs && fs);if(!vs || !fs) goto cleanup;
  ctx->bind_vs_state(ctx,vs);ctx->bind_fs_state(ctx,fs);
  struct pipe_blend_state blend={0};blend.rt[0].colormask=0xf;
  bs=ctx->create_blend_state(ctx,&blend);
  RUNTIME_CHECK(bs);if(!bs) goto cleanup;
  ctx->bind_blend_state(ctx,bs);
  struct pipe_rasterizer_state raster={0};
  raster.fill_front=raster.fill_back=PIPE_POLYGON_MODE_FILL;
  raster.depth_clip_near=raster.depth_clip_far=true;raster.half_pixel_center=true;
  raster.point_size=1;raster.line_width=1;
  rs=ctx->create_rasterizer_state(ctx,&raster);
  RUNTIME_CHECK(rs);if(!rs) goto cleanup;
  ctx->bind_rasterizer_state(ctx,rs);
  struct pipe_depth_stencil_alpha_state depth={0};
  ds=ctx->create_depth_stencil_alpha_state(ctx,&depth);
  RUNTIME_CHECK(ds);if(!ds) goto cleanup;
  ctx->bind_depth_stencil_alpha_state(ctx,ds);
  struct pipe_vertex_element element={0};element.src_format=PIPE_FORMAT_R32G32B32A32_FLOAT;element.src_stride=16;
  ve=ctx->create_vertex_elements_state(ctx,1,&element);
  RUNTIME_CHECK(ve);if(!ve) goto cleanup;
  ctx->bind_vertex_elements_state(ctx,ve);
  struct pipe_vertex_buffer vertex_buffer={0};vertex_buffer.buffer.resource=vb;
  ctx->set_vertex_buffers(ctx,1,&vertex_buffer);
  struct pipe_viewport_state vp={0};vp.scale[0]=vp.scale[1]=8;vp.scale[2]=1;
  vp.translate[0]=vp.translate[1]=8;
  ctx->set_viewport_states(ctx,0,1,&vp);
  struct pipe_scissor_state scissor={0};scissor.maxx=scissor.maxy=16;
  ctx->set_scissor_states(ctx,0,1,&scissor);
  union pipe_color_union clear={{0}};clear.f[0]=clear.f[1]=clear.f[2]=0.05f;clear.f[3]=1;
  ctx->clear(ctx,PIPE_CLEAR_COLOR0,0xf,0,NULL,&clear,0,0);
  struct pipe_draw_info info={0};info.mode=MESA_PRIM_TRIANGLES;info.instance_count=1;
  struct pipe_draw_start_count_bias draw={0};draw.count=3;
  /* active_queries enables query updates; it is not a bound query object.
   * Exercise that distinction on the real context before the valid draw. */
  native->active_queries=true;
  RUNTIME_CHECK(!native->cond_query && !native->occlusion_query && !native->time_elapsed &&
      !native->tf_any_overflow);
  for(unsigned i=0;i<ARRAY_SIZE(native->prims_generated);++i)
    RUNTIME_CHECK(!native->prims_generated[i] && !native->tf_prims_generated[i] && !native->tf_overflow[i]);
  for(unsigned i=0;i<ARRAY_SIZE(native->pipeline_statistics);++i)
    RUNTIME_CHECK(!native->pipeline_statistics[i]);
  ctx->draw_vbo(ctx,&info,0,NULL,&draw,1);
  struct agx_batch *batch=native->batch;
  fprintf(stderr,"NATIVE_DRAW_STATE: batch=%u draws=%u capsule=%u failed=%u faults=%u\n",
      batch!=NULL,batch?batch->draws:0,batch && batch->windows_batch,backend->Failed,native->any_faults);
  RUNTIME_CHECK(batch && batch->draws==1 && batch->windows_batch && !backend->Failed);
  if(!batch || batch->draws!=1 || !batch->windows_batch || backend->Failed) goto cleanup;
  ctx->flush(ctx,NULL,0);
  AGX_WIN32_ASAHI_BATCH *held=batch->windows_batch;
  RUNTIME_CHECK(held && held->Submitted && !held->Retired);
  if(!held || !held->Submitted) goto cleanup;
  checkpoint(owner,1); /* Verify holds, then signal the actual UMD marker. */
  marker_signalled=1;
  agx_sync_all(native,"Windows native proof retirement");
  RUNTIME_CHECK(!batch->windows_batch && !agx_batch_is_submitted(batch));
  if(batch->windows_batch || agx_batch_is_submitted(batch)) goto cleanup;
  /* Unsupported topology is rejected through the same real draw entry before
   * a new batch, capture or submission can exist. Positive work already retired. */
  RUNTIME_CHECK(!native->any_faults && !native->batch && !backend->ActiveCapture);
  info.mode=MESA_PRIM_LINES;
  ctx->draw_vbo(ctx,&info,0,NULL,&draw,1);
  RUNTIME_CHECK(native->any_faults && !native->batch && !backend->ActiveCapture && !backend->Failed);
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i)
    RUNTIME_CHECK(!native->batches.slots[i].windows_batch &&
        !BITSET_TEST(native->batches.active,i) &&
        !BITSET_TEST(native->batches.submitted,i));
cleanup:
  /* Never let unbinding/destruction flush a partially constructed failed draw.
   * Abort only pre-submit work; accepted work keeps its storage until the same
   * existing marker retires. No reset, replay, or force-cleared ownership. */
  if(native) {
    for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
      struct agx_batch *pending=&native->batches.slots[i];
      AGX_WIN32_ASAHI_BATCH *c=pending->windows_batch;
      if(c && c->Submitted && !c->Retired) {
        if(!marker_signalled) {checkpoint(owner,1);marker_signalled=1;}
        agx_sync_batch(native,pending);
      } else if(c || BITSET_TEST(native->batches.active,i) || BITSET_TEST(native->batches.submitted,i)) {
        agx_batch_reset(native,pending);
      }
      if(pending->windows_batch || BITSET_TEST(native->batches.active,i) || BITSET_TEST(native->batches.submitted,i)) ++retained;
    }
    RUNTIME_CHECK(!retained);
    if(retained) {
      fprintf(stderr,"NATIVE_RUNTIME_CLEANUP: retained_batches=%u (completion/unwind not proved)\n",retained);
      return errors;
    }
    ctx->bind_vs_state(ctx,NULL);ctx->bind_fs_state(ctx,NULL);
    if(vs) ctx->delete_vs_state(ctx,vs);
    if(fs) ctx->delete_fs_state(ctx,fs);
    ctx->bind_vertex_elements_state(ctx,NULL);if(ve) ctx->delete_vertex_elements_state(ctx,ve);
    ctx->bind_blend_state(ctx,NULL);if(bs) ctx->delete_blend_state(ctx,bs);
    ctx->bind_rasterizer_state(ctx,NULL);if(rs) ctx->delete_rasterizer_state(ctx,rs);
    ctx->bind_depth_stencil_alpha_state(ctx,NULL);if(ds) ctx->delete_depth_stencil_alpha_state(ctx,ds);
    ctx->set_vertex_buffers(ctx,0,NULL);
    memset(&fb,0,sizeof(fb));ctx->set_framebuffer_state(ctx,&fb);
  }
  pipe_resource_reference(&vb,NULL);pipe_resource_reference(&rt,NULL);
  if(ctx) ctx->destroy(ctx);
  if(screen) {
    struct agx_device *saved_native=&agx_screen(screen)->dev;
    struct agx_bo *saved_rodata=agx_screen(screen)->rodata;
    void *saved_helper=screen->transfer_helper;
    RUNTIME_CHECK(saved_rodata && saved_helper && backend->LiveBos==1);
    if(saved_rodata && saved_helper && backend->LiveBos==1) {
      checkpoint(owner,3);
      screen->destroy(screen);
      RUNTIME_CHECK(backend->Native==saved_native && backend->BatchOps==submit && backend->BatchOwner==owner);
      if(backend->Native==saved_native) {
        /* The screen relinquishes rodata once; its zero-ref backing stays in
         * the real owner until Collect can deallocate it. The helper must
         * remain available while detach is retryable. */
        int retry_safe=agx_screen(screen)->rodata==NULL &&
            screen->transfer_helper==saved_helper && saved_rodata->refcnt==0;
        RUNTIME_CHECK(retry_safe);
        checkpoint(owner,4);
        if(!retry_safe) return errors; /* RED must not deliberately double-free. */
        screen->destroy(screen);
      } else {
        checkpoint(owner,4); /* failed injection is evidence, not a retry */
      }
      RUNTIME_CHECK(!backend->Native && !backend->BatchOps && !backend->BatchOwner);
    } else screen->destroy(screen);
  }
  checkpoint(owner,2);
  if(!errors) {
    /* Reuse the same authoritative backend after completed teardown. Stale
     * BatchOps would otherwise make the second real ScreenCreate fail. */
    struct pipe_screen *second=AgxWin32AsahiScreenCreate(backend,windows,owners,owner,submit,&params);
    RUNTIME_CHECK(second && backend->BatchOps==submit && backend->BatchOwner==owner);
    if(second) {
      struct pipe_context *second_ctx=second->context_create(second,owner,0);
      RUNTIME_CHECK(second_ctx);
      if(second_ctx) second_ctx->destroy(second_ctx);
      second->destroy(second);
      RUNTIME_CHECK(!backend->Native && !backend->BatchOps && !backend->BatchOwner);
    }
    checkpoint(owner,2);
    printf("NATIVE_RUNTIME_RECREATE: errors=%u (same backend, real second screen/context)\n",errors);
  }
  printf("NATIVE_RUNTIME_PRODUCER: errors=%u (actual context/resource/NIR/clear/draw/flush/retire)\n",errors);
  return errors;
#undef RUNTIME_CHECK
}
