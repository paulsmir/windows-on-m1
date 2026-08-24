#include <initguid.h>
#include "apple_input_device.h"
#include "apple_trackpad.h"

static VOID AiTrackpadCaptureClearLocked(PAI_DEVICE_CONTEXT Context)
{
    RtlZeroMemory(&Context->TrackpadCapture,
                  sizeof(Context->TrackpadCapture));
    Context->TrackpadCapture.Version = AI_TRACKPAD_CAPTURE_VERSION;
    Context->TrackpadCapture.Size = sizeof(Context->TrackpadCapture);
}

NTSTATUS AiTrackpadCaptureInitialize(WDFDEVICE Device,
                                     PAI_DEVICE_CONTEXT Context)
{
    AiTrackpadCaptureClearLocked(Context);
    return WdfDeviceCreateDeviceInterface(
        Device, &GUID_DEVINTERFACE_APPLE_INPUT_TRACKPAD_CAPTURE, NULL);
}

VOID AiTrackpadCaptureCancel(PAI_DEVICE_CONTEXT Context)
{
    if (!Context || !Context->CaptureLock)
        return;
    if (!NT_SUCCESS(WdfWaitLockAcquire(Context->CaptureLock, NULL)))
        return;
    AiTrackpadCaptureClearLocked(Context);
    WdfWaitLockRelease(Context->CaptureLock);
}

VOID AiTrackpadCaptureRecord(PAI_DEVICE_CONTEXT Context, UCHAR Device,
                             const UCHAR *Report, SIZE_T Length)
{
    PAI_TRACKPAD_CAPTURE_RECORD record;

    if (!Context || !Context->CaptureLock || !Report || Device != 2u ||
        Length == 0 || Length > AI_TRACKPAD_CAPTURE_MAX_REPORT_SIZE)
        return;
    if (!NT_SUCCESS(WdfWaitLockAcquire(Context->CaptureLock, NULL)))
        return;
    if (!Context->TrackpadCapture.Armed) {
        WdfWaitLockRelease(Context->CaptureLock);
        return;
    }
    if (Context->TrackpadCapture.Trigger ==
            AI_TRACKPAD_CAPTURE_TRIGGER_RELEASE &&
        !ai_apple_trackpad_release_candidate(Report, Length)) {
        WdfWaitLockRelease(Context->CaptureLock);
        return;
    }
    if (Length > Context->TrackpadCapture.ReportSizeLimit ||
        Context->TrackpadCapture.ReportCount >= AI_TRACKPAD_CAPTURE_MAX_REPORTS ||
        Context->TrackpadCapture.ReportCount >=
            Context->TrackpadCapture.ReportLimit) {
        Context->TrackpadCapture.DroppedCount++;
        Context->TrackpadCapture.Armed = FALSE;
        Context->TrackpadCapture.Complete = TRUE;
        WdfWaitLockRelease(Context->CaptureLock);
        return;
    }

    record = &Context->TrackpadCapture.Records[
        Context->TrackpadCapture.ReportCount++];
    record->Length = (ULONG)Length;
    RtlCopyMemory(record->Bytes, Report, Length);
    if (Context->TrackpadCapture.ReportCount >=
        Context->TrackpadCapture.ReportLimit) {
        Context->TrackpadCapture.Armed = FALSE;
        Context->TrackpadCapture.Complete = TRUE;
    }
    WdfWaitLockRelease(Context->CaptureLock);
}

static NTSTATUS AiTrackpadCaptureArm(PAI_DEVICE_CONTEXT Context,
                                     WDFREQUEST Request)
{
    PAI_TRACKPAD_CAPTURE_ARM_REQUEST input = NULL;
    NTSTATUS status;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(*input), (PVOID *)&input, NULL);
    if (!NT_SUCCESS(status))
        return status;
    if (input->Version != AI_TRACKPAD_CAPTURE_VERSION ||
        input->ReportLimit == 0 ||
        input->ReportLimit > AI_TRACKPAD_CAPTURE_MAX_REPORTS ||
        input->ReportSizeLimit == 0 ||
        input->ReportSizeLimit > AI_TRACKPAD_CAPTURE_MAX_REPORT_SIZE ||
        input->Trigger > AI_TRACKPAD_CAPTURE_TRIGGER_RELEASE)
        return STATUS_INVALID_PARAMETER;
    if (Context->Discovery.phase != AI_DISCOVERY_READY ||
        !Context->Descriptors.trackpad.valid)
        return STATUS_DEVICE_NOT_READY;
    status = WdfWaitLockAcquire(Context->CaptureLock, NULL);
    if (!NT_SUCCESS(status))
        return status;
    AiTrackpadCaptureClearLocked(Context);
    Context->TrackpadCapture.ReportLimit = input->ReportLimit;
    Context->TrackpadCapture.ReportSizeLimit = input->ReportSizeLimit;
    Context->TrackpadCapture.Trigger = input->Trigger;
    RtlCopyMemory(Context->TrackpadCapture.TrackpadDescriptorSha256,
                  Context->Diagnostics.TrackpadDescriptorSha256,
                  AI_SHA256_DIGEST_SIZE);
    Context->TrackpadCapture.Armed = TRUE;
    WdfWaitLockRelease(Context->CaptureLock);
    return STATUS_SUCCESS;
}

