#pragma once

#include <ntddk.h>
#include <wdf.h>

typedef struct _AI_DEVICE_CONTEXT {
    PHYSICAL_ADDRESS MemoryBase[3];
    ULONG MemoryLength[3];
    ULONG InterruptVector;
    PUCHAR SpiRegisters;
    PUCHAR ApGpioRegisters;
    PUCHAR NubGpioRegisters;
    BOOLEAN ResourcesValidated;
} AI_DEVICE_CONTEXT, *PAI_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AI_DEVICE_CONTEXT, AiGetDeviceContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AppleInputEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE AppleInputEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE AppleInputEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY AppleInputEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT AppleInputEvtDeviceD0Exit;

NTSTATUS AppleInputCreateDevice(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit);
NTSTATUS AiDeviceParseResources(WDFCMRESLIST Raw, WDFCMRESLIST Translated,
                                PAI_DEVICE_CONTEXT Context);
NTSTATUS AiSpiValidateReadOnly(PAI_DEVICE_CONTEXT Context);
NTSTATUS AiGpioValidateReadOnly(PAI_DEVICE_CONTEXT Context);
