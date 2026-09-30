#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionCpuQueueSubmit(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args,
    ULONG Kind, const VOID *Data, UINT Bytes) {
  ADMISSION_CPU_PACKET *packet;
  ADMISSION_RENDER_CONTEXT *presentContext = NULL;
  ADMISSION_PRESENT_BLT_COMMAND presentCommand;
  KIRQL oldIrql;
  if (Context == NULL || Args == NULL || Data == NULL || Bytes == 0u ||
      (Kind != ADMISSION_CPU_PACKET_PAGING && Kind != ADMISSION_CPU_PACKET_PRESENT) ||
      (Kind == ADMISSION_CPU_PACKET_PRESENT && Bytes > ADMISSION_PRESENT_BLT_DMA_MAX) ||
      (Kind == ADMISSION_CPU_PACKET_PAGING && Bytes % sizeof(ADMISSION_PAGING_RECORD) != 0u) ||
      Bytes > sizeof(Context->CpuQueue[0].Data) || Context->PagingWorkItem == NULL)
    return STATUS_INVALID_PARAMETER;
  if (Kind == ADMISSION_CPU_PACKET_PRESENT &&
      Bytes >= sizeof(presentCommand)) {
    RtlCopyMemory(&presentCommand, Data, sizeof(presentCommand));
    if (presentCommand.Magic == ADMISSION_PRESENT_BLT_MAGIC &&
        presentCommand.Version == ADMISSION_PRESENT_BLT_GPUVA_VERSION) {
      presentContext = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
      if (presentContext == NULL ||
          presentCommand.ContextToken != (ULONGLONG)(ULONG_PTR)presentContext)
        return STATUS_INVALID_PARAMETER;
    }
  }
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  if (InterlockedCompareExchange(&Context->PagingStopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->SchedulerFaulted, 0, 0) != 0 ||
      (presentContext != NULL && presentContext->Object.FenceOutstanding != 0u) ||
      Context->CpuQueueCount >= APPLE_AGX_SCHEDULER_QUEUE_CAPACITY ||
      !AppleAgxSchedulerQueueFence(&Context->Scheduler, 0u, 0u, Args->SubmissionFenceId)) {
    KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
    KeReleaseSpinLock(&Context->PagingLock, oldIrql);
    return STATUS_DEVICE_BUSY;
  }
  packet = &Context->CpuQueue[(Context->CpuQueueHead + Context->CpuQueueCount) %
      APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
  packet->Fence = Args->SubmissionFenceId;
  packet->Kind = Kind;
  packet->Bytes = Bytes;
  packet->PresentContext = presentContext;
  if (presentContext != NULL)
    presentContext->Object.FenceOutstanding = Args->SubmissionFenceId;
  RtlCopyMemory(&packet->Data, Data, Bytes);
  ++Context->CpuQueueCount;
  if (Kind == ADMISSION_CPU_PACKET_PAGING) {
    LONG records = (LONG)(Bytes / sizeof(ADMISSION_PAGING_RECORD));
    LONG before, after;
    do {
      before = Context->PagingRecordsUnsubmitted;
      after = before > records ? before - records : 0;
    } while (InterlockedCompareExchange(&Context->PagingRecordsUnsubmitted,
                                        after, before) != before);
  }
  KeClearEvent(&Context->PagingIdle);
  KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
  KeReleaseSpinLock(&Context->PagingLock, oldIrql);
  AdmissionDispatchQueuedWork(Context);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *Context) {
  ADMISSION_CPU_PACKET *packet;
  KIRQL oldIrql;
  ULONG fence, attempt = 0u;
  BOOLEAN cpu = FALSE, render = FALSE;
  if (Context == NULL || !Context->Started ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0 ||
      InterlockedCompareExchange(&Context->SchedulerFaulted, 0, 0) != 0)
    return;
Retry:
  cpu = render = FALSE;
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  fence = AppleAgxSchedulerQueuedFence(&Context->Scheduler, 0u, 0u);
  if (fence != 0u && Context->DispatchedFence == 0u &&
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 &&
      AppleAgxSchedulerActiveFence(&Context->Scheduler, 0u, 0u) == 0u &&
      !AppleAgxSchedulerDispatchBlocked(&Context->Scheduler)) {
    packet = Context->CpuQueueCount != 0u ? &Context->CpuQueue[Context->CpuQueueHead] : NULL;
    if (packet != NULL && packet->Fence == fence &&
        AppleAgxSchedulerActivateFence(&Context->Scheduler, 0u, 0u, fence)) {
      Context->PagingRecordCount = 0u;
      Context->PresentCopyBytes = 0u;
      Context->PresentCopyContext = NULL;
      if (packet->Kind == ADMISSION_CPU_PACKET_PRESENT) {
        RtlCopyMemory(Context->PresentCopyCommand, packet->Data.Present, packet->Bytes);
        Context->PresentCopyBytes = packet->Bytes;
        Context->PresentCopyContext = packet->PresentContext;
      } else {
        RtlCopyMemory(Context->PagingRecords, packet->Data.Paging, packet->Bytes);
        Context->PagingRecordCount = packet->Bytes / sizeof(ADMISSION_PAGING_RECORD);
      }
      Context->CpuQueueHead = (Context->CpuQueueHead + 1u) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY;
      --Context->CpuQueueCount;
      Context->PagingFence = fence;
      Context->PagingCompletionStatus = STATUS_PENDING;
      Context->DispatchedFence = fence;
      InterlockedExchange(&Context->PagingPending, 1);
      cpu = TRUE;
    } else if (AdmissionRenderPacketState(&Context->RenderPacket) == AdmissionRenderPacketQueued &&
               Context->RenderPacket.Description.Fence == fence) {
      Context->DispatchedFence = fence;
      render = TRUE;
    }
  }
  KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
  KeReleaseSpinLock(&Context->PagingLock, oldIrql);
  if (cpu)
    AdmissionPagingQueueActive(Context);
  if (render && !AdmissionPlatformRuntimeSubmit(Context)) {
    /* The preceding backend worker may still be returning after completion.
     * Its finished path retries dispatch after releasing WorkScheduled. */
    KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
    if (Context->DispatchedFence == fence)
      Context->DispatchedFence = 0u;
    KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
    /* Close the lost-wakeup window if the preceding worker finished between
     * the rejected enqueue and releasing this dispatch reservation. */
    if (attempt == 0u && AdmissionPlatformRuntimeReady(Context)) {
      attempt = 1u;
      goto Retry;
    }
  }
}

_Use_decl_annotations_ void AdmissionPagingNoteEncoded(ADMISSION_CONTEXT *Context,
                                                       UINT Records) {
  if (Context != NULL && Records != 0u)
    InterlockedExchangeAdd(&Context->PagingRecordsUnsubmitted, (LONG)Records);
}

/* R161: UPDATE_PAGE_TABLE changes the logical mapping when it is built, while
 * FILL/TRANSFER records run later in the paging worker. A CPU copy through the
 * logical mapping is ordered only after every built paging record ran. */
_Use_decl_annotations_ BOOLEAN AdmissionPagingQuiescent(ADMISSION_CONTEXT *Context) {
  BOOLEAN quiescent;
  KIRQL oldIrql;
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  quiescent = Context->PagingRecordsUnsubmitted == 0 &&
      Context->CpuQueueCount == 0u &&
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingWorkersActive, 0, 0) == 0;
  KeReleaseSpinLock(&Context->PagingLock, oldIrql);
  return quiescent;
}
