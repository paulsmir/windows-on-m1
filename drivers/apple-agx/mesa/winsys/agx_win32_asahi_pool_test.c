#include "agx_device.h"
#include "gallium/drivers/asahi/agx_state.h"
#include "pool.h"
#include "agx_win32_asahi_bo.h"
#include "agx_win32_asahi_capture.h"
#include "agx_win32_asahi_pipeline.h"
#include "agx_pack.h"
#include <stdio.h>
#include <string.h>

struct agx_encoder AgxWin32NativeEncoderAllocateTest(struct agx_batch *,struct agx_device *);
uint8_t *AgxWin32NativeEncodeStateTest(struct agx_batch *,uint8_t *);

/* Calls the original native pool implementation; only Windows runtime callbacks
 * are controlled by the enclosing UMD owner test. No draw packet is fabricated. */
unsigned AgxWin32AsahiPoolTest(AGX_WIN32_SCREEN *screen,
    const AGX_WIN32_ASAHI_OWNER_OPS *ops,void *owner,
    AGX_WIN32_ASAHI_BACKEND *backend,void (*holds)(void *,int)) {
  struct agx_device native={0};
  struct agx_pool pool, state_pool;
  struct agx_bo *first=NULL,*second=NULL;
  AGX_WIN32_RELOC_ALLOCATION identity;
  AGX_WIN32_ASAHI_CAPTURE capture={0};
  APPLE_AGX_U32 reference;
  unsigned errors=0;
#define CHECK_NATIVE(x) do { if(!(x)) { ++errors; fprintf(stderr,"NATIVE_POOL line=%u %s\n",(unsigned)__LINE__,#x); } } while(0)
  CHECK_NATIVE(AgxWin32AsahiAttach(backend,&native,screen,ops,owner,0x1100000000ULL));
  {
    struct agx_batch batch={0};
    struct agx_encoder native_encoder=AgxWin32NativeEncoderAllocateTest(&batch,&native);
    struct agx_bo *encoder=native_encoder.bo;
    APPLE_AGX_U32 classId=0;
    CHECK_NATIVE(encoder && AgxWin32AsahiClass(backend,encoder,&classId) &&
                 classId==AgxWin32BufferClassEncoder && native_encoder.current &&
                 native_encoder.end-native_encoder.current==0x80000);
    agx_bo_unreference(&native,encoder);
  }
  agx_pool_init(&pool,&native,"Windows native pipeline",AGX_BO_LOW_VA,false);
  agx_pool_init(&state_pool,&native,"Windows state source",0,false);
  struct agx_ptr a=agx_pool_alloc_aligned_with_bo(&pool,64,64,&first);
  struct agx_ptr b=agx_pool_alloc_aligned_with_bo(&pool,128,64,&second);
  struct agx_ptr state_bytes=agx_pool_alloc_aligned(&state_pool,128,64);
  CHECK_NATIVE(first && first==second && first->dev==&native);
  CHECK_NATIVE(a.cpu && b.cpu && (char *)b.cpu-(char *)a.cpu==64);
  CHECK_NATIVE(state_bytes.cpu && state_bytes.gpu != a.gpu);
  CHECK_NATIVE(b.gpu-a.gpu==64 && first->va->addr==a.gpu);
  if(a.cpu) memset(a.cpu,0x37,64);
  CHECK_NATIVE(AgxWin32AsahiIdentity(backend,first,&identity));
  CHECK_NATIVE(identity.Owner && identity.Token && identity.Serial &&
               identity.Generation==screen->Generation && identity.Bytes==0x40000 &&
               identity.AllocationIndex==~0u);
  CHECK_NATIVE(!AgxWin32AsahiIdentity(backend,(struct agx_bo *)(uintptr_t)1,&identity));
  CHECK_NATIVE(AgxWin32AsahiIdentity(backend,first,&identity));
  {
    struct agx_bo *found=NULL; APPLE_AGX_U64 offset=0;
    CHECK_NATIVE(AgxWin32AsahiFindAddress(backend,identity.Owner,identity.Generation,
        b.gpu,128,&found,&offset) && found==first && offset==64);
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,identity.Owner+1,identity.Generation,
        b.gpu,128,&found,&offset) && !found && !offset);
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,identity.Owner,identity.Generation+1,
        b.gpu,128,&found,&offset) && !found && !offset);
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,identity.Owner,identity.Generation,
        a.gpu+0x3ffff,2,&found,&offset));
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,identity.Owner,identity.Generation,
        a.gpu+0x1000000,1,&found,&offset));
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,identity.Owner,identity.Generation,
        ~(APPLE_AGX_U64)0-7,16,&found,&offset));
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,identity.Owner,identity.Generation,
        a.gpu,0,&found,&offset));
    ++first->va->addr;
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,identity.Owner,identity.Generation,
        a.gpu+64,1,&found,&offset));
    --first->va->addr;
    CHECK_NATIVE(first->refcnt==1); /* Lookup alone is not a source hold. */
  }
  CHECK_NATIVE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,1)==AgxRelocOk);
  {
    struct agx_bo *found=NULL; APPLE_AGX_U64 address=0,offset=0;
    CHECK_NATIVE(AgxWin32AsahiFindCpuAddress(backend,identity.Owner,identity.Generation,
        b.cpu,128,&found,&address,&offset) && found==first && address==b.gpu && offset==64);
    CHECK_NATIVE(!AgxWin32AsahiFindCpuAddress(backend,identity.Owner,identity.Generation,
        (char *)b.cpu+0x1000000,2,&found,&address,&offset));
    CHECK_NATIVE(!AgxWin32AsahiFindCpuAddress(backend,identity.Owner,identity.Generation+1,
        b.cpu,128,&found,&address,&offset));
    CHECK_NATIVE(!AgxWin32AsahiFindCpuAddress(backend,identity.Owner,identity.Generation,
        b.cpu,0,&found,&address,&offset));
  }
  CHECK_NATIVE(AgxWin32AsahiCaptureReference(&capture,first,AppleAgxWin32RoleUscPipeline,
      AppleAgxWin32AccessRead,0,64,&reference)==AgxRelocOk && reference==0);
  CHECK_NATIVE(AgxWin32AsahiCaptureAddress(&capture,b.gpu,128,AppleAgxWin32RoleUscPipeline,
      AppleAgxWin32AccessRead,&reference)==AgxRelocOk && reference==1);
  CHECK_NATIVE(AgxWin32AsahiCaptureCpuRange(&capture,a.cpu,a.gpu,64,
      AppleAgxWin32RoleUscPipeline,AppleAgxWin32AccessRead,&reference)==AgxRelocOk && reference==0);
  CHECK_NATIVE(AgxWin32AsahiCaptureCpuRange(&capture,a.cpu,a.gpu+4,64,
      AppleAgxWin32RoleUscPipeline,AppleAgxWin32AccessRead,&reference)==AgxRelocStale);
  CHECK_NATIVE(AgxWin32AsahiCaptureAddress(&capture,a.gpu+0x1000000,128,
      AppleAgxWin32RoleUscPipeline,AppleAgxWin32AccessRead,&reference)!=AgxRelocOk);
  CHECK_NATIVE(first->refcnt==3 && capture.Capture.ReferenceCount==2);
  CHECK_NATIVE(capture.Count==1 && capture.Capture.ReferenceCount==2 &&
      capture.Capture.Allocations[0].Token==identity.Token &&
      capture.Capture.Allocations[1].Serial==identity.Serial &&
      capture.Capture.References[0].AllocationIndex==0 &&
      capture.Capture.References[1].AllocationIndex==0);
  /* Native USC emission into real pool memory: metadata comes from its live
   * Windows owner, not a synthetic token table. */
  CHECK_NATIVE(AgxWin32RelocAbort(&capture.Capture)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,2)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureActivate(&capture));
  CHECK_NATIVE(!AgxWin32AsahiCaptureActivate(&capture));
  AGX_WIN32_ASAHI_PIPELINE emission={0};
  CHECK_NATIVE(AgxWin32AsahiPipelineBegin(&native,a.cpu,a.gpu,64,&emission));
  CHECK_NATIVE(!AgxWin32AsahiCaptureDeactivate(&capture));
  agx_pack(a.cpu,USC_TEXTURE,cfg) { cfg.start=0; cfg.count=1; cfg.buffer=b.gpu; }
  AgxWin32AsahiPipelineRecord(&emission,(char *)a.cpu+8,
      AppleAgxWin32RelocationUscTableAddress39,b.gpu,AGX_TEXTURE_LENGTH,
      AppleAgxWin32RoleDescriptor);
  CHECK_NATIVE(AgxWin32AsahiPipelineFinish(&emission,(char *)a.cpu+8));
  CHECK_NATIVE(capture.Capture.ReferenceCount==2 && capture.Capture.RelocationCount==1 &&
      capture.Capture.References[0].Bytes==8 && capture.Capture.References[1].Offset==64 &&
      capture.Capture.Relocations[0].TargetReference==1);
  CHECK_NATIVE(AgxWin32AsahiCaptureDeactivate(&capture));
  CHECK_NATIVE(!AgxWin32AsahiPipelineBegin(&native,a.cpu,a.gpu,64,&emission));
  CHECK_NATIVE(AgxWin32RelocAbort(&capture.Capture)==AgxRelocOk);
  /* A state/VDM range may surround a nested USC emission. Scope restoration
   * must preserve the parent rather than treating nesting as a second request. */
  CHECK_NATIVE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,3)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureActivate(&capture));
  AGX_WIN32_ASAHI_PIPELINE state={0};
  CHECK_NATIVE(AgxWin32AsahiEmissionBegin(&native,state_bytes.cpu,state_bytes.gpu,128,
      AppleAgxWin32RoleDescriptor,&state));
  CHECK_NATIVE(AgxWin32AsahiPipelineBegin(&native,a.cpu,a.gpu,64,&emission));
  agx_pack(a.cpu,USC_TEXTURE,cfg) { cfg.start=0; cfg.count=1; cfg.buffer=b.gpu; }
  AgxWin32AsahiPipelineRecord(&emission,(char *)a.cpu+8,
      AppleAgxWin32RelocationUscTableAddress39,b.gpu,64,AppleAgxWin32RoleDescriptor);
  CHECK_NATIVE(AgxWin32AsahiPipelineFinish(&emission,(char *)a.cpu+8));
  CHECK_NATIVE(state.Capture==&capture && backend->ActiveEmission==&state);
  CHECK_NATIVE(AgxWin32AsahiPipelineFinish(&state,(char *)state_bytes.cpu+128));
  CHECK_NATIVE(capture.Capture.ReferenceCount==3 && capture.Capture.RelocationCount==1);
  CHECK_NATIVE(AgxWin32AsahiCaptureDeactivate(&capture));
  CHECK_NATIVE(AgxWin32RelocAbort(&capture.Capture)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,4)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureActivate(&capture));
  CHECK_NATIVE(!AgxWin32AsahiPipelineBegin(&native,b.cpu,a.gpu,64,&emission));
  CHECK_NATIVE(capture.Capture.ReferenceCount==0);
  CHECK_NATIVE(AgxWin32AsahiPipelineBegin(&native,a.cpu,a.gpu,64,&emission));
  agx_pack(a.cpu,USC_TEXTURE,cfg) { cfg.count=1; cfg.buffer=b.gpu+8; }
  AgxWin32AsahiPipelineRecord(&emission,(char *)a.cpu+8,
      AppleAgxWin32RelocationUscTableAddress39,b.gpu,AGX_TEXTURE_LENGTH,
      AppleAgxWin32RoleDescriptor);
  CHECK_NATIVE(!AgxWin32AsahiPipelineFinish(&emission,(char *)a.cpu+8));
  CHECK_NATIVE(capture.Capture.State==0 && first->refcnt==1);
  CHECK_NATIVE(AgxWin32AsahiCaptureDeactivate(&capture));
  CHECK_NATIVE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,5)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureReference(&capture,first,AppleAgxWin32RoleUscPipeline,
      AppleAgxWin32AccessRead,0,64,&reference)==AgxRelocOk);
  CHECK_NATIVE(AgxWin32AsahiCaptureReference(&capture,first,AppleAgxWin32RoleDescriptor,
      AppleAgxWin32AccessRead,64,128,&reference)==AgxRelocOk);
  holds(owner,1);
  agx_pool_cleanup(&pool);
  agx_pool_cleanup(&state_pool);
  CHECK_NATIVE(first->refcnt==2);
  CHECK_NATIVE(AgxWin32RelocAbort(&capture.Capture)==AgxRelocOk);
  CHECK_NATIVE(backend->LiveBos==2 && !AgxWin32AsahiCollect(backend));
  CHECK_NATIVE(!AgxWin32AsahiIdentity(backend,first,&identity));
  {
    struct agx_bo *found=NULL; APPLE_AGX_U64 offset=0;
    CHECK_NATIVE(!AgxWin32AsahiFindAddress(backend,capture.Capture.Owner,screen->Generation,
        a.gpu,64,&found,&offset) && !found && !offset);
  }
  CHECK_NATIVE(!AgxWin32AsahiDetach(backend));
  holds(owner,0);
  holds(owner,4);
  CHECK_NATIVE(!AgxWin32AsahiCollect(backend) && backend->LiveBos==1);
  CHECK_NATIVE(AgxWin32AsahiCollect(backend));
  CHECK_NATIVE(backend->LiveBos==0 && AgxWin32AsahiDetach(backend));
  CHECK_NATIVE(AgxWin32AsahiAttach(backend,&native,screen,ops,owner,0x1100000000ULL));
  holds(owner,2);
  agx_pool_init(&pool,&native,"Windows allocation failure",AGX_BO_LOW_VA,false);
  first=(struct agx_bo *)(uintptr_t)1;
  a=agx_pool_alloc_aligned_with_bo(&pool,64,64,&first);
  CHECK_NATIVE(!a.cpu && !a.gpu && !first && backend->Failed);
  {
    struct agx_batch failed_batch={0};
    struct agx_encoder failed_encoder=AgxWin32NativeEncoderAllocateTest(&failed_batch,&native);
    CHECK_NATIVE(!failed_encoder.bo && !failed_encoder.current && !failed_encoder.end);
  }
  agx_pool_cleanup(&pool);
  holds(owner,3);
  CHECK_NATIVE(AgxWin32AsahiDetach(backend));
  printf("NATIVE_POOL_WINDOWS_OWNER: errors=%u\n",errors);
  return errors;
}

