/* Original UNLINKED actual-NIR clone/consume caller. SPDX-License-Identifier: MIT */
#include <string.h>
#include "agx_owned_nir_pipe.h"
#include "dxbc_to_nir.h"
#include "nir.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
static bool empty(const struct agx_owned_nir_pipe_state *v){
 if(v->state||v->token_stage||v->shared_bytes||v->bindings.active||v->bindings.count)return false;
 for(unsigned i=0;i<3;i++)if(v->block[i])return false;
 for(unsigned i=0;i<DXBC_BINDING_MAP_MAX;i++){
  const struct dxbc_binding *b=&v->bindings.bindings[i];
  if(b->kind||b->id||b->space||b->reg||b->slot)return false;
 }
 for(unsigned i=0;i<DXBC_RESOURCE_SLOTS;i++)if(v->srv_dimensions[i])return false;
 for(unsigned i=0;i<DXBC_UAV_DIMENSION_SLOTS;i++)if(v->uav_dimensions[i])return false;
 return true;
}
enum agx_owned_nir_pipe_status agx_owned_nir_create_pipe_state(
 const struct agx_owned_program_nir_native *source,struct pipe_context *pipe,
 struct agx_owned_nir_pipe_state *out){
 if(!out||!empty(out))return AGX_OWNED_NIR_PIPE_INVALID;
 if(!source||!source->nir||!pipe)return AGX_OWNED_NIR_PIPE_INVALID;
 struct agx_owned_nir_pipe_state local={0};
 const mesa_shader_stage stage=source->nir->info.stage;
 switch(stage){
 case MESA_SHADER_COMPUTE:
  if(!pipe->create_compute_state||!pipe->delete_compute_state)return AGX_OWNED_NIR_PIPE_INVALID;
  local.token_stage=5;
  dxbc_nir_compute_info(source->nir,local.block,&local.shared_bytes);
  for(unsigned i=0;i<3;i++)
   if(!local.block[i]||local.block[i]!=source->workgroup[i])return AGX_OWNED_NIR_PIPE_UNSUPPORTED;
  break;
 case MESA_SHADER_VERTEX:
  if(!pipe->create_vs_state||!pipe->delete_vs_state)return AGX_OWNED_NIR_PIPE_INVALID;
  local.token_stage=1;break;
 case MESA_SHADER_FRAGMENT:
  if(!pipe->create_fs_state||!pipe->delete_fs_state)return AGX_OWNED_NIR_PIPE_INVALID;
  local.token_stage=0;break;
 default:return AGX_OWNED_NIR_PIPE_UNSUPPORTED;
 }
 if(!dxbc_nir_srv_dimensions(source->nir,local.srv_dimensions)||
    !dxbc_nir_uav_dimensions(source->nir,local.uav_dimensions))return AGX_OWNED_NIR_PIPE_UNSUPPORTED;
 local.bindings=source->map;
 struct nir_shader *clone=nir_shader_clone(NULL,source->nir);
 if(!clone)return AGX_OWNED_NIR_PIPE_CLONE_FAILED;
 /* From invocation onwards the callback owns clone, including NULL result. */
 if(stage==MESA_SHADER_COMPUTE){
  struct pipe_compute_state state={0};state.ir_type=PIPE_SHADER_IR_NIR;
  state.prog=clone;state.static_shared_mem=local.shared_bytes;
  local.state=pipe->create_compute_state(pipe,&state);
 }else{
  struct pipe_shader_state state={0};state.type=PIPE_SHADER_IR_NIR;state.ir.nir=clone;
  local.state=stage==MESA_SHADER_VERTEX?pipe->create_vs_state(pipe,&state):pipe->create_fs_state(pipe,&state);
 }
 if(!local.state)return AGX_OWNED_NIR_PIPE_CREATE_FAILED;
 *out=local;return AGX_OWNED_NIR_PIPE_OK;
}
void agx_owned_nir_delete_pipe_state(struct pipe_context *pipe,struct agx_owned_nir_pipe_state *v){
 if(!v||!v->state)return;
 /* Matching live context/callbacks are caller preconditions, not inferred
  * from an arbitrary stale pointer or driver return value. */
 if(v->token_stage==5)pipe->delete_compute_state(pipe,v->state);
 else if(v->token_stage==1)pipe->delete_vs_state(pipe,v->state);
 else pipe->delete_fs_state(pipe,v->state);
 memset(v,0,sizeof(*v));
}
