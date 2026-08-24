#include "apple_input_device.h"

NTSTATUS AiVhfFrontendStart(PAI_DEVICE_CONTEXT Context)
{
    NTSTATUS status;

    if (!Context || !Context->FrontendLock)
        return STATUS_INVALID_PARAMETER;
    if (Context->TransportOnly)
        return STATUS_SUCCESS;

    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (Context->KeyboardVhfState == AiVhfRunning) {
        WdfWaitLockRelease(Context->FrontendLock);
        return STATUS_SUCCESS;
    }
    if (Context->KeyboardVhfState == AiVhfStopping) {
        WdfWaitLockRelease(Context->FrontendLock);
        return STATUS_DEVICE_BUSY;
    }
    if (!Context->Descriptors.keyboard.valid ||
        !Context->KeyboardInputContract.valid) {
        WdfWaitLockRelease(Context->FrontendLock);
        return STATUS_DEVICE_NOT_READY;
    }

    Context->KeyboardVhfState = AiVhfDescriptorsReady;
    Context->KeyboardVhfState = AiVhfStarting;
    status = AiKeyboardVhfStart(Context);
    Context->KeyboardVhfState = NT_SUCCESS(status) ?
        AiVhfRunning : AiVhfDescriptorsReady;
    WdfWaitLockRelease(Context->FrontendLock);
    return status;
}

NTSTATUS AiVhfFrontendSubmitKeyboard(PAI_DEVICE_CONTEXT Context,
                                     const UCHAR *Report, SIZE_T Length)
{
    NTSTATUS status;

    if (!Context || !Context->FrontendLock)
        return STATUS_INVALID_PARAMETER;
    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (Context->KeyboardVhfState != AiVhfRunning)
        status = STATUS_DEVICE_NOT_READY;
    else
        status = AiKeyboardVhfSubmit(Context, Report, Length);
    WdfWaitLockRelease(Context->FrontendLock);
    return status;
}

VOID AiVhfFrontendStop(PAI_DEVICE_CONTEXT Context)
{
    if (!Context || !Context->FrontendLock)
        return;
    WdfWaitLockAcquire(Context->FrontendLock, NULL);
    if (Context->KeyboardVhfState == AiVhfAbsent) {
        WdfWaitLockRelease(Context->FrontendLock);
        return;
    }
    Context->KeyboardVhfState = AiVhfStopping;
    AiKeyboardVhfStop(Context);
    Context->KeyboardVhfState = AiVhfAbsent;
    WdfWaitLockRelease(Context->FrontendLock);
}
