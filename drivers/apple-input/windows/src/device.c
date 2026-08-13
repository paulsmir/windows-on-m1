#include "apple_input_device.h"
#include "j313_apple_input.generated.h"

static const ULONGLONG AiExpectedBases[3] = {
    J313_APPLE_INPUT_SPI_BASE,
    J313_APPLE_INPUT_AP_GPIO_BASE,
    J313_APPLE_INPUT_NUB_GPIO_BASE,
};

static const ULONG AiExpectedSizes[3] = {
    (ULONG)J313_APPLE_INPUT_SPI_SIZE,
    (ULONG)J313_APPLE_INPUT_AP_GPIO_SIZE,
    (ULONG)J313_APPLE_INPUT_NUB_GPIO_SIZE,
};

NTSTATUS AppleInputCreateDevice(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS callbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = AppleInputEvtDevicePrepareHardware;
    callbacks.EvtDeviceReleaseHardware = AppleInputEvtDeviceReleaseHardware;
    callbacks.EvtDeviceD0Entry = AppleInputEvtDeviceD0Entry;
    callbacks.EvtDeviceD0Exit = AppleInputEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &callbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, AI_DEVICE_CONTEXT);
    return WdfDeviceCreate(&DeviceInit, &attributes, &device);
}

NTSTATUS AiDeviceParseResources(WDFCMRESLIST Raw, WDFCMRESLIST Translated,
                                PAI_DEVICE_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Raw);
    ULONG memory = 0;
    ULONG interrupts = 0;

    RtlZeroMemory(Context, sizeof(*Context));
    for (ULONG index = 0; index < WdfCmResourceListGetCount(Translated); index++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR resource =
            WdfCmResourceListGetDescriptor(Translated, index);
        if (resource == NULL)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (resource->Type == CmResourceTypeMemory) {
            if (memory >= (ULONG)RTL_NUMBER_OF(AiExpectedBases) ||
                resource->u.Memory.Start.QuadPart != AiExpectedBases[memory] ||
                resource->u.Memory.Length != AiExpectedSizes[memory])
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            Context->MemoryBase[memory] = resource->u.Memory.Start;
            Context->MemoryLength[memory++] = resource->u.Memory.Length;
        } else if (resource->Type == CmResourceTypeInterrupt) {
            if (interrupts++ != 0 ||
                resource->u.Interrupt.Vector != (ULONG)J313_APPLE_INPUT_GUEST_VINTID)
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            Context->InterruptVector = resource->u.Interrupt.Vector;
        }
    }
    if (memory != 3 || interrupts != 1)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Context->ResourcesValidated = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS AppleInputEvtDevicePrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw,
                                             WDFCMRESLIST Translated)
{
    PAI_DEVICE_CONTEXT context = AiGetDeviceContext(Device);
    NTSTATUS status = AiDeviceParseResources(Raw, Translated, context);
    if (!NT_SUCCESS(status))
        return status;

    context->SpiRegisters = MmMapIoSpaceEx(context->MemoryBase[0],
        context->MemoryLength[0], PAGE_READWRITE | PAGE_NOCACHE);
    context->ApGpioRegisters = MmMapIoSpaceEx(context->MemoryBase[1],
        context->MemoryLength[1], PAGE_READWRITE | PAGE_NOCACHE);
    context->NubGpioRegisters = MmMapIoSpaceEx(context->MemoryBase[2],
        context->MemoryLength[2], PAGE_READWRITE | PAGE_NOCACHE);
    if (!context->SpiRegisters || !context->ApGpioRegisters || !context->NubGpioRegisters) {
        AppleInputEvtDeviceReleaseHardware(Device, Translated);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = AiSpiValidateReadOnly(context);
    if (NT_SUCCESS(status))
        status = AiGpioValidateReadOnly(context);
    if (!NT_SUCCESS(status))
        AppleInputEvtDeviceReleaseHardware(Device, Translated);
    return status;
}

NTSTATUS AppleInputEvtDeviceReleaseHardware(WDFDEVICE Device,
                                             WDFCMRESLIST Translated)
{
    UNREFERENCED_PARAMETER(Translated);
    PAI_DEVICE_CONTEXT context = AiGetDeviceContext(Device);
    if (context->NubGpioRegisters)
        MmUnmapIoSpace(context->NubGpioRegisters, context->MemoryLength[2]);
    if (context->ApGpioRegisters)
        MmUnmapIoSpace(context->ApGpioRegisters, context->MemoryLength[1]);
    if (context->SpiRegisters)
        MmUnmapIoSpace(context->SpiRegisters, context->MemoryLength[0]);
    context->SpiRegisters = NULL;
    context->ApGpioRegisters = NULL;
    context->NubGpioRegisters = NULL;
    context->ResourcesValidated = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS AppleInputEvtDeviceD0Entry(WDFDEVICE Device,
                                    WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);
    return AiGetDeviceContext(Device)->ResourcesValidated
               ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}

NTSTATUS AppleInputEvtDeviceD0Exit(WDFDEVICE Device,
                                   WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(TargetState);
    return STATUS_SUCCESS;
}
