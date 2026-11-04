#include "render_admission.h"

/* Fault diagnostics: first nonzero transition stores file tag 2 and
 * source line. Consumers retain zero/nonzero semantics; reset clears it. */
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#include "gpuva_g3_private.h"
/* L1 receipt-only lock profile: every fast mutex in this file is the G3
 * state Lock (gpuva_g3_private.h). */
#define ADMISSION_G3_LOCK_FILE 3u
#define ExAcquireFastMutex(m) ADMISSION_G3_PROFILED_ACQUIRE(m)
#define ExReleaseFastMutex(m) ADMISSION_G3_PROFILED_RELEASE(m)
#endif

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

static NTSTATUS AdmissionBuildPagingBuffer(
    HANDLE Adapter, DXGKARG_BUILDPAGINGBUFFER *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  APPLE_AGX_PHYSICAL_PAGING_PLAN plan;
  APPLE_AGX_PHYSICAL_PAGING_RESULT result;
  PMDL mdl;
  ULONGLONG systemOffset;
  NTSTATUS status;
  if (context == NULL || Args == NULL ||
      context->Memory.Initialized != APPLE_AGX_TRUE ||
      context->Memory.UatReady != APPLE_AGX_TRUE)
    return STATUS_INVALID_DEVICE_STATE;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (Args->Operation == DXGK_OPERATION_UPDATE_PAGE_TABLE ||
      Args->Operation == DXGK_OPERATION_FLUSH_TLB ||
      Args->Operation == DXGK_OPERATION_VIRTUAL_FILL ||
      Args->Operation == DXGK_OPERATION_VIRTUAL_TRANSFER ||
      Args->Operation == DXGK_OPERATION_SIGNAL_MONITORED_FENCE)
    return AdmissionGpuvaG3BuildPagingBufferChecked(context, Args);
#endif
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
        Args->UnmapApertureSegment.NumberOfPages > MAXULONG)
      return STATUS_INVALID_PARAMETER;
    return AdmissionMemoryRuntimeUnmapAperture(
        context,
        (ULONGLONG)Args->UnmapApertureSegment.OffsetInPages << PAGE_SHIFT,
        (UINT)Args->UnmapApertureSegment.NumberOfPages,
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
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionGpuvaG3NoteAllocationPaging(context, Args->Transfer.hAllocation,
        2u, Args->Transfer.Destination.SegmentId);
#endif
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
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionGpuvaG3NoteAllocationPaging(context, Args->Fill.hAllocation,
        1u, Args->Fill.Destination.SegmentId);
#endif
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
  status = AdmissionEncodePaging(Args, &plan, mdl);
  if (NT_SUCCESS(status)) AdmissionPagingNoteEncoded(context, 1u);
  return status;
}

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
static VOID AdmissionPagingBuildTraceWrite(
    ADMISSION_CONTEXT *Context, ULONG Field, ULONG Value) {
  volatile ULONG64 *request;
  volatile ULONG *command;
  if (Context == NULL || Context->BrokerBase == NULL)
    return;
  request = (volatile ULONG64 *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  command = (volatile ULONG *)(Context->BrokerBase +
      J313_AGX_G2_POWER_REG_COMMAND);
  WRITE_REGISTER_ULONG64(
      request, AdmissionPagingBuildTraceWord(Field, Value));
  WRITE_REGISTER_ULONG(command, J313_AGX_G2_POWER_CMD_QUERY);
}

static VOID AdmissionPagingBuildTrace(
    ADMISSION_CONTEXT *Context, ULONG Irql, ULONG Operation,
    ULONG DmaSize, ULONG PrivateSize, NTSTATUS Status) {
  if (Context == NULL || Context->BrokerBase == NULL ||
      InterlockedCompareExchange(
          &Context->PagingCorrelationArmed, 0, 0) == 0 ||
      InterlockedCompareExchange(
          &Context->PagingBuildTraceClaimed, 1, 0) != 0)
    return;
  AdmissionPagingBuildTraceWrite(Context, 1u, 1u);
  AdmissionPagingBuildTraceWrite(Context, 2u, Irql);
  AdmissionPagingBuildTraceWrite(Context, 3u, Operation);
  AdmissionPagingBuildTraceWrite(Context, 4u, DmaSize);
  AdmissionPagingBuildTraceWrite(Context, 5u, PrivateSize);
  AdmissionPagingBuildTraceWrite(Context, 6u, (ULONG)Status);
}
#else
#define AdmissionPagingBuildTrace(Context, Irql, Operation, DmaSize, PrivateSize, Status) \
  do {                                                                                     \
    (void)(Context); (void)(Irql); (void)(Operation); (void)(DmaSize);                     \
    (void)(PrivateSize); (void)(Status);                                                    \
  } while (0)
#endif

/* EXP1121 receipt-only: memory writes only, no registry I/O (EXP996). */
static VOID AdmissionPagingCensusRecord(ADMISSION_CONTEXT *Context,
    const DXGKARG_BUILDPAGINGBUFFER *Args) {
  ADMISSION_PAGING_CENSUS_ENTRY *e;
  ULONG slot;
  if (Context == NULL || Args == NULL ||
      (Args->Operation != DXGK_OPERATION_VIRTUAL_TRANSFER &&
       Args->Operation != DXGK_OPERATION_VIRTUAL_FILL &&
       Args->Operation != DXGK_OPERATION_UPDATE_PAGE_TABLE))
    return;
  slot = (ULONG)InterlockedIncrement(&Context->PagingCensus.Next) - 1u;
  e = &Context->PagingCensus.Entries[slot % ADMISSION_PAGING_CENSUS_ENTRIES];
  RtlZeroMemory(e, sizeof(*e));
  e->Qpc = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
  e->Operation = (ULONG)Args->Operation;
  if (Args->Operation == DXGK_OPERATION_VIRTUAL_TRANSFER) {
    e->Allocation = (ULONGLONG)(ULONG_PTR)Args->TransferVirtual.hAllocation;
    e->Offset = Args->TransferVirtual.AllocationOffsetInBytes;
    e->Bytes = Args->TransferVirtual.TransferSizeInBytes;
    e->SourceVa = Args->TransferVirtual.SourceVirtualAddress;
    e->DestinationVa = Args->TransferVirtual.DestinationVirtualAddress;
    e->Detail = (ULONG)Args->TransferVirtual.TransferDirection;
    e->Flags = Args->TransferVirtual.Flags.Flags;
  } else if (Args->Operation == DXGK_OPERATION_VIRTUAL_FILL) {
    e->Allocation = (ULONGLONG)(ULONG_PTR)Args->FillVirtual.hAllocation;
    e->Offset = Args->FillVirtual.AllocationOffsetInBytes;
    e->Bytes = Args->FillVirtual.FillSizeInBytes;
    e->DestinationVa = Args->FillVirtual.DestinationVirtualAddress;
    e->Flags = Args->FillVirtual.FillPattern;
  } else {
    e->Allocation = (ULONGLONG)(ULONG_PTR)Args->UpdatePageTable.hAllocation;
    e->Process = (ULONGLONG)(ULONG_PTR)Args->UpdatePageTable.hProcess;
    e->Detail = Args->UpdatePageTable.PageTableLevel |
        ((ULONG)Args->UpdatePageTable.UpdateMode << 8);
    RtlCopyMemory(&e->Flags, &Args->UpdatePageTable.Flags,
        min(sizeof(e->Flags), sizeof(Args->UpdatePageTable.Flags)));
    e->Entries = Args->UpdatePageTable.NumPageTableEntries;
    e->Offset = Args->UpdatePageTable.StartIndex;
    e->DestinationVa = Args->UpdatePageTable.FirstPteVirtualAddress;
  }
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiBuildPagingBuffer(
    HANDLE Adapter, DXGKARG_BUILDPAGINGBUFFER *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  ULONG irql = (ULONG)KeGetCurrentIrql();
  ULONG operation = Args == NULL ? MAXULONG : (ULONG)Args->Operation;
  ULONG dmaSize = Args == NULL ? 0u : Args->DmaSize;
  ULONG privateSize = Args == NULL ? 0u : Args->DmaBufferPrivateDataSize;
  NTSTATUS status;
  /* EXP996: no registry I/O here. The G3 bring-up receipts wrote and flushed
   * the device key twice per paging operation (VidMm worker found in
   * NtFlushKey -> CmpFlushHive at the 0x119 bugcheck). Failures keep their
   * own gated receipts in the G3 paging path. */
  {
    /* EXP1052 receipt-only: in-memory per-operation build time. */
    LONGLONG start = KeQueryPerformanceCounter(NULL).QuadPart;
    AdmissionPagingCensusRecord(context, Args);
    status = AdmissionBuildPagingBuffer(Adapter, Args);
    if (context != NULL && operation < ADMISSION_PAGING_PROFILE_OPS) {
      AdmissionPagingProfileAdd(context->PagingProfile.Build[operation],
          KeQueryPerformanceCounter(NULL).QuadPart - start);
      if (Args->Operation == DXGK_OPERATION_UPDATE_PAGE_TABLE)
        InterlockedAdd64(&context->PagingProfile.UpdatePageTableEntries,
                         (LONG64)Args->UpdatePageTable.NumPageTableEntries);
    }
  }
  AdmissionPagingBuildTrace(
      context, irql, operation, dmaSize, privateSize, status);
  return status;
}

typedef struct _ADMISSION_PAGING_NOTIFICATION {
  ADMISSION_CONTEXT *Context;
  UINT Fence;
  ULONG Node;
  NTSTATUS Status;
  ULONGLONG FaultVa;
  ULONG FaultWrite;
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
    data.DmaCompleted.NodeOrdinal = notification->Node;
    data.DmaCompleted.EngineOrdinal = 0u;
  } else {
    /* DMA_FAULTED is reserved. WDDM also uses PAGE_FAULTED for a
     * non-MMU error: VA zero and a device-specific cause. The CPU worker
     * has already retired this exact packet before notifying its fence. */
    data.InterruptType = DXGK_INTERRUPT_DMA_PAGE_FAULTED;
    data.DmaPageFaulted.FaultedFenceId = notification->Fence;
    data.DmaPageFaulted.FaultedPrimitiveAPISequenceNumber =
        MAXULONGLONG;
    data.DmaPageFaulted.FaultedPipelineStage =
        DXGK_RENDER_PIPELINE_STAGE_UNKNOWN;
    data.DmaPageFaulted.FaultedBindTableEntry = MAXUINT;
    data.DmaPageFaulted.PageFaultFlags = notification->FaultWrite
        ? DXGK_PAGE_FAULT_WRITE : (DXGK_PAGE_FAULT_FLAGS)0;
    data.DmaPageFaulted.FaultedVirtualAddress = notification->FaultVa;
    data.DmaPageFaulted.NodeOrdinal = notification->Node;
    data.DmaPageFaulted.EngineOrdinal = 0u;
    data.DmaPageFaulted.PageTableLevel = 0u;
    if (notification->FaultVa == 0ULL) {
      data.DmaPageFaulted.FaultErrorCode.IsDeviceSpecificCode = 1u;
      data.DmaPageFaulted.FaultErrorCode.DeviceSpecificCode =
          (ULONG)notification->Status & 0x7fffffffu;
    }
  }
  context->Interface.DxgkCbNotifyInterrupt(
      context->Interface.DeviceHandle, &data);
  context->PagingNotifyQpc = KeQueryPerformanceCounter(NULL).QuadPart;
  if (notification->Node == 0u &&
      InterlockedCompareExchange(&context->PresentTransferState, 2, 2) == 2 &&
      context->PresentTransferReceipt.Fence == notification->Fence) {
    context->PresentTransferReceipt.NotifyInterrupt = 1u;
    InterlockedExchange(&context->PresentTransferState, 3);
  }
  InterlockedExchange(&context->PagingDpcPending, 1);
  (void)context->Interface.DxgkCbQueueDpc(
      context->Interface.DeviceHandle);
  return TRUE;
}

/* Only the worker that finished executing the CPU packet may call this.
 * A known-fence error advances QueryCurrentFence and pending preemption just
 * like a retired GPU error, but never reports a successful DMA completion. */
static BOOLEAN AdmissionPagingRecordTerminal(ADMISSION_CONTEXT *Context,
    UINT Fence, NTSTATUS Status) {
  BOOLEAN completed;
  KIRQL oldIrql;
  APPLE_AGX_SCHEDULER *scheduler;
  if (Context != NULL && Context->PagingActiveNode == 0u && NT_SUCCESS(Status))
    return AdmissionSchedulerRecordCompletion(Context, Fence);
  if (Context == NULL || Fence == 0u ||
      InterlockedCompareExchange(&Context->SchedulerInitialized, 0, 0) == 0)
    return FALSE;
  KeAcquireSpinLock(&Context->SchedulerLock, &oldIrql);
  scheduler = Context->PagingActiveNode ? &Context->PagingScheduler : &Context->Scheduler;
  completed = (NT_SUCCESS(Status) ?
      AppleAgxSchedulerCompleteActiveFence(scheduler, 0u, 0u, Fence) :
      AppleAgxSchedulerFaultActiveFence(scheduler, 0u, 0u, Fence)) ? TRUE : FALSE;
  if (completed && Context->PagingActiveNode && Context->PagingNodeDispatchedFence == Fence)
    Context->PagingNodeDispatchedFence = 0u;
  if (completed && !Context->PagingActiveNode && Context->DispatchedFence == Fence)
    Context->DispatchedFence = 0u;
  if (completed && AppleAgxSchedulerPreemptionPhase(scheduler) ==
          AppleAgxPreemptionWaitCurrentBoundary)
    completed = AppleAgxSchedulerObserveBoundaryCompletion(
        scheduler, 0u, 0u, Fence) ? TRUE : FALSE;
  KeReleaseSpinLock(&Context->SchedulerLock, oldIrql);
  if (!completed)
    InterlockedCompareExchange(&Context->SchedulerFaulted, 0x20000L | __LINE__, 0);
  return completed;
}

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* Same mutex as BeginJobAs and copy escapes: retain it through the CPU
 * writes, closing the idle-observation/new-GPU-job race. No spinlock held. */
static NTSTATUS AdmissionPagingAcquireMutation(ADMISSION_CONTEXT *Context,
                                               BOOLEAN *Held) {
  ADMISSION_G3_STATE *state = (ADMISSION_G3_STATE *)Context->GpuvaG3State;
  ULONGLONG deadline;
  ULONG i, iterations = 0u;
  ADMISSION_G3_LIVE_WAIT_ENTRY entry = {0};
  NTSTATUS result;
  LARGE_INTEGER delay;
  *Held = FALSE;
  if (!Context->LiveAdditivePaging || Context->PagingActiveNode == 0u)
    return STATUS_SUCCESS;
  for (i = 0u; i < Context->PagingRecordCount; ++i)
    if (Context->PagingRecords[i].Kind != AdmissionPagingMonitoredFence) break;
  if (i == Context->PagingRecordCount) return STATUS_SUCCESS;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
  deadline = KeQueryInterruptTime() + 30000000ULL;
  for (;;) {
    PLIST_ENTRY link;
    BOOLEAN busy = FALSE;
    KeClearEvent(&state->JobEvent);
    ExAcquireFastMutex(&state->Lock);
    for (link = state->Processes.Flink; link != &state->Processes; link = link->Flink) {
      ADMISSION_G3_PROCESS *p = CONTAINING_RECORD(link, ADMISSION_G3_PROCESS, Link);
      if (p->Graph.JobInFlight) { busy = TRUE; break; }
    }
    if (!busy) { *Held = TRUE; result = STATUS_SUCCESS; break; }
    if (iterations == 0u) {
      ADMISSION_G3_PROCESS *p = CONTAINING_RECORD(link, ADMISSION_G3_PROCESS, Link);
      LARGE_INTEGER frequency;
      entry.Qpc = (ULONGLONG)KeQueryPerformanceCounter(&frequency).QuadPart;
      entry.QpcFrequency = (ULONGLONG)frequency.QuadPart;
      entry.ProcessId = p->Graph.ProcessId;
      entry.ActiveProcessId = state->ActiveProcess ? state->ActiveProcess->Graph.ProcessId : 0ULL;
      entry.ActiveFence = state->ActiveFence;
      entry.CompletedFence = state->LastCompletedFence;
      entry.JobState = (p->Graph.JobInFlight ? 1u : 0u) |
          (p->Graph.LeaseToken ? 2u : 0u) | (p->Graph.JobNested ? 4u : 0u);
    }
    ExReleaseFastMutex(&state->Lock);
    if (KeQueryInterruptTime() >= deadline) { result = STATUS_DEVICE_BUSY; break; }
    delay.QuadPart = -10000LL;
    ++iterations;
    (void)KeWaitForSingleObject(&state->JobEvent, Executive, KernelMode, FALSE, &delay);
  }
  if (iterations != 0u) {
    entry.EndQpc = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
    entry.Iterations = iterations;
    entry.Exit = *Held ? AdmissionLiveWaitIdle : AdmissionLiveWaitTimeout;
    entry.Reason = entry.LastReason = AdmissionLiveWaitCpuMutation;
    entry.Index = ~0ULL;
    /* One event per destructive encoded record held by this packet gate.
     * Monitored fences alone bypass it and are not mutation events. */
    for (i = 0u; i < Context->PagingRecordCount; ++i) {
      const ADMISSION_PAGING_RECORD *r = &Context->PagingRecords[i];
      if (r->Kind == AdmissionPagingMonitoredFence) continue;
      entry.Operation = r->Kind == AdmissionPagingPhysical ? 0x100u + r->Plan.Kind :
          r->Kind == AdmissionPagingVirtualFill ? DXGK_OPERATION_VIRTUAL_FILL :
          DXGK_OPERATION_VIRTUAL_TRANSFER;
      entry.AddressKind = r->Kind == AdmissionPagingPhysical ? 2u : 1u;
      entry.Va = r->Kind == AdmissionPagingPhysical ? r->Plan.LocalOffset : r->Virtual.DestinationVa;
      entry.SourceVa = r->Kind == AdmissionPagingPhysical ? r->Plan.SecondLocalOffset : r->Virtual.SourceVa;
      entry.RangeBytes = r->Kind == AdmissionPagingPhysical ? r->Plan.Bytes : r->Bytes;
      AdmissionRecordG3LiveWait(Context, &entry);
    }
  }
  return result;
}
#endif

static VOID AdmissionPagingWorker(_In_ PDEVICE_OBJECT DeviceObject,
                                  _In_opt_ PVOID Opaque) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Opaque;
  ADMISSION_PAGING_NOTIFICATION notification;
  BOOLEAN synchronized = FALSE;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  BOOLEAN mutationHeld = FALSE;
#endif
  NTSTATUS status = STATUS_SUCCESS;
  ULONG index;
  ULONGLONG copiedBytes = 0ULL;
  ULONGLONG executedSeq = 0ULL;
  KIRQL oldIrql;

  LONGLONG workerStart;
  UNREFERENCED_PARAMETER(DeviceObject);
  if (context == NULL)
    return;
  workerStart = KeQueryPerformanceCounter(NULL).QuadPart;
  if (context->PagingDispatchQpc != 0)
    AdmissionPagingProfileAdd(context->PagingProfile.DispatchToWorker,
                              workerStart - context->PagingDispatchQpc);
  InterlockedIncrement(&context->PagingCallbacksActive);
  InterlockedIncrement(&context->PagingWorkersActive);
  context->PresentCopyFaultVa = 0ULL;
  context->PresentCopyFaultWrite = 0u;
  if (context->PresentCopyBytes != 0u)
    status = AdmissionMemoryRuntimeExecutePresent(context,
        context->PresentCopyCommand, context->PresentCopyBytes, &copiedBytes);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (NT_SUCCESS(status)) status = AdmissionPagingAcquireMutation(context, &mutationHeld);
#endif
  for (index = 0u; NT_SUCCESS(status) && index < context->PagingRecordCount; ++index) {
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    if (context->PagingRecords[index].Kind != AdmissionPagingPhysical)
      status = AdmissionG3ExecuteVirtualPaging(
          context, &context->PagingRecords[index]);
    else
#endif
    status = AdmissionMemoryRuntimeExecutePaging(
        context, &context->PagingRecords[index]);
    if (!NT_SUCCESS(status))
      break;
    if (context->PagingRecords[index].Seq > executedSeq)
      executedSeq = context->PagingRecords[index].Seq;
  }
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (mutationHeld)
    ExReleaseFastMutex(&((ADMISSION_G3_STATE *)context->GpuvaG3State)->Lock);
#endif
  if (context->PresentCopyBytes != 0u)
    AdmissionRecordPresentTransfer(context, context->PagingFence,
        context->PresentCopyCommand, context->PresentCopyBytes, copiedBytes, status);
  if (!AdmissionPagingRecordTerminal(context, context->PagingFence, status)) {
    /* A mismatch proves neither ownership nor retirement. Keep the pending
     * packet for recovery; do not notify a fence belonging to other work. */
    context->PagingCompletionStatus = STATUS_INVALID_DEVICE_STATE;
    InterlockedDecrement(&context->PagingWorkersActive);
    InterlockedDecrement(&context->PagingCallbacksActive);
    return;
  }
  context->PagingCompletionStatus = status;
  AdmissionPagingProfileAdd(context->PagingProfile.Worker,
      KeQueryPerformanceCounter(NULL).QuadPart - workerStart);
  RtlZeroMemory(&notification, sizeof(notification));
  notification.Context = context;
  notification.Node = context->PagingActiveNode;
  notification.Fence = context->PagingFence;
  notification.Status = status;
  notification.FaultVa = context->PresentCopyBytes != 0u &&
      !NT_SUCCESS(status) ? context->PresentCopyFaultVa : 0ULL;
  notification.FaultWrite = context->PresentCopyFaultWrite;
  status = context->Interface.DxgkCbSynchronizeExecution(
      context->Interface.DeviceHandle, AdmissionPagingNotifyAtInterrupt,
      &notification, 0u, &synchronized);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  /* M2-f3: after the fence is signalled; the batch ran in record order. */
  if (executedSeq != 0ULL) AdmissionMemoryPoolExecuted(context, executedSeq);
#endif
  KeAcquireSpinLock(&context->PagingLock, &oldIrql);
  if (!NT_SUCCESS(status) || !synchronized) {
    InterlockedCompareExchange(&context->SchedulerFaulted, 0x20000L | __LINE__, 0);
    if (context->PagingFence == notification.Fence) {
      InterlockedExchange(&context->PagingDpcPending, 0);
      InterlockedExchange(&context->PagingPending, 0);
      context->PagingRecordCount = context->PresentCopyBytes = 0u;
    }
  }
  InterlockedDecrement(&context->PagingWorkersActive);
  AdmissionPagingUpdateIdleLocked(context);
  KeReleaseSpinLock(&context->PagingLock, oldIrql);
  AdmissionSchedulerWorkerFinished(context);
  AdmissionDispatchQueuedWork(context);
  KeAcquireSpinLock(&context->PagingLock, &oldIrql);
  InterlockedDecrement(&context->PagingCallbacksActive);
  AdmissionPagingUpdateIdleLocked(context);
  KeReleaseSpinLock(&context->PagingLock, oldIrql);
}

