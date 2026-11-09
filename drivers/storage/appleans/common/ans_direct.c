/* SPDX-License-Identifier: MIT */
#include "ans_direct.h"
#if defined(_KERNEL_MODE)
#define ansd_zero(p, n) RtlZeroMemory((p), (n))
#define ansd_copy(d, s, n) RtlCopyMemory((d), (s), (n))
#else
#include <string.h>
#define ansd_zero(p, n) memset((p), 0, (n))
#define ansd_copy(d, s, n) memcpy((d), (s), (n))
#endif

/* RTKit post-boot responder set (m1n1 rtkit.c strict dispatch). */
#define RTK_EP_MGMT 0u
#define RTK_EP_CRASHLOG 1u
#define RTK_EP_SYSLOG 2u
#define RTK_EP_IOREPORT 4u
#define RTK_TYPE(m) ((unsigned)(((m) >> 52) & 0xffu))
#define RTK_MGMT_IOP_PWR_ACK 7u
#define RTK_MGMT_AP_PWR_ACK 0xbu
#define RTK_BUFFER_REQUEST 1u
#define RTK_SYSLOG_INIT 8u
#define RTK_SYSLOG_LOG 5u

#define POLL_US 10u
#define DEFAULT_TIMEOUT_MS 1000u
#define MAX_TIMEOUT_MS 5000u

static int in_range(ans_u64 base, ans_u64 size, ans_u64 addr, ans_u64 bytes)
{
    return addr >= base && addr - base < size && bytes <= size - (addr - base);
}
static int overlaps(ans_u64 base, ans_u64 size, ans_u64 addr, ans_u64 bytes)
{
    return addr < base + size && base < addr + bytes;
}
int ansd_translate(ans_u64 ipa, ans_u64 bytes, ans_u64 *pa)
{
    if (!pa) return 0;
    *pa = 0;
    if (!bytes || ipa > ANS_U64_MAX - bytes) return 0;
    if (in_range(ANSD_LOW_IPA, ANSD_LOW_SIZE, ipa, bytes)) {
        *pa = ipa - ANSD_LOW_IPA + ANSD_LOW_PA;
        return 1;
    }
    if (in_range(ANSD_RAM_BASE, ANSD_RAM_END - ANSD_RAM_BASE, ipa, bytes) &&
        !overlaps(ANSD_LOW_PA, ANSD_LOW_SIZE, ipa, bytes)) {
        *pa = ipa;
        return 1;
    }
    return 0;
}
int ansd_translate_prps(const struct ans_logical_prps *in, struct ans_logical_prps *out)
{
    struct ans_logical_prps t;
    if (!out) return 0;
    ansd_zero(out, sizeof(*out));
    if (!in || !in->count || in->count > ANS_MAX_DATA_PAGES) return 0;
    ansd_zero(&t, sizeof(t));
    for (ans_u32 i = 0; i < in->count; i++) {
        ans_u64 addr = in->page[i];
        ans_u64 off = addr & (ANS_PRP_PAGE_SIZE - 1);
        if (i && off) return 0;
        if (!ansd_translate(addr, ANS_PRP_PAGE_SIZE - off, &t.page[i])) return 0;
    }
    t.count = in->count;
    t.bytes = in->bytes;
    *out = t;
    return 1;
}

static ans_u32 r32(struct ansd_ctrl *c, unsigned bank, ans_u32 off)
{ return c->ops->read32(c->ctx, bank, off); }
static void w32(struct ansd_ctrl *c, unsigned bank, ans_u32 off, ans_u32 v)
{ c->ops->write32(c->ctx, bank, off, v); }

