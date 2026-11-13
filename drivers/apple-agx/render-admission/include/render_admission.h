#ifndef APPLE_AGX_RENDER_ADMISSION_H
#define APPLE_AGX_RENDER_ADMISSION_H

#include "apple_agx_vsync.h"

#include <ntddk.h>
#include <windef.h>
#include <winerror.h>
#include <wingdi.h>
#include <ntddvdeo.h>
#include <d3dkmddi.h>
#include "gpuva_g1b_profile.h"
#include <d3dkmthk.h>
#include <dispmprt.h>
#include <ntstrsafe.h>
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#define APPLE_AGX_NATIVE_STAGING 1
#include "render_native_staging.h"
#endif
#include "render_objects.h"
#include "render_memory.h"
#include "render_allocation.h"
#include "render_pool_probe.h"
#include "render_pool_demand.h"
#include "render_pool_slab.h"
#include "render_hvc.h"
#include "apple_agx_memory.h"
#include "apple_agx_g3_private_failure.h"
#include "apple_agx_local_reserve_abi.h"
#include "apple_agx_pool_window_abi.h"
#include "apple_agx_g3_copy_query_receipt.h"
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_residency.h"
#include "apple_agx_uat_publication.h"
#include "render_paging.h"
#include "render_omitted_leaves.h"
#include "render_native_staging_receipt.h"
#include "render_gdi.h"
#include "render_umd_command.h"
#include "render_win32_transport.h"
#include "apple_agx_win32_device_info.h"
#include "render_gdi_receipt.h"
#include "render_call_correlation.h"
#include "render_present.h"
#include "render_visible_scanout.h"
#include "render_submit_trace.h"
#include "render_submission.h"
#include "render_backend_image.h"
#include "render_dynamic_overlay.h"
#include "render_dynamic_dma.h"
#include "render_dynamic_output.h"
#include "render_completed_output.h"
#include "render_qualification.h"
#include "render_output_queue.h"
#include "render_gpu_fault.h"
#include "apple_agx_wddm_feature_contract.h"
#include "apple_agx_scheduler.h"
#include "apple_agx_platform_provider.h"
#include "apple_agx_firmware_provider.h"
#include "apple_agx_device_control.h"
#include "apple_agx_initdata_memory.h"
#include "apple_agx_context0_broker.h"
#include "apple_agx_gpuva_broker_v5_client.h"
#include "apple_agx_gpuva_g3_graph.h"
#include "apple_agx_gpuva_g3_translation.h"
#include "apple_agx_retained_root_abi.h"
#include "apple_agx_power.h"
#include "apple_agx_rtkit_session.h"
#include "apple_agx_fixed_panel.h"
#include "apple_agx_post_display_route.h"
#include "j313_agx_abi_admission.generated.h"
#include "mpo_contract.h"
#include "mpo_layers.h"
#include "mpo_hold.h"

#define ADMISSION_POOL_TAG 'mRGA'
#define ADMISSION_DMA_BUFFER_SIZE 0x50000u
#define ADMISSION_ALLOCATION_LIST_SIZE 64u
#define ADMISSION_PATCH_LIST_SIZE 64u
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 0x51000u
#define ADMISSION_GDI_ALLOCATION_LIST_SIZE 256u
#define ADMISSION_GDI_PATCH_LIST_SIZE 256u
#define ADMISSION_MAX_PAGING_RECORDS 64u
/* Match the private allocation to the existing scheduler record capacity.
 * A single 4-KiB page holds only 32 records on the 64-bit ABI. */
#define ADMISSION_PAGING_PRIVATE_BYTES (ADMISSION_MAX_PAGING_RECORDS * sizeof(ADMISSION_PAGING_RECORD))

typedef enum _ADMISSION_RECEIPT {
  AdmissionReceiptAddEntered = 1,
  AdmissionReceiptAddSucceeded = 2,
  AdmissionReceiptStartEntered = 3,
  AdmissionReceiptStartDeviceInfo = 4,
  AdmissionReceiptStartSucceeded = 5,
  AdmissionReceiptQueryAdapterInfo = 6,
  AdmissionReceiptStop = 7,
  AdmissionReceiptRemove = 8,
  AdmissionReceiptNodeMetadata = 9,
  AdmissionReceiptStartPostDisplay = 10,
  AdmissionReceiptChildRelations = 11,
  AdmissionReceiptChildStatus = 12,
  AdmissionReceiptVidPn = 13,
  AdmissionReceiptSourceAddress = 14,
  AdmissionReceiptStartInterrupt = 15,
  AdmissionReceiptMemoryQualified = 16,
} ADMISSION_RECEIPT;

typedef enum _ADMISSION_DISPLAY_DDI_TRACE_ID {
  AdmissionDisplayDdiQueryChildRelations = 1,
  AdmissionDisplayDdiQueryChildStatus = 2,
  AdmissionDisplayDdiQueryDeviceDescriptor = 3,
  AdmissionDisplayDdiIsSupportedVidPn = 4,
  AdmissionDisplayDdiRecommendFunctionalVidPn = 5,
  AdmissionDisplayDdiEnumVidPnCofuncModality = 6,
  AdmissionDisplayDdiSetVidPnSourceVisibility = 7,
  AdmissionDisplayDdiCommitVidPn = 8,
  AdmissionDisplayDdiUpdateActiveVidPnPresentPath = 9,
  AdmissionDisplayDdiRecommendMonitorModes = 10,
  AdmissionDisplayDdiQueryVidPnHWCapability = 11,
  AdmissionDisplayDdiUpdateMonitorLinkInfo = 12
} ADMISSION_DISPLAY_DDI_TRACE_ID;

/* rc110 receipt (Wom1DisplayModesReceipt): the display-mode path end to
 * end, written after every traced display DDI.  Counters are updated at any
 * IRQL with interlocked operations; the writer runs at PASSIVE_LEVEL. */
#define ADMISSION_DISPLAY_MODES_RECEIPT_VERSION 1u
typedef struct _ADMISSION_DISPLAY_MODES_RECEIPT {
  volatile LONG Version, Bytes;
  volatile LONG BrokerAbi, BrokerCaps, ScaledModes, BuildSwitch;
  volatile LONG EnumCalls, LastEnumStatus, LastEnumPivot, LastEnumSourceModes;
  volatile LONG LastEnumTargetModes, LastCommitTargetWidth, LastCommitTargetHeight;
  volatile LONG IsSupportedCalls, LastIsSupportedStatus, LastIsSupportedVerdict;
  volatile LONG CommitCalls, LastCommitStatus, LastCommitWidth, LastCommitHeight;
  volatile LONG LastCommitStride, LastCommitFormat, LastCommitScaling;
  volatile LONG VisibilityCalls, LastVisible;
  volatile LONG MonitorModeCalls, LastMonitorModesStatus, LastMonitorModesAdded;
  volatile LONG SourceAddressCalls, LastSourceAddressStatus, LastSourceAddressFlags;
  volatile LONG LastSourceAddressFormat, LastSourceAddressWidth, LastSourceAddressHeight;
  volatile LONG GeometryRefusals, FormatRefusals, LayoutRefusals, DeferredFlips;
  volatile LONG CommittedWidth, CommittedHeight, CommittedStride, CommittedFormat;
  volatile LONG CommittedScaling, SourceVisible, DisplayActive;
} ADMISSION_DISPLAY_MODES_RECEIPT;

/* S2 receipt (Wom1MpoReceipt, device key): the two-plane MPO path.  Written
 * with Wom1DisplayModesReceipt and touched only while the Wom1MpoTwoPlanes
 * switch is effective (switch = 1 AND broker CAPABILITIES bit 9 AND the
 * REG_NEGOTIATED read-back); all zero otherwise.  Counters are interlocked
 * at any IRQL. */
#define ADMISSION_MPO_RECEIPT_VERSION 9u /* v9: tail ... Ids, HOLD-2 (H2*), CUR-1 v2, HOLD-3 (H3*), HOLD-3b, HOLD-3c, RQ-1 */
#define ADMISSION_MPO_RECEIPT_REASONS 40u
/* Key: clip width (12 bits) | height << 12 | format class << 24 |
 * blend << 28; 0 = free row.  Origin/DstSize: x | y << 16. */
#define ADMISSION_MPO_CLIP_ROWS 12u
typedef struct _ADMISSION_MPO_CLIP_ROW {
  volatile LONG Key, Count, Supported, HandleChanges;
  volatile LONG LastOrigin, LastDstSize;
} ADMISSION_MPO_CLIP_ROW;
/* Origin/Size: x | y << 16; Info: format class | blend << 8 |
 * handle changed << 16 | Check3 reason << 24; Alpha: max alpha | count
 * of nonzero-alpha pixels << 8; Ticks: the copy pass; Qpc: low 32 bits. */
#define ADMISSION_MPO_CLIP_SAMPLES 24u
#define ADMISSION_MPO_CLIP_FIXED 8u
typedef struct _ADMISSION_MPO_CLIP_SAMPLE {
  volatile LONG Origin, Size, Info, Alpha, Crc32, Ticks, Qpc;
} ADMISSION_MPO_CLIP_SAMPLE;
#define ADMISSION_MPO_FORM_TWO 1
#define ADMISSION_MPO_FORM_LONE 2
#define ADMISSION_MPO_FORM_ONE 3   /* DWM-only: one layer record or V2 */
typedef struct _ADMISSION_MPO_RECEIPT {
  volatile LONG Version, Bytes;
  /* Start: switch value seen, CAP_TWO_LAYERS seen, negotiation read back. */
  volatile LONG SwitchSet, BrokerAdvertised, Effective, ContractVersion;
  volatile LONG CapsCalls, Check3Calls, Check3Supported, Check3Unsupported;
  volatile LONG LastCheck3Reason, LastCheck3FailingPlane, LastCheck3Planes;
  /* Unsupported Check3 answers by ADMISSION_MPO_REASON (0 unused). */
  volatile LONG Check3Reasons[ADMISSION_MPO_RECEIPT_REASONS];
  volatile LONG Set3Calls, TwoPlaneCommits, RemovalCommits, V2Presents;
  volatile LONG PlaneOffCompletions, TwoPlaneLatches, RemovalLatches;
  /* Set3 refused before the broker: table (a plane Check3 accepted no
   * longer qualifies with its placement), or an arrangement no Check3
   * answer admits (LayerIndex 1 alone, malformed planes). */
  volatile LONG Set3TableRefusals, LastSet3TableReason, Set3Refusals;
  volatile LONG Busy, NotVisible, TransportFailures;
  /* Contract defect: the broker refused a commit the table accepted. */
  volatile LONG BrokerRefusals, LastBrokerRefusal, LastRefusalCount;
  APPLE_AGX_SCANOUT_LAYER LastRefusalRecords[2];
  /* ---- version 2 (A1.2): slot 1 removed before a hide / mode commit ---- */
  volatile LONG Slot1Retires, Slot1RetireFailures, Slot1RetireRefusals;
  volatile LONG LastSlot1RetireStage;  /* ADMISSION_MPO_RETIRE_STAGE_* */
  /* ---- version 3 (STRETCH-1): scaled planes ---- */
  /* Start: broker bit 8 with two-layer scan-out effective.  Caps: the last
   * DxgkDdiGetMultiPlaneOverlayCaps answer (StretchRGB, factor bits). */
  volatile LONG ScalingSupported, CapsStretchRgb;
  volatile LONG CapsMaxStretchBits, CapsMaxShrinkBits;
  /* Check3 answers that involved a scaled plane, and the last one's
   * reason (0 = supported), StretchQuality, source and destination sizes
   * (Width | Height << 16) and per-axis factor (destination * 1000 /
   * source). */
  volatile LONG Check3ScaledSupported, Check3ScaledRefused;
  volatile LONG LastScaledReason, LastScaledStretchQuality;
  volatile LONG LastScaledSourceSize, LastScaledDestinationSize;
  volatile LONG LastScaledFactorX, LastScaledFactorY;
  /* Set3: two-plane commits with a scaled plane; one-plane layer-record
   * commits (AdmissionMpoEvaluateOne), and those of them that scale. */
  volatile LONG ScaledTwoPlaneCommits, OnePlaneRecordCommits;
  volatile LONG ScaledOnePlaneCommits;
  /* ---- version 4 (CUR-1 receipt, behaviour unchanged): Check3 calls
   * whose top plane (highest LayerIndex of two) is clipped (ClipRect !=
   * DstRect).  Rows keyed by clip size, format class and blend; per row
   * the count, supported answers, top-plane allocation changes since the
   * previous clipped Check3, and the last clip origin and destination
   * size.  ClipOnePlane: a one-plane Check3 with a clipped plane. */
  volatile LONG ClipChecks, ClipHandleChanges, ClipOverflow, ClipOnePlane;
  volatile LONG LastClipHandle;
  ADMISSION_MPO_CLIP_ROW ClipRows[ADMISSION_MPO_CLIP_ROWS];
  /* Wom1MpoClipRead = 1 (opt-in): for a refused clipped top plane of at
   * most 64x64 the clip block is read through the local segment's CPU
   * view at Check3 (PASSIVE).  NotPlaced: the allocation's local-segment
   * placement is unknown (no user-process page-table update seen) or
   * the block lies outside the view; NotEligible: scaled, larger than
   * 64x64 or not 32 bpp.  Read time (QPC ticks, the copy pass only) for
   * 1x1 clips and for the other clips separately: count/min/max/sum.
   * Zero1x1/NonZero1x1: 1x1 reads with alpha 0 / not 0.  Straight: reads
   * with any colour channel above alpha (straight, not premultiplied). */
  volatile LONG ClipReadSwitch, ClipReads, ClipNotPlaced, ClipNotEligible;
  volatile LONG ClipZero1x1, ClipNonZero1x1, ClipStraight, QpcFrequencyKhz;
  volatile LONG ReadTicks1x1[4], ReadTicksOther[4];
  /* First ADMISSION_MPO_CLIP_FIXED samples kept, the rest a ring;
   * at most one sample per 250 ms. */
  volatile LONG ClipSampleCount, ClipLastSampleMs;
  ADMISSION_MPO_CLIP_SAMPLE ClipSamples[ADMISSION_MPO_CLIP_SAMPLES];
  /* NP-2: Set3-side publish requests by the IRQL Set3 ran at (above /
   * at-or-below DISPATCH_LEVEL), the highest IRQL seen, publishes the
   * work item made. */
  volatile LONG PublishAboveDispatch, PublishAtOrBelowDispatch;
  volatile LONG Set3MaxIrql, PublishWorkerRuns;
  /* STRETCH-1b: Set3's raw StretchQuality per scaled plane (0, 1, 2,
   * other) and the last value (Check3's is LastScaledStretchQuality);
   * Set3 table refusals while the last Check3 answer was supported (a
   * contract defect) and the last such reason. */
  volatile LONG Set3StretchQuality[4], LastSet3StretchQuality;
  volatile LONG Set3RefusedAfterSupport, LastSet3RefusedAfterSupportReason;
  /* S3-C: PresentIds (low 32 bits) of the last Busy Set3 (layer slots 0/1
   * of its completion list) and of the next successful Set3; per Busy, did
   * the next success carry one of the refused ids (re-issued) or not
   * (superseded). */
  volatile LONG LastBusyPresentId, LastBusyPresentId1, BusyOutstanding;
  volatile LONG NextSuccessPresentId, BusyThenSameLatched, BusyThenSuperseded;
  /* S3-B: Set3 answers DEVICE_BUSY for every runtime obstacle (dxgkrnl's
   * Set3 thunk retries only SUCCESS/DEVICE_BUSY; anything else is offered
   * again forever, EXP2374).  Per cause: no runtime / stopping (row I), not
   * visible (E1), not qualified (E4), transport (E5, last result), broker
   * rejected (E3, a defect: records kept), table invariant at Set3 counted
   * by Set3TableRefusals (D1/G2), build invariant (D2/G1), the V2 path's
   * runtime statuses (H2, last status).  Consecutive Busy answers with no
   * accepted Set3 between them (current, maximum) are the freeze detector. */
  volatile LONG S3BusyNoRuntime, S3BusyNotVisible, S3BusyNotQualified;
  volatile LONG S3BusyTransport, LastTransportResult, S3BusyBrokerRejected;
  volatile LONG S3BusyBuild, S3BusyLegacy, LastLegacyStatus;
  volatile LONG ConsecutiveBusy, MaxConsecutiveBusy;
  /* FS-1: one-plane Check3 answers for LayerIndex 1 alone, lone-plane
   * commits (scaled), the scan-out form after the last accepted commit
   * (ADMISSION_MPO_FORM_*) and transitions into / out of the lone form. */
  volatile LONG LoneChecksSupported, LoneChecksRefused;
  volatile LONG LoneCommits, ScaledLoneCommits, LastForm, ToLone, FromLone;
  /* HOLD-1: flips merged into the held set (SUCCESS), refused while held
   * (same layer pending or not acceptable: DEVICE_BUSY), the hold's
   * try-lock contended, held sets committed by the latch / vertical tick,
   * commits retried at the next tick, sets dropped (a non-retry failure). */
  volatile LONG HoldMerges, HoldBusy, HoldContended;
  volatile LONG HoldCommits, HoldCommitRetries, HoldDropped;
  /* HOLD-1: PresentIds per LayerIndex (0/1) in every Set3 answered
   * SUCCESS, and per LayerIndex reported to dxgkrnl in a
   * CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2 interrupt.  Accepted - Reported at
   * the end of an arm is the flips in flight (queued + held) or lost ids. */
  volatile LONG AcceptedIds0, AcceptedIds1, ReportedIds0, ReportedIds1;
  /* HOLD-2: Set3 outcomes (joined an empty / a pending set), commits by
   * source (commit DPC after dxgkrnl's pass, latch handler, vertical tick),
   * commits carrying both layers, the longest first-Set3-to-commit delay. */
  volatile LONG H2Set3MergedFirst, H2Set3MergedSecond;
  volatile LONG H2CommitsFromDpc, H2CommitsFromLatch, H2CommitsFromTick;
  volatile LONG H2TwoLayerCommits, H2MaxSet3ToCommitUs;
  /* CUR-1 v2 scan-out scratch (render_memory.h): segment-2 size VidMm is
   * told and the DCP pool size (KiB), scratch pool offset (KiB), Ready = the
   * region was zeroed at scan-out start and read back zero; the bytes that
   * did not read back zero. */
  volatile LONG VidMmSegment2Kib, PoolKib, ScratchOffsetKib;
  volatile LONG ScratchReady, ScratchNonZeroBytes;
  /* CUR-1 v2 Set3: two-plane commits showing a scratch / a crop, scratch
   * writes skipped (all-zero over all-zero), copy time in QPC ticks
   * (count, min, max, sum; the read + fill + barrier). */
  volatile LONG ScratchCommits, CropCommits, ScratchSkippedWrites;
  volatile LONG ScratchCopyTicks[4];
  /* HOLD-3: commits at the frame deadline / by the DPC after the deadline,
   * timer arms, deadline error (actual - planned, us: n, min, max, sum),
   * swaps whose latch missed the vblank after the commit (HOLD-3c: more
   * than Lead + Period/2 after it; v7 used 1.5 frames and was blind), the lead
   * in use (us), and the Set3 phase after the last vsync per layer
   * (L0 / L1, 2 ms bins, the last bin = 16 ms and later). */
  volatile LONG H3CommitsDeadline, H3CommitsLate, H3TimerArms;
  volatile LONG H3DeadlineErrN, H3DeadlineErrMinUs, H3DeadlineErrMaxUs, H3DeadlineErrSumUs;
  volatile LONG H3MissedVblanks, H3LeadUs;
  volatile LONG H3PhaseL0[9], H3PhaseL1[9];
  /* HOLD-3b: sets pending when a latch arrived (handed to the deadline,
   * never committed by the latch), same-layer DEVICE_BUSY per layer, vsyncs
   * after which a layer still had an accepted flip not on screen (count and
   * longest run): the direct detector of the every-other-vsync regime. */
  volatile LONG H3HeldAtLatch, H3SameLayerBusyL0, H3SameLayerBusyL1;
  volatile LONG H3NoLatchFramesL0, H3NoLatchFramesL1;
  volatile LONG H3MaxNoLatchRunL0, H3MaxNoLatchRunL1;
  /* HOLD-3c: missed vblanks whose commit came from the deadline timer (the
   * lead's own misses; H3MissedVblanks counts every source), deadline
   * decisions taken without a fresh latch reference (committed at once),
   * the smallest commit-to-latch of a swap that made its vblank and the
   * largest time from a commit to the latch it missed (us; the scan-out
   * floor lies between them), ticks that set the reference (tick - learned
   * tick-to-latch offset, frames without a latch), the learned offset (us)
   * and the tick/latch pairs rejected outside the Period/4 window. */
  volatile LONG H3MissedDeadline, H3StaleReference;
  volatile LONG H3MadeMinUs, H3MissMaxUs;
  volatile LONG H3TickReference, H3TickOffsetUs, H3TickPairsRejected;
  /* RQ-1 (v9, after HOLD-3c): swaps the broker failed after acceptance (IRQ_ERROR, RESULT !=
   * OK, RECEIPT == the pending sequence), re-submissions, failed swaps that
   * latched on a retry, failed swaps whose PresentIds were reported after
   * the last try, the last failure's broker result, and IRQ_ERRORs that were
   * not about the pending present (a RELEASE failure, a stale error). */
  volatile LONG SwapFailures, SwapRetries, SwapRetryLatched;
  volatile LONG SwapRetryExhaustedReported, LastSwapFailureResult, ForeignErrors;
} ADMISSION_MPO_RECEIPT;
/* HOLD-2 receipt: Wom1MpoTiming, the last ADMISSION_MPO_TIMING_ENTRIES Set3
 * outcomes, commits (by source) and latches with QPC, CPU and IRQL. */
#define ADMISSION_MPO_TIMING_VERSION 1u
#define ADMISSION_MPO_TIMING_ENTRIES 512u
#define ADMISSION_MPO_TIMING_SET3_FIRST 1u
#define ADMISSION_MPO_TIMING_SET3_SECOND 2u
#define ADMISSION_MPO_TIMING_SET3_BUSY 3u
#define ADMISSION_MPO_TIMING_COMMIT_DPC 4u
#define ADMISSION_MPO_TIMING_COMMIT_LATCH 5u
#define ADMISSION_MPO_TIMING_COMMIT_TICK 6u
#define ADMISSION_MPO_TIMING_LATCH 7u
#define ADMISSION_MPO_TIMING_COMMIT_DEADLINE 8u
typedef struct _ADMISSION_MPO_TIMING_ENTRY {
  ULONGLONG Qpc;
  ULONG Id;          /* low 32 bits of the first PresentId */
  UCHAR Kind, Cpu, Irql, Mask;
} ADMISSION_MPO_TIMING_ENTRY;
typedef struct _ADMISSION_MPO_TIMING {
  ULONG Version;
  volatile LONG Next;
  LONGLONG Frequency;
  ADMISSION_MPO_TIMING_ENTRY Entries[ADMISSION_MPO_TIMING_ENTRIES];
} ADMISSION_MPO_TIMING;
#define ADMISSION_MPO_RETIRE_STAGE_HIDE 1u
#define ADMISSION_MPO_RETIRE_STAGE_COMMIT 2u

