#ifndef APPLE_AGX_DRIVER_H
#define APPLE_AGX_DRIVER_H

/* The WDK display headers require the base NT, Win32 and GDI types first. */
// clang-format off
#include <ntddk.h>
#include <windef.h>
#include <winerror.h>
#include <wingdi.h>
#include <ntddvdeo.h>
#include <d3dkmddi.h>
#include <d3dkmthk.h>
#include <dispmprt.h>
// clang-format on

#include "apple_agx_memory.h"
#include "apple_agx_power.h"
#include "apple_agx_state.h"
#include "apple_agx_uat_publication.h"
#if defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                           \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
#include "apple_agx_asc_transport.h"
#include "apple_agx_rtkit_session.h"
#endif
#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                            \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
#include "apple_agx_mapping.h"
#endif

#if defined(APPLE_AGX_G2_POWER_QUALIFICATION) ||                               \
    defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_LIFECYCLE_QUALIFICATION) ||                           \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                            \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
#define APPLE_AGX_G2_QUALIFICATION_DIAGNOSTICS 1
#endif

#define APPLE_AGX_POOL_TAG 'xgAA'

typedef struct _APPLE_AGX_WINDOWS_MEMORY_ALLOCATOR {
  PDXGKRNL_INTERFACE Interface;
} APPLE_AGX_WINDOWS_MEMORY_ALLOCATOR;

typedef struct _APPLE_AGX_WINDOWS_UAT_PUBLICATION {
  PDXGKRNL_INTERFACE Interface;
  NTSTATUS LastStatus;
} APPLE_AGX_WINDOWS_UAT_PUBLICATION;

#if defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                           \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
typedef struct _APPLE_AGX_WINDOWS_ASC_TRANSPORT {
  volatile UCHAR *Base;
  ULONG Length;
} APPLE_AGX_WINDOWS_ASC_TRANSPORT;
#endif

#if defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
typedef struct _APPLE_AGX_POWER_SESSION {
  volatile UCHAR *Base;
  BOOLEAN Powered;
} APPLE_AGX_POWER_SESSION;

typedef enum _APPLE_AGX_POWER_RECEIPT {
  AppleAgxPowerReceiptAcquired = 1,
  AppleAgxPowerReceiptReleased,
} APPLE_AGX_POWER_RECEIPT;
#endif

#ifdef APPLE_AGX_G2_RTKIT_QUALIFICATION
#define APPLE_AGX_RTKIT_BOOT_FLAG_BEGUN (1u << 0)
#define APPLE_AGX_RTKIT_BOOT_FLAG_HELLO_SEEN (1u << 1)
#define APPLE_AGX_RTKIT_BOOT_FLAG_ENDPOINT_MAP_COMPLETE (1u << 2)
#define APPLE_AGX_RTKIT_BOOT_FLAG_IOP_POWER_READY (1u << 3)
#define APPLE_AGX_RTKIT_BOOT_FLAG_AP_POWER_REQUESTED (1u << 4)
#define APPLE_AGX_RTKIT_BOOT_FLAG_AP_POWER_READY (1u << 5)
#define APPLE_AGX_RTKIT_BOOT_FLAG_RUNNING (1u << 6)
#define APPLE_AGX_RTKIT_BOOT_FLAG_CPU_READY (1u << 7)

typedef struct _APPLE_AGX_RTKIT_QUALIFICATION_RESULT {
  NTSTATUS BootStatus;
  NTSTATUS StopStatus;
  ULONG BootPhase;
  ULONG BootFlags;
  ULONG NegotiatedVersion;
  NTSTATUS FinalCpuStatusReadStatus;
  ULONG FinalCpuStatus;
} APPLE_AGX_RTKIT_QUALIFICATION_RESULT;
#endif

