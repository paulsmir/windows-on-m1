#include "gallium/drivers/asahi/agx_state.h"
#include "agx_win32_asahi_pipeline.h"
#include <stdio.h>
#include <string.h>

uint32_t AgxWin32NativeBuildPipelineTest(struct agx_batch *,struct agx_compiled_shader *,
    struct agx_linked_shader *,mesa_shader_stage);

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
