/* SPDX-License-Identifier: MIT */
/* Simulated ANS2 controller + ASC mailbox for the ANS-DIRECT runtime (ans_direct.c). */
#include "../ans_direct.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sim {
    ans_u32 cc, csts, boot, aqa, linear, maxpend, nvmmu_num, tcb_stat;
    ans_u64 cap, asq, acq, asq_tcb, iosq_tcb;
    ans_u32 db_acq, db_iocq, inval_count, last_inval;
    unsigned rdy_delay, writes;
    ans_u32 log[64]; unsigned nlog; /* register write order */
    /* device-side CQ producer */
    unsigned cq_tail[2], cq_phase[2];
    ans_u16 next_status;
    int autocomplete;
    /* mailbox */
    ans_u64 in0[16]; ans_u32 in1[16]; unsigned in_head, in_count;
    ans_u64 out0[16]; ans_u32 out1[16]; unsigned out_count;
    ans_u64 pending_send0;
    struct ansd_ctrl *c;
};
static void logw(struct sim *s, ans_u32 off) { if (s->nlog < 64) s->log[s->nlog++] = off; }

static void post(struct sim *s, unsigned q, unsigned tag, ans_u16 status)
{
    struct ansd_queue *qq = &s->c->q[q];
    volatile struct ansd_cqe *e = &qq->cq[s->cq_tail[q]];
    e->result = 0x1234;
    e->tail = tag | ((ans_u32)s->cq_phase[q] << 16) | ((ans_u32)status << 17);
    if (++s->cq_tail[q] == qq->depth) { s->cq_tail[q] = 0; s->cq_phase[q] ^= 1; }
}
static ans_u32 rd32(void *ctx, unsigned bank, ans_u32 off)
{
    struct sim *s = ctx;
    if (bank == ANSD_BANK_MBOX) {
        if (off == ANSD_MBOX_I2A_CONTROL) return s->in_count ? 0 : ANSD_MBOX_EMPTY;
        if (off == ANSD_MBOX_A2I_CONTROL) return s->out_count >= 16 ? ANSD_MBOX_FULL : 0;
        assert(0);
    }
    switch (off) {
    case ANSD_CC: return s->cc;
    case ANSD_CSTS:
        if ((s->cc & ANSD_CC_EN) && !(s->csts & ANSD_CSTS_RDY)) {
            if (s->rdy_delay) s->rdy_delay--; else s->csts |= ANSD_CSTS_RDY;
        }
        return s->csts;
    case ANSD_BOOT_STATUS: return s->boot;
    case ANSD_NVMMU_TCB_STAT: return s->tcb_stat;
    }
    assert(!"unexpected read32");
    return 0;
}
static ans_u64 rd64(void *ctx, unsigned bank, ans_u32 off)
{
    struct sim *s = ctx;
    if (bank == ANSD_BANK_MBOX) {
        assert(s->in_count);
        if (off == ANSD_MBOX_I2A_RECV0) return s->in0[s->in_head];
        if (off == ANSD_MBOX_I2A_RECV1) { ans_u32 v = s->in1[s->in_head]; s->in_head++; s->in_count--; return v; }
        assert(0);
    }
    assert(off == ANSD_CAP);
    return s->cap;
}
static void wr32(void *ctx, unsigned bank, ans_u32 off, ans_u32 v)
{
    struct sim *s = ctx;
    assert(bank == ANSD_BANK_CTRL);
    logw(s, off); s->writes++;
    switch (off) {
    case ANSD_CC:
        s->cc = v;
        if (!(v & ANSD_CC_EN)) s->csts &= ~(ANSD_CSTS_RDY | ANSD_CSTS_SHST_MASK);
        if ((v & ANSD_CC_SHN_MASK) == ANSD_CC_SHN_NORMAL) s->csts = (s->csts & ~ANSD_CSTS_SHST_MASK) | ANSD_CSTS_SHST_DONE;
        return;
    case ANSD_AQA: s->aqa = v; return;
    case ANSD_LINEAR_SQ_CTRL: s->linear = v; return;
    case ANSD_MAX_PEND: s->maxpend = v; return;
    case ANSD_NVMMU_NUM: s->nvmmu_num = v; return;
    case ANSD_DB_ACQ: s->db_acq = v; return;
    case ANSD_DB_IOCQ: s->db_iocq = v; return;
    case ANSD_NVMMU_TCB_INVAL: s->inval_count++; s->last_inval = v; return;
    case ANSD_DB_LINEAR_ASQ: case ANSD_DB_LINEAR_IOSQ: {
        unsigned q = off == ANSD_DB_LINEAR_IOSQ;
        /* Device reads the SQE+TCB the host published for this slot. */
        const ans_u8 *sqe = s->c->q[q].sq + v * 64u;
        assert(sqe[2] == v && s->c->q[q].tcb[v].command_id == v);
        if (s->autocomplete) post(s, q, v, s->next_status);
        return;
    }
    }
    assert(!"unexpected write32");
}
static void wr64(void *ctx, unsigned bank, ans_u32 off, ans_u64 v)
{
    struct sim *s = ctx;
    if (bank == ANSD_BANK_MBOX) {
        if (off == ANSD_MBOX_A2I_SEND0) { s->pending_send0 = v; return; }
        assert(off == ANSD_MBOX_A2I_SEND1);
        s->out0[s->out_count] = s->pending_send0; s->out1[s->out_count] = (ans_u32)v; s->out_count++;
        return;
    }
    logw(s, off);
    switch (off) {
    case ANSD_ASQ: s->asq = v; return;
    case ANSD_ACQ: s->acq = v; return;
    case ANSD_NVMMU_ASQ_TCB: s->asq_tcb = v; return;
    case ANSD_NVMMU_IOSQ_TCB: s->iosq_tcb = v; return;
    }
    assert(!"unexpected write64");
}
static void barrier(void *ctx) { (void)ctx; }
static void stall(void *ctx, ans_u32 us) { (void)ctx; (void)us; }
static const struct ansd_ops ops = { rd32, wr32, rd64, wr64, barrier, stall };

