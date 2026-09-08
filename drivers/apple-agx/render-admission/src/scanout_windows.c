#include "render_admission.h"

#define ADMISSION_SCANOUT_TAG 'sRGA'
#define ADMISSION_SCANOUT_TIMEOUT_MS 2000ULL
#define ADMISSION_SCANOUT_MAX_POLLS 40000u

typedef struct _ADMISSION_SCANOUT_RUNTIME {
  ADMISSION_CONTEXT *Adapter;
  APPLE_AGX_FIXED_PANEL Panel;
  volatile LONG PresentGate;
  volatile LONG PendingValid;
  volatile LONG IrqEnabled;
  volatile LONG VsyncNotifyEnabled;
  volatile LONG Faulted;
  volatile LONG64 PendingPhysicalAddress;
  volatile LONG64 PendingSequence;
  volatile LONG64 LastNotifiedSequence;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  KSPIN_LOCK LeaseLock;
  ADMISSION_DISPLAY_OUTPUT_LEASE ActiveLease;
  ADMISSION_DISPLAY_OUTPUT_LEASE FallbackLease;
  volatile LONG FallbackGeneration;
  ULONGLONG FallbackAllocationToken;
  ULONG PresentCount;
  ADMISSION_PRESENT_QUERY PresentHistory[ADMISSION_PRESENT_QUERY_CAPACITY];
#endif
} ADMISSION_SCANOUT_RUNTIME;

_Use_decl_annotations_ ULONG AdmissionScanoutReceiptState(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Context->ScanoutRuntime;
  return runtime == NULL ? 0u :
      1u | (InterlockedCompareExchange(&runtime->IrqEnabled, 0, 0) != 0 ? 2u : 0u);
}

static APPLE_AGX_SCANOUT_U64 AdmissionScanoutNow(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  return (APPLE_AGX_SCANOUT_U64)(KeQueryInterruptTime() / 10000ULL);
}

static BOOLEAN AdmissionScanoutOffsetValid(
    APPLE_AGX_SCANOUT_U32 Offset, APPLE_AGX_SCANOUT_U32 Width) {
  return Width != 0u && Offset <= J313_AGX_G2_POWER_BROKER_SIZE &&
                 Width <= J313_AGX_G2_POWER_BROKER_SIZE - Offset
             ? TRUE
             : FALSE;
}

static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutRead32(
    void *Context, APPLE_AGX_SCANOUT_U32 Offset,
    APPLE_AGX_SCANOUT_U32 *Value) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Context;
  if (runtime == NULL || runtime->Adapter == NULL || Value == NULL ||
      runtime->Adapter->BrokerBase == NULL ||
      !AdmissionScanoutOffsetValid(Offset, sizeof(*Value)))
    return APPLE_AGX_SCANOUT_FALSE;
  *Value = READ_REGISTER_ULONG(
      (volatile ULONG *)(runtime->Adapter->BrokerBase + Offset));
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutRead64(
    void *Context, APPLE_AGX_SCANOUT_U32 Offset,
    APPLE_AGX_SCANOUT_U64 *Value) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Context;
  if (runtime == NULL || runtime->Adapter == NULL || Value == NULL ||
      runtime->Adapter->BrokerBase == NULL ||
      !AdmissionScanoutOffsetValid(Offset, sizeof(*Value)))
    return APPLE_AGX_SCANOUT_FALSE;
  *Value = READ_REGISTER_ULONG64(
      (volatile ULONG64 *)(runtime->Adapter->BrokerBase + Offset));
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutWrite32(
    void *Context, APPLE_AGX_SCANOUT_U32 Offset,
    APPLE_AGX_SCANOUT_U32 Value) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Context;
  if (runtime == NULL || runtime->Adapter == NULL ||
      runtime->Adapter->BrokerBase == NULL ||
      !AdmissionScanoutOffsetValid(Offset, sizeof(Value)))
    return APPLE_AGX_SCANOUT_FALSE;
  WRITE_REGISTER_ULONG(
      (volatile ULONG *)(runtime->Adapter->BrokerBase + Offset), Value);
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutWrite64(
    void *Context, APPLE_AGX_SCANOUT_U32 Offset,
    APPLE_AGX_SCANOUT_U64 Value) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Context;
  if (runtime == NULL || runtime->Adapter == NULL ||
      runtime->Adapter->BrokerBase == NULL ||
      !AdmissionScanoutOffsetValid(Offset, sizeof(Value)))
    return APPLE_AGX_SCANOUT_FALSE;
  WRITE_REGISTER_ULONG64(
      (volatile ULONG64 *)(runtime->Adapter->BrokerBase + Offset), Value);
  return APPLE_AGX_SCANOUT_TRUE;
}

static APPLE_AGX_SCANOUT_BOOL AdmissionScanoutPause(void *Context) {
  LARGE_INTEGER interval;
  UNREFERENCED_PARAMETER(Context);
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return APPLE_AGX_SCANOUT_FALSE;
  interval.QuadPart = -10000LL;
  return NT_SUCCESS(
             KeDelayExecutionThread(KernelMode, FALSE, &interval))
             ? APPLE_AGX_SCANOUT_TRUE
             : APPLE_AGX_SCANOUT_FALSE;
}

static ADMISSION_SCANOUT_RUNTIME *AdmissionScanoutGet(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime;
  if (Context == NULL || Context->ScanoutRuntime == NULL)
    return NULL;
  runtime = (ADMISSION_SCANOUT_RUNTIME *)Context->ScanoutRuntime;
  return runtime->Adapter == Context && runtime->Panel.Started
             ? runtime
             : NULL;
}

