/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_DIRECT_H
#define APPLE_ANS_DIRECT_H
/* ANS-DIRECT: the Windows driver owns the ANS2 NVMe runtime (investigation/ANS_DIRECT_PLAN.md).
 * Register map and sequencing: m1n1 nvme.c/asc.c/rtkit.c (MIT, this tree) and the
 * observable behaviour of Asahi drivers/nvme/host/apple.c (GPL; read, nothing copied).
 * m1n1 hands the controller over once at ExitBootServices: RTKit booted,
 * BOOT_STATUS ok, CC.EN=0/CSTS.RDY=0, no queues. EL2 never touches ANS afterwards. */
#include "ans_scsi.h"

/* Register banks: 0 = NVMe controller (0x27bcc0000), 1 = ASC mailbox (0x277408000). */
#define ANSD_BANK_CTRL 0u
#define ANSD_BANK_MBOX 1u
#define ANSD_CTRL_BASE 0x27bcc0000ULL
#define ANSD_CTRL_SIZE 0x40000u
#define ANSD_MBOX_BASE 0x277408000ULL
#define ANSD_MBOX_SIZE 0x4000u

#define ANSD_CAP 0x00u
#define ANSD_CC 0x14u
#define ANSD_CSTS 0x1cu
#define ANSD_AQA 0x24u
#define ANSD_ASQ 0x28u
#define ANSD_ACQ 0x30u
#define ANSD_DB_ACQ 0x1004u
#define ANSD_DB_IOCQ 0x100cu
#define ANSD_MAX_PEND 0x1210u
#define ANSD_BOOT_STATUS 0x1300u
#define ANSD_BOOT_STATUS_OK 0xde71ce55u
#define ANSD_LINEAR_SQ_CTRL 0x24908u
#define ANSD_DB_LINEAR_ASQ 0x2490cu
#define ANSD_DB_LINEAR_IOSQ 0x24910u
#define ANSD_NVMMU_NUM 0x28100u
#define ANSD_NVMMU_ASQ_TCB 0x28108u
#define ANSD_NVMMU_IOSQ_TCB 0x28110u
#define ANSD_NVMMU_TCB_INVAL 0x28118u
#define ANSD_NVMMU_TCB_STAT 0x28120u

#define ANSD_CC_EN 0x1u
#define ANSD_CC_SHN_MASK (3u << 14)
#define ANSD_CC_SHN_NORMAL (1u << 14)
#define ANSD_CC_IOSQES_IOCQES ((6u << 16) | (4u << 20))
#define ANSD_CSTS_RDY 0x1u
#define ANSD_CSTS_CFS 0x2u
#define ANSD_CSTS_SHST_MASK (3u << 2)
#define ANSD_CSTS_SHST_DONE (2u << 2)

#define ANSD_MBOX_A2I_CONTROL 0x110u
#define ANSD_MBOX_I2A_CONTROL 0x114u
#define ANSD_MBOX_FULL (1u << 16)
#define ANSD_MBOX_EMPTY (1u << 17)
#define ANSD_MBOX_A2I_SEND0 0x800u
#define ANSD_MBOX_A2I_SEND1 0x808u
#define ANSD_MBOX_I2A_RECV0 0x830u
#define ANSD_MBOX_I2A_RECV1 0x838u

/* Guest DMA view (config/j313-guest-layout.json, m1n1 hv_autonomous_runtime map_stage2):
 * guest RAM is identity; the low window IPA is backed elsewhere and the alias backing itself
 * is never a valid Windows DMA address. RAM starts at phys_base 0x850000000 (legacy) or at
 * 0x818000000 with run_uefi --guest-ram-profile expanded (m1n1 guest_memory.expanded_ram_base,
 * identity load_raw(memory_base)); the identity range covers both. Addresses Windows does not
 * own can never appear in its SG lists, so the wider range adds no aliasing. */
#define ANSD_LOW_IPA 0x100000ULL
#define ANSD_LOW_PA 0x8a0100000ULL
#define ANSD_LOW_SIZE 0x3ff00000ULL
#define ANSD_RAM_BASE 0x818000000ULL
#define ANSD_RAM_END 0xa00000000ULL

#define ANSD_ADMIN_DEPTH 2u   /* Asahi APPLE_NVME_AQ_DEPTH */
#define ANSD_IO_DEPTH 64u     /* t8103 max queue depth, tag == linear SQ slot */
#define ANSD_QUEUE_ALIGN 16384u
#define ANSD_DMA_NONE 0u
#define ANSD_DMA_FROM_DEVICE 1u
#define ANSD_DMA_TO_DEVICE 2u

struct ansd_tcb {
    ans_u8 opcode, dma_flags, command_id, unk0;
    ans_u32 length;
    ans_u64 unk1[2], prp1, prp2, unk2[2];
    ans_u8 aes_iv[8], aes_unk[64];
};
typedef char ansd_tcb_size_check[(sizeof(struct ansd_tcb) == 128) ? 1 : -1];
struct ansd_cqe {
    ans_u32 result, result_hi, sq_head_id, tail;
    /* tail: [15:0] command id, [16] phase, [31:17] status. */
};
typedef char ansd_cqe_size_check[(sizeof(struct ansd_cqe) == 16) ? 1 : -1];

