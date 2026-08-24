#include "apple_input_device.h"

#define AI_PTP_VHF_VENDOR_ID 0x05acu
#define AI_PTP_VHF_PRODUCT_ID 0x0000u
#define AI_PTP_VHF_VERSION 0x0001u

static VOID AiTrackpadCounterIncrement(ULONGLONG *Counter)
{
    InterlockedIncrement64((volatile LONG64 *)Counter);
}

static SIZE_T AiTrackpadFeatureReportSize(UCHAR ReportId)
{
    switch (ReportId) {
    case AI_PTP_REPORT_CAPABILITIES:
        return AI_PTP_CAPABILITIES_REPORT_SIZE;
    case AI_PTP_REPORT_CERTIFICATION:
        return AI_PTP_CERTIFICATION_REPORT_SIZE;
    case AI_PTP_REPORT_INPUT_MODE:
        return AI_PTP_INPUT_MODE_REPORT_SIZE;
    case AI_PTP_REPORT_SELECTIVE:
        return AI_PTP_SELECTIVE_REPORT_SIZE;
    default:
        return 0;
    }
}

static NTSTATUS AiTrackpadProtocolStatus(enum ai_status Status)
{
    switch (Status) {
    case AI_OK:
        return STATUS_SUCCESS;
    case AI_ERR_ARGUMENT:
        return STATUS_INVALID_PARAMETER;
    case AI_ERR_LENGTH:
        return STATUS_INVALID_BUFFER_SIZE;
    case AI_ERR_PROTOCOL:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_UNSUCCESSFUL;
    }
}

VOID AiTrackpadVhfGetFeature(
    PVOID VhfClientContext,
    VHFOPERATIONHANDLE VhfOperationHandle,
    PVOID VhfOperationContext,
    PHID_XFER_PACKET HidTransferPacket)
{
    PAI_DEVICE_CONTEXT Context = (PAI_DEVICE_CONTEXT)VhfClientContext;
    struct ai_ptp_feature_state features;
    enum ai_status protocol_status;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    SIZE_T expected;
    size_t length = 0;
    KIRQL old_irql;

    UNREFERENCED_PARAMETER(VhfOperationContext);
    if (!Context || !HidTransferPacket ||
        !HidTransferPacket->reportBuffer)
        goto Complete;
    expected = AiTrackpadFeatureReportSize(HidTransferPacket->reportId);
    if (!expected) {
        status = STATUS_NOT_SUPPORTED;
        goto Complete;
    }
    if (HidTransferPacket->reportBufferLen != expected) {
        status = STATUS_INVALID_BUFFER_SIZE;
        goto Complete;
    }

    KeAcquireSpinLock(&Context->TrackpadVhf.FeatureLock, &old_irql);
    features = Context->TrackpadVhf.Features;
    KeReleaseSpinLock(&Context->TrackpadVhf.FeatureLock, old_irql);
    protocol_status = ai_ptp_get_feature(
        &features, HidTransferPacket->reportId,
        HidTransferPacket->reportBuffer,
        HidTransferPacket->reportBufferLen, &length);
    status = AiTrackpadProtocolStatus(protocol_status);
    if (NT_SUCCESS(status))
        HidTransferPacket->reportBufferLen = (ULONG)length;

Complete:
    if (Context) {
        AiTrackpadCounterIncrement(
            &Context->Diagnostics.TrackpadGetFeatureCount);
        Context->Diagnostics.TrackpadFeatureLastStatus = status;
    }
    (VOID)VhfAsyncOperationComplete(VhfOperationHandle, status);
}

VOID AiTrackpadVhfSetFeature(
    PVOID VhfClientContext,
    VHFOPERATIONHANDLE VhfOperationHandle,
    PVOID VhfOperationContext,
    PHID_XFER_PACKET HidTransferPacket)
{
    PAI_DEVICE_CONTEXT Context = (PAI_DEVICE_CONTEXT)VhfClientContext;
    enum ai_status protocol_status;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    SIZE_T expected;
    bool neutral_required = false;
    KIRQL old_irql;

    UNREFERENCED_PARAMETER(VhfOperationContext);
    if (!Context || !HidTransferPacket ||
        !HidTransferPacket->reportBuffer)
        goto Complete;
    expected = AiTrackpadFeatureReportSize(HidTransferPacket->reportId);
    if (expected != AI_PTP_INPUT_MODE_REPORT_SIZE &&
        expected != AI_PTP_SELECTIVE_REPORT_SIZE) {
        status = STATUS_NOT_SUPPORTED;
        goto Complete;
    }
    if (HidTransferPacket->reportBufferLen != expected ||
        HidTransferPacket->reportBuffer[0] != HidTransferPacket->reportId) {
        status = STATUS_INVALID_BUFFER_SIZE;
        goto Complete;
    }

    KeAcquireSpinLock(&Context->TrackpadVhf.FeatureLock, &old_irql);
    protocol_status = ai_ptp_set_feature(
        &Context->TrackpadVhf.Features, HidTransferPacket->reportId,
        HidTransferPacket->reportBuffer,
        HidTransferPacket->reportBufferLen,
        Context->TrackpadVhf.ContactsActive ? true : false,
        &neutral_required);
    if (protocol_status == AI_OK && neutral_required)
        Context->TrackpadVhf.NeutralRequired = TRUE;
    KeReleaseSpinLock(&Context->TrackpadVhf.FeatureLock, old_irql);
    status = AiTrackpadProtocolStatus(protocol_status);

Complete:
    if (Context) {
        AiTrackpadCounterIncrement(
            &Context->Diagnostics.TrackpadSetFeatureCount);
        Context->Diagnostics.TrackpadFeatureLastStatus = status;
    }
    (VOID)VhfAsyncOperationComplete(VhfOperationHandle, status);
}