/* S2-1b: Wom1MpoGateReceipt (device key, receipt-only).  Where the plane
 * capability is lost: every DRIVERCAPS answer (first and last), the MPO
 * caps / Check3 DDIs counted ungated and never zeroed, StartDevice and
 * scanout starts, Effective transitions, and every QueryAdapterInfo type.
 * Lives in the context from AddDevice (zeroed there only). */
#define ADMISSION_MPO_GATE_RECEIPT_VERSION 1u
#define ADMISSION_MPO_GATE_QUERY_TYPES 64u
typedef struct _ADMISSION_MPO_GATE_CAPS {
  LONG Valid, Mpo, DirectFlip, MaxOverlayPlanes;
  LONG WddmVersion, Status, Zeroed, Reserved;
  LONGLONG Qpc;
} ADMISSION_MPO_GATE_CAPS;
typedef struct _ADMISSION_MPO_GATE_RECEIPT {
  volatile LONG Version, Bytes;
  volatile LONG StartDeviceCount, ScanoutStartCount, DrvCapsQueryCount;
  volatile LONG EffectiveTransitions, LastEffectiveClearReason;
  volatile LONG CapsCallsAll, Check3CallsAll, CapsLastVidPnSourceId;
  volatile LONG CapsLastMaxPlanes, StopDeviceCount;
  LONGLONG QpcFrequency, StartEntryQpc, StartExitQpc, MpoEffectiveSetQpc;
  volatile LONGLONG CapsFirstQpc, Check3FirstQpc;
  ADMISSION_MPO_GATE_CAPS FirstDrvCaps, LastDrvCaps;
  /* index = DXGKQAITYPE; the last slot counts every type >= 63 */
  volatile LONG QueryTypeCounts[ADMISSION_MPO_GATE_QUERY_TYPES];
  volatile LONG QueryRefusalBitmap[2];
} ADMISSION_MPO_GATE_RECEIPT;
#define ADMISSION_MPO_GATE_CLEAR_STOP 1u

/* PresentIds to report, with their LayerIndex, at the latch of a flip. */
typedef struct _ADMISSION_MPO_COMPLETION {
  ULONG Count;
  ULONG LayerIndex[2];
  ULONGLONG PresentId[2];
} ADMISSION_MPO_COMPLETION;

/* Current state of one MPO plane (LayerIndex 0 or 1), as the last accepted
 * Set3 left it.  A Set3 lists only the planes it changes; the others keep
 * this state (Microsoft Learn, "Hardware flip queue": flips reference a
 * subset of planes, "planes affected by the flip"). */
typedef struct _ADMISSION_MPO_PLANE_STATE {
  ULONG Valid;
  ULONG Enabled;
  ADMISSION_MPO_PLANE_INPUT Input;
  UINT PrimarySegment;
  LONGLONG PrimaryAddress;
  HANDLE hAllocation;
  ULONG FlipImmediate;
  ULONG FlipOnNextVSync;
} ADMISSION_MPO_PLANE_STATE;

typedef enum _ADMISSION_START_STAGE {
  AdmissionStartNone = 0,
  AdmissionStartEntered = 1,
  AdmissionStartDeviceInfo = 2,
  AdmissionStartInterrupt = 3,
  AdmissionStartMemory = 4,
  AdmissionStartBackendImage = 5,
  AdmissionStartScheduler = 6,
  AdmissionStartPaging = 7,
  AdmissionStartPlatform = 8,
  AdmissionStartPostDisplay = 9,
  AdmissionStartScanout = 10,
  AdmissionStartObjects = 11,
  AdmissionStartComplete = 12,
} ADMISSION_START_STAGE;

typedef enum _ADMISSION_PLATFORM_STAGE {
  AdmissionPlatformNone = 0,
  AdmissionPlatformEntered = 1,
  AdmissionPlatformResources = 2,
  AdmissionPlatformRuntimeAllocated = 3,
  AdmissionPlatformMemoryIo = 4,
  AdmissionPlatformSnapshot = 5,
  AdmissionPlatformSgxMap = 6,
  AdmissionPlatformHandoffMap = 7,
  AdmissionPlatformHandoffBind = 8,
  AdmissionPlatformInitdata = 9,
  AdmissionPlatformFirmwareProvider = 10,
  AdmissionPlatformQueueProvider = 11,
  AdmissionPlatformBackendStart = 12,
  AdmissionPlatformWorkItem = 13,
  AdmissionPlatformComplete = 14,
} ADMISSION_PLATFORM_STAGE;

/* Immutable first source-address call. Publication is 0 empty, 1 writer,
 * 2 complete, 3 passive persistence claimed. No production decision reads it. */
typedef struct _ADMISSION_SOURCE_ADDRESS_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Status;
  ULONG QueueCalled;
  ULONG Irql;
  ULONG ArgsPresent;
  ULONG SourceId;
  ULONG PrimarySegment;
  ULONGLONG PrimaryAddress;
  ULONGLONG Allocation;
  ULONG Flags;
  ULONG ContextCount;
  ULONG Width;
  ULONG Height;
  ULONG Stride;
  ULONG Format;
  ULONG Started;
  ULONG DisplayActive;
  ULONG SourceVisible;
  ULONG ScanoutState; /* bit0 runtime exists; bit1 IRQ enabled */
  /* EXP807: selected primary identity. This is not a CDD write receipt. */
  ULONGLONG SelectedCpuAddress;
  ULONGLONG SelectedHostPhysicalAddress;
  ULONGLONG SelectedGuestIpaAddress;
  ULONGLONG SelectedSurfaceOffset;
  ULONG SelectedMapStatus;
  ULONG CacheCleanPerformed;
} ADMISSION_SOURCE_ADDRESS_RECEIPT;

#define ADMISSION_DWM_SOURCE_MAP_VERSION 2u
typedef struct _ADMISSION_DWM_SOURCE_MAP_RECEIPT {
  ULONG Version, Bytes, OsProcessId, PteFound;
  ULONGLONG GraphProcessId, Allocation, CanonicalGpuVa, RootIpa;
  ULONGLONG MappingGeneration, PteAllocation, PteAllocationOffset;
  ULONGLONG PteGuestIpa, ResolvedGuestIpa;
  ULONGLONG SelectedHostPhysicalAddress, SelectedPrimaryAddress;
  ULONGLONG SelectedSurfaceBytes;
  ULONG SegmentId, PteFlags, SourceReceiptState, Ordinal;
  ULONG InSelectedRange, InSelectedRangeCount;
} ADMISSION_DWM_SOURCE_MAP_RECEIPT;

typedef struct _ADMISSION_PRESENT_TRANSFER_RECEIPT {
  ULONG Version, Bytes, Fence, Status;
  ULONGLONG BytesCopied, SourceLocation, DestinationLocation, ContextToken;
  ULONG Width, Height, NotifyInterrupt, NotifyDpc;
} ADMISSION_PRESENT_TRANSFER_RECEIPT;

#define ADMISSION_QUEUE_SUBMISSION_RECEIPT_VERSION 1u
typedef struct _ADMISSION_QUEUE_SUBMISSION_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG BackendPhase;
  ULONG ProviderPhase;
  ULONG RuntimePhase;
  ULONG InitialProgressValid;
  ULONG TaEventNumber;
  ULONG D3EventNumber;
  ULONG TaRingCapacity;
  ULONG D3RingCapacity;
  ULONG TaCpuWritePointer;
  ULONG TaGpuDonePointer;
  ULONG TaStamp;
  ULONG TaExpectedStamp;
  ULONG TaExpectedDonePointer;
  ULONG D3CpuWritePointer;
  ULONG D3GpuDonePointer;
  ULONG D3Stamp;
  ULONG D3ExpectedStamp;
  ULONG D3ExpectedDonePointer;
  ULONG TaChannelReadPointer;
  ULONG TaChannelWritePointer;
  ULONG D3ChannelReadPointer;
  ULONG D3ChannelWritePointer;
  ULONG TaDoorbell;
  ULONG D3Doorbell;
  ULONGLONG TaQueueInfoGpuAddress;
  ULONGLONG D3QueueInfoGpuAddress;
  ULONGLONG TaChannelStateGpuAddress;
  ULONGLONG TaChannelRingGpuAddress;
  ULONGLONG D3ChannelStateGpuAddress;
  ULONGLONG D3ChannelRingGpuAddress;
  ULONGLONG TaWorkAddresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  ULONGLONG D3WorkAddresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  UCHAR TaRunMessage[APPLE_AGX_G13_RUN_MESSAGE_SIZE];
  UCHAR D3RunMessage[APPLE_AGX_G13_RUN_MESSAGE_SIZE];
} ADMISSION_QUEUE_SUBMISSION_RECEIPT;

#define ADMISSION_QUEUE_INFO_RECEIPT_VERSION 1u
#define ADMISSION_QUEUE_INFO_BYTES 184u
#define ADMISSION_QUEUE_POINTERS_BYTES 96u
typedef struct _ADMISSION_QUEUE_INFO_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG Reserved;
  UCHAR D3Info[ADMISSION_QUEUE_INFO_BYTES];
  UCHAR TaInfo[ADMISSION_QUEUE_INFO_BYTES];
  UCHAR D3Pointers[ADMISSION_QUEUE_POINTERS_BYTES];
  UCHAR TaPointers[ADMISSION_QUEUE_POINTERS_BYTES];
} ADMISSION_QUEUE_INFO_RECEIPT;

#define ADMISSION_BUFFER_MANAGER_RECEIPT_VERSION 1u
#define ADMISSION_BUFFER_MANAGER_INFO_BYTES 188u
#define ADMISSION_BUFFER_MANAGER_STATE_BYTES 64u
typedef struct _ADMISSION_BUFFER_MANAGER_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG Reserved;
  UCHAR Info[ADMISSION_BUFFER_MANAGER_INFO_BYTES];
  UCHAR BlockControl[ADMISSION_BUFFER_MANAGER_STATE_BYTES];
  UCHAR Counter[ADMISSION_BUFFER_MANAGER_STATE_BYTES];
  UCHAR Misc[ADMISSION_BUFFER_MANAGER_STATE_BYTES];
} ADMISSION_BUFFER_MANAGER_RECEIPT;

#define ADMISSION_TA_PROGRESS_RECEIPT_VERSION 2u
#define ADMISSION_TA_INITBM_BYTES 32u
#define ADMISSION_TA_MICROSEQUENCE_OPCODE_COUNT 6u
#define ADMISSION_TA_WORK_TIMESTAMP_TAIL_BYTES 0x68u
#define ADMISSION_TA_STATS_HEAD_BYTES 0x78u
#define ADMISSION_TA_STATS_TIMESTAMPS_BYTES 0x80u
#define ADMISSION_TA_STAMP_BYTES 8u
#define ADMISSION_TA_TIMESTAMP_TARGET_BYTES 32u
typedef struct _ADMISSION_TA_PROGRESS_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG ElapsedMs;
  UCHAR InitBm[ADMISSION_TA_INITBM_BYTES];
  ULONG MicrosequenceOpcodes[ADMISSION_TA_MICROSEQUENCE_OPCODE_COUNT];
  UCHAR TaInfo[ADMISSION_QUEUE_INFO_BYTES];
  UCHAR TaPointers[ADMISSION_QUEUE_POINTERS_BYTES];
  UCHAR EventControl[176u];
  UCHAR WorkTimestampTail[ADMISSION_TA_WORK_TIMESTAMP_TAIL_BYTES];
  UCHAR StatsHead[ADMISSION_TA_STATS_HEAD_BYTES];
  UCHAR StatsTimestamps[ADMISSION_TA_STATS_TIMESTAMPS_BYTES];
  UCHAR TaStamps[ADMISSION_TA_STAMP_BYTES];
  UCHAR TimestampTargets[ADMISSION_TA_TIMESTAMP_TARGET_BYTES];
} ADMISSION_TA_PROGRESS_RECEIPT;

#define ADMISSION_TA_RETIRE_RECEIPT_VERSION 1u
#define ADMISSION_TA_FINALIZE_RETIRE_BYTES 0x88u
#define ADMISSION_REGIONC_PENDING_STAMPS_BYTES 0x800u
typedef struct _ADMISSION_TA_RETIRE_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG ElapsedMs;
  ULONG EventReadPointer;
  ULONG EventWritePointer;
  UCHAR FinalizeAndRetire[ADMISSION_TA_FINALIZE_RETIRE_BYTES];
  UCHAR EventCount[4u];
  UCHAR JobList[24u];
  UCHAR PendingStamps[ADMISSION_REGIONC_PENDING_STAMPS_BYTES];
} ADMISSION_TA_RETIRE_RECEIPT;

#define ADMISSION_TA_TEMPORAL_RECEIPT_VERSION 1u
#define ADMISSION_TA_TEMPORAL_SAMPLE_COUNT 2u
typedef struct _ADMISSION_TA_TEMPORAL_SAMPLE {
  ULONG ElapsedMs;
  UCHAR TaStamps[ADMISSION_TA_STAMP_BYTES];
  UCHAR TimestampTargets[ADMISSION_TA_TIMESTAMP_TARGET_BYTES];
  UCHAR WorkTimestampTail[ADMISSION_TA_WORK_TIMESTAMP_TAIL_BYTES];
} ADMISSION_TA_TEMPORAL_SAMPLE;
typedef struct _ADMISSION_TA_TEMPORAL_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG SampleCount;
  ADMISSION_TA_TEMPORAL_SAMPLE Samples[ADMISSION_TA_TEMPORAL_SAMPLE_COUNT];
} ADMISSION_TA_TEMPORAL_RECEIPT;

#define ADMISSION_KTRACE_RECEIPT_VERSION 1u
#define ADMISSION_KTRACE_ENTRY_BYTES 0x38u
#define ADMISSION_KTRACE_ENTRY_COUNT 16u
typedef struct _ADMISSION_KTRACE_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG InitialWritePointer;
  ULONG FinalWritePointer;
  ULONG Reserved[3];
  UCHAR Entries[ADMISSION_KTRACE_ENTRY_COUNT][ADMISSION_KTRACE_ENTRY_BYTES];
} ADMISSION_KTRACE_RECEIPT;

#define ADMISSION_EVENT_DRAIN_RECEIPT_VERSION 1u
typedef struct _ADMISSION_EVENT_DRAIN_RECEIPT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG PollGuard;
  ULONG DrainGuard;
  ULONG ReadPointer;
  ULONG WritePointer;
  ULONG IngestGuard;
  ULONG RuntimeResult;
  ULONG MessageValid;
  UCHAR Message[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
} ADMISSION_EVENT_DRAIN_RECEIPT;

#define ADMISSION_QUEUE_FAULT_SNAPSHOT_VERSION 2u
#define ADMISSION_QUEUE_FAULT_REGIONB_WORDS 32u
#define ADMISSION_QUEUE_FAULT_REGIONC_WORDS 6u
typedef struct _ADMISSION_QUEUE_FAULT_SNAPSHOT {
  ULONG Version;
  ULONG Bytes;
  ULONG Fence;
  ULONG ElapsedMs;
  ULONG TaChannelReadPointer;
  ULONG D3ChannelReadPointer;
  ULONGLONG SgxFaultInfo;
  ULONG RegionBFault[ADMISSION_QUEUE_FAULT_REGIONB_WORDS];
  ULONG RegionCFault[ADMISSION_QUEUE_FAULT_REGIONC_WORDS];
} ADMISSION_QUEUE_FAULT_SNAPSHOT;

#define ADMISSION_CPU_PACKET_PAGING 1u
#define ADMISSION_CPU_PACKET_PRESENT 2u
/* EXP1003: completes in order with no work (residency touch). */
#define ADMISSION_CPU_PACKET_NOP 3u
typedef struct _ADMISSION_CPU_PACKET {
  ULONG Fence, Kind, Bytes;
  LONGLONG SubmitQpc; /* EXP1052 receipt: SubmitCommand time. */
  struct _ADMISSION_RENDER_CONTEXT *PresentContext;
  union {
    ADMISSION_PAGING_RECORD Paging[ADMISSION_MAX_PAGING_RECORDS];
    UCHAR Present[ADMISSION_PRESENT_BLT_DMA_MAX];
  } Data;
} ADMISSION_CPU_PACKET;

typedef struct _ADMISSION_POST_DPC_HEALTH_RECEIPT {
  ULONG Version, Bytes, Fence, SchedulerFaulted;
  ULONG CurrentFence, ActiveFence, DispatchedFence, RenderPacketState;
  ULONG BackendPhase, ProviderPhase, CompletionPhase, CompletedOutputPhase;
  ULONG OutputQueuePhase, WorkScheduled, WorkersActive, DpcPending;
} ADMISSION_POST_DPC_HEALTH_RECEIPT;

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#define ADMISSION_G3_LEAF_HISTORY_COUNT 256u
/* Bounded event history, with no guaranteed time coverage; failure snapshots
 * keep 256 records, matching events (same table, VA or allocation) first. */
#define ADMISSION_G3_LEAF_RING 8192u
#define ADMISSION_G3_LEAF_EVENT_RESET 0x10u
#define ADMISSION_G3_LEAF_EVENT_SYSTEM_RETIRE 0x20u
/* Previous logical owner of pages invalidated at a leaf's logical commit.
 * Count/ValidCount are old valid 4-KiB pages, not new/native mappings.
 * Status=success names this commit, not the enclosing paging DDI result.
 * Allocation is a historical token, never safe to dereference. */
#define ADMISSION_G3_LEAF_EVENT_UNMAP_OWNER 0x40u
/* Flags: 1 Use64KBPages, 2 Repeat, 4 NotifyEviction, 8 InitialUpdate. */
typedef struct _ADMISSION_G3_LEAF_HISTORY {
  ULONGLONG Qpc, ProcessId, TableIpa, Allocation, FirstVa, MappingGeneration;
  ULONG First, Count, ValidCount, Flags, Status, FirstSegment;
} ADMISSION_G3_LEAF_HISTORY;
/* EXP1052 receipt-only paging profile. Triplets are {count, ticks, max}.
 * Kept in memory on the paging path; published only from the DWM frame-arm
 * escape (PASSIVE, rate-limited, no flush). */
#define ADMISSION_PAGING_PROFILE_OPS 32u
typedef struct _ADMISSION_PAGING_PROFILE {
  volatile LONG64 Build[ADMISSION_PAGING_PROFILE_OPS][3];
  volatile LONG64 UpdatePageTableEntries;
  volatile LONG64 QueueToDispatch[3], DispatchToWorker[3], Worker[3];
  volatile LONG64 NotifyToDpc[3];
  volatile LONG64 LastPublishedQpc;
} ADMISSION_PAGING_PROFILE;
/* EXP1121 receipt-only: the last 128 VIRTUAL_TRANSFER, VIRTUAL_FILL and
 * UPDATE_PAGE_TABLE operations VidMm built, kept in memory on the paging
 * path and published with the paging profile. */
#define ADMISSION_PAGING_CENSUS_ENTRIES 128u
typedef struct _ADMISSION_PAGING_CENSUS_ENTRY {
  ULONGLONG Qpc;
  ULONG Operation, Detail, Flags, Entries;
  ULONGLONG Allocation, Process, Offset, Bytes, SourceVa, DestinationVa;
} ADMISSION_PAGING_CENSUS_ENTRY;
typedef struct _ADMISSION_PAGING_CENSUS {
  ULONG Version, Bytes;
  volatile LONG Next;
  ULONG Reserved;
  ADMISSION_PAGING_CENSUS_ENTRY Entries[ADMISSION_PAGING_CENSUS_ENTRIES];
} ADMISSION_PAGING_CENSUS;
/* EXP1131 receipt-only: display-path DDI calls, to time what the KMD was
 * asked to show while DWM rebuilt its primaries (Settings close). Kind 1
 * visibility (Detail = Visible) and 2 CommitVidPn (Flags = power
 * transition | powered off << 1, Detail = 1 when powered off/no functional
 * VidPn, Allocation = hPrimaryAllocation) are always kept.
 * Flips, 3 SetVidPnSourceAddress (Flags, Address, Allocation) and 4 MPO3
 * (Detail = PlaneCount | Enabled << 8, Flags = plane input flags), arrive
 * ~60/s (an MPO3 flip also reaches SetVidPnSourceAddress), so one is kept
 * only after a gap of more than 50 ms since the previous flip of its kind or
 * when its flags or detail change; Gap is then the microseconds since that
 * flip, and Flips counts every flip of both kinds. */
#define ADMISSION_DISPLAY_RING_ENTRIES 256u
typedef struct _ADMISSION_DISPLAY_RING_ENTRY {
  ULONGLONG Qpc, Address, Allocation;
  ULONG Kind, Flags, Detail, Gap;
} ADMISSION_DISPLAY_RING_ENTRY;
typedef struct _ADMISSION_DISPLAY_RING {
  ULONG Version, Bytes;
  volatile LONG Next;
  volatile LONG Flips;
  LONGLONG LastFlipQpc[2];
  ULONG LastFlipFlags[2], LastFlipDetail[2];
  ADMISSION_DISPLAY_RING_ENTRY Entries[ADMISSION_DISPLAY_RING_ENTRIES];
} ADMISSION_DISPLAY_RING;
/* EXP1123 receipt-only: the last 256 allocation creations (Kind 1) and
 * destructions (Kind 2), to name the creator and lifetime of allocations
 * VidMm fills and transfers (census Allocation is the same handle).
 * Flags: bit0 CpuVisible, bit1 WrittenPrimary, bit2 Presentation,
 * bits 8-15 preferred segment, bits 16-23 read segment set. */
#define ADMISSION_ALLOCATION_LIFE_ENTRIES 256u
typedef struct _ADMISSION_ALLOCATION_LIFE_ENTRY {
  ULONGLONG Qpc, Allocation, Bytes;
  ULONG Kind, ProcessId, Width, Height;
  ULONG Format, Type, ClassId, Flags, CreateFlags, Reserved;
} ADMISSION_ALLOCATION_LIFE_ENTRY;
typedef struct _ADMISSION_ALLOCATION_LIFE {
  ULONG Version, Bytes;
  volatile LONG Next;
  ULONG Reserved;
  ADMISSION_ALLOCATION_LIFE_ENTRY Entries[ADMISSION_ALLOCATION_LIFE_ENTRIES];
} ADMISSION_ALLOCATION_LIFE;
/* EXP987 receipt-only: R155/R165 BuildPagingBuffer waits for a process's
 * in-flight job. Snapshot of the condition when the wait began. */
