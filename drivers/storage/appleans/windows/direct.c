/* SPDX-License-Identifier: MIT */
/* ANS-DIRECT runtime (investigation/ANS_DIRECT_PLAN.md): Windows owns the ANS2 NVMe queues,
 * NVMMU TCBs, PRPs, IRQ and the post-boot IOP mailbox replies. No EL2 code in the data path. */
#include "adapter.h"
#include "request.h"

HW_TIMER AppleAnsDirectTimer;
#define DIRECT_TIMER_US 50000u
#define DIRECT_ADMIN_POLLS 100000u /* x10 us = 1 s */

static ans_u32 d_read32(void *ctx, unsigned bank, ans_u32 off)
{
    PAPPLE_ANS_ADAPTER a = ctx;
    return StorPortReadRegisterUlong(a, (PULONG)((bank ? a->Mbox : a->Ctrl) + off));
}
static void d_write32(void *ctx, unsigned bank, ans_u32 off, ans_u32 value)
{
    PAPPLE_ANS_ADAPTER a = ctx;
    StorPortWriteRegisterUlong(a, (PULONG)((bank ? a->Mbox : a->Ctrl) + off), value);
}
static ans_u64 d_read64(void *ctx, unsigned bank, ans_u32 off)
{
    PAPPLE_ANS_ADAPTER a = ctx;
    return StorPortReadRegisterUlong64(a, (PULONG64)((bank ? a->Mbox : a->Ctrl) + off));
}
static void d_write64(void *ctx, unsigned bank, ans_u32 off, ans_u64 value)
{
    PAPPLE_ANS_ADAPTER a = ctx;
    StorPortWriteRegisterUlong64(a, (PULONG64)((bank ? a->Mbox : a->Ctrl) + off), value);
}
static void d_barrier(void *ctx) { UNREFERENCED_PARAMETER(ctx); MemoryBarrier(); }
static void d_stall(void *ctx, ans_u32 us) { UNREFERENCED_PARAMETER(ctx); StorPortStallExecution(us); }
static const struct ansd_ops direct_ops = { d_read32, d_write32, d_read64, d_write64, d_barrier, d_stall };

static VOID direct_fault(PAPPLE_ANS_ADAPTER a)
{
    a->Ready = FALSE;
    a->Namespace.ready = 0;
    a->Ansd.faulted = 1;
    ans_order_fault(&a->Order);
    a->Failure = ANS_RESULT_IO_ERROR;
    a->Stage = AppleAnsStageQuarantined;
}

/* Translate one Storport common-buffer span (IPA) into a controller PA. */
static BOOLEAN span_pa(PAPPLE_ANS_ADAPTER a, PUCHAR va, ULONG bytes, ULONGLONG *pa)
{
    ULONG length = 0;
    STOR_PHYSICAL_ADDRESS ipa = StorPortGetPhysicalAddress(a, NULL, va, &length);
    if (!ipa.QuadPart || length < bytes) return FALSE;
    return ansd_translate((ans_u64)ipa.QuadPart, bytes, pa) != 0;
}

/* Lays the six queue arrays and the PRP pages over a->Common and resolves their controller
 * PAs. common_ipa == 0: live instance, Storport translates each span. Otherwise the dump
 * instance: the buffer is the live one, lent as one contiguous range starting at common_ipa.
 * Nothing in the buffer is written here (the controller may still be running on it). */
