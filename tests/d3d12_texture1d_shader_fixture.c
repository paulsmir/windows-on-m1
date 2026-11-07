/* SPDX-License-Identifier: MIT
 * Execute the hardware probe's FXC shaders through the actual DXBC/NIR path.
 * IR assertions precede the linked Asahi preprocessing, so dimension metadata
 * and texture/image coordinates are checked independently of lowering effects.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dxbc_to_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "asahi/compiler/agx_compile.h"
static unsigned type_size(const struct glsl_type *t,bool bindless) {(void)bindless;return glsl_count_attribute_slots(t,false);}
int main(int argc,char **argv) {
 assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 void *data=malloc(size);assert(fread(data,1,size,f)==(size_t)size);fclose(f);
 const void *chunk;uint32_t bytes;assert(dxbc_container_find_chunk(data,size,DXBC_FOURCC('S','H','E','X'),&chunk,&bytes));
 void *saved=malloc(bytes);memcpy(saved,chunk,bytes);
 struct dxbc_binding_map map;struct dxbc_nir_error e;uint32_t limits[4]={14,128,8,16};uint8_t dims[DXBC_RESOURCE_SLOTS];
 glsl_type_singleton_init_or_ref();nir_shader *nir=dxbc_translate_tokens_bindings(chunk,&agx_nir_options,limits,&map,&e);
 if(!nir) {printf("REFUSED opcode=%u offset=%u message=%s\n",e.opcode,e.offset,e.message);return 1;}
 assert(!map.active && !map.count);assert(dxbc_nir_srv_dimensions(nir,dims));
 bool store=!strcmp(argv[2],"C"),pixel=!strcmp(argv[2],"E");
 assert(nir->info.stage == (pixel ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE));
 unsigned loads=0,sizes=0,levels=0,samples=0,image_stores=0,image_loads=0,ubo0=0,raw_stores=0;
 uint8_t udims[DXBC_UAV_DIMENSION_SLOTS];assert(dxbc_nir_uav_dimensions(nir,udims));
 for(unsigned u=0;u<DXBC_UAV_DIMENSION_SLOTS;u++)assert(udims[u]==(u==0?(store?DXBC_UAV_1D:DXBC_UAV_OTHER):DXBC_UAV_UNUSED));
 for(unsigned i=0;i<DXBC_RESOURCE_SLOTS;i++) assert(dims[i]==(!store && i==0?DXBC_SRV_1D:DXBC_SRV_UNUSED));
 nir_foreach_function_impl(impl,nir) nir_foreach_block(block,impl) nir_foreach_instr(ins,block) {
  if(ins->type==nir_instr_type_tex) {
   nir_tex_instr *t=nir_instr_as_tex(ins);assert(!store && t->sampler_dim==GLSL_SAMPLER_DIM_1D && !t->is_array && t->texture_index==0);
   if(t->op==nir_texop_txf) {assert(t->coord_components==1 && t->dest_type==nir_type_uint32);++loads;}
   else if(t->op==nir_texop_txs) {assert(t->def.num_components==1 && t->dest_type==nir_type_uint32);++sizes;}
   else if(t->op==nir_texop_query_levels) {assert(t->def.num_components==1);++levels;}
   else if(t->op==nir_texop_txl) {assert(pixel && t->coord_components==1 && t->dest_type==nir_type_float32 && t->sampler_index==0);++samples;}
   else assert(!"unexpected volume texture instruction");
  } else if(ins->type==nir_instr_type_intrinsic) {
   nir_intrinsic_instr *in=nir_instr_as_intrinsic(ins);
   if(in->intrinsic==nir_intrinsic_image_load) {
    assert(store && nir_intrinsic_image_dim(in)==GLSL_SAMPLER_DIM_1D && !nir_intrinsic_image_array(in));
    assert(nir_src_is_const(in->src[0]) && nir_src_as_uint(in->src[0])==0);++image_loads;
   }
   if(in->intrinsic==nir_intrinsic_image_store) {
    assert(store && nir_intrinsic_image_dim(in)==GLSL_SAMPLER_DIM_1D && !nir_intrinsic_image_array(in));
    assert(nir_src_is_const(in->src[0]) && nir_src_as_uint(in->src[0])==0);++image_stores;
   }
   if(in->intrinsic==nir_intrinsic_load_ubo) {assert(nir_src_is_const(in->src[0]) && nir_src_as_uint(in->src[0])==0);++ubo0;}
   if(in->intrinsic==nir_intrinsic_store_ssbo) {assert(!store && !pixel && nir_src_is_const(in->src[1]) && nir_src_as_uint(in->src[1])==0);++raw_stores;}
  }
 }
 if(store) assert(image_stores==1 && image_loads==1 && ubo0 && !loads && !sizes && !samples);
 else if(pixel) assert(samples==1 && sizes==1 && levels==1 && !loads);
 else assert(loads==1 && sizes==1 && levels==1 && raw_stores==2);
 if(!pixel) assert(nir->info.workgroup_size[0]==64 && nir->info.workgroup_size[1]==1 && nir->info.workgroup_size[2]==1);
 nir_validate_shader(nir,"C23 Texture1D finalization");
 NIR_PASS(_,nir,nir_lower_io,nir_var_shader_in|nir_var_shader_out,type_size,nir_lower_io_lower_64bit_to_32|nir_lower_io_use_interpolated_input_intrinsics);
 nir_shader_gather_info(nir,nir_shader_get_entrypoint(nir));agx_preprocess_nir(nir);nir_validate_shader(nir,"C23 AGX1D preprocessing");
 assert(!memcmp(saved,chunk,bytes));
 printf("PASS C23%s map=inactive metadata1D=%u txf=%u txs1=%u levels1=%u txl=%u image_store1D=%u ubo0=%u raw_stores=%u NIR+Asahi\n",argv[2],!store,loads,sizes,levels,samples,image_stores,ubo0,raw_stores);
 ralloc_free(nir);glsl_type_singleton_decref();free(saved);free(data);
}
