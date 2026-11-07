/* Original framing cases, no upstream parser implementation. */
#include "agx_dxil_program.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const struct agx_dxil_profile compute60={1u<<5,0x60,0x60};
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(8*i);return v;}
static unsigned cases;
static struct agx_dxil_program check(const void *p,size_t n,enum agx_dxil_input_kind k,
 const struct agx_dxil_profile *profile,enum agx_dxil_parse_status expected){
 struct {uint64_t pre;struct agx_dxil_program output;uint64_t post;} box;
 memset(&box,0xa5,sizeof(box));
 struct agx_dxil_program *out=&box.output;
 uint8_t *before=malloc(n?n:1);assert(before);if(p&&n)memcpy(before,p,n);
 enum agx_dxil_parse_status status=agx_dxil_parse(p,n,k,profile,out);
 assert(box.pre==UINT64_C(0xa5a5a5a5a5a5a5a5)&&box.post==UINT64_C(0xa5a5a5a5a5a5a5a5));
 if(status!=expected){fprintf(stderr,"case%u status%d expected%d bytes%zu kind%d\n",cases,status,expected,n,k);abort();}
 if(p&&n)assert(!memcmp(before,p,n));free(before);
 if(status!=AGX_DXIL_PARSE_OK){struct agx_dxil_program zero;memset(&zero,0,sizeof(zero));assert(!memcmp(out,&zero,sizeof(*out)));}
 cases++;return *out;
}
static void program(uint8_t *p,size_t n){memset(p,0,n);put32(p,0x50060);put32(p+4,(uint32_t)(n/4));memcpy(p+8,"DXIL",4);put32(p+12,0x100);put32(p+16,16);put32(p+20,(uint32_t)n-24);}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t actual[2824];assert(fread(actual,1,sizeof(actual),f)==sizeof(actual));assert(fgetc(f)==EOF);fclose(f);
 struct agx_dxil_program c=check(actual,sizeof(actual),AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_OK);
 assert(c.container_bytes==2824&&c.program_offset==1548&&c.program_bytes==1276&&c.bitcode_offset==1572&&c.bitcode_bytes==1252);
 assert(c.shader_kind==5&&c.shader_major==6&&!c.shader_minor&&c.program_version==0x50060&&c.dxil_version==0x100);
 struct agx_dxil_program p=check(actual+1548,1276,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_OK);
 assert(!p.container_bytes&&!p.program_offset&&p.program_bytes==1276&&p.bitcode_offset==24&&p.bitcode_bytes==1252);
 for(size_t n=0;n<sizeof(actual);n++)check(actual,n,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_INVALID);
 for(size_t n=0;n<1276;n++)check(actual+1548,n,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_INVALID);
 uint8_t unaligned[2826];memcpy(unaligned+1,actual,sizeof(actual));check(unaligned+1,sizeof(actual),AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_OK);
 uint8_t trailing[2832];memcpy(trailing,actual,sizeof(actual));memset(trailing+2824,0xa5,8);check(trailing,sizeof(trailing),AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_OK);
 check(actual,2824,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_INVALID);
 check(actual+1548,1276,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_INVALID);
 check(NULL,0,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_INVALID);
 check(actual,2824,(enum agx_dxil_input_kind)99,&compute60,AGX_DXIL_PARSE_INVALID);
 check(actual,2824,AGX_DXIL_INPUT_CONTAINER,NULL,AGX_DXIL_PARSE_INVALID);
 assert(agx_dxil_parse(actual,2824,AGX_DXIL_INPUT_CONTAINER,&compute60,NULL)==AGX_DXIL_PARSE_INVALID);
 struct agx_dxil_profile profile=compute60;profile.shader_kind_mask=1;check(actual,2824,AGX_DXIL_INPUT_CONTAINER,&profile,AGX_DXIL_PARSE_UNSUPPORTED);
 profile=compute60;profile.minimum_shader_model=profile.maximum_shader_model=0x61;check(actual,2824,AGX_DXIL_INPUT_CONTAINER,&profile,AGX_DXIL_PARSE_UNSUPPORTED);
 profile.minimum_shader_model=0x100;check(actual,2824,AGX_DXIL_INPUT_CONTAINER,&profile,AGX_DXIL_PARSE_INVALID);
 uint8_t q[40];program(q,32);check(q,32,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_OK);
 // No invented alignment/fixed-offset/version: gap+seven-byte payload+padding.
 program(q,40);put32(q+16,21);put32(q+20,7);put32(q+12,0x109);check(q,40,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_OK);
 for(unsigned field=0;field<6;field++){
  program(q,32);switch(field){case 0:put32(q+4,5);break;case 1:put32(q+4,UINT32_MAX);break;case 2:put32(q+16,UINT32_MAX);break;case 3:put32(q+20,UINT32_MAX);break;case 4:put32(q+20,0);break;case 5:put32(q+16,15);break;}
  check(q,32,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_INVALID);
 }
 program(q,32);put32(q+4,6);check(q,32,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_INVALID); // Payload outside declared program.
 program(q,32);put32(q,0xffff0060);check(q,32,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_UNSUPPORTED);
 program(q,32);put32(q,0xffff0060);put32(q+20,UINT32_MAX);check(q,32,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_INVALID); // Structure wins over profile rejection.
 uint8_t edited[2824];
 for(unsigned fault=0;fault<8;fault++){
  memcpy(edited,actual,2824);switch(fault){case 0:put32(edited+28,UINT32_MAX);break;case 1:put32(edited+32,32);break;case 2:put32(edited+36,60);break;case 3:put32(edited+32,61);break;case 4:put32(edited+24,UINT32_MAX);break;case 5:put32(edited+64,UINT32_MAX);break;case 6:put32(edited+32+6*4,UINT32_MAX);break;case 7:memcpy(edited+220,"DXIL",4);break;}
  check(edited,2824,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_INVALID);
 }
 memcpy(edited,actual,2824);memcpy(edited+1540,"NONE",4);check(edited,2824,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_UNSUPPORTED);
 memcpy(edited,actual,2824);edited[22]=7;check(edited,2824,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_OK); // Minor is not required to be zero.
 // Cross-product offset/length edges includes legal gaps and byte lengths.
 const uint32_t offsets[]={0,15,16,17,23,24,UINT32_MAX};
 const uint32_t lengths[]={0,1,7,8,9,16,UINT32_MAX};
 for(unsigned oi=0;oi<sizeof(offsets)/sizeof(offsets[0]);oi++)for(unsigned li=0;li<sizeof(lengths)/sizeof(lengths[0]);li++){
  program(q,32);put32(q+16,offsets[oi]);put32(q+20,lengths[li]);
  int legal=offsets[oi]>=16&&offsets[oi]<=24&&lengths[li]&&lengths[li]<=24-offsets[oi];
  check(q,32,AGX_DXIL_INPUT_PROGRAM,&compute60,legal?AGX_DXIL_PARSE_OK:AGX_DXIL_PARSE_INVALID);
 }
 for(unsigned part=0;part<7;part++)for(unsigned edge=0;edge<4;edge++){
  memcpy(edited,actual,2824);uint32_t off=get32(actual+32+4*part),size=get32(actual+off+4);
  if(edge<2)put32(edited+32+4*part,edge?off+1:off-1);
  else put32(edited+off+4,edge==2?size-1:size+1);
  check(edited,2824,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_INVALID);
 }
 memcpy(edited,actual,2824);edited[20]=2;check(edited,2824,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_UNSUPPORTED);
 memcpy(edited,actual,2824);edited[4]^=0x55;memcpy(edited+60,"NONE",4);check(edited,2824,AGX_DXIL_INPUT_CONTAINER,&compute60,AGX_DXIL_PARSE_OK); // Unknown parts/digest are not interpreted.
 program(q,40);put32(q+4,8);put32(q+20,8);p=check(q,40,AGX_DXIL_INPUT_PROGRAM,&compute60,AGX_DXIL_PARSE_OK);assert(p.program_bytes==32);
 profile=compute60;profile.maximum_shader_model=0x50;check(actual,2824,AGX_DXIL_INPUT_CONTAINER,&profile,AGX_DXIL_PARSE_INVALID);
 assert(get32(actual+24)==2824);
 printf("AGX_DXIL_INPUT_PASS cases=%u original_container=2824 program=1276 bitcode=1252 zero_failures=YES immutable=YES\n",cases);
}
