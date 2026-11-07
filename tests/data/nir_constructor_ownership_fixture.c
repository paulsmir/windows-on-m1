/* Original actual early-constructor ownership regression. SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nir.h"
#include "asahi/compiler/agx_compile.h"
#include "agx_state.h"
#include "util/ralloc.h"
static unsigned checks,failures,allocation_calls,armed,consumed[8];
static void fatal(int okay,const char *message){if(!okay){fprintf(stderr,"HARNESS_BOUNDARY_STOP %s\n",message);exit(90);}}
static void check(int okay,const char *message){++checks;if(!okay){++failures;printf("CHECK_FAIL %s\n",message);}}
static _Noreturn void ownership_forbidden_continuation(void){fprintf(stderr,"FORBIDDEN_CONSTRUCTOR_CONTINUATION\n");exit(91);}
static void *first_shader_allocation(const void *ctx,size_t bytes){
 fatal(armed&&allocation_calls==0&&ctx==NULL&&bytes==sizeof(struct agx_uncompiled_shader),"exactfirstshaderallocation");
 ++allocation_calls;armed=0;return NULL;
}
/* Instrument only the actual selected shader-object allocation callsites. */
#undef rzalloc
#define rzalloc(ctx,type) ((type *)first_shader_allocation((ctx),sizeof(type)))
#include "constructors.inc"
#undef rzalloc
struct lifetime_token{unsigned id;};
static void destroyed(void *ptr){struct lifetime_token *token=ptr;fatal(token->id<8,"destructorid");++consumed[token->id];}
static nir_shader *owned(mesa_shader_stage stage,unsigned id){
 nir_shader *nir=nir_shader_create(NULL,stage,&agx_nir_options);fatal(nir!=NULL,"actualnirallocation");
 struct lifetime_token *token=ralloc_size(nir,sizeof(*token));fatal(token!=NULL,"actualrallocchild");token->id=id;
 fatal(ralloc_parent(token)==nir,"actualparent");ralloc_set_destructor(token,destroyed);return nir;
}
static void observe(nir_shader *nir,unsigned id,const char *label,void *result,unsigned should_consume){
 unsigned failures_at_entry=failures;unsigned before_cleanup=consumed[id];
 check(result==NULL,"actualconstructorNULL");check(allocation_calls==1&&!armed,"oneexactallocationfailure");
 check(before_cleanup==should_consume,"ownershipbeforeharnesscleanup");
 printf("AGX_NIR_CONSUME_CASE %s resultNULL=%u allocations=%u destroyed_before_cleanup=%u expected=%u\n",label,(unsigned)(result==NULL),allocation_calls,before_cleanup,should_consume);
 /* PristineRED is observed first; cleanupavoidsintentionalleak. GREENneveraccessesfreedpointer. */
 if(before_cleanup==0)ralloc_free(nir);
 check(consumed[id]==1,"exactlyonefinaldestructor");
 printf("AGX_D3D12_NIR_CONSUME_%s: %s destroyed_before_cleanup=%u expected=%u\n",label,failures==failures_at_entry?"PASS":"FAIL",before_cleanup,should_consume);
}
int main(void){
 glsl_type_singleton_init_or_ref();
 struct agx_context *context=calloc(1,sizeof(*context));struct agx_screen *screen=calloc(1,sizeof(*screen));fatal(context&&screen,"realcontextscreenstorage");
 context->base.screen=&screen->pscreen;ownership_install(&context->base);
 struct{const char *label;mesa_shader_stage stage;void *(*callback)(struct pipe_context *,const struct pipe_shader_state *);} cases[]={
  {"VS",MESA_SHADER_VERTEX,context->base.create_vs_state},{"FS",MESA_SHADER_FRAGMENT,context->base.create_fs_state},
  {"GS",MESA_SHADER_GEOMETRY,context->base.create_gs_state},{"TCS",MESA_SHADER_TESS_CTRL,context->base.create_tcs_state},
  {"TES",MESA_SHADER_TESS_EVAL,context->base.create_tes_state}};
 for(unsigned i=0;i<5;++i){nir_shader *nir=owned(cases[i].stage,i);struct pipe_shader_state state={0};state.type=PIPE_SHADER_IR_NIR;state.ir.nir=nir;allocation_calls=0;armed=1;
  void *result=cases[i].callback(&context->base,&state);observe(consumed[i] ? NULL : nir,i,cases[i].label,result,1);
 }
 nir_shader *nir=owned(MESA_SHADER_COMPUTE,5);struct pipe_compute_state compute={0};compute.ir_type=PIPE_SHADER_IR_NIR;compute.prog=nir;allocation_calls=0;armed=1;
 void *result=context->base.create_compute_state(&context->base,&compute);observe(consumed[5] ? NULL : nir,5,"CS",result,1);
 /* ReadableownedopaqueIR-tag controls: no TGSI/program parsing or admission is tested. */
 nir=owned(MESA_SHADER_FRAGMENT,6);uint32_t token_storage[4]={0};struct pipe_shader_state borrowed={0};borrowed.type=PIPE_SHADER_IR_TGSI;borrowed.tokens=(const struct tgsi_token *)token_storage;borrowed.ir.nir=nir;allocation_calls=0;armed=1;
 result=context->base.create_fs_state(&context->base,&borrowed);observe(consumed[6] ? NULL : nir,6,"TGSI_BORROWED_TAG",result,0);
 nir=owned(MESA_SHADER_COMPUTE,7);compute.ir_type=PIPE_SHADER_IR_TGSI;compute.prog=nir;allocation_calls=0;armed=1;
 result=context->base.create_compute_state(&context->base,&compute);observe(consumed[7] ? NULL : nir,7,"NON_NIR_COMPUTE_BORROWED_TAG",result,0);
 free(context);free(screen);glsl_type_singleton_decref();
 printf("AGX_NIR_CONSUME_EARLY_NULL %s checks=%u failures=%u NIRstages=6 borrowed_controls=2 actualbranch=YES realralloc=YES successpath=NEVER BO=NEVER\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}