NTSTATUS AiTrackpadVhfStart(PAI_DEVICE_CONTEXT Context)
{
    VHF_CONFIG config;
    VHFHANDLE handle = NULL;
    NTSTATUS status;

    PAGED_CODE();
    if (!Context || !Context->Device)
        return STATUS_INVALID_PARAMETER;
    if (Context->TrackpadInit.phase != AI_TRACKPAD_INIT_READY)
        return STATUS_DEVICE_NOT_READY;
    if (Context->TrackpadVhf.Handle)
        return STATUS_SUCCESS;

    if (!Context->TrackpadAxisContract.valid)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (!AiPrecisionTouchpadDescriptorPatch(
            Context->TrackpadVhf.ReportDescriptor,
            sizeof(Context->TrackpadVhf.ReportDescriptor),
            &Context->TrackpadAxisContract))
        return STATUS_DEVICE_PROTOCOL_ERROR;

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

    KeInitializeSpinLock(&Context->TrackpadVhf.FeatureLock);
    ai_ptp_feature_init(&Context->TrackpadVhf.Features);
    Context->TrackpadVhf.ContactsActive = FALSE;
    Context->TrackpadVhf.NeutralRequired = FALSE;
    Context->TrackpadVhf.Running = FALSE;

    VHF_CONFIG_INIT(
        &config, WdfDeviceWdmGetDeviceObject(Context->Device),
        (USHORT)sizeof(Context->TrackpadVhf.ReportDescriptor),
        Context->TrackpadVhf.ReportDescriptor);
    config.VhfClientContext = Context;
    config.EvtVhfAsyncOperationGetFeature = AiTrackpadVhfGetFeature;
    config.EvtVhfAsyncOperationSetFeature = AiTrackpadVhfSetFeature;
    config.VendorID = AI_PTP_VHF_VENDOR_ID;
    config.ProductID = AI_PTP_VHF_PRODUCT_ID;
    config.VersionNumber = AI_PTP_VHF_VERSION;
    status = VhfCreate(&config, &handle);
    if (!NT_SUCCESS(status))
        return status;
    Context->TrackpadVhf.Handle = handle;
    status = VhfStart(handle);
    if (!NT_SUCCESS(status)) {
        Context->TrackpadVhf.Handle = NULL;
        VhfDelete(handle, TRUE);
        return status;
    }
    Context->TrackpadVhf.Running = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS AiTrackpadVhfSubmit(PAI_DEVICE_CONTEXT Context,
                             const UCHAR *Report, SIZE_T Length)
{
    HID_XFER_PACKET packet;

    if (!Context || !Context->TrackpadVhf.Handle ||
        !Context->TrackpadVhf.Running)
        return STATUS_DEVICE_NOT_READY;
    if (!Report || Length != AI_PTP_INPUT_REPORT_SIZE ||
        Report[0] != AI_PTP_REPORT_INPUT)
        return STATUS_INVALID_BUFFER_SIZE;
    RtlZeroMemory(&packet, sizeof(packet));
    packet.reportBuffer = (PUCHAR)Report;
    packet.reportBufferLen = (ULONG)Length;
    packet.reportId = AI_PTP_REPORT_INPUT;
    return VhfReadReportSubmit(Context->TrackpadVhf.Handle, &packet);
}

VOID AiTrackpadVhfStop(PAI_DEVICE_CONTEXT Context)
{
    VHFHANDLE handle;

    PAGED_CODE();
    if (!Context)
        return;
    handle = Context->TrackpadVhf.Handle;
    Context->TrackpadVhf.Running = FALSE;
    Context->TrackpadVhf.Handle = NULL;
    if (handle)
        VhfDelete(handle, TRUE);
}
