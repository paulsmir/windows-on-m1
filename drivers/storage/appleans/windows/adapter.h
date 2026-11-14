/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_ADAPTER_H
#define APPLE_ANS_ADAPTER_H
#include <ntddk.h>
#include <storport.h>
#include "transport.h"
#include "identify.h"
#include "../common/ans_order.h"
#include "../common/ans_direct.h"
#include "../common/ans_trim.h"
#include "../common/ans_reject.h"
#include "../common/ans_bounce.h"
#include "telemetry.h"
#include "dump.h"
#ifndef APPLE_ANS_NATIVE_READ_ONLY
#define APPLE_ANS_NATIVE_READ_ONLY 0
#endif
/* ANS008: a write-capable package still writes only when the broker advertises
 * ANS_FEATURE_WRITE_WINDOW; EL2 denies any WRITE outside that window. */
#ifndef APPLE_ANS_NATIVE_WRITE
#define APPLE_ANS_NATIVE_WRITE 0
#endif
#ifndef APPLE_ANS_QUALIFICATION_STOP
#define APPLE_ANS_QUALIFICATION_STOP 0
#endif
#if APPLE_ANS_QUALIFICATION_STOP < 0 || APPLE_ANS_QUALIFICATION_STOP > 3
#error Invalid ANS qualification stop level
#endif
#define APPLE_ANS_METADATA_PAGES (ANS_QUEUE_DEPTH + 2)
#define APPLE_ANS_METADATA_BYTES (APPLE_ANS_METADATA_PAGES * ANS_PRP_PAGE_SIZE + ANS_PRP_PAGE_SIZE - 1)
/* Direct mode: six 16 KiB queue arrays (admin/IO SQ, CQ, TCB), then the metadata pages,
 * plus alignment slack (StorPortGetUncachedExtension guarantees only page alignment). */
#define APPLE_ANS_DIRECT_BYTES (6u * ANSD_QUEUE_ALIGN + APPLE_ANS_METADATA_PAGES * ANS_PRP_PAGE_SIZE + \
    ANS_BOUNCE_COUNT * ANS_BOUNCE_BYTES + ANSD_QUEUE_ALIGN) /* ... then the bounce buffers */