#ifdef AGX_WIN32_NATIVE_STATE_TEST
/* Catches a missing test export or a state hook that mutates capture on the
 * native dirty-zero fast path. The initial Encoder is allocated by the exact
 * transformed native batch helper, and capture remains active throughout. */
unsigned AgxWin32AsahiStateDirtyZeroTest(AGX_WIN32_SCREEN *screen,
    const AGX_WIN32_ASAHI_OWNER_OPS *ops,void *owner,
    AGX_WIN32_ASAHI_BACKEND *backend) {
  struct agx_device native={0};
  struct agx_context ctx={0};
  struct agx_batch batch={0};
  AGX_WIN32_ASAHI_CAPTURE capture={0};
  AGX_WIN32_RELOC_ALLOCATION identity;
  unsigned errors=0;
#define CHECK_STATE(x) do { if(!(x)) { ++errors; fprintf(stderr,"NATIVE_STATE line=%u %s\n",(unsigned)__LINE__,#x); } } while(0)
  CHECK_STATE(AgxWin32AsahiAttach(backend,&native,screen,ops,owner,0x1100000000ULL));
  batch.ctx=&ctx;
  struct agx_encoder encoder=AgxWin32NativeEncoderAllocateTest(&batch,&native);
  APPLE_AGX_U32 classId=0;
  CHECK_STATE(encoder.bo && encoder.current &&
      AgxWin32AsahiClass(backend,encoder.bo,&classId) &&
      classId==AgxWin32BufferClassEncoder && encoder.end-encoder.current==0x80000);
  CHECK_STATE(AgxWin32AsahiIdentity(backend,encoder.bo,&identity));
  CHECK_STATE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,
      identity.Generation,5)==AgxRelocOk && AgxWin32AsahiCaptureActivate(&capture));
  ctx.dirty=0;
  CHECK_STATE(AgxWin32NativeEncodeStateTest(&batch,encoder.current)==encoder.current);
  CHECK_STATE(capture.Capture.ReferenceCount==0 &&
      capture.Capture.RelocationCount==0 && backend->ActiveCapture==&capture &&
      backend->ActiveEmission==NULL && !backend->Failed);
  CHECK_STATE(AgxWin32AsahiCaptureDeactivate(&capture));
  CHECK_STATE(AgxWin32RelocAbort(&capture.Capture)==AgxRelocOk);
  if(encoder.bo) agx_bo_unreference(&native,encoder.bo);
  CHECK_STATE(AgxWin32AsahiDetach(backend));
  printf("ACTUAL_NATIVE_ENCODE_STATE_DIRTY_ZERO: errors=%u (no native state bytes emitted)\n",errors);
  return errors;
}

#endif /* AGX_WIN32_NATIVE_STATE_TEST */