/* rc114: CriticalWorkQueue, not DelayedWorkQueue. VidMm residency waits on
 * these packets, and a DelayedWorkQueue item queues behind the pool prefill
 * (rc88) and the reclaim, whose MmAllocatePagesForMdlEx calls run up to
 * 1.5 s (rc91: DispatchToWorker max 1.06-1.55 s with the prefill on). */
_Use_decl_annotations_ void AdmissionPagingQueueActive(ADMISSION_CONTEXT *Context) {
  IoQueueWorkItem(Context->PagingWorkItem, AdmissionPagingWorker, CriticalWorkQueue, Context);
}

_Use_decl_annotations_ void AdmissionPagingUpdateIdleLocked(ADMISSION_CONTEXT *Context) {
  if (Context->CpuQueueCount == 0u && Context->PagingNodeCount == 0u &&
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingDpcPending, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingCallbacksActive, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingWorkersActive, 0, 0) == 0 &&
      InterlockedCompareExchange(&Context->PagingDpcsActive, 0, 0) == 0)
    KeSetEvent(&Context->PagingIdle, IO_NO_INCREMENT, FALSE);
  else
    KeClearEvent(&Context->PagingIdle);
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
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!NT_SUCCESS(AdmissionPagingReceiptsStart(Context))) {
    IoFreeWorkItem(Context->PagingWorkItem);
    Context->PagingWorkItem = NULL;
    return STATUS_INSUFFICIENT_RESOURCES;
  }