enum APPLE_ANS_STAGE {
    AppleAnsStageEntry=1, AppleAnsStageFindAdapter=2, AppleAnsStageResourcesRejected=3,
    AppleAnsStageResourcesAccepted=4, AppleAnsStageTransportUnavailable=5,
    AppleAnsStageStopped=6, AppleAnsStageQuery=10, AppleAnsStageClaim=11,
    AppleAnsStageIdentifyController=12, AppleAnsStageIdentifyNamespace=13,
    AppleAnsStageCache=14, AppleAnsStageCreateCq=15, AppleAnsStageCreateSq=16,
    AppleAnsStageReady=17, AppleAnsStageQuarantined=18, AppleAnsStageReset=19,
    AppleAnsStageAdminReady=20, AppleAnsStageQualificationStop=21
};
typedef struct _APPLE_ANS_SRB {
    UCHAR Cdb[16], CdbLength, Valid, PrpFailure, Misaligned, MisalignedLow;
    /* Bounce (common/ans_bounce.h): BuildIo sets NeedBounce and the mapped address of the SRB's
     * data, or BounceDefer when the buffer could not be mapped; StartIo claims a buffer
     * (BounceIndex = index + 1) and AppleAnsComplete copies a read back and releases it. */
    UCHAR NeedBounce, BounceDefer, BounceIndex;
    PVOID BounceVa;
    ULONG Length, DiagValue, DiagCount;
    struct ans_logical_prps Prps;
} APPLE_ANS_SRB, *PAPPLE_ANS_SRB;
typedef struct _APPLE_ANS_SLOT {
    PSCSI_REQUEST_BLOCK Srb;
    ULONG Length;
    BOOLEAN Busy, Shutdown;
    struct ans_order_token Token;
    struct ans_scsi_plan Plan;
    struct ans_broker_request Request;
} APPLE_ANS_SLOT;
typedef struct _APPLE_ANS_ADAPTER {
    ULONG Signature, Version;
    volatile ULONG Stage;
    ULONG Failure, FailureStage, LastCheckpoint;
    STOR_PHYSICAL_ADDRESS BrokerAddress;
    ULONG BrokerLength, AssignedInterruptVector, AssignedInterruptLevel;
    BOOLEAN ResourcesAccepted, Claimed, MayOwn, Ready;
    PUCHAR Broker;
    PVOID Common;
    PUCHAR Pages[APPLE_ANS_METADATA_PAGES];
    ULONGLONG PageDma[APPLE_ANS_METADATA_PAGES];
    struct ans_wire Wire;
    struct ans_namespace Namespace;
    struct ans_sense Sense;
    APPLE_ANS_SLOT Slots[ANS_QUEUE_DEPTH];
    struct ans_order Order;
    struct ans_telemetry Stats;
    STOR_DPC ShutdownDpc;
    volatile LONG DpcReady;
    BOOLEAN ShuttingDown, ShutdownFlushOk;
    unsigned ShutdownTag;
    /* ANS-DIRECT: selected by the ACPI resource shape (controller + mailbox ranges). */
    BOOLEAN Direct;
    PUCHAR Ctrl, Mbox;
    struct ansd_ctrl Ansd;
    volatile LONG DiagLogs; /* ANS012-R3 receipt-only System-log records (bounded) */
    /* Crash-dump hand-over (dump.h): sealed on SRB_FUNCTION_DUMP_POINTERS, lent to the dump
     * instance; DumpReceiptLogged is the receipt value already written to the System log. */
    struct ans_dump_context DumpContext;
    ULONG DumpPointersSrbs, DumpReceiptLogged;
    /* Non-zero only in the dump image (PORT_CONFIGURATION_INFORMATION.DumpMode): interrupts
     * are off, so no lock, DPC, timer or log API is used and StartIo polls its own request.
     * DumpContext is then this instance's validated copy of the live record. */
    ULONG DumpMode;
    /* UNMAP policy (Parameters\Device: UnmapMode, UnmapFenceStart, UnmapFenceEnd, read in
     * FindAdapter) and receipt. Zero (the dump instance) is ANS_TRIM_OFF. */
    struct ans_trim Trim;
    /* Requests refused as not implemented (SRB functions, SRB_IO_CONTROL codes, CDB opcodes). */
    struct ans_rejects Rejects;
    /* Page-aligned bounce buffers in the uncached extension and the controller address of each
     * of their pages (resolved once in direct_carve). Pool state under InterruptLock. */
    struct ans_bounce_pool Bounce;
    PUCHAR BounceBuffer[ANS_BOUNCE_COUNT];
    ULONGLONG BouncePa[ANS_BOUNCE_COUNT][ANS_BOUNCE_PAGES];
} APPLE_ANS_ADAPTER, *PAPPLE_ANS_ADAPTER;
BOOLEAN AppleAnsNativeResources(PAPPLE_ANS_ADAPTER a, PPORT_CONFIGURATION_INFORMATION config);
BOOLEAN AppleAnsNativeInitialize(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsNativeStart(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb);
BOOLEAN AppleAnsNativeInterrupt(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsNativeReset(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsNativeStop(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsDirectResources(PAPPLE_ANS_ADAPTER a, PPORT_CONFIGURATION_INFORMATION config);
BOOLEAN AppleAnsDirectInitialize(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsDirectDumpInitialize(PAPPLE_ANS_ADAPTER a);
VOID AppleAnsDirectDumpDrain(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsDirectInterrupt(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsDirectReset(PAPPLE_ANS_ADAPTER a);
BOOLEAN AppleAnsDirectStop(PAPPLE_ANS_ADAPTER a);
int AppleAnsDirectSubmit(PAPPLE_ANS_ADAPTER a, unsigned tag, const struct ans_broker_request *r);
VOID AppleAnsDirectPeer(PAPPLE_ANS_ADAPTER a, struct ans_telemetry *peer);
VOID AppleAnsDispatch(PAPPLE_ANS_ADAPTER a);
/* Receipt-only diagnostics: StorPortLogError UniqueId = code<<24 | (value & 0xffffff), at most
 * APPLE_ANS_DIAG_MAX per adapter start. Codes: 0xA1 DMA translation reject (value = IPA>>12),
 * 0xA2 SG->PRP reject (value = error<<20 | (first SG address>>12 & 0xfffff)), 0xA3 SG element
 * count (value = count), 0xA4 NVMe CQE error (value = status<<8 | opcode), 0xA5 reset,
 * 0xA6 SRB_FUNCTION_DUMP_POINTERS answered (value = sealed<<8 | count), 0xA7 dump-instance
 * receipt (value = ans_dump_context_receipt_value), 0xA8 Identify ONCS without Dataset
 * Management: UNMAP stays unadvertised (value = ONCS), 0xA9 UNMAP policy at start
 * (value = effective mode<<16 | ONCS), 0xAA completions reaped by the timer because their
 * interrupt did not arrive within a timer period (value = running count). */
#define APPLE_ANS_DIAG_MAX 96
VOID AppleAnsDiag(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb, ULONG code, ULONG value);
VOID AppleAnsFinish(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb, USHORT nvme_status, ULONG length);
HW_PASSIVE_INITIALIZE_ROUTINE AppleAnsPassiveInitialize;
VOID AppleAnsComplete(PAPPLE_ANS_ADAPTER a, PSCSI_REQUEST_BLOCK srb, UCHAR status, ULONG length);
#endif
