/* Original EXP1398 CPU representation/namespace gate; no AGX execution. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "nir.h"
#include "nir_builder.h"
#include "compiler/spirv/nir_spirv.h"
#include "asahi/compiler/agx_compile.h"
static unsigned ubo_indices,ssbo_indices,descriptors,srv_derefs;
static bool lower_buffer_descriptor(nir_builder *b,nir_intrinsic_instr *i,void *unused){
 (void)unused;nir_def *value=NULL;b->cursor=nir_before_instr(&i->instr);
 if(i->intrinsic==nir_intrinsic_vulkan_resource_index){
  unsigned set=nir_intrinsic_desc_set(i),slot=nir_intrinsic_binding(i);nir_descriptor_type type=nir_intrinsic_desc_type(i);
  assert(nir_src_is_const(i->src[0])&&nir_src_as_uint(i->src[0])==0&&i->def.num_components==2&&i->def.bit_size==32);
  if(type==nir_descriptor_type_uniform_buffer){assert(set==0&&slot==0);ubo_indices|=1u<<slot;}
  else{assert(type==nir_descriptor_type_storage_buffer&&set==2&&slot<2);ssbo_indices|=1u<<slot;}
  value=nir_imm_ivec2(b,slot,0);
 }else if(i->intrinsic==nir_intrinsic_load_vulkan_descriptor){
  nir_descriptor_type type=nir_intrinsic_desc_type(i);assert(type==nir_descriptor_type_uniform_buffer||type==nir_descriptor_type_storage_buffer);
  assert(i->def.num_components==2&&i->def.bit_size==32);value=i->src[0].ssa;descriptors++;
 }else if(i->intrinsic==nir_intrinsic_vulkan_resource_reindex){assert(!"unexpected dynamic/reindexed resource");}
 if(!value)return false;nir_def_rewrite_uses(&i->def,value);nir_instr_remove(&i->instr);return true;
}
static void check_srv_derefs(nir_shader *s){
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(ins,block){
  if(ins->type!=nir_instr_type_tex)continue;nir_tex_instr *tex=nir_instr_as_tex(ins);
  assert(tex->op==nir_texop_txf&&tex->sampler_dim==GLSL_SAMPLER_DIM_BUF&&!tex->is_array);
  nir_deref_instr *deref=nir_get_tex_deref(tex,nir_tex_src_texture_deref);assert(deref);
  nir_variable *var=nir_deref_instr_get_variable(deref);assert(var&&!glsl_type_is_array(var->type));
  assert((glsl_type_is_texture(var->type)||glsl_type_is_sampler(var->type))&&glsl_get_sampler_dim(var->type)==GLSL_SAMPLER_DIM_BUF&&glsl_get_sampler_result_type(var->type)==GLSL_TYPE_UINT);
  assert(var->data.descriptor_set==1&&var->data.binding<2);srv_derefs|=1u<<var->data.binding;
  assert(nir_tex_instr_src_index(tex,nir_tex_src_sampler_deref)<0);
 }
 assert(srv_derefs==3);
}
static void final_contract(nir_shader *s){
 unsigned srv=0,uav=0,cbv=0;
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(ins,block){
  if(ins->type==nir_instr_type_tex){nir_tex_instr *tex=nir_instr_as_tex(ins);assert(tex->op==nir_texop_txf&&tex->sampler_dim==GLSL_SAMPLER_DIM_BUF&&tex->texture_index<2);assert(nir_tex_instr_src_index(tex,nir_tex_src_texture_deref)<0);srv|=1u<<tex->texture_index;continue;}
  if(ins->type!=nir_instr_type_intrinsic)continue;nir_intrinsic_instr *i=nir_instr_as_intrinsic(ins);
  assert(i->intrinsic!=nir_intrinsic_vulkan_resource_index&&i->intrinsic!=nir_intrinsic_load_vulkan_descriptor&&i->intrinsic!=nir_intrinsic_vulkan_resource_reindex);
  assert(i->intrinsic!=nir_intrinsic_load_ssbo); // Raw SRVs must remain TEXEL_BUFFER.
  if(i->intrinsic==nir_intrinsic_load_ubo){assert(nir_src_is_const(i->src[0])&&nir_src_as_uint(i->src[0])==0);cbv=1;}
  if(i->intrinsic==nir_intrinsic_store_ssbo){assert(nir_src_is_const(i->src[1])&&nir_src_as_uint(i->src[1])<2);uav|=1u<<nir_src_as_uint(i->src[1]);}
 }
 assert(cbv==1&&srv==3&&uav==3);
 assert(s->info.workgroup_size[0]==64&&s->info.workgroup_size[1]==1&&s->info.workgroup_size[2]==1);
}
static void dump(nir_shader *s,const char *path){FILE *f=fopen(path,"w");assert(f);nir_print_shader(s,f);fclose(f);}
int main(int argc,char **argv){assert(argc==4);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>=20&&!(length%4));rewind(f);uint32_t *words=malloc((size_t)length);assert(words&&fread(words,1,(size_t)length,f)==(size_t)length);fclose(f);
 glsl_type_singleton_init_or_ref();struct spirv_to_nir_options options={.environment=NIR_SPIRV_VULKAN,.ubo_addr_format=nir_address_format_32bit_index_offset,.ssbo_addr_format=nir_address_format_32bit_index_offset,.push_const_addr_format=nir_address_format_32bit_offset,.shared_addr_format=nir_address_format_32bit_offset};
 nir_shader *s=spirv_to_nir(words,(size_t)length/4,NULL,MESA_SHADER_COMPUTE,"main",&options,&agx_nir_options);assert(s);nir_validate_shader(s,"mixed C15 spirv input");dump(s,argv[2]);
 NIR_PASS(_,s,nir_lower_variable_initializers,nir_var_function_temp);NIR_PASS(_,s,nir_lower_returns);NIR_PASS(_,s,nir_inline_functions);NIR_PASS(_,s,nir_opt_copy_prop);NIR_PASS(_,s,nir_opt_constant_folding);NIR_PASS(_,s,nir_opt_deref);nir_remove_non_cmat_call_entrypoints(s);NIR_PASS(_,s,nir_lower_variable_initializers,~0);NIR_PASS(_,s,nir_split_var_copies);
 check_srv_derefs(s); // nir_lower_samplers discards set identity, so audit it first.
 NIR_PASS(_,s,nir_shader_intrinsics_pass,lower_buffer_descriptor,nir_metadata_control_flow,NULL);assert(ubo_indices==1&&ssbo_indices==3&&descriptors>=3);
 NIR_PASS(_,s,nir_lower_samplers);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ubo|nir_var_mem_ssbo,nir_address_format_32bit_index_offset);
 NIR_PASS(_,s,nir_lower_system_values);NIR_PASS(_,s,nir_lower_vars_to_ssa);NIR_PASS(_,s,nir_opt_copy_prop);NIR_PASS(_,s,nir_opt_constant_folding);NIR_PASS(_,s,nir_opt_dce);
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));nir_validate_shader(s,"mixed C15 Gallium namespaces");final_contract(s);dump(s,argv[3]);
 ralloc_free(s);glsl_type_singleton_decref();free(words);
 puts("C15_MIXED_NIR_PASS cbv_ubo_slot=0 srv_txf_slots=0,1 uav_ssbo_slots=0,1 class_before_lowering=YES raw_srv_ssbo_alias=NO SEMANTIC_EXECUTION_UNPROVEN OWNER_INTEGRATION_UNPROVEN");
}