#include "render_live_paging_wait.h"

typedef struct _ADMISSION_G3_PAGING_WAIT_RECEIPT {
  ULONG Version, Bytes, Waits, Timeouts;
  ULONGLONG TotalIterations, TotalTicks, QpcFrequency;
  ULONGLONG MaxTicks, MaxQpc, MaxGraphProcessId, MaxActiveGraphProcessId;
  ULONG MaxIterations, MaxOperation, MaxJobInFlight, MaxLease;
  ULONG MaxActiveIsProcess, MaxActiveFence, MaxLastCompletedFence,
      MaxPrivateCompletionFence;
} ADMISSION_G3_PAGING_WAIT_RECEIPT;
/* EXP988: QUERY predicate57 re-validation (PTE not yet populated by VidMm). */
typedef struct _ADMISSION_G3_PTE_WAIT_RECEIPT {
  ULONG Version, Bytes, Waited, Recovered, TimedOut, MaxIterations;
  ULONGLONG MaxTicks, TotalTicks, QpcFrequency, LastVa;
} ADMISSION_G3_PTE_WAIT_RECEIPT;
/* EXP990 receipt-only: what the GPU will read for a G4 render, sampled at
 * BeginJob through the process logical PTEs (bit0 resolved, bit1 all valid). */
typedef struct _ADMISSION_G4_DRAW_SNAP {
  ULONG Fence, Flags, PppCtrl, Width, Height, BgUsc, EotUsc, Process;
  ULONGLONG VdmBase, ScissorBase, DbiasBase, VdmIpa;
  ULONG VdmState, ScissorState, DbiasState, Reserved;
  UCHAR Vdm[256];
  UCHAR Scissor[32];
  UCHAR Dbias[16];
  /* EXP991: chain decoded from the VDM stream at job time. */
  ULONGLONG PppAddr[4], PipeAddr;
  ULONG PppState[4], PipeState, PppCount, IndexWord, IndexAt;
  UCHAR Ppp[4][64];
  UCHAR Pipe[64];
} ADMISSION_G4_DRAW_SNAP;
#define ADMISSION_G4_DRAW_SNAP_COUNT 4u
typedef struct _ADMISSION_G4_DRAW_SNAPSHOT {
  ULONG Version, Bytes, Next, Reserved;
  ADMISSION_G4_DRAW_SNAP Slot[ADMISSION_G4_DRAW_SNAP_COUNT];
} ADMISSION_G4_DRAW_SNAPSHOT;
/* EXP992 receipt-only: final firmware TA/3D/microsequence bytes of a 77x77
 * marker render at BeginJob (template objects 19, 18, 15, 17). */
typedef struct _ADMISSION_G4_FW_SNAP {
  ULONG Fence, Sizes[4];
  ULONGLONG GpuVa[4];
  UCHAR Ta[1564];
  UCHAR D3[2420];
  UCHAR Seq15[512];
  UCHAR Seq17[512];
  /* EXP993: native-graph (GPU table) inspection of the TA buffers. */
  ULONGLONG CheckVa[10], PrivateVa;
  ULONG CheckOk[10], CheckReason[10], CheckLevel[10], CheckCount;
} ADMISSION_G4_FW_SNAP;
typedef struct _ADMISSION_G4_FW_SNAPSHOT {
  ULONG Version, Bytes, Next, Reserved;
  ADMISSION_G4_FW_SNAP Slot[4];
} ADMISSION_G4_FW_SNAPSHOT;
#define ADMISSION_G3_ALLOC_TRACK_COUNT 1024u
/* EXP982: last valid leaf mapping per VidMm allocation handle. */
typedef struct _ADMISSION_G3_ALLOC_TRACK {
  ULONGLONG Allocation, ProcessId, LastValidVa, LastValidQpc, LastAnyQpc, LastAnyVa;
  ULONG LastValidCount, Maps, LastSegment, LastFlags;
  /* EXP983: physical/virtual FILL and TRANSFER paging operations. */
  ULONGLONG LastFillQpc, LastTransferQpc;
  ULONG Fills, Transfers, LastPagingSegment, LastPagingOperation;
  /* T7: copy while UpdatePageTable owns hAllocation. A submit refusal may
   * inspect this record later without dereferencing an expired handle. */
  ULONGLONG AllocationBytes;
  ULONG ClassId, Win32Flags, AllocationType, CpuVisible, MetadataValid, LastStatus;
} ADMISSION_G3_ALLOC_TRACK;
typedef struct _ADMISSION_G3_LEAF_HISTORY_SNAPSHOT {
  ULONG Version, Bytes, Next, Predicate;
  ULONGLONG FailVa, FailProcessId, FailTableIpa, FailAllocation, Qpc, QpcFrequency;
  ULONG FailIndex, Reserved;
  ADMISSION_G3_LEAF_HISTORY Records[ADMISSION_G3_LEAF_HISTORY_COUNT];
  /* EXP982 (Version 2): failing allocation and raw failing PTE. */
  ULONGLONG KmdAllocation, AllocationSize, PteGuestIpa, PteAllocation, PteAllocationOffset;
  ULONG AllocationType, PteFound, PteSegment, PteFlags, TrackFound, TrackReserved;
  ADMISSION_G3_ALLOC_TRACK Track;
} ADMISSION_G3_LEAF_HISTORY_SNAPSHOT;
#endif

/* Phase 5a (docs/superpowers/plans/2026-10-10-kmd-cross-context-render-
 * queue.md): SubmitCommandVirtual runs on dxgkrnl's VidSch worker thread, so
 * waiting there for the single render slot stalled the whole GPU scheduler
 * (EXP1130: ~1 s of 15.9 s; EXP1133). A submission of a context with nothing
 * outstanding is validated and copied here instead, its fence queued in
 * order; the platform worker binds it when the slot empties. One job per
 * context is still the rule (FenceOutstanding). */
/* EXP1135 receipt-only: which path G4 submissions took and how long
 * SubmitCommandVirtual waited for the render slot, by the first busy
 * ReadyEx predicate: [0] 4 backend submitted, [1] 8 worker finishing,
 * [2] 16 slot occupied, [3] other. QPC units. */
/* v2 (receipt-only, R9 A.6): why a G4 submission was not admissible to the
 * pending queue at its one decision (AdmissionG4PendingAdmissible), the
 * first failing predicate in code order, and how often the context limit
 * failed while the context's oldest fence had already been reported to
 * dxgkrnl but not yet retired (ADMISSION_CONTEXT.SubmitReportedFence).
 * Requeued: waits that ended on the deferred path (Wom1SubmitRequeue). */
#include "render_submit_path.h"
typedef struct _ADMISSION_SUBMIT_PATH {
  ULONG Version, Bytes;
  volatile LONG Immediate, Deferred, Binds, Reserved;
  volatile LONG Waits[4];
  volatile LONG64 WaitQpc[4];
  volatile LONG64 MaxWaitQpc;
  /* v2: [reason - 1] */
  volatile LONG NotAdmissible[ADMISSION_SUBMIT_NOT_REASONS];
  volatile LONG ContextLimitReported;
  volatile LONG Requeued;
} ADMISSION_SUBMIT_PATH;
#define ADMISSION_G4_PENDING_CAPACITY 2u
typedef enum _ADMISSION_G4_PENDING_STATE {
  AdmissionG4PendingFree = 0,
  AdmissionG4PendingFilling, /* reserved by the submitter, not yet counted */
  AdmissionG4PendingQueued,
  AdmissionG4PendingBinding,
  AdmissionG4PendingDropped = 4,
  /* rc90 build-ahead: bound into the backend image while the previous job
   * runs; the worker that ran it promotes or unbinds it (G4PrebindFence). */
  AdmissionG4PendingPrebound = 5,
  /* rc102 depth-2 window: a Prebound entry kicked to the firmware behind
   * the running job (scheduler KickedFence). Neither a preemption nor a
   * reset hands it back to VidSch; it becomes the render packet when it
   * is adopted (AdmissionG4PendingAdoptLocked). */
  AdmissionG4PendingKicked = 6,
} ADMISSION_G4_PENDING_STATE;
struct _ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G4_PENDING {
  ADMISSION_G4_PENDING_STATE State;
  ULONG Fence;
  struct _ADMISSION_RENDER_CONTEXT *Context;
  ADMISSION_RENDER_PACKET_DESCRIPTION Packet;
  /* Native, Render and Attachments point into this entry at bind time. */
  APPLE_AGX_G4_SUBMIT_VIEW View;
  ULONGLONG MappingGeneration, DmaBufferVa;
  ULONG DmaBufferBytes;
  /* Every attachment of the submission (colour targets, depth, stencil),
   * named to the firmware when the entry binds (D3D-G2a/G2b). */
  APPLE_AGX_G4_ATTACHMENT Attachments[APPLE_AGX_G4_MAX_ATTACHMENTS];
  unsigned char Render[sizeof(APPLE_AGX_G4_NATIVE_RENDER)];
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
  /* G2: a compute entry binds this envelope; View, Attachments, Render and
   * Native are unused. */
  BOOLEAN Compute;
  APPLE_AGX_G4_COMPUTE_ENVELOPE ComputeEnvelope;
  /* G2 HW2b: a present blt compute entry binds this job instead. */
  BOOLEAN Present;
  ADMISSION_PRESENT_COMPUTE PresentJob;
} ADMISSION_G4_PENDING;