typedef struct _APPLE_AGX_ADAPTER {
  PDEVICE_OBJECT PhysicalDeviceObject;
  APPLE_AGX_STATE State;
#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                            \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
  APPLE_AGX_MAPPING_STATE MappingState;
  DXGKRNL_INTERFACE DxgkInterface;
  BOOLEAN DxgkInterfaceValid;
#endif

} APPLE_AGX_ADAPTER;

typedef enum _APPLE_AGX_ADD_STAGE {
  AppleAgxAddEntered = 1,
  AppleAgxAddReturned,
} APPLE_AGX_ADD_STAGE;

typedef enum _APPLE_AGX_START_STAGE {
  AppleAgxStartEntered = 1,
  AppleAgxStartDeviceInformation,
  AppleAgxStartResourcesValidated,
  AppleAgxStartStateValidated,
  AppleAgxStartBrokerAddress,
  AppleAgxStartBrokerTransaction,
  AppleAgxStartPowerAcquired,
  AppleAgxStartPowerReleased,
  AppleAgxStartFailClosed,
} APPLE_AGX_START_STAGE;

#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                               \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION)
typedef enum _APPLE_AGX_MMIO_STAGE {
  AppleAgxMmioMapped = 1,
  AppleAgxMmioSubviewValidated,
  AppleAgxMmioUnmapped,
} APPLE_AGX_MMIO_STAGE;
#endif

DXGKDDI_ADD_DEVICE AppleAgxDdiAddDevice;
DXGKDDI_START_DEVICE AppleAgxDdiStartDevice;
DXGKDDI_STOP_DEVICE AppleAgxDdiStopDevice;
DXGKDDI_REMOVE_DEVICE AppleAgxDdiRemoveDevice;
DXGKDDI_DISPATCH_IO_REQUEST AppleAgxDdiDispatchIoRequest;
DXGKDDI_INTERRUPT_ROUTINE AppleAgxDdiInterruptRoutine;
DXGKDDI_DPC_ROUTINE AppleAgxDdiDpcRoutine;
DXGKDDI_QUERY_CHILD_RELATIONS AppleAgxDdiQueryChildRelations;
DXGKDDI_QUERY_CHILD_STATUS AppleAgxDdiQueryChildStatus;
DXGKDDI_QUERY_DEVICE_DESCRIPTOR AppleAgxDdiQueryDeviceDescriptor;
DXGKDDI_SET_POWER_STATE AppleAgxDdiSetPowerState;
DXGKDDI_RESET_DEVICE AppleAgxDdiResetDevice;
DXGKDDI_UNLOAD AppleAgxDdiUnload;
DXGKDDI_QUERYADAPTERINFO AppleAgxDdiQueryAdapterInfo;

/*
 * A full WDDM miniport must publish the render-only callback contract before
 * dxgkrnl will start the adapter.  These entry points intentionally remain
 * fail-closed: they validate no hardware, create no GPU objects, touch no
 * registers, and return STATUS_NOT_SUPPORTED until their individual contracts
 * are implemented and qualified.
 */
DXGKDDI_NOTIFY_ACPI_EVENT AppleAgxDdiNotifyAcpiEvent;
DXGKDDI_QUERY_INTERFACE AppleAgxDdiQueryInterface;
VOID AppleAgxDdiControlEtwLogging(_In_ BOOLEAN Enable, _In_ ULONG Flags,
                                  _In_ UCHAR Level);
