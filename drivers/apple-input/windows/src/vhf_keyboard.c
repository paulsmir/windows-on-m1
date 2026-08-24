#include "apple_input_device.h"

NTSTATUS AiKeyboardVhfStart(PAI_DEVICE_CONTEXT Context)
{
    const struct ai_descriptor_slot *descriptor;
    VHF_CONFIG config;
    VHFHANDLE handle = NULL;
    NTSTATUS status;

    if (!Context || !Context->Device)
        return STATUS_INVALID_PARAMETER;
    descriptor = ai_descriptor_store_get(&Context->Descriptors, 1);
    if (!descriptor || !Context->KeyboardInputContract.valid)
        return STATUS_DEVICE_NOT_READY;
    if (Context->KeyboardVhf)
        return STATUS_SUCCESS;

    VHF_CONFIG_INIT(&config,
        WdfDeviceWdmGetDeviceObject(Context->Device),
        descriptor->length, (PUCHAR)descriptor->bytes);
    config.VhfClientContext = Context;
    status = VhfCreate(&config, &handle);
    if (!NT_SUCCESS(status))
        return status;
    Context->KeyboardVhf = handle;
    status = VhfStart(handle);
    if (!NT_SUCCESS(status)) {
        Context->KeyboardVhf = NULL;
        VhfDelete(handle, TRUE);
    }
    return status;
}

NTSTATUS AiKeyboardVhfSubmit(PAI_DEVICE_CONTEXT Context,
                             const UCHAR *Report, SIZE_T Length)
{
    HID_XFER_PACKET packet;
    uint8_t report_id;

    if (!Context || !Context->KeyboardVhf)
        return STATUS_DEVICE_NOT_READY;
    if (!ai_hid_input_report_valid(&Context->KeyboardInputContract,
                                   Report, Length, &report_id))
        return STATUS_INVALID_BUFFER_SIZE;
    RtlZeroMemory(&packet, sizeof(packet));
    packet.reportBuffer = (PUCHAR)Report;
    packet.reportBufferLen = (ULONG)Length;
    packet.reportId = report_id;
    return VhfReadReportSubmit(Context->KeyboardVhf, &packet);
}

VOID AiKeyboardVhfStop(PAI_DEVICE_CONTEXT Context)
{
    VHFHANDLE handle;

    if (!Context)
        return;
    handle = Context->KeyboardVhf;
    Context->KeyboardVhf = NULL;
    if (handle)
        VhfDelete(handle, TRUE);
}