typedef struct _ADMISSION_CONTEXT {
  ADMISSION_OBJECT_ADAPTER ObjectAdapter;
  ADMISSION_MEMORY_CONTRACT Memory;
  PVOID MemoryRuntime;
  /* M2-f2: segment-3 pool (Windows memory the KMD allocated at start). */
  PMDL PoolMdl;
  PVOID PoolCpu;
  ULONGLONG PoolIpa; /* hypervisor window base (segment 3 CPU base) */
  ULONGLONG PoolBytes;
  /* M2-f6: GPU broker table bytes m1n1 published (0: built-in 2 GiB). */
  ULONGLONG PoolBrokerBytes;
  /* Pool pages kept because the window could not release them. */
  volatile LONG PoolLeaks;
  /* M2-f3: segment 3 backed on demand (pool_windows.c); NULL for the fixed
   * pool or no pool. */
  PVOID PoolDemand;
  /* RC13: boot-only opt-in; GPU-only General may spill to system backing. */
  BOOLEAN SysmemSpill;
  /* RC23: requested policy only; eligible creates refuse until bridge exists. */
  BOOLEAN SysmemShadow;
  /* Sequence number of the last encoded paging record. */
  volatile LONG64 PagingRecordSeq;
  PVOID PlatformRuntime;
  PVOID ScanoutRuntime;
  APPLE_AGX_SOFTWARE_APERTURE_ENTRY *ApertureEntries;
  PDEVICE_OBJECT PhysicalDeviceObject;
  DXGK_START_INFO StartInfo;
  DXGKRNL_INTERFACE Interface;
  DXGK_DEVICE_INFO DeviceInformation;
  APPLE_AGX_LOCAL_RESERVE_RECEIPT LocalReserveReceipt;
  DXGK_DISPLAY_INFORMATION PostDisplayInformation;
  BOOLEAN InterfaceValid;
  BOOLEAN Started;
  BOOLEAN DisplayActive;
  BOOLEAN SourceVisible;
  ULONG CommittedWidth;
  ULONG CommittedHeight;
  ULONG CommittedStride;
  D3DDDIFORMAT CommittedFormat;
  /* rc106: D3DKMDT_VPPS_* transform of the committed source to the panel. */
  ULONG CommittedScaling;
  ADMISSION_DISPLAY_MODES_RECEIPT DisplayModes;
  volatile LONG SourceAddressStage;
  volatile LONG SourceAddressStatus;
  volatile LONG SourceAddressReceiptState;
  ADMISSION_SOURCE_ADDRESS_RECEIPT SourceAddressReceipt;
  /* EXP928: lifetime observations for the immutable first selected primary. */
  volatile LONG SelectedPrimaryDestroyEnter;
  volatile LONG SelectedPrimaryDestroySuccess;
  volatile LONG SelectedPrimaryDestroyFailure;
  volatile LONG PaletteStatus;
  volatile LONG ScanLineStage;
  volatile LONG ScanLineStatus;
  volatile UCHAR *BrokerBase;
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
  PVOID GpuvaB1State;
#endif
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  PVOID GpuvaG3State;
  volatile LONG G3CopyQueryFailureClaim;
  ULONG G3CopyQueryFailurePredicate, G3CopyQueryFailureStatus;
  APPLE_AGX_G3_COPY_QUERY_RECEIPT G3CopyQueryFailure;
  volatile LONG G3CopyTransferFailureClaim;
  APPLE_AGX_G3_COPY_TRANSFER_FAILURE G3CopyTransferFailure;
  /* EXP979 diagnostic: leaf UpdatePageTable history frozen at the first
   * copy predicate57 (PTE present but not valid). */
  volatile LONG G3LeafHistoryClaim;
  ADMISSION_G3_LEAF_HISTORY_SNAPSHOT G3LeafHistorySnapshot;
  ADMISSION_G3_PAGING_WAIT_RECEIPT G3PagingWait;
  /* EXP1026: paging operations completed after a disallowed internal status. */
  volatile LONG G3PagingContractCompletions;
  volatile LONG G3PagingContractLastStatus;
  ADMISSION_G3_PTE_WAIT_RECEIPT G3PteWait;
  ADMISSION_G4_DRAW_SNAPSHOT G4DrawSnapshot;
  ADMISSION_G4_FW_SNAPSHOT G4FwSnapshot;
  volatile LONG G4DrawSnapshotDirty;
  volatile LONG G3PteWaitDirty;
  volatile LONG G3PagingWaitDirty;
  /* EXP1073: paging-DDI receipts staged for a work item (receipts.c). */
  PVOID PagingReceipts;
  ADMISSION_PAGING_PROFILE PagingProfile;
  ADMISSION_PAGING_CENSUS PagingCensus;
  ADMISSION_ALLOCATION_LIFE AllocationLife;
  ADMISSION_DISPLAY_RING DisplayRing;
  LONGLONG PagingDispatchQpc, PagingNotifyQpc;
  /* EXP997 diagnostic: first poisoned G3 process seen at completion. */
  volatile LONG G3PoisonClaim;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  ADMISSION_DWM_FRAME_PROBE DwmFrameProbe;
  volatile LONG DwmSourceMapRecordCount;
  volatile LONG DwmSourceMapInRangeCount;
#endif
  volatile LONG G3PrivateFailureClaim;
  APPLE_AGX_G3_PRIVATE_FAILURE G3PrivateFailure;
  volatile LONG G4SubmitFailureClaim;
  volatile LONG G4SubmitFailureCount;
  /* EXP1901N receipt-only (Wom1G4BindRefusal, published with
   * Wom1G4SubmitFailure): the backend image when a prepared job first could
   * not bind it (1577 Bind refusals, then the desktop stayed black). */
  volatile LONG G4BindRefusalClaim;
  struct _ADMISSION_G4_BIND_REFUSAL {
    ULONG Version, Bytes, Kind, Fence, EntryMs;
    ULONG Ready, BoundFence, JobFence, JobReady, NativeBound, G4Native;
    ULONG G4Compute, PresentCompute;
    ULONG CompletedFence, LastSubmittedFence, ActiveFence, PreemptionPhase;
    ULONG PacketState, PacketFence, DispatchedFence, RenderWorkerScheduled;
    ULONG BindFailure; /* version 2: ADMISSION_BACKEND_IMAGE.BindFailure */
  } G4BindRefusal;
  struct _ADMISSION_G4_SUBMIT_FAILURE {
    ULONG Version, Bytes, Branch, Status, DownstreamStatus;
    ULONGLONG DmaBufferVirtualAddress;
    ULONG DmaBufferSize, PrivateDataSize, UmdPrivateDataSize;
    ULONG Flags, ContextFlags, Pid, TotalFailures;
    ULONG Subsite, Kind, Ordinal, AccessBytes, Write, GraphPresent;
    ULONGLONG Va, OwnerProcessId, RootIpa, ProcessGeneration,
        MappingGeneration;
    ULONGLONG LogicalIpa[4];
    ULONG LogicalSegment[4], LogicalFlags[4];
    /* Version 3: identities of the four logical 4-KiB pages and the last
     * paging record for the failing page. TrackFound=0 means unknown (the
     * bounded track table can collide), never proof of non-residency. */
    ULONGLONG LogicalAllocation[4], LogicalAllocationOffset[4];
    ULONG TrackFound, OwnerOsProcessId;
    ADMISSION_G3_ALLOC_TRACK Track;
    /* Version 4: Prepare rejection snapshot; v3 prefix stays byte-identical.
     * PrepareMask/Site are the stable ABI in gpuva_g3_windows.c. */
    ULONG PrepareMask, PrepareSite, PrepareFence, PrepareFenceOutstanding;
    ULONG PrepareSecondFence, PreparePendingCount, PreparePendingCapacity;
    ULONG PrepareTail, PrepareTailState, PreparePacketState;
    ULONG PrepareSchedulerFaulted, PreparePreemptionPhase;
    ULONG PrepareSchedulerQueueCount, PrepareSchedulerQueueCapacity;
    ULONG PrepareSchedulerLastSubmittedFence, PrepareEntryState;
  } G4SubmitFailure;
  /* EXP1210 receipt-only (Wom1G4PreemptOverlap): DxgkDdiPreemptCommand calls
   * and those that arrived while a DxgkDdiSubmitCommandVirtual call was still
   * inside the driver, with the fences of the first and the last such call. */
  volatile LONG G4SubmitVirtualInFlight;
  volatile LONG G4SubmitVirtualFence;
  volatile LONG G4SubmitVirtualFlags;
  volatile LONG G4PreemptOverlapClaim;
  volatile LONG G4PreemptOverlapDirty;
  struct _ADMISSION_G4_PREEMPT_OVERLAP {
    ULONG Version, Bytes;
    volatile LONG Preemptions, Overlaps, Submissions, Resubmissions;
    ULONG FirstPreemptionFence, FirstSubmitFence, FirstSubmitFlags;
    volatile LONG LastPreemptionFence, LastSubmitFence, LastSubmitFlags;
  } G4PreemptOverlap;
#endif
  volatile LONG InterruptReady;
  volatile LONG InterruptIngressEnabled;
  volatile LONG InterruptCount;
  volatile LONG InterruptAckCount;
  volatile LONG LastInterruptStatus;
  volatile LONG DpcCount;
  /* IRQ-completion step 1 (investigation/analysis/KMD-IRQ-COMPLETION.md):
   * translated vectors of the synthetic scanout edge and, when the ACPI
   * device lists it (Mu AGX_MAILBOX_IRQ), of the ASC mailbox recv-not-empty
   * level line. MailboxInterruptVector == 0 keeps today's single-line ISR. */
  ULONG ScanoutInterruptVector;
  ULONG MailboxInterruptVector;
  /* The platform runtime as the mailbox ISR and DPC may use it: set with the
   * ASC mapping when the line is listed, cleared under the interrupt lock
   * (and queued DPCs flushed) before the ASC loses power, its mapping, or
   * the runtime is freed. They never reach the runtime through
   * PlatformRuntime, which is cleared without that synchronization. */
  PVOID volatile MailboxIsrRuntime;
  /* Receipt-only ISR census: calls per MessageNumber (0..2, 3 = other),
   * mailbox-handled calls, the MessageNumbers that carried mailbox work,
   * scanout-handled calls. Learn how dxgkrnl numbers two line interrupts. */
  volatile LONG IrqIsrByMessage[4];
  volatile LONG IrqMailboxHandled;
  volatile LONG IrqMailboxMessageMask;
  volatile LONG IrqScanoutHandled;
  KSPIN_LOCK PagingLock;
  PIO_WORKITEM PagingWorkItem;
  KEVENT PagingIdle;
  /* R161: records encoded by BuildPagingBuffer but not yet handed to
   * SubmitCommand; their memory effects are still pending. */
  volatile LONG PagingRecordsUnsubmitted;
  ADMISSION_PAGING_RECORD PagingRecords[ADMISSION_MAX_PAGING_RECORDS];
  ULONG PagingRecordCount;
  ULONG PresentCopyBytes;
  struct _ADMISSION_RENDER_CONTEXT *PresentCopyContext;
  ULONG CpuQueueHead, CpuQueueCount, DispatchedFence;
  ADMISSION_CPU_PACKET CpuQueue[APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
  UCHAR PresentCopyCommand[ADMISSION_PRESENT_BLT_DMA_MAX];
  ULONGLONG PresentCopyFaultVa;
  ULONG PresentCopyFaultWrite;
  volatile LONG PresentTransferState;
  ADMISSION_PRESENT_TRANSFER_RECEIPT PresentTransferReceipt;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  volatile LONG SubmitTraceClaimed;
  volatile LONG UmdRenderTraceClaimed;
  volatile LONG OpenAllocationTraceClaimed;
  volatile LONG PagingBuildTraceClaimed;
  volatile LONG PagingCorrelationArmed;
  volatile LONG GdiReceiptClaimed;
  volatile LONG GdiSubmitTraceClaimed;
  KSPIN_LOCK GdiReceiptLock;
  ADMISSION_GDI_HW_RECEIPT GdiReceipt;
  volatile LONG PostDpcHealthValid;
  ADMISSION_POST_DPC_HEALTH_RECEIPT PostDpcHealth;
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  KSPIN_LOCK RenderCorrelationLock;
  PIO_WORKITEM RenderCorrelationWorkItem;
  KEVENT RenderCorrelationIdle;
  volatile LONG RenderCorrelationDirty;
  volatile LONG RenderCorrelationWorkerQueued;
  volatile LONG RenderCorrelationStopping;
  ADMISSION_RENDER_CORRELATION_STATE RenderCorrelation;
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  volatile LONG StandardPresentTraceArmed;
  volatile LONG StandardPresentTraceNext;
  volatile LONG StandardPresentTraceOverflow;
  ADMISSION_STANDARD_PRESENT_TRACE StandardPresentTrace;
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  ADMISSION_DWM_DDI_PROBE DwmDdiProbe;
#endif
#if defined(APPLE_AGX_BLT_PROBE_QUALIFICATION)
  ADMISSION_BLT_PROBE BltProbe;
#endif
  UINT PagingFence;
  UINT PagingLastSubmittedFence;
  UINT PagingLastCompletedFence;
  NTSTATUS PagingCompletionStatus;
  volatile LONG PagingPending;
  volatile LONG PagingWorkersActive, PagingDpcsActive;
  volatile LONG PagingCallbacksActive; /* includes final dispatch/retry tail */
  volatile LONG PagingStopping;
  volatile LONG PagingDpcPending;
  volatile LONG MemoryStartStage;
  volatile LONG MemoryStartStatus;
  KSPIN_LOCK SchedulerLock;
  APPLE_AGX_SCHEDULER Scheduler;
  /* Q22b: independent external node1, local scheduler node0. */
  APPLE_AGX_SCHEDULER PagingScheduler;
  ULONG LiveAdditivePaging, PagingActiveNode;
  /* GC (experiment): Wom1GpuPrivateCached = 1 and the broker capability. */
  ULONG GpuPrivateCached;
  /* V1 (experiment): Wom1G3RetainedSlots = 1 (REG_DWORD), read once at
   * start: per-process VM slots kept between jobs (gpuva_g3_private.h). */
  ULONG G3RetainedSlots;
  /* C1: Wom1CrossProcessPairs = 1 and its prerequisites, set at start with
   * ADMISSION_JOB_WINDOW_CAP_CROSS_PROCESS: BeginSuccessorJob may begin a
   * compute successor of another process on its own retained lease. */
  ULONG G3CrossProcessPairs;
  /* K1: Wom1CrossProcessRenderPairs = 1 on top of C1, set at start with
   * ADMISSION_JOB_WINDOW_CAP_XPROC_RENDER: the successor of another process
   * may also be a native render with a private scene. */
  ULONG G3CrossProcessRenderPairs;
  volatile LONG PagingAdapterResetting;
  ULONG PagingNodeHead, PagingNodeCount, PagingNodeDispatchedFence;
  ADMISSION_CPU_PACKET PagingNodeQueue[APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
  ADMISSION_RENDER_PACKET RenderPacket;
  /* Phase 5a: validated G4 submissions of other contexts waiting for the
   * render slot, oldest first, under SchedulerLock (see
   * ADMISSION_G4_PENDING). The worker binds the head into RenderPacket. */
  ADMISSION_G4_PENDING G4Pending[ADMISSION_G4_PENDING_CAPACITY];
  ULONG G4PendingHead, G4PendingCount;
  /* rc90 build-ahead: fence of the head entry bound (Prebound, or Binding/
   * Dropped while the worker binds or unbinds it) into BackendImage while
   * RenderPacket still holds the running job; 0 = none. Worker-owned,
   * under SchedulerLock. */
  ULONG G4PrebindFence;
  ADMISSION_SUBMIT_PATH SubmitPath;
  /* Receipt-only (SubmitPath v2): the fence the completion worker has
   * reported to dxgkrnl and not yet retired from its context; 0 = none.
   * Written by the worker (cleared under SchedulerLock with the retire),
   * compared under SchedulerLock by the submit decision. */
  volatile LONG SubmitReportedFence;
  /* F-A2 (R9 A.8): REG_DWORD Wom1SubmitRequeue = 1, read at start, default
   * 0. A submission that was not admissible keeps testing admissibility
   * while it waits and then takes the deferred path
   * (AdmissionG4SubmitAwait); 0: it waits for "ready and empty" only. */
  ULONG SubmitRequeue;
  ADMISSION_BACKEND_IMAGE BackendImage;
  /* EXP1105: Prepare image of the objects a G4 job changes. */
  ADMISSION_BACKEND_IMAGE_SNAPSHOT BackendSnapshot;
  volatile LONG SchedulerInitialized;
  /* Zero is healthy. First fault: high16=file (1 scheduler,2 paging,
   * 3 submission,4 backend), low16=source line in the exact build.
   * Boolean diagnostic/public fields must normalize this value to0/1. */
  volatile LONG SchedulerFaulted;
  volatile LONG SchedulerDpcPending;
  volatile LONG RenderDpcFence;
  ULONG Win32BootGeneration;
  /* EXP1317: SGX core topology read at the first power-on (Asahi regs.rs
   * get_gpu_id): ID_VERSION, ID_COUNTS_1, ID_CLUSTERS, CORE_MASK_0/1 as
   * read, and the derived cores per cluster/mask (0/0 when invalid). */
  ULONG GpuIdRegisters[5];
  ULONG GpuCoresPerCluster;
  ULONG GpuCoreMask;
  /* rc82: nonzero while the started runtime reports soft faults to the UMD
   * (AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS, receipt Wom1SoftFaultState). */
  volatile LONG GpuSoftFaults;
  /* OV2: nonzero while the started runtime runs Wom1Ov2NoHazardLead = 1 on
   * Q2c (AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE): the compute parser
   * then admits a VdmBarrier NONE envelope. */
  volatile LONG GpuIndependentSubqueue;
  /* N4c-v2: nonzero while the started runtime runs Wom1Ov2CrossLead = 1 on
   * Q2c (AGX_WIN32_DEVICE_FEATURE_OV2_CROSS_LEAD): the compute parser admits
   * the same VdmBarrier NONE envelope. */
  volatile LONG GpuOv2CrossLead;
  /* C2a: nonzero while the started runtime runs Wom1CrossProcessPrivateLead
   * = 1 (AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE): the compute parser
   * then admits APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY in the envelope header. */
  volatile LONG GpuPrivateOnlyCompute;
  /* OV5 step 2: nonzero while the started runtime runs Wom1Ov5TaWaiver = 1
   * with the 3D-queue Barrier path available
   * (AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT): the render parser
   * then admits a CdmBarrier NONE render header. */
  volatile LONG GpuRenderTaIndependent;
  volatile LONG FeatureReadyMask;
  /* EXP1321 receipt-only: broker failure count last staged (Wom1G3BrokerFailures). */
  volatile LONG64 G3BrokerFailuresStaged;
  /* G4-r3 (EXP1296): Wom1GpuReset; written by the reset thread only
   * (first-level synchronization). */
  ADMISSION_GPU_RESET_RECEIPT GpuReset;
  /* EXP1899N receipt-only (Wom1PreemptRefusal, published with Wom1GpuReset):
   * the scheduler at the first DxgkDdiPreemptCommand whose boundary
   * preemption could not begin (scheduler_windows.c), and the last one that
   * began. Written under the scheduler lock. */
  volatile LONG PreemptRefusalClaim;
  volatile LONG PreemptRefusals;
  ULONG PreemptBeganMs, PreemptBeganFence;
  struct _ADMISSION_PREEMPT_REFUSAL {
    ULONG Version, Bytes, Refusals;
    ULONG EntryMs, PreemptionFence, CutoffFence, ActiveFence, QueuedFence;
    ULONG CompletedFence, LastSubmittedFence, SchedulerQueuedFence, QueueCount;
    ULONG SchedulerActiveFence, PendingPreemptionFence, PreemptionCutoffFence;
    ULONG PreemptionActiveFence, PreemptionLastCompletedFence;
    ULONG PreemptionPending, Phase, Responsive;
    ULONG DispatchedFence, PacketState, PagingPending, PagingDpcPending;
    ULONG PagingDpcsActive, SchedulerDpcPending, RenderWorkerScheduled;
    ULONG BeganMs, BeganFence;
  } PreemptRefusal;
  /* G4-r3 TDR drill (ADMISSION_TDR_DRILL): armed by escape for one process. */
  BOOLEAN TdrDrillAllowed;
  BOOLEAN GpuFaultDrillAllowed;
  volatile LONG TdrDrillPid, TdrDrillJobs, TdrDrillFence;
  /* Firmware restart drill (ADMISSION_FW_RESTART_DRILL): device value
   * Wom1FwRestartDrillAllowed = 1, read once at runtime start. */
  BOOLEAN FwRestartDrillAllowed;
  /* S2: two-plane MPO (Wom1MpoTwoPlanes); untouched unless effective. */
  ADMISSION_MPO_RECEIPT Mpo;
  ADMISSION_MPO_GATE_RECEIPT MpoGate;
  /* EXP2357 (a): interrupt time of the last periodic Wom1MpoReceipt publish. */
  volatile LONG64 MpoPublishTime100ns;
  /* NP-1 (EXP2365): while DWM presents only through Set3 (two planes) no
   * DdiPresent runs, so the periodic publish stalled.  Set3 (IRQL up to
   * PROFILE_LEVEL - 1) queues this work item at most once a second when it
   * runs at <= DISPATCH_LEVEL; the item publishes at PASSIVE_LEVEL.
   * Allocated at scanout start with the MPO receipt, drained and freed at
   * scanout stop.  Receipt-only. */
  PIO_WORKITEM MpoPublishWork;
  volatile LONG MpoPublishQueued;
  KEVENT MpoPublishIdle;
  /* NP-2 (EXP2373: no Set3-side publish in steady windowed GM; dxgkrnl
   * calls Set3 above DISPATCH_LEVEL there): above DISPATCH_LEVEL the
   * request goes through this DPC, which queues the work item. */
  KDPC MpoPublishDpc;
  /* HOLD-1: the held plane set (mpo_hold.h) and its plane states; the
   * try-lock serialises Set3 and the scan-out ISR without waiting. */
  ADMISSION_MPO_HOLD MpoHold;
  ADMISSION_MPO_PLANE_STATE MpoHoldNext[2];
  volatile LONG MpoHoldLock;
  /* HOLD-2: the commit DPC (queued from Set3 when no swap is outstanding),
   * its readiness, the QPC of the first Set3 of the pending set, and the
   * timing ring. */
  KDPC MpoCommitDpc;
  volatile LONG MpoCommitDpcReady;
  LONGLONG MpoHoldFirstQpc;
  ADMISSION_MPO_TIMING MpoTiming;
  /* HOLD-3: the frame-deadline commit timer (high resolution), armed only
   * while a set is pending; the last vsync / latch, the last commit and the
   * planned deadline (QPC); the panel period and the lead (QPC). */
  PEX_TIMER MpoDeadlineTimer;
  volatile LONG MpoDeadlineArmed;
  volatile LONG64 MpoLastVsyncQpc;
  volatile LONG64 MpoLastCommitQpc;
  volatile LONG MpoLastCommitSource; /* HOLD-3c: ADMISSION_MPO_TIMING_COMMIT_* */
  ADMISSION_MPO_PHASE MpoPhase;      /* HOLD-3c: ISR-serialized frame phase */
  LONGLONG MpoDeadlineQpc;
  LONGLONG MpoPeriodQpc;
  LONGLONG MpoLeadQpc;
  ULONG MpoLeadUs;
  LONG MpoNoLatchRun[2];             /* HOLD-3b: current runs per layer */
  /* CUR-1 v2: which scan-out scratch DCP scans (mpo_layers.h). */
  ADMISSION_MPO_SCRATCH_STATE MpoScratch;
  ULONGLONG MpoScratchPoolOffset;   /* scratch 0; scratch 1 = + STEP */
  PUCHAR MpoScratchCpu;             /* write-combined view of scratch 0 */
  /* CUR-1c: cached copy of the visible block (read once from the pool). */
  UCHAR MpoScratchStage[ADMISSION_MPO_SCRATCH_BYTES];
  ADMISSION_MPO_PLANE_STATE MpoPlanes[2];
} ADMISSION_CONTEXT;

typedef struct _ADMISSION_DEVICE {
  ADMISSION_OBJECT_DEVICE Object;
  volatile LONG Win32Generation;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  PVOID GpuvaG3Process;
#endif
} ADMISSION_DEVICE;

typedef struct _ADMISSION_RENDER_CONTEXT {
  ADMISSION_OBJECT_CONTEXT Object;
  APPLE_AGX_SCHEDULER_CONTEXT SchedulerContext;
  ADMISSION_PREPATCHED_RENDER PrepatchedRender;
  ULONG Win32Generation;
  BOOLEAN Win32Transport;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  PVOID GpuvaG3Process;
  struct _ADMISSION_RENDER_CONTEXT *GpuvaG3NextContext;
  ULONGLONG GpuvaG3PrivateManagerGeneration;
  volatile LONG GpuvaG3PrivateFence, GpuvaG3CancelFence, GpuvaG3CancelUncertain, GpuvaG3PreemptFence;
  /* Phase 5b: a context may have two private G4 jobs queued. The second
   * uses these slot-2 markers; slots are matched by fence, never shifted,
   * so a DPC marking one slot cannot race a PASSIVE clear of the other. */
  volatile LONG GpuvaG3PrivateFence2, GpuvaG3CancelFence2, GpuvaG3PreemptFence2;
  /* Receipt-only (resubmission defect, branch 7 subsite 0x201): the caller
   * of the last AdmissionGpuvaG3PrivateCancel per slot, as its address minus
   * the function's own (resolved with the build's PDB). */
  volatile LONG GpuvaG3CancelCaller, GpuvaG3CancelCaller2;
  /* Phase 5b: FenceOutstanding is the context's oldest outstanding job;
   * this is the next one (queued after it), promoted when the oldest
   * retires. Changed under SchedulerLock. */
  ULONG GpuvaG3SecondFence;
  BOOLEAN GpuvaG3Closing;
  ULONGLONG GpuvaG3RootIpa;
  ULONGLONG GpuvaG3LastSetRootIpa;
  ULONG GpuvaG3SetRootCount;
  ULONGLONG GpuvaG3DmaBufferVa;
  ULONGLONG GpuvaG3MappingGeneration;
  ULONG GpuvaG3DmaBufferBytes;
  BOOLEAN GpuvaG3Poisoned;
#endif
} ADMISSION_RENDER_CONTEXT;

/* Phase 5b: the job with Fence left the context (completed, cancelled,
 * preempted or refused); the next outstanding job becomes the oldest.
 * Callers hold SchedulerLock. */
static __inline VOID AdmissionContextRetireFence(
    ADMISSION_RENDER_CONTEXT *Context, ULONG Fence) {
  if (Context == NULL || Fence == 0u)
    return;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (Context->Object.FenceOutstanding == Fence) {
    Context->Object.FenceOutstanding = Context->GpuvaG3SecondFence;
    Context->GpuvaG3SecondFence = 0u;
  } else if (Context->GpuvaG3SecondFence == Fence) {
    Context->GpuvaG3SecondFence = 0u;
  }
#else
  if (Context->Object.FenceOutstanding == Fence)
    Context->Object.FenceOutstanding = 0u;
#endif
}

typedef struct _ADMISSION_ALLOCATION_HANDLE {
  ADMISSION_ALLOCATION_OBJECT Object;
  ULONG QualificationCookie;
  ULONG Win32ClassId;
  ULONG Win32Flags;
  ULONG WrittenPrimary;
  /* EXP1060: a classless allocation created with presentation resource data
   * (a swapchain or primary surface); the copy escape may access it. */
  ULONG Presentation;
  /* rc110 (EXP2049): the only allocations the scanout gates accept: a plain
   * linear description (the D3D11 UMD's AIL-linear textures, the D3D12
   * presentation storage and the standard allocations the KMD describes
   * itself) of a GPU-only 32-bpp GDI-surface texture in a format the broker
   * scans; never an R105 native resource, whose layout the Mesa layer owns
   * (twiddled or compressed). */
  ULONG ScanoutCapable;
  /* Created-with refresh rate reported by DxgkDdiDescribeAllocation. */
  UINT PrimaryRefreshNumerator;
  UINT PrimaryRefreshDenominator;
  /* M2-f3: current segment-3 placement, guarded by the pool lock. */
  ADMISSION_POOL_PLACEMENT PoolPlacement;
  /* M2-f5: segment-3 pages reserved for this allocation at creation. */
  ULONGLONG PoolReserved;
  /* CUR-1 receipt: 1 + the local-segment (segment 2) offset of the
   * allocation's base from the last user-process page-table update that
   * mapped it consistently there; 0 unknown.  Receipt input only. */
  volatile LONGLONG LocalPlacement;
#ifdef APPLE_AGX_NATIVE_STAGING
  PVOID NativeStagingBinding;
#endif
} ADMISSION_ALLOCATION_HANDLE;

#define ADMISSION_OPEN_ALLOCATION_MAGIC 0x4f504152u
NTSTATUS AdmissionGpuvaG3NativeQueryEscape(ADMISSION_CONTEXT *Adapter,
    const DXGKARG_ESCAPE *Args);
#ifdef APPLE_AGX_NATIVE_STAGING
NTSTATUS AdmissionGpuvaG3NativeStagingEscape(ADMISSION_CONTEXT *Adapter,
    const DXGKARG_ESCAPE *Args);
#endif
NTSTATUS AdmissionGpuvaG4PreflightEscape(ADMISSION_CONTEXT *Adapter,
    const DXGKARG_ESCAPE *Args);
NTSTATUS AdmissionGpuvaG3NativeQueryBatchEscape(ADMISSION_CONTEXT *Adapter,
    const DXGKARG_ESCAPE *Args);
void AdmissionGpuvaG3InvalidateAllocation(ADMISSION_CONTEXT *Adapter,
    HANDLE Allocation);
NTSTATUS AdmissionGpuvaG3CopyEscape(ADMISSION_CONTEXT *Adapter,
                                  const DXGKARG_ESCAPE *Args);
typedef struct _ADMISSION_OPEN_ALLOCATION {
  ULONG Magic;
  ADMISSION_DEVICE *Device;
  D3DKMT_HANDLE RuntimeAllocation;
  ADMISSION_ALLOCATION_OBJECT *Allocation;
  BOOLEAN ReadOnly;
  ULONG Win32Generation;
  ULONG Win32ClassId;
  ULONG Win32Flags;
} ADMISSION_OPEN_ALLOCATION;

/* Observational only; fixed process slots preserve the standard client's result. */
VOID AdmissionRecordUmdRenderGuard(_In_opt_ ADMISSION_CONTEXT *Context,
                                   _In_reads_(16) const ULONG *Snapshot,
                                   _In_ ULONG Guard,
                                   _In_ NTSTATUS Status);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
NTSTATUS AdmissionDwmDdiProbeQueryWindows(
    _In_ ADMISSION_CONTEXT *Context, _Inout_ ADMISSION_DWM_DDI_PROBE *Query);
VOID AdmissionDwmDdiProbeRecordWindows(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_DWM_DDI_EVENT *Event);
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
NTSTATUS AdmissionStandardPresentTraceQueryWindows(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Inout_ ADMISSION_STANDARD_PRESENT_TRACE *Query);
VOID AdmissionStandardPresentTraceRecordWindows(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_STANDARD_PRESENT_EVENT *Event);
#endif
#if defined(APPLE_AGX_BLT_PROBE_QUALIFICATION)
NTSTATUS AdmissionBltProbeQueryWindows(
    _In_ ADMISSION_CONTEXT *Context, _Inout_ ADMISSION_BLT_PROBE *Query);
VOID AdmissionBltProbeRecordWindows(
    _In_ ADMISSION_CONTEXT *Context, _In_ const ADMISSION_BLT_EXECUTION *Event);
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
VOID AdmissionUmdRenderTraceArm(_In_ ADMISSION_CONTEXT *Context);
VOID AdmissionUmdRenderTraceDisarm(_In_ ADMISSION_CONTEXT *Context);
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
NTSTATUS AdmissionRenderCorrelationStartWindows(
    _Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionRenderCorrelationStopWindows(
    _Inout_ ADMISSION_CONTEXT *Context);
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
ULONG AdmissionRenderCorrelationBeginWindows(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_RENDER_CONTEXT *RenderContext,
    _In_opt_ const DXGKARG_RENDER *Args);
VOID AdmissionRenderCorrelationValidatedWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG CallSequence,
    _In_ ULONGLONG CommandHash, _In_ UINT AllocationCount,
    _In_ UINT DestinationIndex, _In_ UINT DestinationSegment,
    _In_reads_(2) const ULONGLONG *AllocationTokens);
VOID AdmissionRenderCorrelationExitWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG CallSequence,
    _In_ ULONG Guard, _In_ NTSTATUS Status, _In_ ULONG DmaBytes,
    _In_ ULONG Patches, _In_ BOOLEAN Prepatched);
VOID AdmissionRenderCorrelationPatchWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, _In_ ULONGLONG ContextToken,
    _In_ BOOLEAN Entry, _In_ ULONG Guard, _In_ NTSTATUS Status);
VOID AdmissionRenderCorrelationSubmitWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, _In_ ULONGLONG ContextToken,
    _In_ BOOLEAN Entry, _In_ ULONG Fence, _In_ ULONG Guard,
    _In_ NTSTATUS Status);
VOID AdmissionRenderCorrelationWorkerWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG Fence,
    _In_ BOOLEAN Entry, _In_ ULONG Status);
VOID AdmissionRenderCorrelationOutputWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG Fence,
    _In_ ULONG Stage, _In_ ULONG Status);
VOID AdmissionRenderCorrelationNotifyAtInterruptWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG Fence,
    _In_ ULONGLONG Timestamp, _In_ BOOLEAN QueueDpcResult);
VOID AdmissionRenderCorrelationSynchronizeWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG Fence,
    _In_ NTSTATUS Status, _In_ BOOLEAN CallbackResult);
VOID AdmissionRenderCorrelationDpcWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG Fence,
    _In_ ULONGLONG Timestamp);
VOID AdmissionRenderCorrelationQueryFenceWindows(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG Fence);
#else
#if !defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#define AdmissionRenderCorrelationStartWindows(Context) STATUS_SUCCESS
#define AdmissionRenderCorrelationStopWindows(Context) STATUS_SUCCESS
#endif
#define AdmissionRenderCorrelationBeginWindows(Context, RenderContext, Args) \
  (0u)
#define AdmissionRenderCorrelationValidatedWindows(                          \
    Context, CallSequence, CommandHash, AllocationCount, DestinationIndex,   \
    DestinationSegment, AllocationTokens)                                    \
  ((void)0)
#define AdmissionRenderCorrelationExitWindows(                               \
    Context, CallSequence, Guard, Status, DmaBytes, Patches, Prepatched)     \
  ((void)0)
#define AdmissionRenderCorrelationPatchWindows(                              \
    Context, ContextToken, Entry, Guard, Status)                             \
  ((void)0)
#define AdmissionRenderCorrelationSubmitWindows(                             \
    Context, ContextToken, Entry, Fence, Guard, Status)                      \
  ((void)0)
#define AdmissionRenderCorrelationWorkerWindows(                             \
    Context, Fence, Entry, Status)                                           \
  ((void)0)
#define AdmissionRenderCorrelationNotifyAtInterruptWindows(                  \
    Context, Fence, Timestamp, QueueDpcResult)                               \
  ((void)0)
#define AdmissionRenderCorrelationSynchronizeWindows(                        \
    Context, Fence, Status, CallbackResult)                                  \
  ((void)0)
#define AdmissionRenderCorrelationDpcWindows(Context, Fence, Timestamp)      \
  ((void)0)
#define AdmissionRenderCorrelationQueryFenceWindows(Context, Fence)          \
  ((void)0)
