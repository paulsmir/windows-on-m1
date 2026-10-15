#include "render_admission.h"

/* Fault diagnostics: first nonzero transition stores file tag 1 and
 * source line. Consumers retain zero/nonzero semantics; reset clears it. */

#define ADMISSION_SCHEDULER_NODE 0u
#define ADMISSION_SCHEDULER_ENGINE 0u

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, AdmissionDdiQueryDependentEngineGroup)
#pragma alloc_text(PAGE, AdmissionDdiQueryEngineStatus)
#pragma alloc_text(PAGE, AdmissionDdiResetEngine)
#endif

typedef struct _ADMISSION_PREEMPTION_NOTIFICATION {
  ADMISSION_CONTEXT *Context;
  APPLE_AGX_PREEMPTION Preemption;
} ADMISSION_PREEMPTION_NOTIFICATION;

typedef struct _ADMISSION_SCHEDULER_DEBUG_SNAPSHOT {
  ULONG Version;
  ULONG Size;
  ULONG CompletedFence;
  ULONG LastSubmittedFence;
  ULONG ActiveFence;
  ULONG PreemptionPhase;
  ULONG ContextCount;
  ULONG Flags;
  /* Version 2 (G4-r3): the EXP1296 minidump blackbox held only the Faulted
   * flag; the code names its setter (file tag << 16 | line). */
  ULONG SchedulerFaultedCode;
  ULONG RenderPacketState;
  ULONG RenderPacketFence;
  ULONG PlatformState; /* AdmissionPlatformRuntimeDebugState */
  ULONG Resets, ResetsSucceeded;
} ADMISSION_SCHEDULER_DEBUG_SNAPSHOT;

#define ADMISSION_SCHEDULER_DEBUG_VERSION 2u
#define ADMISSION_SCHEDULER_DEBUG_V1_SIZE (8u * sizeof(ULONG))
#define ADMISSION_SCHEDULER_DEBUG_FAULTED (1u << 0)
#define ADMISSION_SCHEDULER_DEBUG_PAGING_PENDING (1u << 1)

static BOOLEAN AdmissionNotifyPreemptionAtInterrupt(_In_ PVOID Opaque) {
  ADMISSION_PREEMPTION_NOTIFICATION *notification =
      (ADMISSION_PREEMPTION_NOTIFICATION *)Opaque;
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
  data.InterruptType = DXGK_INTERRUPT_DMA_PREEMPTED;
  data.DmaPreempted.PreemptionFenceId =
      notification->Preemption.PreemptionFence;
  data.DmaPreempted.LastCompletedFenceId =
      notification->Preemption.LastCompletedFence;
  data.DmaPreempted.NodeOrdinal = ADMISSION_SCHEDULER_NODE;
  data.DmaPreempted.EngineOrdinal = ADMISSION_SCHEDULER_ENGINE;
  context->Interface.DxgkCbNotifyInterrupt(
      context->Interface.DeviceHandle, &data);
  InterlockedExchange(&context->SchedulerDpcPending, 1);
  (void)context->Interface.DxgkCbQueueDpc(
      context->Interface.DeviceHandle);
  return TRUE;
}

