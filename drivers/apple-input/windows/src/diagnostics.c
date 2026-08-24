#include <initguid.h>

#include "apple_input_device.h"

AI_DIAGNOSTIC_SNAPSHOT_V2 g_AiDiagnosticSnapshot;

NTSTATUS AiDiagnosticsInitialize(WDFDEVICE Device, PAI_DEVICE_CONTEXT Context)
{
    WDF_IO_QUEUE_CONFIG queue_config;
    NTSTATUS status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue_config,
                                            WdfIoQueueDispatchParallel);
    queue_config.EvtIoDeviceControl = AiDiagnosticsEvtIoDeviceControl;
    status = WdfIoQueueCreate(Device, &queue_config,
                              WDF_NO_OBJECT_ATTRIBUTES,
                              &Context->DiagnosticQueue);
    if (!NT_SUCCESS(status))
        return status;
    return WdfDeviceCreateDeviceInterface(
        Device, &GUID_DEVINTERFACE_APPLE_INPUT_DIAGNOSTIC, NULL);
}

VOID AiDiagnosticsRecordHeader(PAI_DEVICE_CONTEXT Context,
                               const struct ai_packet_view *Packet,
                               enum ai_status Result)
{
    ULONG index;
    PAI_PACKET_HEADER_V1 header;

    if (!Context || !Packet)
        return;
    index = Context->Diagnostics.HeaderWriteIndex++;
    header = &Context->Diagnostics.Headers[
        index % AI_PACKET_HEADER_RING_CAPACITY];
    header->Sequence = index;
    header->Result = (ULONG)Result;
    header->Offset = Packet->offset;
    header->Remaining = Packet->remaining;
    header->Length = Packet->length;
    header->Flags = Packet->flags;
    header->Device = Packet->device;
}

VOID AiDiagnosticsRecordMessage(PAI_DEVICE_CONTEXT Context,
                                const struct ai_protocol_message *Message)
{
    if (!Context || !Message)
        return;
    Context->Diagnostics.MessagePhase = (ULONG)Context->Discovery.phase;
    Context->Diagnostics.MessageType = Message->type;
    Context->Diagnostics.MessageReport = Message->report;
    Context->Diagnostics.MessageDevice = Message->device;
    Context->Diagnostics.MessageId = Message->id;
    Context->Diagnostics.MessageResponseLength = Message->response_length;
    Context->Diagnostics.MessagePayloadLength = Message->payload_length;
}

VOID AiDiagnosticsPublish(PAI_DEVICE_CONTEXT Context)
{
    if (!Context)
        return;
    Context->Diagnostics.TransportPhase = (ULONG)Context->Discovery.phase;
    RtlCopyMemory(&g_AiDiagnosticSnapshot, &Context->Diagnostics,
                  sizeof(g_AiDiagnosticSnapshot));
}

VOID AiDiagnosticsEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request,
                                     SIZE_T OutputBufferLength,
                                     SIZE_T InputBufferLength,
                                     ULONG IoControlCode)
{
    PAI_DIAGNOSTIC_SNAPSHOT_V2 output = NULL;
    PAI_DEVICE_CONTEXT context;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    if (IoControlCode != IOCTL_AI_GET_SNAPSHOT) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    context = AiGetDeviceContext(WdfIoQueueGetDevice(Queue));
    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(AI_DIAGNOSTIC_SNAPSHOT_V2),
        (PVOID *)&output, NULL);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_BUFFER_TOO_SMALL)
            WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        else
            WdfRequestComplete(Request, status);
        return;
    }

    AiDiagnosticsPublish(context);
    RtlCopyMemory(output, &context->Diagnostics, sizeof(*output));
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                      sizeof(*output));
}