#endif

typedef struct _ADMISSION_PHYSICAL_ALLOCATION {
  PDXGKRNL_INTERFACE Interface;
  HANDLE PhysicalMemoryObject;
  HANDLE AdapterMemoryObject;
  DXGK_ADL *Adl;
  PVOID MappedBase;
  SIZE_T MappedSize;
  PUCHAR CpuBase;
  SIZE_T Size;
  ULONGLONG GuestIpaBase;
  ULONGLONG HostPhysicalBase;
  BOOLEAN BorrowedFirmwareReserve;
} ADMISSION_PHYSICAL_ALLOCATION;

typedef struct _ADMISSION_PHYSICAL_OWNER {
  PDXGKRNL_INTERFACE Interface;
  PDEVICE_OBJECT DeviceObject;
  FAST_MUTEX Lock;
  ADMISSION_PHYSICAL_ALLOCATION *Scratch;
  struct hv_guest_ipa_pa_request *Request;
  ULONGLONG RequestIpa;
  LONG AllocationCount;
  ULONG LastHvcReturnStatus;
  ULONG LastHvcPayloadStatus;
  ULONG HvcInvocationCount;
  ULONG TranslatedPageCount;
  ULONGLONG LastAllocateBytes;
  ULONG LastAllocateStep;
  NTSTATUS LastAllocateStatus;
  BOOLEAN Initialized;
} ADMISSION_PHYSICAL_OWNER;

typedef struct _ADMISSION_BACKEND_MEMORY_VIEW {
  PVOID CpuAddress;
  ULONGLONG GuestIpaAddress;
  ULONGLONG HostPhysicalAddress;
  ULONGLONG GpuVirtualAddress;
  ULONGLONG Bytes;
} ADMISSION_BACKEND_MEMORY_VIEW;

typedef struct _ADMISSION_SCANOUT_MEMORY_VIEW {
  PVOID CpuAddress;
  ULONGLONG GuestIpaAddress;
  ULONGLONG HostPhysicalAddress;
  ULONGLONG GpuVirtualAddress;
  ULONGLONG Bytes;
  /* Fixed broker DMA window; Bytes alone bounds VidMm/scanout surfaces. */
  ULONGLONG PoolBytes;
} ADMISSION_SCANOUT_MEMORY_VIEW;

#define ADMISSION_MEMORY_QUALIFICATION_VERSION 1u
typedef enum _ADMISSION_MEMORY_START_STAGE {
  AdmissionMemoryStartNone = 0,
  AdmissionMemoryStartEntered = 1,
  AdmissionMemoryStartInventories = 2,
  AdmissionMemoryStartPhysicalOwner = 3,
  AdmissionMemoryStartLocalObject = 4,
  AdmissionMemoryStartResidency = 5,
  AdmissionMemoryStartMapping = 6,
  AdmissionMemoryStartTtbr = 7,
  AdmissionMemoryStartPublication = 8,
  AdmissionMemoryStartContract = 9,
  AdmissionMemoryStartComplete = 10,
} ADMISSION_MEMORY_START_STAGE;

typedef struct _ADMISSION_MEMORY_QUALIFICATION {
  ULONG Version;
  ULONG Size;
  NTSTATUS QualificationStatus;
  NTSTATUS CleanupStatus;
  ULONG HvcReturnStatus;
  ULONG HvcPayloadStatus;
  ULONG HvcInvocationCount;
  ULONG TranslatedPageCount;
  ULONG Context;
  ULONG UatPageCount;
  ULONG UatMappingCount;
  ULONG StartStage;
  ULONGLONG GuestIpaBase;
  ULONGLONG HostPhysicalBase;
  ULONGLONG LocalGpuVa;
  ULONGLONG LocalBytes;
  ULONGLONG Ttbr0;
  ULONGLONG Ttbr1;
  ULONGLONG FirstResolvedPhysical;
  ULONGLONG LastResolvedPhysical;
  ULONGLONG FirstLeafDescriptor;
  ULONGLONG LastLeafDescriptor;
} ADMISSION_MEMORY_QUALIFICATION;

void AdmissionRecordService(_In_ PUNICODE_STRING RegistryPath,
                            _In_ PCWSTR Name, _In_ ULONG Value);
#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
void AdmissionRecordG1bAllocationInput(_In_opt_ PDEVICE_OBJECT DeviceObject,
                                       _In_ USHORT MinimumPageSize,
                                       _In_ USHORT RecommendedPageSize);
void AdmissionRecordG1bDdiFailure(_In_opt_ PDEVICE_OBJECT DeviceObject,
                                  _In_ ULONG DdiId, _In_ NTSTATUS Status);
#endif
void AdmissionRecordDevice(_In_opt_ PDEVICE_OBJECT DeviceObject,
                           _In_ ADMISSION_RECEIPT Receipt,
                           _In_ NTSTATUS Status);
void AdmissionRecordGpuvaArmGate(_In_opt_ ADMISSION_CONTEXT *Context,
                                _In_ ULONG Phase, _In_ NTSTATUS Status,
                                _In_ ULONGLONG HypercallResult);
void AdmissionRecordStartStage(_In_opt_ ADMISSION_CONTEXT *Context,
                               _In_ ADMISSION_START_STAGE Stage,
                               _In_ NTSTATUS Status);
void AdmissionRecordTranslatedResources(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordPostDisplay(_In_ ADMISSION_CONTEXT *Context,
                                _In_ NTSTATUS AcquireStatus,
                                _In_ APPLE_AGX_POST_DISPLAY_ROUTE Route,
                                _In_ NTSTATUS DecisionStatus);
void AdmissionRecordMemoryStartFailure(
    _In_ ADMISSION_CONTEXT *Context, _In_ ULONG Substage,
    _In_ NTSTATUS Status, _In_ ULONGLONG RequestedBytes,
    _In_ ULONG OperationResult, _In_ LONG OutstandingAllocations,
    _In_ ULONG PhysicalStep, _In_ NTSTATUS PhysicalStatus,
    _In_ ULONGLONG PhysicalBytes);
void AdmissionRecordMemoryStop(_In_ ADMISSION_CONTEXT *Context,
                               _In_ NTSTATUS Status,
                               _In_ LONG OutstandingBefore,
                               _In_ LONG OutstandingAfter);
void AdmissionRecordPlatformStage(_In_ ADMISSION_CONTEXT *Context,
                                  _In_ ADMISSION_PLATFORM_STAGE Stage,
                                  _In_ NTSTATUS Status);
void AdmissionRecordBackendStartResult(
    _In_ ADMISSION_CONTEXT *Context,
    _In_ APPLE_AGX_BACKEND_RUNTIME_RESULT Result);
void AdmissionRecordFirmwarePhase(
    _In_ ADMISSION_CONTEXT *Context, _In_ APPLE_AGX_FIRMWARE_PHASE Phase,
    _In_ APPLE_AGX_FIRMWARE_RESULT Result, _In_ ULONG CompletedMask);
void AdmissionRecordFirmwarePowerOn(_In_ ADMISSION_CONTEXT *Context,
                                    _In_ BOOLEAN Acquired, _In_ ULONG State,
                                    _In_ ULONG Result,
                                    _In_ ULONGLONG ReceiptSequence);
void AdmissionRecordFirmwarePrefix(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Stage, _In_opt_ const AGX_FW_PREFIX *Prefix,
    _In_opt_ const ULONGLONG *Imported);
void AdmissionRecordRetainedRoot(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Operation, _In_ const AGX_RR_RESPONSE *Response);
void AdmissionRecordRetainedTrace(_In_ ADMISSION_CONTEXT *Context,
    _In_reads_bytes_(Bytes) const VOID *Data, _In_ ULONG Bytes);
void AdmissionRecordContext0Inventory(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Stage, _In_ ULONG Result, _In_ const APPLE_AGX_CONTEXT0_BROKER *Journal);
#include "apple_agx_firmware_io.h"
#include "apple_agx_hwdata_profile_abi.h"
void AdmissionRecordHwdataProfile(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Result,_In_ const AGX_HWDATA_RECEIPT *Receipt);
void AdmissionRecordFirmwareQualification(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG StartResult,_In_ ULONG StartReturn,_In_ ULONG CompletedMask,_In_ ULONG CleanupResult);
void AdmissionRecordBackendQualification(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Stage,_In_ ULONG Result,_In_ ULONG Phase,_In_ ULONG Flags,
    _In_ ULONGLONG ArenaGpu,_In_ ULONG ArenaBytes);
void AdmissionRecordDeviceControl(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Idle,_In_ ULONG Result,_In_ ULONG ReadPointer,
    _In_ ULONG WritePointer,_In_ ULONG Expected);
void AdmissionRecordFirmwareIo(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Result,_In_ const AGX_FW_IO_MANIFEST *Manifest);
void AdmissionRecordEndpoint(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Endpoint, _In_ ULONG Success);
void AdmissionRecordRtkitBoot(_In_ ADMISSION_CONTEXT *Context,
                              _In_ APPLE_AGX_RTKIT_SESSION_RESULT Result,
                              _In_ const APPLE_AGX_RTKIT_SESSION *Session);
void AdmissionRecordPreManagementUat(_In_ ADMISSION_CONTEXT *Context,
                                     _In_ BOOLEAN Published,
                                     _In_ const APPLE_AGX_UAT_PUBLICATION_STATE *State);
void AdmissionRecordProviderBootstrap(_In_ ADMISSION_CONTEXT *Context,
                                      _In_ ULONG Phase, _In_ UCHAR Success,
                                      _In_ ULONG State);
void AdmissionRecordRtkitCrashlog(_In_ ADMISSION_CONTEXT *Context,
                                 _In_reads_bytes_(Bytes) const VOID *Data,
                                 _In_ ULONG Bytes);
void AdmissionRecordQuery(_In_opt_ PDEVICE_OBJECT DeviceObject,
                          _In_ DXGK_QUERYADAPTERINFOTYPE Type,
                          _In_ ULONG OutputDataSize, _In_ NTSTATUS Status,
                          _In_reads_bytes_opt_(OutputDataSize)
                              const VOID *OutputData);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
void AdmissionRecordGpuvaG3Query(_In_opt_ PDEVICE_OBJECT DeviceObject,
                                 _In_ const DXGKARG_QUERYADAPTERINFO *Query,
                                 _In_ NTSTATUS Status, UINT MmuCount);
void AdmissionRecordGpuvaG3Node(_In_opt_ PDEVICE_OBJECT DeviceObject,
                                UINT NodeOrdinal,
                                _In_ const DXGKARG_GETNODEMETADATA *Metadata,
                                NTSTATUS Status);
void AdmissionRecordGpuvaG3CreateInput(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ const DXGKARG_CREATEPROCESS *Args,
    BOOLEAN AdapterStarted, KIRQL CurrentIrql);
void AdmissionRecordGpuvaG3DeviceInput(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ const DXGKARG_CREATEDEVICE *Args, KIRQL CurrentIrql);
void AdmissionRecordGpuvaG3ContextInput(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ const DXGKARG_CREATECONTEXT *Args, KIRQL CurrentIrql);
void AdmissionRecordGpuvaG3DmaContext(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ const DXGKARG_CREATECONTEXT *Args);
void AdmissionRecordGpuvaG3DmaCreate(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ const DXGKARG_CREATEALLOCATION *Args, NTSTATUS Status);
void AdmissionRecordGpuvaG3DmaOpen(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ const DXGKARG_OPENALLOCATION *Args, NTSTATUS Status);
void AdmissionRecordGpuvaG3PagingInput(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_opt_ const DXGKARG_BUILDPAGINGBUFFER *Args, ULONG CurrentIrql);
void AdmissionRecordGpuvaG3WorkInput(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_opt_ const DXGKARG_BUILDPAGINGBUFFER *Args, ULONG CurrentIrql);
void AdmissionRecordGpuvaG3PagingResult(
    _In_opt_ ADMISSION_CONTEXT *Context, NTSTATUS Status);
typedef struct _ADMISSION_G3_FLUSH_RECEIPT {
  ULONG Version, Bytes, Branch, RootSegment, ResolveStatus, BrokerStatus;
  ULONGLONG Process, RootOffset, ResolvedRootIpa, GraphRootIpa;
  ULONGLONG InputStart, InputEnd, FlushStart, FlushEnd;
} ADMISSION_G3_FLUSH_RECEIPT;
void AdmissionRecordGpuvaG3Flush(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_G3_FLUSH_RECEIPT *Receipt);
typedef struct _ADMISSION_G3_PAGING_FAILURE {
  ULONG Version, Bytes, Branch, Level, Index, PageTablePageSize;
  ULONG Status, UpdateMode, GraphLastStatus, GraphUncertain;
  ULONGLONG TableAddress, TableIpa, PteFlags, PageAddress, ChildIpa;
  ULONG TableFirstNonzeroIndex, TableAddBranch;
  ULONGLONG TableFirstNonzeroWord;
  ULONGLONG BrokerTableIpa;
} ADMISSION_G3_PAGING_FAILURE;
void AdmissionRecordGpuvaG3PagingFailure(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_G3_PAGING_FAILURE *Failure);
/* EXP1321 receipt-only: Wom1G3BrokerFailures, the V5 client's failure ring
 * (staged by the paging receipt worker when the failure count moved). */
#define ADMISSION_G3_BROKER_FAILURES_VERSION 1u
typedef struct _ADMISSION_G3_BROKER_FAILURES {
  ULONG Version, Bytes;
  ULONGLONG FailureCount;
  APPLE_AGX_GPUVA_V5_FAILURE Ring[APPLE_AGX_GPUVA_V5_FAILURE_RING];
} ADMISSION_G3_BROKER_FAILURES;
void AdmissionRecordGpuvaG3BrokerFailures(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_opt_ const APPLE_AGX_GPUVA_V5_CLIENT *Client);
void AdmissionRenderCorrelationSubmitFailureWindows(
    _In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG4SubmitFailure(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG4PreemptOverlap(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3CopyQueryFailure(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3LeafHistory(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3PagingWait(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3LiveWait(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_G3_LIVE_WAIT_ENTRY *Entry);
VOID AdmissionPagingProfileAdd(_Inout_ volatile LONG64 *Triplet, LONGLONG Ticks);
/* Caller at PASSIVE_LEVEL outside the paging path; Client may be NULL. */
void AdmissionRecordPagingProfile(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_opt_ const struct _APPLE_AGX_GPUVA_V5_CLIENT *Client,
    _In_opt_ const ULONGLONG *PrivateStats);
void AdmissionRecordG3PteWait(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3Poison(_In_opt_ ADMISSION_CONTEXT *Context,
                             ULONG Site, ULONG ProcessId, ULONG BrokerStatus);
void AdmissionRecordG4DrawSnapshot(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3CopyTransferFailure(_In_opt_ ADMISSION_CONTEXT *Context);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
NTSTATUS AdmissionGpuvaG3FrameArmEscape(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ const DXGKARG_ESCAPE *Args);
NTSTATUS AdmissionDwmFrameProbeQueryWindows(_In_ ADMISSION_CONTEXT *Adapter,
    _Inout_ ADMISSION_DWM_FRAME_PROBE *Probe);
BOOLEAN AdmissionDwmFrameArmWindows(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ ULONG OsPid, _In_ ULONGLONG GraphPid,
    _In_ ULONGLONG Allocation, _In_ ULONGLONG CanonicalVa);
VOID AdmissionDwmFrameRecordQuery(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ ULONG Predicate, _In_ NTSTATUS Status,
    _In_ ULONG ResidentPages);
VOID AdmissionDwmFrameRecordSubmit(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ ULONGLONG CommandVa, _In_ ULONGLONG Fence,
    _In_ ULONG Branch, _In_ NTSTATUS Status, _In_ BOOLEAN Present);
VOID AdmissionDwmFrameRecordReject(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ ULONG Branch, _In_ NTSTATUS Status);
VOID AdmissionDwmFrameRecordEnvelope(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ const ADMISSION_DWM_ENVELOPE_RECEIPT *Receipt);
VOID AdmissionDwmFrameRecordCompletion(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ ULONGLONG Fence);
VOID AdmissionDwmFrameRecordPresent(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ NTSTATUS Status, _In_ BOOLEAN Count);
VOID AdmissionDwmFrameRecordBlt(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ ULONGLONG SourceAllocation,
    _In_ ULONGLONG DestinationAllocation, _In_ ULONGLONG SourceVa,
    _In_ ULONGLONG DestinationVa);
VOID AdmissionDwmFrameRecordCopy(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ PVOID Context, _In_ ULONGLONG DestinationIpa,
    _In_ ULONGLONG BytesCopied, _In_ NTSTATUS Status);
VOID AdmissionDwmFrameRecordTdr(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ NTSTATUS Status, _In_ BOOLEAN AfterReset);
VOID AdmissionDwmFrameRecordPrivateReset(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ BOOLEAN Succeeded);
#endif
void AdmissionRecordG3PrivateFailure(_In_opt_ ADMISSION_CONTEXT *Context);
/* EXP1114 receipt-only: per-process private storage snapshot. */
void AdmissionRecordG3PrivatePressure(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_bytes_(Bytes) const ULONG *Values, _In_ ULONG Bytes);
void AdmissionRecordG3PrivateProcesses(_In_opt_ ADMISSION_CONTEXT *Context,
                                       _In_reads_bytes_(Bytes) const ULONG *Values,
                                       _In_ ULONG Bytes);
/* EXP2206 receipt-only: Wom1G3NativeQueryRefusal, an
 * APPLE_AGX_G3_NQ_REFUSAL_RECEIPT (apple_agx_g3_native_query_refusal.h);
 * never flushed. */
void AdmissionRecordG3NativeQueryRefusal(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_bytes_(Bytes) const VOID *Receipt, _In_ ULONG Bytes);
/* G2 HW2 receipt: the per-process private VA reservations (Wom1G3Reserve:
 * version, reserved, refused, last status, last VA low, last VA high). */
#define ADMISSION_G3_RESERVE_RECEIPT_WORDS 6u
void AdmissionRecordG3Reservation(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(ADMISSION_G3_RESERVE_RECEIPT_WORDS) const ULONG *Values);
/* G2 HW2b receipt: present arenas (Wom1G2PresentArena: version, made,
 * refused, last predicate, last status, last arena VA low, high). */
#define ADMISSION_G2_PRESENT_ARENA_RECEIPT_WORDS 7u
/* G2 HW2b receipt: present paths (Wom1G2PresentPath: version, a count per
 * reason 0 GPU .. 9 GPU refused, last reason, last fence). */
#define ADMISSION_G2_PRESENT_PATH_REASONS 10u
#define ADMISSION_G2_PRESENT_PATH_RECEIPT_WORDS (3u + ADMISSION_G2_PRESENT_PATH_REASONS)
void AdmissionRecordG2PresentPath(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(ADMISSION_G2_PRESENT_PATH_RECEIPT_WORDS) const ULONG *Values);
/* G2 HW3 receipt: what DxgkDdiPresent Blt receives on a virtual-addressing,
 * non-system context (an OpenGL ICD's D3DKMTPresent as dxgkrnl redirects it,
 * or a D3D process's blt). Wom1G2PresentDdi: version, calls, refusals, last
 * status, flags, process id, context flags; source then destination (open
 * reason 0 ours/1 none/2 other, type, format, width, height, pitch,
 * CPU-visible, GPU VA low, high); SrcRect; DstRect; sub-rect count; first
 * sub-rect. Written for the first call, the first refusal and at most every
 * 2 s. */
#define ADMISSION_G2_PRESENT_DDI_RECEIPT_WORDS 38u
void AdmissionRecordG2PresentDdi(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_opt_ ADMISSION_DEVICE *Device, _In_opt_ HANDLE RenderContext,
    _In_opt_ const DXGKARG_PRESENT *Present, _In_ NTSTATUS Status);
void AdmissionRecordG2PresentArena(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(ADMISSION_G2_PRESENT_ARENA_RECEIPT_WORDS) const ULONG *Values);
void AdmissionRecordGpuvaG3UnpublishedGroups(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(32) const ULONGLONG *Counts);
void AdmissionRecordGpuvaG3Leaf64KPtes(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(32) const ULONGLONG *Counts);
void AdmissionRecordGpuvaG3SystemGroups(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(8) const ULONGLONG *Counts);
/* M2 receipt Wom1G3SystemShapes: the system (segment-0) 16 KiB groups of
 * valid leaf updates per allocation shape, attributed through
 * DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE.hAllocation. Traits: 1 CpuVisible,
 * 2 Presentation, 4 WrittenPrimary, 8 handle is not an allocation,
 * 0x40000000 slot in use, 0x80000000 overflow slot (every later shape).
 * Groups: [0] 4 KiB published, [1] 4 KiB unpublished (incomplete or
 * refused), [2] 64 KiB published, [3] 64 KiB unpublished. */
#define ADMISSION_G3_SYSTEM_SHAPE_COUNT 16u
typedef struct _ADMISSION_G3_SYSTEM_SHAPE {
  ULONG ClassId, Win32Flags, Traits, SizeLog2;
  ULONGLONG Groups[4];
  ULONGLONG UnpublishedVa, UnpublishedIpa;
} ADMISSION_G3_SYSTEM_SHAPE;
/* M2-f2: segment-3 pool lifetime (pool_windows.c). */
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionMemoryPoolStart(_Inout_ ADMISSION_CONTEXT *Context);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionMemoryPoolStop(_Inout_opt_ ADMISSION_CONTEXT *Context);
/* M2-f7: the largest General-class BO (segment 3's size while the pool is
 * mapped, else the legacy 16 MiB); reported to the UMD and enforced by
 * CreateAllocation. */
ULONGLONG AdmissionGeneralMaximumBytes(_In_opt_ const ADMISSION_CONTEXT *Context);
BOOLEAN AdmissionMemoryPoolView(_In_opt_ const ADMISSION_CONTEXT *Context,
                                _Out_ ADMISSION_SCANOUT_MEMORY_VIEW *View);
/* M2-f3: the demand pool's granule state under its lock; NULL (no lock
 * taken) when segment 3 is not demand-backed. PASSIVE or APC level; the G3
 * lock, when held, is taken first. */
ADMISSION_POOL_DEMAND *AdmissionMemoryPoolLock(_In_opt_ ADMISSION_CONTEXT *Context);
VOID AdmissionMemoryPoolUnlock(_In_ ADMISSION_CONTEXT *Context);
/* Stamps Record->Seq; a record naming segment-3 pages holds them until it
 * has executed. */
VOID AdmissionMemoryPoolNoteRecord(_In_opt_ ADMISSION_CONTEXT *Context,
                                   _Inout_ ADMISSION_PAGING_RECORD *Record);
/* Every record up to Seq has executed; releases what became releasable. */
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionMemoryPoolExecuted(_In_opt_ ADMISSION_CONTEXT *Context,
                                 ULONGLONG Seq);
/* DestroyAllocation: the placement ends. */
VOID AdmissionMemoryPoolForget(_In_opt_ ADMISSION_CONTEXT *Context,
                               _Inout_ ADMISSION_POOL_PLACEMENT *Placement);
/* M2-f5: CreateAllocation reserves the pages a segment-3 allocation may be
 * placed in (FALSE: Windows has none, the caller returns STATUS_NO_MEMORY);
 * DestroyAllocation ends the reservation. TRUE without a demand pool.
 * rc104: a refusal at the LowMemoryCondition floor first waits, with no
 * driver lock held, up to the Wom1PoolPressureWaitMs budget for the memory
 * manager to clear it, so the call may block that long. */
_IRQL_requires_(PASSIVE_LEVEL)
BOOLEAN AdmissionMemoryPoolReserve(_In_opt_ ADMISSION_CONTEXT *Context,
                                   ULONGLONG Bytes);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionMemoryPoolUnreserve(_In_opt_ ADMISSION_CONTEXT *Context,
                                  ULONGLONG Bytes);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionMemoryPoolReclaim(_In_opt_ ADMISSION_CONTEXT *Context);
/* rc80 (apple_agx_g3_tvb.h): one exclusive 64 KiB slab granule of Windows
 * memory for a TVB window block, zeroed, outside the segment-3 promise and
 * bounded by the slab PrivateCap; *GuestIpa is its 64 KiB-aligned IPA. Take
 * refuses while Windows signals LowMemory (AdmissionPoolMayGrow). Give zeroes
 * it again and returns it; call it only after the broker revoked every
 * GPU mapping. A granule that cannot be zeroed is retained (PoolLeaks). */
_IRQL_requires_max_(APC_LEVEL)
BOOLEAN AdmissionMemoryPoolTvbTake(_In_opt_ ADMISSION_CONTEXT *Context,
                                   _Inout_ ADMISSION_POOL_PRIVATE *Lease,
                                   _Out_ ULONGLONG *GuestIpa);
_IRQL_requires_max_(APC_LEVEL)
BOOLEAN AdmissionMemoryPoolTvbGive(_In_opt_ ADMISSION_CONTEXT *Context,
                                   _Inout_ ADMISSION_POOL_PRIVATE *Lease);
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionRecordM2PoolProbe(_In_opt_ ADMISSION_CONTEXT *Context,
                                _In_ const ADMISSION_POOL_PROBE *Probe);
/* M2-f3 receipt Wom1M2PoolDemand (staged; written by a work item). */
void AdmissionRecordM2PoolDemand(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_POOL_DEMAND_RECEIPT *Receipt);
/* POOL-PROBE receipt Wom1PoolAllocProbe (staged; written by a work item). */
void AdmissionRecordM2PoolAllocProbe(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_POOL_ALLOC_PROBE_RECEIPT *Receipt);
void AdmissionRecordGpuvaG3SystemShapes(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(ADMISSION_G3_SYSTEM_SHAPE_COUNT)
        const ADMISSION_G3_SYSTEM_SHAPE *Shapes);
void AdmissionRecordG4OmittedLeaves(ADMISSION_CONTEXT *Context, const ADMISSION_OMITTED_LEAVES *Receipt);
/* RC60 receipt Wom1G3NativePteRefusal (staged; written by a work item). */
void AdmissionRecordG3NativePteRefusal(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_NATIVE_PTE_REFUSAL *Receipt);
BOOLEAN AdmissionGpuvaG3DeclarationReady(_In_ const ADMISSION_CONTEXT *Context);
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS AdmissionPagingReceiptsStart(_In_ ADMISSION_CONTEXT *Context);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionPagingReceiptsStop(_In_ ADMISSION_CONTEXT *Context);
#endif
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionFlushSourceAddressReceipt(_In_ ADMISSION_CONTEXT *Context);
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionRecordPresent(_In_opt_ ADMISSION_DEVICE *Device,
                            _In_opt_ const DXGKARG_PRESENT *Present,
                            ULONG Branch, NTSTATUS Status);
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionRecordPresentOpenFailure(_In_ ADMISSION_DEVICE *Device,
    HANDLE Context, _In_ const DXGKARG_PRESENT *Present,
    _In_ const ADMISSION_PRESENT_OPEN_ENDPOINT *Source,
    _In_ const ADMISSION_PRESENT_OPEN_ENDPOINT *Destination);
void AdmissionRecordPresentTransfer(_In_ ADMISSION_CONTEXT *Context,
    UINT Fence, _In_reads_bytes_(Bytes) const VOID *Command, UINT Bytes,
    ULONGLONG BytesCopied, NTSTATUS Status);
void AdmissionFlushPresentTransfer(_In_ ADMISSION_CONTEXT *Context);
ULONG AdmissionScanoutReceiptState(_In_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionPresentBlt(_In_ ADMISSION_DEVICE *Device, HANDLE Context,
                             _Inout_ DXGKARG_PRESENT *Present);
BOOLEAN AdmissionPresentIsBltPrivate(_In_opt_ PVOID Data, UINT Bytes);
ULONG AdmissionPresentPrivateStage(
    _In_opt_ PVOID Data, UINT Bytes,
    _Out_opt_ ADMISSION_PRESENT_BLT_COMMAND *Command);
NTSTATUS AdmissionPresentPatch(_In_ ADMISSION_CONTEXT *Context,
                               _In_ const DXGKARG_PATCH *Args);
NTSTATUS AdmissionPresentSubmit(_In_ ADMISSION_CONTEXT *Context,
                                _In_ const DXGKARG_SUBMITCOMMAND *Args);
NTSTATUS AdmissionPresentSubmitTraced(_In_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_SUBMITCOMMAND *Args, BOOLEAN Trace);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
NTSTATUS AdmissionPresentSubmitVirtual(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ ADMISSION_RENDER_CONTEXT *Context,
    _In_ const DXGKARG_SUBMITCOMMANDVIRTUAL *Args);
#endif
/* Failure evidence is required in normal builds as well as qualification. */
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordEventDrain(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_EVENT_DRAIN_RECEIPT *Receipt);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
BOOLEAN AdmissionSubmitTraceBegin(_In_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_SUBMITCOMMAND *Args, ULONG PrivateStage,
    _In_opt_ const ADMISSION_PRESENT_BLT_COMMAND *Command);
VOID AdmissionSubmitTraceValueWindows(_In_ ADMISSION_CONTEXT *Context,
    BOOLEAN Enabled, ULONG Field, ULONG Value);
VOID AdmissionSubmitRenderGuardWindows(_In_opt_ ADMISSION_CONTEXT *Context,
    ULONG Guard, NTSTATUS Status);
VOID AdmissionSubmitFenceDetailWindows(_In_opt_ ADMISSION_CONTEXT *Context,
    ULONG Outstanding, ULONG Submitted);
VOID AdmissionPatchRenderGuardWindows(_In_opt_ ADMISSION_CONTEXT *Context,
    ULONG Guard, NTSTATUS Status);
VOID AdmissionPrepatchAdoptGuardWindows(_In_opt_ ADMISSION_CONTEXT *Context,
    ULONG Guard, NTSTATUS Status);
VOID AdmissionSubmitPacketGuardWindows(_In_opt_ ADMISSION_CONTEXT *Context,
    ULONG Guard, NTSTATUS Status);
VOID AdmissionBackendSubmitResultWindows(_In_opt_ ADMISSION_CONTEXT *Context,
    ULONG Result, ULONG Phase);
VOID AdmissionTerminalObservationTraceWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, ULONG Source,
    ULONG CompletionStatus, ULONG RuntimePhase, ULONG ProviderPhase,
    ULONG Fence);
VOID AdmissionTerminalExitTraceWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, ULONG Reason, ULONG ValidMask,
    ULONG RuntimePhase, ULONG ProviderPhase, ULONG Fence);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordPreSubmitHeartbeat(
    _In_opt_ ADMISSION_CONTEXT *Context,
    APPLE_AGX_RTKIT_SESSION_RESULT Result,
    _In_ const APPLE_AGX_RTKIT_SESSION *Session);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordTerminalReceipt(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_TERMINAL_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordOutputTerminalSnapshot(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_TERMINAL_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordDynamicOutputSnapshot(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_DYNAMIC_OUTPUT_SNAPSHOT *Snapshot);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordDynamicGraph(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_DYNAMIC_GRAPH_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordNativeGraph(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_NATIVE_GRAPH_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordDynamicStore(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_DYNAMIC_STORE_RECEIPT *Receipt);
VOID AdmissionTerminalReceiptDpcWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, ULONG Fence);
VOID AdmissionBackendProgressWindows(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const APPLE_AGX_G13_QUEUE_PROGRESS *Progress);
VOID AdmissionBackendChannelProgressWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, ULONG TaRead, ULONG D3Read,
    ULONG Fence);
VOID AdmissionProviderPollGuardWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, ULONG Guard, ULONG ProviderPhase,
    ULONG RuntimePhase, ULONG Fence);
VOID AdmissionProviderDrainTraceWindows(
    _In_opt_ ADMISSION_CONTEXT *Context, ULONG Guard, ULONG ReadPointer,
    ULONG WritePointer);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordQueueSubmission(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_QUEUE_SUBMISSION_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordQueueInfo(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_QUEUE_INFO_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordBufferManager(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_BUFFER_MANAGER_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordTaProgress(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_TA_PROGRESS_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordTaRetire(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_TA_RETIRE_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordTaTemporal(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_TA_TEMPORAL_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordKTrace(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_KTRACE_RECEIPT *Receipt);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordQueueFaultSnapshot(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_QUEUE_FAULT_SNAPSHOT *Snapshot);
_IRQL_requires_(PASSIVE_LEVEL)
VOID AdmissionRecordComputeIdentityDiagnostic(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const APPLE_AGX_G13_COMPUTE_IDENTITY_DIAGNOSTIC *Diagnostic);
VOID AdmissionGdiReceiptBeginWindows(_In_ ADMISSION_CONTEXT *Context,
    ULONGLONG ContextToken, ULONG Opcode, ULONG Color, ULONG RectCount,
    ULONG DmaBytes);
VOID AdmissionGdiReceiptPatchWindows(_In_ ADMISSION_CONTEXT *Context,
    ULONGLONG ContextToken, ULONG Fence, ULONGLONG DestinationGpuVa,
    ULONGLONG DestinationPhysical, ULONG DestinationBytes);
VOID AdmissionGdiReceiptSubmitWindows(_In_ ADMISSION_CONTEXT *Context,
    _In_opt_ const DXGKARG_SUBMITCOMMAND *Args, NTSTATUS Status);
VOID AdmissionGdiReceiptBackendWindows(_In_ ADMISSION_CONTEXT *Context,
    ULONG Fence, ULONG Result, _In_opt_ const APPLE_AGX_BACKEND_JOB_IMAGE *Job);
VOID AdmissionGdiReceiptCompleteWindows(_In_ ADMISSION_CONTEXT *Context,
    ULONG Fence, ULONG Status, BOOLEAN NotifyInterrupt);
VOID AdmissionGdiReceiptProgressWindows(_In_ ADMISSION_CONTEXT *Context,
    ULONG Fence, _In_ const APPLE_AGX_G13_QUEUE_PROGRESS *Progress,
    ULONG WorkerFinalPhase);
VOID AdmissionGdiReceiptDpcWindows(_In_ ADMISSION_CONTEXT *Context, ULONG Fence);
VOID AdmissionPlatformRecordPostDpcHealth(_In_ ADMISSION_CONTEXT *Context,
    ULONG Fence);
VOID AdmissionFlushGdiReceipt(_In_ ADMISSION_CONTEXT *Context);
#else
#define AdmissionSubmitTraceBegin(Context, Args, PrivateStage, Command) FALSE
#define AdmissionSubmitTraceValueWindows(Context, Enabled, Field, Value)       \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Enabled);                                                           \
    (void)(Field);                                                             \
    (void)(Value);                                                             \
  } while (0)
#define AdmissionSubmitRenderGuardWindows(Context, Guard, Status)              \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Guard);                                                             \
    (void)(Status);                                                            \
  } while (0)
#define AdmissionSubmitFenceDetailWindows(Context, Outstanding, Submitted)     \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Outstanding);                                                       \
    (void)(Submitted);                                                         \
  } while (0)
#define AdmissionPatchRenderGuardWindows(Context, Guard, Status)               \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Guard);                                                             \
    (void)(Status);                                                            \
  } while (0)
#define AdmissionPrepatchAdoptGuardWindows(Context, Guard, Status)             \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Guard);                                                             \
    (void)(Status);                                                            \
  } while (0)
#define AdmissionSubmitPacketGuardWindows(Context, Guard, Status)              \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Guard);                                                             \
    (void)(Status);                                                            \
  } while (0)
#define AdmissionBackendSubmitResultWindows(Context, Result, Phase)            \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Result);                                                            \
    (void)(Phase);                                                             \
  } while (0)
#define AdmissionTerminalObservationTraceWindows(Context, Source,             \
                                                  CompletionStatus,            \
                                                  RuntimePhase, ProviderPhase, \
                                                  Fence)                       \
  do {                                                                         \
    (void)(Context); (void)(Source); (void)(CompletionStatus);                 \
    (void)(RuntimePhase); (void)(ProviderPhase); (void)(Fence);                \
  } while (0)
#define AdmissionTerminalExitTraceWindows(Context, Reason, ValidMask,          \
                                           RuntimePhase, ProviderPhase, Fence) \
  do {                                                                         \
    (void)(Context); (void)(Reason); (void)(ValidMask);                        \
    (void)(RuntimePhase); (void)(ProviderPhase); (void)(Fence);                \
  } while (0)
#define AdmissionRecordPreSubmitHeartbeat(Context, Result, Session)            \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Result);                                                            \
    (void)(Session);                                                           \
  } while (0)
#define AdmissionRecordTerminalReceipt(Context, Receipt)                       \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordOutputTerminalSnapshot(Context, Receipt)                \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordDynamicGraph(Context, Receipt)                          \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordDynamicStore(Context, Receipt)                          \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordNativeGraph(Context, Receipt)                           \
  do { (void)(Context); (void)(Receipt); } while (0)
#define AdmissionTerminalReceiptDpcWindows(Context, Fence)                     \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Fence);                                                             \
  } while (0)
#define AdmissionBackendProgressWindows(Context, Progress)                     \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Progress);                                                          \
  } while (0)
#define AdmissionBackendChannelProgressWindows(Context, TaRead, D3Read, Fence) \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(TaRead);                                                            \
    (void)(D3Read);                                                            \
    (void)(Fence);                                                             \
  } while (0)
#define AdmissionProviderPollGuardWindows(Context, Guard, ProviderPhase,       \
                                          RuntimePhase, Fence)                  \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Guard);                                                             \
    (void)(ProviderPhase);                                                     \
    (void)(RuntimePhase);                                                      \
    (void)(Fence);                                                             \
  } while (0)
