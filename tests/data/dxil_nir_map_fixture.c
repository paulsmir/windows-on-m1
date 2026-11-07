/* Original EXP1398 CPU representation/namespace gate; no AGX execution. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "nir.h"
#include "nir_builder.h"
#include "compiler/spirv/nir_spirv.h"
#include "asahi/compiler/agx_compile.h"
#include "agx_dxil_nir_map.h"
#include "dxil_nir_semantic_oracle.h"
static const struct dxbc_binding_map actual_map = {1,5,{
 {0,0,3,0,0},{1,0,2,0,0},{1,1,7,0,1},{2,0,1,0,0},{2,1,9,0,1}}};
/* Logical declarations are the frozen mixed-map.json from actual conversion. */
static char *snapshot(nir_shader *s) {
 FILE *f=tmpfile();assert(f);nir_print_shader(s,f);assert(!fseek(f,0,SEEK_END));
 long n=ftell(f);assert(n>=0);rewind(f);char *p=malloc((size_t)n+1);assert(p);
 assert(fread(p,1,(size_t)n,f)==(size_t)n);p[n]=0;fclose(f);return p;
}
static void refused(nir_shader *s,const struct dxbc_binding_map *m,
 enum agx_dxil_nir_map_status expected) {
 char *before=snapshot(s);struct dxbc_binding_map saved=*m;
 struct agx_dxil_nir_map_stats out;memset(&out,0xff,sizeof(out));
 assert(agx_dxil_nir_lower_map(s,m,&out)==expected);
 struct agx_dxil_nir_map_stats zero={0};assert(!memcmp(&out,&zero,sizeof(out)));
 assert(!memcmp(m,&saved,sizeof(saved)));char *after=snapshot(s);
 assert(!strcmp(before,after));free(before);free(after);
}
static nir_intrinsic_instr *find_intrinsic(nir_shader *s,nir_intrinsic_op op) {
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(ins,block)
  if(ins->type==nir_instr_type_intrinsic&&nir_instr_as_intrinsic(ins)->intrinsic==op)
   return nir_instr_as_intrinsic(ins);
 return NULL;
}
static void negatives(nir_shader *s) {
 struct dxbc_binding_map bad=actual_map;bad.bindings[2].slot=0;
 refused(s,&bad,AGX_DXIL_NIR_MAP_INVALID);
 bad=actual_map;bad.count--;refused(s,&bad,AGX_DXIL_NIR_MAP_UNSUPPORTED);
 nir_shader *n=nir_shader_clone(NULL,s);nir_intrinsic_instr *i=find_intrinsic(n,nir_intrinsic_vulkan_resource_index);assert(i);
 nir_intrinsic_set_desc_set(i,1);refused(n,&actual_map,AGX_DXIL_NIR_MAP_UNSUPPORTED);ralloc_free(n);
 n=nir_shader_clone(NULL,s);i=find_intrinsic(n,nir_intrinsic_vulkan_resource_index);assert(i);
 nir_builder b=nir_builder_at(nir_before_instr(&i->instr));nir_src_rewrite(&i->src[0],nir_imm_int(&b,1));
 refused(n,&actual_map,AGX_DXIL_NIR_MAP_UNSUPPORTED);ralloc_free(n);
 n=nir_shader_clone(NULL,s);i=find_intrinsic(n,nir_intrinsic_load_vulkan_descriptor);assert(i);
 nir_descriptor_type original=nir_intrinsic_desc_type(i);
 assert(original==nir_descriptor_type_uniform_buffer||original==nir_descriptor_type_storage_buffer);
 nir_intrinsic_set_desc_type(i,original==nir_descriptor_type_uniform_buffer?
  nir_descriptor_type_storage_buffer:nir_descriptor_type_uniform_buffer);
 refused(n,&actual_map,AGX_DXIL_NIR_MAP_UNSUPPORTED);ralloc_free(n);
 n=nir_shader_clone(NULL,s);bool changed=false;
 nir_foreach_variable_with_modes(v,n,nir_var_uniform)if(!changed){v->data.descriptor_set=2;changed=true;}
 assert(changed);refused(n,&actual_map,AGX_DXIL_NIR_MAP_UNSUPPORTED);ralloc_free(n);
 n=nir_shader_clone(NULL,s);i=find_intrinsic(n,nir_intrinsic_load_vulkan_descriptor);assert(i);
 b=nir_builder_at(nir_before_instr(&i->instr));nir_intrinsic_instr *re=nir_intrinsic_instr_create(n,nir_intrinsic_vulkan_resource_reindex);
 re->src[0]=nir_src_for_ssa(i->src[0].ssa);re->src[1]=nir_src_for_ssa(nir_imm_int(&b,0));
 nir_intrinsic_set_desc_type(re,nir_intrinsic_desc_type(i));nir_def_init(&re->instr,&re->def,2,32);re->num_components=2;
 nir_builder_instr_insert(&b,&re->instr);nir_src_rewrite(&i->src[0],&re->def);
 refused(n,&actual_map,AGX_DXIL_NIR_MAP_UNSUPPORTED);ralloc_free(n);
 puts("C15_MAP_NIR_REFUSALS cases=7 shader_and_map_unchanged=YES stats_empty=YES");
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
int main(int argc,char **argv){assert(argc==5);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>=20&&!(length%4));rewind(f);uint32_t *words=malloc((size_t)length);assert(words&&fread(words,1,(size_t)length,f)==(size_t)length);fclose(f);
 glsl_type_singleton_init_or_ref();struct spirv_to_nir_options options={.environment=NIR_SPIRV_VULKAN,.ubo_addr_format=nir_address_format_32bit_index_offset,.ssbo_addr_format=nir_address_format_32bit_index_offset,.push_const_addr_format=nir_address_format_32bit_offset,.shared_addr_format=nir_address_format_32bit_offset};
 nir_shader *s=spirv_to_nir(words,(size_t)length/4,NULL,MESA_SHADER_COMPUTE,"main",&options,&agx_nir_options);assert(s);nir_validate_shader(s,"mixed C15 spirv input");dump(s,argv[2]);
 NIR_PASS(_,s,nir_lower_variable_initializers,nir_var_function_temp);NIR_PASS(_,s,nir_lower_returns);NIR_PASS(_,s,nir_inline_functions);NIR_PASS(_,s,nir_opt_copy_prop);NIR_PASS(_,s,nir_opt_constant_folding);NIR_PASS(_,s,nir_opt_deref);nir_remove_non_cmat_call_entrypoints(s);NIR_PASS(_,s,nir_lower_variable_initializers,~0);NIR_PASS(_,s,nir_split_var_copies);
 negatives(s);
 struct agx_dxil_nir_map_stats stats;struct dxbc_binding_map saved=actual_map;
 assert(agx_dxil_nir_lower_map(s,&actual_map,&stats)==AGX_DXIL_NIR_MAP_OK);
 assert(!memcmp(&saved,&actual_map,sizeof(saved)));
 assert(stats.ubo_indices&&stats.ssbo_indices&&stats.descriptors&&stats.srv_fetches);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ubo|nir_var_mem_ssbo,nir_address_format_32bit_index_offset);
 NIR_PASS(_,s,nir_lower_system_values);NIR_PASS(_,s,nir_lower_vars_to_ssa);NIR_PASS(_,s,nir_opt_copy_prop);NIR_PASS(_,s,nir_opt_constant_folding);NIR_PASS(_,s,nir_opt_dce);
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));nir_validate_shader(s,"mixed C15 Gallium namespaces");final_contract(s);semantic_oracle(s,argv[4]);dump(s,argv[3]);
 ralloc_free(s);glsl_type_singleton_decref();free(words);
 puts("C15_MIXED_NIR_PASS cbv_ubo_slot=0 srv_txf_slots=0,1 uav_ssbo_slots=0,1 class_before_lowering=YES raw_srv_ssbo_alias=NO MAP_PASS_PROPOSAL OWNER_INTEGRATION_UNPROVEN");
}
