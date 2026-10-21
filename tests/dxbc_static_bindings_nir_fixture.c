/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dxbc_to_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "asahi/compiler/agx_compile.h"
static unsigned size_vec4(const struct glsl_type *type, bool bindless) {
   (void)bindless; return glsl_count_attribute_slots(type, false);
}
static void dimensions(void) {
   glsl_type_singleton_init_or_ref();
   nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,&agx_nir_options,"cube dimensions");
   uint8_t dims[DXBC_RESOURCE_SLOTS];
   nir_tex_instr *t[2];
   for(unsigned i=0;i<2;++i) {
      t[i]=nir_tex_instr_create(b.shader,2);
      t[i]->op=nir_texop_txl; t[i]->sampler_dim=GLSL_SAMPLER_DIM_CUBE;
      t[i]->is_array=i!=0; t[i]->texture_index=7+i; t[i]->sampler_index=0;
      t[i]->dest_type=nir_type_float32; t[i]->coord_components=3+i;
      t[i]->src[0]=nir_tex_src_for_ssa(nir_tex_src_coord,
         nir_trim_vector(&b,nir_imm_vec4(&b,1,0,0,0),3+i));
      t[i]->src[1]=nir_tex_src_for_ssa(nir_tex_src_lod,nir_imm_float(&b,0));
      nir_def_init(&t[i]->instr,&t[i]->def,4,32);nir_builder_instr_insert(&b,&t[i]->instr);
   }
   nir_validate_shader(b.shader,"cube dimensions source");
   assert(dxbc_nir_srv_dimensions(b.shader,dims));
   assert(dims[7]==DXBC_SRV_CUBE && dims[8]==DXBC_SRV_CUBE_ARRAY && dims[0]==DXBC_SRV_UNUSED);
   // A single cube-array view remains array even though cube count lives outside NIR.
   t[1]->texture_index=7;
   assert(!dxbc_nir_srv_dimensions(b.shader,dims));
   for(unsigned i=0;i<DXBC_RESOURCE_SLOTS;++i) assert(!dims[i]);
   t[1]->texture_index=DXBC_RESOURCE_SLOTS;
   assert(!dxbc_nir_srv_dimensions(b.shader,dims));
   t[1]->texture_index=8;
   /* C34: a dynamically indexed range declares its base slot and the used
    * slots after it */
   nir_tex_instr_add_src(t[1],nir_tex_src_texture_offset,nir_imm_int(&b,0));
   BITSET_SET(b.shader->info.textures_used,9);BITSET_SET(b.shader->info.textures_used,10);
   assert(dxbc_nir_srv_dimensions(b.shader,dims));
   assert(dims[7]==DXBC_SRV_CUBE && dims[8]==DXBC_SRV_CUBE_ARRAY && dims[9]==DXBC_SRV_CUBE_ARRAY &&
          dims[10]==DXBC_SRV_CUBE_ARRAY && dims[11]==DXBC_SRV_UNUSED);
   ralloc_free(b.shader);glsl_type_singleton_decref();puts("PASS cube dimension metadata");
}
int main(int argc, char **argv) {
   if(argc==2 && !strcmp(argv[1],"--dimensions")) {dimensions();return 0;}
   assert(argc==2 || argc==3); FILE *f=fopen(argv[1],"rb"); assert(f);
   fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
   void *data=malloc(size); assert(fread(data,1,size,f)==(size_t)size); fclose(f);
   const void *chunk; uint32_t bytes;
   assert(dxbc_container_find_chunk(data,size,DXBC_FOURCC('S','H','E','X'),&chunk,&bytes) ||
      dxbc_container_find_chunk(data,size,DXBC_FOURCC('S','H','D','R'),&chunk,&bytes));
   void *saved=malloc(bytes); memcpy(saved,chunk,bytes);
   if(argc==3 && !strcmp(argv[2],"--translate-fail")) {
      struct dxbc_program p; struct dxbc_error pe;
      assert(dxbc_parse_program(chunk,bytes/4,&p,&pe)==DXBC_OK);
      for(unsigned i=0;i<p.num_instructions;i++) if(p.instructions[i].opcode==DXBC_OP_RET) {
         uint32_t *token=(uint32_t *)chunk+p.instructions[i].offset;
         *token=(*token & ~0x7ffu)|DXBC_OP_DEBUG_BREAK;
      }
      dxbc_program_free(&p); memcpy(saved,chunk,bytes);
   }
   struct dxbc_binding_map map; struct dxbc_nir_error e;
   uint32_t limits[4]={14,128,8,16};
   glsl_type_singleton_init_or_ref();
   nir_shader *old=dxbc_translate_tokens(chunk,&agx_nir_options,&e);
   bool pixel_uav=argc==3 && !strcmp(argv[2],"--pixel-uav");
   bool texture=argc==3 && !strcmp(argv[2],"--texture");
   bool cube=argc==3 && (!strcmp(argv[2],"--cube") || !strcmp(argv[2],"--cube-array"));
   bool cube_array=cube && !strcmp(argv[2],"--cube-array");
   if(argc==3 && !strcmp(argv[2],"--ranges")) {
      /* C34: ps51 declares S0[0:1] (space 2) and T0[0:7] (space 1) and samples
       * T0[r0.y] with S0[r0.x], then loads T0[r1.x]: each register of a range
       * gets a consecutive slot and the register becomes texture_offset /
       * sampler_offset of the texture instruction */
      nir_shader *mapped=dxbc_translate_tokens_bindings(chunk,&agx_nir_options,limits,&map,&e);
      if(!mapped) {fprintf(stderr,"REFUSED %s\n",e.message);return 1;}
      assert(map.active && map.count==11);
      for(unsigned r=0;r<8;++r) {
         const struct dxbc_binding *t=dxbc_binding_find(&map,DXBC_BINDING_SRV,1,r);
         assert(t && t->slot==r && !t->flags);
      }
      for(unsigned r=0;r<2;++r) assert(dxbc_binding_find(&map,DXBC_BINDING_SAMPLER,2,r)->slot==r);
      unsigned dynamic_textures=0,dynamic_samplers=0;
      nir_foreach_function_impl(impl,mapped) nir_foreach_block(block,impl) nir_foreach_instr(ins,block) {
         if(ins->type!=nir_instr_type_tex) continue;
         nir_tex_instr *tex=nir_instr_as_tex(ins);
         assert(tex->texture_index==0);
         dynamic_textures+=nir_tex_instr_src_index(tex,nir_tex_src_texture_offset)>=0;
         dynamic_samplers+=nir_tex_instr_src_index(tex,nir_tex_src_sampler_offset)>=0;
      }
      assert(dynamic_textures==2 && dynamic_samplers==1);
      uint8_t dims[DXBC_RESOURCE_SLOTS];
      assert(dxbc_nir_srv_dimensions(mapped,dims));
      for(unsigned i=0;i<8;++i) assert(dims[i]==DXBC_SRV_OTHER);
      assert(dims[8]==DXBC_SRV_UNUSED);
      nir_validate_shader(mapped,"C34 ranges");
      ralloc_free(old);ralloc_free(mapped);glsl_type_singleton_decref();free(saved);free(data);
      puts("PASS C34 SM5.1 ranges");return 0;
   }
   if(argc==3 && !pixel_uav && !texture && !cube) {
      nir_shader *mapped=dxbc_translate_tokens_bindings(chunk,&agx_nir_options,limits,&map,&e);
      if(!strcmp(argv[2],"--refuse") || !strcmp(argv[2],"--translate-fail")) {
         assert(!old && !mapped && !map.active && !map.count);
      } else {
         assert(old && mapped && !map.active && !map.count);
         assert(old->info.stage==mapped->info.stage &&
            old->info.num_ubos==mapped->info.num_ubos &&
            old->info.num_ssbos==mapped->info.num_ssbos &&
            old->info.num_images==mapped->info.num_images &&
            old->info.inputs_read==mapped->info.inputs_read &&
            old->info.outputs_written==mapped->info.outputs_written);
         nir_validate_shader(mapped,"legacy binding identity");
      }
      assert(!memcmp(chunk,saved,bytes));
      ralloc_free(old); ralloc_free(mapped); glsl_type_singleton_decref();
      free(saved); free(data); puts("PASS legacy/refusal"); return 0;
   }
   /* The existing path refuses CBV/texture SM5.1 dimensions.  A raw-UAV-
    * only PS was already accepted there using its ID as an old slot;
    * this D3D12 change does not alter that legacy behavior. */
   if(pixel_uav || cube) assert(old);
   else assert(!old);
   ralloc_free(old);
   nir_shader *nir=dxbc_translate_tokens_bindings(chunk,&agx_nir_options,limits,&map,&e);
   if(!nir) { fprintf(stderr,"REFUSED %s\n",e.message); return 1; }
   if(cube) {
      uint8_t dims[DXBC_RESOURCE_SLOTS];
      assert(!map.active && !map.count && dxbc_nir_srv_dimensions(nir,dims));
      assert(dims[0]==(cube_array?DXBC_SRV_CUBE_ARRAY:DXBC_SRV_CUBE));
      for(unsigned i=1;i<DXBC_RESOURCE_SLOTS;++i) assert(dims[i]==DXBC_SRV_UNUSED);
   } else assert(map.active);
   if(cube) {
      assert(nir->info.stage==MESA_SHADER_FRAGMENT);
   } else if(pixel_uav) {
      const struct dxbc_binding *u=dxbc_binding_find(&map,DXBC_BINDING_UAV,4,1);
      assert(map.count==1 && u && u->id==0 && u->slot==0);
      assert(nir->info.stage==MESA_SHADER_FRAGMENT);
   } else if(texture) {
      const struct dxbc_binding *t=dxbc_binding_find(&map,DXBC_BINDING_SRV,5,0);
      const struct dxbc_binding *s=dxbc_binding_find(&map,DXBC_BINDING_SAMPLER,6,0);
      assert(map.count==2 && t && s && t->id==0 && t->slot==0 && s->id==0 && s->slot==0);
      assert(nir->info.stage==MESA_SHADER_FRAGMENT);
   } else {
      assert(map.count==3);
   }
   unsigned stores=0, cbvs=0, atomics=0, textures=0, sizes=0, levels=0;
   nir_foreach_function_impl(impl,nir) nir_foreach_block(block,impl)
      nir_foreach_instr(ins,block) {
       if(ins->type==nir_instr_type_tex) {
         nir_tex_instr *tex=nir_instr_as_tex(ins);
         assert(tex->texture_index==0 && tex->sampler_index==0);
         if(cube) {
            assert(tex->sampler_dim==GLSL_SAMPLER_DIM_CUBE && tex->is_array==cube_array);
            if(tex->op==nir_texop_txl) assert(tex->coord_components==(cube_array?4:3));
            if(tex->op==nir_texop_txs) {
               assert(tex->def.num_components==(cube_array?3:2));
               int lod=nir_tex_instr_src_index(tex,nir_tex_src_lod);
               assert(lod>=0 && nir_src_is_const(tex->src[lod].src) && nir_src_as_uint(tex->src[lod].src)==0);
               ++sizes;
            }
            if(tex->op==nir_texop_query_levels) {assert(tex->def.num_components==1);++levels;}
         }
         ++textures;
       }
       if(ins->type==nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intr=nir_instr_as_intrinsic(ins);
         if(intr->intrinsic==nir_intrinsic_store_ssbo) {
            assert(nir_src_is_const(intr->src[1]));
            stores |= 1u<<nir_src_as_uint(intr->src[1]);
         }
         if(intr->intrinsic==nir_intrinsic_ssbo_atomic) {
            assert(nir_src_is_const(intr->src[0]) && nir_src_as_uint(intr->src[0])==0);
            ++atomics;
         }
         if(intr->intrinsic==nir_intrinsic_load_ubo) {
            assert(nir_src_is_const(intr->src[0]));
            cbvs |= 1u<<nir_src_as_uint(intr->src[0]);
         }
      }
      }
   if(cube) assert(textures>=3 && sizes==1 && levels==1 && !atomics);
   else if(pixel_uav) assert(atomics==1 && !textures);
   else if(texture) assert(textures==1 && !atomics);
   else assert(stores==3 && cbvs==1);
   nir_validate_shader(nir,"static bindings");
   NIR_PASS(_,nir,nir_lower_io,nir_var_shader_in|nir_var_shader_out,size_vec4,
      nir_lower_io_lower_64bit_to_32|nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(nir,nir_shader_get_entrypoint(nir));
   agx_preprocess_nir(nir); nir_validate_shader(nir,"static bindings Asahi");
   assert(!memcmp(chunk,saved,bytes));
   ralloc_free(nir); glsl_type_singleton_decref(); free(saved); free(data);
   if(cube) puts(cube_array?"PASS cube-array t0 NIR+Asahi":"PASS cube t0 NIR+Asahi");
   else if(pixel_uav) puts("PASS pixel-uav u1/space4->slot0 NIR+Asahi");
   else if(texture) puts("PASS texture t0/space5->slot0 s0/space6->slot0 NIR+Asahi");
   else puts("PASS store-slots=0,1 cbv-slot=0 NIR+Asahi");
   return 0;
}