static void *zalloc(size_t n) { void *p = aligned_alloc(16384, (n + 16383) & ~(size_t)16383); assert(p); memset(p, 0, n); return p; }
static struct ansd_queue mkq(unsigned depth)
{
    struct ansd_queue q = {0};
    q.sq = zalloc(depth * 64u); q.cq = zalloc(depth * 16u); q.tcb = zalloc(depth * sizeof(struct ansd_tcb));
    q.sq_pa = (uintptr_t)q.sq; q.cq_pa = (uintptr_t)q.cq; q.tcb_pa = (uintptr_t)q.tcb; q.depth = depth;
    return q;
}
static void freeq(struct ansd_queue *q) { free(q->sq); free((void *)q->cq); free(q->tcb); }
static void inbox(struct sim *s, ans_u64 m0, ans_u32 ep) { unsigned i = s->in_head + s->in_count; s->in0[i] = m0; s->in1[i] = ep; s->in_count++; }

static void setup(struct sim *s, struct ansd_ctrl *c, struct ansd_queue qs[2])
{
    memset(s, 0, sizeof(*s));
    s->boot = ANSD_BOOT_STATUS_OK;
    s->cap = (63ULL) | (20ULL << 24); /* MQES 63, TO 20*500ms (capped), MPSMIN 0 */
    s->cq_phase[0] = s->cq_phase[1] = 1;
    s->c = c;
    qs[0] = mkq(ANSD_ADMIN_DEPTH); qs[1] = mkq(ANSD_IO_DEPTH);
    assert(ansd_init(c, &ops, s, qs) == ANSD_OK);
}

