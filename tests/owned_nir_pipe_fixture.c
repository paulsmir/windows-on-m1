/* Original real-NIR caller regression; callbacks below are CPU fixtures,
 * NOT an Asahi shader compiler/BO/success implementation. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agx_owned_nir_pipe.h"
#include "dxbc_to_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_serialize.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
static unsigned checks,calls,deletes,clone_frees,original_frees,fail_callback,seen_stage;
static struct nir_shader *original;
static unsigned expected_shared;
static struct blob fs_expected;
static struct agx_owned_program_nir_native retained_vs;
static unsigned retained_refs;
static void need(int p,const char *why){++checks;if(!p){fprintf(stderr,"NIR_PIPE_FAIL %s\n",why);abort();}}
static void child_dead(void *p){(void)p;++clone_frees;}
static void original_child_dead(void *p){(void)p;++original_frees;}
struct fixture_state {unsigned stage;};
static void *consume(struct nir_shader *nir,unsigned stage){
 ++calls;need(nir&&nir!=original,"actual clone distinct from borrowed original");
 if(stage==0){struct blob observed;blob_init(&observed);nir_serialize(&observed,nir,false);
  need(!observed.out_of_memory&&observed.size==fs_expected.size&&!memcmp(observed.data,fs_expected.data,observed.size),"FS clone exact structural serialization, no repeated position lowering");blob_finish(&observed);}

 need(nir->info.stage==original->info.stage,"clone stage preserved");seen_stage=stage;
 void *child=ralloc_size(nir,1);need(child!=NULL,"clone observer child");ralloc_set_destructor(child,child_dead);
 ralloc_free(nir);
 if(fail_callback)return NULL;
 struct fixture_state *s=malloc(sizeof(*s));need(s!=NULL,"ordinary fixture state allocation");s->stage=stage;return s;
}
static void *cs(struct pipe_context *p,const struct pipe_compute_state *s){
 (void)p;need(s->ir_type==PIPE_SHADER_IR_NIR&&s->static_shared_mem==expected_shared,"actual CS record");return consume((struct nir_shader*)s->prog,5);
}
static void *vs(struct pipe_context *p,const struct pipe_shader_state *s){(void)p;need(s->type==PIPE_SHADER_IR_NIR,"actual VS record");return consume(s->ir.nir,1);}
static void *fs(struct pipe_context *p,const struct pipe_shader_state *s){(void)p;need(s->type==PIPE_SHADER_IR_NIR,"actual FS record");return consume(s->ir.nir,0);}
static void drop(struct pipe_context *p,void *v){(void)p;need(v!=NULL,"delete actual fixture state");++deletes;free(v);}
static void delete_cs(struct pipe_context *p,void *v){need(((struct fixture_state*)v)->stage==5,"CS delete callback matches stage");drop(p,v);}
static void delete_vs(struct pipe_context *p,void *v){need(((struct fixture_state*)v)->stage==1,"VS delete callback matches stage");drop(p,v);}
static void delete_fs(struct pipe_context *p,void *v){need(((struct fixture_state*)v)->stage==0,"FS delete callback matches stage");drop(p,v);}
static int zero(const struct agx_owned_nir_pipe_state *v){
 if(v->state||v->token_stage||v->shared_bytes||v->bindings.active||v->bindings.count)return 0;
 for(unsigned i=0;i<3;i++)if(v->block[i])return 0;
 for(unsigned i=0;i<DXBC_BINDING_MAP_MAX;i++){
  const struct dxbc_binding *b=&v->bindings.bindings[i];if(b->kind||b->id||b->space||b->reg||b->slot)return 0;
 }
 for(unsigned i=0;i<DXBC_RESOURCE_SLOTS;i++)if(v->srv_dimensions[i])return 0;
 for(unsigned i=0;i<DXBC_UAV_DIMENSION_SLOTS;i++)if(v->uav_dimensions[i])return 0;
 return 1;
}
static int maps_equal(const struct dxbc_binding_map *a,const struct dxbc_binding_map *b){
 if(a->active!=b->active||a->count!=b->count)return 0;
 for(unsigned i=0;i<DXBC_BINDING_MAP_MAX;i++){
  const struct dxbc_binding *x=&a->bindings[i],*y=&b->bindings[i];
  if(x->kind!=y->kind||x->id!=y->id||x->space!=y->space||x->reg!=y->reg||x->slot!=y->slot)return 0;
 }return 1;
}
extern void agx_owned_program_nir_inspect(struct nir_shader *,unsigned,const char *);
void agx_owned_nir_pipe_fixture(struct nir_shader *nir,const struct dxbc_binding_map *map,
 const uint32_t workgroup[3],unsigned token_stage,const char *vectors){
 original=nir;unsigned original_before=original_frees;
 if(token_stage==0){blob_init(&fs_expected);nir_serialize(&fs_expected,nir,false);need(!fs_expected.out_of_memory&&fs_expected.size,"FS original structural snapshot");}
 void *original_child=ralloc_size(nir,1);need(original_child!=NULL,"borrowed original observer child");ralloc_set_destructor(original_child,original_child_dead);
 struct agx_owned_program_nir_native source={0};source.nir=nir;source.map=*map;
 for(unsigned i=0;i<3;i++)source.workgroup[i]=workgroup[i];
 struct pipe_context p={0};p.create_compute_state=cs;p.create_vs_state=vs;p.create_fs_state=fs;
 p.delete_compute_state=delete_cs;p.delete_vs_state=delete_vs;p.delete_fs_state=delete_fs;
 struct agx_owned_nir_pipe_state out={0};unsigned block[3];dxbc_nir_compute_info(nir,block,&expected_shared);
 agx_owned_program_nir_inspect(nir,token_stage,vectors);
 unsigned before=calls,free_before=clone_frees,delete_before=deletes;
 fail_callback=0;
 need(agx_owned_nir_create_pipe_state(&source,&p,&out)==AGX_OWNED_NIR_PIPE_OK,"nominal callback success");
 need(out.state&&calls==before+1&&clone_frees==free_before+1,"callback consumed sole actual clone");
 need(seen_stage==token_stage&&out.token_stage==token_stage,"matching callback/stage publication");
 need(maps_equal(&out.bindings,map),"copied binding metadata publication");
 uint8_t srv[DXBC_RESOURCE_SLOTS],uav[DXBC_UAV_DIMENSION_SLOTS];
 need(dxbc_nir_srv_dimensions(nir,srv)&&dxbc_nir_uav_dimensions(nir,uav),"actual metadata oracle");
 need(!memcmp(srv,out.srv_dimensions,sizeof(srv))&&!memcmp(uav,out.uav_dimensions,sizeof(uav)),"dimension snapshot publication");
 if(token_stage==5)for(unsigned i=0;i<3;i++)need(out.block[i]==workgroup[i],"block snapshot");
 void *live=out.state;struct dxbc_binding_map saved=out.bindings;
 need(agx_owned_nir_create_pipe_state(&source,&p,&out)==AGX_OWNED_NIR_PIPE_INVALID,"nonempty live result refuses");
 need(out.state==live&&maps_equal(&out.bindings,&saved)&&calls==before+1,"nonempty live result unchanged");
 need(out.token_stage==token_stage&&!memcmp(srv,out.srv_dimensions,sizeof(srv))&&!memcmp(uav,out.uav_dimensions,sizeof(uav)),"nonempty metadata unchanged");
 if(token_stage==5){need(out.shared_bytes==expected_shared,"nonempty shared metadata unchanged");for(unsigned i=0;i<3;i++)need(out.block[i]==workgroup[i],"nonempty block metadata unchanged");}
 agx_owned_nir_delete_pipe_state(&p,&out);need(zero(&out)&&deletes==delete_before+1,"matching delete clears result");
 agx_owned_nir_delete_pipe_state(&p,&out);need(deletes==delete_before+1,"empty delete idempotent");
 fail_callback=1;
 need(agx_owned_nir_create_pipe_state(&source,&p,&out)==AGX_OWNED_NIR_PIPE_CREATE_FAILED,"ordinary callback-reported failure");
 need(zero(&out)&&calls==before+2&&clone_frees==free_before+2,"failure consumes clone, publishes nothing");
 fail_callback=0;
 need(agx_owned_nir_create_pipe_state(&source,&p,&out)==AGX_OWNED_NIR_PIPE_OK,"valid subsequent ordinary call");agx_owned_nir_delete_pipe_state(&p,&out);
 need(original_frees==original_before&&maps_equal(&source.map,map),"original retained alive unchanged");
 agx_owned_program_nir_inspect(nir,token_stage,vectors);
 struct pipe_context missing=p;missing.delete_compute_state=NULL;missing.delete_vs_state=NULL;missing.delete_fs_state=NULL;before=calls;
 need(agx_owned_nir_create_pipe_state(&source,&missing,&out)==AGX_OWNED_NIR_PIPE_INVALID&&zero(&out)&&calls==before,"missing delete refuses before callback");
 need(agx_owned_nir_create_pipe_state(NULL,&p,&out)==AGX_OWNED_NIR_PIPE_INVALID&&zero(&out),"empty invalid source allzero");
 if(token_stage==5){source.workgroup[0]++;before=calls;need(agx_owned_nir_create_pipe_state(&source,&p,&out)==AGX_OWNED_NIR_PIPE_UNSUPPORTED&&zero(&out)&&calls==before,"metadata mismatch refuses before callback");source.workgroup[0]--;}
 struct nir_shader *unsupported=nir_shader_clone(NULL,nir);need(unsupported!=NULL,"ordinary test stage clone");unsupported->info.stage=MESA_SHADER_GEOMETRY;source.nir=unsupported;before=calls;
 need(agx_owned_nir_create_pipe_state(&source,&p,&out)==AGX_OWNED_NIR_PIPE_UNSUPPORTED&&zero(&out)&&calls==before,"unsupported stage refuses before callback");ralloc_free(unsupported);
 source.nir=nir;
 if(token_stage==1){
  need(!retained_vs.nir,"fixture VS snapshot initially empty");
  need(retained_refs==0,"retained GLSL reference initially empty");glsl_type_singleton_init_or_ref();++retained_refs;retained_vs=source;retained_vs.nir=nir_shader_clone(NULL,nir);
  need(retained_vs.nir!=NULL,"fixture retained VS clone for pair composition");
 }
 if(token_stage==0){
  need(retained_vs.nir!=NULL,"fixture has actual VS for rollback composition");
  struct agx_owned_nir_pipe_state a={0},b={0};unsigned d=deletes;
  original=retained_vs.nir;fail_callback=0;
  need(agx_owned_nir_create_pipe_state(&retained_vs,&p,&a)==AGX_OWNED_NIR_PIPE_OK,"fixture caller first stage succeeds");
  original=nir;source.nir=nir;fail_callback=1;
  need(agx_owned_nir_create_pipe_state(&source,&p,&b)==AGX_OWNED_NIR_PIPE_CREATE_FAILED&&zero(&b),"fixture caller second stage ordinary refusal");
  agx_owned_nir_delete_pipe_state(&p,&a);need(zero(&a)&&deletes==d+1,"fixture caller rollback deletes VS exactly once");
  agx_owned_nir_delete_pipe_state(&p,&a);need(deletes==d+1,"fixture caller rollback empty second delete");
  fail_callback=0;ralloc_free(retained_vs.nir);memset(&retained_vs,0,sizeof(retained_vs));glsl_type_singleton_decref();need(retained_refs==1,"retained GLSL reference balanced at release");--retained_refs;
  puts("C15_NIR_PIPE_FIXTURE_PAIR_ROLLBACK_PASS NO_PRODUCTION_GRAPHICS_CLAIM");
 }
 ralloc_free(original_child);need(original_frees==original_before+1,"original observer explicit cleanup");
 if(token_stage==0){struct blob after;blob_init(&after);nir_serialize(&after,nir,false);need(!after.out_of_memory&&after.size==fs_expected.size&&!memcmp(after.data,fs_expected.data,after.size),"FS original structure unchanged after helper and callbacks");blob_finish(&after);blob_finish(&fs_expected);puts("C15_NIR_PIPE_FS_SERIALIZATION_PASS ORIGINAL_AND_TRANSFERRED_CLONES_UNCHANGED");}
 printf("C15_NIR_PIPE_CALLBACK_PASS token_stage=%u checks=%u REAL_NIR_CLONE_FIXTURE_CALLBACK_NO_ASAHI_BO\n",token_stage,checks);
}

void agx_owned_nir_pipe_fixture_finish(void){
 need(!retained_vs.nir&&!retained_refs,"fixture-end retained clone and GLSL reference balanced");
 printf("C15_NIR_PIPE_CALLBACK_FINAL_PASS checks=%u retained_refs=0 NO_ASAHI_BO\n",checks);
}
