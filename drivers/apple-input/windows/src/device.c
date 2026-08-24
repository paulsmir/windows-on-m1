#include "apple_input_device.h"
#include "j313_apple_input.generated.h"

static const LONGLONG AiExpectedBases[3] = {
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
    WDF_INTERRUPT_CONFIG interrupt_config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    PAI_DEVICE_CONTEXT context;
    NTSTATUS status;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&callbacks);
    callbacks.EvtDevicePrepareHardware = AppleInputEvtDevicePrepareHardware;
    callbacks.EvtDeviceReleaseHardware = AppleInputEvtDeviceReleaseHardware;
    callbacks.EvtDeviceD0Entry = AppleInputEvtDeviceD0Entry;
    callbacks.EvtDeviceD0EntryPostInterruptsEnabled =
        AppleInputEvtDeviceD0EntryPostInterruptsEnabled;
    callbacks.EvtDeviceD0ExitPreInterruptsDisabled =
        AppleInputEvtDeviceD0ExitPreInterruptsDisabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &callbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, AI_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
        return status;

    context = AiGetDeviceContext(device);
    context->TransportOnly = TRUE;
    context->Diagnostics.Version = AI_DIAGNOSTIC_SNAPSHOT_VERSION_3;
    context->Diagnostics.Size = sizeof(context->Diagnostics);

    WDF_INTERRUPT_CONFIG_INIT(&interrupt_config, AiInputInterruptIsr, NULL);
    interrupt_config.EvtInterruptWorkItem = AiTransportWorker;
    interrupt_config.PassiveHandling = TRUE;
    interrupt_config.AutomaticSerialization = FALSE;
    status = WdfInterruptCreate(device, &interrupt_config,
                                WDF_NO_OBJECT_ATTRIBUTES,
                                &context->Interrupt);
    if (!NT_SUCCESS(status))
        return status;
    return AiDiagnosticsInitialize(device, context);
}

NTSTATUS AiDeviceParseResources(WDFCMRESLIST Raw, WDFCMRESLIST Translated,
                                PAI_DEVICE_CONTEXT Context)
{
    ULONG memory = 0;
    ULONG interrupts = 0;
    ULONG raw_count = WdfCmResourceListGetCount(Raw);
    ULONG translated_count = WdfCmResourceListGetCount(Translated);

    RtlZeroMemory(Context->MemoryBase, sizeof(Context->MemoryBase));
    RtlZeroMemory(Context->MemoryLength, sizeof(Context->MemoryLength));
    Context->InterruptVector = 0;
    Context->ResourcesValidated = FALSE;
    if (raw_count != translated_count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    for (ULONG index = 0; index < translated_count; index++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR raw_resource =
            WdfCmResourceListGetDescriptor(Raw, index);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR translated_resource =
            WdfCmResourceListGetDescriptor(Translated, index);
        if (raw_resource == NULL || translated_resource == NULL ||
            raw_resource->Type != translated_resource->Type)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (translated_resource->Type == CmResourceTypeMemory) {
            if (memory >= (ULONG)RTL_NUMBER_OF(AiExpectedBases) ||
                raw_resource->u.Memory.Start.QuadPart != AiExpectedBases[memory] ||
                raw_resource->u.Memory.Length != AiExpectedSizes[memory] ||
                translated_resource->u.Memory.Length != AiExpectedSizes[memory])
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            Context->MemoryBase[memory] = translated_resource->u.Memory.Start;
            Context->MemoryLength[memory++] = translated_resource->u.Memory.Length;
        } else if (translated_resource->Type == CmResourceTypeInterrupt) {
            if (interrupts++ != 0 ||
                raw_resource->u.Interrupt.Vector !=
                    (ULONG)J313_APPLE_INPUT_GUEST_VINTID)
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            Context->InterruptVector = translated_resource->u.Interrupt.Vector;
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
    AiTransportStop(context);
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
    PAI_DEVICE_CONTEXT context = AiGetDeviceContext(Device);
    if (!context->ResourcesValidated)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

NTSTATUS AppleInputEvtDeviceD0EntryPostInterruptsEnabled(
    WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);
    return AiTransportStart(AiGetDeviceContext(Device));
}

NTSTATUS AppleInputEvtDeviceD0ExitPreInterruptsDisabled(
    WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
    UNREFERENCED_PARAMETER(TargetState);
    AiTransportStop(AiGetDeviceContext(Device));
    return STATUS_SUCCESS;
}