static BOOLEAN carve_pa(PAPPLE_ANS_ADAPTER a, ULONGLONG common_ipa, PUCHAR va, ULONG bytes, ULONGLONG *pa)
{
    /* Each array is one physically contiguous chunk inside one DMA window. */
    if (!(common_ipa ? ansd_translate(common_ipa + (ULONGLONG)(va - (PUCHAR)a->Common), bytes, pa) != 0
                     : span_pa(a, va, bytes, pa))) return FALSE;
    return (*pa & (bytes - 1)) == 0;
}
static BOOLEAN direct_carve(PAPPLE_ANS_ADAPTER a, ULONGLONG common_ipa, struct ansd_queue q[2])
{
    ULONG_PTR base = ((ULONG_PTR)a->Common + ANSD_QUEUE_ALIGN - 1) & ~(ULONG_PTR)(ANSD_QUEUE_ALIGN - 1);
    PUCHAR chunk[6] = {0};
    ULONGLONG chunk_pa[6] = {0};
    for (unsigned i = 0; i < 6; i++) {
        chunk[i] = (PUCHAR)(base + (ULONG_PTR)i * ANSD_QUEUE_ALIGN);
        if (!carve_pa(a, common_ipa, chunk[i], ANSD_QUEUE_ALIGN, &chunk_pa[i])) return FALSE;
    }
    PUCHAR pages = (PUCHAR)(base + 6u * ANSD_QUEUE_ALIGN);
    for (unsigned i = 0; i < APPLE_ANS_METADATA_PAGES; i++) {
        a->Pages[i] = pages + (ULONG_PTR)i * ANS_PRP_PAGE_SIZE;
        if (!carve_pa(a, common_ipa, a->Pages[i], ANS_PRP_PAGE_SIZE, &a->PageDma[i])) return FALSE;
    }
    PUCHAR bounce = pages + (ULONG_PTR)APPLE_ANS_METADATA_PAGES * ANS_PRP_PAGE_SIZE;
    for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) {
        a->BounceBuffer[i] = bounce + (ULONG_PTR)i * ANS_BOUNCE_BYTES;
        for (unsigned p = 0; p < ANS_BOUNCE_PAGES; p++)
            if (!carve_pa(a, common_ipa, a->BounceBuffer[i] + (ULONG_PTR)p * ANS_PRP_PAGE_SIZE,
                          ANS_PRP_PAGE_SIZE, &a->BouncePa[i][p])) return FALSE;
    }
    for (unsigned n = 0; n < 2; n++) {
        q[n] = (struct ansd_queue){0};
        q[n].sq = chunk[3*n]; q[n].cq = (volatile struct ansd_cqe *)chunk[3*n+1];
        q[n].tcb = (struct ansd_tcb *)chunk[3*n+2];
        q[n].sq_pa = chunk_pa[3*n]; q[n].cq_pa = chunk_pa[3*n+1]; q[n].tcb_pa = chunk_pa[3*n+2];
        q[n].depth = n ? ANSD_IO_DEPTH : ANSD_ADMIN_DEPTH;
    }
    return TRUE;
}

BOOLEAN AppleAnsDirectResources(PAPPLE_ANS_ADAPTER a, PPORT_CONFIGURATION_INFORMATION config)
{
    PACCESS_RANGE r = *config->AccessRanges;
    a->Ctrl = StorPortGetDeviceBase(a, config->AdapterInterfaceType, config->SystemIoBusNumber,
                                    r[0].RangeStart, r[0].RangeLength, FALSE);
    a->Mbox = StorPortGetDeviceBase(a, config->AdapterInterfaceType, config->SystemIoBusNumber,
                                    r[1].RangeStart, r[1].RangeLength, FALSE);
    if (!a->Ctrl || !a->Mbox) goto failed;
    config->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;
    /* Dump instance: no allocation, the validated record names the live buffer; mapping the
     * registers is all FindAdapter does with the hardware. */
    if (a->DumpMode) { a->Common = (PVOID)(ULONG_PTR)a->DumpContext.common_va; a->Direct = TRUE; return TRUE; }
    a->Common = StorPortGetUncachedExtension(a, config, APPLE_ANS_DIRECT_BYTES);
    if (!a->Common) goto failed;
    ULONG_PTR base = ((ULONG_PTR)a->Common + ANSD_QUEUE_ALIGN - 1) & ~(ULONG_PTR)(ANSD_QUEUE_ALIGN - 1);
    RtlZeroMemory((PVOID)base, APPLE_ANS_DIRECT_BYTES - ANSD_QUEUE_ALIGN);
    struct ansd_queue q[2];
    if (!direct_carve(a, 0, q) || ansd_init(&a->Ansd, &direct_ops, a, q) != ANSD_OK) goto failed;
    a->Direct = TRUE;
    return TRUE;
failed:
    /* Nothing was published to the controller; Storport owns the common buffer. */
    if (a->Ctrl) StorPortFreeDeviceBase(a, a->Ctrl);
    if (a->Mbox) StorPortFreeDeviceBase(a, a->Mbox);
    a->Ctrl = a->Mbox = NULL;
    return FALSE;
}

