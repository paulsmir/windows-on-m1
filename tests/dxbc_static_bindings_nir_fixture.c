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
int main(int argc, char **argv) {
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
   if(argc==3 && !pixel_uav && !texture) {
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
   if(pixel_uav) assert(old);
   else assert(!old);
   ralloc_free(old);
   nir_shader *nir=dxbc_translate_tokens_bindings(chunk,&agx_nir_options,limits,&map,&e);
   if(!nir) { fprintf(stderr,"REFUSED %s\n",e.message); return 1; }
   assert(map.active);
   if(pixel_uav) {
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
   unsigned stores=0, cbvs=0, atomics=0, textures=0;
   nir_foreach_function_impl(impl,nir) nir_foreach_block(block,impl)
      nir_foreach_instr(ins,block) {
       if(ins->type==nir_instr_type_tex) {
         nir_tex_instr *tex=nir_instr_as_tex(ins);
         assert(tex->texture_index==0 && tex->sampler_index==0);
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
   if(pixel_uav) assert(atomics==1 && !textures);
   else if(texture) assert(textures==1 && !atomics);
   else assert(stores==3 && cbvs==1);
   nir_validate_shader(nir,"static bindings");
   NIR_PASS(_,nir,nir_lower_io,nir_var_shader_in|nir_var_shader_out,size_vec4,
      nir_lower_io_lower_64bit_to_32|nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(nir,nir_shader_get_entrypoint(nir));
   agx_preprocess_nir(nir); nir_validate_shader(nir,"static bindings Asahi");
   assert(!memcmp(chunk,saved,bytes));
   ralloc_free(nir); glsl_type_singleton_decref(); free(saved); free(data);
   if(pixel_uav) puts("PASS pixel-uav u1/space4->slot0 NIR+Asahi");
   else if(texture) puts("PASS texture t0/space5->slot0 s0/space6->slot0 NIR+Asahi");
   else puts("PASS store-slots=0,1 cbv-slot=0 NIR+Asahi");
   return 0;
}
