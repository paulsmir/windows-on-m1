#include "gallium/drivers/asahi/agx_state.h"
#include "agx_win32_asahi_pipeline.h"
#include "asahi/lib/agx_ppp.h"
#include <stdio.h>
#include <string.h>

uint32_t AgxWin32NativeBuildPipelineTest(struct agx_batch *,struct agx_compiled_shader *,
    struct agx_linked_shader *,mesa_shader_stage);

/* Real regression: native PPP is General-backed and its pointer edges must
 * retain exact, already-finished native USC spans through two nesting levels.
 * These are the pinned PPP header/push/fini and actual USC emitter with
 * controlled inputs, not the whole agx_encode_state or a submitted draw. */
static unsigned TestNativePppCapture(struct agx_batch *batch,
    struct agx_compiled_shader *original, AGX_WIN32_ASAHI_BACKEND *backend,
    const AGX_WIN32_RELOC_ALLOCATION *identity) {
  unsigned errors=0;
  struct agx_device *dev=backend->Native;
  struct agx_compiled_shader shader=*original;
  shader.push_range_count=0;
  unsigned saved_textures[2]={batch->texture_count[MESA_SHADER_VERTEX],batch->texture_count[MESA_SHADER_FRAGMENT]};
  unsigned saved_samplers[2]={batch->sampler_count[MESA_SHADER_VERTEX],batch->sampler_count[MESA_SHADER_FRAGMENT]};
  batch->texture_count[MESA_SHADER_VERTEX]=batch->texture_count[MESA_SHADER_FRAGMENT]=0;
  batch->sampler_count[MESA_SHADER_VERTEX]=batch->sampler_count[MESA_SHADER_FRAGMENT]=0;
#define CHECK_PPP(x) do { if(!(x)) { ++errors; fprintf(stderr,"NATIVE_PPP line=%u %s\n",(unsigned)__LINE__,#x); } } while(0)
  struct AGX_PPP_HEADER present={.fragment_shader=true,.viewport_count=1};
  size_t ppp_bytes=agx_ppp_update_size(&present);
  size_t cf_bytes=AGX_CF_BINDING_HEADER_LENGTH+AGX_CF_BINDING_LENGTH;
  struct agx_ptr vdm=agx_pool_alloc_aligned(&batch->pipeline_pool,64,64);
  struct agx_ptr ppp_source=agx_pool_alloc_aligned(&batch->pool,ppp_bytes,AGX_PPP_HEADER_ALIGN);
  struct agx_ptr cf=agx_pool_alloc_aligned(&batch->pipeline_pool,cf_bytes,16);
  CHECK_PPP(vdm.cpu && ppp_source.cpu && cf.cpu);
  if(!vdm.cpu || !ppp_source.cpu || !cf.cpu) return errors+1;
  memset(cf.cpu,0,cf_bytes);
  for(unsigned pass=0;pass<6;++pass) {
    AGX_WIN32_ASAHI_CAPTURE c={0};
    AGX_WIN32_ASAHI_PIPELINE encoder={0},ppp_scope={0};
    CHECK_PPP(AgxWin32AsahiCaptureBegin(&c,backend,identity->Owner,identity->Generation,pass+1)==AgxRelocOk);
    CHECK_PPP(AgxWin32AsahiCaptureActivate(&c));
    uint8_t *out=vdm.cpu;
    CHECK_PPP(AgxWin32AsahiEmissionBegin(dev,vdm.cpu,vdm.gpu,64,AppleAgxWin32RoleEncoder,&encoder));
    shader.stage=shader.b.info.stage=MESA_SHADER_VERTEX;
    uint32_t vs=AgxWin32NativeBuildPipelineTest(batch,&shader,NULL,MESA_SHADER_VERTEX);
    CHECK_PPP(vs && backend->ActiveEmission==&encoder);
    agx_push(out,VDM_STATE_VERTEX_SHADER_WORD_1,cfg) { cfg.pipeline=vs; }
    AgxWin32AsahiPipelineRecordCaptured(&encoder,out,AppleAgxWin32RelocationVdmPipelineOffset32,
        dev->shader_base+vs+(pass==1?64:0),AppleAgxWin32RoleUscPipeline);
    if(pass==1) {
      CHECK_PPP(encoder.Failed && !AgxWin32AsahiPipelineFinish(&encoder,out));
    } else {
      struct agx_ppp_update ppp=agx_new_ppp_update(ppp_source,ppp_bytes,&present);
      CHECK_PPP(AgxWin32AsahiEmissionBegin(dev,ppp_source.cpu,ppp_source.gpu,
          (APPLE_AGX_U32)ppp_bytes,AppleAgxWin32RolePppState,&ppp_scope));
      CHECK_PPP(backend->ActiveEmission==&ppp_scope);
      agx_ppp_push(&ppp,FRAGMENT_SHADER_WORD_0,cfg) { cfg.cf_binding_count=1; }
      shader.stage=shader.b.info.stage=MESA_SHADER_FRAGMENT;
      uint32_t fs=AgxWin32NativeBuildPipelineTest(batch,&shader,NULL,MESA_SHADER_FRAGMENT);
      CHECK_PPP(fs && backend->ActiveEmission==&ppp_scope);
      agx_ppp_push(&ppp,FRAGMENT_SHADER_WORD_1,cfg) { cfg.pipeline=fs; }
      AgxWin32AsahiPipelineRecordCaptured(&ppp_scope,ppp.head,AppleAgxWin32RelocationPppPipelineOffset32,
          dev->shader_base+fs,AppleAgxWin32RoleUscPipeline);
      agx_ppp_push(&ppp,FRAGMENT_SHADER_WORD_2,cfg) { cfg.cf_bindings=agx_usc_addr(dev,cf.gpu); }
      AgxWin32AsahiPipelineRecord(&ppp_scope,ppp.head,AppleAgxWin32RelocationPppCfBindingsOffset32,
          cf.gpu+(pass==2?4:0),cf_bytes,AppleAgxWin32RoleDescriptor);
      agx_ppp_push(&ppp,FRAGMENT_SHADER_WORD_3,cfg) { cfg.unknown=0; }
      int ppp_finished=AgxWin32AsahiPipelineFinish(&ppp_scope,ppp.head);
      CHECK_PPP(ppp_finished==(pass!=2));
      CHECK_PPP(backend->ActiveEmission==&encoder);
      agx_ppp_fini(&out,&ppp);
      if(pass==3) ((uint8_t *)out)[-AGX_PPP_STATE_LENGTH+1]++;
      if(pass==5) ((uint8_t *)out)[-AGX_PPP_STATE_LENGTH+3]|=0x20;
      AgxWin32AsahiPipelineRecordCaptured(&encoder,out,AppleAgxWin32RelocationPppStateAddress40,
          ppp_source.gpu,AppleAgxWin32RolePppState);
      if(pass==4) {
        AGX_WIN32_ASAHI_PIPELINE wrong={0};
        CHECK_PPP(!AgxWin32AsahiEmissionBegin(dev,cf.cpu,cf.gpu,(APPLE_AGX_U32)cf_bytes,
            AppleAgxWin32RolePppState,&wrong));
      }
      int finished=AgxWin32AsahiPipelineFinish(&encoder,out);
      CHECK_PPP(finished==(pass==0 || pass==4));
      if(finished) {
        unsigned saw_vs=0,saw_fs=0,saw_cf=0,saw_ppp=0;
        CHECK_PPP(c.Capture.References[encoder.Reference].Bytes==12);
        for(unsigned i=0;i<c.Capture.RelocationCount;++i) {
          const APPLE_AGX_WIN32_RELOCATION *r=&c.Capture.Relocations[i];
          const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *target=&c.Capture.References[r->TargetReference];
          if(r->Kind==AppleAgxWin32RelocationVdmPipelineOffset32) {
            ++saw_vs; CHECK_PPP(target->Bytes==38 && r->DestinationReference==encoder.Reference);
          } else if(r->Kind==AppleAgxWin32RelocationPppPipelineOffset32) {
            ++saw_fs; CHECK_PPP(target->Bytes==38 && r->DestinationReference==ppp_scope.Reference);
          } else if(r->Kind==AppleAgxWin32RelocationPppCfBindingsOffset32) {
            ++saw_cf; CHECK_PPP(target->Bytes==cf_bytes && r->DestinationReference==ppp_scope.Reference);
          } else if(r->Kind==AppleAgxWin32RelocationPppStateAddress40) {
            ++saw_ppp; CHECK_PPP(target->Role==AppleAgxWin32RolePppState && target->Bytes==ppp_bytes);
          }
        }
        CHECK_PPP(saw_vs==1 && saw_fs==1 && saw_cf==1 && saw_ppp==1);
      } else CHECK_PPP(c.Capture.State==0 && c.Capture.ReferenceCount==0);
    }
    CHECK_PPP(AgxWin32AsahiCaptureDeactivate(&c));
    if(c.Capture.State) CHECK_PPP(AgxWin32RelocAbort(&c.Capture)==AgxRelocOk);
    CHECK_PPP(!backend->ActiveEmission && !backend->Failed);
  }
  batch->texture_count[MESA_SHADER_VERTEX]=saved_textures[0];
  batch->texture_count[MESA_SHADER_FRAGMENT]=saved_textures[1];
  batch->sampler_count[MESA_SHADER_VERTEX]=saved_samplers[0];
  batch->sampler_count[MESA_SHADER_FRAGMENT]=saved_samplers[1];
  printf("NATIVE_PPP_CAPTURE: errors=%u (native PPP helpers and USC emitter; no draw or GPU execution)\n",errors);
  return errors;
#undef CHECK_PPP
}

