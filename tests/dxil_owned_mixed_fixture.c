/* Original EXP1398 actual-C-API owned mixed binding checkpoint. */
#include "agx_dxil_owned.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint32_t kinds[5]={DXBC_BINDING_CBV,DXBC_BINDING_SRV,DXBC_BINDING_SRV,DXBC_BINDING_UAV,DXBC_BINDING_UAV};
static const uint32_t spaces[5]={3,2,7,1,9},slots[5]={0,0,1,0,1};
static void inspect(const struct agx_dxil_owned_result *o){
 assert(o->spirv&&o->spirv_bytes>=20&&!(o->spirv_bytes%4)&&o->spirv[0]==0x07230203);
 assert(o->bindings.active&&o->bindings.count==5&&!strcmp(o->entry,"main"));
 assert(o->workgroup[0]==64&&o->workgroup[1]==1&&o->workgroup[2]==1);
 for(unsigned i=0;i<5;i++){const struct dxbc_binding *b=&o->bindings.bindings[i];assert(b->kind==kinds[i]&&b->space==spaces[i]&&!b->reg&&b->slot==slots[i]);}
 // Inspect all descriptor decorations after translator/TLS teardown.
 unsigned set_count=0,binding_count=0;size_t words=o->spirv_bytes/4;uint32_t ids[5]={0};
 for(size_t at=5;at<words;){unsigned n=o->spirv[at]>>16,op=o->spirv[at]&65535;assert(n&&n<=words-at);
  if(op==71&&n==4&&o->spirv[at+2]==34){uint32_t id=o->spirv[at+1],set=o->spirv[at+3],binding=UINT32_MAX;
   for(size_t p=5;p<words;){unsigned nn=o->spirv[p]>>16;assert(nn&&nn<=words-p);if((o->spirv[p]&65535)==71&&nn==4&&o->spirv[p+1]==id&&o->spirv[p+2]==33)binding=o->spirv[p+3];p+=nn;}
   unsigned found=5;for(unsigned i=0;i<5;i++)if(kinds[i]==set&&slots[i]==binding){assert(!ids[i]);found=i;ids[i]=id;break;}assert(found<5);set_count++;
  }
  if(op==71&&n==4&&o->spirv[at+2]==33)binding_count++;
  at+=n;
 }
 assert(set_count==5&&binding_count==5);for(unsigned i=0;i<5;i++)assert(ids[i]);
}
int main(int argc,char **argv){
 assert(argc==4);FILE *f=fopen(argv[1],"rb");assert(f);unsigned char input[3704],before[3704];assert(fread(input,1,sizeof(input),f)==sizeof(input)&&fgetc(f)==EOF);fclose(f);memcpy(before,input,sizeof(input));
 struct agx_dxil_owned_request q={0};q.input=input;q.input_bytes=sizeof(input);q.input_kind=AGX_DXIL_INPUT_CONTAINER;q.entry="main";q.entry_bytes=4;
 q.binding_limits[0]=14;q.binding_limits[1]=128;q.binding_limits[2]=8;q.binding_limits[3]=16;
 struct {uint64_t pre;struct agx_dxil_owned_result out;uint64_t post;} box={0};box.pre=0x1122334455667788ull;box.post=0x8877665544332211ull;
 enum agx_dxil_owned_status status=agx_dxil_convert_owned(&q,&box.out);
 fprintf(stderr,"MIXED_INITIAL_STATUS=%d expected=%d\n",status,AGX_DXIL_OWNED_OK);
 assert(status==AGX_DXIL_OWNED_OK); // Functional RED against existing raw-UAV-only adapter.
 inspect(&box.out);assert(!memcmp(input,before,sizeof(input)));
 assert(box.pre==0x1122334455667788ull&&box.post==0x8877665544332211ull);
 f=fopen(argv[2],"wb");assert(f&&fwrite(box.out.spirv,1,box.out.spirv_bytes,f)==box.out.spirv_bytes);fclose(f);
 f=fopen(argv[3],"w");assert(f);fprintf(f,"{\"bindings\":[");
 for(unsigned i=0;i<5;i++){const struct dxbc_binding *b=&box.out.bindings.bindings[i];fprintf(f,"%s{\"kind\":%u,\"id\":%u,\"space\":%u,\"reg\":%u,\"slot\":%u}",i?",":"",b->kind,b->id,b->space,b->reg,b->slot);}fprintf(f,"]}\n");fclose(f);
 agx_dxil_owned_free(&box.out);struct agx_dxil_owned_result zero={0};assert(!memcmp(&box.out,&zero,sizeof(zero)));
 // Exact mixed declarations must still respect independent owner capacities.
 for(unsigned kind=0;kind<3;kind++){
  uint32_t old=q.binding_limits[kind];q.binding_limits[kind]=kind==DXBC_BINDING_CBV?0:1;
  assert(agx_dxil_convert_owned(&q,&box.out)==AGX_DXIL_OWNED_UNSUPPORTED&&!memcmp(&box.out,&zero,sizeof(zero)));
  q.binding_limits[kind]=old;
  assert(agx_dxil_convert_owned(&q,&box.out)==AGX_DXIL_OWNED_OK);inspect(&box.out);agx_dxil_owned_free(&box.out);
 }
 assert(!memcmp(input,before,sizeof(input)));
 puts("C15_MIXED_OWNED_PASS keys=5 same_reg_different_spaces=YES independent_kind_slots=YES after_TLS=YES refusal_then_valid=YES internal_OOM=UNPROVEN");
}
