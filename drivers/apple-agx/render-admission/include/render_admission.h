#ifndef APPLE_AGX_RENDER_ADMISSION_H
#define APPLE_AGX_RENDER_ADMISSION_H

#include <ntddk.h>
#include <windef.h>
#include <winerror.h>
#include <wingdi.h>
#include <ntddvdeo.h>
#include <d3dkmddi.h>
#include <d3dkmthk.h>
#include <dispmprt.h>
#include <ntstrsafe.h>
#include "render_objects.h"
#include "render_memory.h"
#include "render_allocation.h"
#include "render_hvc.h"
#include "apple_agx_memory.h"
#include "apple_agx_residency.h"
#include "apple_agx_uat_publication.h"
#include "render_paging.h"
#include "render_gdi.h"
#include "render_umd_command.h"
#include "render_gdi_receipt.h"
#include "render_present.h"
#include "render_visible_scanout.h"
#include "render_submit_trace.h"
#include "render_submission.h"
#include "render_backend_image.h"
#include "apple_agx_wddm_feature_contract.h"
#include "apple_agx_scheduler.h"
#include "apple_agx_platform_provider.h"
#include "apple_agx_firmware_provider.h"
#include "apple_agx_device_control.h"
#include "apple_agx_initdata_memory.h"
#include "apple_agx_context0_broker.h"
#include "apple_agx_retained_root_abi.h"
#include "apple_agx_power.h"
#include "apple_agx_rtkit_session.h"
#include "apple_agx_fixed_panel.h"
#include "j313_agx_abi_admission.generated.h"

#define ADMISSION_POOL_TAG 'mRGA'
#define ADMISSION_DMA_BUFFER_SIZE 4096u
#define ADMISSION_ALLOCATION_LIST_SIZE 64u
#define ADMISSION_PATCH_LIST_SIZE 64u
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 8192u
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
} ADMISSION_SOURCE_ADDRESS_RECEIPT;

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
typedef struct _ADMISSION_CPU_PACKET {
  ULONG Fence, Kind, Bytes;
  union {
    ADMISSION_PAGING_RECORD Paging[ADMISSION_MAX_PAGING_RECORDS];
    UCHAR Present[ADMISSION_PRESENT_BLT_DMA_MAX];
  } Data;
} ADMISSION_CPU_PACKET;

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
  volatile LONG PaletteStatus;
  volatile LONG ScanLineStage;
  volatile LONG ScanLineStatus;
  volatile UCHAR *BrokerBase;
  volatile LONG InterruptReady;
  volatile LONG InterruptIngressEnabled;
  volatile LONG InterruptCount;
  volatile LONG InterruptAckCount;
  volatile LONG LastInterruptStatus;
  volatile LONG DpcCount;
  KSPIN_LOCK PagingLock;
  PIO_WORKITEM PagingWorkItem;
  KEVENT PagingIdle;
  ADMISSION_PAGING_RECORD PagingRecords[ADMISSION_MAX_PAGING_RECORDS];
  ULONG PagingRecordCount;
  ULONG PresentCopyBytes;
  ULONG CpuQueueHead, CpuQueueCount, DispatchedFence;
  ADMISSION_CPU_PACKET CpuQueue[APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
  UCHAR PresentCopyCommand[ADMISSION_PRESENT_BLT_DMA_MAX];
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
  ADMISSION_BACKEND_IMAGE BackendImage;
  volatile LONG SchedulerInitialized;
  volatile LONG SchedulerFaulted;
  volatile LONG SchedulerDpcPending;
  volatile LONG RenderDpcFence;
  volatile LONG FeatureReadyMask;
} ADMISSION_CONTEXT;

typedef struct _ADMISSION_DEVICE {
  ADMISSION_OBJECT_DEVICE Object;
} ADMISSION_DEVICE;

typedef struct _ADMISSION_RENDER_CONTEXT {
  ADMISSION_OBJECT_CONTEXT Object;
  APPLE_AGX_SCHEDULER_CONTEXT SchedulerContext;
  ADMISSION_PREPATCHED_RENDER PrepatchedRender;
} ADMISSION_RENDER_CONTEXT;

typedef struct _ADMISSION_ALLOCATION_HANDLE {
  ADMISSION_ALLOCATION_OBJECT Object;
  ULONG QualificationCookie;
} ADMISSION_ALLOCATION_HANDLE;

#define ADMISSION_OPEN_ALLOCATION_MAGIC 0x4f504152u
typedef struct _ADMISSION_OPEN_ALLOCATION {
  ULONG Magic;
  ADMISSION_DEVICE *Device;
  D3DKMT_HANDLE RuntimeAllocation;
  ADMISSION_ALLOCATION_OBJECT *Allocation;
  BOOLEAN ReadOnly;
} ADMISSION_OPEN_ALLOCATION;

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
VOID AdmissionUmdRenderTraceArm(_In_ ADMISSION_CONTEXT *Context);
VOID AdmissionUmdRenderTraceDisarm(_In_ ADMISSION_CONTEXT *Context);
VOID AdmissionRecordUmdRenderGuard(_In_opt_ ADMISSION_CONTEXT *Context,
                                   _In_ ULONG Guard,
                                   _In_ NTSTATUS Status);