static VOID put(ans_u8 *p, ans_u64 value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (ans_u8)(value >> (8*i));
}
/* Initialization/reset only: one admin command, bounded polling, mailbox serviced. */
static BOOLEAN direct_admin(PAPPLE_ANS_ADAPTER a, UCHAR opcode, ULONG nsid, ULONG cdw10,
    ULONG cdw11, ULONGLONG prp1, ULONG *dw0)
{
    ans_u8 sqe[64] = {0};
    struct ansd_cqe e;
    sqe[0] = opcode;
    put(sqe + 4, nsid, 4);
    put(sqe + 24, prp1, 8);
    put(sqe + 40, cdw10, 4);
    put(sqe + 44, cdw11, 4);
    /* Asahi TCB rule: no PRP -> none; NVMe opcode bit 0 (host-to-controller) -> TO_DEVICE. */
    unsigned flags = !prp1 ? ANSD_DMA_NONE : (opcode & 1) ? ANSD_DMA_TO_DEVICE : ANSD_DMA_FROM_DEVICE;
    if (ansd_submit(&a->Ansd, 0, 0, sqe, flags) != ANSD_OK) return FALSE;
    ans_stats_submit(&a->Stats, 0, opcode);
    for (unsigned i = 0; i < DIRECT_ADMIN_POLLS; i++) {
        int ret = ansd_poll(&a->Ansd, 0, &e);
        if (ret == ANSD_OK) {
            BOOLEAN ok = ansd_cqe_status(&e) == 0;
            ans_stats_add(&a->Stats, ANS_TELEM_CQES, 1);
            ans_stats_complete(&a->Stats, 0, opcode, 0, ok);
            if (dw0) *dw0 = e.result;
            return ok;
        }
        if (ret != ANSD_EMPTY || ansd_mbox_service(&a->Ansd, 8) == ANSD_CRASHED) break;
        StorPortStallExecution(10);
    }
    ans_stats_add(&a->Stats, ANS_TELEM_TIMEOUTS, 1);
    direct_fault(a); /* A lost admin command keeps its DMA ownership: never reuse the tag. */
    return FALSE;
}

static BOOLEAN direct_create_io(PAPPLE_ANS_ADAPTER a)
{
    a->Stage = AppleAnsStageCreateCq;
    /* Asahi: Create IOCQ PC|IEN vector 0, then IOSQ bound to CQ 1. */
    if (!direct_admin(a, 5, 0, ((ANSD_IO_DEPTH - 1) << 16) | 1, 3, a->Ansd.q[1].cq_pa, NULL)) return FALSE;
    a->Stage = AppleAnsStageCreateSq;
    return direct_admin(a, 1, 0, ((ANSD_IO_DEPTH - 1) << 16) | 1, 0x10001, a->Ansd.q[1].sq_pa, NULL);
}