static NTSTATUS AiTrackpadCaptureRead(PAI_DEVICE_CONTEXT Context,
                                      WDFREQUEST Request,
                                      SIZE_T *Information)
{
    PAI_TRACKPAD_CAPTURE_BLOB output = NULL;
    NTSTATUS status;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(*output), (PVOID *)&output, NULL);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfWaitLockAcquire(Context->CaptureLock, NULL);
    if (!NT_SUCCESS(status))
        return status;
    RtlCopyMemory(output, &Context->TrackpadCapture, sizeof(*output));
    WdfWaitLockRelease(Context->CaptureLock);
    *Information = sizeof(*output);
    return STATUS_SUCCESS;
}

static NTSTATUS AiTrackpadCaptureReadDescriptor(PAI_DEVICE_CONTEXT Context,
                                                WDFREQUEST Request,
                                                SIZE_T *Information)
{
    PAI_TRACKPAD_DESCRIPTOR_CAPTURE output = NULL;
    const struct ai_descriptor_slot *descriptor;
    NTSTATUS status;

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(*output), (PVOID *)&output, NULL);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfWaitLockAcquire(Context->TransportLock, NULL);
    if (!NT_SUCCESS(status))
        return status;
    descriptor = &Context->Descriptors.trackpad;
    if (Context->Discovery.phase != AI_DISCOVERY_READY ||
        !descriptor->valid || descriptor->length == 0u ||
        descriptor->length > AI_TRACKPAD_DESCRIPTOR_CAPTURE_MAX_SIZE) {
        WdfWaitLockRelease(Context->TransportLock);
        return STATUS_DEVICE_NOT_READY;
    }
    RtlZeroMemory(output, sizeof(*output));
    output->Version = AI_TRACKPAD_DESCRIPTOR_CAPTURE_VERSION;
    output->Size = sizeof(*output);
    output->Length = descriptor->length;
    RtlCopyMemory(output->TrackpadDescriptorSha256,
                  Context->Diagnostics.TrackpadDescriptorSha256,
                  AI_SHA256_DIGEST_SIZE);
    RtlCopyMemory(output->Bytes, descriptor->bytes, descriptor->length);
    WdfWaitLockRelease(Context->TransportLock);
    *Information = sizeof(*output);
    return STATUS_SUCCESS;
}

BOOLEAN AiTrackpadCaptureIoctl(PAI_DEVICE_CONTEXT Context,
                               WDFREQUEST Request, ULONG IoControlCode)
{
    NTSTATUS status;
    SIZE_T information = 0;

    if (IoControlCode == IOCTL_AI_TRACKPAD_CAPTURE_ARM) {
        status = AiTrackpadCaptureArm(Context, Request);
    } else if (IoControlCode == IOCTL_AI_TRACKPAD_CAPTURE_READ) {
        status = AiTrackpadCaptureRead(Context, Request, &information);
    } else if (IoControlCode == IOCTL_AI_TRACKPAD_CAPTURE_CANCEL) {
        AiTrackpadCaptureCancel(Context);
        status = STATUS_SUCCESS;
    } else if (IoControlCode ==
               IOCTL_AI_TRACKPAD_CAPTURE_READ_DESCRIPTOR) {
        status = AiTrackpadCaptureReadDescriptor(Context, Request,
                                                 &information);
    } else {
        return FALSE;
    }
    WdfRequestCompleteWithInformation(Request, status, information);
    return TRUE;
}