static int queue_valid(const struct ansd_queue *q, ans_u32 depth)
{
    return q->sq && q->cq && q->tcb && q->depth == depth &&
        q->sq_pa && q->cq_pa && q->tcb_pa &&
        !(q->sq_pa & (ANSD_QUEUE_ALIGN - 1)) && !(q->cq_pa & (ANSD_QUEUE_ALIGN - 1)) &&
        !(q->tcb_pa & (ANSD_QUEUE_ALIGN - 1));
}
static int fault(struct ansd_ctrl *c, int result)
{
    c->faulted = 1;
    return result;
}
/* Bounded wait; services the IOP mailbox like m1n1's rtkit_poll while waiting. */
static int wait_csts(struct ansd_ctrl *c, ans_u32 mask, ans_u32 wanted, ans_u32 timeout_ms)
{
    ans_u32 loops = timeout_ms * (1000u / POLL_US);
    for (ans_u32 i = 0; i <= loops; i++) {
        ans_u32 v = r32(c, ANSD_BANK_CTRL, ANSD_CSTS);
        c->last_csts = v;
        if ((v & mask) == wanted) return ANSD_OK;
        if (v == 0xffffffffu) return ANSD_FAULT;
        if (ansd_mbox_service(c, 8) == ANSD_CRASHED) return ANSD_CRASHED;
        c->ops->stall(c->ctx, POLL_US);
    }
    return ANSD_TIMEOUT;
}
int ansd_takeover(struct ansd_ctrl *c);
static int bind(struct ansd_ctrl *c, const struct ansd_ops *ops, void *ctx,
                const struct ansd_queue queues[2])
{
    if (!c || !ops || !ops->read32 || !ops->write32 || !ops->read64 || !ops->write64 ||
        !ops->barrier || !ops->stall || !queues ||
        !queue_valid(&queues[0], ANSD_ADMIN_DEPTH) || !queue_valid(&queues[1], ANSD_IO_DEPTH))
        return ANSD_INVALID;
    ansd_zero(c, sizeof(*c));
    c->ops = ops;
    c->ctx = ctx;
    for (unsigned i = 0; i < 2; i++) {
        c->q[i] = queues[i];
        c->q[i].head = 0;
        c->q[i].phase = 1;
        c->q[i].outstanding = 0;
    }
    c->stage = ANSD_STAGE_HANDOFF;
    return ANSD_OK;
}
static void clear_queues(struct ansd_ctrl *c)
{
    for (unsigned i = 0; i < 2; i++) {
        ansd_zero((void *)c->q[i].cq, c->q[i].depth * sizeof(struct ansd_cqe));
        ansd_zero(c->q[i].tcb, c->q[i].depth * sizeof(struct ansd_tcb));
        ansd_zero(c->q[i].sq, c->q[i].depth * 64u);
    }
}
int ansd_init(struct ansd_ctrl *c, const struct ansd_ops *ops, void *ctx,
              const struct ansd_queue queues[2])
{
    int r = bind(c, ops, ctx, queues);
    if (r == ANSD_OK) clear_queues(c);
    return r;
}
int ansd_init_takeover(struct ansd_ctrl *c, const struct ansd_ops *ops, void *ctx,
                       const struct ansd_queue queues[2])
{
    int r = bind(c, ops, ctx, queues);
    if (r != ANSD_OK) return r;
    /* The controller may still be running on this very memory: stop it first. */
    r = ansd_takeover(c);
    if (r == ANSD_OK) clear_queues(c);
    return r;
}