DXGKDDI_CREATEDEVICE AppleAgxDdiCreateDevice;
DXGKDDI_DESTROYDEVICE AppleAgxDdiDestroyDevice;
DXGKDDI_CREATEALLOCATION AppleAgxDdiCreateAllocation;
DXGKDDI_DESTROYALLOCATION AppleAgxDdiDestroyAllocation;
DXGKDDI_DESCRIBEALLOCATION AppleAgxDdiDescribeAllocation;
DXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA
AppleAgxDdiGetStandardAllocationDriverData;
DXGKDDI_OPENALLOCATIONINFO AppleAgxDdiOpenAllocation;
DXGKDDI_CLOSEALLOCATION AppleAgxDdiCloseAllocation;
DXGKDDI_PATCH AppleAgxDdiPatch;
DXGKDDI_SUBMITCOMMAND AppleAgxDdiSubmitCommand;
DXGKDDI_BUILDPAGINGBUFFER AppleAgxDdiBuildPagingBuffer;
DXGKDDI_PREEMPTCOMMAND AppleAgxDdiPreemptCommand;
DXGKDDI_RENDER AppleAgxDdiRender;
DXGKDDI_PRESENT AppleAgxDdiPresent;
DXGKDDI_RESETFROMTIMEOUT AppleAgxDdiResetFromTimeout;
DXGKDDI_RESTARTFROMTIMEOUT AppleAgxDdiRestartFromTimeout;
DXGKDDI_ESCAPE AppleAgxDdiEscape;
DXGKDDI_COLLECTDBGINFO AppleAgxDdiCollectDbgInfo;
DXGKDDI_QUERYCURRENTFENCE AppleAgxDdiQueryCurrentFence;
DXGKDDI_CONTROLINTERRUPT AppleAgxDdiControlInterrupt;
DXGKDDI_CREATECONTEXT AppleAgxDdiCreateContext;
DXGKDDI_DESTROYCONTEXT AppleAgxDdiDestroyContext;
DXGKDDI_RENDERKM AppleAgxDdiRenderKm;
DXGKDDI_QUERYDEPENDENTENGINEGROUP AppleAgxDdiQueryDependentEngineGroup;
DXGKDDI_QUERYENGINESTATUS AppleAgxDdiQueryEngineStatus;
DXGKDDI_RESETENGINE AppleAgxDdiResetEngine;
DXGKDDI_CANCELCOMMAND AppleAgxDdiCancelCommand;
DXGKDDISETPOWERCOMPONENTFSTATE AppleAgxDdiSetPowerComponentFState;
DXGKDDIPOWERRUNTIMECONTROLREQUEST AppleAgxDdiPowerRuntimeControlRequest;
DXGKDDI_GETNODEMETADATA AppleAgxDdiGetNodeMetadata;
DXGKDDI_SUBMITCOMMANDVIRTUAL AppleAgxDdiSubmitCommandVirtual;
DXGKDDI_CREATEPROCESS AppleAgxDdiCreateProcess;
DXGKDDI_DESTROYPROCESS AppleAgxDdiDestroyProcess;
DXGKDDI_CALIBRATEGPUCLOCK AppleAgxDdiCalibrateGpuClock;
DXGKDDI_SETSTABLEPOWERSTATE AppleAgxDdiSetStablePowerState;

NTSTATUS
AppleAgxValidateTranslatedResources(_In_ PCM_RESOURCE_LIST TranslatedResources);
NTSTATUS
AppleAgxGetPowerBrokerAddress(_In_ PCM_RESOURCE_LIST TranslatedResources,
                              _Out_ PPHYSICAL_ADDRESS PowerBrokerAddress);
NTSTATUS AppleAgxQualifyPowerBroker(_In_ PDXGKRNL_INTERFACE DxgkInterface,
                                    _In_ PHYSICAL_ADDRESS PowerBrokerAddress);
NTSTATUS AppleAgxWindowsMemoryInitialize(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Out_ APPLE_AGX_WINDOWS_MEMORY_ALLOCATOR *Allocator,
    _Out_ APPLE_AGX_MEMORY_IO *Io);
NTSTATUS AppleAgxWindowsUatPublicationInitialize(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Out_ APPLE_AGX_WINDOWS_UAT_PUBLICATION *Publication,
    _Out_ APPLE_AGX_UAT_PUBLICATION_IO *Io);
