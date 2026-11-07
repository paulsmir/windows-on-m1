/* SPDX-License-Identifier: MIT */
#include "adapter.h"
#include "request.h"
#include <ntddscsi.h>
C_ASSERT(sizeof(SRB_IO_CONTROL)==sizeof(struct ans_io_control));
C_ASSERT(ANS_IOCTL_QUERY==CTL_CODE(FILE_DEVICE_SCSI,0x800,METHOD_BUFFERED,FILE_READ_ACCESS));

HW_DPC_ROUTINE AppleAnsShutdownDpc;

_Use_decl_annotations_
BOOLEAN AppleAnsPassiveInitialize(PVOID DeviceExtension)
{
    PAPPLE_ANS_ADAPTER a=DeviceExtension;
    StorPortInitializeDpc(a,&a->ShutdownDpc,AppleAnsShutdownDpc);
    /* One-time publication. ISR cannot issue it before this flag is set. */
    InterlockedExchange(&a->DpcReady,1);
    return TRUE;
}

static ans_u32 wire_read32(void *ctx, ans_u32 off)
{
    PAPPLE_ANS_ADAPTER a = ctx;
    return StorPortReadRegisterUlong(a, (PULONG)(a->Broker + off));
}
static ans_u64 wire_read64(void *ctx, ans_u32 off)
{
    PAPPLE_ANS_ADAPTER a = ctx;
    /* PENDING low read drains and latches; high returns the same snapshot. */
    ULONG low=StorPortReadRegisterUlong(a,(PULONG)(a->Broker+off));
    ULONG high=StorPortReadRegisterUlong(a,(PULONG)(a->Broker+off+4));
    return ((ULONGLONG)high<<32)|low;
}
static void wire_write32(void *ctx, ans_u32 off, ans_u32 value)
{
    PAPPLE_ANS_ADAPTER a = ctx;
    StorPortWriteRegisterUlong(a, (PULONG)(a->Broker + off), value);
}
static void wire_barrier(void *ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    MemoryBarrier();
}
static const struct ans_wire_ops wire_ops = {
    wire_read32, wire_read64, wire_write32, wire_barrier
};
static VOID quarantine(PAPPLE_ANS_ADAPTER a)
{
    a->Ready = FALSE;
    a->Namespace.ready = 0;
    a->Wire.faulted = 1;
    ans_order_fault(&a->Order);
    a->Failure = ANS_RESULT_IO_ERROR;
    a->Stage = AppleAnsStageQuarantined;
}
static BOOLEAN faulted(PAPPLE_ANS_ADAPTER a)
{
    return a->Direct ? (a->Ansd.faulted != 0) : (a->Wire.faulted != 0);
}
/* Platform contract: this one-way MMIO abort holds all guest vCPUs and retains
 * every DMA pin/backing allocation. It is not a successful controller stop. */
