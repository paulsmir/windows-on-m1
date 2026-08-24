#include "apple_input_device.h"
#include "j313_apple_input.generated.h"

#define AI_TRANSPORT_MAX_PACKETS_PER_WORKER 32u
#define AI_TRANSPORT_DISCOVERY_TIMEOUT_US 1000000ull
#define AI_TRANSPORT_DISCOVERY_RETRY_LIMIT 2u
#define AI_TRANSPORT_TRACKPAD_INIT_TIMEOUT_MS 1000u
#define AI_TRANSPORT_TRACKPAD_INIT_TIMEOUT_US 1000000ull
#define AI_TRANSPORT_TRACKPAD_INIT_RETRY_LIMIT 2u
#define AI_TRACKPAD_VHF_FAILURE_LIMIT 3u

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

static USHORT AiTrackpadNextScanTime(PAI_DEVICE_CONTEXT Context)
{
    USHORT next = (USHORT)(AiNowMicroseconds() / 100u);

    if (next == Context->TrackpadScanTime100us)
        next++;
    Context->TrackpadScanTime100us = next;
    return Context->TrackpadScanTime100us;
}

static VOID AiTrackpadReject(PAI_DEVICE_CONTEXT Context,
                             enum AI_TRACKPAD_REJECTION Reason)
{
    AiCounterIncrement(&Context->Diagnostics.TrackpadReportRejectedCount);
    Context->Diagnostics.TrackpadLastRejection = (ULONG)Reason;
}