#define AdmissionProviderDrainTraceWindows(Context, Guard, ReadPointer,        \
                                           WritePointer)                       \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Guard);                                                             \
    (void)(ReadPointer);                                                       \
    (void)(WritePointer);                                                      \
  } while (0)
#define AdmissionRecordQueueSubmission(Context, Receipt)                       \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordQueueInfo(Context, Receipt)                             \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordBufferManager(Context, Receipt)                         \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordTaProgress(Context, Receipt)                            \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordTaRetire(Context, Receipt)                              \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordTaTemporal(Context, Receipt)                            \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordKTrace(Context, Receipt)                                \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Receipt);                                                           \
  } while (0)
#define AdmissionRecordQueueFaultSnapshot(Context, Snapshot)                   \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Snapshot);                                                          \
  } while (0)
#define AdmissionRecordComputeIdentityDiagnostic(Context, Diagnostic)          \
  do {                                                                         \
    (void)(Context);                                                           \
    (void)(Diagnostic);                                                        \
  } while (0)
#define AdmissionGdiReceiptBeginWindows(Context, ContextToken, Opcode, Color, RectCount, DmaBytes) ((void)0)
#define AdmissionGdiReceiptPatchWindows(Context, ContextToken, Fence, DestinationGpuVa, DestinationPhysical, DestinationBytes) ((void)0)
#define AdmissionGdiReceiptSubmitWindows(Context, Args, Status) ((void)0)
#define AdmissionGdiReceiptBackendWindows(Context, Fence, Result, Job) ((void)0)
#define AdmissionGdiReceiptCompleteWindows(Context, Fence, Status, NotifyInterrupt) ((void)0)
#define AdmissionGdiReceiptProgressWindows(Context, Fence, Progress, WorkerFinalPhase) ((void)0)
#define AdmissionGdiReceiptDpcWindows(Context, Fence) ((void)0)
#define AdmissionPlatformRecordPostDpcHealth(Context, Fence) ((void)0)
#define AdmissionFlushGdiReceipt(Context) ((void)0)
#endif
NTSTATUS AdmissionPagingSubmitPresent(_In_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_SUBMITCOMMAND *Args,
    _In_reads_bytes_(Bytes) const VOID *Command, UINT Bytes);
NTSTATUS AdmissionCpuQueueSubmit(_In_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_SUBMITCOMMAND *Args, ULONG Kind,
    _In_reads_bytes_(Bytes) const VOID *Data, UINT Bytes);
