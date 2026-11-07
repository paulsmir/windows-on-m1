/* SPDX-License-Identifier: MIT
 * Catch namespace aliasing, SM5.1 dimensionality and refusal atomicity. */
#include "dxbc_parse.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Parsed-IR contracts for all declaration shapes and a resource reference
 * used as a CB vector's relative offset.  Such operands live in the owned
 * relative pool rather than the instruction's inline operands. */
static void all_kinds(void) {
   const unsigned ops[]={DXBC_OP_DCL_CONSTANT_BUFFER,DXBC_OP_DCL_RESOURCE,
      DXBC_OP_DCL_RESOURCE_RAW,DXBC_OP_DCL_RESOURCE_STRUCTURED,
      DXBC_OP_DCL_UAV_TYPED,DXBC_OP_DCL_UAV_RAW,DXBC_OP_DCL_UAV_STRUCTURED,
      DXBC_OP_DCL_SAMPLER};
   const unsigned types[]={8,7,7,7,30,30,30,6};
   const unsigned kinds[]={0,1,1,1,2,2,2,3};
   uint32_t payloads[8][2],limits[]={14,128,8,16};
   struct dxbc_instruction ins[9]={0};
   struct dxbc_operand relative={0};
   struct dxbc_program p={0}; struct dxbc_error e; struct dxbc_binding_map map;
   p.major=5; p.minor=1; p.num_instructions=9; p.instructions=ins;
   p.relative=&relative; p.num_relative=1;
   for(unsigned i=0;i<8;i++) {
      ins[i].opcode=ops[i]; ins[i].num_operands=1;
      ins[i].operands[0].type=types[i]; ins[i].operands[0].index_dimension=3;
      ins[i].operands[0].index[0].imm=71+i;
      ins[i].operands[0].index[1].imm=ins[i].operands[0].index[2].imm=200+i;
      ins[i].num_payload=(i==0 || i==1 || i==3 || i==4 || i==6)?2:1;
      payloads[i][0]=16; payloads[i][ins[i].num_payload-1]=1000+i;
      ins[i].payload=payloads[i];
   }
   relative=ins[7].operands[0]; relative.index_dimension=2;
   relative.index[1].imm=207; memset(&relative.index[2],0,sizeof(relative.index[2]));
   ins[8].opcode=DXBC_OP_MOV; ins[8].num_operands=1;
   ins[8].operands[0]=ins[0].operands[0];
   ins[8].operands[0].index[2].representation=DXBC_INDEX_IMM32_PLUS_RELATIVE;
   ins[8].operands[0].index[2].imm=7; ins[8].operands[0].index[2].relative=&relative;
   assert(dxbc_prepare_static_bindings(&p,limits,&map,&e)==DXBC_OK);
   assert(map.count==8 && map.active);
   for(unsigned i=0;i<8;i++) {
      const struct dxbc_binding *b=dxbc_binding_find(&map,kinds[i],1000+i,200+i);
      assert(b && b->id==71+i);
      assert(ins[i].operands[0].index[0].imm==b->slot);
      if(i==1 || i==3 || i==4 || i==6) assert(ins[i].num_payload==1 && ins[i].payload[0]==16);
   }
   assert(ins[8].operands[0].index_dimension==2);
   assert(ins[8].operands[0].index[1].imm==7);
   assert(ins[8].operands[0].index[1].relative==&relative);
   assert(relative.index_dimension==1 && relative.index[0].imm==0);
}
int main(int argc, char **argv) {
   assert(argc == 3);
   all_kinds();
   FILE *f=fopen(argv[1],"rb"); assert(f);
   fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
   void *data=malloc(size); assert(fread(data,1,size,f)==(size_t)size); fclose(f);
   const void *chunk; uint32_t bytes;
   assert(dxbc_container_find_chunk(data,size,DXBC_FOURCC('S','H','E','X'),&chunk,&bytes));
   void *saved=malloc(bytes); memcpy(saved,chunk,bytes);
   struct dxbc_program p; struct dxbc_error e; struct dxbc_binding_map map;
   uint32_t limits[4]={14,128,8,16};
   assert(dxbc_parse_program(chunk,bytes/4,&p,&e)==DXBC_OK);
   int mode=atoi(argv[2]);
   struct dxbc_instruction *cb=NULL,*u0=NULL,*u1=NULL,*store=NULL;
   for(unsigned i=0;i<p.num_instructions;i++) {
      struct dxbc_instruction *in=&p.instructions[i];
      if(in->opcode==DXBC_OP_DCL_CONSTANT_BUFFER) cb=in;
      if(in->opcode==DXBC_OP_DCL_UAV_RAW) { if(!u0) u0=in; else u1=in; }
      if(in->opcode==DXBC_OP_STORE_RAW) store=in;
   }
   assert(cb && u0 && u1 && store);
   if(mode==1) u1->operands[0].index[0].imm=u0->operands[0].index[0].imm; /* ID collision */
   if(mode==2) u1->payload=u0->payload; /* semantic collision */
   if(mode==3) u1->operands[0].index[2].imm=1; /* array */
   if(mode==4) store->operands[0].index[0].imm=999; /* missing ID */
   if(mode==5) store->operands[0].index[1].representation=DXBC_INDEX_RELATIVE;
   if(mode==6) limits[2]=1;
   if(mode==7) cb->num_payload=1;
   if(mode==8) store->operands[0].index_dimension=1;
   if(mode==9) u1->operands[0].index[1].imm=u1->operands[0].index[2].imm=UINT32_MAX;
   if(mode==10) store->operands[0].index[1].imm=7;
   if(mode==11) p.minor=0;
   struct dxbc_instruction *before=malloc(p.num_instructions*sizeof(*before));
   memcpy(before,p.instructions,p.num_instructions*sizeof(*before));
   memset(&map,0xff,sizeof(map));
   enum dxbc_status status=dxbc_prepare_static_bindings(&p,limits,&map,&e);
   if(mode && mode!=11) {
      assert(status!=DXBC_OK); assert(!map.active && !map.count);
      assert(!memcmp(before,p.instructions,p.num_instructions*sizeof(*before)));
   } else if(mode==11) {
      assert(status==DXBC_OK && !map.active && !map.count);
      assert(!memcmp(before,p.instructions,p.num_instructions*sizeof(*before)));
   } else {
      assert(status==DXBC_OK && map.active && map.count==3);
      const struct dxbc_binding *a=dxbc_binding_find(&map,DXBC_BINDING_UAV,1,0);
      const struct dxbc_binding *b=dxbc_binding_find(&map,DXBC_BINDING_UAV,2,0);
      assert(a && b && a->slot==0 && b->slot==1);
      assert(dxbc_binding_find(&map,DXBC_BINDING_CBV,3,0)->slot==0);
      assert(cb->operands[0].index_dimension==2 && cb->operands[0].index[1].imm==1);
      assert(u0->operands[0].index_dimension==1 && !u0->num_payload);
      assert(store->operands[0].index_dimension==1 && store->operands[0].index[0].imm==1);
   }
   assert(!memcmp(chunk,saved,bytes));
   free(before); free(saved); dxbc_program_free(&p); free(data); puts("PASS"); return 0;
}