static NTSTATUS AiTransportProcessTrackpadReport(
    PAI_DEVICE_CONTEXT Context, const UCHAR *Payload, SIZE_T Length)
{
    struct ai_apple_trackpad_frame native_frame;
    struct ai_trackpad_output_frame output_frame;
    struct ai_ptp_feature_state features;
    UCHAR report[AI_PTP_INPUT_REPORT_SIZE];
    enum ai_status protocol_status;
    size_t report_length = 0;
    bool neutral_required;
    KIRQL old_irql;
    NTSTATUS status;

    if (!Context || !Payload)
        return STATUS_INVALID_PARAMETER;
    if (Context->TrackpadInit.phase != AI_TRACKPAD_INIT_READY ||
        Context->TransportOnly || !Context->PublishTrackpad ||
        Context->TrackpadPublicationFailed ||
        Context->TrackpadVhfState != AiVhfRunning)
        return STATUS_SUCCESS;

    protocol_status = ai_apple_trackpad_decode(Payload, Length,
                                                &native_frame);
    if (protocol_status != AI_OK) {
        AiTrackpadReject(Context, AiTrackpadRejectDecode);
        return STATUS_SUCCESS;
    }
    AiCounterIncrement(&Context->Diagnostics.TrackpadReportDecodedCount);
    protocol_status = ai_trackpad_tracker_update(
        &Context->TrackpadTracker, &native_frame, &output_frame);
    if (protocol_status != AI_OK) {
        AiTrackpadReject(Context, AiTrackpadRejectTrack);
        return STATUS_SUCCESS;
    }
    Context->Diagnostics.TrackpadActiveCount = output_frame.active_count;
    Context->Diagnostics.TrackpadAdmittedCount = output_frame.active_count;
    Context->Diagnostics.TrackpadSuppressedCount =
        output_frame.suppressed_count;

    KeAcquireSpinLock(&Context->TrackpadVhf.FeatureLock, &old_irql);
    Context->TrackpadVhf.ContactsActive =
        output_frame.active_count != 0u ? TRUE : FALSE;
    (VOID)ai_ptp_feature_contacts_update(
        &Context->TrackpadVhf.Features,
        output_frame.active_count != 0u);
    features = Context->TrackpadVhf.Features;
    neutral_required = Context->TrackpadVhf.NeutralRequired ? true : false;
    Context->TrackpadVhf.NeutralRequired = FALSE;
    if (ai_ptp_feature_take_neutral(&Context->TrackpadVhf.Features))
        neutral_required = true;
    KeReleaseSpinLock(&Context->TrackpadVhf.FeatureLock, old_irql);

    protocol_status = ai_ptp_encode_input(
        &Context->TrackpadAxisContract, &output_frame,
        AiTrackpadNextScanTime(Context), &features,
        report, sizeof(report), &report_length);
    if (protocol_status != AI_OK) {
        AiTrackpadReject(Context, AiTrackpadRejectEncode);
        return STATUS_SUCCESS;
    }
    if (neutral_required) {
        protocol_status = ai_ptp_encode_neutral(
            Context->TrackpadScanTime100us,
            report, sizeof(report), &report_length);
        if (protocol_status != AI_OK) {
            AiTrackpadReject(Context, AiTrackpadRejectEncode);
            return STATUS_SUCCESS;
        }
    }
    if (!report_length)
        return STATUS_SUCCESS;

    status = AiVhfFrontendSubmitTrackpad(
        Context, report, report_length);
    Context->Diagnostics.TrackpadVhfLastStatus = status;
    if (NT_SUCCESS(status)) {
        Context->TrackpadConsecutiveSubmissionFailures = 0;
        Context->Diagnostics.TrackpadLastRejection = AiTrackpadRejectNone;
        AiCounterIncrement(
            &Context->Diagnostics.TrackpadReportSubmittedCount);
        return STATUS_SUCCESS;
    }

    AiCounterIncrement(
        &Context->Diagnostics.TrackpadVhfSubmissionFailureCount);
    AiTrackpadReject(Context, AiTrackpadRejectSubmit);
    Context->TrackpadConsecutiveSubmissionFailures++;
    if (Context->TrackpadConsecutiveSubmissionFailures >=
        AI_TRACKPAD_VHF_FAILURE_LIMIT) {
        Context->TrackpadPublicationFailed = TRUE;
        AiVhfFrontendStopTrackpad(Context);
    }
    return STATUS_SUCCESS;
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
    if (status == STATUS_IO_TIMEOUT)
        AiCounterIncrement(&Context->Diagnostics.SpiTimeoutCount);
    if (!NT_SUCCESS(status))
        return status;
    if (!ai_write_status_valid(Context->StatusBytes,
                               sizeof(Context->StatusBytes)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    return STATUS_SUCCESS;
}

static VOID AiTrackpadInitArmTimer(PAI_DEVICE_CONTEXT Context)
{
    WdfTimerStart(Context->TrackpadInitTimer,
                  WDF_REL_TIMEOUT_IN_MS(
                      AI_TRANSPORT_TRACKPAD_INIT_TIMEOUT_MS));
}

static NTSTATUS AiTransportSendTrackpadInitRequest(
    PAI_DEVICE_CONTEXT Context)
{
    enum ai_status protocol_status;
    NTSTATUS status;

    protocol_status = ai_trackpad_init_request_encode(
        Context->TrackpadInit.phase, Context->TrackpadInit.message_id,
        Context->TransmitPacket);
    if (protocol_status != AI_OK)
        return STATUS_INVALID_DEVICE_STATE;
    if (Context->Diagnostics.TrackpadInitAttemptCount != MAXUCHAR)
        Context->Diagnostics.TrackpadInitAttemptCount++;

    RtlZeroMemory(Context->StatusBytes, sizeof(Context->StatusBytes));
    status = AiSpiWritePacketReadStatus(Context, Context->TransmitPacket,
                                        Context->StatusBytes,
                                        AiTransferDeadlineQpc());
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

NTSTATUS AiCaptureDiscoveryDescriptor(
    PAI_DEVICE_CONTEXT Context, enum ai_discovery_phase Phase,
    const struct ai_protocol_message *Message)
{
    const struct ai_descriptor_slot *descriptor;
    enum ai_status protocol_status;
    UCHAR device;

    if (!Context || !Message)
        return STATUS_INVALID_PARAMETER;
    if (Phase == AI_DISCOVERY_KEYBOARD_DESCRIPTOR)
        device = 1;
    else if (Phase == AI_DISCOVERY_TRACKPAD_DESCRIPTOR)
        device = 2;
    else
        return STATUS_SUCCESS;
    if (Message->device != device)
        return STATUS_DEVICE_PROTOCOL_ERROR;

    protocol_status = ai_descriptor_store_put(
        &Context->Descriptors, device, Message->payload,
        Message->payload_length);
    if (protocol_status != AI_OK)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    descriptor = ai_descriptor_store_get(&Context->Descriptors, device);
    if (!descriptor)
        return STATUS_INTERNAL_ERROR;

    if (device == 1) {
        protocol_status = ai_hid_input_contract_parse(
            descriptor->bytes, descriptor->length,
            &Context->KeyboardInputContract);
        if (protocol_status != AI_OK)
            return STATUS_DEVICE_PROTOCOL_ERROR;
        Context->Diagnostics.KeyboardContractValid = TRUE;
    }
    AiDiagnosticsRecordDescriptor(Context, descriptor);
    return STATUS_SUCCESS;
}

static NTSTATUS AiTransportProcessPacket(PAI_DEVICE_CONTEXT Context)
{
    struct ai_packet_view packet;
    struct ai_message_view wire;
    struct ai_protocol_message message;
    enum ai_discovery_phase phase = Context->Discovery.phase;
    enum ai_status protocol_status;
    NTSTATUS status;
    ULONGLONG now_us = AiNowMicroseconds();

    protocol_status = ai_packet_decode(Context->ReceivePacket, &packet);
    if (protocol_status != AI_OK) {
        if (protocol_status == AI_ERR_CRC)
            AiCounterIncrement(&Context->Diagnostics.PacketCrcFailureCount);
        return STATUS_DATA_ERROR;
    }
    AiDiagnosticsRecordHeader(Context, &packet, protocol_status);

    if (phase == AI_DISCOVERY_WAIT_BOOT) {
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

    if (phase == AI_DISCOVERY_READY) {
        if (ai_trackpad_init_response_matches(
                &Context->TrackpadInit, &wire, &message)) {
            WdfTimerStop(Context->TrackpadInitTimer, FALSE);
            protocol_status = ai_trackpad_init_accept(
                &Context->TrackpadInit, &wire, &message, now_us,
                AI_TRANSPORT_TRACKPAD_INIT_TIMEOUT_US);
            if (protocol_status == AI_OK) {
                protocol_status = ai_trackpad_axis_contract_from_dimensions(
                    &Context->TrackpadInit.dimensions,
                    &Context->TrackpadAxisContract);
                if (protocol_status != AI_OK) {
                    Context->TrackpadInit.phase = AI_TRACKPAD_INIT_OFFLINE;
                    AiCounterIncrement(&Context->Diagnostics.OfflineCount);
                    return STATUS_DEVICE_PROTOCOL_ERROR;
                }
                Context->Diagnostics.TrackpadAxisXValid =
                    Context->TrackpadAxisContract.x.valid ? TRUE : FALSE;
                Context->Diagnostics.TrackpadAxisYValid =
                    Context->TrackpadAxisContract.y.valid ? TRUE : FALSE;
                Context->Diagnostics.TrackpadLogicalXMinimum =
                    Context->TrackpadAxisContract.x.logical_min;
                Context->Diagnostics.TrackpadLogicalXMaximum =
                    Context->TrackpadAxisContract.x.logical_max;
                Context->Diagnostics.TrackpadLogicalYMinimum =
                    Context->TrackpadAxisContract.y.logical_min;
                Context->Diagnostics.TrackpadLogicalYMaximum =
                    Context->TrackpadAxisContract.y.logical_max;
                Context->Diagnostics.TrackpadPhysicalXMinimum =
                    Context->TrackpadAxisContract.x.physical_min;
                Context->Diagnostics.TrackpadPhysicalXMaximum =
                    Context->TrackpadAxisContract.x.physical_max;
                Context->Diagnostics.TrackpadPhysicalYMinimum =
                    Context->TrackpadAxisContract.y.physical_min;
                Context->Diagnostics.TrackpadPhysicalYMaximum =
                    Context->TrackpadAxisContract.y.physical_max;
                Context->Diagnostics.TrackpadUnit =
                    Context->TrackpadAxisContract.x.unit;
                Context->Diagnostics.TrackpadUnitExponent =
                    Context->TrackpadAxisContract.x.unit_exponent;
                (VOID)AiTransportSendTrackpadInitRequest(Context);
                AiTrackpadInitArmTimer(Context);
            } else if (protocol_status == AI_COMPLETE) {
                (VOID)AiVhfFrontendStart(Context);
            } else if (protocol_status != AI_COMPLETE) {
                Context->TrackpadInit.phase = AI_TRACKPAD_INIT_OFFLINE;
                AiCounterIncrement(&Context->Diagnostics.OfflineCount);
            }
        } else if (wire.flags == AI_PACKET_READ && wire.device == 1u) {
            AiCounterIncrement(&Context->Diagnostics.KeyboardReportCount);
            if (!ai_hid_input_report_valid(
                    &Context->KeyboardInputContract, message.payload,
                    message.payload_length, NULL)) {
                AiCounterIncrement(
                    &Context->Diagnostics.KeyboardReportRejectedCount);
                Context->Diagnostics.KeyboardVhfLastStatus =
                    STATUS_INVALID_BUFFER_SIZE;
                return STATUS_SUCCESS;
            }
            AiCounterIncrement(
                &Context->Diagnostics.KeyboardReportAcceptedCount);
            if (!Context->TransportOnly && Context->PublishKeyboard) {
                status = AiVhfFrontendSubmitKeyboard(
                    Context, message.payload, message.payload_length);
                Context->Diagnostics.KeyboardVhfLastStatus = status;
                if (NT_SUCCESS(status))
                    AiCounterIncrement(
                        &Context->Diagnostics.KeyboardReportSubmittedCount);
                else
                    AiCounterIncrement(
                        &Context->Diagnostics.KeyboardVhfSubmissionFailureCount);
            }
        } else if (wire.flags == AI_PACKET_READ && wire.device == 2u) {
            AiCounterIncrement(&Context->Diagnostics.TrackpadReportCount);
#if AI_ENABLE_TRACKPAD_CAPTURE
            AiTrackpadCaptureRecord(Context, 2u, message.payload,
                                    message.payload_length);
#endif
            (VOID)AiTransportProcessTrackpadReport(
                Context, message.payload, message.payload_length);
        }
        return STATUS_SUCCESS;
    }

    if (!ai_discovery_response_matches(phase, &wire, &message))
        return STATUS_DEVICE_PROTOCOL_ERROR;

    status = AiCaptureDiscoveryDescriptor(Context, phase, &message);
    if (!NT_SUCCESS(status))
        return status;

    protocol_status = ai_discovery_accept(
        &Context->Discovery, Context->Discovery.request_id, true, now_us,
        AI_TRANSPORT_DISCOVERY_TIMEOUT_US);
    if (protocol_status == AI_COMPLETE) {
        status = AiVhfFrontendStart(Context);
        Context->Diagnostics.KeyboardVhfLastStatus = status;
        Context->Diagnostics.KeyboardVhfState =
            (ULONG)Context->KeyboardVhfState;
        if (!NT_SUCCESS(status))
            AiCounterIncrement(
                &Context->Diagnostics.KeyboardVhfStartFailureCount);
        ai_trackpad_init_start(
            &Context->TrackpadInit, Context->MessageId++, now_us,
            AI_TRANSPORT_TRACKPAD_INIT_TIMEOUT_US,
            AI_TRANSPORT_TRACKPAD_INIT_RETRY_LIMIT);
        (VOID)AiTransportSendTrackpadInitRequest(Context);
        AiTrackpadInitArmTimer(Context);
        return STATUS_SUCCESS;
    }
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

    WdfWaitLockAcquire(context->TransportLock, NULL);

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

    WdfWaitLockRelease(context->TransportLock);

    AiCounterIncrement(&context->Diagnostics.WorkerCompletedCount);
    AiDiagnosticsPublish(context);
}

VOID AiTrackpadInitTimer(WDFTIMER Timer)
{
    PAI_DEVICE_CONTEXT context = AiGetDeviceContext(
        (WDFDEVICE)WdfTimerGetParentObject(Timer));
    enum ai_status protocol_status;

    WdfWaitLockAcquire(context->TransportLock, NULL);
    if (context->Stopping || !context->HardwareStarted)
        goto Exit;

    protocol_status = ai_trackpad_init_poll(
        &context->TrackpadInit, AiNowMicroseconds(),
        AI_TRANSPORT_TRACKPAD_INIT_TIMEOUT_US);
    if (protocol_status == AI_ERR_TIMEOUT &&
        context->TrackpadInit.phase != AI_TRACKPAD_INIT_OFFLINE) {
        (VOID)AiTransportSendTrackpadInitRequest(context);
        AiTrackpadInitArmTimer(context);
    } else if (context->TrackpadInit.phase == AI_TRACKPAD_INIT_OFFLINE) {
        AiCounterIncrement(&context->Diagnostics.OfflineCount);
    }
    AiDiagnosticsPublish(context);

Exit:
    WdfWaitLockRelease(context->TransportLock);
}

NTSTATUS AiTransportStart(PAI_DEVICE_CONTEXT Context)
{
    NTSTATUS status;

    if (!Context || !Context->ResourcesValidated)
        return STATUS_DEVICE_NOT_READY;
    WdfTimerStop(Context->TrackpadInitTimer, TRUE);
    WdfWaitLockAcquire(Context->TransportLock, NULL);
    Context->Stopping = FALSE;
    Context->HardwareStarted = FALSE;
    ai_transport_queue_reset(&Context->TransportQueue);
    ai_reassembler_reset(&Context->Reassembler);
    AI_MEMSET(&Context->TrackpadInit, sizeof(Context->TrackpadInit));
    AI_MEMSET(&Context->TrackpadTracker, sizeof(Context->TrackpadTracker));
    ai_descriptor_store_reset(&Context->Descriptors);
    AI_MEMSET(&Context->KeyboardInputContract,
              sizeof(Context->KeyboardInputContract));
    RtlZeroMemory(&Context->Diagnostics, sizeof(Context->Diagnostics));
    Context->Diagnostics.Version = AI_DIAGNOSTIC_SNAPSHOT_VERSION_4;
    Context->Diagnostics.Size = sizeof(Context->Diagnostics);
    Context->MessageId = 0;
    Context->TrackpadScanTime100us = 0;
    Context->TrackpadConsecutiveSubmissionFailures = 0;
    Context->TrackpadPublicationFailed = FALSE;
#if AI_ENABLE_TRACKPAD_CAPTURE
    AiTrackpadCaptureCancel(Context);
#endif

    status = AiSpiInitialize(Context);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = AiGpioEnableInputInterrupt(Context);
    if (!NT_SUCCESS(status))
        goto Exit;
    ai_discovery_start(&Context->Discovery, AiNowMicroseconds(),
                       AI_TRANSPORT_DISCOVERY_TIMEOUT_US,
                       AI_TRANSPORT_DISCOVERY_RETRY_LIMIT);
    Context->HardwareStarted = TRUE;
    status = AiGpioResetInputController(Context);
    AiCounterIncrement(&Context->Diagnostics.ResetCount);
    if (!NT_SUCCESS(status)) {
        Context->HardwareStarted = FALSE;
        Context->Stopping = TRUE;
        goto Exit;
    }
    AiDiagnosticsPublish(Context);
Exit:
    WdfWaitLockRelease(Context->TransportLock);
    return status;
}

VOID AiTransportStop(PAI_DEVICE_CONTEXT Context)
{
    if (!Context)
        return;
    Context->Stopping = TRUE;
    Context->HardwareStarted = FALSE;
    WdfTimerStop(Context->TrackpadInitTimer, TRUE);
    WdfWaitLockAcquire(Context->TransportLock, NULL);
    ai_transport_queue_reset(&Context->TransportQueue);
    ai_reassembler_reset(&Context->Reassembler);
    Context->TrackpadInit.phase = AI_TRACKPAD_INIT_IDLE;
#if AI_ENABLE_TRACKPAD_CAPTURE
    AiTrackpadCaptureCancel(Context);
#endif
    AiVhfFrontendStop(Context);
    Context->Diagnostics.KeyboardVhfState =
        (ULONG)Context->KeyboardVhfState;
    AiDiagnosticsPublish(Context);
    WdfWaitLockRelease(Context->TransportLock);
}
