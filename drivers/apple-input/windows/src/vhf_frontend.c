#include "apple_input_device.h"

NTSTATUS AiVhfFrontendStart(PAI_DEVICE_CONTEXT Context)
{
    NTSTATUS keyboard_status = STATUS_SUCCESS;
    NTSTATUS trackpad_status;

    PAGED_CODE();
    if (!Context || !Context->FrontendLock)
        return STATUS_INVALID_PARAMETER;
    if (Context->TransportOnly ||
        (!Context->PublishKeyboard && !Context->PublishTrackpad))
        return STATUS_SUCCESS;

    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (Context->KeyboardVhfState == AiVhfStopping) {
        WdfWaitLockRelease(Context->FrontendLock);
        return STATUS_DEVICE_BUSY;
    }
    if (Context->PublishKeyboard &&
        Context->KeyboardVhfState != AiVhfRunning) {
        Context->KeyboardVhfState = AiVhfDescriptorsReady;
        if (!Context->Descriptors.keyboard.valid ||
            !Context->KeyboardInputContract.valid) {
            keyboard_status = STATUS_DEVICE_NOT_READY;
        } else {
            Context->KeyboardVhfState = AiVhfStarting;
            keyboard_status = AiKeyboardVhfStart(Context);
            Context->KeyboardVhfState = NT_SUCCESS(keyboard_status) ?
                AiVhfRunning : AiVhfDescriptorsReady;
        }
    }

    if (Context->PublishTrackpad &&
        !Context->TrackpadPublicationFailed &&
        Context->TrackpadVhfState != AiVhfRunning &&
        Context->TrackpadVhfState != AiVhfStopping) {
        Context->TrackpadVhfState = AiVhfDescriptorsReady;
        if (Context->TrackpadInit.phase == AI_TRACKPAD_INIT_READY) {
            Context->TrackpadVhfState = AiVhfStarting;
            trackpad_status = AiTrackpadVhfStart(Context);
            Context->TrackpadVhfState = NT_SUCCESS(trackpad_status) ?
                AiVhfRunning : AiVhfDescriptorsReady;
            Context->Diagnostics.TrackpadVhfLastStatus = trackpad_status;
            if (!NT_SUCCESS(trackpad_status))
                InterlockedIncrement64((volatile LONG64 *)&
                    Context->Diagnostics.TrackpadVhfStartFailureCount);
        }
    }
    WdfWaitLockRelease(Context->FrontendLock);
    return keyboard_status;
}

NTSTATUS AiVhfFrontendSubmitKeyboard(PAI_DEVICE_CONTEXT Context,
                                     const UCHAR *Report, SIZE_T Length)
{
    NTSTATUS status;

    if (!Context || !Context->FrontendLock)
        return STATUS_INVALID_PARAMETER;
    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (!Context->PublishKeyboard ||
        Context->KeyboardVhfState != AiVhfRunning)
        status = STATUS_DEVICE_NOT_READY;
    else
        status = AiKeyboardVhfSubmit(Context, Report, Length);
    WdfWaitLockRelease(Context->FrontendLock);
    return status;
}

NTSTATUS AiVhfFrontendSubmitTrackpad(PAI_DEVICE_CONTEXT Context,
                                     const UCHAR *Report, SIZE_T Length)
{
    NTSTATUS status;

    if (!Context || !Context->FrontendLock)
        return STATUS_INVALID_PARAMETER;
    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (!Context->PublishTrackpad || Context->TrackpadPublicationFailed ||
        Context->TrackpadVhfState != AiVhfRunning)
        status = STATUS_DEVICE_NOT_READY;
    else
        status = AiTrackpadVhfSubmit(Context, Report, Length);
    WdfWaitLockRelease(Context->FrontendLock);
    return status;
}

VOID AiVhfFrontendStopTrackpad(PAI_DEVICE_CONTEXT Context)
{
    PAGED_CODE();
    if (!Context || !Context->FrontendLock)
        return;
    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (Context->TrackpadVhfState != AiVhfAbsent) {
        Context->TrackpadVhfState = AiVhfStopping;
        AiTrackpadVhfStop(Context);
        Context->TrackpadVhfState = AiVhfAbsent;
    }
    WdfWaitLockRelease(Context->FrontendLock);
}

VOID AiVhfFrontendStop(PAI_DEVICE_CONTEXT Context)
{
    UCHAR neutral[AI_PTP_INPUT_REPORT_SIZE];
    size_t neutral_length = 0;

    PAGED_CODE();
    if (!Context || !Context->FrontendLock)
        return;
    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (Context->TrackpadVhfState != AiVhfAbsent) {
        Context->TrackpadVhfState = AiVhfStopping;
        if (Context->TrackpadVhf.Running &&
            ai_ptp_encode_neutral(0u, neutral, sizeof(neutral),
                                  &neutral_length) == AI_OK)
            (VOID)AiTrackpadVhfSubmit(Context, neutral, neutral_length);
        AiTrackpadVhfStop(Context);
        Context->TrackpadVhfState = AiVhfAbsent;
    }
    if (Context->KeyboardVhfState != AiVhfAbsent) {
        Context->KeyboardVhfState = AiVhfStopping;
        AiKeyboardVhfStop(Context);
        Context->KeyboardVhfState = AiVhfAbsent;
    }
    WdfWaitLockRelease(Context->FrontendLock);
}
