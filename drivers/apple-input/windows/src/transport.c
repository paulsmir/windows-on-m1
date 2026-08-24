#include "apple_input_device.h"
#include "j313_apple_input.generated.h"

#define AI_TRANSPORT_MAX_PACKETS_PER_WORKER 32u
#define AI_TRANSPORT_DISCOVERY_TIMEOUT_US 1000000ull
#define AI_TRANSPORT_DISCOVERY_RETRY_LIMIT 2u

static VOID AiCounterIncrement(ULONGLONG *Counter)
{
    InterlockedIncrement64((volatile LONG64 *)Counter);
}

static ULONGLONG AiNowMicroseconds(VOID)
{
    LARGE_INTEGER frequency;
    ULONGLONG count = (ULONGLONG)
        KeQueryPerformanceCounter(&frequency).QuadPart;
    ULONGLONG hz = (ULONGLONG)frequency.QuadPart;

    if (!hz)
        return 0;
    return (count / hz) * 1000000ull +
           ((count % hz) * 1000000ull) / hz;
}

static ULONGLONG AiTransferDeadlineQpc(VOID)
{
    LARGE_INTEGER frequency;
    ULONGLONG now = (ULONGLONG)
        KeQueryPerformanceCounter(&frequency).QuadPart;
    ULONGLONG hz = (ULONGLONG)frequency.QuadPart;
    ULONGLONG delta = (hz / 1000u) * AI_SPI_TRANSFER_TIMEOUT_MS;

    delta += ((hz % 1000u) * AI_SPI_TRANSFER_TIMEOUT_MS + 999u) / 1000u;
    if (~(ULONGLONG)0 - now < delta)
        return ~(ULONGLONG)0;
    return now + delta;
}