BOOLEAN AppleAnsDirectInitialize(PAPPLE_ANS_ADAPTER a)
{
    ULONG dw0 = 0;
    ans_stats_add(&a->Stats, ANS_TELEM_STARTS, 1);
    a->Ready = FALSE;
    a->Namespace.ready = 0;
    a->ShuttingDown = FALSE; a->ShutdownTag = ANS_QUEUE_DEPTH;
    if (!a->Direct || !a->Ctrl || !a->Mbox || a->Ansd.faulted || a->Order.faulted) return FALSE;
    a->Stage = AppleAnsStageAdminReady;
    a->MayOwn = TRUE;
    if (ansd_enable(&a->Ansd) != ANSD_OK) goto failure;
    a->LastCheckpoint = a->Stage;
    a->Stage = AppleAnsStageIdentifyController;
    RtlZeroMemory(a->Pages[ANS_QUEUE_DEPTH], 2 * ANS_PRP_PAGE_SIZE);
    if (!direct_admin(a, 6, 0, 1, 0, a->PageDma[ANS_QUEUE_DEPTH], NULL)) goto failure;
    a->Stage = AppleAnsStageIdentifyNamespace;
    if (!direct_admin(a, 6, 1, 0, 0, a->PageDma[ANS_QUEUE_DEPTH + 1], NULL) ||
        !ans_identify_namespace(a->Pages[ANS_QUEUE_DEPTH], a->Pages[ANS_QUEUE_DEPTH + 1], &a->Namespace))
        goto failure;
    /* UNMAP is advertised from ONCS alone. A controller without Dataset Management is
     * recorded once per start so an absent TRIM can be told from an unread VPD page. */
    if (!a->Namespace.deallocate)
        AppleAnsDiag(a, NULL, 0xA8, (ULONG)a->Pages[ANS_QUEUE_DEPTH][520] |
                                    ((ULONG)a->Pages[ANS_QUEUE_DEPTH][521] << 8));
    /* Policy: OFF withdraws the capability; the effective mode is recorded once per start. */
    a->Trim.oncs = (ULONG)a->Pages[ANS_QUEUE_DEPTH][520] | ((ULONG)a->Pages[ANS_QUEUE_DEPTH][521] << 8);
    if (a->Trim.config.mode == ANS_TRIM_OFF) a->Namespace.deallocate = 0;
    AppleAnsDiag(a, NULL, 0xA9, (a->Trim.config.mode << 16) | a->Trim.oncs);
    a->Stage = AppleAnsStageCache;
    if (a->Pages[ANS_QUEUE_DEPTH][525] & 1) {
        if (!direct_admin(a, 0x0a, 0, 6, 0, 0, &dw0) || (dw0 & ~1u)) goto failure;
        a->Namespace.write_cache_enabled = (ans_u8)(dw0 & 1);
    }
    a->Namespace.read_cache_disabled = 0;
    a->Namespace.cache_known = 1;
    if (!direct_create_io(a)) goto failure;
    /* An ordinary storage driver: Windows decides which sectors it writes. */
    a->Namespace.read_only = 0;
    a->Namespace.tagged_queuing = 1;
    a->Namespace.ready = 1;
    a->Claimed = TRUE;
    a->Ready = TRUE;
    a->Stage = AppleAnsStageReady;
    StorPortNotification(RequestTimerCall, a, AppleAnsDirectTimer, DIRECT_TIMER_US);
    return TRUE;
failure:
    a->FailureStage = a->Stage;
    a->Failure = ANS_RESULT_IO_ERROR;
    if (!a->Ansd.crashed && a->Ansd.enabled) (void)ansd_disable(&a->Ansd);
    direct_fault(a);
    return FALSE;
}

/* Dump instance, DUMP_MODE_CRASH (ANS-DUMP-MODE.md, TRANSLATION 3). The live instance is dead
 * but its controller may still be running on the lent queue memory, so the controller is
 * stopped first (ansd_init_takeover) and only then are the arrays cleared and reused. The
 * namespace comes from the validated record: no Identify, no feature changes, no timer. */
BOOLEAN AppleAnsDirectDumpInitialize(PAPPLE_ANS_ADAPTER a)
{
    const struct ans_dump_context *c = &a->DumpContext;
    struct ansd_queue q[2];
    ans_stats_add(&a->Stats, ANS_TELEM_STARTS, 1);
    a->Ready = FALSE;
    a->ShuttingDown = FALSE; a->ShutdownTag = ANS_QUEUE_DEPTH;
    a->Namespace = (struct ans_namespace){0};
    if (!a->Direct || !a->Ctrl || !a->Mbox || !a->Common || !direct_carve(a, c->common_pa, q)) return FALSE;
    a->Stage = AppleAnsStageAdminReady;
    a->MayOwn = TRUE;
    if (ansd_init_takeover(&a->Ansd, &direct_ops, a, q) != ANSD_OK || ansd_enable(&a->Ansd) != ANSD_OK ||
        !direct_create_io(a)) {
        a->FailureStage = a->Stage;
        direct_fault(a);
        return FALSE;
    }
    a->Namespace.blocks = c->blocks; a->Namespace.block_size = c->block_size;
    a->Namespace.nsid = c->nsid; a->Namespace.read_only = (ans_u8)c->read_only;
    /* The live instance left the volatile write cache as it was: report it enabled so the
     * dump stack keeps flushing. INQUIRY strings are fixed (no Identify data here). */
    a->Namespace.cache_known = 1; a->Namespace.write_cache_enabled = 1;
    RtlCopyMemory(a->Namespace.vendor, "NVMe    ", 8);
    RtlCopyMemory(a->Namespace.product, "APPLE SSD (DUMP)", 16);
    RtlCopyMemory(a->Namespace.revision, "DUMP", 4);
    a->Namespace.ready = 1;
    a->Claimed = TRUE;
    a->Ready = TRUE;
    a->Stage = AppleAnsStageReady;
    return TRUE;
}