int ansd_takeover(struct ansd_ctrl *c)
{
    if (!c || !c->ops || c->faulted || c->enabled || c->stage != ANSD_STAGE_HANDOFF)
        return ANSD_STATE;
    if (r32(c, ANSD_BANK_CTRL, ANSD_BOOT_STATUS) != ANSD_BOOT_STATUS_OK) return fault(c, ANSD_STATE);
    ans_u32 cc = r32(c, ANSD_BANK_CTRL, ANSD_CC), csts = r32(c, ANSD_BANK_CTRL, ANSD_CSTS);
    c->last_csts = csts;
    if (csts == 0xffffffffu || (csts & ANSD_CSTS_CFS)) return fault(c, ANSD_FAULT);
    if (!(cc & ANSD_CC_EN) && !(csts & ANSD_CSTS_RDY)) return ANSD_OK;
    ans_u32 to = (ans_u32)((c->ops->read64(c->ctx, ANSD_BANK_CTRL, ANSD_CAP) >> 24) & 0xff) * 500u;
    to = to ? (to > MAX_TIMEOUT_MS ? MAX_TIMEOUT_MS : to) : DEFAULT_TIMEOUT_MS;
    if (cc & (ANSD_CC_EN | ANSD_CC_SHN_MASK)) {
        w32(c, ANSD_BANK_CTRL, ANSD_CC, cc & ~(ANSD_CC_EN | ANSD_CC_SHN_MASK));
        c->ops->barrier(c->ctx);
    }
    ans_u32 loops = to * (1000u / POLL_US);
    for (ans_u32 i = 0; i <= loops; i++) {
        csts = r32(c, ANSD_BANK_CTRL, ANSD_CSTS);
        c->last_csts = csts;
        if (csts == 0xffffffffu || (csts & ANSD_CSTS_CFS)) return fault(c, ANSD_FAULT);
        if (!(csts & ANSD_CSTS_RDY)) return ANSD_OK;
        if (ansd_mbox_service(c, 8) == ANSD_CRASHED) return fault(c, ANSD_CRASHED);
        c->ops->stall(c->ctx, POLL_US);
    }
    return fault(c, ANSD_TIMEOUT);
}
int ansd_enable(struct ansd_ctrl *c)
{
    if (!c || !c->ops || c->faulted || c->enabled || c->stage != ANSD_STAGE_HANDOFF)
        return ANSD_STATE;
    /* Hand-off contract. Anything else is an m1n1 defect: never "repair" it here. */
    if (r32(c, ANSD_BANK_CTRL, ANSD_BOOT_STATUS) != ANSD_BOOT_STATUS_OK) return fault(c, ANSD_STATE);
    ans_u32 cc = r32(c, ANSD_BANK_CTRL, ANSD_CC), csts = r32(c, ANSD_BANK_CTRL, ANSD_CSTS);
    c->last_csts = csts;
    if ((cc & ANSD_CC_EN) || (csts & (ANSD_CSTS_RDY | ANSD_CSTS_CFS)) || csts == 0xffffffffu)
        return fault(c, ANSD_STATE);
    ans_u64 cap = c->ops->read64(c->ctx, ANSD_BANK_CTRL, ANSD_CAP);
    /* CAP.MPSMIN [51:48] must allow 4 KiB pages; CAP.MQES [15:0] is zero based. */
    if (((cap >> 48) & 0xf) != 0 || (cap & 0xffff) + 1 < ANSD_IO_DEPTH) return fault(c, ANSD_STATE);
    ans_u32 to = (ans_u32)((cap >> 24) & 0xff) * 500u;
    c->cap_timeout_ms = to ? (to > MAX_TIMEOUT_MS ? MAX_TIMEOUT_MS : to) : DEFAULT_TIMEOUT_MS;

    c->stage = ANSD_STAGE_CONFIG;
    w32(c, ANSD_BANK_CTRL, ANSD_LINEAR_SQ_CTRL, 1);
    w32(c, ANSD_BANK_CTRL, ANSD_MAX_PEND, ANSD_IO_DEPTH | (ANSD_IO_DEPTH << 16));
    w32(c, ANSD_BANK_CTRL, ANSD_NVMMU_NUM, ANSD_IO_DEPTH - 1);
    w32(c, ANSD_BANK_CTRL, ANSD_AQA, (ANSD_ADMIN_DEPTH - 1) | ((ANSD_ADMIN_DEPTH - 1) << 16));
    c->ops->write64(c->ctx, ANSD_BANK_CTRL, ANSD_ASQ, c->q[0].sq_pa);
    c->ops->write64(c->ctx, ANSD_BANK_CTRL, ANSD_ACQ, c->q[0].cq_pa);
    c->ops->write64(c->ctx, ANSD_BANK_CTRL, ANSD_NVMMU_ASQ_TCB, c->q[0].tcb_pa);
    c->ops->write64(c->ctx, ANSD_BANK_CTRL, ANSD_NVMMU_IOSQ_TCB, c->q[1].tcb_pa);
    c->ops->barrier(c->ctx);

    c->stage = ANSD_STAGE_ENABLE;
    w32(c, ANSD_BANK_CTRL, ANSD_CC, ANSD_CC_IOSQES_IOCQES | ANSD_CC_EN);
    int ret = wait_csts(c, ANSD_CSTS_RDY | ANSD_CSTS_CFS, ANSD_CSTS_RDY, c->cap_timeout_ms);
    if (ret != ANSD_OK) return fault(c, ret == ANSD_CRASHED ? ANSD_CRASHED : ANSD_TIMEOUT);
    c->enabled = 1;
    c->stage = ANSD_STAGE_READY;
    return ANSD_OK;
}

static ans_u64 get64(const ans_u8 *p)
{ ans_u64 v = 0; for (unsigned i = 0; i < 8; i++) v |= (ans_u64)p[i] << (8 * i); return v; }