VOID AdmissionRecordUmdRenderCall(
    _In_opt_ ADMISSION_CONTEXT *Context,
    _In_ const ADMISSION_UMD_RENDER_CALL_RECEIPT *Receipt);
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
  BOOLEAN Initialized;
} ADMISSION_PHYSICAL_OWNER;

typedef struct _ADMISSION_BACKEND_MEMORY_VIEW {
  PVOID CpuAddress;
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
void AdmissionRecordDevice(_In_opt_ PDEVICE_OBJECT DeviceObject,
                           _In_ ADMISSION_RECEIPT Receipt,
                           _In_ NTSTATUS Status);
void AdmissionRecordStartStage(_In_ ADMISSION_CONTEXT *Context,
                               _In_ ADMISSION_START_STAGE Stage,
                               _In_ NTSTATUS Status);
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
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionFlushSourceAddressReceipt(_In_ ADMISSION_CONTEXT *Context);
_IRQL_requires_(PASSIVE_LEVEL)
void AdmissionRecordPresent(_In_opt_ ADMISSION_DEVICE *Device,
                            _In_opt_ const DXGKARG_PRESENT *Present,
                            ULONG Branch, NTSTATUS Status);
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
#define AdmissionGdiReceiptBeginWindows(Context, ContextToken, Opcode, Color, RectCount, DmaBytes) ((void)0)
#define AdmissionGdiReceiptPatchWindows(Context, ContextToken, Fence, DestinationGpuVa, DestinationPhysical, DestinationBytes) ((void)0)
#define AdmissionGdiReceiptSubmitWindows(Context, Args, Status) ((void)0)
#define AdmissionGdiReceiptBackendWindows(Context, Fence, Result, Job) ((void)0)
#define AdmissionGdiReceiptCompleteWindows(Context, Fence, Status, NotifyInterrupt) ((void)0)
#define AdmissionGdiReceiptProgressWindows(Context, Fence, Progress, WorkerFinalPhase) ((void)0)
#define AdmissionGdiReceiptDpcWindows(Context, Fence) ((void)0)
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
NTSTATUS AdmissionMemoryRuntimeExecutePresent(_In_ ADMISSION_CONTEXT *Context,
    _In_reads_bytes_(Bytes) const VOID *Command, UINT Bytes,
    _Out_ ULONGLONG *BytesCopied);
void AdmissionRecordDisplayDdi(_In_opt_ PDEVICE_OBJECT DeviceObject,
                               _In_ ULONG DdiId, _In_ ULONG Phase,
                               _In_ NTSTATUS Status);
void AdmissionRecordMemoryQualification(
    _In_opt_ PDEVICE_OBJECT DeviceObject,
    _In_ const ADMISSION_MEMORY_QUALIFICATION *Qualification);
NTSTATUS AdmissionInterruptStart(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionInterruptStop(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionDdiQuerySegment4(
    _In_ ADMISSION_CONTEXT *Context,
    _In_ const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo);
NTSTATUS AdmissionPhysicalOwnerInitialize(
    _In_ PDXGKRNL_INTERFACE Interface, _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ ADMISSION_PHYSICAL_OWNER *Owner);
NTSTATUS AdmissionPhysicalOwnerDestroy(
    _Inout_ ADMISSION_PHYSICAL_OWNER *Owner);
NTSTATUS AdmissionPhysicalAllocate(
    _Inout_ ADMISSION_PHYSICAL_OWNER *Owner, _In_ SIZE_T Bytes,
    _Outptr_ ADMISSION_PHYSICAL_ALLOCATION **Allocation);
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
NTSTATUS AdmissionMemoryRuntimeBackendView(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ ADMISSION_BACKEND_MEMORY_VIEW *View);
NTSTATUS AdmissionMemoryRuntimeResolveLocal(
    _Inout_ ADMISSION_CONTEXT *Context,
    _In_ ULONGLONG AllocationSegmentAddress,
    _In_ ULONGLONG AllocationSize,
    _In_ ULONGLONG AllocationOffset,
    _Out_ ADMISSION_LOCAL_MEMORY_VIEW *View);
NTSTATUS AdmissionMemoryRuntimeBorrowIo(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ APPLE_AGX_MEMORY_IO *Io);
BOOLEAN AdmissionMemoryRuntimeContextPublished(
    _Inout_ ADMISSION_CONTEXT *Context);
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
BOOLEAN AdmissionPlatformRuntimeReady(
    _Inout_ ADMISSION_CONTEXT *Context);
BOOLEAN AdmissionPlatformRuntimeSubmit(
    _Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionPlatformRuntimeReset(
    _Inout_ ADMISSION_CONTEXT *Context,
    _Out_ APPLE_AGX_U32 *LastAbortedFence);
BOOLEAN AdmissionPlatformRuntimeResponsive(
    _Inout_ ADMISSION_CONTEXT *Context);
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
    _In_reads_bytes_(SourceBytes) const VOID *Source,
    _In_ ULONG SourceBytes, _In_ ULONGLONG SourceGpuAddress,
    _In_ ULONGLONG SourcePhysicalAddress, _In_ ULONG Fence);
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

#endif
