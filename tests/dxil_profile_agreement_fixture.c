/* Original actual-DXC module admission fixture. SPDX-License-Identifier: MIT
 * No mocked metadata, compiler, pointer faults or malformed-metadata generation. */
#include "agx_dxil_owned.h"
#include "agx_dxil_checked.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do { ++checks; if(!(x)){++failures;fprintf(stderr,"check %u: %s\n",checks,#x);} }while(0)
static unsigned char *read_file(const char *path,size_t *bytes){
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
 if(n<24||n>1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
 unsigned char *p=malloc((size_t)n);if(!p){fclose(f);return NULL;}
 if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
 fclose(f);*bytes=(size_t)n;return p;
}
static void word(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static unsigned char *program(const void *container,size_t bytes,unsigned model,size_t *out_bytes){
 const struct agx_dxil_profile profile={1u<<5,0x60,0x61};struct agx_dxil_program f={0};
 if(agx_dxil_parse(container,bytes,AGX_DXIL_INPUT_CONTAINER,&profile,&f)!=AGX_DXIL_PARSE_OK)return NULL;
 CHECK(f.shader_kind==5&&f.shader_major==6&&f.shader_minor==model);
 unsigned char *p=malloc(f.program_bytes);if(!p)return NULL;memcpy(p,(const unsigned char*)container+f.program_offset,f.program_bytes);*out_bytes=f.program_bytes;return p;
}
static int empty(const struct agx_dxil_owned_result *r){
 if(r->spirv||r->spirv_bytes||r->bindings.active||r->bindings.count)return 0;
 for(unsigned i=0;i<3;i++)if(r->workgroup[i])return 0;
 for(unsigned i=0;i<sizeof(r->entry);i++)if(r->entry[i])return 0;
 return 1;
}
static void convert(const char *label,const unsigned char *p,size_t bytes,int success){
 unsigned char *before=malloc(bytes);if(!before){CHECK(0);return;}memcpy(before,p,bytes);
 struct agx_dxil_owned_request q={.input=p,.input_bytes=bytes,.input_kind=AGX_DXIL_INPUT_PROGRAM,.entry="main",.entry_bytes=4,.binding_limits={14,128,8,16}};
 struct agx_dxil_owned_result r={0};enum agx_dxil_owned_status status=agx_dxil_convert_owned(&q,&r);
 CHECK(status==(success?AGX_DXIL_OWNED_OK:AGX_DXIL_OWNED_UNSUPPORTED));CHECK(!memcmp(before,p,bytes));
 if(success){CHECK(r.spirv&&r.spirv_bytes>=20);CHECK(r.bindings.active==1&&r.bindings.count==1);const struct dxbc_binding *u=dxbc_binding_find(&r.bindings,DXBC_BINDING_UAV,0,0);CHECK(u&&u->slot==0);CHECK(r.workgroup[0]==64&&r.workgroup[1]==1&&r.workgroup[2]==1);CHECK(!strcmp(r.entry,"main"));}
 else CHECK(empty(&r));
 printf("AGX_DXIL_PROFILE_CASE %s status=%u expected=%u\n",label,(unsigned)status,(unsigned)(success?AGX_DXIL_OWNED_OK:AGX_DXIL_OWNED_UNSUPPORTED));
 agx_dxil_owned_free(&r);CHECK(empty(&r));free(before);
}
#ifdef AGX_PROFILE_QUERY_AVAILABLE
static void query(const unsigned char *p,size_t bytes,unsigned minor){
 const struct agx_dxil_profile policy={1u<<5,0x60,0x61};struct agx_dxil_program f={0};CHECK(agx_dxil_parse(p,bytes,AGX_DXIL_INPUT_PROGRAM,&policy,&f)==AGX_DXIL_PARSE_OK);
 dxil_spv_parsed_blob blob=NULL;dxil_spv_compute_profile profile={0};CHECK(agx_checked_begin()==DXIL_SPV_SUCCESS);
 CHECK(agx_checked_parse(p+f.bitcode_offset,f.bitcode_bytes,&blob)==DXIL_SPV_SUCCESS);
 if(blob){CHECK(agx_checked_compute_profile(blob,&profile)==DXIL_SPV_SUCCESS);CHECK(profile.shader_major==6&&profile.shader_minor==minor);CHECK(profile.dxil_major==1&&profile.dxil_minor==minor);printf("AGX_DXIL_REAL_MODULE_PROFILE sm=%u.%u dxil=%u.%u\n",profile.shader_major,profile.shader_minor,profile.dxil_major,profile.dxil_minor);CHECK(agx_checked_blob_free(blob)==DXIL_SPV_SUCCESS);}
 CHECK(agx_checked_end()==DXIL_SPV_SUCCESS);
}
#endif
int main(int argc,char **argv){
 if(argc!=3)return 2;size_t n60=0,n61=0,p60n=0,p61n=0;
 unsigned char *c60=read_file(argv[1],&n60),*c61=read_file(argv[2],&n61);if(!c60||!c61){free(c60);free(c61);return 2;}
 unsigned char *p60=program(c60,n60,0,&p60n),*p61=program(c61,n61,1,&p61n);if(!p60||!p61){free(c60);free(c61);free(p60);free(p61);return 2;}
 unsigned char *masked=malloc(p61n),*version=malloc(p60n);if(!masked||!version){free(c60);free(c61);free(p60);free(p61);free(masked);free(version);return 2;}
 memcpy(masked,p61,p61n);word(masked,0x00050060);word(masked+12,0x100);
 memcpy(version,p60,p60n);word(version+12,0x101);
 for(size_t i=0;i<p61n;i++)if(!(i<4||(i>=12&&i<16)))CHECK(masked[i]==p61[i]);
 for(size_t i=0;i<p60n;i++)if(!(i>=12&&i<16))CHECK(version[i]==p60[i]);
 CHECK(!memcmp(masked+24,p61+24,p61n-24));CHECK(!memcmp(version+24,p60+24,p60n-24));
#ifdef AGX_PROFILE_QUERY_AVAILABLE
 query(p60,p60n,0);query(p61,p61n,1);query(masked,p61n,1);query(version,p60n,0);
#endif
 convert("actual_cs60",p60,p60n,1);convert("actual_cs61_unsupported",p61,p61n,0);
 convert("cs61_masked_header_cs60",masked,p61n,0);convert("same_thread_nominal_after_model_refusal",p60,p60n,1);
 convert("cs60_header_dxil11",version,p60n,0);convert("same_thread_nominal_after_version_refusal",p60,p60n,1);
 free(c60);free(c61);free(p60);free(p61);free(masked);free(version);
 printf("AGX_DXIL_PROFILE_AGREEMENT %s checks=%u failures=%u\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
