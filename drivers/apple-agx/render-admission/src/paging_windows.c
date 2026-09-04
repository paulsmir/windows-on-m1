#include "render_admission.h"

static NTSTATUS AdmissionEncodePaging(
    _Inout_ DXGKARG_BUILDPAGINGBUFFER *Args,
    _In_ const APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan, _In_opt_ PMDL Mdl) {
  ADMISSION_PAGING_MARKER marker;
  ADMISSION_PAGING_RECORD record;
  if (Args->pDmaBuffer == NULL || Args->pDmaBufferPrivateData == NULL ||
      Args->DmaSize < sizeof(marker) ||
      Args->DmaBufferPrivateDataSize < sizeof(record))
    return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
  RtlZeroMemory(&marker, sizeof(marker));
  marker.Magic = ADMISSION_PAGING_MAGIC;
  marker.Version = ADMISSION_PAGING_VERSION;
  marker.RecordBytes = sizeof(record);
  RtlZeroMemory(&record, sizeof(record));
  record.Header = marker;
  record.Plan = *Plan;
  record.SystemMdl = Mdl;
  RtlCopyMemory(Args->pDmaBuffer, &marker, sizeof(marker));
  RtlCopyMemory(Args->pDmaBufferPrivateData, &record, sizeof(record));
  Args->pDmaBuffer = (PUCHAR)Args->pDmaBuffer + sizeof(marker);
  Args->DmaSize -= sizeof(marker);
  Args->pDmaBufferPrivateData =
      (PUCHAR)Args->pDmaBufferPrivateData + sizeof(record);
  Args->DmaBufferPrivateDataSize -= sizeof(record);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiBuildPagingBuffer(
    HANDLE Adapter, DXGKARG_BUILDPAGINGBUFFER *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  APPLE_AGX_PHYSICAL_PAGING_PLAN plan;
  APPLE_AGX_PHYSICAL_PAGING_RESULT result;
  PMDL mdl;
  ULONGLONG systemOffset;
  if (context == NULL || Args == NULL ||
      context->Memory.Initialized != APPLE_AGX_TRUE ||
      context->Memory.UatReady != APPLE_AGX_TRUE)
    return STATUS_INVALID_DEVICE_STATE;
  switch (Args->Operation) {
  case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
    if (Args->MapApertureSegment.SegmentId !=
            ADMISSION_MEMORY_APERTURE_SEGMENT ||
        Args->MapApertureSegment.OffsetInPages >
            (MAXULONGLONG >> PAGE_SHIFT) ||
        Args->MapApertureSegment.NumberOfPages > MAXULONG)
      return STATUS_INVALID_PARAMETER;
    return AdmissionMemoryRuntimeMapAperture(
        context,
        (ULONGLONG)Args->MapApertureSegment.OffsetInPages << PAGE_SHIFT,
        Args->MapApertureSegment.pMdl,
        Args->MapApertureSegment.MdlOffset,
        (UINT)Args->MapApertureSegment.NumberOfPages);
  case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
    if (Args->UnmapApertureSegment.SegmentId !=
            ADMISSION_MEMORY_APERTURE_SEGMENT ||
        Args->UnmapApertureSegment.OffsetInPages >
            (MAXULONGLONG >> PAGE_SHIFT) ||
        Args->UnmapApertureSegment.NumberOfPages !=
            (SIZE_T)APPLE_AGX_SYSTEM_PAGES_PER_WDDM_PAGE)
      return STATUS_INVALID_PARAMETER;
    return AdmissionMemoryRuntimeUnmapAperture(
        context,
        (ULONGLONG)Args->UnmapApertureSegment.OffsetInPages << PAGE_SHIFT,
        (ULONGLONG)Args->UnmapApertureSegment.DummyPage.QuadPart);
  case DXGK_OPERATION_TRANSFER:
    if (Args->Transfer.Flags.Reserved != 0u ||
        Args->Transfer.Flags.Swizzle || Args->Transfer.Flags.Unswizzle ||
        Args->Transfer.TransferSize == 0u ||
        Args->Transfer.MdlOffset > (MAXULONGLONG >> PAGE_SHIFT))
      return STATUS_INVALID_PARAMETER;
    if (Args->Transfer.Source.SegmentId == 0u)
      mdl = Args->Transfer.Source.pMdl;
    else if (Args->Transfer.Destination.SegmentId == 0u)
      mdl = Args->Transfer.Destination.pMdl;
    else
      mdl = NULL;
    if (mdl == NULL)
      return STATUS_INVALID_PARAMETER;
    systemOffset = (ULONGLONG)Args->Transfer.MdlOffset << PAGE_SHIFT;
    result = AdmissionMemoryPlanTransfer(
        &context->Memory, Args->Transfer.Source.SegmentId,
        (ULONGLONG)Args->Transfer.Source.SegmentAddress.QuadPart,
        Args->Transfer.Destination.SegmentId,
        (ULONGLONG)Args->Transfer.Destination.SegmentAddress.QuadPart,
        Args->Transfer.TransferOffset, systemOffset,
        Args->Transfer.TransferSize, &plan);
    break;
  case DXGK_OPERATION_FILL:
    result = AdmissionMemoryPlanFill(
        &context->Memory, Args->Fill.Destination.SegmentId,
        (ULONGLONG)Args->Fill.Destination.SegmentAddress.QuadPart,
        Args->Fill.FillSize, Args->Fill.FillPattern, &plan);
    mdl = NULL;
    break;
  case DXGK_OPERATION_DISCARD_CONTENT:
    if (Args->DiscardContent.Flags.Reserved != 0u)
      return STATUS_INVALID_PARAMETER;
    result = AdmissionMemoryPlanDiscard(
        &context->Memory, Args->DiscardContent.SegmentId,
        (ULONGLONG)Args->DiscardContent.SegmentAddress.QuadPart, &plan);
    mdl = NULL;
    break;
  default:
    return STATUS_NOT_SUPPORTED;
  }
  if (result == AppleAgxPhysicalPagingOutOfRange)
    return STATUS_INVALID_ADDRESS;
  if (result == AppleAgxPhysicalPagingUnsupportedEndpoint)
    return STATUS_NOT_SUPPORTED;
  if (result != AppleAgxPhysicalPagingOk)
    return STATUS_INVALID_PARAMETER;
  return AdmissionEncodePaging(Args, &plan, mdl);
}

typedef struct _ADMISSION_PAGING_NOTIFICATION {
  ADMISSION_CONTEXT *Context;
  UINT Fence;
  NTSTATUS Status;
} ADMISSION_PAGING_NOTIFICATION;

static BOOLEAN AdmissionPagingNotifyAtInterrupt(PVOID Opaque) {
  ADMISSION_PAGING_NOTIFICATION *notification =
      (ADMISSION_PAGING_NOTIFICATION *)Opaque;
  DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
  ADMISSION_CONTEXT *context;
  if (notification == NULL || notification->Context == NULL)
    return FALSE;
  context = notification->Context;
  if (!context->InterfaceValid ||
      context->Interface.DxgkCbNotifyInterrupt == NULL ||
      context->Interface.DxgkCbQueueDpc == NULL)
    return FALSE;
  RtlZeroMemory(&data, sizeof(data));
  if (NT_SUCCESS(notification->Status)) {
    data.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
    data.DmaCompleted.SubmissionFenceId = notification->Fence;
    data.DmaCompleted.NodeOrdinal = 0u;
    data.DmaCompleted.EngineOrdinal = 0u;
  } else {
    data.InterruptType = DXGK_INTERRUPT_DMA_FAULTED;
    data.DmaFaulted.FaultedFenceId = notification->Fence;
    data.DmaFaulted.Status = notification->Status;
    data.DmaFaulted.NodeOrdinal = 0u;
    data.DmaFaulted.EngineOrdinal = 0u;
  }
  context->Interface.DxgkCbNotifyInterrupt(
      context->Interface.DeviceHandle, &data);
  InterlockedExchange(&context->PagingDpcPending, 1);
  (void)context->Interface.DxgkCbQueueDpc(
      context->Interface.DeviceHandle);
  return TRUE;
}

static VOID AdmissionPagingWorker(_In_ PDEVICE_OBJECT DeviceObject,
                                  _In_opt_ PVOID Opaque) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Opaque;
  ADMISSION_PAGING_NOTIFICATION notification;
  BOOLEAN synchronized = FALSE;
  NTSTATUS status = STATUS_SUCCESS;
  ULONG index;

  UNREFERENCED_PARAMETER(DeviceObject);
  if (context == NULL)
    return;
  for (index = 0u; index < context->PagingRecordCount; ++index) {
    status = AdmissionMemoryRuntimeExecutePaging(
        context, &context->PagingRecords[index]);
    if (!NT_SUCCESS(status))
      break;
  }
  if (NT_SUCCESS(status) &&
      !AdmissionSchedulerRecordCompletion(context, context->PagingFence))
    status = STATUS_INVALID_DEVICE_STATE;
  context->PagingCompletionStatus = status;
  notification.Context = context;
  notification.Fence = context->PagingFence;
  notification.Status = status;
  status = context->Interface.DxgkCbSynchronizeExecution(
      context->Interface.DeviceHandle, AdmissionPagingNotifyAtInterrupt,
      &notification, 0u, &synchronized);
  if (!NT_SUCCESS(status) || !synchronized) {
    InterlockedExchange(&context->PagingDpcPending, 0);
    InterlockedExchange(&context->PagingPending, 0);
    KeSetEvent(&context->PagingIdle, IO_NO_INCREMENT, FALSE);
  }
}

