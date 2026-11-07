/* Original actual-owner/C19 DXBC/actual-Asahi coordinate regression. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "nir.h"
#include "nir_builder.h"
#include "asahi/compiler/agx_compile.h"
#include "asahi/lib/agx_tilebuffer.h"
#include "dxbc_to_nir.h"
using UINT=unsigned;using HRESULT=int;using BYTE=unsigned char;
#define S_OK 0
#define E_NOTIMPL (-1)
#include "agx_d3d12_probe_ps_cube.h"
#include "agx_d3d12_probe_ps_cube_array.h"
#include "agx_d3d12_probe_ps_cube_array_second.h"
#include "agx_d3d12_probe_vs_tex.h"
static const uint32_t owner_binding_limits[DXBC_BINDING_KIND_COUNT]={14,128,8,16};
static void receipt(const char *,HRESULT,UINT,UINT,UINT){}
/* ACTUAL_OWNER_CREATE_SHADER */
static unsigned fs_calls,vs_calls,pixel_x,pixel_y;
static void *capture_fs(pipe_context *,const pipe_shader_state *state){assert(state->type==PIPE_SHADER_IR_NIR);fs_calls++;return state->ir.nir;}
static void *capture_vs(pipe_context *,const pipe_shader_state *state){assert(state->type==PIPE_SHADER_IR_NIR);vs_calls++;return state->ir.nir;}
static uint32_t bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
static float floating(uint32_t u){float f;memcpy(&f,&u,4);return f;}
static uint32_t eval(nir_def *def,unsigned component,unsigned depth=0){
 assert(def&&component<def->num_components&&depth<64);nir_instr *ins=nir_def_instr(def);
 if(ins->type==nir_instr_type_load_const)return nir_instr_as_load_const(ins)->value[component].u32;
 if(ins->type==nir_instr_type_intrinsic){auto *i=nir_instr_as_intrinsic(ins);switch(i->intrinsic){case nir_intrinsic_load_pixel_coord:assert(component<2);return component?pixel_y:pixel_x;case nir_intrinsic_load_frag_coord_z:return bits(.25f);case nir_intrinsic_load_frag_coord_w:return bits(.75f);default:fprintf(stderr,"unsupported coordinate intrinsic %s\n",nir_intrinsic_infos[i->intrinsic].name);abort();}}
 assert(ins->type==nir_instr_type_alu);auto *a=nir_instr_as_alu(ins);
 auto input=[&](unsigned index,unsigned c){return eval(a->src[index].src.ssa,a->src[index].swizzle[c],depth+1);};
 switch(a->op){case nir_op_mov:return input(0,component);case nir_op_vec2:case nir_op_vec3:case nir_op_vec4:return input(component,0);case nir_op_u2f32:return bits(static_cast<float>(input(0,component)));case nir_op_fadd:return bits(floating(input(0,component))+floating(input(1,component)));case nir_op_frcp:return bits(1.0f/floating(input(0,component)));default:fprintf(stderr,"unsupported coordinate ALU %s\n",nir_op_infos[a->op].name);abort();}
}
static nir_alu_src *coordinate_source(nir_shader *s,nir_op op,float constant){
 nir_alu_src *result=nullptr;
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(ins,block){if(ins->type!=nir_instr_type_alu)continue;auto *a=nir_instr_as_alu(ins);if(a->op!=op)continue;
  for(unsigned k=0;k<2;k++){nir_def *d=a->src[k].src.ssa;if(nir_def_instr(d)->type==nir_instr_type_load_const&&nir_instr_as_load_const(nir_def_instr(d))->value[a->src[k].swizzle[0]].u32==bits(constant)){assert(!result);result=&a->src[1-k];}}
 }
 assert(result);return result;
}
static nir_shader *translate(pipe_context *pipe,const BYTE *data,size_t bytes,mesa_shader_stage stage,uint8_t dimensions[DXBC_RESOURCE_SLOTS]){
 const void *chunk=nullptr;uint32_t size=0;assert(dxbc_container_find_chunk(data,bytes,DXBC_FOURCC('S','H','E','X'),&chunk,&size)||dxbc_container_find_chunk(data,bytes,DXBC_FOURCC('S','H','D','R'),&chunk,&size));
 auto *aligned=static_cast<UINT *>(malloc(size));assert(aligned);memcpy(aligned,chunk,size);dxbc_binding_map map={};void *shader=create_shader(pipe,aligned,stage,&map,dimensions);free(aligned);assert(shader);return static_cast<nir_shader *>(shader);
}
static void check(pipe_context *pipe,const BYTE *data,size_t bytes,bool array){
 uint8_t dimensions[DXBC_RESOURCE_SLOTS]={};nir_shader *s=translate(pipe,data,bytes,MESA_SHADER_FRAGMENT,dimensions);
 assert(dimensions[0]==(array?DXBC_SRV_CUBE_ARRAY:DXBC_SRV_CUBE));assert(s->info.fs.origin_upper_left&&!s->info.fs.pixel_center_integer&&!s->info.fs.uses_sample_shading);
 nir_alu_src *x=coordinate_source(s,nir_op_fmul,3.0f/64.0f),*y=coordinate_source(s,nir_op_fge,32.0f);
 unsigned center_count=0;nir_def *normalized=nullptr;
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(ins,block){
  if(ins->type==nir_instr_type_intrinsic&&nir_instr_as_intrinsic(ins)->intrinsic==nir_intrinsic_load_sample_pos_or_center)center_count++;
  if(ins->type==nir_instr_type_alu){auto *a=nir_instr_as_alu(ins);if(a->op==nir_op_fadd&&a->def.num_components==4){for(unsigned k=0;k<2;k++){nir_def *d=a->src[k].src.ssa;if(nir_def_instr(d)->type==nir_instr_type_intrinsic&&nir_instr_as_intrinsic(nir_def_instr(d))->intrinsic==nir_intrinsic_load_frag_coord){assert(!normalized);normalized=&a->def;}}}}
 }
 // The real Asahi passes implement the backend's integer coordinate contract.
 NIR_PASS(_,s,agx_nir_lower_sample_intrinsics,true);
 NIR_PASS(_,s,nir_lower_frag_coord_to_pixel_coord);
 nir_validate_shader(s,"actual owner C19 coordinate path");
 const unsigned xs[]={21,0,22,42,43,63},ys[]={0,31,32,63};
 for(unsigned px:xs)for(unsigned py:ys){pixel_x=px;pixel_y=py;float actual_x=floating(eval(x->src.ssa,x->swizzle[0])),actual_y=floating(eval(y->src.ssa,y->swizzle[0]));
  if(actual_x!=px+.5f||actual_y!=py+.5f)fprintf(stderr,"CENTER_MISMATCH pixel=%u,%u got=%g,%g expected=%g,%g center_intrinsics=%u\n",px,py,actual_x,actual_y,px+.5f,py+.5f,center_count);
  assert(actual_x==px+.5f&&actual_y==py+.5f);
  unsigned face=static_cast<unsigned>(actual_x*(3.0f/64.0f))+(actual_y>=32?3u:0u);assert(face==(6*px+3)/128+(py>=32?3u:0u));
 }
 assert(center_count==1&&normalized);pixel_x=21;pixel_y=0;
 assert(floating(eval(normalized,0))==21.5f&&floating(eval(normalized,1))==.5f&&floating(eval(normalized,2))==.25f&&floating(eval(normalized,3))==.75f);
 assert(s->info.fs.origin_upper_left&&!s->info.fs.pixel_center_integer);ralloc_free(s);
}
int main(){glsl_type_singleton_init_or_ref();pipe_screen screen={};for(unsigned i=0;i<MESA_SHADER_STAGES;i++)screen.nir_options[i]=&agx_nir_options;pipe_context pipe={};pipe.screen=&screen;pipe.create_fs_state=capture_fs;pipe.create_vs_state=capture_vs;
 check(&pipe,g_agx_d3d12_probe_ps_cube,sizeof(g_agx_d3d12_probe_ps_cube),false);check(&pipe,g_agx_d3d12_probe_ps_cube_array,sizeof(g_agx_d3d12_probe_ps_cube_array),true);check(&pipe,g_agx_d3d12_probe_ps_cube_array_second,sizeof(g_agx_d3d12_probe_ps_cube_array_second),true);
 uint8_t dims[DXBC_RESOURCE_SLOTS]={};nir_shader *vs=translate(&pipe,g_agx_d3d12_probe_vs_tex,sizeof(g_agx_d3d12_probe_vs_tex),MESA_SHADER_VERTEX,dims);nir_foreach_function_impl(impl,vs)nir_foreach_block(block,impl)nir_foreach_instr(ins,block)if(ins->type==nir_instr_type_intrinsic)assert(nir_instr_as_intrinsic(ins)->intrinsic!=nir_intrinsic_load_sample_pos_or_center);ralloc_free(vs);assert(fs_calls==3&&vs_calls==1);glsl_type_singleton_decref();
 puts("C19_PIXEL_CENTER_PASS actual_owner=YES exact_FXC=3 actual_Asahi_passes=YES coordinate_pairs=72 x21=21.5 ZW_preserved=YES once_only=YES VS_untouched=YES NO_GPU");
}