static BOOLEAN AdmissionSchedulerTryNotifyPreemption(
    _Inout_ ADMISSION_CONTEXT *Context) {
  ADMISSION_PREEMPTION_NOTIFICATION notification;
  BOOLEAN synchronized = FALSE;
  BOOLEAN claimed;
  APPLE_AGX_PREEMPTION_PHASE phase;
  NTSTATUS status;
  KIRQL oldIrql;

  if (Context == NULL || !Context->InterfaceValid ||
      Context->Interface.DxgkCbSynchronizeExecution == NULL ||
      Context->Interface.DxgkCbNotifyInterrupt == NULL ||
      Context->Interface.DxgkCbQueueDpc == NULL)
    return FALSE;
  RtlZeroMemory(&notification, sizeof(notification));
  notification.Context = Context;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  /* A discarded queued render can still own a delayed IO work item.
   * Do not offer its slot back to Windows until that callback has retired. */
  if (AdmissionPlatformRenderWorkerScheduled(Context)) {
    KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
    return TRUE; /* Accepted and deferred; WorkerFinished retries. */
  }
  /* Callers decide to retry under this lock, then release it before coming
   * here. Another caller may already own or have completed that notification;
   * a new boundary may even be waiting. These are accepted no-op/deferred
   * attempts, not evidence that the scheduler failed. */
  phase = AppleAgxSchedulerPreemptionPhase(&Context->Scheduler);
  if (phase == AppleAgxPreemptionIdle ||
      phase == AppleAgxPreemptionNotificationClaimed ||
      phase == AppleAgxPreemptionWaitCurrentBoundary) {
    KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
    return TRUE;
  }
  claimed = AppleAgxSchedulerClaimBoundaryPreemption(
                &Context->Scheduler, &notification.Preemption)
                ? TRUE
                : FALSE;
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  if (!claimed)
    return FALSE;

  status = Context->Interface.DxgkCbSynchronizeExecution(
      Context->Interface.DeviceHandle, AdmissionNotifyPreemptionAtInterrupt,
      &notification, 0u, &synchronized);
  if (!NT_SUCCESS(status) || !synchronized) {
    InterlockedCompareExchange(&Context->SchedulerFaulted, 0x10000L | __LINE__, 0);
    return FALSE;
  }

  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  claimed = AppleAgxSchedulerCommitBoundaryPreemption(
                &Context->Scheduler,
                notification.Preemption.PreemptionFence)
                ? TRUE
                : FALSE;
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  if (!claimed)
    InterlockedCompareExchange(&Context->SchedulerFaulted, 0x10000L | __LINE__, 0);
  else
    /* A DPC may already have queued work while delivery still held the
     * dispatch gate. Opening the gate must also supply its missed wakeup. */
    AdmissionDispatchQueuedWork(Context);
  return claimed;
}

_Use_decl_annotations_ VOID AdmissionSchedulerWorkerFinished(
    ADMISSION_CONTEXT *Context) {
  BOOLEAN retry;
  KIRQL oldIrql;
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  retry = AppleAgxSchedulerPreemptionPhase(&Context->Scheduler) ==
              AppleAgxPreemptionReadyToNotify &&
      Context->DispatchedFence == 0u &&
      (InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 ||
       InterlockedCompareExchange(&Context->PagingDpcPending, 0, 0) != 0 ||
       InterlockedCompareExchange(&Context->PagingDpcsActive, 0, 0) != 0);
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  if (retry)
    (void)AdmissionSchedulerTryNotifyPreemption(Context);
}

_Use_decl_annotations_ NTSTATUS AdmissionSchedulerStart(
    ADMISSION_CONTEXT *Context) {
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) != 0)
    return STATUS_INVALID_DEVICE_STATE;
  KeInitializeSpinLock(&Context->SchedulerLock);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  KeInitializeSpinLock(&Context->GdiReceiptLock);
  AdmissionGdiReceiptInitialize(&Context->GdiReceipt);
  InterlockedExchange(&Context->GdiReceiptClaimed, 0);
  InterlockedExchange(&Context->GdiSubmitTraceClaimed, 0);
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  if (!NT_SUCCESS(AdmissionRenderCorrelationStartWindows(Context)))
    return STATUS_INSUFFICIENT_RESOURCES;
#elif defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  /* A diagnostic collector must not change StartDevice's result. */
  (void)AdmissionRenderCorrelationStartWindows(Context);
#endif
  AppleAgxSchedulerInitialize(&Context->Scheduler);
  AdmissionRenderPacketInitialize(&Context->RenderPacket);
  InterlockedExchange(&Context->SchedulerFaulted, 0);
  InterlockedExchange(&Context->SchedulerDpcPending, 0);
  InterlockedExchange(&Context->RenderDpcFence, 0);
  InterlockedExchange(&Context->SchedulerInitialized, 1);
  (void)InterlockedOr(&Context->FeatureReadyMask,
                      APPLE_AGX_WDDM_READY_ONE_NODE_TOPOLOGY);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionSchedulerStop(
    ADMISSION_CONTEXT *Context) {
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  if (InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_SUCCESS;
  if (Context->Scheduler.ContextCount != 0u ||
      AdmissionRenderPacketState(&Context->RenderPacket) !=
          AdmissionRenderPacketEmpty ||
      Context->G4PendingCount != 0u ||
      AppleAgxSchedulerHasOutstandingFence(
          &Context->Scheduler, ADMISSION_SCHEDULER_NODE,
          ADMISSION_SCHEDULER_ENGINE) ||
      Context->CpuQueueCount != 0u ||
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingWorkersActive, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingDpcsActive, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->SchedulerDpcPending, 0, 0) != 0)
    return STATUS_DEVICE_BUSY;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!NT_SUCCESS(AdmissionRenderCorrelationStopWindows(Context)))
    return STATUS_DEVICE_BUSY;
