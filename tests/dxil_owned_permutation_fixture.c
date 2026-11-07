/* Actual C API scan records -> extracted actual freeze_map; no matching sort. */
#include "agx_dxil_owned.h"
#include "dxil_spirv_c.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* ACTUAL_FREEZE_HELPERS */
static struct dxbc_binding records[5];static unsigned count,permutations;
static void record(uint32_t kind,const dxil_spv_d3d_binding *b,unsigned counter){
 assert(count<5&&b->stage==DXIL_SPV_STAGE_COMPUTE&&b->range_size==1&&!counter&&!b->alignment);
 assert(b->kind==(kind==DXBC_BINDING_CBV?DXIL_SPV_RESOURCE_KIND_CONSTANT_BUFFER:DXIL_SPV_RESOURCE_KIND_RAW_BUFFER));
 printf("ACTUAL_SCAN kind=%u id=%u space=%u reg=%u count=%u shape=%u alignment=%u\n",kind,b->resource_index,b->register_space,b->register_index,b->range_size,b->kind,b->alignment);
 records[count++]=(struct dxbc_binding){kind,b->resource_index,b->register_space,b->register_index,0};
}
static dxil_spv_bool srv(void *u,const dxil_spv_d3d_binding *b,dxil_spv_srv_vulkan_binding *v){(void)u;memset(v,0,sizeof(*v));record(DXBC_BINDING_SRV,b,0);return DXIL_SPV_TRUE;}
static dxil_spv_bool cbv(void *u,const dxil_spv_d3d_binding *b,dxil_spv_cbv_vulkan_binding *v){(void)u;memset(v,0,sizeof(*v));record(DXBC_BINDING_CBV,b,0);return DXIL_SPV_TRUE;}
static dxil_spv_bool uav(void *u,const dxil_spv_uav_d3d_binding *b,dxil_spv_uav_vulkan_binding *v){(void)u;memset(v,0,sizeof(*v));record(DXBC_BINDING_UAV,&b->d3d_binding,b->has_counter);return DXIL_SPV_TRUE;}
static dxil_spv_bool sampler(void *u,const dxil_spv_d3d_binding *b,dxil_spv_vulkan_binding *v){(void)u;(void)b;(void)v;assert(!"unexpected sampler");return DXIL_SPV_FALSE;}
static void permutation(unsigned n){
 if(n==5){struct conversion_job job={0};job.map.count=5;memcpy(job.map.bindings,records,sizeof(records));job.limits[0]=14;job.limits[1]=128;job.limits[2]=8;job.limits[3]=16;
  assert(freeze_map(&job));const unsigned kinds[]={0,1,1,2,2},spaces[]={3,2,7,1,9},slots[]={0,0,1,0,1};
  for(unsigned i=0;i<5;i++){const struct dxbc_binding *b=&job.map.bindings[i];assert(b->kind==kinds[i]&&b->space==spaces[i]&&!b->reg&&b->slot==slots[i]);unsigned matches=0;for(unsigned j=0;j<5;j++)if(records[j].kind==b->kind&&records[j].space==b->space){assert(records[j].id==b->id);matches++;}assert(matches==1);}
  job.rejected=1;assert(!freeze_map(&job));permutations++;return;
 }
 for(unsigned i=n;i<5;i++){struct dxbc_binding save=records[n];records[n]=records[i];records[i]=save;permutation(n+1);save=records[n];records[n]=records[i];records[i]=save;}
}
int main(int argc,char **argv){assert(argc==2);unsigned char input[3704];FILE *f=fopen(argv[1],"rb");assert(f&&fread(input,1,sizeof(input),f)==sizeof(input)&&fgetc(f)==EOF);fclose(f);
 dxil_spv_parsed_blob blob=NULL;dxil_spv_begin_thread_allocator_context();assert(dxil_spv_parse_dxil_blob(input,sizeof(input),&blob)==DXIL_SPV_SUCCESS);
 assert(dxil_spv_parsed_blob_scan_resources(blob,srv,sampler,cbv,uav,NULL)==DXIL_SPV_SUCCESS&&count==5);
 dxil_spv_parsed_blob_free(blob);dxil_spv_end_thread_allocator_context();
 permutation(0);assert(permutations==120);puts("C15_MIXED_MAP_PASS actual_scan_records=5 actual_freeze_permutations=120 source_ids_preserved=YES sticky_rejection=YES");
}