#if defined(APPLE_AGX_VISIBLE_SCANOUT_QUALIFICATION)
static NTSTATUS AdmissionScanoutQualifyVisible(
    ADMISSION_CONTEXT *Context, ADMISSION_SCANOUT_RUNTIME *Runtime,
    const ADMISSION_SCANOUT_MEMORY_VIEW *Memory) {
  ADMISSION_VISIBLE_SCANOUT_RECEIPT receipt;
  APPLE_AGX_FIXED_PANEL_RESULT panel_result;
  BOOLEAN (*consume_interrupt)(ADMISSION_CONTEXT *) =
      AdmissionScanoutInterrupt;
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  APPLE_AGX_SCANOUT_U64 started;
  APPLE_AGX_SCANOUT_U64 deadline;
  NTSTATUS status = STATUS_DEVICE_HARDWARE_ERROR;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = ADMISSION_VISIBLE_SCANOUT_RECEIPT_VERSION;
  receipt.Bytes = sizeof(receipt);
  receipt.Status = STATUS_PENDING;
  receipt.CpuAddress = (ULONGLONG)(ULONG_PTR)Memory->CpuAddress;
  receipt.GuestIpaAddress = Memory->GuestIpaAddress;
  receipt.HostPhysicalAddress = Memory->HostPhysicalAddress;
  receipt.SurfaceOffset = 0ULL;
  receipt.SourceVisible = 1u;
  started = AdmissionScanoutNow(Runtime);
  if (!AdmissionVisiblePatternFill(
          Memory->CpuAddress, APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, 590u,
          &receipt.Pattern)) {
    status = STATUS_INVALID_BUFFER_SIZE;
    goto Exit;
  }
  KeMemoryBarrier();
  receipt.Stage = 1u;
  panel_result = AppleAgxFixedPanelCommit(
      &Runtime->Panel, 0u, 0u, APPLE_AGX_SCANOUT_J313_WIDTH,
      APPLE_AGX_SCANOUT_J313_HEIGHT, APPLE_AGX_SCANOUT_J313_STRIDE,
      APPLE_AGX_SCANOUT_FORMAT_BGRA8888);
  if (panel_result != AppleAgxFixedPanelOk) {
    status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    goto Exit;
  }
  receipt.Stage = 2u;
  deadline = AdmissionScanoutNow(Runtime) + ADMISSION_SCANOUT_TIMEOUT_MS;
  if (InterlockedCompareExchange(&Runtime->PresentGate, 1, 0) != 0) {
    status = STATUS_DEVICE_BUSY;
    goto Exit;
  }
  panel_result = AppleAgxFixedPanelQueuePresent(
      &Runtime->Panel, ADMISSION_MEMORY_LOCAL_SEGMENT, 0ULL, &sequence);
  if (panel_result != AppleAgxFixedPanelOk) {
    InterlockedExchange(&Runtime->PresentGate, 0);
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Exit;
  }
  receipt.RequestedSequence = sequence;
  InterlockedExchange64(&Runtime->PendingPhysicalAddress,
                        (LONG64)Memory->GuestIpaAddress);
  InterlockedExchange64(&Runtime->PendingSequence, (LONG64)sequence);
  InterlockedExchange(&Runtime->PendingValid, 1);
  receipt.Stage = 3u;
  while ((APPLE_AGX_SCANOUT_U64)InterlockedCompareExchange64(
             &Runtime->LastNotifiedSequence, 0, 0) != sequence &&
         AdmissionScanoutNow(Runtime) < deadline) {
    (void)consume_interrupt(Context);
    if ((APPLE_AGX_SCANOUT_U64)InterlockedCompareExchange64(
            &Runtime->LastNotifiedSequence, 0, 0) == sequence)
      break;
    if (!AdmissionScanoutPause(Runtime))
      break;
  }
  if ((APPLE_AGX_SCANOUT_U64)InterlockedCompareExchange64(
          &Runtime->LastNotifiedSequence, 0, 0) != sequence) {
    status = STATUS_IO_TIMEOUT;
    goto Exit;
  }
  receipt.Stage = 4u;
  receipt.LatchedSequence = sequence;
  Runtime->Panel.ActiveOffset = 0ULL;
  if (!AdmissionScanoutRead64(Runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE,
          &receipt.AppliedSequence) ||
      !AdmissionScanoutRead64(Runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE,
          &receipt.LatchedSequence) ||
      !AdmissionScanoutRead64(Runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET,
          &receipt.ActiveOffset) ||
      !AdmissionScanoutRead64(Runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_POOL_PA,
          &receipt.PoolPhysicalAddress) ||
      !AdmissionScanoutRead32(Runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_SWAP_ID,
          &receipt.SwapId) ||
      receipt.AppliedSequence != sequence ||
      receipt.LatchedSequence != sequence || receipt.ActiveOffset != 0ULL) {
    status = STATUS_DATA_ERROR;
    goto Exit;
  }
  Runtime->Panel.LastSwapId = receipt.SwapId;
  status = STATUS_SUCCESS;
  receipt.Status = STATUS_SUCCESS;
  if (!AdmissionVisibleScanoutReceiptValid(&receipt))
    status = STATUS_DATA_ERROR;
Exit:
  receipt.Status = (ULONG)status;
  receipt.ElapsedMs = (ULONG)(AdmissionScanoutNow(Runtime) - started);
  AdmissionRecordVisibleScanout(Context, &receipt);
  return status;
}
#endif

