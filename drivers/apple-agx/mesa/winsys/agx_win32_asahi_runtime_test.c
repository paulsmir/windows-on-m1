#include "gallium/drivers/asahi/agx_state.h"
#include "compiler/nir/nir_builder.h"
#include "asahi/lib/agx_linker.h"
#include "util/u_inlines.h"
#include "drm-uapi/drm_fourcc.h"
#include "agx_win32_asahi_scene.h"
#include <stdio.h>
#include <string.h>

/* Kernel/runtime callbacks are controlled by the existing UMD test. Everything
 * above that boundary is the actual native producer, including NIR compilation. */
unsigned AgxWin32AsahiRuntimeTest(AGX_WIN32_SCREEN *windows,
    const AGX_WIN32_ASAHI_OWNER_OPS *owners,void *owner,AGX_WIN32_ASAHI_BACKEND *backend,
    const AGX_WIN32_ASAHI_BATCH_OPS *submit,void (*checkpoint)(void *,unsigned)) {
  unsigned errors=0;
  struct pipe_screen *screen=NULL;
  struct pipe_context *ctx=NULL;
  struct agx_context *native=NULL;
  AGX_WIN32_ASAHI_SCENE scene={0};
  unsigned marker_signalled=0;
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
  /* Offline QueryDevice callbacks remain controlled, while this is the same
   * validated KMD-contract constructor used by the real qualification caller. */
  screen=AgxWin32AsahiScreenCreateForWindows(backend,windows,owners,owner,submit);
  RUNTIME_CHECK(screen);if(!screen) goto cleanup;
  ctx=AgxWin32AsahiContextCreate(screen,owner);
  RUNTIME_CHECK(ctx);if(!ctx) goto cleanup;
  native=agx_context(ctx);
  RUNTIME_CHECK(AgxWin32AsahiSceneInit(&scene,screen,ctx));
  if(scene.Phase!=AgxAsahiScenePrepared) goto cleanup;
  printf("NATIVE_RUNTIME_TARGET: bytes=%llu tiling=%u stride=%llu\n",
      (unsigned long long)scene.Receipt.TargetBytes,scene.Receipt.Tiling,
      (unsigned long long)scene.Receipt.TargetLayerStride);
  /* active_queries enables query updates; it is not a bound query object.
   * Exercise that distinction on the real context before the valid draw. */
  native->active_queries=true;
  RUNTIME_CHECK(!native->cond_query && !native->occlusion_query && !native->time_elapsed &&
      !native->tf_any_overflow);
  for(unsigned i=0;i<ARRAY_SIZE(native->prims_generated);++i)
    RUNTIME_CHECK(!native->prims_generated[i] && !native->tf_prims_generated[i] && !native->tf_overflow[i]);
  for(unsigned i=0;i<ARRAY_SIZE(native->pipeline_statistics);++i)
    RUNTIME_CHECK(!native->pipeline_statistics[i]);
  RUNTIME_CHECK(AgxWin32AsahiSceneDraw(&scene));
  struct agx_batch *batch=native->batch;
  fprintf(stderr,"NATIVE_DRAW_STATE: batch=%u draws=%u capsule=%u failed=%u faults=%u\n",
      batch!=NULL,batch?batch->draws:0,batch && batch->windows_batch,backend->Failed,native->any_faults);
  RUNTIME_CHECK(batch && batch->draws==1 && batch->windows_batch && !backend->Failed);
  if(!batch || batch->draws!=1 || !batch->windows_batch || backend->Failed) goto cleanup;
  RUNTIME_CHECK(AgxWin32AsahiSceneSubmit(&scene));
  AGX_WIN32_ASAHI_BATCH *held=batch->windows_batch;
  RUNTIME_CHECK(held && held->Submitted && !held->Retired);
  if(!held || !held->Submitted) goto cleanup;
  RUNTIME_CHECK(scene.Phase==AgxAsahiSceneSubmitted && scene.Receipt.Request==held->Request &&
      scene.Receipt.References==held->Capture.Capture.ReferenceCount &&
      scene.Receipt.Relocations==held->Capture.Capture.RelocationCount && scene.Receipt.EncoderBytes==137);
  RUNTIME_CHECK(!AgxWin32AsahiSceneRetire(&scene,0));
  RUNTIME_CHECK(scene.Phase==AgxAsahiSceneSubmitted && scene.Target && batch->windows_batch==held);
  checkpoint(owner,1); /* Verify holds, then signal the actual UMD marker. */
  marker_signalled=1;
  RUNTIME_CHECK(AgxWin32AsahiSceneRetire(&scene,0));
  RUNTIME_CHECK(!batch->windows_batch && !agx_batch_is_submitted(batch));
  if(batch->windows_batch || agx_batch_is_submitted(batch)) goto cleanup;
  /* Unsupported topology is rejected through the same real draw entry before
   * a new batch, capture or submission can exist. Positive work already retired. */
  RUNTIME_CHECK(!native->any_faults && !native->batch && !backend->ActiveCapture);
  struct pipe_draw_info info={0};info.mode=MESA_PRIM_LINES;info.instance_count=1;
  struct pipe_draw_start_count_bias draw={0};draw.count=3;
  ctx->draw_vbo(ctx,&info,0,NULL,&draw,1);
  RUNTIME_CHECK(native->any_faults && !native->batch && !backend->ActiveCapture && !backend->Failed);
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i)
    RUNTIME_CHECK(!native->batches.slots[i].windows_batch &&
        !BITSET_TEST(native->batches.active,i) &&
        !BITSET_TEST(native->batches.submitted,i));
