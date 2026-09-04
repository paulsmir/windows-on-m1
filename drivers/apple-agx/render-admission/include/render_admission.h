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
#include "render_submission.h"
#include "render_backend_image.h"
#include "apple_agx_wddm_feature_contract.h"
#include "apple_agx_scheduler.h"
#include "apple_agx_platform_provider.h"
#include "apple_agx_firmware_provider.h"
#include "apple_agx_device_control.h"
#include "apple_agx_initdata_memory.h"
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
  UINT PagingFence;
  UINT PagingLastSubmittedFence;
  UINT PagingLastCompletedFence;
  NTSTATUS PagingCompletionStatus;
  volatile LONG PagingPending;
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
  volatile LONG FeatureReadyMask;
} ADMISSION_CONTEXT;

typedef struct _ADMISSION_DEVICE {
  ADMISSION_OBJECT_DEVICE Object;
} ADMISSION_DEVICE;

typedef struct _ADMISSION_RENDER_CONTEXT {
  ADMISSION_OBJECT_CONTEXT Object;
  APPLE_AGX_SCHEDULER_CONTEXT SchedulerContext;
} ADMISSION_RENDER_CONTEXT;

typedef struct _ADMISSION_ALLOCATION_HANDLE {
  ADMISSION_ALLOCATION_OBJECT Object;
} ADMISSION_ALLOCATION_HANDLE;

#define ADMISSION_OPEN_ALLOCATION_MAGIC 0x4f504152u
typedef struct _ADMISSION_OPEN_ALLOCATION {
  ULONG Magic;
  ADMISSION_DEVICE *Device;
  D3DKMT_HANDLE RuntimeAllocation;
  ADMISSION_ALLOCATION_OBJECT *Allocation;
  BOOLEAN ReadOnly;
} ADMISSION_OPEN_ALLOCATION;

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
void AdmissionRecordQuery(_In_opt_ PDEVICE_OBJECT DeviceObject,
                          _In_ DXGK_QUERYADAPTERINFOTYPE Type,
                          _In_ ULONG OutputDataSize, _In_ NTSTATUS Status);
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
    _In_ ULONGLONG DummyPage);
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
DXGKDDI_RECOMMENDFUNCTIONALVIDPN AdmissionDdiRecommendFunctionalVidPn;
DXGKDDI_ENUMVIDPNCOFUNCMODALITY AdmissionDdiEnumVidPnCofuncModality;
DXGKDDI_SETVIDPNSOURCEVISIBILITY AdmissionDdiSetVidPnSourceVisibility;
DXGKDDI_COMMITVIDPN AdmissionDdiCommitVidPn;
DXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH AdmissionDdiUpdateActiveVidPnPresentPath;
DXGKDDI_RECOMMENDMONITORMODES AdmissionDdiRecommendMonitorModes;
DXGKDDI_GETSCANLINE AdmissionDdiGetScanLine;
DXGKDDI_QUERYVIDPNHWCAPABILITY AdmissionDdiQueryVidPnHWCapability;
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
