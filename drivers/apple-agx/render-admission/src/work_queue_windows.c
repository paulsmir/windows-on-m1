#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionCpuQueueSubmit(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args,
    ULONG Kind, const VOID *Data, UINT Bytes) {
  ADMISSION_CPU_PACKET *packet;
  KIRQL oldIrql;
  if (Context == NULL || Args == NULL || Data == NULL || Bytes == 0u ||
      (Kind != ADMISSION_CPU_PACKET_PAGING && Kind != ADMISSION_CPU_PACKET_PRESENT) ||
      (Kind == ADMISSION_CPU_PACKET_PRESENT && Bytes > ADMISSION_PRESENT_BLT_DMA_MAX) ||
      (Kind == ADMISSION_CPU_PACKET_PAGING && Bytes % sizeof(ADMISSION_PAGING_RECORD) != 0u) ||
      Bytes > sizeof(Context->CpuQueue[0].Data) || Context->PagingWorkItem == NULL)
    return STATUS_INVALID_PARAMETER;
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  if (InterlockedCompareExchange(&Context->PagingStopping, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->SchedulerFaulted, 0, 0) != 0 ||
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
  RtlCopyMemory(&packet->Data, Data, Bytes);
  ++Context->CpuQueueCount;
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
      if (packet->Kind == ADMISSION_CPU_PACKET_PRESENT) {
        RtlCopyMemory(Context->PresentCopyCommand, packet->Data.Present, packet->Bytes);
        Context->PresentCopyBytes = packet->Bytes;
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
