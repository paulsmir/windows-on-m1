/* Original calls to actual project frontend/map; no backend/BO or DDI admission. SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include "agx_owned_program_nir_native.h"
#include "agx_legacy_program_frame.h"
#include "agx_dxil_owned.h"
#include "agx_dxil_nir_map.h"
#include "dxbc_to_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "compiler/spirv/nir_spirv.h"
#include "asahi/compiler/agx_compile.h"
static mesa_shader_stage stage(unsigned token_stage){return token_stage==0?MESA_SHADER_FRAGMENT:token_stage==1?MESA_SHADER_VERTEX:MESA_SHADER_COMPUTE;}
void agx_owned_program_nir_native_free(struct agx_owned_program_nir_native *v){if(!v)return;if(v->nir){ralloc_free(v->nir);glsl_type_singleton_decref();}memset(v,0,sizeof(*v));}
enum agx_owned_nir_status agx_owned_program_nir_native_prepare(unsigned kind,unsigned token_stage,const void *program,size_t bytes,const char *entry,size_t entry_bytes,const uint32_t limits[4],struct agx_owned_program_nir_native *out){
 if(!out)return AGX_OWNED_NIR_INVALID;memset(out,0,sizeof(*out));
 if(!program||!limits||(token_stage!=0&&token_stage!=1&&token_stage!=5))return AGX_OWNED_NIR_UNSUPPORTED;
 const uint32_t maxima[4]={14,128,8,16};for(unsigned i=0;i<4;i++)if(limits[i]>maxima[i])return AGX_OWNED_NIR_UNSUPPORTED;
 struct agx_owned_program_nir_native v={0};struct agx_dxil_owned_result converted={0};enum agx_owned_nir_status status=AGX_OWNED_NIR_FRONTEND_ERROR;
 glsl_type_singleton_init_or_ref();
 if(kind==1){
  const struct agx_legacy_frame_profile profile={1u<<token_stage,AGX_LEGACY_SM40|AGX_LEGACY_SM41|AGX_LEGACY_SM50|AGX_LEGACY_SM51};struct agx_legacy_program_frame frame;
  if(agx_legacy_frame_program(program,bytes,&profile,&frame)!=AGX_LEGACY_FRAME_OK||frame.program_bytes!=bytes){status=AGX_OWNED_NIR_INVALID;goto fail;}
  struct dxbc_nir_error error={0};v.nir=dxbc_translate_tokens_bindings(program,&agx_nir_options,limits,&v.map,&error);
  if(!v.nir){status=AGX_OWNED_NIR_UNSUPPORTED;goto fail;}
  if(token_stage==0)dxbc_nir_lower_fragment_position(v.nir);
 }else if(kind==2){
  if(token_stage!=5){status=AGX_OWNED_NIR_UNSUPPORTED;goto fail;}
  const struct agx_dxil_owned_request request={.input=program,.input_bytes=bytes,.input_kind=AGX_DXIL_INPUT_PROGRAM,.entry=entry,.entry_bytes=entry_bytes,.binding_limits={limits[0],limits[1],limits[2],limits[3]}};
  enum agx_dxil_owned_status rc=agx_dxil_convert_owned(&request,&converted);
  if(rc!=AGX_DXIL_OWNED_OK){status=rc==AGX_DXIL_OWNED_UNSUPPORTED?AGX_OWNED_NIR_UNSUPPORTED:rc==AGX_DXIL_OWNED_PROJECT_OOM?AGX_OWNED_NIR_PROJECT_OOM:AGX_OWNED_NIR_CONVERSION_ERROR;goto fail;}
  v.map=converted.bindings;
  struct spirv_to_nir_options options={.environment=NIR_SPIRV_VULKAN,.ubo_addr_format=nir_address_format_32bit_index_offset,.ssbo_addr_format=nir_address_format_32bit_index_offset,.push_const_addr_format=nir_address_format_32bit_offset,.shared_addr_format=nir_address_format_32bit_offset};
  v.nir=spirv_to_nir(converted.spirv,converted.spirv_bytes/4,NULL,MESA_SHADER_COMPUTE,converted.entry,&options,&agx_nir_options);
  if(!v.nir)goto fail;
  NIR_PASS(_,v.nir,nir_lower_variable_initializers,nir_var_function_temp);NIR_PASS(_,v.nir,nir_lower_returns);NIR_PASS(_,v.nir,nir_inline_functions);NIR_PASS(_,v.nir,nir_opt_copy_prop);NIR_PASS(_,v.nir,nir_opt_constant_folding);NIR_PASS(_,v.nir,nir_opt_deref);nir_remove_non_cmat_call_entrypoints(v.nir);NIR_PASS(_,v.nir,nir_lower_variable_initializers,~0);NIR_PASS(_,v.nir,nir_split_var_copies);
  struct agx_dxil_nir_map_stats stats={0};if(agx_dxil_nir_lower_map(v.nir,&v.map,&stats)!=AGX_DXIL_NIR_MAP_OK){status=AGX_OWNED_NIR_UNSUPPORTED;goto fail;}
  NIR_PASS(_,v.nir,nir_lower_explicit_io,nir_var_mem_ubo|nir_var_mem_ssbo,nir_address_format_32bit_index_offset);NIR_PASS(_,v.nir,nir_lower_system_values);
  /* This Asahi owner profile exposes non-base Dispatch only. The target has
   * has_cs_global_id=true: retain global ID, eliminate unsupported base loads.
   * Match Mesa state_tracker normalization before final optimization/info. */
  struct nir_lower_compute_system_values_options cs_options={0};
  NIR_PASS(_,v.nir,nir_lower_compute_system_values,&cs_options);
  NIR_PASS(_,v.nir,nir_lower_vars_to_ssa);NIR_PASS(_,v.nir,nir_opt_copy_prop);NIR_PASS(_,v.nir,nir_opt_constant_folding);NIR_PASS(_,v.nir,nir_opt_dce);
  nir_shader_gather_info(v.nir,nir_shader_get_entrypoint(v.nir));
 }else{status=AGX_OWNED_NIR_INVALID;goto fail;}
 if(v.nir->info.stage!=stage(token_stage)){status=AGX_OWNED_NIR_INVALID;goto fail;}
 nir_validate_shader(v.nir,"owned PROGRAM to owner-facing NIR CPU bridge");
 if(token_stage==5)for(unsigned i=0;i<3;i++)v.workgroup[i]=v.nir->info.workgroup_size[i];
 agx_dxil_owned_free(&converted);*out=v;return AGX_OWNED_NIR_OK;
fail:
 agx_dxil_owned_free(&converted);if(v.nir)ralloc_free(v.nir);glsl_type_singleton_decref();return status;
}