static NTSTATUS AiTransportSendCurrentRequest(PAI_DEVICE_CONTEXT Context)
{
    struct ai_discovery_request request;
    enum ai_status protocol_status;
    NTSTATUS status;

    protocol_status = ai_discovery_request_for_phase(Context->Discovery.phase,
                                                     &request);
    if (protocol_status != AI_OK)
        return STATUS_INVALID_DEVICE_STATE;
    protocol_status = ai_discovery_request_encode(
        &request, (UCHAR)Context->Discovery.request_id,
        Context->TransmitPacket);
    if (protocol_status != AI_OK)
        return STATUS_DATA_ERROR;

    RtlZeroMemory(Context->StatusBytes, sizeof(Context->StatusBytes));
    status = AiSpiWritePacketReadStatus(Context, Context->TransmitPacket,
                                        Context->StatusBytes,
                                        AiTransferDeadlineQpc());
    AiCounterIncrement(&Context->Diagnostics.SpiTransferCount);
    AiCounterIncrement(&Context->Diagnostics.SpiTransferCount);
    if (status == STATUS_IO_TIMEOUT)
        AiCounterIncrement(&Context->Diagnostics.SpiTimeoutCount);
    if (!NT_SUCCESS(status))
        return status;
    if (!ai_write_status_valid(Context->StatusBytes,
                               sizeof(Context->StatusBytes)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    return STATUS_SUCCESS;
}

static NTSTATUS AiTransportProcessPacket(PAI_DEVICE_CONTEXT Context)
{
    struct ai_packet_view packet;
    struct ai_message_view wire;
    struct ai_protocol_message message;
    enum ai_status protocol_status;
    ULONGLONG now_us = AiNowMicroseconds();

    protocol_status = ai_packet_decode(Context->ReceivePacket, &packet);
    if (protocol_status != AI_OK) {
        if (protocol_status == AI_ERR_CRC)
            AiCounterIncrement(&Context->Diagnostics.PacketCrcFailureCount);
        return STATUS_DATA_ERROR;
    }
    AiDiagnosticsRecordHeader(Context, &packet, protocol_status);

    if (Context->Discovery.phase == AI_DISCOVERY_WAIT_BOOT) {
        protocol_status = ai_discovery_accept_boot(
            &Context->Discovery, packet.data, packet.length, now_us,
            AI_TRANSPORT_DISCOVERY_TIMEOUT_US);
        if (protocol_status != AI_OK)
            return STATUS_DEVICE_PROTOCOL_ERROR;
        return AiTransportSendCurrentRequest(Context);
    }

    protocol_status = ai_reassembler_push(&Context->Reassembler, &packet,
                                          &wire);
    if (protocol_status == AI_OK)
        return STATUS_SUCCESS;
    if (protocol_status != AI_COMPLETE) {
        AiCounterIncrement(&Context->Diagnostics.FragmentFailureCount);
        return STATUS_DATA_ERROR;
    }

    protocol_status = ai_message_decode(wire.data, wire.length, &message);
    if (protocol_status != AI_OK) {
        if (protocol_status == AI_ERR_CRC)
            AiCounterIncrement(&Context->Diagnostics.MessageCrcFailureCount);
        return STATUS_DATA_ERROR;
    }
    AiDiagnosticsRecordMessage(Context, &message);

    if (Context->Discovery.phase == AI_DISCOVERY_READY) {
        if (wire.flags == AI_PACKET_READ && wire.device == 1u)
            AiCounterIncrement(&Context->Diagnostics.KeyboardReportCount);
        else if (wire.flags == AI_PACKET_READ && wire.device == 2u)
            AiCounterIncrement(&Context->Diagnostics.TrackpadReportCount);
        return STATUS_SUCCESS;
    }

    if (!ai_discovery_response_matches(Context->Discovery.phase, &wire,
                                       &message))
        return STATUS_DEVICE_PROTOCOL_ERROR;

    protocol_status = ai_discovery_accept(
        &Context->Discovery, Context->Discovery.request_id, true, now_us,
        AI_TRANSPORT_DISCOVERY_TIMEOUT_US);
    if (protocol_status == AI_COMPLETE)
        return STATUS_SUCCESS;
    if (protocol_status != AI_OK)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    return AiTransportSendCurrentRequest(Context);
}

BOOLEAN AiInputInterruptIsr(WDFINTERRUPT Interrupt, ULONG MessageID)
{
    PAI_DEVICE_CONTEXT context = AiGetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    BOOLEAN queue_worker;

    UNREFERENCED_PARAMETER(MessageID);
    if (!context->HardwareStarted || context->Stopping ||
        !AiGpioInputAsserted(context))
        return FALSE;

    AiGpioAcknowledge(context);
    AiCounterIncrement(&context->Diagnostics.InterruptCount);
    queue_worker = ai_transport_irq(&context->TransportQueue) ? TRUE : FALSE;
    if (queue_worker && WdfInterruptQueueWorkItemForIsr(Interrupt))
        AiCounterIncrement(&context->Diagnostics.WorkerQueuedCount);
    return TRUE;
}

VOID AiTransportWorker(WDFINTERRUPT Interrupt, WDFOBJECT AssociatedObject)
{
    PAI_DEVICE_CONTEXT context = AiGetDeviceContext(
        WdfInterruptGetDevice(Interrupt));
    ULONG packet_count = 0;
    BOOLEAN asserted;
    BOOLEAN drain_again;

    UNREFERENCED_PARAMETER(AssociatedObject);
    WdfInterruptAcquireLock(Interrupt);
    if (!ai_transport_worker_begin(&context->TransportQueue)) {
        WdfInterruptReleaseLock(Interrupt);
        return;
    }
    WdfInterruptReleaseLock(Interrupt);

    do {
        while (packet_count < AI_TRANSPORT_MAX_PACKETS_PER_WORKER) {
            NTSTATUS status;

            if (context->Stopping || !context->HardwareStarted)
                break;
            RtlZeroMemory(context->ReceivePacket, sizeof(context->ReceivePacket));
            status = AiSpiTransfer(context, NULL, context->ReceivePacket,
                                   AI_PACKET_SIZE,
                                   AiTransferDeadlineQpc());
            packet_count++;
            AiCounterIncrement(&context->Diagnostics.SpiTransferCount);
            if (status == STATUS_IO_TIMEOUT)
                AiCounterIncrement(&context->Diagnostics.SpiTimeoutCount);
            if (!NT_SUCCESS(status))
                break;
            status = AiTransportProcessPacket(context);
            AiGpioAcknowledge(context);
            if (!NT_SUCCESS(status) || !AiGpioInputAsserted(context))
                break;
        }

        asserted = AiGpioInputAsserted(context);
        WdfInterruptAcquireLock(Interrupt);
        drain_again = ai_transport_worker_complete(
            &context->TransportQueue, asserted ? true : false) ? TRUE : FALSE;
        if (drain_again &&
            packet_count < AI_TRANSPORT_MAX_PACKETS_PER_WORKER) {
            drain_again = ai_transport_worker_begin(
                &context->TransportQueue) ? TRUE : FALSE;
        } else if (drain_again) {
            /* Bound this callback; an asserted level can schedule the next one. */
            context->TransportQueue.pending = false;
        }
        WdfInterruptReleaseLock(Interrupt);
    } while (drain_again && !context->Stopping && context->HardwareStarted);

    AiCounterIncrement(&context->Diagnostics.WorkerCompletedCount);
    AiDiagnosticsPublish(context);
}

NTSTATUS AiTransportStart(PAI_DEVICE_CONTEXT Context)
{
    NTSTATUS status;

    if (!Context || !Context->ResourcesValidated)
        return STATUS_DEVICE_NOT_READY;
    Context->Stopping = FALSE;
    Context->HardwareStarted = FALSE;
    ai_transport_queue_reset(&Context->TransportQueue);
    ai_reassembler_reset(&Context->Reassembler);
    RtlZeroMemory(&Context->Diagnostics, sizeof(Context->Diagnostics));
    Context->Diagnostics.Version = AI_DIAGNOSTIC_SNAPSHOT_VERSION_2;
    Context->Diagnostics.Size = sizeof(Context->Diagnostics);

    status = AiSpiInitialize(Context);
    if (!NT_SUCCESS(status))
        return status;
    status = AiGpioEnableInputInterrupt(Context);
    if (!NT_SUCCESS(status))
        return status;
    ai_discovery_start(&Context->Discovery, AiNowMicroseconds(),
                       AI_TRANSPORT_DISCOVERY_TIMEOUT_US,
                       AI_TRANSPORT_DISCOVERY_RETRY_LIMIT);
    Context->HardwareStarted = TRUE;
    status = AiGpioResetInputController(Context);
    AiCounterIncrement(&Context->Diagnostics.ResetCount);
    if (!NT_SUCCESS(status)) {
        Context->HardwareStarted = FALSE;
        Context->Stopping = TRUE;
        return status;
    }
    AiDiagnosticsPublish(Context);
    return STATUS_SUCCESS;
}

VOID AiTransportStop(PAI_DEVICE_CONTEXT Context)
{
    if (!Context)
        return;
    Context->Stopping = TRUE;
    Context->HardwareStarted = FALSE;
    ai_transport_queue_reset(&Context->TransportQueue);
    ai_reassembler_reset(&Context->Reassembler);
    AiDiagnosticsPublish(Context);
}