#endif
  (void)InterlockedAnd(&Context->FeatureReadyMask,
                       ~((LONG)APPLE_AGX_WDDM_READY_ONE_NODE_TOPOLOGY));
  InterlockedExchange(&Context->SchedulerInitialized, 0);
  RtlZeroMemory(&Context->Scheduler, sizeof(Context->Scheduler));
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ BOOLEAN AdmissionSchedulerRecordCompletion(
    ADMISSION_CONTEXT *Context, UINT Fence) {
  BOOLEAN completed;
  KIRQL oldIrql;

  if (Context == NULL || Fence == 0u ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0)
    return FALSE;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  completed = AppleAgxSchedulerCompleteActiveFence(
                  &Context->Scheduler, ADMISSION_SCHEDULER_NODE,
                  ADMISSION_SCHEDULER_ENGINE, Fence)
                  ? TRUE
                  : FALSE;
  if (completed && Context->DispatchedFence == Fence)
    Context->DispatchedFence = 0u;
  if (completed &&
      AppleAgxSchedulerPreemptionPhase(&Context->Scheduler) ==
          AppleAgxPreemptionWaitCurrentBoundary)
    completed = AppleAgxSchedulerObserveBoundaryCompletion(
                    &Context->Scheduler, ADMISSION_SCHEDULER_NODE,
                    ADMISSION_SCHEDULER_ENGINE, Fence)
                    ? TRUE
                    : FALSE;
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  if (!completed)
    InterlockedCompareExchange(&Context->SchedulerFaulted, 0x10000L | __LINE__, 0);
  return completed;
}

_Use_decl_annotations_ BOOLEAN AdmissionSchedulerSubmitFence(
    ADMISSION_CONTEXT *Context, UINT Fence) {
  BOOLEAN submitted = FALSE;

  if (Context == NULL || Fence == 0u ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0)
    return FALSE;
  KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
  if (AppleAgxSchedulerQueueFence(
          &Context->Scheduler, ADMISSION_SCHEDULER_NODE,
          ADMISSION_SCHEDULER_ENGINE, Fence) &&
      AppleAgxSchedulerActivateFence(
          &Context->Scheduler, ADMISSION_SCHEDULER_NODE,
          ADMISSION_SCHEDULER_ENGINE, Fence))
    submitted = TRUE;
  KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
  if (!submitted)
    InterlockedCompareExchange(&Context->SchedulerFaulted, 0x10000L | __LINE__, 0);
  return submitted;
}

