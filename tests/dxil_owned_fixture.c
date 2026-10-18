/* Original actual-C-API ownership regression, not a DDI admission test. */
#include "agx_dxil_owned.h"
#include "dxil_spirv_c.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned rejects;
static dxil_spv_bool reject(void *u,const dxil_spv_uav_d3d_binding *b,dxil_spv_uav_vulkan_binding *v){(void)u;(void)b;(void)v;rejects++;return DXIL_SPV_FALSE;}
static void direct_rejection(const uint8_t *input,size_t bytes){
 dxil_spv_parsed_blob blob=NULL;dxil_spv_converter converter=NULL;
 dxil_spv_begin_thread_allocator_context();
 assert(dxil_spv_parse_dxil_blob(input,bytes,&blob)==DXIL_SPV_SUCCESS);
 // Actual upstream scan SUCCESS does NOT imply callback acceptance.
 assert(dxil_spv_parsed_blob_scan_resources(blob,NULL,NULL,NULL,reject,NULL)==DXIL_SPV_SUCCESS&&rejects==1);
 assert(dxil_spv_create_converter(blob,&converter)==DXIL_SPV_SUCCESS);
 dxil_spv_converter_set_entry_point(converter,"main");dxil_spv_converter_set_uav_remapper(converter,reject,NULL);
 assert(dxil_spv_converter_run(converter)!=DXIL_SPV_SUCCESS&&rejects==2);
 dxil_spv_converter_free(converter);dxil_spv_parsed_blob_free(blob);dxil_spv_reset_thread_allocator_context();dxil_spv_end_thread_allocator_context();
}
static void decoration(const struct agx_dxil_owned_result *out){
 assert(out->spirv_bytes>=20&&!(out->spirv_bytes%4)&&out->spirv[0]==0x07230203);
 uint32_t target=0,sets=0,bindings=0;size_t words=out->spirv_bytes/4;
 for(size_t pos=5;pos<words;){uint32_t op=out->spirv[pos]&0xffff,n=out->spirv[pos]>>16;assert(n&&n<=words-pos);
  if(op==71&&n==4&&out->spirv[pos+2]==34){assert(out->spirv[pos+3]==DXBC_BINDING_UAV);target=out->spirv[pos+1];sets++;}
  pos+=n;
 }
 for(size_t pos=5;pos<words;){uint32_t n=out->spirv[pos]>>16;if((out->spirv[pos]&0xffff)==71&&n==4&&out->spirv[pos+2]==33){assert(out->spirv[pos+1]==target&&out->spirv[pos+3]==0);bindings++;}pos+=n;}
 assert(sets==1&&bindings==1);
}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t bytes[2824],original[2824];assert(fread(bytes,1,sizeof(bytes),f)==sizeof(bytes)&&fgetc(f)==EOF);fclose(f);memcpy(original,bytes,sizeof(bytes));
 struct agx_dxil_owned_request q={0};q.input=bytes;q.input_bytes=sizeof(bytes);q.input_kind=AGX_DXIL_INPUT_CONTAINER;q.entry="main";q.entry_bytes=4;
 q.binding_limits[0]=14;q.binding_limits[1]=128;q.binding_limits[2]=8;q.binding_limits[3]=16;
 struct {uint64_t pre;struct agx_dxil_owned_result value;uint64_t post;} box={0};box.pre=0x1122334455667788ull;box.post=0x8877665544332211ull;
 direct_rejection(bytes,sizeof(bytes));
 for(unsigned pass=0;pass<3;pass++){
  assert(agx_dxil_convert_owned(&q,&box.value)==AGX_DXIL_OWNED_OK);
  assert(box.pre==0x1122334455667788ull&&box.post==0x8877665544332211ull);
  assert(!memcmp(bytes,original,sizeof(bytes)));
  assert(box.value.bindings.active&&box.value.bindings.count==1);
  const struct dxbc_binding *b=&box.value.bindings.bindings[0];assert(b->kind==DXBC_BINDING_UAV&&!b->id&&!b->space&&!b->reg&&!b->slot);
  assert(box.value.workgroup[0]==64&&box.value.workgroup[1]==1&&box.value.workgroup[2]==1&&!strcmp(box.value.entry,"main"));
  decoration(&box.value); // Access after converter/blob/TLS have all been destroyed.
  agx_dxil_owned_free(&box.value);struct agx_dxil_owned_result zero={0};assert(!memcmp(&box.value,&zero,sizeof(zero)));
  q.binding_limits[DXBC_BINDING_UAV]=0;
  assert(agx_dxil_convert_owned(&q,&box.value)==AGX_DXIL_OWNED_UNSUPPORTED&&!memcmp(&box.value,&zero,sizeof(zero)));
  q.binding_limits[DXBC_BINDING_UAV]=8;
 }
 q.input=bytes+1548;q.input_bytes=1276;q.input_kind=AGX_DXIL_INPUT_PROGRAM;assert(agx_dxil_convert_owned(&q,&box.value)==AGX_DXIL_OWNED_OK);decoration(&box.value);agx_dxil_owned_free(&box.value);
 q.entry="missing";q.entry_bytes=7;assert(agx_dxil_convert_owned(&q,&box.value)!=AGX_DXIL_OWNED_OK&&!box.value.spirv);
 q.entry="main";q.entry_bytes=4;q.input_bytes=23;assert(agx_dxil_convert_owned(&q,&box.value)==AGX_DXIL_OWNED_INVALID&&!box.value.spirv);
 assert(!memcmp(bytes,original,sizeof(bytes)));
 puts("C15_OWNED_CONVERTER_PASS one_key=YES raw_and_container=YES scan_false_success_verified=YES reject_then_valid_same_thread=YES output_after_TLS=YES map_sorted_multikey=UNPROVEN internal_OOM=UNPROVEN");
}