int ansd_submit(struct ansd_ctrl *c, unsigned queue, unsigned tag, const ans_u8 sqe[64],
                unsigned dma_flags)
{
    if (!c || queue > 1 || !sqe || dma_flags > ANSD_DMA_TO_DEVICE) return ANSD_INVALID;
    if (!c->enabled || c->faulted || c->crashed) return ANSD_STATE;
    struct ansd_queue *q = &c->q[queue];
    if (tag >= q->depth) return ANSD_INVALID;
    ans_u64 bit = (ans_u64)1 << tag;
    if (q->outstanding & bit) return ANSD_BUSY;
    ans_u64 prp1 = get64(sqe + 24), prp2 = get64(sqe + 32);
    if ((dma_flags != ANSD_DMA_NONE) != (prp1 != 0)) return ANSD_INVALID;
    ans_u8 *slot = q->sq + (ans_u64)tag * 64u;
    ansd_copy(slot, sqe, 64);
    slot[1] = 0;            /* PRP data transfer, no fused op */
    slot[2] = (ans_u8)tag;  /* command id == tag == linear SQ slot */
    slot[3] = 0;
    struct ansd_tcb *t = &q->tcb[tag];
    ansd_zero(t, sizeof(*t));
    t->opcode = 0;
    t->dma_flags = (ans_u8)dma_flags;
    t->command_id = (ans_u8)tag;
    t->length = (ans_u32)(sqe[48] | ((ans_u32)sqe[49] << 8)); /* cdw12[15:0] = NLB */
    t->prp1 = prp1;
    t->prp2 = prp2;
    q->outstanding |= bit;
    c->ops->barrier(c->ctx);
    w32(c, ANSD_BANK_CTRL, queue ? ANSD_DB_LINEAR_IOSQ : ANSD_DB_LINEAR_ASQ, tag);
    return ANSD_OK;
}

int ansd_peek(const struct ansd_ctrl *c, unsigned queue)
{
    if (!c || queue > 1 || !c->enabled) return 0;
    const struct ansd_queue *q = &c->q[queue];
    return ((q->cq[q->head].tail >> 16) & 1u) == q->phase;
}

int ansd_poll(struct ansd_ctrl *c, unsigned queue, struct ansd_cqe *out)
{
    if (!c || queue > 1 || !out) return ANSD_INVALID;
    if (!c->enabled || c->faulted) return ANSD_STATE;
    struct ansd_queue *q = &c->q[queue];
    volatile struct ansd_cqe *entry = &q->cq[q->head];
    if (((entry->tail >> 16) & 1u) != q->phase) return ANSD_EMPTY;
    c->ops->barrier(c->ctx); /* order payload/CQE reads after the phase observation */
    struct ansd_cqe e;
    e.result = entry->result;
    e.result_hi = entry->result_hi;
    e.sq_head_id = entry->sq_head_id;
    e.tail = entry->tail;
    unsigned tag = ansd_cqe_tag(&e);
    if (((e.tail >> 16) & 1u) != q->phase || tag >= q->depth ||
        !(q->outstanding & ((ans_u64)1 << tag))) {
        c->stats.invalid_cqes++;
        return fault(c, ANSD_FAULT);
    }
    w32(c, ANSD_BANK_CTRL, ANSD_NVMMU_TCB_INVAL, tag);
    /* Advisory (Asahi warns only; m1n1 ANS004/ANS006 showed intermittent nonzero). */
    if (r32(c, ANSD_BANK_CTRL, ANSD_NVMMU_TCB_STAT)) c->stats.tcb_stat_nonzero++;
    if (++q->head == q->depth) { q->head = 0; q->phase ^= 1u; }
    w32(c, ANSD_BANK_CTRL, queue ? ANSD_DB_IOCQ : ANSD_DB_ACQ, q->head);
    q->outstanding &= ~((ans_u64)1 << tag);
    c->stats.cqes++;
    *out = e;
    return ANSD_OK;
}

static int mbox_send(struct ansd_ctrl *c, ans_u64 msg0, ans_u32 msg1)
{
    for (unsigned i = 0; i < 1000; i++) {
        if (!(r32(c, ANSD_BANK_MBOX, ANSD_MBOX_A2I_CONTROL) & ANSD_MBOX_FULL)) {
            c->ops->barrier(c->ctx);
            c->ops->write64(c->ctx, ANSD_BANK_MBOX, ANSD_MBOX_A2I_SEND0, msg0);
            c->ops->write64(c->ctx, ANSD_BANK_MBOX, ANSD_MBOX_A2I_SEND1, msg1);
            return ANSD_OK;
        }
        c->ops->stall(c->ctx, 1);
    }
    return ANSD_TIMEOUT;
}

