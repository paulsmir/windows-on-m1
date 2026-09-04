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

#define ADMISSION_POOL_TAG 'mRGA'
#define ADMISSION_DMA_BUFFER_SIZE 4096u
#define ADMISSION_ALLOCATION_LIST_SIZE 64u
#define ADMISSION_PATCH_LIST_SIZE 64u
#define ADMISSION_GDI_DMA_PRIVATE_SIZE 8192u
#define ADMISSION_GDI_ALLOCATION_LIST_SIZE 256u
#define ADMISSION_GDI_PATCH_LIST_SIZE 256u

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
} ADMISSION_RECEIPT;

typedef struct _ADMISSION_CONTEXT {
  ADMISSION_OBJECT_ADAPTER ObjectAdapter;
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
} ADMISSION_CONTEXT;

typedef struct _ADMISSION_DEVICE {
  ADMISSION_OBJECT_DEVICE Object;
} ADMISSION_DEVICE;

typedef struct _ADMISSION_RENDER_CONTEXT {
  ADMISSION_OBJECT_CONTEXT Object;
} ADMISSION_RENDER_CONTEXT;

void AdmissionRecordService(_In_ PUNICODE_STRING RegistryPath,
                            _In_ PCWSTR Name, _In_ ULONG Value);
void AdmissionRecordDevice(_In_opt_ PDEVICE_OBJECT DeviceObject,
                           _In_ ADMISSION_RECEIPT Receipt,
                           _In_ NTSTATUS Status);
void AdmissionRecordQuery(_In_opt_ PDEVICE_OBJECT DeviceObject,
                          _In_ DXGK_QUERYADAPTERINFOTYPE Type,
                          _In_ ULONG OutputDataSize, _In_ NTSTATUS Status);
NTSTATUS AdmissionInterruptStart(_Inout_ ADMISSION_CONTEXT *Context);
NTSTATUS AdmissionInterruptStop(_Inout_ ADMISSION_CONTEXT *Context);

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