static VOID direct_retire(PAPPLE_ANS_ADAPTER a, UCHAR status);
/* Dump instance: interrupts are off, so StartIo finishes what it started by polling. Time is
 * counted in stall iterations (no clock at HIGH_LEVEL). A request that does not complete in
 * DIRECT_DUMP_POLLS x 10 us faults the adapter and fails every outstanding SRB; nothing is
 * freed, so a controller that still owns the buffers cannot corrupt reused memory. */
#define DIRECT_DUMP_POLLS 3000000u
VOID AppleAnsDirectDumpDrain(PAPPLE_ANS_ADAPTER a)
{
    for (unsigned polls = 0; !a->Ansd.faulted; polls++) {
        unsigned tag;
        (void)AppleAnsDirectInterrupt(a);
        for (tag = 0; tag < ANS_QUEUE_DEPTH && !a->Slots[tag].Busy; tag++) {}
        if (tag == ANS_QUEUE_DEPTH) return;
        if (polls == DIRECT_DUMP_POLLS) { ans_stats_add(&a->Stats, ANS_TELEM_TIMEOUTS, 1); direct_fault(a); break; }
        StorPortStallExecution(10);
    }
    direct_retire(a, SRB_STATUS_ERROR);
}

/* StartIo/ISR under InterruptLock: publish the already validated SQE. */
int AppleAnsDirectSubmit(PAPPLE_ANS_ADAPTER a, unsigned tag, const struct ans_broker_request *r)
{
    unsigned flags = r->direction == ANS_DMA_TO_DEVICE ? ANSD_DMA_TO_DEVICE :
                     r->direction == ANS_DMA_FROM_DEVICE ? ANSD_DMA_FROM_DEVICE : ANSD_DMA_NONE;
    return ansd_submit(&a->Ansd, 1, tag, r->sqe, flags);
}

static VOID complete_tag(PAPPLE_ANS_ADAPTER a, const struct ansd_cqe *e)
{
    unsigned tag = ansd_cqe_tag(e);
    USHORT status = (USHORT)(e->tail >> 16); /* NVMe status with phase in bit 0 */
    APPLE_ANS_SLOT *slot = &a->Slots[tag];
    if (tag < ANS_ADMIN_TAGS || !slot->Busy || !slot->Srb ||
        ans_order_complete(&a->Order, slot->Token) != ANS_ORDER_OK) {
        ans_stats_add(&a->Stats, ANS_TELEM_INVALID_TAG, 1);
        direct_fault(a);
        return;
    }
    ans_stats_add(&a->Stats, ANS_TELEM_CQES, 1);
    ans_stats_complete(&a->Stats, 1, slot->Request.sqe[0], slot->Length, (status >> 1) == 0);
    if (status >> 1) AppleAnsDiag(a, slot->Srb, 0xA4, ((ULONG)(status >> 1) << 8) | slot->Request.sqe[0]);
    if (slot->Request.sqe[0] == ANS_NVME_DSM) ans_trim_done(&a->Trim, (ULONG)(status >> 1));
    if (slot->Shutdown) {
        a->ShutdownTag = tag; a->ShutdownFlushOk = (status >> 1) == 0;
        (void)StorPortIssueDpc(a, &a->ShutdownDpc, NULL, NULL);
        return;
    }
    PSCSI_REQUEST_BLOCK srb = slot->Srb;
    ULONG length = slot->Length;
    *slot = (APPLE_ANS_SLOT){0};
    MemoryBarrier();
    AppleAnsFinish(a, srb, status, length);
}