#ifdef APPLE_AGX_G2_RTKIT_QUALIFICATION
NTSTATUS AppleAgxQualifyRtkitReadyStop(
    _In_reads_bytes_(AscLength) volatile UCHAR *AscBase, _In_ ULONG AscLength,
    _Out_ APPLE_AGX_RTKIT_QUALIFICATION_RESULT *Result);
void AppleAgxRecordRtkitQualification(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ const APPLE_AGX_RTKIT_QUALIFICATION_RESULT *Result);
#endif
#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                            \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                     \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
NTSTATUS
AppleAgxQualifyMmioMapping(_In_ PDXGKRNL_INTERFACE DxgkInterface,
                           _Out_ APPLE_AGX_MAPPING_STATE *MappingState);
NTSTATUS
AppleAgxReleaseMmioMapping(_In_ PDXGKRNL_INTERFACE DxgkInterface,
                           _Inout_ APPLE_AGX_MAPPING_STATE *MappingState);
#endif
#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                               \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION)
void AppleAgxRecordMmioQualification(
    _In_ PDEVICE_OBJECT DeviceObject, _In_ APPLE_AGX_MMIO_STAGE Stage,
    _In_ NTSTATUS Status, _In_opt_ const APPLE_AGX_MAPPING_STATE *MappingState);
#endif
void AppleAgxLogStartStage(_In_opt_ PDEVICE_OBJECT DeviceObject,
                           _In_ APPLE_AGX_START_STAGE Stage,
                           _In_ NTSTATUS Status);
void AppleAgxRecordDriverEntryBoundary(_In_ PUNICODE_STRING RegistryPath,
                                       _In_ ULONG Stage, _In_ NTSTATUS Status);
void AppleAgxRecordAddDeviceBoundary(_In_ PDEVICE_OBJECT DeviceObject,
                                     _In_ APPLE_AGX_ADD_STAGE Stage,
                                     _In_ NTSTATUS Status);
void AppleAgxRecordStartDeviceBoundary(_In_ PDEVICE_OBJECT DeviceObject,
                                       _In_ APPLE_AGX_START_STAGE Stage,
                                       _In_ NTSTATUS Status);
void AppleAgxRecordTranslatedResources(_In_ PDEVICE_OBJECT DeviceObject,
                                       _In_opt_ PCM_RESOURCE_LIST
                                           TranslatedResources);

#if defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION) ||                           \
    defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION)
NTSTATUS AppleAgxFirmwareTransportInitialize(
    _In_reads_bytes_(AscLength) volatile UCHAR *AscBase, _In_ ULONG AscLength,
    _Out_ APPLE_AGX_WINDOWS_ASC_TRANSPORT *Transport,
    _Out_ APPLE_AGX_ASC_IO *Io);
NTSTATUS
AppleAgxQualifyAscCpuStatus(_In_reads_bytes_(AscLength) volatile UCHAR *AscBase,
                            _In_ ULONG AscLength, _Out_ PULONG CpuStatus);
void AppleAgxRecordAscCpuStatus(_In_ PDEVICE_OBJECT DeviceObject,
                                _In_ NTSTATUS Status, _In_ ULONG CpuStatus);
#endif

#if defined(APPLE_AGX_G2_POWERED_STATUS_QUALIFICATION) ||                    \
    defined(APPLE_AGX_G2_RTKIT_QUALIFICATION)
NTSTATUS AppleAgxPowerSessionBegin(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _In_ PHYSICAL_ADDRESS PowerBrokerAddress,
    _Out_ APPLE_AGX_POWER_SESSION *Session);
NTSTATUS AppleAgxPowerSessionEnd(_In_ PDXGKRNL_INTERFACE DxgkInterface,
                                 _Inout_ APPLE_AGX_POWER_SESSION *Session);
void AppleAgxRecordPowerSession(_In_ PDEVICE_OBJECT DeviceObject,
                                _In_ APPLE_AGX_POWER_RECEIPT Receipt,
                                _In_ NTSTATUS Status);
#endif

#endif /* APPLE_AGX_DRIVER_H */
