#include "render_admission.h"

#define ADMISSION_SCHEDULER_NODE 0u
#define ADMISSION_SCHEDULER_ENGINE 0u

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
} ADMISSION_SCHEDULER_DEBUG_SNAPSHOT;

#define ADMISSION_SCHEDULER_DEBUG_VERSION 1u
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
    InterlockedExchange(&Context->SchedulerFaulted, 1);
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
    InterlockedExchange(&Context->SchedulerFaulted, 1);
  return claimed;
}

_Use_decl_annotations_ NTSTATUS AdmissionSchedulerStart(
    ADMISSION_CONTEXT *Context) {
  if (Context == NULL ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) != 0)
    return STATUS_INVALID_DEVICE_STATE;
  KeInitializeSpinLock(&Context->SchedulerLock);
  AppleAgxSchedulerInitialize(&Context->Scheduler);
  InterlockedExchange(&Context->SchedulerFaulted, 0);
  InterlockedExchange(&Context->SchedulerDpcPending, 0);
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
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->SchedulerDpcPending, 0, 0) != 0)
    return STATUS_DEVICE_BUSY;
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
  completed = AppleAgxSchedulerCompleteFence(
                  &Context->Scheduler, ADMISSION_SCHEDULER_NODE,
                  ADMISSION_SCHEDULER_ENGINE, Fence)
                  ? TRUE
                  : FALSE;
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
    InterlockedExchange(&Context->SchedulerFaulted, 1);
  return completed;
}

_Use_decl_annotations_ VOID AdmissionSchedulerDpc(
    ADMISSION_CONTEXT *Context) {
  APPLE_AGX_PREEMPTION_PHASE phase;
  KIRQL oldIrql;

  if (Context == NULL ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0)
    return;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  phase = AppleAgxSchedulerPreemptionPhase(&Context->Scheduler);
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  if (phase == AppleAgxPreemptionReadyToNotify)
    (void)AdmissionSchedulerTryNotifyPreemption(Context);
  if (InterlockedExchange(&Context->SchedulerDpcPending, 0) != 0 &&
      Context->InterfaceValid && Context->Interface.DxgkCbNotifyDpc != NULL)
    Context->Interface.DxgkCbNotifyDpc(Context->Interface.DeviceHandle);
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
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiPreemptCommand(
    HANDLE Adapter, const DXGKARG_PREEMPTCOMMAND *PreemptCommand) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  BOOLEAN notifyNow;
  UINT activeFence;

  if (context == NULL || PreemptCommand == NULL ||
      PreemptCommand->Flags.Value != 0u ||
      PreemptCommand->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      PreemptCommand->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  activeFence = InterlockedCompareExchange(
                    &context->PagingPending, 0, 0) != 0
                    ? context->PagingFence
                    : 0u;
  KeAcquireSpinLockAtDpcLevel(&context->SchedulerLock);
  if (!AppleAgxSchedulerBeginBoundaryPreemption(
          &context->Scheduler, PreemptCommand->NodeOrdinal,
          PreemptCommand->EngineOrdinal, PreemptCommand->PreemptionFenceId,
          context->PagingLastSubmittedFence, activeFence)) {
    InterlockedExchange(&context->SchedulerFaulted, 1);
    KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
    return STATUS_SUCCESS;
  }
  notifyNow = AppleAgxSchedulerPreemptionPhase(&context->Scheduler) ==
              AppleAgxPreemptionReadyToNotify;
  KeReleaseSpinLockFromDpcLevel(&context->SchedulerLock);
  if (notifyNow && !AdmissionSchedulerTryNotifyPreemption(context))
    InterlockedExchange(&context->SchedulerFaulted, 1);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQueryDependentEngineGroup(
    HANDLE Adapter, DXGKARG_QUERYDEPENDENTENGINEGROUP *DependentGroup) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
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
  if (context == NULL || EngineStatus == NULL ||
      EngineStatus->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      EngineStatus->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  EngineStatus->EngineStatus.Value = 0u;
  EngineStatus->EngineStatus.Responsive =
      InterlockedCompareExchange(&context->SchedulerFaulted, 0, 0) == 0
          ? 1u
          : 0u;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiResetEngine(
    HANDLE Adapter, DXGKARG_RESETENGINE *ResetEngine) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  APPLE_AGX_U32 lastAborted = 0u;
  BOOLEAN reset;
  KIRQL oldIrql;

  if (context == NULL || ResetEngine == NULL ||
      ResetEngine->NodeOrdinal != ADMISSION_SCHEDULER_NODE ||
      ResetEngine->EngineOrdinal != ADMISSION_SCHEDULER_ENGINE ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  if (InterlockedCompareExchange(&context->PagingPending, 0, 0) != 0)
    return STATUS_DEVICE_BUSY;
  KeAcquireSpinLock(&context->SchedulerLock, &oldIrql);
  reset = AppleAgxSchedulerResetEngine(
              &context->Scheduler, ResetEngine->NodeOrdinal,
              ResetEngine->EngineOrdinal, &lastAborted)
              ? TRUE
              : FALSE;
  KeReleaseSpinLock(&context->SchedulerLock, oldIrql);
  if (!reset)
    return STATUS_INVALID_DEVICE_STATE;
  ResetEngine->LastAbortedFenceId = lastAborted;
  InterlockedExchange(&context->SchedulerFaulted, 0);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiResetFromTimeout(HANDLE Adapter) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  DXGKARG_RESETENGINE reset;
  if (context == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&reset, sizeof(reset));
  return AdmissionDdiResetEngine(context, &reset);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRestartFromTimeout(HANDLE Adapter) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  if (context == NULL ||
      InterlockedCompareExchange(&context->SchedulerInitialized, 0, 0) == 0 ||
      InterlockedCompareExchange(&context->SchedulerFaulted, 0, 0) != 0)
    return STATUS_DEVICE_NOT_READY;
  return STATUS_SUCCESS;
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
  if (CollectDbgInfo->BufferSize < sizeof(snapshot))
    return STATUS_BUFFER_TOO_SMALL;
  RtlZeroMemory(&snapshot, sizeof(snapshot));
  snapshot.Version = ADMISSION_SCHEDULER_DEBUG_VERSION;
  snapshot.Size = sizeof(snapshot);
  KeAcquireSpinLock(&context->SchedulerLock, &oldIrql);
  snapshot.CompletedFence = context->Scheduler.CompletedFence;
  snapshot.LastSubmittedFence = context->PagingLastSubmittedFence;
  snapshot.ActiveFence = context->PagingPending ? context->PagingFence : 0u;
  snapshot.PreemptionPhase = context->Scheduler.PreemptionPhase;
  snapshot.ContextCount = context->Scheduler.ContextCount;
  KeReleaseSpinLock(&context->SchedulerLock, oldIrql);
  if (InterlockedCompareExchange(&context->SchedulerFaulted, 0, 0) != 0)
    snapshot.Flags |= ADMISSION_SCHEDULER_DEBUG_FAULTED;
  if (InterlockedCompareExchange(&context->PagingPending, 0, 0) != 0)
    snapshot.Flags |= ADMISSION_SCHEDULER_DEBUG_PAGING_PENDING;
  RtlCopyMemory(CollectDbgInfo->pBuffer, &snapshot, sizeof(snapshot));
  if (CollectDbgInfo->pExtension != NULL) {
    RtlZeroMemory(CollectDbgInfo->pExtension,
                  sizeof(*CollectDbgInfo->pExtension));
    CollectDbgInfo->pExtension->CurrentDmaBufferOffset = 0u;
  }
  return STATUS_SUCCESS;
}