_Use_decl_annotations_ NTSTATUS AdmissionPagingStart(
    ADMISSION_CONTEXT *Context) {
  if (Context == NULL || Context->PagingWorkItem != NULL ||
      Context->PhysicalDeviceObject == NULL || !Context->InterfaceValid ||
      Context->Interface.DxgkCbSynchronizeExecution == NULL ||
      Context->Interface.DxgkCbNotifyInterrupt == NULL ||
      Context->Interface.DxgkCbQueueDpc == NULL ||
      Context->Interface.DxgkCbNotifyDpc == NULL ||
      Context->Memory.Initialized != APPLE_AGX_TRUE ||
      Context->Memory.UatReady != APPLE_AGX_TRUE)
    return STATUS_INVALID_DEVICE_STATE;
  KeInitializeSpinLock(&Context->PagingLock);
  KeInitializeEvent(&Context->PagingIdle, NotificationEvent, TRUE);
  Context->PagingWorkItem = IoAllocateWorkItem(Context->PhysicalDeviceObject);
  if (Context->PagingWorkItem == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  Context->PagingRecordCount = 0u;
  Context->PagingFence = 0u;
  Context->PagingLastSubmittedFence = 0u;
  Context->PagingLastCompletedFence = 0u;
  Context->PagingCompletionStatus = STATUS_SUCCESS;
  InterlockedExchange(&Context->PagingPending, 0);
  InterlockedExchange(&Context->PagingStopping, 0);
  InterlockedExchange(&Context->PagingDpcPending, 0);
  if (!AdmissionMemoryMarkPagingReady(&Context->Memory)) {
    IoFreeWorkItem(Context->PagingWorkItem);
    Context->PagingWorkItem = NULL;
    return STATUS_INVALID_DEVICE_STATE;
  }
  (void)InterlockedOr(&Context->FeatureReadyMask,
                      APPLE_AGX_WDDM_READY_MEMORY_PAGING);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionPagingStop(
    ADMISSION_CONTEXT *Context) {
  LARGE_INTEGER timeout;
  NTSTATUS status;
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  if (Context->PagingWorkItem == NULL)
    return STATUS_SUCCESS;
  InterlockedExchange(&Context->PagingStopping, 1);
  timeout.QuadPart = -20000000LL;
  status = KeWaitForSingleObject(&Context->PagingIdle, Executive, KernelMode,
                                 FALSE, &timeout);
  if (!NT_SUCCESS(status) ||
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingDpcPending, 0, 0) != 0)
    return STATUS_DEVICE_BUSY;
  IoFreeWorkItem(Context->PagingWorkItem);
  Context->PagingWorkItem = NULL;
  Context->PagingRecordCount = 0u;
  (void)InterlockedAnd(&Context->FeatureReadyMask,
                       ~((LONG)APPLE_AGX_WDDM_READY_MEMORY_PAGING));
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID AdmissionPagingDpc(
    ADMISSION_CONTEXT *Context) {
  if (Context == NULL ||
      InterlockedExchange(&Context->PagingDpcPending, 0) == 0)
    return;
  if (Context->InterfaceValid &&
      Context->Interface.DxgkCbNotifyDpc != NULL)
    Context->Interface.DxgkCbNotifyDpc(
        Context->Interface.DeviceHandle);
  if (NT_SUCCESS(Context->PagingCompletionStatus))
    Context->PagingLastCompletedFence = Context->PagingFence;
  Context->PagingRecordCount = 0u;
  InterlockedExchange(&Context->PagingPending, 0);
  KeSetEvent(&Context->PagingIdle, IO_NO_INCREMENT, FALSE);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSubmitCommand(
    HANDLE Adapter, const DXGKARG_SUBMITCOMMAND *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  const ADMISSION_PAGING_RECORD *records;
  SIZE_T privateBytes;
  UINT dmaBytes;
  UINT count;
  KIRQL oldIrql;

  if (context == NULL || Args == NULL || !context->Started ||
      !Args->Flags.Paging || Args->Flags.Reserved != 0u ||
      Args->NodeOrdinal != 0u || Args->EngineOrdinal != 0u ||
      Args->SubmissionFenceId == 0u ||
      context->PagingWorkItem == NULL ||
      Args->pDmaBufferPrivateData == NULL ||
      Args->DmaBufferSubmissionStartOffset >
          Args->DmaBufferSubmissionEndOffset ||
      Args->DmaBufferSubmissionEndOffset > Args->DmaBufferSize ||
      Args->DmaBufferPrivateDataSubmissionStartOffset >
          Args->DmaBufferPrivateDataSubmissionEndOffset ||
      Args->DmaBufferPrivateDataSubmissionEndOffset >
          Args->DmaBufferPrivateDataSize)
    return STATUS_INVALID_PARAMETER;
  privateBytes = Args->DmaBufferPrivateDataSubmissionEndOffset -
                 Args->DmaBufferPrivateDataSubmissionStartOffset;
  dmaBytes = Args->DmaBufferSubmissionEndOffset -
             Args->DmaBufferSubmissionStartOffset;
  if (privateBytes == 0u ||
      privateBytes % sizeof(ADMISSION_PAGING_RECORD) != 0u)
    return STATUS_INVALID_PARAMETER;
  count = (UINT)(privateBytes / sizeof(ADMISSION_PAGING_RECORD));
  if (count == 0u || count > ADMISSION_MAX_PAGING_RECORDS ||
      count > MAXULONG / sizeof(ADMISSION_PAGING_MARKER) ||
      dmaBytes != count * sizeof(ADMISSION_PAGING_MARKER))
    return STATUS_INVALID_PARAMETER;
  records = (const ADMISSION_PAGING_RECORD *)(
      (const UCHAR *)Args->pDmaBufferPrivateData +
      Args->DmaBufferPrivateDataSubmissionStartOffset);
  if (!AdmissionPagingRecordsValid(records, count,
                                   ADMISSION_MAX_PAGING_RECORDS, dmaBytes))
    return STATUS_INVALID_PARAMETER;

  KeAcquireSpinLock(&context->PagingLock, &oldIrql);
  if (InterlockedCompareExchange(&context->PagingStopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&context->PagingPending, 0, 0) != 0 ||
      !AdmissionPagingFenceCanSubmit(
          context->PagingLastSubmittedFence, Args->SubmissionFenceId,
          Args->Flags.Resubmission ? 1u : 0u)) {
    KeReleaseSpinLock(&context->PagingLock, oldIrql);
    return STATUS_DEVICE_BUSY;
  }
  InterlockedExchange(&context->PagingPending, 1);
  RtlCopyMemory(context->PagingRecords, records,
                count * sizeof(*records));
  context->PagingRecordCount = count;
  context->PagingFence = Args->SubmissionFenceId;
  context->PagingLastSubmittedFence = Args->SubmissionFenceId;
  context->PagingCompletionStatus = STATUS_PENDING;
  KeClearEvent(&context->PagingIdle);
  KeReleaseSpinLock(&context->PagingLock, oldIrql);
  IoQueueWorkItem(context->PagingWorkItem, AdmissionPagingWorker,
                  DelayedWorkQueue, context);
  return STATUS_SUCCESS;
}
