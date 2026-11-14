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
    assert(ans_io_request(&plan,&prps,63,0x200000,list,0,&r));
    assert(r.tag==63 && r.queue==1 && r.sqe[2]==63 && r.sqe[3]==0);
    assert(get(r.sqe+24,8)==spans[0].address && get(r.sqe+32,8)==0x200000);
    assert(get(list+30*8,8)==spans[31].address && get(r.sqe+48,4)==31);
    /* A read-only namespace rejects WRITE even if the SCSI planner errs. */
    plan.nvme_opcode=1;assert(!ans_io_request(&plan,&prps,2,0x200000,list,0,&r));
    /* A writable namespace still requires TO_DEVICE for WRITE. */
    assert(!ans_io_request(&plan,&prps,2,0x200000,list,1,&r));
    plan.direction=ANS_DMA_TO_DEVICE;
    assert(ans_io_request(&plan,&prps,5,0x200000,list,1,&r));
    assert(r.sqe[0]==1 && r.direction==ANS_DMA_TO_DEVICE && r.transfer_length==131072 &&
           get(r.sqe+48,4)==31 && get(r.sqe+24,8)==spans[0].address && get(r.sqe+32,8)==0x200000);
    assert(!ans_io_request(&plan,&prps,5,0x200000,list,0,&r));
    plan.direction=ANS_DMA_FROM_DEVICE;
    plan.nvme_opcode=2;assert(!ans_io_request(&plan,&prps,1,0x200000,list,0,&r));
    plan.direction=ANS_DMA_TO_DEVICE;assert(!ans_io_request(&plan,&prps,5,0x200000,list,1,&r));
    plan.direction=ANS_DMA_FROM_DEVICE;
    assert(!ans_io_request(&plan,&prps,2,0x200001,list,0,&r));
    prps.count=31;assert(!ans_io_request(&plan,&prps,2,0x200000,list,0,&r));
    plan.nvme_opcode=3;prps.count=32;assert(!ans_io_request(&plan,&prps,2,0x200000,list,1,&r));
    plan=(struct ans_scsi_plan){.action=ANS_SCSI_SUBMIT,.nsid=1};
    assert(ans_io_request(&plan,NULL,2,0,NULL,0,&r) && !r.transfer_length && !r.sqe[0]);
    /* SCSI WRITE(16) on a writable namespace plans an NVMe WRITE that the builder accepts. */
    ns.read_only=0;cdb[0]=0x8a;plan=ans_scsi_prepare(&ns,cdb,16,131072,NULL);
    assert(plan.action==ANS_SCSI_SUBMIT && plan.nvme_opcode==1 && plan.direction==ANS_DMA_TO_DEVICE);
    assert(ans_io_request(&plan,&prps,7,0x200000,list,1,&r) && r.sqe[0]==1 && get(r.sqe+40,8)==(1ULL<<32));
    ns.read_only=1;plan=ans_scsi_prepare(&ns,cdb,16,131072,NULL);
    assert(plan.action==ANS_SCSI_CHECK && plan.sense.key==7);
    /* UNMAP -> Dataset Management: one deallocate command whose range page is the tag's
     * private metadata page. No SG payload, so prps are not consulted. */
    ns.read_only=0;ns.deallocate=1;
    ans_u8 unmap[10]={0x42},param[40]={0},dsm[4096];
    unmap[8]=40;param[1]=38;param[3]=32;param[15]=9;param[19]=3;param[31]=77;param[35]=1;
    plan=ans_scsi_prepare(&ns,unmap,10,40,NULL);
    ans_scsi_unmap_resolve(&ns,&plan,param,dsm);
    assert(plan.action==ANS_SCSI_SUBMIT && plan.dsm_ranges==2);
    memset(&r,0xff,sizeof r);
    assert(ans_io_request(&plan,NULL,9,0x300000,dsm,1,&r));
    assert(r.operation==ANS_OP_AUTHORIZE_SUBMIT && r.queue==1 && r.tag==9);
    assert(r.sqe[0]==9 && r.sqe[2]==9 && get(r.sqe+4,4)==1);
    assert(get(r.sqe+24,8)==0x300000 && get(r.sqe+32,8)==0);
    assert(get(r.sqe+40,4)==1 && get(r.sqe+44,4)==4 && get(r.sqe+48,4)==0); /* NR 0-based, AD */
    assert(r.direction==ANS_DMA_TO_DEVICE && r.transfer_length==4096);
    assert(get(dsm+4,4)==3 && get(dsm+8,8)==9 && get(dsm+20,4)==1 && get(dsm+24,8)==77);
    assert(!ans_io_request(&plan,NULL,9,0x300000,dsm,0,&r));   /* read-only build */
    assert(!ans_io_request(&plan,NULL,9,0,dsm,1,&r));          /* no range page */
    assert(!ans_io_request(&plan,NULL,9,0x300010,dsm,1,&r));
    assert(!ans_io_request(&plan,NULL,9,0x300000,NULL,1,&r));
    assert(!ans_io_request(&plan,NULL,1,0x300000,dsm,1,&r));   /* admin tag */
    plan.dsm_ranges=0;assert(!ans_io_request(&plan,NULL,9,0x300000,dsm,1,&r)); /* unresolved */
    plan.dsm_ranges=257;assert(!ans_io_request(&plan,NULL,9,0x300000,dsm,1,&r));
    plan.dsm_ranges=256;assert(ans_io_request(&plan,NULL,9,0x300000,dsm,1,&r) && get(r.sqe+40,4)==255);
    plan.direction=ANS_DMA_FROM_DEVICE;assert(!ans_io_request(&plan,NULL,9,0x300000,dsm,1,&r));
    plan.direction=ANS_DMA_TO_DEVICE;plan.nvme_cdw12=1;
    assert(!ans_io_request(&plan,NULL,9,0x300000,dsm,1,&r));
    /* FUA write: CDW12 = zero-based block count | bit 30; the TCB length (low 16 bits) is unchanged. */
    {
        struct ans_namespace fns=ns; ans_u8 w[10]={0x2a,0x08};struct ans_logical_prps one={0};
        fns.read_only=0;fns.deallocate=0;w[5]=7;w[8]=1;one.page[0]=0x400000;one.count=1;one.bytes=4096;
        struct ans_scsi_plan fp=ans_scsi_prepare(&fns,w,10,4096,NULL);
        assert(fp.action==ANS_SCSI_SUBMIT && fp.nvme_cdw12==0x40000000u);
        assert(ans_io_request(&fp,&one,9,0x300000,list,1,&r));
        assert(r.sqe[0]==1 && get(r.sqe+48,4)==0x40000000u && get(r.sqe+48,2)==0 && get(r.sqe+40,8)==7);
        fp.nvme_cdw12=0x40000001u;assert(!ans_io_request(&fp,&one,9,0x300000,list,1,&r)); /* count mismatch */
        fp.nvme_cdw12=0x80000000u;assert(!ans_io_request(&fp,&one,9,0x300000,list,1,&r)); /* other bits */
    }
    puts("request_test: ok");
}