void AdmissionDispatchQueuedWork(_In_ ADMISSION_CONTEXT *Context);
void AdmissionPagingQueueActive(_In_ ADMISSION_CONTEXT *Context);
/* Caller holds PagingLock. */
void AdmissionPagingUpdateIdleLocked(_In_ ADMISSION_CONTEXT *Context);
void AdmissionPagingNoteEncoded(_In_ ADMISSION_CONTEXT *Context, _In_ UINT Records);
BOOLEAN AdmissionPagingQuiescent(_In_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionMemoryRuntimeExecutePresent(_In_ ADMISSION_CONTEXT *Context,
    _In_reads_bytes_(Bytes) const VOID *Command, UINT Bytes,
    _Out_ ULONGLONG *BytesCopied);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
NTSTATUS AdmissionGpuvaG3ExecutePresentVirtual(_In_ ADMISSION_CONTEXT *Adapter,
    _In_ ADMISSION_RENDER_CONTEXT *Context,
    _In_reads_bytes_(Bytes) const VOID *Command, UINT Bytes,
    _Out_ ULONGLONG *BytesCopied);
#endif
void AdmissionRecordDisplayModes(_In_ ADMISSION_CONTEXT *Context);
void AdmissionRecordDisplayDdi(_In_opt_ PDEVICE_OBJECT DeviceObject,
                               _In_ ULONG DdiId, _In_ ULONG Phase,
                               _In_ NTSTATUS Status);
void AdmissionRecordDwmSourceMap(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ const ADMISSION_DWM_SOURCE_MAP_RECEIPT *Receipt,
    _In_ ULONG Ordinal);
void AdmissionRecordMemoryQualification(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ const ADMISSION_MEMORY_QUALIFICATION *Qualification);
NTSTATUS AdmissionInterruptStart(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionInterruptStop(_Inout_ ADMISSION_CONTEXT *Context);
/* DIRQL: dismiss the ASC mailbox level line: pop the outbox FIFO until EMPTY
 * (into the platform runtime's ring while armed, dropped otherwise; see
 * render_mailbox_isr.h).  FALSE = nothing was popped (not ours). */
BOOLEAN AdmissionPlatformMailboxInterrupt(_Inout_ ADMISSION_CONTEXT *Context,
                                          ULONG MessageNumber);

/* Wom1IrqRx receipt (REG_BINARY): the step-1 census, extended by v2 and v3. */
typedef struct _ADMISSION_IRQ_RX_RECEIPT {
  ULONG Version, Bytes;
  ULONG ScanoutVector, MailboxVector;
  ULONG Armed;
  ULONG IsrByMessage[4];
  ULONG MailboxHandled, MailboxMessageMask, ScanoutHandled;
  ULONG IsrEmptyCalls;      /* mailbox line fired with an empty FIFO */
  ULONG IsrMessages;        /* messages the ISR moved into the ring */
  ULONG RingDrops;          /* ring full: message lost (must stay 0) */
  ULONG RingConsumed;       /* ring entries the worker consumed */
  ULONG WorkerSyncPops;     /* worker found the ring empty: synchronized FIFO pops */
  ULONG WorkerSyncPopHits;  /* ... that found messages (interrupt missed or raced) */
  ULONG RingHighWater;
  /* v2 (step 2, Wom1IrqCompletion): the ISR queues the DPC, the DPC sets the
   * poll wait event; the worker waits instead of spinning/sleeping. */
  ULONG IrqCompletionOn;
  ULONG DpcQueued;          /* ISR -> DxgkCbQueueDpc requests */
  ULONG DpcWakes;           /* DPC -> KeSetEvent(JobPollTimerEvent) */
  ULONG WaitEarly;          /* poll waits ended by the wake before the timer */
  ULONG WaitTimeout;        /* poll waits ended by the timer (fallback) */
  ULONG WaitSkipped;        /* a wake was already pending: re-polled at once */
  /* v3 (default-on plan): the level line is dismissed in every state. */
  ULONG Discarded;          /* popped and dropped while not armed (no consumer) */
  ULONG Overflow;           /* sticky: a message was dropped at a full ring;
                             * the worker faulted the transport (engine reset) */
} ADMISSION_IRQ_RX_RECEIPT;
#define ADMISSION_IRQ_RX_RECEIPT_VERSION 3u

/* DISPATCH_LEVEL (DxgkDdiDpcRoutine): wake the job worker's poll wait when
 * Wom1IrqCompletion is on and the mailbox ISR queued this DPC. */
VOID AdmissionPlatformMailboxDpc(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionDdiQuerySegment4(
    _In_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo);
#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
NTSTATUS AdmissionDdiQuerySegment5(
    _In_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo);
#endif
NTSTATUS AdmissionPhysicalOwnerInitialize(
    _In_ PDXGKRNL_INTERFACE Interface, _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ ADMISSION_PHYSICAL_OWNER *Owner);
NTSTATUS AdmissionPhysicalOwnerDestroy(
    _Inout_ ADMISSION_PHYSICAL_OWNER *Owner);
NTSTATUS AdmissionPhysicalAllocate(
    _Inout_ ADMISSION_PHYSICAL_OWNER *Owner, _In_ SIZE_T Bytes,
    _Outptr_ ADMISSION_PHYSICAL_ALLOCATION **Allocation);
NTSTATUS AdmissionPhysicalBorrowLocal(
    _Inout_ ADMISSION_PHYSICAL_OWNER *Owner,
    _In_ const DXGK_DEVICE_INFO *DeviceInformation,
    _Outptr_ ADMISSION_PHYSICAL_ALLOCATION **Allocation,
    _Out_ APPLE_AGX_LOCAL_RESERVE_RECEIPT *Receipt);
NTSTATUS AdmissionRecordLocalReserve(
    _In_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_PHYSICAL_ALLOCATION *Allocation);
NTSTATUS AdmissionPhysicalFree(
    _Inout_ ADMISSION_PHYSICAL_OWNER *Owner,
    _Inout_ ADMISSION_PHYSICAL_ALLOCATION *Allocation);
NTSTATUS AdmissionMemoryRuntimeStart(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionMemoryRuntimeStop(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionMemoryRuntimeQualify(
    _In_ ADMISSION_CONTEXT *Context,
    _Out_ ADMISSION_MEMORY_QUALIFICATION *Qualification);
NTSTATUS AdmissionMemoryRuntimeMapAperture(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONGLONG ApertureByteOffset,
    _In_ PMDL Mdl, _In_ SIZE_T MdlPageOffset, _In_ UINT PageCount);
NTSTATUS AdmissionMemoryRuntimeUnmapAperture(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONGLONG ApertureByteOffset,
    _In_ UINT PageCount, _In_ ULONGLONG DummyPage);
NTSTATUS AdmissionMemoryRuntimePrivateView(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ ADMISSION_BACKEND_MEMORY_VIEW *View);
NTSTATUS AdmissionMemoryRuntimeBackendView(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ ADMISSION_BACKEND_MEMORY_VIEW *View);
NTSTATUS AdmissionMemoryRuntimeResolveLocal(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ ULONGLONG AllocationSegmentAddress,
    _In_ ULONGLONG AllocationSize,
    _In_ ULONGLONG AllocationOffset,
    _Out_ ADMISSION_LOCAL_MEMORY_VIEW *View);
NTSTATUS AdmissionMemoryRuntimeReadResident(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ ULONG SegmentId,
    _In_ ULONGLONG AllocationSegmentAddress,
    _In_ ULONGLONG AllocationSize,
    _In_ ULONGLONG AllocationOffset,
    _Out_writes_bytes_(Bytes) PVOID Destination,
    _In_ ULONG Bytes);
NTSTATUS AdmissionMemoryRuntimeBorrowIo(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ APPLE_AGX_MEMORY_IO *Io);
BOOLEAN AdmissionMemoryRuntimeContextPublished(
    _Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionMemoryRuntimeLocalView(
    _In_ ADMISSION_CONTEXT *Context,
    _Out_ ADMISSION_SCANOUT_MEMORY_VIEW *View);

NTSTATUS AdmissionMemoryRuntimeScanoutView(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ ADMISSION_SCANOUT_MEMORY_VIEW *View);
/* CUR-1 v2: the driver-owned scan-out scratch (render_memory.h). */
NTSTATUS AdmissionMemoryRuntimeScanoutScratch(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ ADMISSION_SCANOUT_MEMORY_VIEW *View,
    _Out_ ULONGLONG *PoolOffset);
NTSTATUS AdmissionMemoryRuntimeExecutePaging(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_PAGING_RECORD *Record);
NTSTATUS AdmissionPagingStart(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionPagingStop(_Inout_ ADMISSION_CONTEXT *Context);
VOID AdmissionPagingDpc(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionSchedulerStart(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionSchedulerStop(_Inout_ ADMISSION_CONTEXT *Context);
BOOLEAN AdmissionSchedulerSubmitFence(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ UINT Fence);
BOOLEAN AdmissionSchedulerRecordCompletion(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ UINT Fence);
VOID AdmissionSchedulerDpc(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionDdiSubmitRender(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_SUBMITCOMMAND *Args);
NTSTATUS AdmissionGdiAdoptPrepatchedPacket(
    _Inout_ ADMISSION_CONTEXT *Adapter,
    _Inout_ ADMISSION_RENDER_CONTEXT *Context,
    _In_ const DXGKARG_SUBMITCOMMAND *Args);
NTSTATUS AdmissionBackendImageStart(
    _Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionBackendImageStop(
    _Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionPlatformRuntimeStart(
    _Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionPlatformRuntimeStop(
    _Inout_ ADMISSION_CONTEXT *Context);
/* Read while SchedulerLock protects work-item reservation transitions. */
BOOLEAN AdmissionPlatformRenderWorkerScheduled(_In_ ADMISSION_CONTEXT *Context);
VOID AdmissionSchedulerWorkerFinished(_Inout_ ADMISSION_CONTEXT *Context);
BOOLEAN AdmissionPlatformRuntimeReadyEx(
    _In_ ADMISSION_CONTEXT *Context, _Out_opt_ ULONG *FailedPredicate);
BOOLEAN AdmissionPlatformRuntimeReady(
    _Inout_ ADMISSION_CONTEXT *Context);
/* General depth two retains two packets per context, including kicked work. */
BOOLEAN AdmissionPlatformRuntimeGeneralDepth2(_In_opt_ ADMISSION_CONTEXT *Context);
/* EXP1014: bounded wait until the runtime is ready and the render slot is
 * empty; transient busy states are backpressure, not malformed input. */
VOID AdmissionPlatformRuntimeSlotChanged(_In_opt_ ADMISSION_CONTEXT *Context);
BOOLEAN AdmissionPlatformRuntimeAwaitWork(
    _In_ ADMISSION_CONTEXT *Context, _In_ ULONG TimeoutMs,
    _Out_opt_ ULONG *FailedPredicate);
/* F-A2: the same bounded wait with an optional predicate, tested on every
 * pass after SlotEvent was cleared (PASSIVE_LEVEL, no lock held): TRUE with
 * *Admitted set when it holds, although the runtime is not "ready and
 * empty". Admissible NULL is AdmissionPlatformRuntimeAwaitWork. */
typedef BOOLEAN (*ADMISSION_AWAIT_ADMISSIBLE)(ADMISSION_CONTEXT *Context,
                                              PVOID Argument);
BOOLEAN AdmissionPlatformRuntimeAwaitWorkEx(
    _In_ ADMISSION_CONTEXT *Context, _In_ ULONG TimeoutMs,
    _Out_opt_ ULONG *FailedPredicate,
    _In_opt_ ADMISSION_AWAIT_ADMISSIBLE Admissible, _In_opt_ PVOID Argument,
    _Out_opt_ BOOLEAN *Admitted);
BOOLEAN AdmissionPlatformRuntimeSubmit(
    _Inout_ ADMISSION_CONTEXT *Context);
/* Phase 5a: the runtime can take work later (started, not stopping,
 * resetting or failed); a busy slot or worker is not a refusal. WorkerBusy
 * reports a scheduled worker or a submitted backend job. */
BOOLEAN AdmissionPlatformRuntimeQueueable(_In_ ADMISSION_CONTEXT *Context,
    _Out_opt_ BOOLEAN *WorkerBusy);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
VOID AdmissionJobTimingStartWindows(ADMISSION_CONTEXT *Adapter,
    ADMISSION_RENDER_CONTEXT *RenderContext, ULONG ProcessId,
    ULONG Fence, ULONG DmaBytes);
/* rc81 receipt: ADMISSION_JOB_FLAG_* of the job's timing slot. */
VOID AdmissionJobTimingNoteWindows(ADMISSION_CONTEXT *Adapter, ULONG Fence,
    ULONG Flags);
#endif
/* G4-r3 (EXP1296): bound of the reset's wait for the firmware to end the
 * active job; DxgkDdiResetFromTimeout repeats it after a failed engine reset. */
#define ADMISSION_RESET_QUIESCE_TIMEOUT_MS 1000u
/* PASSIVE_LEVEL. STATUS_SUCCESS once the job with Fence has left the active
 * slot through the backend completion path (natural completion or firmware
 * drop); never stops RTKit. Fails when the bound passes, the worker does not
 * leave, or the provider cannot be polled. */
NTSTATUS AdmissionPlatformRuntimeQuiesceActive(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ ULONG Fence,
    _In_ ULONG TimeoutMs, _Inout_opt_ ADMISSION_GPU_RESET_RECEIPT *Receipt);
/* rc102 (EXP2045): after that quiescence (worker idle, Resetting set),
 * the scheduler's kicked fence when nothing holds it: the backend never
 * accepted it (SubmitSuccessor refused, no queue work became visible) and
 * no packet runs it. 0 otherwise. Its graph job must end unrun before the
 * private reset (AdmissionGpuvaG3AbortJob). */
ULONG AdmissionPlatformRuntimeUnheldKicked(_In_ ADMISSION_CONTEXT *Context);
/* Any IRQL, no locks: the platform half of Wom1GpuReset (backend/provider
 * phases, worker, firmware halted flag, recoveries). */
VOID AdmissionPlatformRuntimeResetState(_In_ ADMISSION_CONTEXT *Context,
    _Inout_ ADMISSION_GPU_RESET_RECEIPT *Receipt);
/* TDR-H0: bound of the wait for the idle worker and of the halt_count poll. */
#define ADMISSION_TDR_HALT_OBSERVE_WAIT_MS 1000u
#define ADMISSION_TDR_HALT_OBSERVE_POLL_MS 1000u
#define ADMISSION_TDR_HALT_OBSERVE_SEND_MS 100u
/* TDR-H0, PASSIVE_LEVEL, DxgkDdiResetFromTimeout entry only. Returns at once
 * and touches nothing unless the device value Wom1TdrHaltObserve was 1 at
 * runtime start. Otherwise: owns the idle worker (Resetting), sends the host
 * halt request, polls halt_count, records Observe, releases Resetting. It
 * never resumes the firmware. */
VOID AdmissionPlatformRuntimeTdrHaltObserve(_Inout_ ADMISSION_CONTEXT *Context,
    _Inout_ ADMISSION_GPU_TDR_HALT_OBSERVE *Observe);
/* Any IRQL, no locks: backend phase | provider phase << 8 | last poll guard
 * << 16 | WorkScheduled << 24 | Resetting << 25 | Stopping << 26. */
ULONG AdmissionPlatformRuntimeDebugState(_In_ ADMISSION_CONTEXT *Context);
/* G4-r3 (EXP1296): Wom1GpuReset (PASSIVE_LEVEL, flushed). */
void AdmissionRecordGpuReset(_In_opt_ ADMISSION_CONTEXT *Context);
/* GPU timestamps escape (ADMISSION_GPU_TIMESTAMP_QUERY), caller's process. */
NTSTATUS AdmissionPlatformGpuTimestampEscape(_Inout_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_ESCAPE *Args);
/* C11d: DxgkDdiCalibrateGpuClock (D3DKMTQueryClockCalibration): the GPU
 * clock in nanoseconds of the firmware timebase, the UMD timestamp units
 * (AdmissionGpuClockCalibrate); any IRQL <= DISPATCH_LEVEL. */
NTSTATUS AdmissionPlatformCalibrateGpuClock(_In_ ADMISSION_CONTEXT *Context,
    _Out_ DXGKARG_CALIBRATEGPUCLOCK *Clock);
/* G4-r3 TDR drill escape (ADMISSION_TDR_DRILL), in the caller's process. */
NTSTATUS AdmissionPlatformTdrDrillEscape(_Inout_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_ESCAPE *Args);
/* TDR stage 2b (TDR-FIRMWARE-RESTART-RECOVERY.md 8.3 with the section 9
 * corrections). PASSIVE_LEVEL. One orderly warm restart of the firmware:
 * ASC CPU and RTKit session stopped and booted again, initdata graph and
 * queue provider rebuilt, power domains and the retained UAT root left as
 * they are (no PREPARE, ACTIVATE, CLOSE or power command). Accepted only
 * with nothing in flight (no job, packet, worker or reset); refused
 * otherwise without touching anything. A failure after the runtime was
 * taken leaves SchedulerFaulted set and the runtime stopped for the
 * ordinary teardown. No reset DDI calls this yet: its only caller is the
 * drill escape below. Wom1GpuReset v5 (Restart) records every call. */
NTSTATUS AdmissionPlatformRuntimeRestart(_Inout_ ADMISSION_CONTEXT *Context);
/* Firmware restart drill escape (ADMISSION_FW_RESTART_DRILL): refused with
 * STATUS_NOT_SUPPORTED unless the device value Wom1FwRestartDrillAllowed
 * was 1 at runtime start. */
NTSTATUS AdmissionPlatformFwRestartDrillEscape(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ const DXGKARG_ESCAPE *Args);
NTSTATUS AdmissionGpuvaG3FaultDrillEscape(_Inout_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_ESCAPE *Args);
BOOLEAN AdmissionGpuvaG3FaultDrillApply(_Inout_ ADMISSION_CONTEXT *Context,
    ULONG Fence, _Inout_ unsigned long long *StreamBase,
    _Inout_ unsigned int *StreamBytes);
BOOLEAN AdmissionPlatformRuntimeResponsive(
    _Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionWin32SnapshotRenderCommand(
    _In_ ADMISSION_RENDER_CONTEXT *Context,
    _In_ const DXGKARG_RENDER *Args,
    _Out_ ADMISSION_WIN32_RENDER_SNAPSHOT *Snapshot);
NTSTATUS AdmissionDynamicRenderBuild(
    _Inout_ ADMISSION_CONTEXT *Adapter,
    _Inout_ ADMISSION_RENDER_CONTEXT *Context,
    _Inout_ DXGKARG_RENDER *Args,
    _In_ const ADMISSION_WIN32_RENDER_SNAPSHOT *Snapshot,
    _Outptr_result_maybenull_ ADMISSION_OPEN_ALLOCATION **Opened,
    _Out_ ADMISSION_LOCAL_MEMORY_VIEW *Destination,
    _Out_ ADMISSION_GDI_PREPARED *Prepared,
    _Out_ ULONGLONG *AllocationOffset,
    _Out_ ULONGLONG *AllocationBytes);
NTSTATUS AdmissionScanoutStart(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionScanoutStop(_Inout_ ADMISSION_CONTEXT *Context);
/* D3D-FS1: TRUE for A8R8G8B8, and for A8B8G8R8 when the hypervisor scans
 * out RGBA8888 (APPLE_AGX_SCANOUT_CAP_RGBA8888). */
/* rc110 receipt: the qualified broker's ABI version and capabilities. */
VOID AdmissionScanoutBrokerInfo(_In_ ADMISSION_CONTEXT *Context,
                                _Out_ ULONG *AbiVersion,
                                _Out_ ULONG *Capabilities);
/* The broker's format code for a scanout-capable D3DDDIFORMAT, else 0. */
ULONG AdmissionScanoutFormatCode(_In_ ADMISSION_CONTEXT *Context,
                                 _In_ D3DDDIFORMAT Format);
BOOLEAN AdmissionScanoutFormatSupported(_In_ ADMISSION_CONTEXT *Context,
                                        _In_ D3DDDIFORMAT Format);
/* rc106: source modes beside the native one are offered only when the
 * broker scales (APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE) and this build switch
 * is on; otherwise the single native 2560x1600 mode is unchanged. */
#ifndef ADMISSION_DISPLAY_SCALED_MODES
#define ADMISSION_DISPLAY_SCALED_MODES 1
#endif
BOOLEAN AdmissionScanoutScaledModes(_In_ ADMISSION_CONTEXT *Context);
/* Index 0 is the native mode; FALSE past the last offered mode. */
BOOLEAN AdmissionScanoutSourceModeAt(_In_ ADMISSION_CONTEXT *Context,
                                     _In_ ULONG Index, _Out_ ULONG *Width,
                                     _Out_ ULONG *Height, _Out_ ULONG *Stride);
BOOLEAN AdmissionScanoutSourceModeSupported(_In_ ADMISSION_CONTEXT *Context,
                                            _In_ ULONG Width,
                                            _In_ ULONG Height,
                                            _In_ ULONG Stride);
/* Bit (1u << D3DKMDT_VPPS_x) for every scaling cofunctional with a source of
 * Width x Height; zero when the size is not an offered source mode. */
ULONG AdmissionScanoutScalingSupport(_In_ ADMISSION_CONTEXT *Context,
                                     _In_ ULONG Width, _In_ ULONG Height);
/* The transform a commit applies for a requested D3DKMDT_VPPS value. */
BOOLEAN AdmissionScanoutResolveScaling(
    _In_ ADMISSION_CONTEXT *Context, _In_ ULONG Width, _In_ ULONG Height,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Requested,
    _Out_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING *Resolved);
NTSTATUS AdmissionScanoutCommit(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Width, _In_ ULONG Height, _In_ ULONG Stride,
    _In_ D3DDDIFORMAT Format,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling);
NTSTATUS AdmissionScanoutSetVisible(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ BOOLEAN Visible);
NTSTATUS AdmissionScanoutQueuePresent(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_SETVIDPNSOURCEADDRESS *Args);
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS AdmissionScanoutQueueDeferred(_Inout_ ADMISSION_CONTEXT *Context);
VOID AdmissionScanoutTagMpoFlip(_Inout_ ADMISSION_CONTEXT *Context,
                                _In_ ULONGLONG PresentId);
VOID AdmissionScanoutClearMpoFlip(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionScanoutMpoPlaneOff(_Inout_ ADMISSION_CONTEXT *Context,
                                     _In_ ULONGLONG PresentId);
/* S2 (all inert unless AdmissionScanoutMpoEffective). */
/* TRUE iff the Wom1MpoTwoPlanes switch was set at StartDevice, the broker
 * advertised CAPABILITIES bit 9 and REG_NEGOTIATED read back 0x200. */
BOOLEAN AdmissionScanoutMpoEffective(_In_opt_ ADMISSION_CONTEXT *Context);
/* STRETCH-1: effective AND the broker advertised CAP_SCALED_SOURCE (bit 8):
 * a layer record's source size may differ from its destination's. */
BOOLEAN AdmissionScanoutMpoScaledSource(_In_opt_ ADMISSION_CONTEXT *Context);
/* Pool view for plane placement: segment base, pool bytes. */
BOOLEAN AdmissionScanoutMpoPool(_In_ ADMISSION_CONTEXT *Context,
                                _Out_ ULONGLONG *SegmentBase,
                                _Out_ ULONGLONG *PoolBytes);
/* Slot 1 may be on screen: the driver queued a two-layer commit whose
 * removal has not latched, or REG_ACTIVE_LAYERS bit 1 is set. */
BOOLEAN AdmissionScanoutMpoSlot1MayShow(_In_ ADMISSION_CONTEXT *Context);
/* HOLD-1: no scan-out commit is outstanding (the scan-out gate is free);
 * readable at any IRQL. */
BOOLEAN AdmissionScanoutMpoGateFree(_In_ ADMISSION_CONTEXT *Context);
/* HOLD-1: commit the held plane set; scan-out ISR context (device IRQL). */
VOID AdmissionMpoCommitPending(_In_ ADMISSION_CONTEXT *Context);
/* HOLD-2: the one commit path with its source (ADMISSION_MPO_TIMING_COMMIT_*). */
VOID AdmissionMpoCommitPendingSource(_In_ ADMISSION_CONTEXT *Context,
                                     _In_ ULONG Source);
VOID AdmissionMpoTimingRecord(_In_opt_ ADMISSION_CONTEXT *Context, _In_ ULONG Kind,
                              _In_ ULONG Mask, _In_ ULONGLONG Id);
/* Scan-out start / stop (PASSIVE). */
VOID AdmissionMpoCommitDpcStart(_In_opt_ ADMISSION_CONTEXT *Context);
VOID AdmissionMpoCommitDpcStop(_In_opt_ ADMISSION_CONTEXT *Context);
/* HOLD-3: a latch (Latch TRUE) or a vertical tick seen by the scan-out ISR. */
VOID AdmissionMpoNoteLatch(_In_ ADMISSION_CONTEXT *Context);
VOID AdmissionMpoNoteTick(_In_ ADMISSION_CONTEXT *Context);
/* HOLD-3b: the latch handler (Latch TRUE) and the vertical tick hand a held
 * set to the commit DPC (deadline rule); they never commit it. */
VOID AdmissionMpoQueueCommit(_In_ ADMISSION_CONTEXT *Context, _In_ BOOLEAN Latch);
/* HOLD-3b receipt: after each MPO vsync notification (scan-out ISR). */
VOID AdmissionMpoNoteNotify(_In_ ADMISSION_CONTEXT *Context);
/* Queue one V3 commit (Count 1 = removal, 2 = two planes); Completion is
 * reported at its latch.  PrimaryAddress is the LayerIndex 0 surface
 * (CRTC_VSYNC physical address bookkeeping). */
NTSTATUS AdmissionScanoutMpoCommit(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_reads_(Count) const APPLE_AGX_SCANOUT_LAYER *Records,
    _In_ ULONG Count, _In_ const ADMISSION_MPO_COMPLETION *Completion,
    _In_ LONGLONG PrimaryAddress);
/* The PresentIds of the next V2 flip (QueuePresent/QueueDeferred carry
 * them to its latch); replaces AdmissionScanoutTagMpoFlip's single id. */
VOID AdmissionScanoutTagMpoFlipSet(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_MPO_COMPLETION *Completion);
/* Planes switched off without a swap: completes on the next vsync. */
NTSTATUS AdmissionScanoutMpoPlaneOffSet(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_MPO_COMPLETION *Completion);
/* mpo_windows.c: the effective-only Set3 path. */
NTSTATUS AdmissionMpoSetVidPnSourceAddress3(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Inout_ DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3 *Args);
/* Wom1MpoReceipt (PASSIVE_LEVEL; only while effective). */
void AdmissionRecordMpo(_In_ ADMISSION_CONTEXT *Context);
void AdmissionRecordMpoPeriodic(_In_opt_ ADMISSION_CONTEXT *Context);
/* NP-1: queue the periodic MPO publish from a path that may run above
 * PASSIVE_LEVEL (Set3); no-op above DISPATCH_LEVEL or without the work item. */
void AdmissionQueueMpoPublish(_In_opt_ ADMISSION_CONTEXT *Context);
/* NP-1: allocate (scanout start, PASSIVE) / drain and free (scanout stop,
 * PASSIVE) the Set3-side publish work item. */
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionMpoPublishStart(_In_opt_ ADMISSION_CONTEXT *Context);
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionMpoPublishStop(_In_opt_ ADMISSION_CONTEXT *Context);
/* S2-1b: Wom1MpoGateReceipt (PASSIVE_LEVEL; ignored above). */
void AdmissionRecordMpoGate(_In_ ADMISSION_CONTEXT *Context);
LONGLONG AdmissionMpoGateQpc(_In_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionScanoutSetTimelinePaused(_Inout_ ADMISSION_CONTEXT *Context,
                                          _In_ BOOLEAN Paused);
VOID AdmissionScanoutDpc(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionScanoutQueryTimeline(_Inout_ ADMISSION_CONTEXT *Context,
                                      _Inout_ APPLE_AGX_VSYNC_QUERY *Query);
BOOLEAN AdmissionScanoutInterrupt(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionScanoutControlInterrupt(
    _Inout_ ADMISSION_CONTEXT *Context, _In_ BOOLEAN Enable);
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
NTSTATUS AdmissionVisibleAgxResolveDestination(
    _Inout_ ADMISSION_CONTEXT *Adapter,
    _In_ const ADMISSION_RENDER_CONTEXT *Context,
    _In_reads_(AllocationCount) const DXGK_ALLOCATIONLIST *Allocations,
    _In_ UINT AllocationCount,
    _In_ UINT RenderAllocationIndex,
    _In_ const ADMISSION_LOCAL_MEMORY_VIEW *RenderDestination,
    _Out_ ADMISSION_LOCAL_MEMORY_VIEW *VisibleDestination,
    _Out_ ULONGLONG *AllocationToken);
NTSTATUS AdmissionScanoutPresentAgxResult(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    _Inout_ ADMISSION_COMPLETED_OUTPUT *Completed,
    _In_ const ADMISSION_TERMINAL_RECEIPT *OutputReceipt,
    _In_reads_bytes_(SourceBytes) const VOID *Source,
    _In_ ULONG SourceBytes, _In_ ULONGLONG SourceGpuAddress,
    _In_ ULONGLONG SourcePhysicalAddress, _In_ ULONG Fence);
BOOLEAN AdmissionScanoutAllowsRender(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_ALLOCATION_OBJECT *Owner);
NTSTATUS AdmissionScanoutRetireAllocation(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Inout_ ADMISSION_ALLOCATION_OBJECT *Owner);
NTSTATUS AdmissionScanoutRetireQualification(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Inout_ ADMISSION_RETIREMENT_QUERY *Query);
NTSTATUS AdmissionScanoutQueryQualification(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Inout_ ADMISSION_PRESENT_QUERY *Query);
VOID AdmissionRecordVisibleAgx(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_VISIBLE_AGX_RECEIPT *Receipt);
#endif
#if defined(APPLE_AGX_VISIBLE_SCANOUT_QUALIFICATION)
VOID AdmissionRecordVisibleScanout(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_VISIBLE_SCANOUT_RECEIPT *Receipt);
#endif

DXGKDDI_ADD_DEVICE AdmissionDdiAddDevice;
DXGKDDI_START_DEVICE AdmissionDdiStartDevice;
DXGKDDI_STOP_DEVICE AdmissionDdiStopDevice;
DXGKDDI_REMOVE_DEVICE AdmissionDdiRemoveDevice;
DXGKDDI_DISPATCH_IO_REQUEST AdmissionDdiDispatchIoRequest;
DXGKDDI_INTERRUPT_ROUTINE AdmissionDdiInterruptRoutine;
DXGKDDI_DPC_ROUTINE AdmissionDdiDpcRoutine;
DXGKDDI_QUERY_CHILD_RELATIONS AdmissionDdiQueryChildRelations;
DXGKDDI_QUERY_CHILD_STATUS AdmissionDdiQueryChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR AdmissionDdiQueryDeviceDescriptor;
DXGKDDI_SET_POWER_STATE AdmissionDdiSetPowerState;
DXGKDDI_RESET_DEVICE AdmissionDdiResetDevice;
DXGKDDI_UNLOAD AdmissionDdiUnload;
DXGKDDI_QUERYADAPTERINFO AdmissionDdiQueryAdapterInfo;
DXGKDDI_SETPALETTE AdmissionDdiSetPalette;
DXGKDDI_SETPOINTERPOSITION AdmissionDdiSetPointerPosition;
DXGKDDI_SETPOINTERSHAPE AdmissionDdiSetPointerShape;
DXGKDDI_ISSUPPORTEDVIDPN AdmissionDdiIsSupportedVidPn;
DXGKDDI_ISSUPPORTEDVIDPN AdmissionTraceDdiIsSupportedVidPn;
DXGKDDI_RECOMMENDFUNCTIONALVIDPN AdmissionDdiRecommendFunctionalVidPn;
DXGKDDI_RECOMMENDFUNCTIONALVIDPN AdmissionTraceDdiRecommendFunctionalVidPn;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY AdmissionDdiEnumVidPnCofuncModality;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY AdmissionTraceDdiEnumVidPnCofuncModality;
DXGKDDI_SETVIDPNSOURCEVISIBILITY AdmissionDdiSetVidPnSourceVisibility;
DXGKDDI_SETVIDPNSOURCEVISIBILITY AdmissionTraceDdiSetVidPnSourceVisibility;
DXGKDDI_COMMITVIDPN AdmissionDdiCommitVidPn;
DXGKDDI_COMMITVIDPN AdmissionTraceDdiCommitVidPn;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH AdmissionDdiUpdateActiveVidPnPresentPath;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH
AdmissionTraceDdiUpdateActiveVidPnPresentPath;
DXGKDDI_RECOMMENDMONITORMODES AdmissionDdiRecommendMonitorModes;
DXGKDDI_RECOMMENDMONITORMODES AdmissionTraceDdiRecommendMonitorModes;
DXGKDDI_UPDATEMONITORLINKINFO AdmissionDdiUpdateMonitorLinkInfo;
DXGKDDI_UPDATEMONITORLINKINFO AdmissionTraceDdiUpdateMonitorLinkInfo;
DXGKDDI_GETSCANLINE AdmissionDdiGetScanLine;
DXGKDDI_STOPCAPTURE AdmissionDdiStopCapture;
DXGKDDI_QUERYVIDPNHWCAPABILITY AdmissionDdiQueryVidPnHWCapability;
DXGKDDI_QUERYVIDPNHWCAPABILITY AdmissionTraceDdiQueryVidPnHWCapability;
DXGKDDI_SETVIDPNSOURCEADDRESS AdmissionDdiSetVidPnSourceAddress;
DXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY3
    AdmissionDdiSetVidPnSourceAddressWithMultiPlaneOverlay3;
/* S2 (mpo_windows.c): registered always; dxgkrnl calls them only while
 * DRIVERCAPS.SupportMultiPlaneOverlay is TRUE (R22). */
DXGKDDI_CHECKMULTIPLANEOVERLAYSUPPORT3
    AdmissionDdiCheckMultiPlaneOverlaySupport3;
DXGKDDI_GETMULTIPLANEOVERLAYCAPS AdmissionDdiGetMultiPlaneOverlayCaps;
DXGKDDI_STOP_DEVICE_AND_RELEASE_POST_DISPLAY_OWNERSHIP
AdmissionDdiStopDeviceAndReleasePostDisplayOwnership;
DXGKDDI_NOTIFY_ACPI_EVENT AdmissionDdiNotifyAcpiEvent;
DXGKDDI_QUERY_INTERFACE AdmissionDdiQueryInterface;
VOID AdmissionDdiControlEtwLogging(_In_ BOOLEAN Enable, _In_ ULONG Flags,
                                   _In_ UCHAR Level);
DXGKDDI_CREATEDEVICE AdmissionDdiCreateDevice;
DXGKDDI_DESTROYDEVICE AdmissionDdiDestroyDevice;
DXGKDDI_CREATEALLOCATION AdmissionDdiCreateAllocation;
DXGKDDI_DESTROYALLOCATION AdmissionDdiDestroyAllocation;
DXGKDDI_DESCRIBEALLOCATION AdmissionDdiDescribeAllocation;
DXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA
AdmissionDdiGetStandardAllocationDriverData;
DXGKDDI_OPENALLOCATIONINFO AdmissionDdiOpenAllocation;
DXGKDDI_CLOSEALLOCATION AdmissionDdiCloseAllocation;
DXGKDDI_PATCH AdmissionDdiPatch;
DXGKDDI_SUBMITCOMMAND AdmissionDdiSubmitCommand;
DXGKDDI_BUILDPAGINGBUFFER AdmissionDdiBuildPagingBuffer;
DXGKDDI_PREEMPTCOMMAND AdmissionDdiPreemptCommand;
DXGKDDI_RENDER AdmissionDdiRender;
DXGKDDI_PRESENT AdmissionDdiPresent;
DXGKDDI_CREATEOVERLAY AdmissionDdiCreateOverlay;
DXGKDDI_UPDATEOVERLAY AdmissionDdiUpdateOverlay;
DXGKDDI_FLIPOVERLAY AdmissionDdiFlipOverlay;
DXGKDDI_DESTROYOVERLAY AdmissionDdiDestroyOverlay;
DXGKDDI_RESETFROMTIMEOUT AdmissionDdiResetFromTimeout;
DXGKDDI_RESTARTFROMTIMEOUT AdmissionDdiRestartFromTimeout;
DXGKDDI_ESCAPE AdmissionDdiEscape;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
BOOLEAN AdmissionGpuvaG3PrivateContextBusy(ADMISSION_RENDER_CONTEXT *);
BOOLEAN AdmissionGpuvaG3PrivateRetireContext(ADMISSION_RENDER_CONTEXT *);
BOOLEAN AdmissionGpuvaG3PrivateReset(ADMISSION_CONTEXT *);
BOOLEAN AdmissionGpuvaG3CompleteJobTimestamp(ADMISSION_CONTEXT *,
    ADMISSION_RENDER_CONTEXT *, ULONG Fence, ULONGLONG TimestampVa,
    ULONGLONG TimestampEndNs);
/* L1 receipt-only: enables the G3 lock profile on its first call and writes
 * Wom1G3LockProfile (and Wom1G3LockProfileHot when Dense) into Key. */
VOID AdmissionGpuvaG3LockProfileExport(ADMISSION_CONTEXT *, HANDLE Key,
    BOOLEAN Dense);
/* noinline: its _ReturnAddress names the cancelling path (receipt). */
__declspec(noinline) VOID AdmissionGpuvaG3PrivateCancel(ADMISSION_RENDER_CONTEXT *, ULONG, BOOLEAN);
VOID AdmissionGpuvaG3PrivatePreempt(ADMISSION_RENDER_CONTEXT *, ULONG);
BOOLEAN AdmissionGpuvaG3PrivateReported(ADMISSION_CONTEXT *, ADMISSION_RENDER_CONTEXT *, ULONG);
VOID AdmissionGpuvaG3CaptureFaultMapping(ADMISSION_CONTEXT *,
    const ADMISSION_GPU_FAULT_EVENT *, ADMISSION_GPU_FAULT_MAPPING *);
/* Phase 5a: bind the head pending submission into the empty render slot
 * (worker, PASSIVE_LEVEL); FALSE only when an accepted job cannot be bound. */
BOOLEAN AdmissionG4PendingBindHead(ADMISSION_CONTEXT *);
/* Phase 5a: drop every pending submission like a queued packet (SchedulerLock
 * held); Preempt selects preemption (resubmitted later) over cancel. */
VOID AdmissionG4PendingDropLocked(ADMISSION_CONTEXT *, BOOLEAN Preempt);
/* rc90 build-ahead (render_job_ahead.h). FillLocked: the adapter-side
 * inputs of a decision (SchedulerLock held; CpuHeadFence needs PagingLock
 * and is left to the caller). PrebindHead: while the running job whose
 * image was released runs, bind the head pending submission into the image
 * (PASSIVE_LEVEL, no lock held; State carries the runtime inputs and
 * receives the rest; TRUE when an entry is now Prebound). PromoteLocked:
 * move the Prebound head with this fence into the empty RenderPacket as
 * Queued (SchedulerLock held). AheadUnbind: release a Prebound (or
 * dropped) entry's image and return it to Queued or free it. */
struct _ADMISSION_JOB_AHEAD_STATE;
VOID AdmissionJobAheadFillLocked(ADMISSION_CONTEXT *,
    struct _ADMISSION_JOB_AHEAD_STATE *);
BOOLEAN AdmissionG4PendingPrebindHead(ADMISSION_CONTEXT *,
    struct _ADMISSION_JOB_AHEAD_STATE *, ULONG *Refusal);
BOOLEAN AdmissionG4PendingAheadPromoteLocked(ADMISSION_CONTEXT *, ULONG Fence);
VOID AdmissionG4PendingAheadUnbind(ADMISSION_CONTEXT *);
/* rc102 (render_job_window.h). KickLocked: the Prebound head with this
 * fence is being kicked behind the running job: the scheduler's queued
 * fence becomes its KickedFence, the context gets the entry's DMA range
 * and mapping generation (its BeginJob reads them), the entry turns
 * Kicked (PagingLock and SchedulerLock held). AdoptLocked: the kicked
 * entry becomes the active render packet once the running job's
 * completion was reported, as dispatch + activation would (SchedulerLock
 * held). AbortKickedLocked: a reset aborted the kicked fence before it
 * was adopted: its scene is cancelled, the context released and the
 * entry freed (it may have run: never resubmitted). */
BOOLEAN AdmissionG4PendingKickLocked(ADMISSION_CONTEXT *, ULONG Fence);
BOOLEAN AdmissionG4PendingAdoptLocked(ADMISSION_CONTEXT *, ULONG Fence);
VOID AdmissionG4PendingAbortKickedLocked(ADMISSION_CONTEXT *);
/* Depth-2 kick fallback (investigation/analysis/DEPTH2-KICK-FALLBACK.md):
 * the kick of the Kicked head with this fence was refused before the
 * firmware saw it: the scheduler's kick is undone and the entry turns
 * Prebound again, so the running job's ahead chain (or ordinary dispatch)
 * submits it at depth 1; during a boundary preemption the scheduler hands
 * the fence back to VidSch and the entry is dropped as
 * AdmissionG4PendingDropLocked drops a Prebound one (PagingLock and
 * SchedulerLock held). */
BOOLEAN AdmissionG4PendingUnkickLocked(ADMISSION_CONTEXT *, ULONG Fence);
/* rc102: a reset proved this fence's job never reached the firmware (a
 * kicked successor never handed over, or an active job whose submit
 * failed): end its graph job unrun (the lease stays with a job still
 * running) and unstart its private scene. PASSIVE_LEVEL. */
VOID AdmissionGpuvaG3AbortJob(ADMISSION_CONTEXT *, ULONG Fence);
NTSTATUS AdmissionGpuvaG3PrivateEscape(ADMISSION_CONTEXT *, const DXGKARG_ESCAPE *);
#endif
DXGKDDI_COLLECTDBGINFO AdmissionDdiCollectDbgInfo;
DXGKDDI_QUERYCURRENTFENCE AdmissionDdiQueryCurrentFence;
DXGKDDI_CONTROLINTERRUPT AdmissionDdiControlInterrupt;
DXGKDDI_CREATECONTEXT AdmissionDdiCreateContext;
DXGKDDI_DESTROYCONTEXT AdmissionDdiDestroyContext;
DXGKDDI_RENDERKM AdmissionDdiRenderKm;
DXGKDDI_QUERYDEPENDENTENGINEGROUP AdmissionDdiQueryDependentEngineGroup;
DXGKDDI_QUERYENGINESTATUS AdmissionDdiQueryEngineStatus;
DXGKDDI_RESETENGINE AdmissionDdiResetEngine;
DXGKDDI_CANCELCOMMAND AdmissionDdiCancelCommand;
DXGKDDISETPOWERCOMPONENTFSTATE AdmissionDdiSetPowerComponentFState;
DXGKDDIPOWERRUNTIMECONTROLREQUEST AdmissionDdiPowerRuntimeControlRequest;
DXGKDDI_GETNODEMETADATA AdmissionDdiGetNodeMetadata;
DXGKDDI_SUBMITCOMMANDVIRTUAL AdmissionDdiSubmitCommandVirtual;
DXGKDDI_CREATEPROCESS AdmissionDdiCreateProcess;
DXGKDDI_DESTROYPROCESS AdmissionDdiDestroyProcess;
DXGKDDI_CALIBRATEGPUCLOCK AdmissionDdiCalibrateGpuClock;
DXGKDDI_SETSTABLEPOWERSTATE AdmissionDdiSetStablePowerState;
BOOLEAN AdmissionGpuvaV5ClientOpen(ADMISSION_CONTEXT *Context,
                                   APPLE_AGX_GPUVA_V5_CLIENT *Client);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
NTSTATUS AdmissionGpuvaG3Start(ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionGpuvaG3Stop(ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionGpuvaG3AttachDevice(ADMISSION_CONTEXT *Adapter,
    ADMISSION_DEVICE *Device, HANDLE ProcessHandle);
VOID AdmissionGpuvaG3DetachDevice(ADMISSION_DEVICE *Device);
NTSTATUS AdmissionGpuvaG3AttachContext(ADMISSION_RENDER_CONTEXT *Context,
    ADMISSION_DEVICE *Device);
/* G2 HW2b: give the context's process its present arena when it is idle;
 * failure leaves the process without one (receipt Wom1G2PresentArena). */
NTSTATUS AdmissionGpuvaG3EnsurePresentArena(ADMISSION_RENDER_CONTEXT *Context);
/* G2 HW2b: queue a GPUVA present blt as a KMD compute job in the presenting
 * process's VM. STATUS_NOT_SUPPORTED leaves the present to the CPU path. */
NTSTATUS AdmissionGpuvaG3SubmitPresentCompute(ADMISSION_CONTEXT *Adapter,
    ADMISSION_RENDER_CONTEXT *Context, const DXGKARG_SUBMITCOMMANDVIRTUAL *Args,
    const ADMISSION_PRESENT_BLT_COMMAND *Command,
    const APPLE_AGX_GDI_RECT *Rects, ULONG RectCount);
VOID AdmissionGpuvaG3DetachContext(ADMISSION_RENDER_CONTEXT *Context);
VOID AdmissionGpuvaG3NoteAllocationPaging(ADMISSION_CONTEXT *Context,
                                          HANDLE Allocation, ULONG Operation,
                                          ULONG Segment);
NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(ADMISSION_CONTEXT *Context,
    DXGKARG_BUILDPAGINGBUFFER *Args);
/* EXP1026: completes disallowed internal statuses (see the definition). */
NTSTATUS AdmissionGpuvaG3BuildPagingBufferChecked(ADMISSION_CONTEXT *Context,
    DXGKARG_BUILDPAGINGBUFFER *Args);
DXGKDDI_SETROOTPAGETABLE AdmissionDdiSetRootPageTable;
#endif
#if defined(APPLE_AGX_GPUVA_B1_QUALIFICATION)
typedef struct _ADMISSION_B1_RETIREMENT_RECEIPT {
  ULONG Version, Bytes, Owner, Step, Status;
  ULONG TaStamp, TaExpectedStamp, TaDone, TaExpectedDone;
  ULONG D3Stamp, D3ExpectedStamp, D3Done, D3ExpectedDone;
  ULONG EventRead, EventWrite, PollGuard, DrainGuard, IngestGuard;
  ULONG BackendPhase, TaComplete, D3Complete, B1Completed;
  ULONG BrokerStatus;
  ULONGLONG BrokerReceipt, BrokerEpoch;
} ADMISSION_B1_RETIREMENT_RECEIPT;

typedef struct _ADMISSION_B1_CLEANUP_RECEIPT {
  ULONG Version, Bytes, Step, Owner, Page, Status, FirstFailure;
  ULONG BrokerStatus, RootMapped, RootGrants, RootParents, RootTables;
  ULONG RootCreated, RootUncertain, StateUncertain, OwnedPages;
  ULONG MemoryResult;
} ADMISSION_B1_CLEANUP_RECEIPT;

typedef struct _ADMISSION_B1_CONTEXT0_HASH_RECEIPT {
  ULONG Version, Bytes, Phase, Status, PageCount;
  ULONGLONG Hash;
} ADMISSION_B1_CONTEXT0_HASH_RECEIPT;

enum {
  AdmissionB1RetireJobBegin = 0,
  AdmissionB1RetireSubmit = 1,
  AdmissionB1RetirePollBefore = 2,
  AdmissionB1RetirePollAfter = 3,
  AdmissionB1RetireCpuFlush = 4,
  AdmissionB1RetireImageRelease = 5,
  AdmissionB1RetireJobEnd = 6,
  AdmissionB1RetireRelease = 7,
  AdmissionB1RetireTlbAck = 8,
  AdmissionB1RetireStepCount = 9
};

NTSTATUS AdmissionGpuvaB1Qualify(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionGpuvaB1RunFirmware(_Inout_ ADMISSION_CONTEXT *Context,
                                     _In_ PVOID OutputCpu,
                                     _In_ ULONGLONG OutputPhysical,
                                     _In_ ULONGLONG OutputVa,
                                     _In_ ULONG Fence, _In_ ULONG Owner);
void AdmissionGpuvaB1RecordRetirement(_In_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Owner, _In_ ULONG Step, _In_ NTSTATUS Status,
    _In_ ULONG BrokerStatus, _In_ ULONGLONG BrokerReceipt,
    _In_ ULONGLONG BrokerEpoch);
void AdmissionRecordB1Retirement(_In_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_B1_RETIREMENT_RECEIPT *Receipt);
void AdmissionRecordB1Cleanup(_In_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_B1_CLEANUP_RECEIPT *Receipt);
BOOLEAN AdmissionGpuvaB1Context0Hash(_In_ ADMISSION_CONTEXT *Context,
    _Out_ ULONGLONG *Hash, _Out_ ULONG *PageCount);
void AdmissionRecordB1Context0Hash(_In_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_B1_CONTEXT0_HASH_RECEIPT *Receipt);
void AdmissionRecordB1Qualification(_In_ ADMISSION_CONTEXT *Context,
                                    _In_ ULONG Stage, _In_ NTSTATUS Status,
                                    _In_ NTSTATUS FirstFailure,
                                    _In_ ULONG Precheck,
                                    _In_ ULONG ProbeStatus,
                                    _In_ ULONGLONG ProbeEpoch,
                                    _In_ ULONG CompletedJobs,
                                    _In_ ULONG BrokerStatus,
                                    _In_ ULONG CleanupStatus,
                                    _In_ ULONG OutputPixelA,
                                    _In_ ULONG OutputPixelB);
#endif

/* RC10: no allocation, registry I/O or handle dereference in status capture. */
BOOLEAN AdmissionRecordDdiStatus(ULONG Slot, NTSTATUS Status, ULONG Operation,
    ULONGLONG Allocation, ULONGLONG Object);
HANDLE AdmissionPagingStatusAllocation(const DXGKARG_BUILDPAGINGBUFFER *Args);
void AdmissionQueueDdiStatus(ADMISSION_CONTEXT *Context);
void AdmissionRecordPagingInternalStatus(ADMISSION_CONTEXT *Context,
    const DXGKARG_BUILDPAGINGBUFFER *Args, NTSTATUS Status);

#endif
