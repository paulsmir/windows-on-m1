#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../include/apple_agx_gpuva_b1_roots.h"
#include "../../../../m1n1_windows/src/hv_agx_gpuva_v5.h"
#include "../../../../m1n1_windows/src/hv_agx_gpuva_v5_mmio.h"
#define BACKEND UINT64_C(0x20000000)
#define TABLES UINT64_C(0x30000000)
#define OUT_A UINT64_C(0x40000000)
#define OUT_B UINT64_C(0x40004000)
struct fixture {
 struct hv_agx_gpuva_v5 broker;
 struct hv_agx_gpuva_v5_wire wire;
 uint64_t table[10][2048], slots[64][2];
 unsigned invalidations[64], fail_map_once;
 unsigned drop_next_response;
};
static uint64_t translate(void *opaque,uint64_t ipa){(void)opaque;
 if((ipa>=BACKEND&&ipa<BACKEND+0x800000)||(ipa>=TABLES&&ipa<TABLES+10*0x4000)||ipa==OUT_A||ipa==OUT_B)return ipa;
 return 0;
}
static uint64_t *map_page(void *opaque,uint64_t ipa,uint64_t pa){struct fixture *f=opaque;
 if(ipa!=pa||ipa<TABLES||ipa>=TABLES+10*0x4000)return NULL;
 return f->table[(ipa-TABLES)/0x4000];
}
static bool read_slot(void *opaque,unsigned slot,uint64_t *low,uint64_t *high){struct fixture *f=opaque;
 if(slot>=64)return false;*low=f->slots[slot][0];*high=f->slots[slot][1];return true;
}
static bool write_slot(void *opaque,unsigned slot,uint64_t low,uint64_t high){struct fixture *f=opaque;
 if(!slot||slot>=64||high)return false;f->slots[slot][0]=low;f->slots[slot][1]=high;return true;
}
static bool sync_tables(void *opaque){(void)opaque;return true;}
static bool invalidate(void *opaque,unsigned slot){struct fixture *f=opaque;++f->invalidations[slot];return true;}
static bool prefix(void *opaque){(void)opaque;return true;}
static bool legacy63(void *opaque){(void)opaque;return true;}
static void execute(void *opaque,const AGX_GPUVA_V5_REQUEST *q,AGX_GPUVA_V5_RESPONSE *r){
 struct fixture *f=opaque;enum hv_agx_gpuva_v5_result status=HV_AGX_GPUVA_V5_STALE;
 uint64_t logical[4]={q->LogicalIpa[0],q->LogicalIpa[1],q->LogicalIpa[2],q->LogicalIpa[3]},token=0;
 if(q->Epoch!=7)goto done;
 if(f->fail_map_once&&q->Command==AGX_GPUVA_V5_UPDATE_LEAF&&q->ValidMask){--f->fail_map_once;status=HV_AGX_GPUVA_V5_TLB;goto done;}
 switch(q->Command){
 case AGX_GPUVA_V5_CREATE:status=hv_agx_gpuva_v5_create(&f->broker,q->ProcessId,q->ProcessGeneration,q->TableIpa,q->Flags!=0);break;
 case AGX_GPUVA_V5_REGISTER_TABLE:status=hv_agx_gpuva_v5_register_table(&f->broker,q->ProcessId,q->ProcessGeneration,q->AuxIpa,q->Index);break;
 case AGX_GPUVA_V5_REGISTER_BACKING:status=hv_agx_gpuva_v5_register_backing(&f->broker,q->ProcessId,q->ProcessGeneration,q->AllocationGeneration,q->AuxIpa);break;
 case AGX_GPUVA_V5_REGISTER_SHARED_BACKING:status=hv_agx_gpuva_v5_register_shared_backing(&f->broker,q->ProcessId,q->ProcessGeneration,q->AllocationGeneration,q->AuxIpa);break;
 case AGX_GPUVA_V5_UPDATE_PARENT:status=hv_agx_gpuva_v5_update_parent(&f->broker,q->ProcessId,q->ProcessGeneration,q->TableIpa,q->Index,q->AuxIpa);break;
 case AGX_GPUVA_V5_UPDATE_LEAF:status=hv_agx_gpuva_v5_update_leaf(&f->broker,q->ProcessId,q->ProcessGeneration,q->TableIpa,q->Index,q->ValidMask?logical:NULL,q->AllocationGeneration,q->Flags,q->ValidMask,q->WritableMask);break;
 case AGX_GPUVA_V5_LEASE:status=hv_agx_gpuva_v5_lease(&f->broker,q->ProcessId,q->ProcessGeneration,q->Slot,&token);r->Token=token;break;
 case AGX_GPUVA_V5_JOB_BEGIN:status=hv_agx_gpuva_v5_job_begin(&f->broker,q->Slot,q->Token);break;
 case AGX_GPUVA_V5_JOB_END:status=hv_agx_gpuva_v5_job_end(&f->broker,q->Slot,q->Token);break;
 case AGX_GPUVA_V5_RELEASE:status=hv_agx_gpuva_v5_release(&f->broker,q->Slot,q->Token);break;
 case AGX_GPUVA_V5_REVOKE_BACKING:status=hv_agx_gpuva_v5_revoke_backing(&f->broker,q->ProcessId,q->ProcessGeneration,q->AllocationGeneration,q->AuxIpa);break;
 case AGX_GPUVA_V5_REVOKE_TABLE:status=hv_agx_gpuva_v5_revoke_table(&f->broker,q->ProcessId,q->ProcessGeneration,q->AuxIpa,q->Index);break;
 case AGX_GPUVA_V5_DESTROY:status=hv_agx_gpuva_v5_destroy(&f->broker,q->ProcessId,q->ProcessGeneration);break;
 default:break;
 }
 done:r->Epoch=7;r->Status=status;
}
static bool w64(void *opaque,unsigned off,unsigned long long v){struct fixture*f=opaque;uint64_t x=v;return hv_agx_gpuva_v5_mmio(&f->wire,off-AGX_GPUVA_V5_OFFSET,&x,true,3,execute,f);}
static bool r64(void *opaque,unsigned off,unsigned long long *v){struct fixture*f=opaque;uint64_t x=0;
 if(f->drop_next_response&&off==AGX_GPUVA_V5_OFFSET+AGX_GPUVA_V5_RESPONSE_OFFSET){f->drop_next_response=0;return false;}
 bool ok=hv_agx_gpuva_v5_mmio(&f->wire,off-AGX_GPUVA_V5_OFFSET,&x,false,3,execute,f);*v=x;return ok;}
