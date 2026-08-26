#ifndef APPLE_AGX_DRIVER_H
#define APPLE_AGX_DRIVER_H

#include <d3dkmddi.h>
#include <dispmprt.h>
#include <ntddk.h>

#include "apple_agx_state.h"

#define APPLE_AGX_POOL_TAG 'xgAA'

typedef struct _APPLE_AGX_ADAPTER {
  PDEVICE_OBJECT PhysicalDeviceObject;
  APPLE_AGX_STATE State;
} APPLE_AGX_ADAPTER;

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

#endif /* APPLE_AGX_DRIVER_H */
