/* SPDX-License-Identifier: MIT */
#include "request.h"
static void put(ans_u8 *p, ans_u64 value, unsigned count)
{
    for (unsigned i=0;i<count;i++) p[i]=(ans_u8)(value>>(8*i));
}
int ans_io_request(const struct ans_scsi_plan *p,
    const struct ans_logical_prps *prps, unsigned tag, ans_u64 list_dma,
    ans_u8 list[4096], int writable, struct ans_broker_request *out)
{
    if (!p || !out || p->action!=ANS_SCSI_SUBMIT || p->nsid!=1 ||
        tag<ANS_ADMIN_TAGS || tag>=ANS_QUEUE_DEPTH) return 0;
    if (p->nvme_opcode==0) {
        if (p->transfer_bytes || p->direction!=ANS_DMA_NONE || p->nvme_cdw12) return 0;
    } else if (p->nvme_opcode==2 || (p->nvme_opcode==1 && writable)) {
        if (!p->transfer_bytes || p->transfer_bytes>ANS_MAX_TRANSFER ||
            p->transfer_bytes%ANS_PRP_PAGE_SIZE ||
            p->direction!=(p->nvme_opcode==1?ANS_DMA_TO_DEVICE:ANS_DMA_FROM_DEVICE) ||
            (p->nvme_cdw12&~ANS_NVME_RW_FUA)!=p->transfer_bytes/ANS_PRP_PAGE_SIZE-1 || !prps ||
            prps->bytes!=p->transfer_bytes || !prps->count || prps->count>ANS_MAX_DATA_PAGES ||
            !prps->page[0] || (prps->page[0]&3) ||
            prps->count!=((prps->page[0]&(ANS_PRP_PAGE_SIZE-1))+p->transfer_bytes+
                         ANS_PRP_PAGE_SIZE-1)/ANS_PRP_PAGE_SIZE) return 0;
        for (unsigned i=1;i<prps->count;i++)
            if (!prps->page[i] || (prps->page[i]&(ANS_PRP_PAGE_SIZE-1))) return 0;
        if (prps->count>2 && (!list || !list_dma || (list_dma&(ANS_PRP_PAGE_SIZE-1)))) return 0;
    } else if (p->nvme_opcode==ANS_NVME_DSM && writable) {
        /* Deallocate: the resolved range list already sits in this tag's private page
         * (ans_scsi_unmap_resolve). The whole 4 KiB page is the data-out transfer. */
        if (!p->dsm_ranges || p->dsm_ranges>ANS_UNMAP_MAX_DESCRIPTORS || !p->transfer_bytes ||
            p->direction!=ANS_DMA_TO_DEVICE || p->nvme_cdw12 || p->lba || !list || !list_dma ||
            (list_dma&(ANS_PRP_PAGE_SIZE-1))) return 0;
        *out=(struct ans_broker_request){0};
        out->operation=ANS_OP_AUTHORIZE_SUBMIT;out->queue=1;out->tag=(ans_u16)tag;
        out->direction=ANS_DMA_TO_DEVICE;out->transfer_length=ANS_PRP_PAGE_SIZE;
        out->sqe[0]=(ans_u8)ANS_NVME_DSM;out->sqe[2]=(ans_u8)tag;
        put(out->sqe+4,p->nsid,4);put(out->sqe+24,list_dma,8);
        put(out->sqe+40,p->dsm_ranges-1,4); /* CDW10 NR, zero based */
        put(out->sqe+44,4,4);               /* CDW11 AD: deallocate */
        return 1;
    } else return 0; /* Independent write rejection when not writable, even if the planner errs. */
    *out=(struct ans_broker_request){0};
    out->operation=ANS_OP_AUTHORIZE_SUBMIT;out->queue=1;out->tag=(ans_u16)tag;
    out->direction=p->direction;out->transfer_length=p->transfer_bytes;
    out->sqe[0]=p->nvme_opcode;out->sqe[2]=(ans_u8)tag;
    put(out->sqe+4,p->nsid,4);put(out->sqe+40,p->lba,8);put(out->sqe+48,p->nvme_cdw12,4);
    if (p->transfer_bytes) {
        put(out->sqe+24,prps->page[0],8);
        if (prps->count==2) put(out->sqe+32,prps->page[1],8);
        else if (prps->count>2) {
            for (unsigned i=1;i<prps->count;i++) put(list+(i-1)*8,prps->page[i],8);
            put(out->sqe+32,list_dma,8);
        }
    }
    return 1;
}
