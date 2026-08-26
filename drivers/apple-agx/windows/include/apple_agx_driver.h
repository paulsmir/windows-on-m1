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

#include "apple_agx_state.h"
#include "apple_agx_power.h"
#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION
#include "apple_agx_mapping.h"
#endif

#define APPLE_AGX_POOL_TAG 'xgAA'

typedef struct _APPLE_AGX_ADAPTER {
  PDEVICE_OBJECT PhysicalDeviceObject;
  APPLE_AGX_STATE State;
#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION
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
  AppleAgxStartFailClosed,
} APPLE_AGX_START_STAGE;

#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION
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

NTSTATUS
AppleAgxValidateTranslatedResources(_In_ PCM_RESOURCE_LIST TranslatedResources);
NTSTATUS AppleAgxGetPowerBrokerAddress(
    _In_ PCM_RESOURCE_LIST TranslatedResources,
    _Out_ PPHYSICAL_ADDRESS PowerBrokerAddress);
NTSTATUS AppleAgxQualifyPowerBroker(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _In_ PHYSICAL_ADDRESS PowerBrokerAddress);
#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION
NTSTATUS AppleAgxQualifyMmioMapping(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Out_ APPLE_AGX_MAPPING_STATE *MappingState);
NTSTATUS AppleAgxReleaseMmioMapping(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Inout_ APPLE_AGX_MAPPING_STATE *MappingState);
void AppleAgxRecordMmioQualification(
    _In_ PDEVICE_OBJECT DeviceObject, _In_ APPLE_AGX_MMIO_STAGE Stage,
    _In_ NTSTATUS Status,
    _In_opt_ const APPLE_AGX_MAPPING_STATE *MappingState);
#endif
void AppleAgxLogStartStage(_In_opt_ PDEVICE_OBJECT DeviceObject,
                           _In_ APPLE_AGX_START_STAGE Stage,
                           _In_ NTSTATUS Status);
void AppleAgxRecordDriverEntryBoundary(_In_ PUNICODE_STRING RegistryPath,
                                       _In_ ULONG Stage,
                                       _In_ NTSTATUS Status);
void AppleAgxRecordAddDeviceBoundary(_In_ PDEVICE_OBJECT DeviceObject,
                                     _In_ APPLE_AGX_ADD_STAGE Stage,
                                     _In_ NTSTATUS Status);
void AppleAgxRecordStartDeviceBoundary(_In_ PDEVICE_OBJECT DeviceObject,
                                       _In_ APPLE_AGX_START_STAGE Stage,
                                       _In_ NTSTATUS Status);
void AppleAgxRecordTranslatedResources(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PCM_RESOURCE_LIST TranslatedResources);

#endif /* APPLE_AGX_DRIVER_H */