int ansd_mbox_service(struct ansd_ctrl *c, unsigned budget)
{
    if (!c || !c->ops) return ANSD_INVALID;
    if (c->crashed) return ANSD_CRASHED;
    for (unsigned n = 0; n < budget; n++) {
        if (r32(c, ANSD_BANK_MBOX, ANSD_MBOX_I2A_CONTROL) & ANSD_MBOX_EMPTY) return ANSD_OK;
        ans_u64 msg = c->ops->read64(c->ctx, ANSD_BANK_MBOX, ANSD_MBOX_I2A_RECV0);
        ans_u32 ep = (ans_u32)c->ops->read64(c->ctx, ANSD_BANK_MBOX, ANSD_MBOX_I2A_RECV1);
        c->ops->barrier(c->ctx);
        c->stats.mbox_messages++;
        unsigned type = RTK_TYPE(msg);
        if (ep == RTK_EP_MGMT && (type == RTK_MGMT_IOP_PWR_ACK || type == RTK_MGMT_AP_PWR_ACK)) {
            if (type == RTK_MGMT_IOP_PWR_ACK) c->stats.iop_power = (ans_u32)(msg & 0xffff);
            else c->stats.ap_power = (ans_u32)(msg & 0xffff);
            c->stats.mbox_power_acks++;
        } else if ((ep == RTK_EP_SYSLOG && type == RTK_SYSLOG_LOG) ||
                   (ep == RTK_EP_IOREPORT && (type == 0x8 || type == 0xc))) {
            /* The IOP waits for this echo before reusing the entry. */
            if (mbox_send(c, msg, ep) != ANSD_OK) return fault(c, ANSD_TIMEOUT);
            c->stats.mbox_echoes++;
        } else if (ep == RTK_EP_SYSLOG && type == RTK_SYSLOG_INIT) {
            c->stats.syslog_count = (ans_u32)(msg & 0xffff);
            c->stats.syslog_size = (ans_u32)((msg >> 24) & 0xffff);
        } else if (ep == RTK_EP_CRASHLOG && type == RTK_BUFFER_REQUEST) {
            /* After boot the crashlog buffer already exists: a new request means the IOP crashed. */
            c->crashed = 1;
            c->faulted = 1;
            return ANSD_CRASHED;
        } else {
            c->stats.mbox_unknown++;
        }
    }
    return ANSD_OK;
}

int ansd_disable(struct ansd_ctrl *c)
{
    if (!c || !c->ops) return ANSD_INVALID;
    ans_u32 cc = r32(c, ANSD_BANK_CTRL, ANSD_CC);
    w32(c, ANSD_BANK_CTRL, ANSD_CC, cc & ~(ANSD_CC_EN | ANSD_CC_SHN_MASK));
    int ret = wait_csts(c, ANSD_CSTS_RDY, 0, c->cap_timeout_ms ? c->cap_timeout_ms : DEFAULT_TIMEOUT_MS);
    c->enabled = 0;
    c->q[0].outstanding = c->q[1].outstanding = 0;
    c->q[0].head = c->q[1].head = 0;
    c->q[0].phase = c->q[1].phase = 1;
    if (ret != ANSD_OK) return fault(c, ret);
    c->stage = ANSD_STAGE_DISABLED;
    return ANSD_OK;
}

int ansd_shutdown(struct ansd_ctrl *c)
{
    if (!c || !c->ops) return ANSD_INVALID;
    if (!c->enabled) return ANSD_STATE;
    c->stage = ANSD_STAGE_SHUTDOWN;
    ans_u32 cc = r32(c, ANSD_BANK_CTRL, ANSD_CC);
    w32(c, ANSD_BANK_CTRL, ANSD_CC, (cc & ~ANSD_CC_SHN_MASK) | ANSD_CC_SHN_NORMAL);
    int ret = wait_csts(c, ANSD_CSTS_SHST_MASK, ANSD_CSTS_SHST_DONE, MAX_TIMEOUT_MS);
    if (ret != ANSD_OK) return fault(c, ret);
    return ansd_disable(c);
}
