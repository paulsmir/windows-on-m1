/* Original finite framing tests: actual new framer + actual current full parser.
 * No implementation stand-ins. Full parser is a fixture comparator, NOT inCalc. */
#include "agx_legacy_program_frame.h"
#include "dxbc_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned checks,positives;
static void need(int v,const char *why){++checks;if(!v){fprintf(stderr,"FAIL %s\n",why);abort();}}
static void wr32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(8*i));}
struct guarded {unsigned char left[16];struct agx_legacy_program_frame out;unsigned char right[16];};
static const struct agx_legacy_frame_profile all={0x3fu,0xfu};
static void frame_case(const void *p,size_t bytes,const struct agx_legacy_frame_profile *profile,enum agx_legacy_frame_status expected){
 for(unsigned poison=0;poison<3;++poison){unsigned char v=poison==0?0:poison==1?0xa5:0xff;struct guarded g;memset(&g,v,sizeof(g));
  need(agx_legacy_frame_program(p,bytes,profile,&g.out)==expected,"exact framing status");
  for(unsigned i=0;i<16;++i)need(g.left[i]==v&&g.right[i]==v,"output canaries");
  if(expected!=AGX_LEGACY_FRAME_OK){struct agx_legacy_program_frame zero;memset(&zero,0,sizeof(zero));need(!memcmp(&zero,&g.out,sizeof(zero)),"all failure output bytes cleared");}
 }
}
int main(int argc,char **argv){
 need(argc>=2,"real FXC PROGRAM fixture paths supplied");
 for(int n=1;n<argc;++n){FILE *f=fopen(argv[n],"rb");need(f!=NULL,"fixture open");need(!fseek(f,0,SEEK_END),"fixture size seek");long length=ftell(f);need(length>=8,"fixture extent");rewind(f);unsigned char *p=malloc((size_t)length+8),*snapshot=malloc((size_t)length);need(p&&snapshot,"fixture setup allocation");need(fread(p,1,(size_t)length,f)==(size_t)length,"fixture read");fclose(f);memcpy(snapshot,p,(size_t)length);
  struct agx_legacy_program_frame out;need(agx_legacy_frame_program(p,(size_t)length,&all,&out)==AGX_LEGACY_FRAME_OK,"actual FXC PROGRAM framing");
  struct dxbc_program parsed;struct dxbc_error error;need(dxbc_parse_program((const uint32_t *)p,(size_t)length/4,&parsed,&error)==DXBC_OK,"actual full parser positive comparator");
  need(out.program_bytes==(size_t)length&&out.words==parsed.length&&out.stage==parsed.type&&out.major==parsed.major&&out.minor==parsed.minor,"framer/full parser metadata agreement");dxbc_program_free(&parsed);++positives;
  memmove(p+1,p,(size_t)length);need(agx_legacy_frame_program(p+1,(size_t)length,&all,&out)==AGX_LEGACY_FRAME_OK,"unaligned actual PROGRAM");need(!memcmp(p+1,snapshot,(size_t)length),"actual input unchanged");
  frame_case(p+1,(size_t)length-1,&all,AGX_LEGACY_FRAME_INVALID);free(snapshot);free(p);
 }
 unsigned char header[24];memset(header,0xcc,sizeof(header));wr32(header,0x00010040);wr32(header+4,2);
 frame_case(header,8,&all,AGX_LEGACY_FRAME_OK);struct agx_legacy_program_frame out;need(agx_legacy_frame_program(header,sizeof(header),&all,&out)==AGX_LEGACY_FRAME_OK&&out.program_bytes==8,"tail excluded; empty body framing only");
 for(size_t n=0;n<8;++n)frame_case(header,n,&all,AGX_LEGACY_FRAME_INVALID);
 frame_case(NULL,8,&all,AGX_LEGACY_FRAME_INVALID);frame_case(header,8,NULL,AGX_LEGACY_FRAME_INVALID);need(agx_legacy_frame_program(header,8,&all,NULL)==AGX_LEGACY_FRAME_INVALID,"null output");
 for(uint32_t w=0;w<2;++w){wr32(header+4,w);frame_case(header,8,&all,AGX_LEGACY_FRAME_INVALID);}wr32(header+4,UINT32_MAX);frame_case(header,8,&all,AGX_LEGACY_FRAME_INVALID);wr32(header+4,3);frame_case(header,8,&all,AGX_LEGACY_FRAME_INVALID);wr32(header+4,2);
 for(unsigned stage=0;stage<6;++stage)for(unsigned m=0;m<4;++m){unsigned model=m==0?0x40:m==1?0x41:m==2?0x50:0x51;wr32(header,(stage<<16)|model);frame_case(header,8,&all,AGX_LEGACY_FRAME_OK);}
 wr32(header,0x000d0051);frame_case(header,8,&all,AGX_LEGACY_FRAME_UNSUPPORTED);wr32(header,0xfff00050);frame_case(header,8,&all,AGX_LEGACY_FRAME_UNSUPPORTED);
 for(unsigned model=0x52;model<=0x60;model+=0xe){wr32(header,0x00050000|model);frame_case(header,8,&all,AGX_LEGACY_FRAME_UNSUPPORTED);}
 wr32(header,0x0001ab40);frame_case(header,8,&all,AGX_LEGACY_FRAME_OK);
 wr32(header,0x43425844);frame_case(header,8,&all,AGX_LEGACY_FRAME_INVALID);
 wr32(header,0xdec04342);frame_case(header,8,&all,AGX_LEGACY_FRAME_UNSUPPORTED);wr32(header,0x4c495844);frame_case(header,8,&all,AGX_LEGACY_FRAME_UNSUPPORTED);
 wr32(header,0x00010040);struct agx_legacy_frame_profile profile=all;profile.stage_mask=1u<<5;frame_case(header,8,&profile,AGX_LEGACY_FRAME_UNSUPPORTED);profile=all;profile.model_mask=AGX_LEGACY_SM51;frame_case(header,8,&profile,AGX_LEGACY_FRAME_UNSUPPORTED);
 for(unsigned bit=0;bit<2;++bit){profile=all;if(bit)profile.model_mask=0x10;else profile.stage_mask=0x40;frame_case(header,8,&profile,AGX_LEGACY_FRAME_INVALID);}profile=all;profile.stage_mask=0;frame_case(header,8,&profile,AGX_LEGACY_FRAME_INVALID);profile=all;profile.model_mask=0;frame_case(header,8,&profile,AGX_LEGACY_FRAME_INVALID);
 // No body reads: valid prefix ends immediately before inaccessible page.
 long page=sysconf(_SC_PAGESIZE);need(page>8,"page size");unsigned char *map=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);need(map!=MAP_FAILED,"guard setup");need(!mprotect(map+page,(size_t)page,PROT_NONE),"guard protect");unsigned char *edge=map+page-8;wr32(edge,0x00010040);wr32(edge+4,2);frame_case(edge,8,&all,AGX_LEGACY_FRAME_OK);wr32(edge+4,3);frame_case(edge,8,&all,AGX_LEGACY_FRAME_INVALID);need(!munmap(map,(size_t)page*2),"guard cleanup");
 printf("LEGACY_PROGRAM_FRAME_PASS checks=%u actual_FXC_positives=%u actual_parser_compare=YES nonallocating_framer=YES IR_validation=NO\n",checks,positives);return 0;
}