cleanup:
  /* Shared scene cleanup does not signal events or destroy caller objects. */
  if(scene.Phase==AgxAsahiSceneSubmitted && !marker_signalled) {
    checkpoint(owner,1);marker_signalled=1;
  }
  if(!AgxWin32AsahiSceneCleanup(&scene,1000)) {
    RUNTIME_CHECK(0);
    fprintf(stderr,"NATIVE_RUNTIME_CLEANUP: retained scene phase=%u\n",scene.Phase);
    return errors;
  }
  if(ctx) RUNTIME_CHECK(AgxWin32AsahiContextDestroy(ctx));
  if(screen) {
    struct agx_device *saved_native=&agx_screen(screen)->dev;
    struct agx_bo *saved_rodata=agx_screen(screen)->rodata;
    void *saved_helper=screen->transfer_helper;
    RUNTIME_CHECK(saved_rodata && saved_helper && backend->LiveBos==1);
    if(saved_rodata && saved_helper && backend->LiveBos==1) {
      checkpoint(owner,3);
      RUNTIME_CHECK(!AgxWin32AsahiScreenDestroy(screen));
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
        RUNTIME_CHECK(AgxWin32AsahiScreenDestroy(screen));
      } else {
        checkpoint(owner,4); /* failed injection is evidence, not a retry */
      }
      RUNTIME_CHECK(!backend->Native && !backend->BatchOps && !backend->BatchOwner);
    } else RUNTIME_CHECK(AgxWin32AsahiScreenDestroy(screen));
  }
  checkpoint(owner,2);
  if(!errors) {
    /* Reuse the same authoritative backend after completed teardown. Stale
     * BatchOps would otherwise make the second real ScreenCreate fail. */
    struct pipe_screen *second=AgxWin32AsahiScreenCreateForWindows(backend,windows,owners,owner,submit);
    RUNTIME_CHECK(second && backend->BatchOps==submit && backend->BatchOwner==owner);
    if(second) {
      struct pipe_context *second_ctx=AgxWin32AsahiContextCreate(second,owner);
      RUNTIME_CHECK(second_ctx);
      if(second_ctx) {
        AGX_WIN32_ASAHI_SCENE immediate={0};
        checkpoint(owner,5);
        RUNTIME_CHECK(AgxWin32AsahiSceneInit(&immediate,second,second_ctx));
        RUNTIME_CHECK(AgxWin32AsahiSceneDraw(&immediate));
        RUNTIME_CHECK(AgxWin32AsahiSceneSubmit(&immediate));
        RUNTIME_CHECK(immediate.Phase==AgxAsahiSceneSubmitted && immediate.Batch &&
            immediate.Batch->windows_batch && immediate.Receipt.Request && immediate.Receipt.Relocations);
        checkpoint(owner,6);
        RUNTIME_CHECK(AgxWin32AsahiSceneRetire(&immediate,0));
        RUNTIME_CHECK(immediate.Phase==AgxAsahiSceneRetired && immediate.Target && !immediate.Batch);
        RUNTIME_CHECK(AgxWin32AsahiSceneCleanup(&immediate,1000));
        if(immediate.Phase!=AgxAsahiSceneReleased) return errors;
        RUNTIME_CHECK(AgxWin32AsahiContextDestroy(second_ctx));
      }
      RUNTIME_CHECK(AgxWin32AsahiScreenDestroy(second));
      RUNTIME_CHECK(!backend->Native && !backend->BatchOps && !backend->BatchOwner);
    }
    checkpoint(owner,2);
    printf("NATIVE_RUNTIME_RECREATE: errors=%u (same backend, real second screen/context)\n",errors);
  }
  printf("NATIVE_RUNTIME_PRODUCER: errors=%u (actual context/resource/NIR/clear/draw/flush/retire)\n",errors);
  return errors;
#undef RUNTIME_CHECK
}