#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
_Use_decl_annotations_ NTSTATUS AdmissionScanoutPresentAgxResult(
    ADMISSION_CONTEXT *Context,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    ADMISSION_COMPLETED_OUTPUT *Completed,
    const ADMISSION_TERMINAL_RECEIPT *OutputReceipt,
    const VOID *Source, ULONG SourceBytes,
    ULONGLONG SourceGpuAddress, ULONGLONG SourcePhysicalAddress, ULONG Fence) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_SCANOUT_MEMORY_VIEW memory;
  ADMISSION_LOCAL_MEMORY_VIEW destination;
  ADMISSION_VISIBLE_AGX_RECEIPT receipt;
  ULONGLONG destinationOffset = 0ULL;
  APPLE_AGX_SCANOUT_U64 deadline;
  APPLE_AGX_SCANOUT_U64 started;
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  APPLE_AGX_FIXED_PANEL_RESULT panel_result;
  ADMISSION_PRESENT_QUERY historyRecord;
  ADMISSION_PRESENT_VERIFICATION verified;
  ULONG historyIndex = 0u;
  BOOLEAN (*consume_interrupt)(ADMISSION_CONTEXT *) =
      AdmissionScanoutInterrupt;
  BOOLEAN directFramebuffer = FALSE;
  NTSTATUS status = STATUS_DEVICE_HARDWARE_ERROR;
  RtlZeroMemory(&receipt, sizeof(receipt));
  RtlZeroMemory(&historyRecord, sizeof(historyRecord));
  RtlZeroMemory(&verified, sizeof(verified));
  receipt.Version = ADMISSION_VISIBLE_AGX_RECEIPT_VERSION;
  receipt.Bytes = sizeof(receipt);
  receipt.Status = STATUS_PENDING;
  receipt.Fence = Fence;
  receipt.SourceGpuAddress = SourceGpuAddress;
  receipt.SourcePhysicalAddress = SourcePhysicalAddress;
  receipt.Guard = AdmissionVisibleAgxGuardEntry;
  started = AdmissionScanoutNow(runtime);
  if (Context == NULL || Packet == NULL || Completed == NULL ||
      OutputReceipt == NULL ||
      runtime == NULL || Source == NULL ||
      SourceBytes < 1024u || Fence == 0u ||
      Packet->Fence != Fence ||
      SourcePhysicalAddress > MAXULONGLONG - SourceBytes)
    goto Exit;
  receipt.Guard = AdmissionVisibleAgxGuardArguments;
  {
    KIRQL oldIrql;
    KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
    historyIndex = runtime->PresentCount;
    KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
  }
  if (historyIndex >= ADMISSION_PRESENT_QUERY_CAPACITY) {
    status = STATUS_BUFFER_OVERFLOW;
    goto Exit;
  }
  if (!runtime->Panel.Committed || !runtime->Panel.Visible)
    goto Exit;
  receipt.Guard = AdmissionVisibleAgxGuardPanel;
  directFramebuffer =
      SourceBytes == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE &&
      Packet->DestinationCpuToken == (ULONGLONG)(ULONG_PTR)Source &&
      Packet->DestinationGpuVa == SourceGpuAddress &&
      Packet->DestinationPhysical == SourcePhysicalAddress &&
      Packet->DestinationBytes == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE;
  receipt.CapturedValid = directFramebuffer
      ? (Packet->AllocationToken != 0ULL ? 1u : 0u)
      : (Packet->VisibleDestinationCpuToken != 0ULL &&
                 Packet->VisibleDestinationGpuVa != 0ULL &&
                 Packet->VisibleDestinationPhysical != 0ULL &&
                 Packet->VisibleDestinationAllocationToken != 0ULL &&
                 Packet->VisibleDestinationBytes != 0u
             ? 1u
             : 0u);
  receipt.CapturedFence = Packet->Fence;
  receipt.DestinationAllocationToken = directFramebuffer
      ? Packet->AllocationToken
      : Packet->VisibleDestinationAllocationToken;
  if (receipt.CapturedValid != 1u ||
      Packet->Fence != Fence ||
      receipt.DestinationAllocationToken == 0ULL)
    goto Exit;
  receipt.Guard = AdmissionVisibleAgxGuardCapturedDestination;
  if (!NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(Context, &memory)))
    goto Exit;
  receipt.Guard = AdmissionVisibleAgxGuardScanoutView;
  destination.CpuAddress = (PVOID)(ULONG_PTR)(directFramebuffer
      ? Packet->DestinationCpuToken
      : Packet->VisibleDestinationCpuToken);
  destination.GpuVirtualAddress = directFramebuffer
      ? Packet->DestinationGpuVa
      : Packet->VisibleDestinationGpuVa;
  destination.HostPhysicalAddress = directFramebuffer
      ? Packet->DestinationPhysical
      : Packet->VisibleDestinationPhysical;
  destination.Bytes = directFramebuffer
      ? Packet->DestinationBytes
      : Packet->VisibleDestinationBytes;
  receipt.DestinationCpuAddress =
      (ULONGLONG)(ULONG_PTR)destination.CpuAddress;
  receipt.DestinationPhysicalAddress = destination.HostPhysicalAddress;
  receipt.DestinationBytes = destination.Bytes;
  if (destination.CpuAddress == NULL ||
      memory.Bytes < APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      destination.GpuVirtualAddress < memory.GpuVirtualAddress ||
      destination.GpuVirtualAddress - memory.GpuVirtualAddress >
          memory.Bytes - APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      destination.Bytes < APPLE_AGX_SCANOUT_J313_SURFACE_SIZE)
    goto Exit;
  receipt.Guard = AdmissionVisibleAgxGuardDestinationRange;
  destinationOffset =
      destination.GpuVirtualAddress - memory.GpuVirtualAddress;
  receipt.DestinationOffset = destinationOffset;
  if ((destinationOffset & (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL ||
      memory.HostPhysicalAddress > MAXULONGLONG - destinationOffset ||
      memory.GuestIpaAddress > MAXULONGLONG - destinationOffset ||
      destination.HostPhysicalAddress !=
          memory.HostPhysicalAddress + destinationOffset)
    goto Exit;
  receipt.DestinationGuestIpa = memory.GuestIpaAddress + destinationOffset;
  receipt.Guard = AdmissionVisibleAgxGuardDestinationIdentity;
  if (!AdmissionScanoutRead64(runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET,
          &receipt.ActiveOffsetBefore) ||
      receipt.ActiveOffsetBefore == destinationOffset ||
      (!directFramebuffer &&
       SourcePhysicalAddress < receipt.DestinationPhysicalAddress +
                                   APPLE_AGX_SCANOUT_J313_SURFACE_SIZE &&
       receipt.DestinationPhysicalAddress <
           SourcePhysicalAddress + SourceBytes)) {
    status = STATUS_CONFLICTING_ADDRESSES;
    goto Exit;
  }
  receipt.Guard = AdmissionVisibleAgxGuardActiveSurface;
  if ((directFramebuffer &&
       !AdmissionVisibleAgxUseFramebuffer(Source, SourceBytes, &receipt)) ||
      (!directFramebuffer &&
       !AdmissionVisibleAgxScale16x16(
           Source, SourceBytes, destination.CpuAddress,
           APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, &receipt))) {
    status = STATUS_INVALID_BUFFER_SIZE;
    goto Exit;
  }
  receipt.Guard = AdmissionVisibleAgxGuardScaled;
  KeMemoryBarrier();
  receipt.Stage = 1u;
  if (InterlockedCompareExchange(&runtime->PresentGate, 1, 0) != 0) {
    status = STATUS_DEVICE_BUSY;
    goto Exit;
  }
  panel_result = AppleAgxFixedPanelQueuePresent(
      &runtime->Panel, ADMISSION_MEMORY_LOCAL_SEGMENT, destinationOffset,
      &sequence);
  if (panel_result != AppleAgxFixedPanelOk) {
    InterlockedExchange(&runtime->PresentGate, 0);
    goto Exit;
  }
  if (!AdmissionCompletedOutputMarkPublished(Completed, Fence, sequence)) {
    (void)AdmissionCompletedOutputMarkOwnershipUnknown(
        Completed, Fence, sequence, (ULONG)STATUS_INVALID_DEVICE_STATE);
    status = STATUS_INVALID_DEVICE_STATE;
    goto Exit;
  }
  receipt.RequestedSequence = sequence;
  InterlockedExchange64(&runtime->PendingPhysicalAddress,
                        (LONG64)(Context->Memory.Topology.Local.Base +
                                 destinationOffset));
  InterlockedExchange64(&runtime->PendingSequence, (LONG64)sequence);
  InterlockedExchange(&runtime->PendingValid, 1);
  receipt.Stage = 2u;
  receipt.Guard = AdmissionVisibleAgxGuardQueued;
  deadline = AdmissionScanoutNow(runtime) + ADMISSION_SCANOUT_TIMEOUT_MS;
  while ((APPLE_AGX_SCANOUT_U64)InterlockedCompareExchange64(
             &runtime->LastNotifiedSequence, 0, 0) != sequence &&
         AdmissionScanoutNow(runtime) < deadline) {
    (void)consume_interrupt(Context);
    if ((APPLE_AGX_SCANOUT_U64)InterlockedCompareExchange64(
            &runtime->LastNotifiedSequence, 0, 0) == sequence)
      break;
    if (!AdmissionScanoutPause(runtime))
      break;
  }
  if (!AdmissionScanoutRead64(runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE,
          &receipt.AppliedSequence) ||
      !AdmissionScanoutRead64(runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE,
          &receipt.LatchedSequence) ||
      !AdmissionScanoutRead64(runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET,
          &receipt.ActiveOffsetAfter) ||
      !AdmissionScanoutRead32(runtime,
          APPLE_AGX_SCANOUT_MMIO_OFFSET + APPLE_AGX_SCANOUT_REG_SWAP_ID,
          &receipt.SwapId) ||
      receipt.AppliedSequence != sequence || receipt.LatchedSequence != sequence ||
      receipt.ActiveOffsetAfter != destinationOffset) {
    status = STATUS_DATA_ERROR;
    goto Exit;
  }
  if (!AdmissionCompletedOutputMarkLatched(Completed, Fence, sequence)) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Exit;
  }
  receipt.Stage = 3u;
  receipt.Guard = AdmissionVisibleAgxGuardComplete;
  status = STATUS_SUCCESS;
  receipt.Status = STATUS_SUCCESS;
  if (!AdmissionVisibleAgxReceiptValid(&receipt))
    status = STATUS_DATA_ERROR;
  if (NT_SUCCESS(status)) {
    verified.CandidateBuild = APPLE_AGX_VERSION_BUILD;
    verified.BootGeneration = Context->RenderCorrelation.BootGeneration;
    verified.Index = historyIndex;
    verified.Purpose = AdmissionPresentPurposeRenderFrame;
    verified.Fence = Fence;
    verified.DestinationIndex = historyIndex;
    verified.ExpectedColor = Completed->View.ExpectedColor;
    verified.PixelsExpected = Completed->View.RenderedBytes / 4u;
    verified.PixelsVerified = OutputReceipt->OutputPixelsExpected;
    verified.Format = Completed->View.AllocationFormat;
    verified.Width = Completed->View.AllocationWidth;
    verified.Height = Completed->View.AllocationHeight;
    verified.Pitch = Completed->View.AllocationPitch;
    verified.AllocationToken = receipt.DestinationAllocationToken;
    verified.Sequence = sequence;
    verified.ActiveOffset = receipt.ActiveOffsetAfter;
    verified.PhysicalAddress = receipt.DestinationPhysicalAddress;
    verified.ContentHash = OutputReceipt->OutputTargetFnv1a;
    if (!(OutputReceipt->ValidMask & ADMISSION_TERMINAL_VALID_OUTPUT) ||
        OutputReceipt->Fence != Fence ||
        OutputReceipt->DestinationPhysical !=
            Completed->View.RenderedPhysicalAddress ||
        OutputReceipt->OutputBytesExamined !=
            Completed->View.RenderedBytes ||
        !AdmissionPresentQueryBuild(&historyRecord, &verified))
      status = STATUS_DATA_ERROR;
  }
  if (NT_SUCCESS(status) &&
      !AdmissionCompletedOutputRecordPresentation(
          Completed, Fence, STATUS_SUCCESS))
    status = STATUS_INVALID_DEVICE_STATE;
  if (NT_SUCCESS(status)) {
    KIRQL oldIrql;
    KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
    if (runtime->PresentCount != historyIndex ||
        !AdmissionCompletedOutputTransferToDisplay(
            Completed, &runtime->ActiveLease, Fence))
      status = STATUS_INVALID_DEVICE_STATE;
    else {
      historyRecord.PublishedToQuery = 1u;
      runtime->PresentHistory[historyIndex] = historyRecord;
      ++runtime->PresentCount;
    }
    KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
  }
Exit:
  if (Completed != NULL && !NT_SUCCESS(status)) {
    if (Completed->Phase == AdmissionCompletedOutputPublishedPending ||
        Completed->Phase == AdmissionCompletedOutputLatched)
      (void)AdmissionCompletedOutputMarkOwnershipUnknown(
          Completed, Fence, sequence, (ULONG)status);
    else if (Completed->Phase == AdmissionCompletedOutputPresenting &&
             Completed->PresentationAttempted == 0u)
      (void)AdmissionCompletedOutputRecordPresentation(
          Completed, Fence, (ULONG)status);
  }
  receipt.Status = (ULONG)status;
  receipt.ElapsedMs = runtime == NULL ? 0u :
      (ULONG)(AdmissionScanoutNow(runtime) - started);
  return status;
}
#endif

