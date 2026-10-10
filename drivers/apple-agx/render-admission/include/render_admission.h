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
#include "render_objects.h"
#include "render_memory.h"
#include "render_allocation.h"
#include "render_hvc.h"
#include "apple_agx_memory.h"
#include "apple_agx_g3_private_failure.h"
#include "apple_agx_local_reserve_abi.h"
#include "apple_agx_g3_copy_query_receipt.h"
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_residency.h"
#include "apple_agx_uat_publication.h"
#include "render_paging.h"
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

#define ADMISSION_POOL_TAG 'mRGA'
#define ADMISSION_DMA_BUFFER_SIZE 0x50000u
#define ADMISSION_ALLOCATION_LIST_SIZE 64u
#define ADMISSION_PATCH_LIST_SIZE 64u
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 0x51000u
#define ADMISSION_GDI_ALLOCATION_LIST_SIZE 256u
#define ADMISSION_GDI_PATCH_LIST_SIZE 256u
#define ADMISSION_MAX_PAGING_RECORDS 64u

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
/* EXP1001: the in-memory ring covers ~200 s; failure snapshots keep 256
 * records, matching events (same table, VA or allocation) first. */
#define ADMISSION_G3_LEAF_RING 8192u
#define ADMISSION_G3_LEAF_EVENT_RESET 0x10u
#define ADMISSION_G3_LEAF_EVENT_SYSTEM_RETIRE 0x20u
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
#define ADMISSION_G4_PENDING_CAPACITY 2u
typedef enum _ADMISSION_G4_PENDING_STATE {
  AdmissionG4PendingFree = 0,
  AdmissionG4PendingFilling, /* reserved by the submitter, not yet counted */
  AdmissionG4PendingQueued,
  AdmissionG4PendingBinding,
  AdmissionG4PendingDropped,
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
  APPLE_AGX_G4_ATTACHMENT Attachment;
  unsigned char Render[sizeof(APPLE_AGX_G4_NATIVE_RENDER)];
  unsigned char Native[APPLE_AGX_G4_NATIVE_MAX_BYTES];
} ADMISSION_G4_PENDING;

typedef struct _ADMISSION_CONTEXT {
  ADMISSION_OBJECT_ADAPTER ObjectAdapter;
  ADMISSION_MEMORY_CONTRACT Memory;
  PVOID MemoryRuntime;
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
  } G4SubmitFailure;
#endif
  volatile LONG InterruptReady;
  volatile LONG InterruptIngressEnabled;
  volatile LONG InterruptCount;
  volatile LONG InterruptAckCount;
  volatile LONG LastInterruptStatus;
  volatile LONG DpcCount;
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
  volatile LONG PagingStopping;
  volatile LONG PagingDpcPending;
  volatile LONG MemoryStartStage;
  volatile LONG MemoryStartStatus;
  KSPIN_LOCK SchedulerLock;
  APPLE_AGX_SCHEDULER Scheduler;
  ADMISSION_RENDER_PACKET RenderPacket;
  /* Phase 5a: validated G4 submissions of other contexts waiting for the
   * render slot, oldest first, under SchedulerLock (see
   * ADMISSION_G4_PENDING). The worker binds the head into RenderPacket. */
  ADMISSION_G4_PENDING G4Pending[ADMISSION_G4_PENDING_CAPACITY];
  ULONG G4PendingHead, G4PendingCount;
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
  volatile LONG FeatureReadyMask;
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

typedef struct _ADMISSION_ALLOCATION_HANDLE {
  ADMISSION_ALLOCATION_OBJECT Object;
  ULONG QualificationCookie;
  ULONG Win32ClassId;
  ULONG Win32Flags;
  ULONG WrittenPrimary;
  /* EXP1060: a classless allocation created with presentation resource data
   * (a swapchain or primary surface); the copy escape may access it. */
  ULONG Presentation;
  /* Created-with refresh rate reported by DxgkDdiDescribeAllocation. */
  UINT PrimaryRefreshNumerator;
  UINT PrimaryRefreshDenominator;
} ADMISSION_ALLOCATION_HANDLE;

#define ADMISSION_OPEN_ALLOCATION_MAGIC 0x4f504152u
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
void AdmissionRenderCorrelationSubmitFailureWindows(
    _In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG4SubmitFailure(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3CopyQueryFailure(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3LeafHistory(_In_opt_ ADMISSION_CONTEXT *Context);
void AdmissionRecordG3PagingWait(_In_opt_ ADMISSION_CONTEXT *Context);
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
void AdmissionRecordG3PrivateProcesses(_In_opt_ ADMISSION_CONTEXT *Context,
                                       _In_reads_bytes_(Bytes) const ULONG *Values,
                                       _In_ ULONG Bytes);
void AdmissionRecordGpuvaG3UnpublishedGroups(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_reads_(32) const ULONGLONG *Counts);
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
VOID AdmissionRecordEventDrain(_In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_EVENT_DRAIN_RECEIPT *Receipt);
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
#define AdmissionRecordEventDrain(Context, Receipt)                            \
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
/* EXP1014: bounded wait until the runtime is ready and the render slot is
 * empty; transient busy states are backpressure, not malformed input. */
VOID AdmissionPlatformRuntimeSlotChanged(_In_opt_ ADMISSION_CONTEXT *Context);
BOOLEAN AdmissionPlatformRuntimeAwaitWork(
    _In_ ADMISSION_CONTEXT *Context, _In_ ULONG TimeoutMs,
    _Out_opt_ ULONG *FailedPredicate);
BOOLEAN AdmissionPlatformRuntimeSubmit(
    _Inout_ ADMISSION_CONTEXT *Context);
/* Phase 5a: the runtime can take work later (started, not stopping,
 * resetting or failed); a busy slot or worker is not a refusal. */
BOOLEAN AdmissionPlatformRuntimeQueueable(_In_ ADMISSION_CONTEXT *Context);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
VOID AdmissionJobTimingStartWindows(ADMISSION_CONTEXT *Adapter,
    ADMISSION_RENDER_CONTEXT *RenderContext, ULONG ProcessId,
    ULONG Fence, ULONG DmaBytes);
#endif
NTSTATUS AdmissionPlatformRuntimeReset(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ APPLE_AGX_U32 *LastAbortedFence);
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
NTSTATUS AdmissionScanoutCommit(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ ULONG Width, _In_ ULONG Height, _In_ ULONG Stride,
    _In_ D3DDDIFORMAT Format);
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
VOID AdmissionGpuvaG3PrivateCancel(ADMISSION_RENDER_CONTEXT *, ULONG, BOOLEAN);
VOID AdmissionGpuvaG3PrivatePreempt(ADMISSION_RENDER_CONTEXT *, ULONG);
BOOLEAN AdmissionGpuvaG3PrivateReported(ADMISSION_CONTEXT *, ADMISSION_RENDER_CONTEXT *, ULONG);
/* Phase 5a: bind the head pending submission into the empty render slot
 * (worker, PASSIVE_LEVEL); FALSE only when an accepted job cannot be bound. */
BOOLEAN AdmissionG4PendingBindHead(ADMISSION_CONTEXT *);
/* Phase 5a: drop every pending submission like a queued packet (SchedulerLock
 * held); Preempt selects preemption (resubmitted later) over cancel. */
VOID AdmissionG4PendingDropLocked(ADMISSION_CONTEXT *, BOOLEAN Preempt);
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

#endif
