/* Original restricted interpreter for actual EXP1398 post-NIR only.
 * SPDX-License-Identifier: MIT. No HLSL arithmetic oracle is implemented here. */
#include <string.h>
enum semantic_status { SEMANTIC_OK,SEMANTIC_UNSUPPORTED,SEMANTIC_BOUNDS,SEMANTIC_UNDEFINED,SEMANTIC_OOM };
enum { SEMANTIC_LANES=192 };
struct semantic_inputs {uint32_t cbv[4],srv[2][192],initial[2][192],expected[2][192];};
struct semantic_result {enum semantic_status status;uint32_t words[2][192];unsigned stores[192][2];};
struct semantic_cpu {uint32_t (*value)[4];uint8_t *valid;unsigned ssa_count,gid;const struct semantic_inputs *input;struct semantic_result *result;unsigned srv_words;};
static enum semantic_status semantic_preflight(struct exec_list *list,unsigned depth){
 if(depth>16)return SEMANTIC_UNSUPPORTED;
 foreach_list_typed(nir_cf_node,node,node,list){
  if(node->type==nir_cf_node_if){nir_if *n=nir_cf_node_as_if(node);enum semantic_status a=semantic_preflight(&n->then_list,depth+1);if(a)return a;a=semantic_preflight(&n->else_list,depth+1);if(a)return a;continue;}
  if(node->type!=nir_cf_node_block)return SEMANTIC_UNSUPPORTED;
  nir_block *block=nir_cf_node_as_block(node);nir_foreach_instr(ins,block){
   if(ins->type==nir_instr_type_load_const)continue;
   if(ins->type==nir_instr_type_alu){nir_alu_instr *a=nir_instr_as_alu(ins);switch(a->op){case nir_op_iadd:case nir_op_isub:case nir_op_imul:case nir_op_amul:case nir_op_iand:case nir_op_ishl:case nir_op_ult:break;default:return SEMANTIC_UNSUPPORTED;}if(nir_op_infos[a->op].num_inputs!=2)return SEMANTIC_UNSUPPORTED;continue;}
   if(ins->type==nir_instr_type_tex){nir_tex_instr *t=nir_instr_as_tex(ins);if(t->op!=nir_texop_txf||t->sampler_dim!=GLSL_SAMPLER_DIM_BUF||t->is_array||t->texture_index>=2||t->num_srcs!=1||t->src[0].src_type!=nir_tex_src_coord||t->coord_components!=1)return SEMANTIC_UNSUPPORTED;continue;}
   if(ins->type!=nir_instr_type_intrinsic)return SEMANTIC_UNSUPPORTED;
   nir_intrinsic_instr *i=nir_instr_as_intrinsic(ins);switch(i->intrinsic){case nir_intrinsic_load_global_invocation_id:case nir_intrinsic_load_base_global_invocation_id:case nir_intrinsic_load_ubo:case nir_intrinsic_store_ssbo:break;default:return SEMANTIC_UNSUPPORTED;}
  }
 }
 return SEMANTIC_OK;
}
static enum semantic_status semantic_read(struct semantic_cpu *cpu,nir_def *def,unsigned component,uint32_t *value){
 if(!def||def->index>=cpu->ssa_count||component>=def->num_components||component>=4||!(cpu->valid[def->index]&(1u<<component)))return SEMANTIC_UNDEFINED;
 *value=cpu->value[def->index][component];return SEMANTIC_OK;
}
static enum semantic_status semantic_write(struct semantic_cpu *cpu,nir_def *def,unsigned component,uint32_t value){
 if(!def||def->index>=cpu->ssa_count||component>=def->num_components||component>=4)return SEMANTIC_UNSUPPORTED;
 cpu->value[def->index][component]=value;cpu->valid[def->index]|=(uint8_t)(1u<<component);return SEMANTIC_OK;
}
#define SEMANTIC_TRY(call) do {enum semantic_status status_=(call);if(status_)return status_;} while(0)
static enum semantic_status semantic_eval(struct exec_list *list,struct semantic_cpu *cpu,unsigned depth){
 if(depth>16)return SEMANTIC_UNSUPPORTED;
 foreach_list_typed(nir_cf_node,node,node,list){
  if(node->type==nir_cf_node_if){nir_if *n=nir_cf_node_as_if(node);uint32_t condition=0;if(n->condition.ssa->bit_size!=1)return SEMANTIC_UNSUPPORTED;SEMANTIC_TRY(semantic_read(cpu,n->condition.ssa,0,&condition));SEMANTIC_TRY(semantic_eval(condition?&n->then_list:&n->else_list,cpu,depth+1));continue;}
  if(node->type!=nir_cf_node_block)return SEMANTIC_UNSUPPORTED;
  nir_block *block=nir_cf_node_as_block(node);nir_foreach_instr(ins,block){
   if(ins->type==nir_instr_type_load_const){nir_load_const_instr *c=nir_instr_as_load_const(ins);if(c->def.num_components>4||(c->def.bit_size!=32&&c->def.bit_size!=1))return SEMANTIC_UNSUPPORTED;for(unsigned k=0;k<c->def.num_components;k++)SEMANTIC_TRY(semantic_write(cpu,&c->def,k,c->value[k].u32));continue;}
   if(ins->type==nir_instr_type_alu){nir_alu_instr *a=nir_instr_as_alu(ins);if(a->def.num_components>4||a->def.bit_size!=(a->op==nir_op_ult?1:32))return SEMANTIC_UNSUPPORTED;
    for(unsigned k=0;k<a->def.num_components;k++){uint32_t x=0,y=0,z=0;if(a->src[0].src.ssa->bit_size!=32||a->src[1].src.ssa->bit_size!=32)return SEMANTIC_UNSUPPORTED;SEMANTIC_TRY(semantic_read(cpu,a->src[0].src.ssa,a->src[0].swizzle[k],&x));SEMANTIC_TRY(semantic_read(cpu,a->src[1].src.ssa,a->src[1].swizzle[k],&y));
     switch(a->op){case nir_op_iadd:z=(uint32_t)((uint64_t)x+y);break;case nir_op_isub:z=x-y;break;case nir_op_imul:z=(uint32_t)((uint64_t)x*y);break;case nir_op_amul:if((uint64_t)x*y>UINT32_MAX)return SEMANTIC_UNDEFINED;z=x*y;break;case nir_op_iand:z=x&y;break;case nir_op_ishl:z=(uint32_t)((uint64_t)x<<(y&31));break;case nir_op_ult:z=x<y;break;default:return SEMANTIC_UNSUPPORTED;}
     SEMANTIC_TRY(semantic_write(cpu,&a->def,k,z));
    }continue;
   }
   if(ins->type==nir_instr_type_tex){nir_tex_instr *t=nir_instr_as_tex(ins);uint32_t coord=0;if(t->def.num_components!=4||t->def.bit_size!=32||t->texture_index>=2)return SEMANTIC_UNSUPPORTED;SEMANTIC_TRY(semantic_read(cpu,t->src[0].src.ssa,0,&coord));if(coord>=cpu->srv_words||coord>=192)return SEMANTIC_BOUNDS;
    // Exact IR consumes only .x; other texture-result components remain undefined.
    SEMANTIC_TRY(semantic_write(cpu,&t->def,0,cpu->input->srv[t->texture_index][coord]));continue;
   }
   if(ins->type!=nir_instr_type_intrinsic)return SEMANTIC_UNSUPPORTED;nir_intrinsic_instr *i=nir_instr_as_intrinsic(ins);
   if(i->intrinsic==nir_intrinsic_load_global_invocation_id||i->intrinsic==nir_intrinsic_load_base_global_invocation_id){if(i->def.num_components!=3||i->def.bit_size!=32)return SEMANTIC_UNSUPPORTED;for(unsigned k=0;k<3;k++)SEMANTIC_TRY(semantic_write(cpu,&i->def,k,(!k&&i->intrinsic==nir_intrinsic_load_global_invocation_id)?cpu->gid:0));continue;}
   if(i->intrinsic==nir_intrinsic_load_ubo){uint32_t slot=0,offset=0;SEMANTIC_TRY(semantic_read(cpu,i->src[0].ssa,0,&slot));SEMANTIC_TRY(semantic_read(cpu,i->src[1].ssa,0,&offset));if(i->def.bit_size!=32||i->def.num_components!=4)return SEMANTIC_UNSUPPORTED;if(slot||offset%4||offset>16||16>16-offset)return SEMANTIC_BOUNDS;for(unsigned k=0;k<4;k++)SEMANTIC_TRY(semantic_write(cpu,&i->def,k,cpu->input->cbv[offset/4+k]));continue;}
   if(i->intrinsic==nir_intrinsic_store_ssbo){uint32_t slot=0,offset=0,value=0;if(nir_intrinsic_write_mask(i)!=1||nir_intrinsic_offset_shift(i)||i->src[0].ssa->num_components!=1||i->src[0].ssa->bit_size!=32)return SEMANTIC_UNSUPPORTED;SEMANTIC_TRY(semantic_read(cpu,i->src[0].ssa,0,&value));SEMANTIC_TRY(semantic_read(cpu,i->src[1].ssa,0,&slot));SEMANTIC_TRY(semantic_read(cpu,i->src[2].ssa,0,&offset));if(slot>=2||offset%4||offset>768||4>768-offset)return SEMANTIC_BOUNDS;cpu->result->words[slot][offset/4]=value;cpu->result->stores[cpu->gid][slot]++;continue;}
   return SEMANTIC_UNSUPPORTED;
  }
 }
 return SEMANTIC_OK;
}
#undef SEMANTIC_TRY
static struct semantic_result semantic_execute(nir_shader *s,const struct semantic_inputs *input,unsigned srv_words){
 struct semantic_result result={0};nir_function_impl *impl=nir_shader_get_entrypoint(s);result.status=semantic_preflight(&impl->body,0);if(result.status)return result;
 if(!impl->ssa_alloc||impl->ssa_alloc>4096){result.status=SEMANTIC_UNSUPPORTED;return result;}
 memcpy(result.words,input->initial,sizeof(result.words));
 for(unsigned gid=0;gid<192;gid++){struct semantic_cpu cpu={0};cpu.ssa_count=impl->ssa_alloc;cpu.gid=gid;cpu.input=input;cpu.result=&result;cpu.srv_words=srv_words;cpu.value=calloc(cpu.ssa_count,sizeof(*cpu.value));cpu.valid=calloc(cpu.ssa_count,sizeof(*cpu.valid));
  if(!cpu.value||!cpu.valid)result.status=SEMANTIC_OOM;else result.status=semantic_eval(&impl->body,&cpu,0);free(cpu.value);free(cpu.valid);
  if(result.status){enum semantic_status status=result.status;memset(&result,0,sizeof(result));result.status=status;return result;}
 }
 return result;
}
static void semantic_read_file(const char *dir,const char *name,void *out,size_t bytes){char path[1024];assert(snprintf(path,sizeof(path),"%s/%s",dir,name)>0);FILE *f=fopen(path,"rb");assert(f&&fread(out,1,bytes,f)==bytes&&fgetc(f)==EOF);fclose(f);}
static unsigned semantic_mismatches(const struct semantic_result *r,const struct semantic_inputs *in){unsigned mismatches=0;for(unsigned slot=0;slot<2;slot++)for(unsigned i=0;i<192;i++)mismatches+=r->words[slot][i]!=in->expected[slot][i];return mismatches;}
static void semantic_oracle(nir_shader *s,const char *vectors){
 struct semantic_inputs input;semantic_read_file(vectors,"cbv_b0_space3.bin",input.cbv,sizeof(input.cbv));
 const char *srv[]={"srv_t0_space2.bin","srv_t0_space7.bin"},*initial[]={"initial_u0_space1.bin","initial_u0_space9.bin"},*expected[]={"expected_u0_space1.bin","expected_u0_space9.bin"};
 for(unsigned i=0;i<2;i++){semantic_read_file(vectors,srv[i],input.srv[i],sizeof(input.srv[i]));semantic_read_file(vectors,initial[i],input.initial[i],sizeof(input.initial[i]));semantic_read_file(vectors,expected[i],input.expected[i],sizeof(input.expected[i]));}
 assert(input.cbv[0]==173&&input.cbv[1]==0x13579bdf);
 struct semantic_result good=semantic_execute(s,&input,192);assert(good.status==SEMANTIC_OK&&!semantic_mismatches(&good,&input));
 unsigned active=0,tail=0;for(unsigned gid=0;gid<192;gid++){assert(good.stores[gid][0]==(gid<173)&&good.stores[gid][1]==(gid<173));if(gid<173)active++;else {tail++;assert(good.words[0][gid]==input.initial[0][gid]&&good.words[1][gid]==input.initial[1][gid]);}}
 struct semantic_inputs altered=input;memcpy(altered.srv[0],input.srv[1],sizeof(input.srv[0]));memcpy(altered.srv[1],input.srv[0],sizeof(input.srv[1]));struct semantic_result swapped=semantic_execute(s,&altered,192);unsigned swap_bad=semantic_mismatches(&swapped,&input);assert(swapped.status==SEMANTIC_OK&&swap_bad);
 altered=input;altered.cbv[1]=0;struct semantic_result seedless=semantic_execute(s,&altered,192);unsigned seed_bad=semantic_mismatches(&seedless,&input);assert(seedless.status==SEMANTIC_OK&&seed_bad);
 nir_shader *wrong=nir_shader_clone(NULL,s);nir_intrinsic_instr *first=NULL,*second=NULL;nir_foreach_function_impl(impl,wrong)nir_foreach_block(block,impl)nir_foreach_instr(ins,block)if(ins->type==nir_instr_type_intrinsic){nir_intrinsic_instr *i=nir_instr_as_intrinsic(ins);if(i->intrinsic==nir_intrinsic_store_ssbo){if(!first)first=i;else second=i;}}
 assert(first&&second);nir_src_rewrite(&second->src[1],first->src[1].ssa);nir_validate_shader(wrong,"semantic negative wrong UAV slot");struct semantic_result wrong_slot=semantic_execute(wrong,&input,192);unsigned slot_bad=semantic_mismatches(&wrong_slot,&input);assert(wrong_slot.status==SEMANTIC_OK&&slot_bad);ralloc_free(wrong);
 wrong=nir_shader_clone(NULL,s);bool changed=false;nir_foreach_function_impl(impl,wrong)nir_foreach_block(block,impl)nir_foreach_instr(ins,block)if(!changed&&ins->type==nir_instr_type_alu){nir_alu_instr *a=nir_instr_as_alu(ins);if(a->op==nir_op_imul){a->op=nir_op_ixor;changed=true;}}
 assert(changed);nir_validate_shader(wrong,"semantic negative unsupported opcode");struct semantic_result unsupported=semantic_execute(wrong,&input,192);assert(unsupported.status==SEMANTIC_UNSUPPORTED);ralloc_free(wrong);
 struct semantic_result bounds=semantic_execute(s,&input,172);assert(bounds.status==SEMANTIC_BOUNDS);struct semantic_result empty;memset(&empty,0,sizeof(empty));empty.status=SEMANTIC_BOUNDS;assert(!memcmp(&bounds,&empty,sizeof(bounds)));
 FILE *f=fopen("actual_u0_space1.bin","wb");assert(f&&fwrite(good.words[0],1,sizeof(good.words[0]),f)==sizeof(good.words[0]));fclose(f);f=fopen("actual_u0_space9.bin","wb");assert(f&&fwrite(good.words[1],1,sizeof(good.words[1]),f)==sizeof(good.words[1]));fclose(f);
 printf("C15_NIR_SEMANTIC_PASS lanes=192 active=%u tail=%u compared_words=384 stores=346 uint32_wrap=YES amul_overflow=CHECKED_UNDEFINED srv_swap_bad=%u seed_loss_bad=%u wrong_slot_bad=%u unsupported=REFUSED bounds=REFUSED ACTUAL_NIR_ONLY NO_GPU\n",active,tail,swap_bad,seed_bad,slot_bad);
}
