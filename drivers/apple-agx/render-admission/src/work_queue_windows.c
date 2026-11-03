#include "render_admission.h"

_Use_decl_annotations_ NTSTATUS AdmissionCpuQueueSubmit(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args,
    ULONG Kind, const VOID *Data, UINT Bytes) {
  ADMISSION_CPU_PACKET *packet;
  APPLE_AGX_SCHEDULER *scheduler;
  ADMISSION_CPU_PACKET *queue;
  ULONG *head, *count, node;
  ADMISSION_RENDER_CONTEXT *presentContext = NULL;
  ADMISSION_PRESENT_BLT_COMMAND presentCommand;
  KIRQL oldIrql;
  BOOLEAN queued, preempted, paging;
  if (Context == NULL || Args == NULL ||
      (Kind == ADMISSION_CPU_PACKET_NOP ? (Data != NULL || Bytes != 0u)
                                        : (Data == NULL || Bytes == 0u)) ||
      (Kind != ADMISSION_CPU_PACKET_PAGING && Kind != ADMISSION_CPU_PACKET_PRESENT &&
       Kind != ADMISSION_CPU_PACKET_NOP) ||
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
  node = Kind == ADMISSION_CPU_PACKET_PAGING && Context->LiveAdditivePaging ? 1u : 0u;
  if (Args->NodeOrdinal != node) return STATUS_INVALID_PARAMETER;
  scheduler = node ? &Context->PagingScheduler : &Context->Scheduler;
  queue = node ? Context->PagingNodeQueue : Context->CpuQueue;
  head = node ? &Context->PagingNodeHead : &Context->CpuQueueHead;
  count = node ? &Context->PagingNodeCount : &Context->CpuQueueCount;
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  queued = preempted = FALSE;
  paging = Kind == ADMISSION_CPU_PACKET_PAGING ? TRUE : FALSE;
  /* RC59 (EXP1969/1976/1978/1980 dumps): dxgmms2 submits system paging
   * packets with allow-failure clear, so any refusal here becomes 0x119/2
   * (VidSchiSubmitPagingCommand -> VidSchiSendToExecutionQueue). A frozen
   * (SchedulerFaulted) scheduler therefore still accepts a valid paging
   * packet: it is held, never dispatched (AdmissionDispatchQueuedWork stops
   * while faulted), and the engine reset that dxgkrnl's TDR issues for the
   * unfinished fence retires the queue and keeps the fence resubmittable.
   * Present/NOP refusals stay fallible device errors. */
  if (InterlockedCompareExchange(&Context->PagingStopping, 0, 0) == 0 &&
      (paging ||
       InterlockedCompareExchange(&Context->SchedulerFaulted, 0, 0) == 0) &&
      (presentContext == NULL || presentContext->Object.FenceOutstanding == 0u) &&
      *count < APPLE_AGX_SCHEDULER_QUEUE_CAPACITY) {
    if (paging && Args->Flags.Resubmission)
      queued = AppleAgxSchedulerQueueResubmittedPagingFence(
          scheduler, 0u, 0u, Args->SubmissionFenceId);
    if (!queued)
      queued = AppleAgxSchedulerQueueFence(scheduler, 0u, 0u,
                                           Args->SubmissionFenceId);
    /* An unclaimed boundary preemption refuses admission; the packet joins
     * that preemption instead and Windows resubmits it after DMA_PREEMPTED. */
    if (!queued && paging)
      preempted = AppleAgxSchedulerAdmitPagingFenceAsPreempted(
          scheduler, 0u, 0u, Args->SubmissionFenceId,
          Args->Flags.Resubmission ? TRUE : FALSE);
  }
  if (preempted) {
    /* No packet is stored: its records stay unsubmitted (R161 quiescence)
     * until the resubmission queues them. */
    KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
    KeReleaseSpinLock(&Context->PagingLock, oldIrql);
    return STATUS_SUCCESS;
  }
  if (!queued) {
    KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
    KeReleaseSpinLock(&Context->PagingLock, oldIrql);
    return STATUS_DEVICE_BUSY;
  }
  packet = &queue[(*head + *count) %
      APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
  packet->Fence = Args->SubmissionFenceId;
  packet->SubmitQpc = KeQueryPerformanceCounter(NULL).QuadPart;
  packet->Kind = Kind;
  packet->Bytes = Bytes;
  packet->PresentContext = presentContext;
  if (presentContext != NULL)
    presentContext->Object.FenceOutstanding = Args->SubmissionFenceId;
  if (Bytes != 0u) RtlCopyMemory(&packet->Data, Data, Bytes);
  ++*count;
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
  ULONG node = 0u;
  APPLE_AGX_SCHEDULER *scheduler;
  ADMISSION_CPU_PACKET *queue;
  ULONG *head, *count, *dispatched;
  if (Context == NULL || !Context->Started ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0 ||
      InterlockedCompareExchange(&Context->SchedulerFaulted, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingAdapterResetting, 0, 0) != 0)
    return;
Retry:
  cpu = render = FALSE;
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  /* One CPU slot owns records/present storage until worker and DPC retire.
   * Node1 can retire paging while node0 GPU work remains active. */
  node = Context->LiveAdditivePaging && Context->PagingNodeCount != 0u &&
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingWorkersActive, 0, 0) == 0 ? 1u : 0u;
  scheduler = node ? &Context->PagingScheduler : &Context->Scheduler;
  queue = node ? Context->PagingNodeQueue : Context->CpuQueue;
  head = node ? &Context->PagingNodeHead : &Context->CpuQueueHead;
  count = node ? &Context->PagingNodeCount : &Context->CpuQueueCount;
  dispatched = node ? &Context->PagingNodeDispatchedFence : &Context->DispatchedFence;
  fence = AppleAgxSchedulerQueuedFence(scheduler, 0u, 0u);
  if (fence != 0u && *dispatched == 0u &&
      (InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 ||
       Context->PagingActiveNode != node) &&
      AppleAgxSchedulerActiveFence(scheduler, 0u, 0u) == 0u &&
      !AppleAgxSchedulerDispatchBlocked(scheduler)) {
    packet = *count != 0u ? &queue[*head] : NULL;
    if (packet != NULL && packet->Fence == fence &&
        InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 &&
        InterlockedCompareExchange(&Context->PagingWorkersActive, 0, 0) == 0 &&
        InterlockedCompareExchange(&Context->PagingDpcsActive, 0, 0) == 0 &&
        AppleAgxSchedulerActivateFence(scheduler, 0u, 0u, fence)) {
      Context->PagingRecordCount = 0u;
      Context->PresentCopyBytes = 0u;
      Context->PresentCopyContext = NULL;
      if (packet->Kind == ADMISSION_CPU_PACKET_PRESENT) {
        RtlCopyMemory(Context->PresentCopyCommand, packet->Data.Present, packet->Bytes);
        Context->PresentCopyBytes = packet->Bytes;
        Context->PresentCopyContext = packet->PresentContext;
      } else if (packet->Kind == ADMISSION_CPU_PACKET_PAGING) {
        RtlCopyMemory(Context->PagingRecords, packet->Data.Paging, packet->Bytes);
        Context->PagingRecordCount = packet->Bytes / sizeof(ADMISSION_PAGING_RECORD);
      }
      Context->PagingDispatchQpc = KeQueryPerformanceCounter(NULL).QuadPart;
      if (packet->Kind == ADMISSION_CPU_PACKET_PAGING)
        AdmissionPagingProfileAdd(Context->PagingProfile.QueueToDispatch,
                                  Context->PagingDispatchQpc - packet->SubmitQpc);
      *head = (*head + 1u) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY;
      --*count;
      Context->PagingActiveNode = node;
      Context->PagingFence = fence;
      Context->PagingCompletionStatus = STATUS_PENDING;
      *dispatched = fence;
      InterlockedExchange(&Context->PagingPending, 1);
      cpu = TRUE;
    } else if (!node && ((AdmissionRenderPacketState(&Context->RenderPacket) == AdmissionRenderPacketQueued &&
                Context->RenderPacket.Description.Fence == fence) ||
               /* Phase 5a: the worker binds a pending head into the empty slot. */
               (AdmissionRenderPacketState(&Context->RenderPacket) == AdmissionRenderPacketEmpty &&
                Context->G4PendingCount != 0u &&
                Context->G4Pending[Context->G4PendingHead].State == AdmissionG4PendingQueued &&
                Context->G4Pending[Context->G4PendingHead].Fence == fence))) {
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
      Context->CpuQueueCount == 0u && Context->PagingNodeCount == 0u &&
      InterlockedCompareExchange(&Context->PagingCallbacksActive, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingWorkersActive, 0, 0) == 0;
  KeReleaseSpinLock(&Context->PagingLock, oldIrql);
  return quiescent;
}
