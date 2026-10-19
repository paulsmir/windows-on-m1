/* Original actual-NIR inspection, existing semantic oracle unchanged. MIT */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "nir.h"
#include "nir_builder.h"
/* Upstream headers keep their build configuration; project oracles must also
 * evaluate in the native NDEBUG build. */
static void agx_owned_nir_check_failed(const char *expression,unsigned line){
 fprintf(stderr,"OWNED_NIR_CHECK_FAILED line=%u expression=%s\n",line,expression);
 abort();
}
#undef assert
#define assert(expression) do { if(!(expression)) agx_owned_nir_check_failed(#expression,__LINE__); } while(0)
#include "dxil_nir_semantic_oracle.h"
void agx_owned_program_nir_inspect(struct nir_shader *s,unsigned expected_stage,const char *vectors){
 assert(s);assert(s->info.stage==(expected_stage==0?MESA_SHADER_FRAGMENT:expected_stage==1?MESA_SHADER_VERTEX:MESA_SHADER_COMPUTE));
 nir_validate_shader(s,"CPU bridge actual returned NIR");
 if(expected_stage==5){
  unsigned base_global=0,base_workgroup=0;
  nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(ins,block){
   if(ins->type!=nir_instr_type_intrinsic)continue;
   nir_intrinsic_op op=nir_instr_as_intrinsic(ins)->intrinsic;
   base_global+=op==nir_intrinsic_load_base_global_invocation_id;
   base_workgroup+=op==nir_intrinsic_load_base_workgroup_id;
  }
  assert(base_global==0&&base_workgroup==0);
  puts("C15_OWNED_NIR_NORMALIZATION_PASS base_global=0 base_workgroup=0");
 }
 if(vectors)semantic_oracle(s,vectors);
}
