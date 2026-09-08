#include "render_admission.h"

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)

static NTSTATUS AdmissionRenderCorrelationExportSnapshot(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_RENDER_CORRELATION_STATE *Snapshot) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  NTSTATUS status;
  if (Context == NULL || Snapshot == NULL ||
      Context->PhysicalDeviceObject == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  status = IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
      PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key);
  if (!NT_SUCCESS(status))
    return status;
  RtlInitUnicodeString(&name, L"Wom1RenderCorrelationState");
  status = ZwSetValueKey(key, &name, 0u, REG_BINARY,
      (PVOID)Snapshot, sizeof(*Snapshot));
  if (NT_SUCCESS(status))
    status = ZwFlushKey(key);
  ZwClose(key);
  return status;
}

static VOID AdmissionRenderCorrelationExportWorker(
    PDEVICE_OBJECT DeviceObject, PVOID Opaque) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Opaque;
  ADMISSION_RENDER_CORRELATION_STATE snapshot;
  NTSTATUS status;
  ULONG generation;
  KIRQL oldIrql;
  UNREFERENCED_PARAMETER(DeviceObject);
  if (context == NULL)
    return;
Restart:
  for (;;) {
    InterlockedExchange(&context->RenderCorrelationDirty, 0);
    KeAcquireSpinLock(&context->RenderCorrelationLock, &oldIrql);
    context->RenderCorrelation.ExportAttempted = 1u;
    context->RenderCorrelation.ExportStatus = (ULONG)STATUS_PENDING;
    context->RenderCorrelation.Durable = 0u;
    snapshot = context->RenderCorrelation;
    generation = snapshot.CapturedGeneration;
    KeReleaseSpinLock(&context->RenderCorrelationLock, oldIrql);

    status = AdmissionRenderCorrelationExportSnapshot(context, &snapshot);
    KeAcquireSpinLock(&context->RenderCorrelationLock, &oldIrql);
    (void)AdmissionRenderCorrelationMarkExport(
        &context->RenderCorrelation, generation, (ULONG)status,
        NT_SUCCESS(status) ? 1u : 0u);
    snapshot = context->RenderCorrelation;
    KeReleaseSpinLock(&context->RenderCorrelationLock, oldIrql);
    if (NT_SUCCESS(status)) {
      status = AdmissionRenderCorrelationExportSnapshot(context, &snapshot);
      if (!NT_SUCCESS(status)) {
        KeAcquireSpinLock(&context->RenderCorrelationLock, &oldIrql);
        (void)AdmissionRenderCorrelationMarkExport(
            &context->RenderCorrelation, generation, (ULONG)status, 0u);
        KeReleaseSpinLock(&context->RenderCorrelationLock, oldIrql);
      }
    }
    if (InterlockedCompareExchange(
            &context->RenderCorrelationDirty, 0, 0) == 0)
      break;
  }
  InterlockedExchange(&context->RenderCorrelationWorkerQueued, 0);
  KeMemoryBarrier();
  if (InterlockedCompareExchange(&context->RenderCorrelationDirty, 0, 0) != 0 &&
      InterlockedCompareExchange(
          &context->RenderCorrelationWorkerQueued, 1, 0) == 0)
    goto Restart;
  KeSetEvent(&context->RenderCorrelationIdle, IO_NO_INCREMENT, FALSE);
}

static VOID AdmissionRenderCorrelationQueueExport(
    ADMISSION_CONTEXT *Context) {
  if (Context == NULL || Context->RenderCorrelationWorkItem == NULL)
    return;
  InterlockedExchange(&Context->RenderCorrelationDirty, 1);
  if (InterlockedCompareExchange(
          &Context->RenderCorrelationWorkerQueued, 1, 0) != 0)
    return;
  KeClearEvent(&Context->RenderCorrelationIdle);
  IoQueueWorkItem(Context->RenderCorrelationWorkItem,
      AdmissionRenderCorrelationExportWorker, DelayedWorkQueue, Context);
}

static ADMISSION_RENDER_CORRELATION_SLOT *
AdmissionRenderCorrelationFindFenceWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence) {
  ULONG index;
  if (Context == NULL || Fence == 0u)
    return NULL;
  for (index = 0u; index < ADMISSION_RENDER_CORRELATION_CAPACITY; ++index) {
    if ((ULONG)InterlockedCompareExchange(
            (volatile LONG *)&Context->RenderCorrelation.Slot[index].Fence,
            0, 0) == Fence)
      return &Context->RenderCorrelation.Slot[index];
  }
  return NULL;
}