/* Executes the original native pipeline function, not a second materializer.
 * Shader bytes/info are controlled input here, not claimed executable AGX. */
unsigned AgxWin32AsahiPipelineTest(AGX_WIN32_SCREEN *screen,
    const AGX_WIN32_ASAHI_OWNER_OPS *ops,void *owner,AGX_WIN32_ASAHI_BACKEND *backend) {
  static struct agx_screen native;
  static struct agx_context ctx;
  static struct agx_batch batch;
  struct agx_compiled_shader cs={0}; struct agx_linked_shader linked={0};
  AGX_WIN32_ASAHI_CAPTURE capture={0}; AGX_WIN32_RELOC_ALLOCATION identity,linked_identity;
  unsigned errors=0;
#define CHECK_PIPE(x) do { if(!(x)) { ++errors; fprintf(stderr,"NATIVE_PIPELINE line=%u %s\n",(unsigned)__LINE__,#x); } } while(0)
  memset(&native,0,sizeof(native)); memset(&ctx,0,sizeof(ctx)); memset(&batch,0,sizeof(batch));
  CHECK_PIPE(AgxWin32AsahiAttach(backend,&native.dev,screen,ops,owner,0x1100000000ULL));
  ctx.base.screen=&native.pscreen; batch.ctx=&ctx;
  cs.stage=cs.b.info.stage=MESA_SHADER_VERTEX;
  cs.b.info.binary_size=576; cs.b.info.main_offset=512; cs.b.info.main_size=64; cs.b.info.nr_gprs=8;
  cs.b.info.has_preamble=true; cs.b.info.preamble_offset=0;
  cs.b.info.rodata=(struct agx_rodata){.offset=256,.base_uniform=8,.size_16=65};
  cs.bo=agx_bo_create(&native.dev,0x4000,0x4000,AGX_BO_LOW_VA|AGX_BO_EXEC,"pipeline shader input");
  CHECK_PIPE(cs.bo);
  if(!cs.bo) return errors+1;
  void *mapped=agx_bo_map(cs.bo); CHECK_PIPE(mapped);
  if(!mapped) return errors+1;
  memset(mapped,0x55,576);
  CHECK_PIPE(AgxWin32AsahiIdentity(backend,cs.bo,&identity));
  /* Native agx_fast_link allocates its own code BO; it does not alias the
   * original part's preamble/rodata allocation. Model that ownership exactly. */
  linked.bo=agx_bo_create(&native.dev,0x4000,0x4000,AGX_BO_LOW_VA|AGX_BO_EXEC,"linked code input");
  CHECK_PIPE(linked.bo);
  if(!linked.bo) return errors+1;
  void *linked_map=agx_bo_map(linked.bo); CHECK_PIPE(linked_map);
  if(!linked_map) return errors+1;
  memset(linked_map,0x33,64);
  CHECK_PIPE(AgxWin32AsahiIdentity(backend,linked.bo,&linked_identity));
  agx_pool_init(&batch.pipeline_pool,&native.dev,"actual native pipeline",AGX_BO_LOW_VA,false);
  agx_pool_init(&batch.pool,&native.dev,"native descriptor table inputs",0,false);
  struct agx_ptr texture=agx_pool_alloc_aligned(&batch.pool,AGX_TEXTURE_LENGTH,64);
  struct agx_ptr sampler=agx_pool_alloc_aligned(&batch.pool,AGX_SAMPLER_LENGTH+AGX_BORDER_LENGTH,64);
  struct agx_ptr uniforms=agx_pool_alloc_aligned(&batch.pool,8,64);
  CHECK_PIPE(texture.cpu && sampler.cpu && uniforms.cpu);
  if(!texture.cpu || !sampler.cpu || !uniforms.cpu) return errors+1;
  memset(texture.cpu,0,AGX_TEXTURE_LENGTH); memset(sampler.cpu,0,AGX_SAMPLER_LENGTH+AGX_BORDER_LENGTH);
  memset(uniforms.cpu,0,8);
  for(unsigned pass=0;pass<2;++pass) {
    if(pass) {
      batch.texture_count[MESA_SHADER_VERTEX]=1;
      batch.stage_uniforms[MESA_SHADER_VERTEX].texture_base=texture.gpu;
      batch.sampler_count[MESA_SHADER_VERTEX]=1; batch.samplers[MESA_SHADER_VERTEX]=sampler.gpu;
      ctx.stage[MESA_SHADER_VERTEX].custom_borders=true;
      cs.push_range_count=1; cs.push[0].table=0; cs.push[0].length=4;
      batch.uniforms.tables[0]=uniforms.gpu;
      agx_pack(&linked.shader,USC_SHADER,cfg) { cfg.code=agx_usc_addr(&native.dev,linked.bo->va->addr); }
      agx_pack(&linked.regs,USC_REGISTERS,cfg) { cfg.register_count=8; }
    }
    CHECK_PIPE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,pass+1)==AgxRelocOk);
    CHECK_PIPE(AgxWin32AsahiCaptureActivate(&capture));
    uint32_t pipeline=AgxWin32NativeBuildPipelineTest(&batch,&cs,pass?&linked:NULL,MESA_SHADER_VERTEX);
    CHECK_PIPE(pipeline!=0);
    CHECK_PIPE(AgxWin32AsahiCaptureDeactivate(&capture));
    unsigned first_shader_edge=pass?3:0;
    CHECK_PIPE(capture.Count==(pass?4u:2u) && capture.Capture.ReferenceCount==(pass?8u:5u) &&
        capture.Capture.RelocationCount==(pass?7u:4u));
    CHECK_PIPE(capture.Capture.References[0].Role==AppleAgxWin32RoleUscPipeline &&
        capture.Capture.References[0].Bytes==(pass?62u:38u));
    if(pass) {
      const unsigned table_kinds[3]={AppleAgxWin32RelocationUscTableAddress39,
          AppleAgxWin32RelocationUscTableAddress39,AppleAgxWin32RelocationUscBufferAddress40};
      const unsigned table_roles[3]={AppleAgxWin32RoleDescriptor,AppleAgxWin32RoleDescriptor,
          AppleAgxWin32RoleConstant};
      const unsigned table_bytes[3]={AGX_TEXTURE_LENGTH,AGX_SAMPLER_LENGTH+AGX_BORDER_LENGTH,8};
      for(unsigned i=0;i<3;++i) {
        const APPLE_AGX_WIN32_RELOCATION *r=&capture.Capture.Relocations[i];
        const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *ref=&capture.Capture.References[r->TargetReference];
        CHECK_PIPE(r->Kind==table_kinds[i] && r->DestinationOffset==i*8 &&
            ref->Role==table_roles[i] && ref->Offset==i*64 && ref->Bytes==table_bytes[i]);
      }
    }
    const unsigned kinds[4]={AppleAgxWin32RelocationUscBufferAddress40,
      AppleAgxWin32RelocationUscBufferAddress40,AppleAgxWin32RelocationUscShaderOffset32,
      AppleAgxWin32RelocationUscPreshaderOffset32};
    const unsigned offsets[4]={0,8,20,30};
    const unsigned target_offsets[4]={256,384,pass?0:512,0};
    for(unsigned i=0;i<4;++i) {
      const APPLE_AGX_WIN32_RELOCATION *r=&capture.Capture.Relocations[first_shader_edge+i];
      const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *ref=&capture.Capture.References[r->TargetReference];
      CHECK_PIPE(r->Kind==kinds[i] && r->DestinationReference==0 && r->DestinationOffset==offsets[i]+first_shader_edge*8);
      const AGX_WIN32_RELOC_ALLOCATION *expected=(pass && i==2)?&linked_identity:&identity;
      CHECK_PIPE(ref->Offset==target_offsets[i] && capture.Capture.Allocations[r->TargetReference].Token==expected->Token &&
          capture.Capture.Allocations[r->TargetReference].Serial==expected->Serial);
      const unsigned expected_bytes[4]={128,2,pass?0x4000:64,256};
      CHECK_PIPE(ref->Bytes==expected_bytes[i]);
    }
    CHECK_PIPE(AgxWin32RelocAbort(&capture.Capture)==AgxRelocOk);
  }
  errors+=TestNativePppCapture(&batch,&cs,backend,&identity);
  /* Linked prolog/epilog scratch need not appear in cs's main/preamble info. */
  agx_pack(&linked.regs,USC_REGISTERS,cfg) { cfg.register_count=8; cfg.spill_size=1; }
  CHECK_PIPE(cs.b.info.scratch_size==0 && cs.b.info.preamble_scratch_size==0);
  CHECK_PIPE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,3)==AgxRelocOk);
  CHECK_PIPE(AgxWin32AsahiCaptureActivate(&capture));
  CHECK_PIPE(AgxWin32NativeBuildPipelineTest(&batch,&cs,&linked,MESA_SHADER_VERTEX)==0);
  CHECK_PIPE(capture.Capture.State==0);
  CHECK_PIPE(AgxWin32AsahiCaptureDeactivate(&capture));
  if(capture.Capture.State) (void)AgxWin32RelocAbort(&capture.Capture);
  /* First untracked push table is rejected by the real producer/capture path. */
  cs.push_range_count=1; cs.push[0].table=0; cs.push[0].length=4;
  batch.uniforms.tables[0]=0x1100ffff00ULL;
  CHECK_PIPE(AgxWin32AsahiCaptureBegin(&capture,backend,identity.Owner,identity.Generation,4)==AgxRelocOk);
  CHECK_PIPE(AgxWin32AsahiCaptureActivate(&capture));
  CHECK_PIPE(AgxWin32NativeBuildPipelineTest(&batch,&cs,NULL,MESA_SHADER_VERTEX)==0);
  CHECK_PIPE(capture.Capture.State==0 && AgxWin32AsahiCaptureDeactivate(&capture));
  agx_pool_cleanup(&batch.pipeline_pool); agx_bo_unreference(&native.dev,cs.bo);
  agx_pool_cleanup(&batch.pool);
  agx_bo_unreference(&native.dev,linked.bo);
  CHECK_PIPE(AgxWin32AsahiDetach(backend));
  printf("ACTUAL_NATIVE_BUILD_PIPELINE: errors=%u (controlled shader input; no GPU execution)\n",errors);
  return errors;
}