_Use_decl_annotations_ NTSTATUS AdmissionScanoutStart(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime;
  ADMISSION_SCANOUT_MEMORY_VIEW memory;
  APPLE_AGX_SCANOUT_IO io;
  APPLE_AGX_SCANOUT_RESULT scanout_result;
  APPLE_AGX_FIXED_PANEL_RESULT result;
  if (Context == NULL || Context->ScanoutRuntime != NULL ||
      Context->BrokerBase == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  if (!NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(Context, &memory)) ||
      memory.Bytes != APPLE_AGX_SCANOUT_J313_POOL_SIZE ||
      (memory.GuestIpaAddress &
       (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL)
    return STATUS_INVALID_ADDRESS;
  runtime = ExAllocatePool2(
      POOL_FLAG_NON_PAGED, sizeof(*runtime), ADMISSION_SCANOUT_TAG);
  if (runtime == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(runtime, sizeof(*runtime));
  runtime->Adapter = Context;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  KeInitializeSpinLock(&runtime->LeaseLock);
  AdmissionDisplayOutputLeaseInitialize(&runtime->ActiveLease);
  AdmissionDisplayOutputLeaseInitialize(&runtime->FallbackLease);
#endif
  RtlZeroMemory(&io, sizeof(io));
  io.Context = runtime;
  io.NowMs = AdmissionScanoutNow;
  io.Read32 = AdmissionScanoutRead32;
  io.Read64 = AdmissionScanoutRead64;
  io.Write32 = AdmissionScanoutWrite32;
  io.Write64 = AdmissionScanoutWrite64;
  io.Pause = AdmissionScanoutPause;
  result = AppleAgxFixedPanelInitialize(
      &runtime->Panel, &io, memory.GuestIpaAddress, 1ULL,
      ADMISSION_SCANOUT_MAX_POLLS);
  if (result != AppleAgxFixedPanelOk) {
    ExFreePoolWithTag(runtime, ADMISSION_SCANOUT_TAG);
    return STATUS_INVALID_PARAMETER;
  }
  /* Qualify before REGISTER so an ABI-v1 recovery broker remains untouched. */
  scanout_result = AppleAgxScanoutQualify(&runtime->Panel.Scanout);
  if (scanout_result != AppleAgxScanoutOk ||
      runtime->Panel.Scanout.AbiVersion != APPLE_AGX_SCANOUT_ABI_VERSION_V2 ||
      (runtime->Panel.Scanout.Capabilities &
       APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES) !=
          APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES) {
    ExFreePoolWithTag(runtime, ADMISSION_SCANOUT_TAG);
    return STATUS_NOT_SUPPORTED;
  }
  result = AppleAgxFixedPanelStart(
      &runtime->Panel,
      AdmissionScanoutNow(runtime) + ADMISSION_SCANOUT_TIMEOUT_MS);
  if (result != AppleAgxFixedPanelOk) {
    if (runtime->Panel.Ownership == AppleAgxFixedPanelOwnershipUnknown) {
      Context->ScanoutRuntime = runtime;
      return STATUS_DEVICE_BUSY;
    }
    ExFreePoolWithTag(runtime, ADMISSION_SCANOUT_TAG);
    return STATUS_DEVICE_HARDWARE_ERROR;
  }
  Context->ScanoutRuntime = runtime;
  /* Physical latch retirement is an internal runtime requirement, including
   * initial modeset before Windows subscribes to VSync notifications. */
  InterlockedExchange(&runtime->IrqEnabled, 1);
  if (AppleAgxScanoutEnableInterrupts(&runtime->Panel.Scanout) !=
      AppleAgxScanoutOk) {
    InterlockedExchange(&runtime->IrqEnabled, 0);
    return STATUS_DEVICE_HARDWARE_ERROR;
  }
#if defined(APPLE_AGX_VISIBLE_SCANOUT_QUALIFICATION)
  {
    NTSTATUS qualification_status =
        AdmissionScanoutQualifyVisible(Context, runtime, &memory);
    if (!NT_SUCCESS(qualification_status))
      return qualification_status;
  }
#endif
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutStop(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime;
  APPLE_AGX_SCANOUT_RESULT scanout_result;
  APPLE_AGX_FIXED_PANEL_RESULT result;
  if (Context == NULL)
    return STATUS_INVALID_PARAMETER;
  runtime = (ADMISSION_SCANOUT_RUNTIME *)Context->ScanoutRuntime;
  if (runtime == NULL)
    return STATUS_SUCCESS;
  InterlockedExchange(&runtime->PresentGate, 1);
  if (InterlockedCompareExchange(&runtime->PendingValid, 0, 0) != 0)
    return STATUS_DEVICE_BUSY;
  if (InterlockedExchange(&runtime->IrqEnabled, 0) != 0 &&
      AppleAgxScanoutDisableInterrupts(&runtime->Panel.Scanout) !=
          AppleAgxScanoutOk)
    return STATUS_DEVICE_BUSY;
  if (runtime->Panel.Ownership == AppleAgxFixedPanelRegistered) {
    result = AppleAgxFixedPanelStop(
        &runtime->Panel,
        AdmissionScanoutNow(runtime) + ADMISSION_SCANOUT_TIMEOUT_MS);
    if (result != AppleAgxFixedPanelOk)
      return STATUS_DEVICE_BUSY;
  } else if (runtime->Panel.Ownership ==
             AppleAgxFixedPanelOwnershipUnknown) {
    scanout_result = AppleAgxScanoutRelease(
        &runtime->Panel.Scanout,
        AdmissionScanoutNow(runtime) + ADMISSION_SCANOUT_TIMEOUT_MS);
    if (scanout_result != AppleAgxScanoutOk)
      return STATUS_DEVICE_BUSY;
    runtime->Panel.Ownership = AppleAgxFixedPanelUnregistered;
  }
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  if (runtime->ActiveLease.Active &&
      !AdmissionDisplayOutputLeaseRetire(&runtime->ActiveLease))
    return STATUS_DEVICE_BUSY;
  if (runtime->FallbackLease.Active &&
      !AdmissionDisplayOutputLeaseRetire(&runtime->FallbackLease))
    return STATUS_DEVICE_BUSY;
#endif
  Context->ScanoutRuntime = NULL;
  ExFreePoolWithTag(runtime, ADMISSION_SCANOUT_TAG);
  return STATUS_SUCCESS;
}

#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
_Use_decl_annotations_ BOOLEAN AdmissionScanoutAllowsRender(
    ADMISSION_CONTEXT *Context, const ADMISSION_ALLOCATION_OBJECT *Owner) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  BOOLEAN allowed;
  KIRQL oldIrql;
  if (runtime == NULL || Owner == NULL)
    return FALSE;
  KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
  allowed = AdmissionDisplayOutputLeaseAllowsRender(
      &runtime->ActiveLease, Owner) ? TRUE : FALSE;
  KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
  return allowed;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutRetireQualification(
    ADMISSION_CONTEXT *Context, ADMISSION_RETIREMENT_QUERY *Query) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_SCANOUT_MEMORY_VIEW memory;
  ADMISSION_DISPLAY_OUTPUT_LEASE temporary;
  ADMISSION_ALLOCATION_OBJECT *fallbackOwner = NULL;
  ULONGLONG fallbackToken = 0ULL;
  ULONGLONG expectedActiveOffset = 0ULL;
  APPLE_AGX_SCANOUT_U64 active = 0ULL;
  APPLE_AGX_SCANOUT_U64 sequence = 0ULL;
  APPLE_AGX_SCANOUT_U64 applied = 0ULL;
  APPLE_AGX_SCANOUT_U64 latched = 0ULL;
  APPLE_AGX_SCANOUT_U64 deadline;
  APPLE_AGX_FIXED_PANEL_RESULT result;
  BOOLEAN (*consumeInterrupt)(ADMISSION_CONTEXT *) =
      AdmissionScanoutInterrupt;
  KIRQL oldIrql;
  BOOLEAN captured = FALSE;
  BOOLEAN moved = FALSE;
  AdmissionDisplayOutputLeaseInitialize(&temporary);
  if (runtime == NULL || Query == NULL ||
      Query->Magic != ADMISSION_RETIREMENT_QUERY_MAGIC ||
      Query->Version != ADMISSION_RETIREMENT_QUERY_VERSION ||
      Query->Command != AdmissionRetirementCommandExecute ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !AdmissionPlatformRuntimeReady(Context) ||
      !NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(Context, &memory)))
    return STATUS_INVALID_PARAMETER;
  KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
  if (runtime->ActiveLease.Active && runtime->FallbackLease.Active &&
      runtime->ActiveLease.Owner != runtime->FallbackLease.Owner &&
      runtime->FallbackAllocationToken != 0ULL &&
      runtime->FallbackLease.View.AllocationGpuAddress ==
          memory.GpuVirtualAddress &&
      runtime->FallbackLease.View.AllocationPhysicalAddress ==
          memory.HostPhysicalAddress &&
      runtime->FallbackLease.View.AllocationCpuAddress == memory.CpuAddress &&
      runtime->FallbackLease.View.AllocationBytes ==
          APPLE_AGX_SCANOUT_J313_SURFACE_SIZE &&
      runtime->FallbackLease.View.AllocationWidth ==
          APPLE_AGX_SCANOUT_J313_WIDTH &&
      runtime->FallbackLease.View.AllocationHeight ==
          APPLE_AGX_SCANOUT_J313_HEIGHT &&
      runtime->FallbackLease.View.AllocationPitch ==
          APPLE_AGX_SCANOUT_J313_STRIDE &&
      runtime->FallbackLease.View.AllocationFormat ==
          (ULONG)D3DDDIFMT_A8R8G8B8 &&
      runtime->ActiveLease.View.AllocationGpuAddress >=
          memory.GpuVirtualAddress) {
    fallbackOwner = runtime->FallbackLease.Owner;
    fallbackToken = runtime->FallbackAllocationToken;
    expectedActiveOffset =
        runtime->ActiveLease.View.AllocationGpuAddress -
        memory.GpuVirtualAddress;
    captured = AdmissionDisplayOutputLeaseCapture(
        &temporary, runtime->FallbackLease.Generation, 0u,
        &runtime->FallbackLease.View, fallbackOwner) ? TRUE : FALSE;
  }
  KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
  if (!captured)
    return STATUS_DEVICE_NOT_READY;
  if (!AdmissionScanoutRead64(
          runtime, APPLE_AGX_SCANOUT_MMIO_OFFSET +
                       APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET, &active) ||
      active == 0ULL || active != expectedActiveOffset ||
      InterlockedCompareExchange(&runtime->PresentGate, 1, 0) != 0) {
    (void)AdmissionDisplayOutputLeaseRetire(&temporary);
    return STATUS_DEVICE_BUSY;
  }
  result = AppleAgxFixedPanelQueuePresent(
      &runtime->Panel, ADMISSION_MEMORY_LOCAL_SEGMENT, 0ULL, &sequence);
  if (result != AppleAgxFixedPanelOk) {
    InterlockedExchange(&runtime->PresentGate, 0);
    (void)AdmissionDisplayOutputLeaseRetire(&temporary);
    return STATUS_DEVICE_BUSY;
  }
  InterlockedExchange64(&runtime->PendingPhysicalAddress,
                        (LONG64)Context->Memory.Topology.Local.Base);
  InterlockedExchange64(&runtime->PendingSequence, (LONG64)sequence);
  InterlockedExchange(&runtime->PendingValid, 1);
  deadline = AdmissionScanoutNow(runtime) + ADMISSION_SCANOUT_TIMEOUT_MS;
  while ((APPLE_AGX_SCANOUT_U64)InterlockedCompareExchange64(
             &runtime->LastNotifiedSequence, 0, 0) != sequence &&
         AdmissionScanoutNow(runtime) < deadline) {
    (void)consumeInterrupt(Context);
    if (!AdmissionScanoutPause(runtime))
      break;
  }
  if (!AdmissionScanoutRead64(
          runtime, APPLE_AGX_SCANOUT_MMIO_OFFSET +
                       APPLE_AGX_SCANOUT_REG_APPLIED_SEQUENCE, &applied) ||
      !AdmissionScanoutRead64(
          runtime, APPLE_AGX_SCANOUT_MMIO_OFFSET +
                       APPLE_AGX_SCANOUT_REG_LATCHED_SEQUENCE, &latched) ||
      !AdmissionScanoutRead64(
          runtime, APPLE_AGX_SCANOUT_MMIO_OFFSET +
                       APPLE_AGX_SCANOUT_REG_ACTIVE_OFFSET, &active) ||
      applied != sequence || latched != sequence || active != 0ULL) {
    InterlockedExchange(&runtime->Faulted, 1);
    (void)AdmissionDisplayOutputLeaseRetire(&temporary);
    return STATUS_DEVICE_BUSY;
  }
  KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
  if (runtime->FallbackLease.Active &&
      runtime->FallbackLease.Owner == fallbackOwner &&
      runtime->FallbackAllocationToken == fallbackToken)
    moved = AdmissionDisplayOutputLeaseMove(
        &runtime->ActiveLease, &runtime->FallbackLease) ? TRUE : FALSE;
  KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
  if (!AdmissionDisplayOutputLeaseRetire(&temporary))
    return STATUS_INVALID_DEVICE_STATE;
  if (!moved || !AdmissionRetirementQueryBuild(
                    Query, APPLE_AGX_VERSION_BUILD,
                    Context->RenderCorrelation.BootGeneration,
                    sequence, fallbackToken, active,
                    memory.HostPhysicalAddress))
    return STATUS_INVALID_DEVICE_STATE;
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutRetireAllocation(
    ADMISSION_CONTEXT *Context, ADMISSION_ALLOCATION_OBJECT *Owner) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  KIRQL oldIrql;
  BOOLEAN active;
  if (runtime == NULL || Owner == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
  active = AdmissionDisplayOutputLeaseMatches(
      &runtime->ActiveLease, Owner) ? TRUE : FALSE;
  KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
  return active ? STATUS_DEVICE_BUSY : STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutQueryQualification(
    ADMISSION_CONTEXT *Context, ADMISSION_PRESENT_QUERY *Query) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_PRESENT_QUERY result;
  KIRQL oldIrql;
  if (runtime == NULL || Query == NULL ||
      Query->Magic != ADMISSION_PRESENT_QUERY_MAGIC ||
      Query->Version != ADMISSION_PRESENT_QUERY_VERSION ||
      Query->Index >= ADMISSION_PRESENT_QUERY_CAPACITY)
    return STATUS_INVALID_PARAMETER;
  if (!AdmissionPlatformRuntimeReady(Context))
    return STATUS_SUCCESS;
  RtlZeroMemory(&result, sizeof(result));
  result.Magic = ADMISSION_PRESENT_QUERY_MAGIC;
  result.Version = ADMISSION_PRESENT_QUERY_VERSION;
  result.Index = Query->Index;
  KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
  result.PresentCount = runtime->PresentCount;
  if (Query->Index < runtime->PresentCount)
    result = runtime->PresentHistory[Query->Index];
  KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
  *Query = result;
  return STATUS_SUCCESS;
}
#endif

_Use_decl_annotations_ NTSTATUS AdmissionScanoutCommit(
    ADMISSION_CONTEXT *Context, ULONG Width, ULONG Height, ULONG Stride,
    D3DDDIFORMAT Format) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  if (runtime == NULL)
    return STATUS_DEVICE_NOT_READY;
  return AppleAgxFixedPanelCommit(
             &runtime->Panel, 0u, 0u, Width, Height, Stride,
             Format == D3DDDIFMT_A8R8G8B8
                 ? APPLE_AGX_SCANOUT_FORMAT_BGRA8888
                 : 0u) == AppleAgxFixedPanelOk
             ? STATUS_SUCCESS
             : STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutSetVisible(
    ADMISSION_CONTEXT *Context, BOOLEAN Visible) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  if (runtime == NULL)
    return STATUS_DEVICE_NOT_READY;
  return AppleAgxFixedPanelSetVisible(
             &runtime->Panel, 0u,
             Visible ? APPLE_AGX_SCANOUT_TRUE
                     : APPLE_AGX_SCANOUT_FALSE) == AppleAgxFixedPanelOk
             ? STATUS_SUCCESS
             : STATUS_DEVICE_HARDWARE_ERROR;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutQueuePresent(
    ADMISSION_CONTEXT *Context,
    const DXGKARG_SETVIDPNSOURCEADDRESS *Args) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_ALLOCATION_HANDLE *allocation;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT address_result;
  APPLE_AGX_FIXED_PANEL_RESULT result;
  APPLE_AGX_U64 surface_offset = 0ULL;
  APPLE_AGX_U64 sequence = 0ULL;
  ADMISSION_DISPLAY_OUTPUT_LEASE fallbackCandidate;
  ADMISSION_BACKEND_OUTPUT_VIEW fallbackView;
  ADMISSION_LOCAL_MEMORY_VIEW fallbackMemory;
  BOOLEAN fallbackCandidateValid = FALSE;
  ULONG fallbackGeneration;
  KIRQL oldIrql;
  AdmissionDisplayOutputLeaseInitialize(&fallbackCandidate);
  RtlZeroMemory(&fallbackView, sizeof(fallbackView));
  RtlZeroMemory(&fallbackMemory, sizeof(fallbackMemory));
  if (runtime == NULL || Args == NULL || Args->VidPnSourceId != 0u ||
      Args->hAllocation == NULL ||
      Args->PrimarySegment != ADMISSION_MEMORY_LOCAL_SEGMENT ||
      Args->PrimaryAddress.QuadPart <= 0 ||
      !Context->DisplayActive || !Context->SourceVisible ||
      InterlockedCompareExchange(&runtime->IrqEnabled, 0, 0) == 0)
    return STATUS_INVALID_PARAMETER;
  allocation = (ADMISSION_ALLOCATION_HANDLE *)Args->hAllocation;
  if (allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC)
    return STATUS_INVALID_HANDLE;
  description = &allocation->Object.Description;
  if (!AdmissionAllocationDescriptionValid(description) ||
      description->Width != APPLE_AGX_SCANOUT_J313_WIDTH ||
      description->Height != APPLE_AGX_SCANOUT_J313_HEIGHT ||
      description->Pitch != APPLE_AGX_SCANOUT_J313_STRIDE ||
      description->BytesPerPixel != 4u ||
      description->Size != APPLE_AGX_SCANOUT_J313_SURFACE_SIZE ||
      description->Format != (UINT)D3DDDIFMT_A8R8G8B8)
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
  address_result = AppleAgxLocalSegmentAddressToGpuVa(
      ADMISSION_MEMORY_LOCAL_SEGMENT, Args->PrimarySegment,
      Context->Memory.Topology.Local.Base,
      Context->Memory.LocalAllocationBytes, 0ULL,
      (APPLE_AGX_U64)Args->PrimaryAddress.QuadPart,
      APPLE_AGX_SCANOUT_J313_SURFACE_SIZE, 0ULL, &surface_offset);
  if (address_result != AppleAgxLocalSegmentAddressOk)
    return STATUS_INVALID_ADDRESS;
  if (surface_offset == 0ULL) {
    if (!NT_SUCCESS(AdmissionMemoryRuntimeResolveLocal(
            Context, (ULONGLONG)Args->PrimaryAddress.QuadPart,
            description->Size, 0ULL, &fallbackMemory)))
      return STATUS_INVALID_ADDRESS;
    fallbackView.AllocationCpuAddress = fallbackMemory.CpuAddress;
    fallbackView.AllocationGpuAddress = fallbackMemory.GpuVirtualAddress;
    fallbackView.AllocationPhysicalAddress =
        fallbackMemory.HostPhysicalAddress;
    fallbackView.AllocationBytes = (ULONG)description->Size;
    fallbackView.RenderedCpuAddress = fallbackMemory.CpuAddress;
    fallbackView.RenderedGpuAddress = fallbackMemory.GpuVirtualAddress;
    fallbackView.RenderedPhysicalAddress = fallbackMemory.HostPhysicalAddress;
    fallbackView.RenderedBytes = (ULONG)description->Size;
    fallbackView.AllocationWidth = fallbackView.RenderWidth =
        description->Width;
    fallbackView.AllocationHeight = fallbackView.RenderHeight =
        description->Height;
    fallbackView.AllocationPitch = fallbackView.RenderPitch =
        description->Pitch;
    fallbackView.AllocationFormat = description->Format;
    fallbackView.Framebuffer = APPLE_AGX_TRUE;
    fallbackGeneration = (ULONG)InterlockedIncrement(
        &runtime->FallbackGeneration);
    if (fallbackGeneration == 0u)
      fallbackGeneration = (ULONG)InterlockedIncrement(
          &runtime->FallbackGeneration);
    if (!AdmissionDisplayOutputLeaseCapture(
            &fallbackCandidate, fallbackGeneration, 0u,
            &fallbackView, &allocation->Object))
      return STATUS_DEVICE_BUSY;
    fallbackCandidateValid = TRUE;
  }
  if (InterlockedCompareExchange(&runtime->PresentGate, 1, 0) != 0) {
    if (fallbackCandidateValid)
      (void)AdmissionDisplayOutputLeaseRetire(&fallbackCandidate);
    return STATUS_DEVICE_BUSY;
  }
  InterlockedExchange64(
      &runtime->PendingPhysicalAddress,
      Args->PrimaryAddress.QuadPart);
  result = AppleAgxFixedPanelQueuePresent(
      &runtime->Panel, Args->PrimarySegment, surface_offset, &sequence);
  if (result != AppleAgxFixedPanelOk) {
    InterlockedExchange64(&runtime->PendingPhysicalAddress, 0);
    InterlockedExchange(&runtime->PresentGate, 0);
    if (fallbackCandidateValid)
      (void)AdmissionDisplayOutputLeaseRetire(&fallbackCandidate);
    return result == AppleAgxFixedPanelPresentPending
               ? STATUS_DEVICE_BUSY
               : STATUS_DEVICE_HARDWARE_ERROR;
  }
  InterlockedExchange64(&runtime->PendingSequence, (LONG64)sequence);
  InterlockedExchange(&runtime->PendingValid, 1);
  if (fallbackCandidateValid) {
    BOOLEAN retained;
    KeAcquireSpinLock(&runtime->LeaseLock, &oldIrql);
    if (AdmissionDisplayOutputLeaseMatches(
            &runtime->FallbackLease, &allocation->Object)) {
      retained = AdmissionDisplayOutputLeaseRetire(&fallbackCandidate)
                     ? TRUE
                     : FALSE;
    } else {
      retained = AdmissionDisplayOutputLeaseMove(
          &runtime->FallbackLease, &fallbackCandidate) ? TRUE : FALSE;
    }
    if (retained)
      runtime->FallbackAllocationToken =
          (ULONGLONG)(ULONG_PTR)Args->hAllocation;
    KeReleaseSpinLock(&runtime->LeaseLock, oldIrql);
    if (!retained) {
      if (fallbackCandidate.Active)
        (void)AdmissionDisplayOutputLeaseRetire(&fallbackCandidate);
      return STATUS_INVALID_DEVICE_STATE;
    }
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutInterrupt(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  APPLE_AGX_SCANOUT_U32 irq_status = 0u;
  APPLE_AGX_SCANOUT_U64 latched_sequence = 0ULL;
  APPLE_AGX_SCANOUT_RESULT result;
  DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
  LONG64 pending_sequence;
  LONG64 physical_address;
  BOOLEAN notify_vsync;
  if (runtime == NULL ||
      InterlockedCompareExchange(&runtime->IrqEnabled, 0, 0) == 0 ||
      !Context->InterfaceValid ||
      Context->Interface.DxgkCbNotifyInterrupt == NULL ||
      Context->Interface.DxgkCbQueueDpc == NULL)
    return FALSE;
  result = AppleAgxScanoutConsumeInterrupt(
      &runtime->Panel.Scanout, &irq_status, &latched_sequence);
  if (result == AppleAgxScanoutNoInterrupt)
    return FALSE;
  InterlockedExchange(&Context->LastInterruptStatus, (LONG)irq_status);
  InterlockedIncrement(&Context->InterruptCount);
  InterlockedIncrement(&Context->InterruptAckCount);
  pending_sequence = InterlockedCompareExchange64(
      &runtime->PendingSequence, 0, 0);
  physical_address = InterlockedCompareExchange64(
      &runtime->PendingPhysicalAddress, 0, 0);
  if (result != AppleAgxScanoutOk ||
      (irq_status & APPLE_AGX_SCANOUT_IRQ_ERROR) != 0u ||
      (irq_status & APPLE_AGX_SCANOUT_IRQ_LATCHED) == 0u ||
      InterlockedCompareExchange(&runtime->PendingValid, 0, 0) == 0 ||
      latched_sequence != (APPLE_AGX_SCANOUT_U64)pending_sequence ||
      latched_sequence <= (APPLE_AGX_SCANOUT_U64)
          InterlockedCompareExchange64(
              &runtime->LastNotifiedSequence, 0, 0)) {
    InterlockedExchange(&runtime->Faulted, 1);
    return TRUE;
  }
  notify_vsync = InterlockedCompareExchange(
      &runtime->VsyncNotifyEnabled, 0, 0) != 0;
  if (notify_vsync) {
    RtlZeroMemory(&data, sizeof(data));
    data.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
    data.CrtcVsync.VidPnTargetId = 0u;
    data.CrtcVsync.PhysicalAddress.QuadPart = physical_address;
    Context->Interface.DxgkCbNotifyInterrupt(
        Context->Interface.DeviceHandle, &data);
  }
  /* This sequence also guards internal completions while reporting is off. */
  InterlockedExchange64(
      &runtime->LastNotifiedSequence, (LONG64)latched_sequence);
  InterlockedExchange(&runtime->PendingValid, 0);
  InterlockedExchange64(&runtime->PendingSequence, 0);
  InterlockedExchange64(&runtime->PendingPhysicalAddress, 0);
  InterlockedExchange(&runtime->PresentGate, 0);
  if (notify_vsync) {
    InterlockedExchange(&Context->SchedulerDpcPending, 1);
    (void)Context->Interface.DxgkCbQueueDpc(Context->Interface.DeviceHandle);
  }
  return TRUE;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutControlInterrupt(
    ADMISSION_CONTEXT *Context, BOOLEAN Enable) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  if (runtime == NULL)
    return STATUS_DEVICE_NOT_READY;
  /* WDDM permits keeping an interrupt enabled for an internal purpose.
   * Disable only the OS reporting subscription, never pending latch ingress. */
  InterlockedExchange(&runtime->VsyncNotifyEnabled, Enable ? 1 : 0);
  return STATUS_SUCCESS;
}