static DECLSPEC_NORETURN VOID terminal_containment(PAPPLE_ANS_ADAPTER a)
{
    quarantine(a);
    if (a->Broker) {
        MemoryBarrier();
        StorPortWriteRegisterUlong(a,(PULONG)(a->Broker+ANS_ABORT_OFFSET),a->FailureStage);
    }
    /* A returning abort is a broken platform contract. In particular never
     * return an initialization failure that lets Storport reclaim DMA memory. */
    for (;;) YieldProcessor();
}
#if APPLE_ANS_QUALIFICATION_STOP == 0 || APPLE_ANS_QUALIFICATION_STOP == 3
static VOID put(ans_u8 *p, ans_u64 value, unsigned size)
{
    for (unsigned i = 0; i < size; i++) p[i] = (ans_u8)(value >> (8*i));
}
#endif
static ans_u64 get(const ans_u8 *p, unsigned size)
{
    ans_u64 value = 0;
    for (unsigned i = 0; i < size; i++) value |= (ans_u64)p[i] << (8*i);
    return value;
}
static BOOLEAN same(const ans_u8 *a, const ans_u8 *b, unsigned size)
{
    for (unsigned i = 0; i < size; i++) if (a[i] != b[i]) return FALSE;
    return TRUE;
}
static int wire_call(PAPPLE_ANS_ADAPTER a,struct ans_broker_request *r,
    struct ans_broker_response *response)
{
    int result=ans_wire_call(&a->Wire,r,response);
    a->Stats.generation=a->Wire.generation;
    if(result==ANS_WIRE_REJECTED) {
        ans_stats_add(&a->Stats,ANS_TELEM_BROKER_REJECTIONS,1);
        if(response->result==ANS_RESULT_BAD_PRP)
            ans_stats_add(&a->Stats,ANS_TELEM_PRP_TRANSLATION_FAILURES,1);
        a->Failure=response->result;
    }
    return result;
}
static BOOLEAN telemetry_query(PAPPLE_ANS_ADAPTER a,PSCSI_REQUEST_BLOCK srb)
{
    ans_u32 sources;
    if(!ans_stats_ioctl_validate(srb->DataBuffer,srb->DataTransferLength,&sources)) {
        AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE;
    }
    struct ans_telemetry peer={0};
    if((sources&ANS_IOCTL_BROKER) && a->Direct) AppleAnsDirectPeer(a,&peer);
    else if((sources&ANS_IOCTL_BROKER) && a->Broker && a->Wire.bound &&
        (a->Wire.features&ANS_FEATURE_TELEMETRY)) {
        /* Pure software CSR. Lowest DWORD latches all 232 bytes; later words
         * do not read controller hardware or drain a completion queue. */
        for(unsigned off=0;off<sizeof(peer);off+=4) {
            ULONG value=wire_read32(a,ANS_TELEMETRY_OFFSET+off);
            RtlCopyMemory((PUCHAR)&peer+off,&value,4);
        }
    }
    a->Stats.generation=a->Wire.generation;
    a->Stats.last_stage=a->Stage;a->Stats.last_error=a->Failure;
    if(!ans_stats_ioctl_reply(srb->DataBuffer,srb->DataTransferLength,&a->Stats,&peer)) {
        AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE;
    }
    AppleAnsComplete(a,srb,SRB_STATUS_SUCCESS,ANS_IOCTL_TOTAL_BYTES);return TRUE;
}
static int operation(PAPPLE_ANS_ADAPTER a, ULONG op)
{
    struct ans_broker_request r = {0};
    struct ans_broker_response response;
    r.operation = op;
    return wire_call(a, &r, &response);
}
BOOLEAN AppleAnsNativeResources(PAPPLE_ANS_ADAPTER a, PPORT_CONFIGURATION_INFORMATION config)
{
    a->Broker = StorPortGetDeviceBase(a, config->AdapterInterfaceType,
        config->SystemIoBusNumber, a->BrokerAddress, a->BrokerLength, FALSE);
    if (!a->Broker) return FALSE;
    if (ans_wire_open(&a->Wire, &wire_ops, a) != ANS_WIRE_OK) {
        StorPortFreeDeviceBase(a, a->Broker);
        a->Broker = NULL;
        return FALSE;
    }
    /* Full64 describes validated guest DMA addresses. EL2 translates and checks
     * every page before hardware publication; Windows never programs ANS PA. */
    config->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;
    a->Common = StorPortGetUncachedExtension(a, config, APPLE_ANS_METADATA_BYTES);
    if (!a->Common) goto resources_failed;
    ULONG_PTR aligned = ((ULONG_PTR)a->Common + ANS_PRP_PAGE_SIZE - 1) &
                        ~(ULONG_PTR)(ANS_PRP_PAGE_SIZE - 1);
    for (unsigned i = 0; i < APPLE_ANS_METADATA_PAGES; i++) {
        ULONG length = 0;
        PUCHAR page = (PUCHAR)(aligned + (ULONG_PTR)i * ANS_PRP_PAGE_SIZE);
        STOR_PHYSICAL_ADDRESS dma = StorPortGetPhysicalAddress(a, NULL, page, &length);
        if (!dma.QuadPart || ((ULONGLONG)dma.QuadPart & (ANS_PRP_PAGE_SIZE - 1)) ||
            length < ANS_PRP_PAGE_SIZE) goto resources_failed;
        a->Pages[i] = page;
        a->PageDma[i] = (ULONGLONG)dma.QuadPart;
        RtlZeroMemory(page, ANS_PRP_PAGE_SIZE);
    }
    return TRUE;
resources_failed:
    /* No request has published these pages; Storport owns Common allocation. */
    StorPortFreeDeviceBase(a,a->Broker);a->Broker=NULL;a->Wire.bound=0;
    return FALSE;
}
#if APPLE_ANS_QUALIFICATION_STOP == 0 || APPLE_ANS_QUALIFICATION_STOP == 3
static BOOLEAN admin(PAPPLE_ANS_ADAPTER a, UCHAR opcode, ULONG nsid,
    ULONG cdw10, ULONG cdw11, unsigned page, ans_u8 result[16])
{
    struct ans_broker_request r = {0};
    struct ans_broker_response response;
    LARGE_INTEGER frequency, started, now;
    if (a->Slots[0].Busy || a->Wire.faulted ||
        StorPortQueryPerformanceCounter(a, &frequency, &started) != STOR_STATUS_SUCCESS ||
        frequency.QuadPart <= 0) return FALSE;
    r.operation = ANS_OP_AUTHORIZE_SUBMIT;
    r.sqe[0] = opcode;
    put(r.sqe + 4, nsid, 4);
    put(r.sqe + 40, cdw10, 4);
    put(r.sqe + 44, cdw11, 4);
    if (opcode == 6) {
        if (page >= APPLE_ANS_METADATA_PAGES) return FALSE;
        RtlZeroMemory(a->Pages[page], ANS_PRP_PAGE_SIZE);
        r.transfer_length = ANS_PRP_PAGE_SIZE;
        r.direction = ANS_DMA_FROM_DEVICE;
        put(r.sqe + 24, a->PageDma[page], 8);
    }
    a->Slots[0].Busy = TRUE;
    int ret = wire_call(a, &r, &response);
    if (ret != ANS_WIRE_OK) {
        if (ret == ANS_WIRE_REJECTED || ret == ANS_WIRE_INVALID) a->Slots[0].Busy = FALSE;
        else { ans_stats_uncertain(&a->Stats,0,opcode);quarantine(a); }
        return FALSE;
    }
    ans_stats_submit(&a->Stats,0,opcode);
    BOOLEAN timed_out=FALSE;
    /* Initialization only. Runtime StartIo/ISR never polls for a completion. */
    for (unsigned polls = 0; polls < 20000; polls++) {
        ans_u64 pending;
        if (ans_wire_pending(&a->Wire, &pending) != ANS_WIRE_OK) break;
        if (pending & ~1ULL) { ans_stats_add(&a->Stats,ANS_TELEM_INVALID_TAG,1);break; }
        if (pending & 1) {
            int cqe_result=ans_wire_cqe(&a->Wire,0,result);
            if(cqe_result!=ANS_WIRE_OK) {
                ans_stats_add(&a->Stats,ANS_TELEM_INVALID_CQE,1);
                if(cqe_result==ANS_WIRE_UNCERTAIN && (result[12] || result[13]))
                    ans_stats_add(&a->Stats,ANS_TELEM_INVALID_TAG,1);
                break;
            }
            ans_stats_add(&a->Stats,ANS_TELEM_CQES,1);
            RtlZeroMemory(&r, sizeof(r));
            r.operation = ANS_OP_REAP_COMPLETION;
            if (wire_call(a, &r, &response) != ANS_WIRE_OK ||
                !same(result, response.cqe, 16)) {
                ans_stats_add(&a->Stats,ANS_TELEM_INVALID_CQE,1);break;
            }
            a->Slots[0].Busy = FALSE;
            ans_stats_complete(&a->Stats,0,opcode,0,(get(result+14,2)>>1)==0);
            MemoryBarrier();
            return (get(result + 14, 2) >> 1) == 0;
        }
        if (StorPortQueryPerformanceCounter(a,NULL,&now)!=STOR_STATUS_SUCCESS ||
            now.QuadPart<started.QuadPart) break;
        if ((ULONGLONG)(now.QuadPart-started.QuadPart)>=(ULONGLONG)frequency.QuadPart) {
            timed_out=TRUE;break;
        }
        StorPortStallExecution(50);
        if(polls==19999) timed_out=TRUE;
    }
    if(timed_out) ans_stats_add(&a->Stats,ANS_TELEM_TIMEOUTS,1);
    quarantine(a); /* Timeout is not DMA-stop proof; retain Identify/TCB lifetime. */
    return FALSE;
}
#endif
#if APPLE_ANS_QUALIFICATION_STOP != 0
static BOOLEAN qualified_stop(PAPPLE_ANS_ADAPTER a)
{
    if (!AppleAnsNativeStop(a)) terminal_containment(a);
    a->Stage=AppleAnsStageQualificationStop;
    return FALSE;
}
#endif
BOOLEAN AppleAnsNativeInitialize(PAPPLE_ANS_ADAPTER a)
{
#if APPLE_ANS_QUALIFICATION_STOP == 0 || APPLE_ANS_QUALIFICATION_STOP == 3
    ans_u8 cqe[16];
#endif
    if (a->Direct) return AppleAnsDirectInitialize(a);
    ans_stats_add(&a->Stats,ANS_TELEM_STARTS,1);
    a->Ready = FALSE;
    a->Namespace.ready = 0;
    a->ShuttingDown=FALSE;a->ShutdownTag=ANS_QUEUE_DEPTH;
    if (!a->Broker || !a->Common || a->MayOwn || a->Order.faulted) return FALSE;
    a->Stage = AppleAnsStageQuery;
    if (ans_wire_open(&a->Wire, &wire_ops, a) != ANS_WIRE_OK ||
        operation(a, ANS_OP_QUERY) != ANS_WIRE_OK) return FALSE;
    a->LastCheckpoint=a->Stage;
    a->Stage = AppleAnsStageClaim;
    a->MayOwn = TRUE;
    int claim_result = operation(a, ANS_OP_CLAIM);
    if (claim_result != ANS_WIRE_OK) {
        if (claim_result == ANS_WIRE_UNCERTAIN) {
            a->FailureStage=a->Stage;
            if (!AppleAnsNativeStop(a)) terminal_containment(a);
        }
        else a->MayOwn = FALSE;
        return FALSE;
    }
    a->Claimed = TRUE;a->LastCheckpoint=a->Stage;
#if APPLE_ANS_QUALIFICATION_STOP == 1
    return qualified_stop(a);
#else
    a->Stage=AppleAnsStageAdminReady;
    if (operation(a, ANS_OP_REGISTER_QUEUES) != ANS_WIRE_OK) goto failure;
    a->LastCheckpoint=a->Stage;
#if APPLE_ANS_QUALIFICATION_STOP == 2
    return qualified_stop(a);
#else
    a->Stage = AppleAnsStageIdentifyController;
    if (!admin(a, 6, 0, 1, 0, ANS_QUEUE_DEPTH, cqe)) goto failure;
    a->LastCheckpoint=a->Stage;
    a->Stage = AppleAnsStageIdentifyNamespace;
    if (!admin(a, 6, 1, 0, 0, ANS_QUEUE_DEPTH + 1, cqe) ||
        !ans_identify_namespace(a->Pages[ANS_QUEUE_DEPTH], a->Pages[ANS_QUEUE_DEPTH+1],
                                &a->Namespace)) goto failure;
    a->LastCheckpoint=a->Stage;
#if APPLE_ANS_QUALIFICATION_STOP == 3
    return qualified_stop(a);
#else
    a->Stage = AppleAnsStageCache;
    if (a->Pages[ANS_QUEUE_DEPTH][525] & 1) {
        if (!admin(a, 0x0a, 0, 6, 0, 0, cqe) || (get(cqe, 4) & ~1ULL)) goto failure;
        a->Namespace.write_cache_enabled = (ans_u8)(cqe[0] & 1);
    }
    /* SNT policy: normal NVMe READ permits caching; no cache-disable translation
     * or MODE SELECT is supported. This is not an observed hardware RCD bit. */
    a->Namespace.read_cache_disabled = 0;
    a->Namespace.cache_known = 1;
    a->Stage = AppleAnsStageCreateCq;
    if (!admin(a, 5, 0, ((ANS_QUEUE_DEPTH-1) << 16) | 1, 3, 0, cqe)) goto failure;
    a->Stage = AppleAnsStageCreateSq;
    if (!admin(a, 1, 0, ((ANS_QUEUE_DEPTH-1) << 16) | 1, 0x10001, 0, cqe)) goto failure;
    a->Namespace.read_only =
        !(APPLE_ANS_NATIVE_WRITE && (a->Wire.features & ANS_FEATURE_WRITE_WINDOW));
    a->Namespace.tagged_queuing = 1;
    a->Namespace.ready = 1;
    a->Ready = TRUE;
    a->Stage = AppleAnsStageReady;
    return TRUE;
#endif /* full path versus Identify stop */
#endif /* Identify paths versus admin stop */
failure:
    a->FailureStage=a->Stage;
    a->Failure = ANS_RESULT_IO_ERROR;
    /* Explicit stop may retire pins; a failed stop preserves them. */
    if (!AppleAnsNativeStop(a)) terminal_containment(a);
    return FALSE;
#endif /* paths after the CLAIM stop */
}
VOID AppleAnsDispatch(PAPPLE_ANS_ADAPTER a);
VOID AppleAnsComplete(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb, UCHAR status, ULONG length)
{
    srb->SrbStatus = status;
    srb->DataTransferLength = length;
    StorPortNotification(RequestComplete, a, srb);
}
static VOID sense(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb, struct ans_sense value)
{
    UCHAR bytes[18];
    a->Sense = value;
    srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    UCHAR status = SRB_STATUS_ERROR;
    if (!(srb->SrbFlags & SRB_FLAGS_DISABLE_AUTOSENSE) && srb->SenseInfoBuffer &&
        srb->SenseInfoBufferLength >= sizeof(bytes)) {
        ans_scsi_fixed_sense(value, bytes);
        RtlCopyMemory(srb->SenseInfoBuffer, bytes, sizeof(bytes));
        status |= SRB_STATUS_AUTOSENSE_VALID;
    }
    AppleAnsComplete(a, srb, status, 0);
}
VOID AppleAnsDiag(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb, ULONG code, ULONG value)
{
    if (InterlockedIncrement(&a->DiagLogs) > APPLE_ANS_DIAG_MAX) return;
    StorPortLogError(a, srb, 0, 0, 0, SP_INTERNAL_ADAPTER_ERROR, (code << 24) | (value & 0xffffff));
}
VOID AppleAnsFinish(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb, USHORT status, ULONG length)
{
    if (status>>1) sense(a,srb,ans_scsi_nvme_sense(status,0));
    else { srb->ScsiStatus=SCSISTAT_GOOD;AppleAnsComplete(a,srb,SRB_STATUS_SUCCESS,length); }
}
VOID AppleAnsDispatch(PAPPLE_ANS_ADAPTER a)
{
    for (unsigned budget=0;budget<ANS_QUEUE_DEPTH && a->Ready && !faulted(a);budget++) {
        struct ans_order_token token;
        int picked=ans_order_pick(&a->Order,&token);
        if (picked==ANS_ORDER_EMPTY || picked==ANS_ORDER_BUSY) return;
        if (picked!=ANS_ORDER_OK) { quarantine(a);return; }
        APPLE_ANS_SLOT *slot=&a->Slots[token.tag];
        PSCSI_REQUEST_BLOCK srb=slot->Srb;
        if (!slot->Busy || !srb || token.epoch!=slot->Token.epoch ||
            token.sequence!=slot->Token.sequence ||
            ans_order_mark_submitted(&a->Order,token)!=ANS_ORDER_OK) {
            quarantine(a);return;
        }
        struct ans_scsi_plan plan=slot->Plan;
        if (plan.action!=ANS_SCSI_SUBMIT) {
            PAPPLE_ANS_SRB x=srb->SrbExtension;
            if (x && x->Cdb[0]==3)
                plan=ans_scsi_prepare(&a->Namespace,x->Cdb,x->CdbLength,x->Length,&a->Sense);
            if (ans_order_complete(&a->Order,token)!=ANS_ORDER_OK) { quarantine(a);return; }
            *slot=(APPLE_ANS_SLOT){0};
            if (plan.action==ANS_SCSI_CHECK) { sense(a,srb,plan.sense);continue; }
            if (plan.metadata_length) {
                if (!srb->DataBuffer || (srb->SrbFlags & SRB_FLAGS_DATA_OUT)) {
                    AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);continue;
                }
                /* Only mapped SCSI response metadata; READ payload is never copied. */
                RtlCopyMemory(srb->DataBuffer,plan.metadata,plan.metadata_length);
            }
            if (x && x->Cdb[0]==3) a->Sense=(struct ans_sense){0};
            srb->ScsiStatus=SCSISTAT_GOOD;
            AppleAnsComplete(a,srb,SRB_STATUS_SUCCESS,plan.transfer_bytes);
            continue;
        }
        struct ans_broker_response response;
        int ret;
        if (a->Direct) {
            /* Every ansd_submit failure returns before the doorbell: no hardware ownership. */
            int d=AppleAnsDirectSubmit(a,token.tag,&slot->Request);
            ret=d==ANSD_OK?ANS_WIRE_OK:ANS_WIRE_INVALID;
        } else ret=wire_call(a,&slot->Request,&response);
        if (ret==ANS_WIRE_REJECTED || ret==ANS_WIRE_INVALID) {
            /* Authenticated nonfaulted rejection, or local rejection before a
             * doorbell: the task ended without hardware owning its payload. */
            if (ans_order_complete(&a->Order,token)!=ANS_ORDER_OK) { quarantine(a);return; }
            if (slot->Shutdown) {
                a->ShutdownTag=token.tag;a->ShutdownFlushOk=FALSE;
                (void)StorPortIssueDpc(a,&a->ShutdownDpc,NULL,NULL);
            } else { *slot=(APPLE_ANS_SLOT){0};AppleAnsComplete(a,srb,SRB_STATUS_ERROR,0); }
        } else if (ret==ANS_WIRE_OK) {
            ans_stats_submit(&a->Stats,1,slot->Request.sqe[0]);
        } else {
            ans_stats_uncertain(&a->Stats,1,slot->Request.sqe[0]);
            (void)ans_order_quarantine(&a->Order,token);quarantine(a);return;
        }
    }
}
BOOLEAN AppleAnsNativeStart(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb)
{
    if(srb->Function==SRB_FUNCTION_IO_CONTROL) return telemetry_query(a,srb);
    PAPPLE_ANS_SRB x=srb->SrbExtension;
    if(srb->Function==SRB_FUNCTION_EXECUTE_SCSI && x && x->PrpFailure) {
        ans_stats_add(&a->Stats,ANS_TELEM_PRP_TRANSLATION_FAILURES,1);x->PrpFailure=FALSE;
        AppleAnsDiag(a,srb,0xA2,x->DiagValue);
        if (x->DiagCount) AppleAnsDiag(a,srb,0xA3,x->DiagCount);
    }
    struct ans_scsi_plan plan={0};
    enum ans_order_attribute attribute=ANS_ORDER_UNTAGGED;
    if (!a->Ready || faulted(a) || a->ShuttingDown || srb->PathId || srb->TargetId || srb->Lun) {
        AppleAnsComplete(a,srb,SRB_STATUS_NO_DEVICE,0);return TRUE;
    }
    if (srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE) {
        switch (srb->QueueAction) {
        case SRB_SIMPLE_TAG_REQUEST: attribute=ANS_ORDER_SIMPLE;break;
        case SRB_ORDERED_QUEUE_TAG_REQUEST: attribute=ANS_ORDER_ORDERED;break;
        case SRB_HEAD_OF_QUEUE_TAG_REQUEST: attribute=ANS_ORDER_HEAD;break;
        default: AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE;
        }
    }
    BOOLEAN shutdown=srb->Function==SRB_FUNCTION_SHUTDOWN;
    if (shutdown && !a->DpcReady) { AppleAnsComplete(a,srb,SRB_STATUS_BUSY,0);return TRUE; }
    if (srb->Function==SRB_FUNCTION_FLUSH || shutdown) {
        plan.action=ANS_SCSI_SUBMIT;plan.nsid=1;
        if (shutdown) attribute=ANS_ORDER_ORDERED; /* global shutdown fence */
    } else if (srb->Function==SRB_FUNCTION_EXECUTE_SCSI && x && x->Valid) {
        plan=ans_scsi_prepare(&a->Namespace,x->Cdb,x->CdbLength,x->Length,&a->Sense);
    } else {
        AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE;
    }
    if (plan.action==ANS_SCSI_SUBMIT && plan.nvme_opcode!=0 && plan.nvme_opcode!=2 &&
        !(plan.nvme_opcode==1 && !a->Namespace.read_only)) {
        plan=(struct ans_scsi_plan){.action=ANS_SCSI_CHECK,.sense={7,0x27,0}};
    }
    if (plan.action==ANS_SCSI_SUBMIT && plan.nvme_opcode==2 &&
        (!x || !x->Valid || !(srb->SrbFlags & SRB_FLAGS_DATA_IN) ||
         (srb->SrbFlags & SRB_FLAGS_DATA_OUT))) {
        AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE;
    }
    if (plan.action==ANS_SCSI_SUBMIT && plan.nvme_opcode==1 &&
        (!x || !x->Valid || !(srb->SrbFlags & SRB_FLAGS_DATA_OUT) ||
         (srb->SrbFlags & SRB_FLAGS_DATA_IN))) {
        AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);return TRUE;
    }
    unsigned tag;
    for(tag=ANS_ADMIN_TAGS;tag<ANS_QUEUE_DEPTH && a->Slots[tag].Busy;tag++) {}
    if(tag==ANS_QUEUE_DEPTH) { AppleAnsComplete(a,srb,SRB_STATUS_BUSY,0);return TRUE; }
    struct ans_order_token token;
    int admitted=ans_order_admit(&a->Order,tag,attribute,&token);
    if(admitted!=ANS_ORDER_OK) {
        AppleAnsComplete(a,srb,admitted==ANS_ORDER_BUSY?SRB_STATUS_BUSY:SRB_STATUS_ERROR,0);
        return TRUE;
    }
    APPLE_ANS_SLOT *slot=&a->Slots[tag];
    slot->Busy=TRUE;slot->Srb=srb;slot->Length=plan.transfer_bytes;
    slot->Plan=plan;slot->Token=token;slot->Shutdown=shutdown;
    /* Direct: controller PAs (guest DMA view translated); PageDma already holds PAs. */
    struct ans_logical_prps translated;
    const struct ans_logical_prps *prps=x?&x->Prps:NULL;
    if(a->Direct && prps && prps->count) {
        if(!ansd_translate_prps(prps,&translated)) {
            for (ans_u32 i=0;i<prps->count;i++) {
                ans_u64 pa,off=prps->page[i]&(ANS_PRP_PAGE_SIZE-1);
                if (!ansd_translate(prps->page[i],ANS_PRP_PAGE_SIZE-off,&pa) || (i && off)) {
                    AppleAnsDiag(a,srb,0xA1,(ULONG)(prps->page[i]>>12));break;
                }
            }
            prps=NULL;
        } else prps=&translated;
    }
    if(plan.action==ANS_SCSI_SUBMIT &&
        !ans_io_request(&plan,prps,tag,a->PageDma[tag],a->Pages[tag],
                        !a->Namespace.read_only,&slot->Request)) {
        ans_stats_add(&a->Stats,ANS_TELEM_PRP_TRANSLATION_FAILURES,1);
        (void)ans_order_cancel_queued(&a->Order,token);
        *slot=(APPLE_ANS_SLOT){0};AppleAnsComplete(a,srb,SRB_STATUS_INVALID_REQUEST,0);
        return TRUE;
    }
    if (shutdown) a->ShuttingDown=TRUE;
    AppleAnsDispatch(a);
    return TRUE;
}
BOOLEAN AppleAnsNativeInterrupt(PAPPLE_ANS_ADAPTER a)
{
    ans_u64 pending;
    if (a->Direct) return AppleAnsDirectInterrupt(a);
    ans_stats_add(&a->Stats,ANS_TELEM_ISR_ENTRIES,1);
    if (!a->Ready || !a->Broker || a->Wire.faulted) {
        ans_stats_add(&a->Stats,ANS_TELEM_SPURIOUS_IRQS,1);return FALSE;
    }
    if (ans_wire_pending(&a->Wire,&pending) != ANS_WIRE_OK) { quarantine(a);return FALSE; }
    if (!pending) { ans_stats_add(&a->Stats,ANS_TELEM_SPURIOUS_IRQS,1);return FALSE; }
    for (unsigned tag=0;tag<ANS_QUEUE_DEPTH;tag++) {
        if (!(pending & (1ULL<<tag))) continue;
        if (tag<ANS_ADMIN_TAGS || !a->Slots[tag].Busy || !a->Slots[tag].Srb) {
            ans_stats_add(&a->Stats,ANS_TELEM_INVALID_TAG,1);quarantine(a);break;
        }
        ans_u8 cqe[16];struct ans_broker_request r={0};struct ans_broker_response response;
        int cqe_result=ans_wire_cqe(&a->Wire,tag,cqe);
        if(cqe_result!=ANS_WIRE_OK) {
            ans_stats_add(&a->Stats,ANS_TELEM_INVALID_CQE,1);
            if(cqe_result==ANS_WIRE_UNCERTAIN && (cqe[12]!=tag || cqe[13]))
                ans_stats_add(&a->Stats,ANS_TELEM_INVALID_TAG,1);
            quarantine(a);break;
        }
        ans_stats_add(&a->Stats,ANS_TELEM_CQES,1);
        r.operation=ANS_OP_REAP_COMPLETION;r.queue=1;r.tag=(ans_u16)tag;
        if (wire_call(a,&r,&response)!=ANS_WIRE_OK || !same(cqe,response.cqe,16)) {
            ans_stats_add(&a->Stats,ANS_TELEM_INVALID_CQE,1);quarantine(a);break;
        }
        USHORT status=(USHORT)get(cqe+14,2);
        ans_stats_complete(&a->Stats,1,a->Slots[tag].Request.sqe[0],a->Slots[tag].Length,(status>>1)==0);
        if(ans_order_complete(&a->Order,a->Slots[tag].Token)!=ANS_ORDER_OK) {
            ans_stats_add(&a->Stats,ANS_TELEM_INVALID_TAG,1);quarantine(a);break;
        }
        if (a->Slots[tag].Shutdown) {
            a->ShutdownTag=tag;a->ShutdownFlushOk=(status>>1)==0;
            /* A false return means this same DPC is already queued; it consumes
             * the current pending shutdown under InterruptLock (coalesced work). */
            (void)StorPortIssueDpc(a,&a->ShutdownDpc,NULL,NULL);
            continue;
        }
        PSCSI_REQUEST_BLOCK srb=a->Slots[tag].Srb;ULONG length=a->Slots[tag].Length;
        a->Slots[tag]=(APPLE_ANS_SLOT){0};
        MemoryBarrier(); /* Payload DMA is visible before RequestComplete. */
        if (status>>1) sense(a,srb,ans_scsi_nvme_sense(status,0));
        else { srb->ScsiStatus=SCSISTAT_GOOD;AppleAnsComplete(a,srb,SRB_STATUS_SUCCESS,length); }
    }
    AppleAnsDispatch(a);
    return TRUE;
}
static VOID retire(PAPPLE_ANS_ADAPTER a, UCHAR status)
{
    (void)ans_order_reset(&a->Order,1);
    ans_stats_stopped(&a->Stats,a->Wire.generation);
    for (unsigned tag=0;tag<ANS_QUEUE_DEPTH;tag++) {
        PSCSI_REQUEST_BLOCK srb=a->Slots[tag].Srb;
        a->Slots[tag]=(APPLE_ANS_SLOT){0};
        if (srb) AppleAnsComplete(a,srb,status,0);
    }
    a->Claimed=FALSE;a->MayOwn=FALSE;
    a->ShuttingDown=FALSE;a->ShutdownTag=ANS_QUEUE_DEPTH;
}
BOOLEAN AppleAnsNativeStop(PAPPLE_ANS_ADAPTER a)
{
    if (a->Direct) return AppleAnsDirectStop(a);
    ans_stats_add(&a->Stats,ANS_TELEM_STOPS,1);
    a->Ready=FALSE;a->Namespace.ready=0;
    if (!a->MayOwn) { a->Stage=AppleAnsStageStopped;return TRUE; }
    if (operation(a,ANS_OP_QUIESCE)!=ANS_WIRE_OK || operation(a,ANS_OP_RELEASE)!=ANS_WIRE_OK) {
        terminal_containment(a);
    }
    retire(a,SRB_STATUS_NO_DEVICE);
    a->Stage=AppleAnsStageStopped;
    return TRUE;
}
BOOLEAN AppleAnsNativeReset(PAPPLE_ANS_ADAPTER a)
{
    if (a->Direct) return AppleAnsDirectReset(a);
    ans_stats_add(&a->Stats,ANS_TELEM_RESETS,1);
    a->Ready=FALSE;a->Namespace.ready=0;a->Stage=AppleAnsStageReset;
    if (!a->Wire.bound) return FALSE;
    /* Healthy CLAIMED ownership must freeze admission before service RESET.
     * FAULTED recovery is already frozen and must retain its direct reset path. */
    if (!a->Wire.faulted && a->Wire.state==ANS_STATE_CLAIMED &&
        operation(a,ANS_OP_QUIESCE)!=ANS_WIRE_OK) {
        if (a->MayOwn) terminal_containment(a);
        return FALSE;
    }
    if (operation(a,ANS_OP_RESET)!=ANS_WIRE_OK) {
        if (a->MayOwn) terminal_containment(a);
        return FALSE;
    }
    retire(a,SRB_STATUS_BUS_RESET);
    return AppleAnsNativeInitialize(a);
}