BOOLEAN AppleAnsDirectInterrupt(PAPPLE_ANS_ADAPTER a)
{
    struct ansd_cqe e;
    BOOLEAN claimed = FALSE;
    ans_stats_add(&a->Stats, ANS_TELEM_ISR_ENTRIES, 1);
    if (!a->Direct || !a->Ansd.enabled || a->Ansd.faulted) {
        ans_stats_add(&a->Stats, ANS_TELEM_SPURIOUS_IRQS, 1);
        return FALSE;
    }
    /* Level IRQ: drain the whole CQ (bounded by one ring) before returning. */
    for (unsigned i = 0; i < ANSD_IO_DEPTH && !a->Ansd.faulted; i++) {
        int ret = ansd_poll(&a->Ansd, 1, &e);
        if (ret == ANSD_EMPTY) break;
        claimed = TRUE;
        if (ret != ANSD_OK) {
            ans_stats_add(&a->Stats, ANS_TELEM_INVALID_CQE, 1);
            direct_fault(a);
            break;
        }
        complete_tag(a, &e);
    }
    if (ansd_mbox_service(&a->Ansd, 16) == ANSD_CRASHED) direct_fault(a);
    if (!claimed) ans_stats_add(&a->Stats, ANS_TELEM_SPURIOUS_IRQS, 1);
    a->Stats.tcb_inval_failures = a->Ansd.stats.tcb_stat_nonzero;
    AppleAnsDispatch(a);
    return claimed;
}

_Use_decl_annotations_
VOID AppleAnsDirectTimer(PVOID DeviceExtension)
{
    PAPPLE_ANS_ADAPTER a = DeviceExtension;
    STOR_LOCK_HANDLE lock = {0};
    /* Full duplex: Storport holds no lock here; InterruptLock is allowed (AcquireSpinLock table). */
    StorPortAcquireSpinLock(a, InterruptLock, NULL, &lock);
    BOOLEAN rearm = a->Direct && a->Ansd.enabled && !a->Ansd.faulted;
    if (rearm) {
        if (ansd_mbox_service(&a->Ansd, 32) == ANSD_CRASHED) { direct_fault(a); rearm = FALSE; }
    }
    /* Missed-interrupt recovery: a CQE that has waited a whole timer period was not delivered
     * by the level interrupt. Reap it here (same drain as the ISR) instead of leaving the
     * request to a Storport timeout and a controller reset. Counted and logged (0xAA). */
    if (rearm && !a->DumpMode && ansd_overdue(&a->Ansd, 1)) {
        ans_u64 before = a->Ansd.stats.cqes;
        (void)AppleAnsDirectInterrupt(a);
        a->Ansd.stats.poll_recovered += a->Ansd.stats.cqes - before;
        AppleAnsDiag(a, NULL, 0xAA, (ULONG)(a->Ansd.stats.poll_recovered & 0xffffff));
        rearm = a->Direct && a->Ansd.enabled && !a->Ansd.faulted;
    }
    StorPortReleaseSpinLock(a, &lock);
    if (rearm) StorPortNotification(RequestTimerCall, a, AppleAnsDirectTimer, DIRECT_TIMER_US);
}

static VOID direct_retire(PAPPLE_ANS_ADAPTER a, UCHAR status)
{
    (void)ans_order_reset(&a->Order, 1);
    ans_stats_stopped(&a->Stats, 0);
    for (unsigned tag = 0; tag < ANS_QUEUE_DEPTH; tag++) {
        PSCSI_REQUEST_BLOCK srb = a->Slots[tag].Srb;
        a->Slots[tag] = (APPLE_ANS_SLOT){0};
        if (srb) AppleAnsComplete(a, srb, status, 0);
    }
    a->Claimed = FALSE;
    a->ShuttingDown = FALSE; a->ShutdownTag = ANS_QUEUE_DEPTH;
}

/* Normal NVMe shutdown proves the controller stopped all DMA (CSTS.SHST complete, RDY=0). */
BOOLEAN AppleAnsDirectStop(PAPPLE_ANS_ADAPTER a)
{
    ans_stats_add(&a->Stats, ANS_TELEM_STOPS, 1);
    a->Ready = FALSE; a->Namespace.ready = 0;
    if (!a->MayOwn) { a->Stage = AppleAnsStageStopped; return TRUE; }
    if (a->Ansd.enabled && ansd_shutdown(&a->Ansd) != ANSD_OK) {
        /* Without stop proof the controller may still DMA into our buffers: keep them. */
        direct_fault(a);
        return FALSE;
    }
    direct_retire(a, SRB_STATUS_NO_DEVICE);
    a->MayOwn = FALSE;
    a->Stage = AppleAnsStageStopped;
    return TRUE;
}