#endif
  Context->PagingRecordCount = 0u;
  Context->PresentCopyBytes = 0u;
  Context->CpuQueueHead = Context->CpuQueueCount = Context->DispatchedFence = 0u;
  Context->PagingNodeHead = Context->PagingNodeCount = Context->PagingNodeDispatchedFence = 0u;
  Context->PagingActiveNode = 0u;
  Context->PagingFence = 0u;
  Context->PagingLastSubmittedFence = 0u;
  Context->PagingLastCompletedFence = 0u;
  Context->PagingCompletionStatus = STATUS_SUCCESS;
  InterlockedExchange(&Context->PagingPending, 0);
  InterlockedExchange(&Context->PagingStopping, 0);
  InterlockedExchange(&Context->PagingDpcPending, 0);
  InterlockedExchange(&Context->PagingWorkersActive, 0);
  InterlockedExchange(&Context->PagingCallbacksActive, 0);
  InterlockedExchange(&Context->PagingDpcsActive, 0);
  if (!AdmissionMemoryMarkPagingReady(&Context->Memory)) {
    IoFreeWorkItem(Context->PagingWorkItem);
    Context->PagingWorkItem = NULL;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
    AdmissionPagingReceiptsStop(Context);
#endif
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
  KIRQL oldIrql;
  PIO_WORKITEM workItem;
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  if (Context->PagingWorkItem == NULL)
    return STATUS_SUCCESS;
  InterlockedExchange(&Context->PagingStopping, 1);
  AdmissionDispatchQueuedWork(Context);
  timeout.QuadPart = -20000000LL;
  status = KeWaitForSingleObject(&Context->PagingIdle, Executive, KernelMode,
                                 FALSE, &timeout);
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  if (!NT_SUCCESS(status) ||
      Context->CpuQueueCount != 0u || Context->PagingNodeCount != 0u ||
      InterlockedCompareExchange(&Context->PagingPending, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingDpcPending, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingCallbacksActive, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingWorkersActive, 0, 0) != 0 ||
      InterlockedCompareExchange(&Context->PagingDpcsActive, 0, 0) != 0) {
    KeReleaseSpinLock(&Context->PagingLock, oldIrql);
    return STATUS_DEVICE_BUSY;
  }
  workItem = Context->PagingWorkItem;
  Context->PagingWorkItem = NULL;
  Context->PagingRecordCount = 0u;
  KeReleaseSpinLock(&Context->PagingLock, oldIrql);
  IoFreeWorkItem(workItem);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  AdmissionPagingReceiptsStop(Context);
#endif
  (void)InterlockedAnd(&Context->FeatureReadyMask,
                       ~((LONG)APPLE_AGX_WDDM_READY_MEMORY_PAGING));
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID AdmissionPagingDpc(
    ADMISSION_CONTEXT *Context) {
  ULONG completedFence;
  KIRQL oldIrql;
  if (Context == NULL ||
      InterlockedExchange(&Context->PagingDpcPending, 0) == 0)
    return;
  InterlockedIncrement(&Context->PagingDpcsActive);
  if (Context->PagingNotifyQpc != 0)
    AdmissionPagingProfileAdd(Context->PagingProfile.NotifyToDpc,
        KeQueryPerformanceCounter(NULL).QuadPart - Context->PagingNotifyQpc);
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  completedFence = Context->PagingFence;
  Context->PagingLastCompletedFence = completedFence;
  if (Context->PresentCopyContext != NULL) {
    KeAcquireSpinLockAtDpcLevel(&Context->SchedulerLock);
    AdmissionContextRetireFence(Context->PresentCopyContext, completedFence);
    KeReleaseSpinLockFromDpcLevel(&Context->SchedulerLock);
    Context->PresentCopyContext = NULL;
  }
  Context->PagingRecordCount = 0u;
  Context->PresentCopyBytes = 0u;
  InterlockedExchange(&Context->PagingPending, 0);
  /* Release the completed slot before the OS can submit its successor. The
   * active DPC counter still prevents teardown while notification is running. */
  AdmissionPagingUpdateIdleLocked(Context);
  KeReleaseSpinLock(&Context->PagingLock, oldIrql);
  if (Context->PagingActiveNode == 0u &&
      InterlockedCompareExchange(&Context->PresentTransferState, 3, 3) == 3 &&
      Context->PresentTransferReceipt.Fence == completedFence) {
    Context->PresentTransferReceipt.NotifyDpc = 1u;
    InterlockedExchange(&Context->PresentTransferState, 4);
  }
  KeAcquireSpinLock(&Context->PagingLock, &oldIrql);
  InterlockedDecrement(&Context->PagingDpcsActive);
  AdmissionPagingUpdateIdleLocked(Context);
  KeReleaseSpinLock(&Context->PagingLock, oldIrql);
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSubmitCommand(
    HANDLE Adapter, const DXGKARG_SUBMITCOMMAND *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  const ADMISSION_PAGING_RECORD *records;
  SIZE_T privateBytes;
  UINT dmaBytes;
  UINT count;
  ADMISSION_PRESENT_BLT_COMMAND presentCommand;
  ULONG privateStage;
  ULONG route;
  NTSTATUS status;
  BOOLEAN trace;

  if (context == NULL || Args == NULL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&presentCommand, sizeof(presentCommand));
  privateStage = AdmissionPresentPrivateMissing;
  if (!Args->Flags.Paging && Args->Flags.Present)
    privateStage = AdmissionPresentPrivateStage(
        Args->pDmaBufferPrivateData, Args->DmaBufferPrivateDataSize,
        &presentCommand);
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  else if (!Args->Flags.Paging)
    privateStage = AdmissionPresentPrivateStage(
        Args->pDmaBufferPrivateData, Args->DmaBufferPrivateDataSize,
        &presentCommand);
#endif
  trace = AdmissionSubmitTraceBegin(context, Args, privateStage,
      privateStage == AdmissionPresentPrivateValid ? &presentCommand : NULL);
  if (!Args->Flags.Paging && Args->Flags.Present &&
      privateStage == AdmissionPresentPrivateValid) {
#if defined(APPLE_AGX_BLT_PROBE_QUALIFICATION)
    InterlockedIncrement(
        (volatile LONG *)&context->BltProbe.PhysicalPresentSubmits);
#endif
    route = AdmissionSubmitRoutePresent;
    AdmissionSubmitTraceValueWindows(context, trace,
        AdmissionSubmitTraceRoute, route);
    status = AdmissionPresentSubmitTraced(context, Args, trace);
    AdmissionSubmitTraceValueWindows(context, trace,
        AdmissionSubmitTraceStatus, (ULONG)status);
    return status;
  }
  if (!Args->Flags.Paging) {
    route = AdmissionSubmitRouteRender;
    AdmissionSubmitTraceValueWindows(context, trace,
        AdmissionSubmitTraceRoute, route);
    status = AdmissionDdiSubmitRender(context, Args);
    AdmissionSubmitTraceValueWindows(context, trace,
        AdmissionSubmitTraceStatus, (ULONG)status);
    return status;
  }
  if (!context->Started || Args->Flags.Reserved != 0u ||
      Args->NodeOrdinal != context->LiveAdditivePaging || Args->EngineOrdinal != 0u ||
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

  return AdmissionCpuQueueSubmit(context, Args, ADMISSION_CPU_PACKET_PAGING,
      records, (UINT)privateBytes);
}

_Use_decl_annotations_ NTSTATUS AdmissionPagingSubmitPresent(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args,
    const VOID *Command, UINT Bytes) {
  if (Context == NULL || Args == NULL || Command == NULL || !Bytes ||
      Bytes > sizeof(Context->PresentCopyCommand) || Context->PagingWorkItem == NULL)
    return STATUS_INVALID_PARAMETER;
  return AdmissionCpuQueueSubmit(Context, Args, ADMISSION_CPU_PACKET_PRESENT, Command, Bytes);
}
