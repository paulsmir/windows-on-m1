/* SPDX-License-Identifier: MIT */
#include "../request.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static ans_u64 get(const ans_u8 *p,unsigned n)
{ ans_u64 v=0;for(unsigned i=0;i<n;i++)v|=(ans_u64)p[i]<<(8*i);return v; }
int main(void)
{
    struct ans_namespace ns={.blocks=1ULL<<40,.block_size=4096,.nsid=1,.ready=1,.read_only=1};
    ans_u8 cdb[16]={0x88},list[4096];struct ans_broker_request r;
    cdb[2]=0;cdb[5]=1;cdb[13]=32;
    struct ans_scsi_plan plan=ans_scsi_prepare(&ns,cdb,16,131072,NULL);
    struct ans_sg_span spans[33];struct ans_logical_prps prps;
    /* Deliberately non-dereferenceable payload addresses, fragmented >4GB. */
    for(unsigned i=0;i<32;i++)spans[i]=(struct ans_sg_span){0xf000000010000000ULL+i*0x10000ULL,4096};
    assert(ans_sg_to_prps(spans,32,131072,&prps)==ANS_PRP_OK);
    assert(ans_readonly_request(&plan,&prps,63,0x200000,list,&r));
    assert(r.tag==63 && r.queue==1 && r.sqe[2]==63 && r.sqe[3]==0);
    assert(get(r.sqe+24,8)==spans[0].address && get(r.sqe+32,8)==0x200000);
    assert(get(list+30*8,8)==spans[31].address && get(r.sqe+48,4)==31);
    plan.nvme_opcode=1;assert(!ans_readonly_request(&plan,&prps,2,0x200000,list,&r));
    plan.nvme_opcode=2;assert(!ans_readonly_request(&plan,&prps,1,0x200000,list,&r));
    assert(!ans_readonly_request(&plan,&prps,2,0x200001,list,&r));
    prps.count=31;assert(!ans_readonly_request(&plan,&prps,2,0x200000,list,&r));
    plan=(struct ans_scsi_plan){.action=ANS_SCSI_SUBMIT,.nsid=1};
    assert(ans_readonly_request(&plan,NULL,2,0,NULL,&r) && !r.transfer_length && !r.sqe[0]);
    puts("request_test: ok");
}