BOOLEAN AppleAnsDirectReset(PAPPLE_ANS_ADAPTER a)
{
    ans_stats_add(&a->Stats, ANS_TELEM_RESETS, 1);
    AppleAnsDiag(a, NULL, 0xA5, (ULONG)(a->Ansd.q[1].outstanding & 0xffffff));
    a->Ready = FALSE; a->Namespace.ready = 0; a->Stage = AppleAnsStageReset;
    if (!a->Direct || a->Ansd.crashed) return FALSE;
    /* CC.EN=0 with RDY=0 is the controller's stop proof; only then are SRBs failed. */
    if (ansd_disable(&a->Ansd) != ANSD_OK) { direct_fault(a); return FALSE; }
    direct_retire(a, SRB_STATUS_BUS_RESET);
    struct ansd_queue q[2] = { a->Ansd.q[0], a->Ansd.q[1] };
    if (ansd_init(&a->Ansd, &direct_ops, a, q) != ANSD_OK) { direct_fault(a); return FALSE; }
    (void)ans_order_init(&a->Order, 1);
    a->MayOwn = FALSE;
    return AppleAnsDirectInitialize(a);
}

VOID AppleAnsDirectPeer(PAPPLE_ANS_ADAPTER a, struct ans_telemetry *peer)
{
    /* Direct-runtime record in the second telemetry slot (TELEMETRY.md, source 3). */
    ans_stats_init(peer);
    peer->source = 3;
    peer->generation = 0x544345524944ULL; /* "DIRECT" */
    peer->valid_fields = ANS_TELEM_PHYSICAL_IRQS | ANS_TELEM_GUEST_IRQ_INJECTIONS |
        ANS_TELEM_BROKER_REJECTIONS | ANS_TELEM_TCB_INVAL_FAILURES | ANS_TELEM_CQES |
        ANS_TELEM_INVALID_CQE | ANS_TELEM_POLL_RECOVERED_COMPLETIONS;
    peer->poll_recovered_completions = a->Ansd.stats.poll_recovered;
    peer->physical_irqs = a->Ansd.stats.mbox_messages;
    peer->guest_irq_injections = a->Ansd.stats.mbox_echoes;
    peer->broker_rejections = a->Ansd.stats.mbox_unknown;
    peer->tcb_inval_failures = a->Ansd.stats.tcb_stat_nonzero;
    peer->cqes = a->Ansd.stats.cqes;
    peer->invalid_cqe = a->Ansd.stats.invalid_cqes;
    /* Evidence fields for a stalled adapter, all read here (no I/O-path bookkeeping added):
     * current_outstanding = bitmap of I/O tags the engine still waits for, max_outstanding = the
     * same for the admin queue, starts = I/O CQ head | phase << 32, timeouts = live CSTS | CC << 32,
     * bytes_read / bytes_written = last unanswered mailbox message and its endpoint. */
    peer->valid_fields |= ANS_TELEM_CURRENT_OUTSTANDING | ANS_TELEM_MAX_OUTSTANDING | ANS_TELEM_STARTS |
        ANS_TELEM_TIMEOUTS | ANS_TELEM_BYTES_READ | ANS_TELEM_BYTES_WRITTEN;
    peer->current_outstanding = a->Ansd.q[1].outstanding;
    peer->max_outstanding = a->Ansd.q[0].outstanding;
    peer->starts = (ans_u64)a->Ansd.q[1].head | ((ans_u64)a->Ansd.q[1].phase << 32);
    peer->timeouts = (a->Ctrl && !a->DumpMode) ?
        ((ans_u64)StorPortReadRegisterUlong(a, (PULONG)(a->Ctrl + ANSD_CSTS)) |
         ((ans_u64)StorPortReadRegisterUlong(a, (PULONG)(a->Ctrl + ANSD_CC)) << 32)) : 0;
    peer->bytes_read = a->Ansd.stats.last_unknown_msg;
    peer->bytes_written = a->Ansd.stats.last_unknown_ep;
    peer->last_stage = a->Ansd.stage;
    peer->last_error = (ans_u32)((a->Ansd.crashed ? 2u : 0u) | (a->Ansd.faulted ? 1u : 0u));
}