static bool w32(void *opaque,unsigned off,unsigned int v){struct fixture*f=opaque;uint64_t x=v;return hv_agx_gpuva_v5_mmio(&f->wire,off-AGX_GPUVA_V5_OFFSET,&x,true,2,execute,f);}
static void barrier(void *opaque){(void)opaque;}
static void run(void){
 struct fixture *f=calloc(1,sizeof(*f));assert(f);f->slots[0][0]=0x91000001;f->slots[0][1]=0x90000001;
 struct hv_agx_gpuva_v5_ops ops={f,translate,map_page,read_slot,write_slot,sync_tables,invalidate,prefix,legacy63};
 APPLE_AGX_GPUVA_V5_IO io={f,w64,r64,w32,barrier, 0};APPLE_AGX_GPUVA_V5_CLIENT client;
 APPLE_AGX_GPUVA_B1_PAGE graph_a[302],graph_b[302];unsigned int count;
 APPLE_AGX_GPUVA_B1_ROOT *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));assert(a&&b);
 APPLE_AGX_GPUVA_B1_ROOT_INPUT ai={1,1,TABLES,TABLES+0x4000,TABLES+0x8000,TABLES+0xc000,TABLES+0x10000,17,27,0};
 APPLE_AGX_GPUVA_B1_ROOT_INPUT bi={2,1,TABLES+0x14000,TABLES+0x18000,TABLES+0x1c000,TABLES+0x20000,TABLES+0x24000,17,28,1};
 AGX_GPUVA_V5_REQUEST q={0};AGX_GPUVA_V5_RESPONSE r={0};uint64_t token;
 assert(hv_agx_gpuva_v5_init(&f->broker,7,&ops)==HV_AGX_GPUVA_V5_OK);
 assert(AppleAgxGpuvaV5ClientInit(&client,&io));q.Command=AGX_GPUVA_V5_CREATE;
 assert(AppleAgxGpuvaV5ClientCall(&client,&q,&r)&&r.Status!=0&&client.Epoch==7);
 assert(AppleAgxGpuvaB1Graph(BACKEND,OUT_A,graph_a,302,&count)&&count==302);
 assert(AppleAgxGpuvaB1Graph(BACKEND,OUT_B,graph_b,302,&count)&&count==302);
 assert(AppleAgxGpuvaB1BuildRoot(&client,&ai,graph_a,count,a));
 assert(AppleAgxGpuvaB1BuildRoot(&client,&bi,graph_b,count,b));
 assert(a->MappedCount==302&&b->MappedCount==302&&a->GrantCount==285&&b->GrantCount==285);
 assert(f->table[4][116]!=f->table[9][116]); /* same VA, different owned output */
 for(unsigned which=0;which<2;which++){
  memset(&q,0,sizeof(q));q.Command=AGX_GPUVA_V5_LEASE;q.ProcessId=which?2:1;q.ProcessGeneration=1;q.Slot=1;
  assert(AppleAgxGpuvaV5ClientCall(&client,&q,&r)&&r.Status==0);token=r.Token;
  q.Command=AGX_GPUVA_V5_JOB_BEGIN;q.Token=token;assert(AppleAgxGpuvaV5ClientCall(&client,&q,&r)&&r.Status==0);
  q.Command=AGX_GPUVA_V5_RELEASE;assert(AppleAgxGpuvaV5ClientCall(&client,&q,&r)&&r.Status!=0);
  q.Command=AGX_GPUVA_V5_JOB_END;assert(AppleAgxGpuvaV5ClientCall(&client,&q,&r)&&r.Status==0);
  q.Command=AGX_GPUVA_V5_RELEASE;assert(AppleAgxGpuvaV5ClientCall(&client,&q,&r)&&r.Status==0);
 }
 assert(AppleAgxGpuvaB1DestroyRoot(&client,b));assert(AppleAgxGpuvaB1DestroyRoot(&client,a));
 f->fail_map_once=1;
 assert(!AppleAgxGpuvaB1BuildRoot(&client,&ai,graph_a,count,a));
 assert(a->LastStatus==HV_AGX_GPUVA_V5_TLB&&a->CleanupStatus==0);
 assert(a->Created==0&&a->TableCount==0&&a->ParentCount==0&&
        a->GrantCount==0&&a->MappedCount==0);
 assert(AppleAgxGpuvaB1BuildRoot(&client,&ai,graph_a,count,a));
 assert(AppleAgxGpuvaB1DestroyRoot(&client,a));
 f->drop_next_response=1;
 assert(!AppleAgxGpuvaB1BuildRoot(&client,&ai,graph_a,count,a));
 assert(a->Uncertain==1u&&f->broker.processes[0].live);
 assert(!AppleAgxGpuvaB1DestroyRoot(&client,a));
 assert(f->slots[0][0]==0x91000001&&f->slots[0][1]==0x90000001&&f->invalidations[1]>=4);
 free(a);free(b);free(f);
}
int main(void){run();return 0;}