static void test_translate(void)
{
    ans_u64 pa;
    assert(ansd_translate(0x100000, 4096, &pa) && pa == 0x8a0100000ULL);
    assert(ansd_translate(0x3ffff000, 4096, &pa) && pa == 0x8a0100000ULL + (0x3ffff000ULL - 0x100000));
    assert(!ansd_translate(0x3ffff000, 8192, &pa) && pa == 0);     /* crosses the window end */
    assert(!ansd_translate(0xff000, 8192, &pa));                    /* crosses the window start */
    assert(!ansd_translate(0x0, 4096, &pa));                        /* below the window */
    assert(!ansd_translate(0x40000000, 4096, &pa));                 /* gap */
    assert(ansd_translate(0x850000000ULL, 4096, &pa) && pa == 0x850000000ULL);
    assert(ansd_translate(0x9fffff000ULL, 4096, &pa) && pa == 0x9fffff000ULL);
    assert(!ansd_translate(0x9fffff000ULL, 8192, &pa));             /* beyond DRAM */
    assert(!ansd_translate(0x8a0100000ULL, 4096, &pa));             /* alias backing */
    assert(!ansd_translate(0x8a00ff000ULL, 8192, &pa));             /* touches alias backing */
    assert(ansd_translate(0x8a00ff000ULL, 4096, &pa) && pa == 0x8a00ff000ULL);
    assert(ansd_translate(0x8e0000000ULL, 4096, &pa));              /* just after alias */
    assert(!ansd_translate(0x850000000ULL, 0, &pa));
    assert(!ansd_translate(~0ULL - 10, 4096, &pa));

    struct ans_logical_prps in = {0}, out;
    in.count = 3; in.bytes = 3 * 4096 - 512;
    in.page[0] = 0x200200; in.page[1] = 0x851000000ULL; in.page[2] = 0x3ff00000;
    assert(ansd_translate_prps(&in, &out));
    assert(out.page[0] == 0x8a0200200ULL && out.page[1] == 0x851000000ULL &&
           out.page[2] == 0x3ff00000 - 0x100000 + 0x8a0100000ULL && out.count == 3 && out.bytes == in.bytes);
    in.page[1] = 0x8a0200000ULL; /* alias backing in the middle */
    assert(!ansd_translate_prps(&in, &out) && out.count == 0 && out.page[0] == 0);
    in.page[1] = 0x851000200ULL; /* non-first page offset */
    assert(!ansd_translate_prps(&in, &out));
    in.count = 0; assert(!ansd_translate_prps(&in, &out));
}

static void test_enable_order(void)
{
    struct sim s; struct ansd_ctrl c; struct ansd_queue qs[2];
    setup(&s, &c, qs);
    s.rdy_delay = 5;
    assert(ansd_enable(&c) == ANSD_OK && c.enabled && c.stage == ANSD_STAGE_READY);
    assert(s.linear == 1 && s.maxpend == (64u | (64u << 16)) && s.nvmmu_num == 63 && s.aqa == ((1u << 16) | 1u));
    assert(s.asq == qs[0].sq_pa && s.acq == qs[0].cq_pa && s.asq_tcb == qs[0].tcb_pa && s.iosq_tcb == qs[1].tcb_pa);
    assert(s.cc == (ANSD_CC_IOSQES_IOCQES | ANSD_CC_EN));
    /* Asahi order: linear SQ, MAX_PEND, NVMMU_NUM, AQA, ASQ, ACQ, TCB bases, then CC enable last. */
    ans_u32 want[] = { ANSD_LINEAR_SQ_CTRL, ANSD_MAX_PEND, ANSD_NVMMU_NUM, ANSD_AQA, ANSD_ASQ, ANSD_ACQ,
                       ANSD_NVMMU_ASQ_TCB, ANSD_NVMMU_IOSQ_TCB, ANSD_CC };
    assert(s.nlog == sizeof(want) / sizeof(want[0]));
    for (unsigned i = 0; i < s.nlog; i++) assert(s.log[i] == want[i]);
    assert(c.cap_timeout_ms == 5000);
    assert(ansd_enable(&c) == ANSD_STATE); /* never twice */
    freeq(&qs[0]); freeq(&qs[1]);
}

static void test_handoff_contract(void)
{
    struct sim s; struct ansd_ctrl c; struct ansd_queue qs[2];
    /* Each violation of the m1n1 hand-off state faults before any register write. */
    for (int k = 0; k < 6; k++) {
        setup(&s, &c, qs);
        if (k == 0) s.boot = 0;
        if (k == 1) s.cc = ANSD_CC_EN;
        if (k == 2) s.csts = ANSD_CSTS_RDY;
        if (k == 3) s.csts = ANSD_CSTS_CFS;
        if (k == 4) s.cap |= 1ULL << 48;        /* MPSMIN 8 KiB */
        if (k == 5) s.cap = (s.cap & ~0xffffULL) | 31; /* MQES too small */
        assert(ansd_enable(&c) == ANSD_STATE && c.faulted && !c.enabled && s.writes == 0);
        freeq(&qs[0]); freeq(&qs[1]);
    }
    setup(&s, &c, qs);
    s.cc = 0; s.rdy_delay = 1u << 30; s.cap &= ~(0xffULL << 24); /* TO=0 -> default 1 s, never ready */
    assert(ansd_enable(&c) == ANSD_TIMEOUT && c.faulted && !c.enabled);
    freeq(&qs[0]); freeq(&qs[1]);
    /* Bad queue backing is rejected before touching the controller. */
    qs[0] = mkq(ANSD_ADMIN_DEPTH); qs[1] = mkq(ANSD_IO_DEPTH);
    qs[1].tcb_pa += 4096;
    assert(ansd_init(&c, &ops, &s, qs) == ANSD_INVALID);
    qs[1].tcb_pa -= 4096; qs[0].depth = 64;
    assert(ansd_init(&c, &ops, &s, qs) == ANSD_INVALID);
    freeq(&qs[0]); freeq(&qs[1]);
}

