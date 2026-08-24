#include <initguid.h>
#include <bcrypt.h>

#include "apple_input_device.h"

AI_DIAGNOSTIC_SNAPSHOT_V3 g_AiDiagnosticSnapshot;

static NTSTATUS AiSha256(const UCHAR *Bytes, ULONG Length,
                         UCHAR Digest[AI_SHA256_DIGEST_SIZE])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    NTSTATUS status;

    RtlZeroMemory(Digest, AI_SHA256_DIGEST_SIZE);
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                         NULL, 0);
    if (!NT_SUCCESS(status))
        return status;
    status = BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0);
    if (NT_SUCCESS(status))
        status = BCryptHashData(hash, (PUCHAR)Bytes, Length, 0);
    if (NT_SUCCESS(status))
        status = BCryptFinishHash(hash, Digest, AI_SHA256_DIGEST_SIZE, 0);
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!NT_SUCCESS(status))
        RtlZeroMemory(Digest, AI_SHA256_DIGEST_SIZE);
    return status;
}

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

VOID AiDiagnosticsRecordDescriptor(
    PAI_DEVICE_CONTEXT Context, const struct ai_descriptor_slot *Descriptor)
{
    UCHAR *digest;
    USHORT *length;
    NTSTATUS status;

    if (!Context || !Descriptor || !Descriptor->valid)
        return;
    if (Descriptor->device == 1) {
        length = &Context->Diagnostics.KeyboardDescriptorLength;
        digest = Context->Diagnostics.KeyboardDescriptorSha256;
    } else if (Descriptor->device == 2) {
        length = &Context->Diagnostics.TrackpadDescriptorLength;
        digest = Context->Diagnostics.TrackpadDescriptorSha256;
    } else {
        return;
    }

    *length = Descriptor->length;
    status = AiSha256(Descriptor->bytes, Descriptor->length, digest);
    if (!NT_SUCCESS(status) &&
        Context->Diagnostics.DescriptorDigestStatus == 0)
        Context->Diagnostics.DescriptorDigestStatus = (ULONG)status;
}

VOID AiDiagnosticsPublish(PAI_DEVICE_CONTEXT Context)
{
    if (!Context)
        return;
    Context->Diagnostics.TransportPhase = (ULONG)Context->Discovery.phase;
    Context->Diagnostics.KeyboardVhfState =
        (ULONG)Context->KeyboardVhfState;
    RtlCopyMemory(&g_AiDiagnosticSnapshot, &Context->Diagnostics,
                  sizeof(g_AiDiagnosticSnapshot));
}

VOID AiDiagnosticsEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request,
                                     SIZE_T OutputBufferLength,
                                     SIZE_T InputBufferLength,
                                     ULONG IoControlCode)
{
    PAI_DIAGNOSTIC_SNAPSHOT_V3 output = NULL;
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
        Request, sizeof(AI_DIAGNOSTIC_SNAPSHOT_V3),
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