struct ansd_ops {
    ans_u32 (*read32)(void *ctx, unsigned bank, ans_u32 offset);
    void (*write32)(void *ctx, unsigned bank, ans_u32 offset, ans_u32 value);
    ans_u64 (*read64)(void *ctx, unsigned bank, ans_u32 offset);
    void (*write64)(void *ctx, unsigned bank, ans_u32 offset, ans_u64 value);
    void (*barrier)(void *ctx);           /* full DMA/MMIO ordering barrier */
    void (*stall)(void *ctx, ans_u32 us); /* initialization/shutdown polling only */
};
struct ansd_queue {
    ans_u8 *sq;                /* depth * 64 bytes */
    volatile struct ansd_cqe *cq; /* depth entries */
    struct ansd_tcb *tcb;      /* depth entries */
    ans_u64 sq_pa, cq_pa, tcb_pa;
    ans_u32 depth, head, phase;
    ans_u64 outstanding;
};
struct ansd_stats {
    ans_u64 mbox_messages, mbox_echoes, mbox_unknown, mbox_power_acks;
    ans_u64 tcb_stat_nonzero, cqes, invalid_cqes;
    ans_u32 iop_power, ap_power, syslog_count, syslog_size;
};
struct ansd_ctrl {
    const struct ansd_ops *ops;
    void *ctx;
    struct ansd_queue q[2]; /* 0 admin, 1 I/O */
    struct ansd_stats stats;
    ans_u32 cap_timeout_ms, last_csts, stage;
    int enabled, crashed, faulted;
};
enum ansd_result {
    ANSD_OK = 0, ANSD_EMPTY = 1, ANSD_INVALID = -1, ANSD_BUSY = -2,
    ANSD_TIMEOUT = -3, ANSD_STATE = -4, ANSD_FAULT = -5, ANSD_CRASHED = -6
};
enum ansd_stage {
    ANSD_STAGE_NONE, ANSD_STAGE_HANDOFF, ANSD_STAGE_CONFIG, ANSD_STAGE_ENABLE,
    ANSD_STAGE_READY, ANSD_STAGE_SHUTDOWN, ANSD_STAGE_DISABLED
};

/* IPA -> PA. Rejects zero length, wrap, ranges crossing a window edge, the alias backing,
 * and anything outside guest RAM / the low window. */
int ansd_translate(ans_u64 ipa, ans_u64 bytes, ans_u64 *pa);
/* Translates every logical page; page[0] keeps its offset. Output zero on failure. */
int ansd_translate_prps(const struct ans_logical_prps *in, struct ans_logical_prps *out);

/* Queue backing must be zeroed, 16 KiB aligned (PA), and already translated. */
int ansd_init(struct ansd_ctrl *c, const struct ansd_ops *ops, void *ctx,
              const struct ansd_queue queues[2]);
/* Verifies the m1n1 hand-off state, programs linear SQ/NVMMU/admin queue, enables. */
/* Dump mode only: stop a possibly live controller (CC.EN=0, CSTS.RDY=0) before ansd_enable. */
int ansd_takeover(struct ansd_ctrl *c);
/* Dump mode only: ansd_init over queue memory the controller may still be using. Nothing in
 * that memory is written until ansd_takeover proved the controller stopped. */
int ansd_init_takeover(struct ansd_ctrl *c, const struct ansd_ops *ops, void *ctx,
                       const struct ansd_queue queues[2]);
int ansd_enable(struct ansd_ctrl *c);
/* Copies a 64-byte SQE (command id forced to tag), fills TCB[tag], doorbell = tag.
 * Caller holds the interrupt lock (Asahi missed-IRQ rule). */
int ansd_submit(struct ansd_ctrl *c, unsigned queue, unsigned tag, const ans_u8 sqe[64],
                unsigned dma_flags);
/* Consumes one CQE: phase, tag validation, NVMMU invalidate, head doorbell. */
int ansd_poll(struct ansd_ctrl *c, unsigned queue, struct ansd_cqe *out);
int ansd_peek(const struct ansd_ctrl *c, unsigned queue);
/* Drains at most budget IOP mailbox messages with the post-boot RTKit responder. */
int ansd_mbox_service(struct ansd_ctrl *c, unsigned budget);
/* NVMe normal shutdown (SHN) then CC.EN=0. Mailbox is serviced while waiting. */
int ansd_shutdown(struct ansd_ctrl *c);
/* CC.EN=0 and wait for RDY=0; queues are forgotten, outstanding must be retired by caller. */
int ansd_disable(struct ansd_ctrl *c);

static inline unsigned ansd_cqe_tag(const struct ansd_cqe *e) { return e->tail & 0xffffu; }
static inline unsigned ansd_cqe_status(const struct ansd_cqe *e) { return e->tail >> 17; }
#endif