static void sqe_rw(ans_u8 sqe[64], ans_u8 op, ans_u64 lba, ans_u16 nlb, ans_u64 prp1, ans_u64 prp2)
{
    memset(sqe, 0, 64);
    sqe[0] = op; sqe[2] = 0xee; sqe[4] = 1;
    for (unsigned i = 0; i < 8; i++) { sqe[24 + i] = (ans_u8)(prp1 >> (8 * i)); sqe[32 + i] = (ans_u8)(prp2 >> (8 * i)); sqe[40 + i] = (ans_u8)(lba >> (8 * i)); }
    sqe[48] = (ans_u8)nlb; sqe[49] = (ans_u8)(nlb >> 8);
}

static void test_io_path(void)
{
    struct sim s; struct ansd_ctrl c; struct ansd_queue qs[2]; ans_u8 sqe[64]; struct ansd_cqe e;
    setup(&s, &c, qs);
    assert(ansd_submit(&c, 1, 5, sqe, ANSD_DMA_NONE) == ANSD_STATE); /* not enabled */
    assert(ansd_enable(&c) == ANSD_OK);
    sqe_rw(sqe, 2, 0x123456, 31, 0x851000000ULL, 0x852000000ULL);
    assert(ansd_submit(&c, 1, 5, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_OK);
    struct ansd_tcb *t = &qs[1].tcb[5];
    assert(t->opcode == 0 && t->dma_flags == ANSD_DMA_FROM_DEVICE && t->command_id == 5 && t->length == 31 &&
           t->prp1 == 0x851000000ULL && t->prp2 == 0x852000000ULL);
    assert(qs[1].sq[5 * 64 + 2] == 5 && qs[1].sq[5 * 64 + 0] == 2 && qs[1].sq[5 * 64 + 40] == 0x56);
    assert(ansd_submit(&c, 1, 5, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_BUSY);
    assert(ansd_submit(&c, 1, 64, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_INVALID);
    assert(ansd_submit(&c, 0, 2, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_INVALID); /* admin depth 2 */
    assert(ansd_submit(&c, 1, 6, sqe, ANSD_DMA_NONE) == ANSD_INVALID);        /* PRP without direction */
    assert(ansd_poll(&c, 1, &e) == ANSD_EMPTY && !ansd_peek(&c, 1));
    post(&s, 1, 5, 0);
    assert(ansd_peek(&c, 1));
    assert(ansd_poll(&c, 1, &e) == ANSD_OK && ansd_cqe_tag(&e) == 5 && ansd_cqe_status(&e) == 0);
    assert(s.last_inval == 5 && s.inval_count == 1 && s.db_iocq == 1 && !(qs[1].outstanding & (1ULL << 5)));
    /* Phase wrap over 2.5 rings, autocompleting device, mixed tags. */
    s.autocomplete = 1;
    for (unsigned i = 0; i < 160; i++) {
        unsigned tag = (i * 7) % 64;
        assert(ansd_submit(&c, 1, tag, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_OK);
        assert(ansd_poll(&c, 1, &e) == ANSD_OK && ansd_cqe_tag(&e) == tag);
    }
    assert(s.db_iocq == (1 + 160) % 64 && qs[1].outstanding == 0);
    /* Error status passes through; TCB_STAT nonzero is advisory. */
    s.next_status = 0x2; s.tcb_stat = 1;
    assert(ansd_submit(&c, 1, 9, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_OK);
    assert(ansd_poll(&c, 1, &e) == ANSD_OK && ansd_cqe_status(&e) == 2 && c.stats.tcb_stat_nonzero == 1 && !c.faulted);
    /* Admin queue (depth 2) wraps independently. */
    s.next_status = 0;
    sqe_rw(sqe, 6, 0, 0, 0x851004000ULL, 0);
    for (unsigned i = 0; i < 5; i++) {
        assert(ansd_submit(&c, 0, i & 1, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_OK);
        assert(ansd_poll(&c, 0, &e) == ANSD_OK && ansd_cqe_tag(&e) == (i & 1));
    }
    assert(s.db_acq == 1);
    /* A CQE for a tag that is not outstanding faults the controller and stops admission. */
    s.autocomplete = 0;
    post(&s, 1, 33, 0);
    assert(ansd_poll(&c, 1, &e) == ANSD_FAULT && c.faulted && c.stats.invalid_cqes == 1);
    assert(ansd_submit(&c, 1, 1, sqe, ANSD_DMA_FROM_DEVICE) == ANSD_STATE);
    freeq(&qs[0]); freeq(&qs[1]);
}

static void test_mailbox(void)
{
    struct sim s; struct ansd_ctrl c; struct ansd_queue qs[2];
    setup(&s, &c, qs);
    ans_u64 log = (5ULL << 52) | 0x17, ior = (8ULL << 52) | 0x99, ior2 = (0xcULL << 52) | 1;
    inbox(&s, log, 2);
    inbox(&s, ior, 4);
    inbox(&s, ior2, 4);
    inbox(&s, (7ULL << 52) | 0x20, 0);   /* IOP power ack */
    inbox(&s, (8ULL << 52) | (0x80ULL << 24) | 0x40, 2); /* syslog init */
    inbox(&s, (3ULL << 52), 9);           /* unknown endpoint: counted, not echoed */
    assert(ansd_mbox_service(&c, 4) == ANSD_OK && s.in_count == 2); /* budget bound */
    assert(ansd_mbox_service(&c, 16) == ANSD_OK && s.in_count == 0);
    assert(s.out_count == 3 && s.out0[0] == log && s.out1[0] == 2 && s.out0[1] == ior && s.out1[1] == 4 && s.out0[2] == ior2);
    assert(c.stats.mbox_messages == 6 && c.stats.mbox_echoes == 3 && c.stats.mbox_unknown == 1 &&
           c.stats.iop_power == 0x20 && c.stats.syslog_count == 0x40 && c.stats.syslog_size == 0x80 && !c.faulted);
    /* Crashlog buffer request after boot: IOP crashed, adapter faulted, nothing echoed. */
    inbox(&s, (1ULL << 52) | (4ULL << 44), 1);
    assert(ansd_mbox_service(&c, 16) == ANSD_CRASHED && c.crashed && c.faulted && s.out_count == 3);
    assert(ansd_mbox_service(&c, 16) == ANSD_CRASHED);
    freeq(&qs[0]); freeq(&qs[1]);
    /* Mailbox is serviced while waiting for CSTS.RDY (m1n1 rtkit_poll during waits). */
    setup(&s, &c, qs);
    s.rdy_delay = 3; inbox(&s, log, 2);
    assert(ansd_enable(&c) == ANSD_OK && s.out_count == 1);
    freeq(&qs[0]); freeq(&qs[1]);
    /* A crash during enable aborts with ANSD_CRASHED. */
    setup(&s, &c, qs);
    s.rdy_delay = 100; inbox(&s, (1ULL << 52), 1);
    assert(ansd_enable(&c) == ANSD_CRASHED && c.faulted && !c.enabled);
    freeq(&qs[0]); freeq(&qs[1]);
}

static void test_shutdown(void)
{
    struct sim s; struct ansd_ctrl c; struct ansd_queue qs[2];
    setup(&s, &c, qs);
    assert(ansd_shutdown(&c) == ANSD_STATE);
    assert(ansd_enable(&c) == ANSD_OK);
    s.nlog = 0;
    assert(ansd_shutdown(&c) == ANSD_OK && !c.enabled && c.stage == ANSD_STAGE_DISABLED);
    assert(s.nlog == 2 && s.log[0] == ANSD_CC && s.log[1] == ANSD_CC && !(s.cc & ANSD_CC_EN) && !(s.csts & ANSD_CSTS_RDY));
    freeq(&qs[0]); freeq(&qs[1]);
}

int main(void)
{
    test_translate();
    test_enable_order();
    test_handoff_contract();
    test_io_path();
    test_mailbox();
    test_shutdown();
    puts("ans_direct tests passed");
    return 0;
}