_Use_decl_annotations_ NTSTATUS AdmissionRenderCorrelationStartWindows(
    ADMISSION_CONTEXT *Context) {
  ULONG bootGeneration;
  if (Context == NULL || Context->PhysicalDeviceObject == NULL ||
      Context->RenderCorrelationWorkItem != NULL)
    return STATUS_INVALID_PARAMETER;
  KeInitializeSpinLock(&Context->RenderCorrelationLock);
  KeInitializeEvent(&Context->RenderCorrelationIdle, NotificationEvent, TRUE);
  InterlockedExchange(&Context->RenderCorrelationDirty, 0);
  InterlockedExchange(&Context->RenderCorrelationWorkerQueued, 0);
  InterlockedExchange(&Context->RenderCorrelationStopping, 0);
  RtlZeroMemory(&Context->RenderCorrelation,
                sizeof(Context->RenderCorrelation));
  bootGeneration = (ULONG)KeQueryInterruptTime();
  if (bootGeneration == 0u)
    bootGeneration = 1u;
  if (!AdmissionRenderCorrelationInitialize(
          &Context->RenderCorrelation, APPLE_AGX_VERSION_BUILD,
          bootGeneration))
    return STATUS_INVALID_DEVICE_STATE;
  Context->RenderCorrelationWorkItem =
      IoAllocateWorkItem(Context->PhysicalDeviceObject);
  return Context->RenderCorrelationWorkItem != NULL
      ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

_Use_decl_annotations_ NTSTATUS AdmissionRenderCorrelationStopWindows(
    ADMISSION_CONTEXT *Context) {
  PIO_WORKITEM workItem;
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  workItem = Context->RenderCorrelationWorkItem;
  if (workItem == NULL)
    return STATUS_SUCCESS;
  InterlockedExchange(&Context->RenderCorrelationStopping, 1);
  AdmissionRenderCorrelationQueueExport(Context);
  (void)KeWaitForSingleObject(&Context->RenderCorrelationIdle,
      Executive, KernelMode, FALSE, NULL);
  Context->RenderCorrelationWorkItem = NULL;
  IoFreeWorkItem(workItem);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ ULONG AdmissionRenderCorrelationBeginWindows(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_RENDER_CONTEXT *RenderContext,
    const DXGKARG_RENDER *Args) {
  unsigned int sequence = 0u;
  KIRQL oldIrql;
  if (Context == NULL || RenderContext == NULL || Args == NULL)
    return 0u;
  KeAcquireSpinLock(&Context->RenderCorrelationLock, &oldIrql);
  (void)AdmissionRenderCorrelationBegin(&Context->RenderCorrelation,
      KeQueryInterruptTime(),
      (ULONGLONG)(ULONG_PTR)Context,
      (ULONGLONG)(ULONG_PTR)RenderContext,
      Args->CommandLength, &sequence);
  KeReleaseSpinLock(&Context->RenderCorrelationLock, oldIrql);
  AdmissionRenderCorrelationQueueExport(Context);
  return (ULONG)sequence;
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationValidatedWindows(
    ADMISSION_CONTEXT *Context, ULONG CallSequence,
    ULONGLONG CommandHash, UINT AllocationCount,
    UINT DestinationIndex, UINT DestinationSegment,
    const ULONGLONG *AllocationTokens) {
  KIRQL oldIrql;
  if (Context == NULL || CallSequence == 0u)
    return;
  KeAcquireSpinLock(&Context->RenderCorrelationLock, &oldIrql);
  (void)AdmissionRenderCorrelationValidated(&Context->RenderCorrelation,
      CallSequence, CommandHash, AllocationCount, DestinationIndex,
      DestinationSegment, AllocationTokens);
  KeReleaseSpinLock(&Context->RenderCorrelationLock, oldIrql);
  AdmissionRenderCorrelationQueueExport(Context);
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationExitWindows(
    ADMISSION_CONTEXT *Context, ULONG CallSequence, ULONG Guard,
    NTSTATUS Status, ULONG DmaBytes, ULONG Patches, BOOLEAN Prepatched) {
  KIRQL oldIrql;
  if (Context == NULL || CallSequence == 0u)
    return;
  KeAcquireSpinLock(&Context->RenderCorrelationLock, &oldIrql);
  (void)AdmissionRenderCorrelationExit(&Context->RenderCorrelation,
      CallSequence, KeQueryInterruptTime(), Guard, (ULONG)Status,
      DmaBytes, Patches, Prepatched ? 1u : 0u);
  KeReleaseSpinLock(&Context->RenderCorrelationLock, oldIrql);
  AdmissionRenderCorrelationQueueExport(Context);
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationPatchWindows(
    ADMISSION_CONTEXT *Context, ULONGLONG ContextToken, BOOLEAN Entry,
    ULONG Guard, NTSTATUS Status) {
  KIRQL oldIrql;
  if (Context == NULL)
    return;
  KeAcquireSpinLock(&Context->RenderCorrelationLock, &oldIrql);
  (void)AdmissionRenderCorrelationPatch(&Context->RenderCorrelation,
      ContextToken, Entry ? 1u : 0u, Guard, (ULONG)Status);
  KeReleaseSpinLock(&Context->RenderCorrelationLock, oldIrql);
  AdmissionRenderCorrelationQueueExport(Context);
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationSubmitWindows(
    ADMISSION_CONTEXT *Context, ULONGLONG ContextToken, BOOLEAN Entry,
    ULONG Fence, ULONG Guard, NTSTATUS Status) {
  KIRQL oldIrql;
  if (Context == NULL)
    return;
  KeAcquireSpinLock(&Context->RenderCorrelationLock, &oldIrql);
  (void)AdmissionRenderCorrelationSubmit(&Context->RenderCorrelation,
      ContextToken, Entry ? 1u : 0u, Fence, Guard, (ULONG)Status);
  KeReleaseSpinLock(&Context->RenderCorrelationLock, oldIrql);
  AdmissionRenderCorrelationQueueExport(Context);
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationWorkerWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence, BOOLEAN Entry, ULONG Status) {
  KIRQL oldIrql;
  if (Context == NULL)
    return;
  KeAcquireSpinLock(&Context->RenderCorrelationLock, &oldIrql);
  (void)AdmissionRenderCorrelationWorker(&Context->RenderCorrelation,
      Fence, Entry ? 1u : 0u, Status);
  KeReleaseSpinLock(&Context->RenderCorrelationLock, oldIrql);
  AdmissionRenderCorrelationQueueExport(Context);
}

_Use_decl_annotations_ VOID
AdmissionRenderCorrelationNotifyAtInterruptWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence, ULONGLONG Timestamp,
    BOOLEAN QueueDpcResult) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot =
      AdmissionRenderCorrelationFindFenceWindows(Context, Fence);
  if (slot == NULL)
    return;
  InterlockedExchange64((volatile LONG64 *)&slot->NotifyTimestamp,
                        (LONG64)Timestamp);
  InterlockedExchange((volatile LONG *)&slot->QueueDpcResult,
                      QueueDpcResult ? 1 : 0);
  (void)InterlockedOr((volatile LONG *)&slot->ValidMask,
                      ADMISSION_RENDER_CAPTURE_NOTIFY);
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationSynchronizeWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence, NTSTATUS Status,
    BOOLEAN CallbackResult) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot;
  KIRQL oldIrql;
  if (Context == NULL)
    return;
  KeAcquireSpinLock(&Context->RenderCorrelationLock, &oldIrql);
  slot = AdmissionRenderCorrelationFindFenceWindows(Context, Fence);
  if (slot != NULL) {
    slot->SynchronizeStatus = (ULONG)Status;
    if (!CallbackResult)
      slot->QueueDpcResult = 0u;
    ++Context->RenderCorrelation.CapturedGeneration;
  }
  KeReleaseSpinLock(&Context->RenderCorrelationLock, oldIrql);
  AdmissionRenderCorrelationQueueExport(Context);
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationDpcWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence, ULONGLONG Timestamp) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot =
      AdmissionRenderCorrelationFindFenceWindows(Context, Fence);
  if (slot == NULL)
    return;
  InterlockedExchange64((volatile LONG64 *)&slot->DpcTimestamp,
                        (LONG64)Timestamp);
  (void)InterlockedOr((volatile LONG *)&slot->ValidMask,
                      ADMISSION_RENDER_CAPTURE_DPC);
  InterlockedIncrement(
      (volatile LONG *)&Context->RenderCorrelation.CapturedGeneration);
  AdmissionRenderCorrelationQueueExport(Context);
}

_Use_decl_annotations_ VOID AdmissionRenderCorrelationQueryFenceWindows(
    ADMISSION_CONTEXT *Context, ULONG Fence) {
  ADMISSION_RENDER_CORRELATION_SLOT *slot =
      AdmissionRenderCorrelationFindFenceWindows(Context, Fence);
  if (slot == NULL)
    return;
  InterlockedExchange((volatile LONG *)&slot->QueryFenceValue, (LONG)Fence);
  InterlockedIncrement((volatile LONG *)&slot->QueryFenceCount);
  (void)InterlockedOr((volatile LONG *)&slot->ValidMask,
                      ADMISSION_RENDER_CAPTURE_QUERY_FENCE);
  InterlockedIncrement(
      (volatile LONG *)&Context->RenderCorrelation.CapturedGeneration);
  AdmissionRenderCorrelationQueueExport(Context);
}

#endif