_Use_decl_annotations_
VOID AppleAnsShutdownDpc(PSTOR_DPC Dpc,PVOID HwDeviceExtension,
    PVOID SystemArgument1,PVOID SystemArgument2)
{
    PAPPLE_ANS_ADAPTER a=HwDeviceExtension;
    STOR_LOCK_HANDLE lock={0};
    UNREFERENCED_PARAMETER(Dpc);UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    StorPortAcquireSpinLock(a,InterruptLock,NULL,&lock);
    unsigned tag=a->ShutdownTag;
    if (tag<ANS_QUEUE_DEPTH && a->Slots[tag].Busy && a->Slots[tag].Shutdown &&
        a->Slots[tag].Srb) {
        PSCSI_REQUEST_BLOCK srb=a->Slots[tag].Srb;
        BOOLEAN flushed=a->ShutdownFlushOk;
        /* CQE+REAP already retired hardware ownership. Keep the SRB outstanding
         * in this callback until the controller/RTKit stop receipt is checked. */
        a->Slots[tag].Srb=NULL;a->ShutdownTag=ANS_QUEUE_DEPTH;
        if (!AppleAnsNativeStop(a)) {
            /* Direct: no stop proof -> fail the SHUTDOWN SRB and keep every buffer (never freed
             * before power-off/reset). Broker: one-way EL2 containment. */
            if (!a->Direct) terminal_containment(a);
            flushed=FALSE;
        }
        if (flushed) srb->ScsiStatus=SCSISTAT_GOOD;
        AppleAnsComplete(a,srb,flushed?SRB_STATUS_SUCCESS:SRB_STATUS_ERROR,0);
    }
    StorPortReleaseSpinLock(a,&lock);
}