_Use_decl_annotations_ VOID AdmissionSchedulerDpc(
    ADMISSION_CONTEXT *Context) {
  APPLE_AGX_PREEMPTION_PHASE phase;
  KIRQL oldIrql;
  ULONG renderFence;
  ULONG reservedFence;
  BOOLEAN cpuUnreported;
  BOOLEAN notified;

  if (Context == NULL ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  phase = AppleAgxSchedulerPreemptionPhase(&Context->Scheduler);
  reservedFence = Context->DispatchedFence;
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  renderFence = (ULONG)InterlockedExchange(&Context->RenderDpcFence, 0);
  cpuUnreported = InterlockedCompareExchange(&Context->PagingPending, 0, 0) != 0 &&
      InterlockedCompareExchange(&Context->PagingDpcPending, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingDpcsActive, 0, 0) == 0;
  if (phase == AppleAgxPreemptionReadyToNotify && !cpuUnreported &&
      (reservedFence == 0u || renderFence == reservedFence))
    (void)AdmissionSchedulerTryNotifyPreemption(Context);
  notified = InterlockedExchange(&Context->SchedulerDpcPending, 0) != 0;
  if (notified && renderFence != 0u)
    AdmissionGdiReceiptDpcWindows(Context, renderFence);
  if (notified && renderFence != 0u)
    AdmissionTerminalReceiptDpcWindows(Context, renderFence);
  if (renderFence != 0u) {
    KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
    if (Context->DispatchedFence == renderFence)
      Context->DispatchedFence = 0u;
    KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  }
  if (notified && renderFence != 0u)
    AdmissionPlatformRecordPostDpcHealth(Context, renderFence);
  AdmissionDispatchQueuedWork(Context);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryCurrentFence(
    HANDLE Adapter, DXGKARG_QUERYCURRENTFENCE *CurrentFence) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  KIRQL oldIrql;

  if (context == NULL || CurrentFence == NULL ||
      CurrentFence->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      CurrentFence->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  KeAcquireSpinLock(&context->SchedulerLock, &oldIrql);
  CurrentFence->CurrentFence = AppleAgxSchedulerCurrentFence(
      &context->Scheduler, CurrentFence->NodeOrdinal,
      CurrentFence->EngineOrdinal);
  KeReleaseSpinLock(&context->SchedulerLock, oldIrql);
  AdmissionRenderCorrelationQueryFenceWindows(
      context, CurrentFence->CurrentFence);
  return STATUS_SUCCESS;
}

static VOID AdmissionCpuQueueReleaseContextsLocked(ADMISSION_CONTEXT *Context) {
  ULONG index;
  for (index = 0u; index < Context->CpuQueueCount; ++index) {
    ADMISSION_CPU_PACKET *packet = &Context->CpuQueue[
        (Context->CpuQueueHead + index) % APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
    if (packet->PresentContext != NULL)
      AdmissionContextRetireFence(packet->PresentContext, packet->Fence);
    packet->PresentContext = NULL;
  }
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiPreemptCommand(
    HANDLE Adapter, const DXGKARG_PREEMPTCOMMAND *PreemptCommand) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  BOOLEAN notifyNow;
  UINT cutoffFence;
  UINT activeFence;
  UINT queuedFence;
  ADMISSION_RENDER_CONTEXT *queuedContext = NULL;

  if (context == NULL || PreemptCommand == NULL ||
      PreemptCommand->Flags.Value != 0u ||
      PreemptCommand->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      PreemptCommand->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  /* EXP1210 receipt-only: does a preemption request arrive while a
   * DxgkDdiSubmitCommandVirtual call is still inside the driver? */
  {
    LONG preemptions = InterlockedIncrement(&context->G4PreemptOverlap.Preemptions);
    BOOLEAN publish = (preemptions & 63) == 1;
    if (InterlockedCompareExchange(&context->G4SubmitVirtualInFlight, 0, 0) > 0) {
      LONG fence = InterlockedCompareExchange(&context->G4SubmitVirtualFence, 0, 0);
      LONG flags = InterlockedCompareExchange(&context->G4SubmitVirtualFlags, 0, 0);
      InterlockedIncrement(&context->G4PreemptOverlap.Overlaps);
      if (InterlockedCompareExchange(&context->G4PreemptOverlapClaim, 1, 0) == 0) {
        context->G4PreemptOverlap.FirstPreemptionFence = PreemptCommand->PreemptionFenceId;
        context->G4PreemptOverlap.FirstSubmitFence = (ULONG)fence;
        context->G4PreemptOverlap.FirstSubmitFlags = (ULONG)flags;
      }
      InterlockedExchange(&context->G4PreemptOverlap.LastPreemptionFence,
                          (LONG)PreemptCommand->PreemptionFenceId);
      InterlockedExchange(&context->G4PreemptOverlap.LastSubmitFence, fence);
      InterlockedExchange(&context->G4PreemptOverlap.LastSubmitFlags, flags);
      publish = TRUE;
    }
    if (publish) {
      InterlockedExchange(&context->G4PreemptOverlapDirty, 1);
      AdmissionRenderCorrelationSubmitFailureWindows(context);
    }
  }
#endif
  KeAcquireSpinLockAtDpcLevel(&context->PagingLock);
  KeAcquireSpinLockAtDpcLevel(&context->SchedulerLock);
  cutoffFence = AppleAgxSchedulerLastSubmittedFence(
      &context->Scheduler, PreemptCommand->NodeOrdinal,
      PreemptCommand->EngineOrdinal);
  activeFence = AppleAgxSchedulerActiveFence(
      &context->Scheduler, PreemptCommand->NodeOrdinal,
      PreemptCommand->EngineOrdinal);
  queuedFence = 0u;
  if (AdmissionRenderPacketState(&context->RenderPacket) ==
          AdmissionRenderPacketQueued) {
    queuedFence = context->RenderPacket.Description.Fence;
    queuedContext = (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)
        context->RenderPacket.Description.ContextToken;
  }
  if (!AppleAgxSchedulerBeginBoundaryPreemption(
          &context->Scheduler, PreemptCommand->NodeOrdinal,
          PreemptCommand->EngineOrdinal, PreemptCommand->PreemptionFenceId,
          cutoffFence, activeFence)) {
    InterlockedCompareExchange(&context->SchedulerFaulted, 0x10000L | __LINE__, 0);
    KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
    KeReleaseSpinLockFromDpcLevel(&context->PagingLock);
    return STATUS_SUCCESS;
  }
  AdmissionCpuQueueReleaseContextsLocked(context);
  context->CpuQueueHead = context->CpuQueueCount = 0u;
  if (activeFence == 0u && queuedFence != 0u && context->DispatchedFence == queuedFence)
    context->DispatchedFence = 0u;
  AdmissionPagingUpdateIdleLocked(context);
  if (queuedFence != 0u &&
      (!AdmissionRenderPacketDiscardQueued(
           &context->RenderPacket, cutoffFence) ||
       queuedContext == NULL ||
       queuedContext->Object.FenceOutstanding != queuedFence ||
       !AdmissionBackendImageReleaseSubmission(
           &context->BackendImage, queuedFence))) {
    InterlockedCompareExchange(&context->SchedulerFaulted, 0x10000L | __LINE__, 0);
  } else if (queuedContext != NULL) {
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionGpuvaG3PrivatePreempt(queuedContext,queuedFence);
#endif
    AdmissionContextRetireFence(queuedContext, queuedFence);
  }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  /* Phase 5a: pending submissions were never started; VidSch resubmits. */
  AdmissionG4PendingDropLocked(context, TRUE);
#endif
  notifyNow = AppleAgxSchedulerPreemptionPhase(&context->Scheduler) ==
                  AppleAgxPreemptionReadyToNotify && context->DispatchedFence == 0u &&
      (InterlockedCompareExchange(&context->PagingPending, 0, 0) == 0 ||
       InterlockedCompareExchange(&context->PagingDpcPending, 0, 0) != 0 ||
       InterlockedCompareExchange(&context->PagingDpcsActive, 0, 0) != 0);
  KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
  KeReleaseSpinLockFromDpcLevel(&context->PagingLock);
  if (notifyNow && !AdmissionSchedulerTryNotifyPreemption(context))
    InterlockedCompareExchange(&context->SchedulerFaulted, 0x10000L | __LINE__, 0);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryDependentEngineGroup(
    HANDLE Adapter, DXGKARG_QUERYDEPENDENTENGINEGROUP *DependentGroup) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  PAGED_CODE();
  if (context == NULL || DependentGroup == NULL ||
      DependentGroup->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      DependentGroup->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  DependentGroup->DependentNodeOrdinalMask = 1ULL;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryEngineStatus(
    HANDLE Adapter, DXGKARG_QUERYENGINESTATUS *EngineStatus) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  PAGED_CODE();
  if (context == NULL || EngineStatus == NULL ||
      EngineStatus->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      EngineStatus->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  EngineStatus->EngineStatus.Value = 0u;
  EngineStatus->EngineStatus.Responsive =
      AdmissionPlatformRuntimeResponsive(context)
          ? 1u
          : 0u;
  return STATUS_SUCCESS;
}

/* G4-r3 (EXP1296 0x116): no host operation stops a running AGX job. The
 * retained firmware ends hung or faulting work itself (Timeout/Fault event,
 * halt) and drops it when resumed (Asahi gpu.rs handle_timeout, recover), and
 * an RTKit stop cannot be undone on it. So the reset waits, bounded, for the
 * firmware to end the active job through the ordinary completion path; that
 * end is the proof that the GPU no longer accesses the job's memory, and the
 * job's fence becomes LastAbortedFenceId (Microsoft: TDR in Windows 8 and
 * later, packet completed before the engine reset). The owning context is
 * faulted first; dispatch stays frozen (SchedulerFaulted) until the reset
 * succeeds. */
static NTSTATUS AdmissionResetQuiesceActive(ADMISSION_CONTEXT *Context) {
  ULONG fence;
  KIRQL oldIrql;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  if (AdmissionRenderPacketState(&Context->RenderPacket) !=
      AdmissionRenderPacketActive) {
    KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
    return STATUS_SUCCESS;
  }
  fence = Context->RenderPacket.Description.Fence;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  {
    ADMISSION_RENDER_CONTEXT *owner = (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)
        Context->RenderPacket.Description.ContextToken;
    if (owner != NULL && owner->Object.FenceOutstanding == fence)
      owner->GpuvaG3Poisoned = TRUE;
  }
#endif
  InterlockedCompareExchange(&Context->SchedulerFaulted, 0x10000L | __LINE__, 0);
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  return AdmissionPlatformRuntimeQuiesceActive(
      Context, fence, ADMISSION_RESET_QUIESCE_TIMEOUT_MS, &Context->GpuReset);
}

/* Lock-bearing code stays in .text even when the PASSIVE wrapper is pageable.
 * noinline prevents optimization from moving these sections back into PAGE. */
static __declspec(noinline) NTSTATUS AdmissionResetEngineInternal(
    HANDLE Adapter, DXGKARG_RESETENGINE *ResetEngine) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  APPLE_AGX_U32 lastAborted = 0u;
  ADMISSION_RENDER_CONTEXT *packetContext = NULL;
  ADMISSION_RENDER_PACKET_STATE packetState;
  APPLE_AGX_U32 packetFence = 0u;
  BOOLEAN reset;
  KIRQL oldIrql;

  if (context == NULL || ResetEngine == NULL ||
      ResetEngine->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      ResetEngine->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  if (!NT_SUCCESS(AdmissionResetQuiesceActive(context)))
    return STATUS_DEVICE_HARDWARE_ERROR;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  {
    BOOLEAN privateReset = AdmissionGpuvaG3PrivateReset(context);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    AdmissionDwmFrameRecordPrivateReset(context, privateReset);
#endif
    if (!privateReset) return STATUS_DEVICE_HARDWARE_ERROR;
  }
#endif
  KeAcquireSpinLock(&context->PagingLock, &oldIrql);
  if (InterlockedCompareExchange(&context->PagingPending, 0, 0) != 0 ||
      InterlockedCompareExchange(&context->PagingWorkersActive, 0, 0) != 0 ||
      InterlockedCompareExchange(&context->PagingDpcsActive, 0, 0) != 0) {
    KeReleaseSpinLock(&context->PagingLock, oldIrql);
    return STATUS_DEVICE_BUSY;
  }
  KeAcquireSpinLockAtDpcLevel(&context->SchedulerLock);
  packetState = AdmissionRenderPacketState(&context->RenderPacket);
  if (packetState == AdmissionRenderPacketActive) {
    /* Activated after the quiescence proof above: nothing proves its end. */
    InterlockedCompareExchange(&context->SchedulerFaulted, 0x10000L | __LINE__, 0);
    KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
    KeReleaseSpinLock(&context->PagingLock, oldIrql);
    return STATUS_DEVICE_HARDWARE_ERROR;
  }
  if (packetState == AdmissionRenderPacketQueued) {
    packetFence = context->RenderPacket.Description.Fence;
    packetContext = (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)
        context->RenderPacket.Description.ContextToken;
    if (!AdmissionRenderPacketReset(
            &context->RenderPacket, packetFence, 0u)) {
      KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
      KeReleaseSpinLock(&context->PagingLock, oldIrql);
      return STATUS_INVALID_DEVICE_STATE;
    }
    if (!AdmissionBackendImageReleaseSubmission(
            &context->BackendImage, packetFence)) {
      InterlockedCompareExchange(&context->SchedulerFaulted, 0x10000L | __LINE__, 0);
      KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
      KeReleaseSpinLock(&context->PagingLock, oldIrql);
      return STATUS_INVALID_DEVICE_STATE;
    }
  }
  reset = AppleAgxSchedulerResetEngine(
              &context->Scheduler, ResetEngine->NodeOrdinal,
              ResetEngine->EngineOrdinal, &lastAborted)
              ? TRUE
              : FALSE;
  if (reset) {
    AdmissionCpuQueueReleaseContextsLocked(context);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionG4PendingDropLocked(context, FALSE);
#endif
    context->CpuQueueHead = context->CpuQueueCount = context->DispatchedFence = 0u;
    InterlockedExchange(&context->RenderDpcFence, 0);
    AdmissionPagingUpdateIdleLocked(context);
  }
  KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
  KeReleaseSpinLock(&context->PagingLock, oldIrql);
  if (!reset)
    return STATUS_INVALID_DEVICE_STATE;
  ResetEngine->LastAbortedFenceId = lastAborted;
  if (packetContext != NULL &&
      packetContext->Object.FenceOutstanding == packetFence) {
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionGpuvaG3PrivateCancel(packetContext,packetFence,FALSE);
#endif
    KeAcquireSpinLock(&context->SchedulerLock, &oldIrql);
    AdmissionContextRetireFence(packetContext, packetFence);
    KeReleaseSpinLock(&context->SchedulerLock, oldIrql);
  }
  InterlockedExchange(&context->SchedulerFaulted, 0);
  return STATUS_SUCCESS;
}

/* G4-r3 (EXP1296): Wom1GpuReset records every reset, the state it found and
 * its result; flushed before a failed adapter reset bugchecks. */
static __declspec(noinline) NTSTATUS AdmissionResetEngineRecorded(
    HANDLE Adapter, DXGKARG_RESETENGINE *ResetEngine, ULONG Entry) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  ADMISSION_GPU_RESET_RECEIPT *receipt;
  NTSTATUS status;
  KIRQL oldIrql;
  if (context == NULL)
    return AdmissionResetEngineInternal(Adapter, ResetEngine);
  receipt = &context->GpuReset;
  receipt->Version = ADMISSION_GPU_RESET_RECEIPT_VERSION;
  receipt->Bytes = sizeof(*receipt);
  ++receipt->Resets;
  receipt->Entry = Entry;
  receipt->SchedulerFaulted =
      (ULONG)InterlockedCompareExchange(&context->SchedulerFaulted, 0, 0);
  KeAcquireSpinLock(&context->SchedulerLock, &oldIrql);
  receipt->PacketState =
      (ULONG)AdmissionRenderPacketState(&context->RenderPacket);
  receipt->PacketFence = context->RenderPacket.Description.Fence;
  receipt->ContextToken = context->RenderPacket.Description.ContextToken;
  receipt->ActiveFence = context->Scheduler.ActiveFence;
  receipt->CompletedFence = context->Scheduler.CompletedFence;
  receipt->LastSubmittedFence = context->Scheduler.LastSubmittedFence;
  receipt->PreemptionPhase = (ULONG)context->Scheduler.PreemptionPhase;
  KeReleaseSpinLock(&context->SchedulerLock, oldIrql);
  receipt->Quiesce = (ULONG)AdmissionGpuResetQuiesceNone;
  receipt->WaitMs = 0u;
  AdmissionPlatformRuntimeResetState(context, receipt);
  status = AdmissionResetEngineInternal(Adapter, ResetEngine);
  receipt->Status = (ULONG)status;
  receipt->LastAbortedFence =
      NT_SUCCESS(status) ? ResetEngine->LastAbortedFenceId : 0u;
  if (NT_SUCCESS(status))
    ++receipt->Succeeded;
  AdmissionRecordGpuReset(context);
  return status;
}

static NTSTATUS AdmissionResetEngineEntry(
    HANDLE Adapter, DXGKARG_RESETENGINE *ResetEngine, ULONG Entry) {
  DXGKARG_RESETENGINE local;
  NTSTATUS status;
  if (ResetEngine == NULL)
    return STATUS_INVALID_PARAMETER;
  local = *ResetEngine;
  status = AdmissionResetEngineRecorded(Adapter, &local, Entry);
  if (NT_SUCCESS(status))
    ResetEngine->LastAbortedFenceId = local.LastAbortedFenceId;
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiResetEngine(
    HANDLE Adapter, DXGKARG_RESETENGINE *ResetEngine) {
  PAGED_CODE();
  return AdmissionResetEngineEntry(Adapter, ResetEngine,
                                   ADMISSION_GPU_RESET_ENGINE);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiResetFromTimeout(HANDLE Adapter) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  DXGKARG_RESETENGINE reset;
  NTSTATUS status;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&reset, sizeof(reset));
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  AdmissionDwmFrameRecordTdr(context, STATUS_PENDING, FALSE);
#endif
  /* Adapter-wide reset quiesces the software source; DCP/scanout reserve
   * remain owned by the retained display layer. Restart resumes that phase. */
  status = AdmissionScanoutSetTimelinePaused(context, TRUE);
  if (NT_SUCCESS(status))
    status = AdmissionResetEngineEntry(context, &reset,
                                       ADMISSION_GPU_RESET_ADAPTER);
  if (NT_SUCCESS(status)) {
    KIRQL oldIrql;
    /* Microsoft (TDR in Windows 8 and later): an adapter-wide reset advances
     * the last completed fence to the last submitted fence at the reset. */
    KeAcquireSpinLock(&context->SchedulerLock, &oldIrql);
    context->Scheduler.CompletedFence = context->Scheduler.LastSubmittedFence;
    KeReleaseSpinLock(&context->SchedulerLock, oldIrql);
  }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  AdmissionDwmFrameRecordTdr(context, status, TRUE);
#endif
  return status;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRestartFromTimeout(HANDLE Adapter) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  if (context == NULL ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0 ||
      InterlockedCompareExchange(&context->SchedulerFaulted, 0, 0) != 0)
    return STATUS_DEVICE_NOT_READY;
  return AdmissionScanoutSetTimelinePaused(context, FALSE);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiCollectDbgInfo(
    HANDLE Adapter, const DXGKARG_COLLECTDBGINFO *CollectDbgInfo) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  ADMISSION_SCHEDULER_DEBUG_SNAPSHOT snapshot;
  KIRQL oldIrql;

  if (context == NULL || CollectDbgInfo == NULL ||
      (CollectDbgInfo->BufferSize != 0u && CollectDbgInfo->pBuffer == NULL))
    return STATUS_INVALID_PARAMETER;
  if (CollectDbgInfo->BufferSize == 0u)
    return STATUS_SUCCESS;
  RtlZeroMemory(CollectDbgInfo->pBuffer, CollectDbgInfo->BufferSize);
  /* A buffer that held version 1 keeps receiving its prefix. */
  if (CollectDbgInfo->BufferSize < ADMISSION_SCHEDULER_DEBUG_V1_SIZE)
    return STATUS_BUFFER_TOO_SMALL;
  RtlZeroMemory(&snapshot, sizeof(snapshot));
  snapshot.Version = ADMISSION_SCHEDULER_DEBUG_VERSION;
  snapshot.Size = sizeof(snapshot);
  KeAcquireSpinLock(&context->SchedulerLock, &oldIrql);
  snapshot.CompletedFence = context->Scheduler.CompletedFence;
  snapshot.LastSubmittedFence = context->Scheduler.LastSubmittedFence;
  snapshot.ActiveFence = context->Scheduler.ActiveFence;
  snapshot.PreemptionPhase = context->Scheduler.PreemptionPhase;
  snapshot.ContextCount = context->Scheduler.ContextCount;
  snapshot.RenderPacketState =
      (ULONG)AdmissionRenderPacketState(&context->RenderPacket);
  snapshot.RenderPacketFence = context->RenderPacket.Description.Fence;
  KeReleaseSpinLock(&context->SchedulerLock, oldIrql);
  snapshot.SchedulerFaultedCode =
      (ULONG)InterlockedCompareExchange(&context->SchedulerFaulted, 0, 0);
  snapshot.PlatformState = AdmissionPlatformRuntimeDebugState(context);
  snapshot.Resets = context->GpuReset.Resets;
  snapshot.ResetsSucceeded = context->GpuReset.Succeeded;
  if (snapshot.SchedulerFaultedCode != 0u)
    snapshot.Flags |= ADMISSION_SCHEDULER_DEBUG_FAULTED;
  if (InterlockedCompareExchange(&context->PagingPending, 0, 0) != 0)
    snapshot.Flags |= ADMISSION_SCHEDULER_DEBUG_PAGING_PENDING;
  RtlCopyMemory(CollectDbgInfo->pBuffer, &snapshot,
                CollectDbgInfo->BufferSize < sizeof(snapshot)
                    ? CollectDbgInfo->BufferSize : sizeof(snapshot));
  if (CollectDbgInfo->pExtension != NULL) {
    RtlZeroMemory(CollectDbgInfo->pExtension,
                  sizeof(*CollectDbgInfo->pExtension));
    CollectDbgInfo->pExtension->CurrentDmaBufferOffset = 0u;
  }
  return STATUS_SUCCESS;
}
