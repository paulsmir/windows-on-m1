/* Original CPU fixture callbacks, NOT Asahi compiler/native BO. MIT */
#include <stdio.h>
#include <stdlib.h>
#include "nir.h"
#include "nir_builder.h"
#include "dxbc_to_nir.h"
#include "agx_owned_program_nir_native.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
struct fixture {struct pipe_context pipe;struct pipe_screen screen;struct nir_shader *original;unsigned calls,deletes,frees,fail,shared,vs_calls,fs_calls,vs_deletes,fs_deletes;};
static void check(int p){if(!p){fputs("OWNER_CALLBACK_FIXTURE_FAIL\n",stderr);abort();}}
static struct fixture *ctx(struct pipe_context *p){return (struct fixture*)p;}
struct observer {struct fixture *f;};
static void dead(void *p){++((struct observer*)p)->f->frees;}
static void *create(struct pipe_context *p,const struct pipe_compute_state *s){
 struct fixture *f=ctx(p);++f->calls;struct nir_shader *n=(struct nir_shader*)s->prog;
 check(s->ir_type==PIPE_SHADER_IR_NIR&&s->static_shared_mem==f->shared&&n&&n!=f->original&&n->info.stage==MESA_SHADER_COMPUTE);
 struct observer *o=rzalloc(n,struct observer);check(o!=NULL);o->f=f;ralloc_set_destructor(o,dead);ralloc_free(n);
 if(f->fail)return NULL;
 unsigned *v=malloc(sizeof(*v));check(v!=NULL);*v=5;return v;
}
static void drop(struct pipe_context *p,void *state){check(state&&*(unsigned*)state==5);++ctx(p)->deletes;free(state);}
/* These available graphics callbacks expose a missing compute-stage guard.
 * They always consume the real clone and report ordinary NULL, never compile
 * or fabricate a graphics success/state. */
static void *graphics(struct pipe_context *p,const struct pipe_shader_state *s,unsigned stage){
 struct fixture *f=ctx(p);if(stage==MESA_SHADER_VERTEX)++f->vs_calls;else ++f->fs_calls;
 struct nir_shader *n=s->ir.nir;check(s->type==PIPE_SHADER_IR_NIR&&n&&n!=f->original&&n->info.stage==stage);
 struct observer *o=rzalloc(n,struct observer);check(o!=NULL);o->f=f;ralloc_set_destructor(o,dead);ralloc_free(n);return NULL;
}
static void *create_vs(struct pipe_context *p,const struct pipe_shader_state *s){return graphics(p,s,MESA_SHADER_VERTEX);}
static void *create_fs(struct pipe_context *p,const struct pipe_shader_state *s){return graphics(p,s,MESA_SHADER_FRAGMENT);}
static void delete_vs(struct pipe_context *p,void *state){++ctx(p)->vs_deletes;check(state!=NULL);free(state);}
static void delete_fs(struct pipe_context *p,void *state){++ctx(p)->fs_deletes;check(state!=NULL);free(state);}
struct pipe_context *owned_owner_callbacks_create(struct nir_shader *original){
 struct fixture *f=calloc(1,sizeof(*f));check(f!=NULL);f->pipe.screen=&f->screen;f->original=original;
 unsigned block[3];dxbc_nir_compute_info(original,block,&f->shared);f->pipe.create_compute_state=create;f->pipe.delete_compute_state=drop;f->pipe.create_vs_state=create_vs;f->pipe.delete_vs_state=delete_vs;f->pipe.create_fs_state=create_fs;f->pipe.delete_fs_state=delete_fs;return &f->pipe;
}
void owned_owner_callbacks_fail(struct pipe_context *p,unsigned fail){ctx(p)->fail=fail;}
unsigned owned_owner_callbacks_calls(struct pipe_context *p){return ctx(p)->calls;}
unsigned owned_owner_callbacks_all_calls(struct pipe_context *p){struct fixture *f=ctx(p);return f->calls+f->vs_calls+f->fs_calls+f->deletes+f->vs_deletes+f->fs_deletes;}
unsigned owned_owner_callbacks_graphics_calls(struct pipe_context *p){struct fixture *f=ctx(p);return f->vs_calls+f->fs_calls+f->vs_deletes+f->fs_deletes;}
unsigned owned_owner_callbacks_deletes(struct pipe_context *p){return ctx(p)->deletes;}
unsigned owned_owner_callbacks_frees(struct pipe_context *p){return ctx(p)->frees;}
void owned_owner_callbacks_finish(struct pipe_context *p){free(ctx(p));}
int owned_owner_dimensions(struct nir_shader *n,uint8_t *srv,uint8_t *uav){return dxbc_nir_srv_dimensions(n,srv)&&dxbc_nir_uav_dimensions(n,uav);}
int owned_owner_query_unknown_control(struct nir_shader *n){
 struct agx_owned_program_nir_native v={0};
 check(agx_owned_program_nir_native_stage(NULL)==AGX_OWNED_NIR_STAGE_UNKNOWN);
 check(agx_owned_program_nir_native_stage(&v)==AGX_OWNED_NIR_STAGE_UNKNOWN);
 v.nir=nir_shader_clone(NULL,n);check(v.nir!=NULL);v.nir->info.stage=MESA_SHADER_GEOMETRY;
 int refused=agx_owned_program_nir_native_stage(&v)==AGX_OWNED_NIR_STAGE_UNKNOWN;ralloc_free(v.nir);return refused;
}
