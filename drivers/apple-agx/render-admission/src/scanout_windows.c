#include "render_admission.h"
#include "apple_agx_vsync.h"

#define ADMISSION_SCANOUT_TAG 'sRGA'
#define ADMISSION_SCANOUT_TIMEOUT_MS 2000ULL
#define ADMISSION_SCANOUT_MAX_POLLS 40000u

static KDEFERRED_ROUTINE AdmissionScanoutTimerDpc;
static KSYNCHRONIZE_ROUTINE AdmissionScanoutVsyncControl;
static ULONGLONG AdmissionScanoutTime100ns(void);
static VOID AdmissionScanoutMpoRetireSlot1(ADMISSION_CONTEXT *Context,
                                           ULONG Stage);

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
  volatile LONG64 PendingQueuedTime100ns;
  volatile LONG64 DeferredPrimaryAddress;
  /* MPO3 flip identity: tagged before the queue, carried with the pending
   * flip, and reported as CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2 on latch.
   * S2: up to two (LayerIndex, PresentId) entries; Count 0 = not an MPO3
   * flip.  One plane (today) is Count 1, LayerIndex 0. */
  volatile LONG NextMpoCount;
  volatile LONG NextMpoLayer[2];
  volatile LONG64 NextMpoPresentId[2];
  volatile LONG PendingMpoCount;
  volatile LONG PendingMpoLayer[2];
  volatile LONG64 PendingMpoPresentId[2];
  /* S2: what the pending flip is.  (Defined beside the field so the host
   * replay harness, which extracts this struct, sees the same values.) */
#define ADMISSION_SCANOUT_MPO_KIND_V2 0
#define ADMISSION_SCANOUT_MPO_KIND_TWO_LAYERS 1
#define ADMISSION_SCANOUT_MPO_KIND_REMOVAL 2
  volatile LONG PendingMpoKind;
  /* S2: a two-layer commit was queued and its removal has not latched. */
  volatile LONG Slot1Shown;
  /* rc110: broker format of the primary deferred while the source is hidden. */
  volatile LONG DeferredFormat;
  volatile LONG PlaneOffPending;
  volatile LONG PlaneOffCount;
  volatile LONG PlaneOffLayer[2];
  volatile LONG64 PlaneOffPresentId[2];
  volatile LONG64 LastNotifiedSequence;
  APPLE_AGX_VSYNC_TIMELINE Timeline;
  KTIMER VsyncTimer;
  KDPC VsyncDpc;
  KSPIN_LOCK TimerLock;
  volatile LONG Stopping, TimelinePaused;
  ULONGLONG TimerDeadline;
  ULONGLONG TimerDue;
  BOOLEAN TimerDeadlineValid;
  BOOLEAN TimerArmed;
  APPLE_AGX_VSYNC_QUERY VsyncReceipt;
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
  InterlockedExchange64(&Runtime->PendingQueuedTime100ns,
                        (LONG64)AdmissionScanoutTime100ns());
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
  InterlockedExchange64(&runtime->PendingQueuedTime100ns,
                        (LONG64)AdmissionScanoutTime100ns());
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
    verified.DestinationIndex = Packet->DestinationIndex;
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
        !AdmissionCompletedOutputReceiptMatchesView(
            &Completed->View, OutputReceipt->DestinationGpuVa,
            OutputReceipt->DestinationPhysical,
            OutputReceipt->DestinationBytes,
            OutputReceipt->OutputBytesExamined) ||
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

/* S2: a device-key REG_DWORD switch; TRUE only for the value 1 (the
 * backend_platform_windows.c Wom1 switch pattern).  PASSIVE_LEVEL. */
/* HOLD-3: a DWORD device parameter, Default when absent. */
static ULONG AdmissionScanoutSwitchValue(ADMISSION_CONTEXT *Context,
                                         PCWSTR ValueName, ULONG Default) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u, result = Default;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return Default;
  RtlInitUnicodeString(&name, ValueName);
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG))
    result = *(const ULONG *)value->Data;
  ZwClose(key);
  return result;
}

static BOOLEAN AdmissionScanoutSwitchEnabled(ADMISSION_CONTEXT *Context,
                                             PCWSTR ValueName) {
  HANDLE key = NULL;
  UNICODE_STRING name;
  ULONG bytes = 0u;
  BOOLEAN enabled = FALSE;
  union {
    ULONGLONG Alignment;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
  } data;
  PKEY_VALUE_PARTIAL_INFORMATION value =
      (PKEY_VALUE_PARTIAL_INFORMATION)data.Buffer;
  if (Context->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &key)))
    return FALSE;
  RtlInitUnicodeString(&name, ValueName);
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutStart(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime;
  ADMISSION_SCANOUT_MEMORY_VIEW memory;
  APPLE_AGX_SCANOUT_IO io;
  APPLE_AGX_SCANOUT_RESULT scanout_result;
  APPLE_AGX_FIXED_PANEL_RESULT result;
  BOOLEAN two_planes_requested;
  if (Context == NULL || !Context->InterfaceValid ||
      Context->Interface.DxgkCbSynchronizeExecution == NULL ||
      Context->ScanoutRuntime != NULL ||
      Context->BrokerBase == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  InterlockedIncrement(&Context->MpoGate.ScanoutStartCount);
  if (!NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(Context, &memory)) ||
      memory.PoolBytes != memory.Bytes ||
      (memory.GuestIpaAddress &
       (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) != 0ULL)
    return STATUS_INVALID_ADDRESS;
  runtime = ExAllocatePool2(
      POOL_FLAG_NON_PAGED, sizeof(*runtime), ADMISSION_SCANOUT_TAG);
  if (runtime == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(runtime, sizeof(*runtime));
  runtime->Adapter = Context;
  KeInitializeSpinLock(&runtime->TimerLock);
  KeInitializeTimer(&runtime->VsyncTimer);
  KeInitializeDpc(&runtime->VsyncDpc, AdmissionScanoutTimerDpc, runtime);
  runtime->Timeline.Running = APPLE_AGX_SCANOUT_TRUE;
  runtime->Timeline.Enabled = APPLE_AGX_SCANOUT_TRUE;
  runtime->VsyncNotifyEnabled = 1;
  runtime->VsyncReceipt.Magic = APPLE_AGX_VSYNC_QUERY_MAGIC;
  runtime->VsyncReceipt.Version = 1u;
  runtime->VsyncReceipt.Bytes = sizeof(runtime->VsyncReceipt);
  runtime->VsyncReceipt.Generation = Context->Win32BootGeneration;
  runtime->VsyncReceipt.RateNumerator = APPLE_AGX_VSYNC_RATE_NUMERATOR;
  runtime->VsyncReceipt.RateDenominator = APPLE_AGX_VSYNC_RATE_DENOMINATOR;
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
      &runtime->Panel, &io, memory.GuestIpaAddress, memory.PoolBytes, 1ULL,
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
  /* S2: Wom1MpoTwoPlanes = 1 asks the panel to negotiate ABI V3 between
   * its Qualify and REGISTER_POOL; absent/0 leaves every access as today. */
  two_planes_requested = AdmissionScanoutSwitchEnabled(Context,
                                                       L"Wom1MpoTwoPlanes");
  runtime->Panel.RequestTwoLayers =
      two_planes_requested ? APPLE_AGX_SCANOUT_TRUE : APPLE_AGX_SCANOUT_FALSE;
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
  if (two_planes_requested) {
    RtlZeroMemory((VOID *)&Context->Mpo, sizeof(Context->Mpo));
    RtlZeroMemory(Context->MpoPlanes, sizeof(Context->MpoPlanes));
    Context->Mpo.Version = (LONG)ADMISSION_MPO_RECEIPT_VERSION;
    Context->Mpo.Bytes = (LONG)sizeof(Context->Mpo);
    Context->Mpo.ContractVersion = (LONG)ADMISSION_MPO_CONTRACT_VERSION;
    Context->Mpo.SwitchSet = 1;
    /* CUR-1 receipt (opt-in, receipt only): read refused clip blocks. */
    Context->Mpo.ClipReadSwitch =
        AdmissionScanoutSwitchEnabled(Context, L"Wom1MpoClipRead") ? 1 : 0;
    Context->Mpo.BrokerAdvertised =
        AppleAgxScanoutTwoLayersAdvertised(&runtime->Panel.Scanout) ? 1 : 0;
    Context->Mpo.Effective =
        AppleAgxFixedPanelTwoLayers(&runtime->Panel) ? 1 : 0;
    /* STRETCH-1: what the plane caps derive from (broker bit 8). */
    Context->Mpo.ScalingSupported =
        Context->Mpo.Effective != 0 &&
                (runtime->Panel.Scanout.Capabilities &
                 APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE) != 0u
            ? 1
            : 0;
    /* NP-1: the Set3-side publish (AdmissionQueueMpoPublish). */
    AdmissionMpoPublishStart(Context);
    AdmissionMpoHoldClear(&Context->MpoHold);
    Context->MpoHoldLock = 0;
    /* HOLD-3: the frame-deadline lead (us), Wom1MpoLeadUs, default 5000
     * (EXP2384: the latest submit seen still latching was 4.08 ms before the
     * vblank), clamped to 1000..12000. */
    Context->MpoLeadUs = AdmissionScanoutSwitchValue(Context, L"Wom1MpoLeadUs", 5000u);
    if (Context->MpoLeadUs < 1000u) Context->MpoLeadUs = 1000u;
    if (Context->MpoLeadUs > 12000u) Context->MpoLeadUs = 12000u;
    /* HOLD-2: Set3 commits through this DPC. */
    AdmissionMpoCommitDpcStart(Context);
    /* CUR-1 v2: zero the scan-out scratch (PASSIVE, before any Set3) and
     * prove it reads back zero; only then may Check3 offer it. */
    {
      ADMISSION_SCANOUT_MEMORY_VIEW scratch;
      ULONGLONG offset = 0ULL, i;
      LONG nonzero = 0;
      AdmissionMpoScratchReset(&Context->MpoScratch);
      Context->MpoScratchCpu = NULL;
      Context->MpoScratchPoolOffset = 0ULL;
      Context->Mpo.VidMmSegment2Kib =
          (LONG)(AdmissionMemoryVidMmLocalBytes(&Context->Memory) >> 10);
      Context->Mpo.PoolKib = (LONG)(Context->Memory.LocalAllocationBytes >> 10);
      if (NT_SUCCESS(AdmissionMemoryRuntimeScanoutScratch(Context, &scratch,
                                                          &offset))) {
        RtlZeroMemory(scratch.CpuAddress, (SIZE_T)scratch.Bytes);
        KeMemoryBarrier();
        for (i = 0ULL; i < scratch.Bytes; ++i)
          if (((volatile const UCHAR *)scratch.CpuAddress)[i] != 0u)
            ++nonzero;
        Context->Mpo.ScratchOffsetKib = (LONG)(offset >> 10);
        Context->Mpo.ScratchNonZeroBytes = nonzero;
        if (nonzero == 0) {
          Context->MpoScratchCpu = (PUCHAR)scratch.CpuAddress;
          Context->MpoScratchPoolOffset = offset;
          Context->Mpo.ScratchReady = 1;
        }
      }
    }
  }
  /* S2-1b: Effective 0 -> 1 at this scanout start. */
  if (AppleAgxFixedPanelTwoLayers(&runtime->Panel)) {
    InterlockedIncrement(&Context->MpoGate.EffectiveTransitions);
    Context->MpoGate.MpoEffectiveSetQpc = AdmissionMpoGateQpc(Context);
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
  BOOLEAN wasEffective = FALSE;
  if (Context == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  runtime = (ADMISSION_SCANOUT_RUNTIME *)Context->ScanoutRuntime;
  if (runtime == NULL)
    return STATUS_SUCCESS;
  {
    KIRQL oldIrql;
    BOOLEAN done = FALSE;
    NTSTATUS status;
    KeAcquireSpinLock(&runtime->TimerLock, &oldIrql);
    InterlockedExchange(&runtime->Stopping, 1);
    InterlockedExchange(&runtime->VsyncNotifyEnabled, 0);
    status = Context->Interface.DxgkCbSynchronizeExecution(
        Context->Interface.DeviceHandle, AdmissionScanoutVsyncControl,
        runtime, 0u, &done);
    (void)KeCancelTimer(&runtime->VsyncTimer);
    runtime->TimerArmed = FALSE;
    (void)KeRemoveQueueDpc(&runtime->VsyncDpc);
    KeReleaseSpinLock(&runtime->TimerLock, oldIrql);
    /* No rearm is possible once Stopping is published under TimerLock. */
    KeFlushQueuedDpcs();
    if (!NT_SUCCESS(status) || !done)
      return STATUS_DEVICE_BUSY;
  }
  InterlockedExchange(&runtime->PresentGate, 1);
  /* NP-1: no Set3 runs once the scanout stops; drain and free the publish
   * work item (its worker touches the context). */
  AdmissionMpoPublishStop(Context);
  /* HOLD-2: no commit DPC outlives scan-out (the gate is held: a running
   * one commits nothing). */
  AdmissionMpoCommitDpcStop(Context);
  /* HOLD-1: a held set cannot outlive scan-out (counted if dropped). */
  if (Context->MpoHold.Valid)
    InterlockedIncrement(&Context->Mpo.HoldDropped);
  AdmissionMpoHoldClear(&Context->MpoHold);
  if (InterlockedCompareExchange(&runtime->PendingValid, 0, 0) != 0)
    return STATUS_DEVICE_BUSY;
  if (InterlockedExchange(&runtime->IrqEnabled, 0) != 0 &&
      AppleAgxScanoutDisableInterrupts(&runtime->Panel.Scanout) !=
          AppleAgxScanoutOk)
    return STATUS_DEVICE_BUSY;
  {
    BOOLEAN done = FALSE;
    if (!NT_SUCCESS(Context->Interface.DxgkCbSynchronizeExecution(
            Context->Interface.DeviceHandle, AdmissionScanoutVsyncControl,
            runtime, 0u, &done)) || !done)
      return STATUS_DEVICE_BUSY;
  }
  /* S2-1b: a RELEASE below clears the V3 negotiation (Effective 1 -> 0). */
  wasEffective = AppleAgxFixedPanelTwoLayers(&runtime->Panel) ? TRUE : FALSE;
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
  if (wasEffective && !AppleAgxFixedPanelTwoLayers(&runtime->Panel)) {
    InterlockedIncrement(&Context->MpoGate.EffectiveTransitions);
    InterlockedExchange(&Context->MpoGate.LastEffectiveClearReason,
                        (LONG)ADMISSION_MPO_GATE_CLEAR_STOP);
  }
  /* A1.2: RELEASE removed slot 1 itself (broker.c:328, :838-841). */
  RtlZeroMemory(Context->MpoPlanes, sizeof(Context->MpoPlanes));
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
  InterlockedExchange64(&runtime->PendingQueuedTime100ns,
                        (LONG64)AdmissionScanoutTime100ns());
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

_Use_decl_annotations_ BOOLEAN AdmissionScanoutFormatSupported(
    ADMISSION_CONTEXT *Context, D3DDDIFORMAT Format) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  if (Format == D3DDDIFMT_A8R8G8B8)
    return TRUE;
  return Format == D3DDDIFMT_A8B8G8R8 && runtime != NULL &&
         (runtime->Panel.Scanout.Capabilities &
          APPLE_AGX_SCANOUT_CAP_RGBA8888) != 0u;
}

_Use_decl_annotations_ VOID AdmissionScanoutBrokerInfo(
    ADMISSION_CONTEXT *Context, ULONG *AbiVersion, ULONG *Capabilities) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  *AbiVersion = runtime != NULL ? runtime->Panel.Scanout.AbiVersion : 0u;
  *Capabilities = runtime != NULL ? runtime->Panel.Scanout.Capabilities : 0u;
}

_Use_decl_annotations_ ULONG AdmissionScanoutFormatCode(
    ADMISSION_CONTEXT *Context, D3DDDIFORMAT Format) {
  if (!AdmissionScanoutFormatSupported(Context, Format))
    return 0u;
  return Format == D3DDDIFMT_A8R8G8B8 ? APPLE_AGX_SCANOUT_FORMAT_BGRA8888
                                      : APPLE_AGX_SCANOUT_FORMAT_RGBA8888;
}

/* The shared panel scaling values are the D3DKMDT_VPPS values. */
C_ASSERT((ULONG)AppleAgxFixedPanelScalingIdentity ==
         (ULONG)D3DKMDT_VPPS_IDENTITY);
C_ASSERT((ULONG)AppleAgxFixedPanelScalingCentered ==
         (ULONG)D3DKMDT_VPPS_CENTERED);
C_ASSERT((ULONG)AppleAgxFixedPanelScalingStretched ==
         (ULONG)D3DKMDT_VPPS_STRETCHED);
C_ASSERT((ULONG)AppleAgxFixedPanelScalingAspectRatioCenteredMax ==
         (ULONG)D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX);
C_ASSERT((ULONG)D3DKMDT_VPPS_UNINITIALIZED == 0u &&
         (ULONG)D3DKMDT_VPPS_UNPINNED == 254u &&
         (ULONG)D3DKMDT_VPPS_NOTSPECIFIED == 255u);

_Use_decl_annotations_ BOOLEAN AdmissionScanoutScaledModes(
    ADMISSION_CONTEXT *Context) {
#if ADMISSION_DISPLAY_SCALED_MODES
  /* EXP2053: dxgkrnl asks for monitor modes once, before the panel is
   * Started (the first scanout), so the decision follows the broker's ABI
   * and capabilities read at StartDevice (AdmissionScanoutStart), not the
   * Started state. Commit and flip still require a started panel. */
  ADMISSION_SCANOUT_RUNTIME *runtime;
  if (Context == NULL || Context->ScanoutRuntime == NULL)
    return FALSE;
  runtime = (ADMISSION_SCANOUT_RUNTIME *)Context->ScanoutRuntime;
  return runtime->Adapter == Context &&
                 runtime->Panel.Scanout.AbiVersion ==
                     APPLE_AGX_SCANOUT_ABI_VERSION_V2 &&
                 (runtime->Panel.Scanout.Capabilities &
                  APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE) != 0u
             ? TRUE
             : FALSE;
#else
  UNREFERENCED_PARAMETER(Context);
  return FALSE;
#endif
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutSourceModeAt(
    ADMISSION_CONTEXT *Context, ULONG Index, ULONG *Width, ULONG *Height,
    ULONG *Stride) {
  APPLE_AGX_FIXED_PANEL_SOURCE_MODE mode;
  *Width = 0u;
  *Height = 0u;
  *Stride = 0u;
  RtlZeroMemory(&mode, sizeof(mode));
  if (!AppleAgxFixedPanelSourceModeAt(
          Index,
          AdmissionScanoutScaledModes(Context) ? APPLE_AGX_SCANOUT_TRUE
                                               : APPLE_AGX_SCANOUT_FALSE,
          &mode))
    return FALSE;
  *Width = mode.Width;
  *Height = mode.Height;
  *Stride = mode.Stride;
  return TRUE;
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutSourceModeSupported(
    ADMISSION_CONTEXT *Context, ULONG Width, ULONG Height, ULONG Stride) {
  return AppleAgxFixedPanelSourceModeFind(
             Width, Height, Stride,
             AdmissionScanoutScaledModes(Context) ? APPLE_AGX_SCANOUT_TRUE
                                                  : APPLE_AGX_SCANOUT_FALSE,
             NULL)
             ? TRUE
             : FALSE;
}

_Use_decl_annotations_ ULONG AdmissionScanoutScalingSupport(
    ADMISSION_CONTEXT *Context, ULONG Width, ULONG Height) {
  return AppleAgxFixedPanelScalingSupport(
      Width, Height,
      AdmissionScanoutScaledModes(Context) ? APPLE_AGX_SCANOUT_TRUE
                                           : APPLE_AGX_SCANOUT_FALSE);
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutResolveScaling(
    ADMISSION_CONTEXT *Context, ULONG Width, ULONG Height,
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Requested,
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING *Resolved) {
  APPLE_AGX_FIXED_PANEL_SCALING scaling = AppleAgxFixedPanelScalingIdentity;
  *Resolved = D3DKMDT_VPPS_UNINITIALIZED;
  if (!AppleAgxFixedPanelResolveScaling(
          Width, Height,
          AdmissionScanoutScaledModes(Context) ? APPLE_AGX_SCANOUT_TRUE
                                               : APPLE_AGX_SCANOUT_FALSE,
          (APPLE_AGX_SCANOUT_U32)Requested, &scaling))
    return FALSE;
  *Resolved = (D3DKMDT_VIDPN_PRESENT_PATH_SCALING)scaling;
  return TRUE;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutCommit(
    ADMISSION_CONTEXT *Context, ULONG Width, ULONG Height, ULONG Stride,
    D3DDDIFORMAT Format, D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  if (runtime == NULL)
    return STATUS_DEVICE_NOT_READY;
  /* A1.2: slot 1 never survives a source-mode change (rule 24 admits two
   * planes only at 2560x1600; a new mode must not inherit them). */
  AdmissionScanoutMpoRetireSlot1(Context, ADMISSION_MPO_RETIRE_STAGE_COMMIT);
#if !ADMISSION_DISPLAY_SCALED_MODES
  if (Width != APPLE_AGX_SCANOUT_J313_WIDTH ||
      Height != APPLE_AGX_SCANOUT_J313_HEIGHT)
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
#endif
  return AppleAgxFixedPanelCommitMode(
             &runtime->Panel, 0u, 0u, Width, Height, Stride,
             Format == D3DDDIFMT_A8R8G8B8
                 ? APPLE_AGX_SCANOUT_FORMAT_BGRA8888
                 : Format == D3DDDIFMT_A8B8G8R8
                       ? APPLE_AGX_SCANOUT_FORMAT_RGBA8888
                       : 0u,
             (APPLE_AGX_FIXED_PANEL_SCALING)Scaling) == AppleAgxFixedPanelOk
             ? STATUS_SUCCESS
             : STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutSetVisible(
    ADMISSION_CONTEXT *Context, BOOLEAN Visible) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  if (runtime == NULL)
    return STATUS_DEVICE_NOT_READY;
  /* A1.2: slot 1 never stays on screen once the source is hidden. */
  if (!Visible)
    AdmissionScanoutMpoRetireSlot1(Context,
                                   ADMISSION_MPO_RETIRE_STAGE_HIDE);
  return AppleAgxFixedPanelSetVisible(
             &runtime->Panel, 0u,
             Visible ? APPLE_AGX_SCANOUT_TRUE
                     : APPLE_AGX_SCANOUT_FALSE) == AppleAgxFixedPanelOk
             ? STATUS_SUCCESS
             : STATUS_DEVICE_HARDWARE_ERROR;
}

static VOID AdmissionScanoutSetNextMpo(
    ADMISSION_SCANOUT_RUNTIME *Runtime,
    const ADMISSION_MPO_COMPLETION *Completion) {
  ULONG index;
  ULONG count = Completion->Count > 2u ? 2u : Completion->Count;
  for (index = 0u; index < count; ++index) {
    InterlockedExchange(&Runtime->NextMpoLayer[index],
                        (LONG)Completion->LayerIndex[index]);
    InterlockedExchange64(&Runtime->NextMpoPresentId[index],
                          (LONG64)Completion->PresentId[index]);
  }
  InterlockedExchange(&Runtime->NextMpoCount, (LONG)count);
}

/* The tagged identity moves with the flip that was queued. */
static VOID AdmissionScanoutTakeNextMpo(ADMISSION_SCANOUT_RUNTIME *Runtime,
                                        LONG Kind) {
  ULONG index;
  for (index = 0u; index < 2u; ++index) {
    InterlockedExchange(&Runtime->PendingMpoLayer[index],
                        InterlockedExchange(&Runtime->NextMpoLayer[index], 0));
    InterlockedExchange64(
        &Runtime->PendingMpoPresentId[index],
        InterlockedExchange64(&Runtime->NextMpoPresentId[index], 0));
  }
  InterlockedExchange(&Runtime->PendingMpoKind, Kind);
  InterlockedExchange(&Runtime->PendingMpoCount,
                      InterlockedExchange(&Runtime->NextMpoCount, 0));
}

_Use_decl_annotations_ VOID AdmissionScanoutTagMpoFlip(
    ADMISSION_CONTEXT *Context, ULONGLONG PresentId) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_MPO_COMPLETION completion;
  if (runtime == NULL)
    return;
  RtlZeroMemory(&completion, sizeof(completion));
  completion.Count = 1u;
  completion.LayerIndex[0] = 0u;
  completion.PresentId[0] = PresentId;
  AdmissionScanoutSetNextMpo(runtime, &completion);
}

_Use_decl_annotations_ VOID AdmissionScanoutTagMpoFlipSet(
    ADMISSION_CONTEXT *Context, const ADMISSION_MPO_COMPLETION *Completion) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  if (runtime == NULL || Completion == NULL)
    return;
  AdmissionScanoutSetNextMpo(runtime, Completion);
}

_Use_decl_annotations_ VOID AdmissionScanoutClearMpoFlip(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ULONG index;
  if (runtime == NULL)
    return;
  InterlockedExchange(&runtime->NextMpoCount, 0);
  for (index = 0u; index < 2u; ++index) {
    InterlockedExchange(&runtime->NextMpoLayer[index], 0);
    InterlockedExchange64(&runtime->NextMpoPresentId[index], 0);
  }
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutMpoPlaneOffSet(
    ADMISSION_CONTEXT *Context, const ADMISSION_MPO_COMPLETION *Completion) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ULONG index;
  ULONG count;
  if (Completion == NULL)
    return STATUS_INVALID_PARAMETER;
  if (runtime == NULL) {
    InterlockedIncrement(&Context->Mpo.S3BusyNoRuntime); /* S3-B row I */
    return STATUS_DEVICE_BUSY;
  }
  count = Completion->Count > 2u ? 2u : Completion->Count;
  /* A fixed scanout plane cannot be detached from DCP; the last latched
   * surface keeps scanning and the request completes at the next vsync. */
  for (index = 0u; index < count; ++index) {
    InterlockedExchange(&runtime->PlaneOffLayer[index],
                        (LONG)Completion->LayerIndex[index]);
    InterlockedExchange64(&runtime->PlaneOffPresentId[index],
                          (LONG64)Completion->PresentId[index]);
  }
  InterlockedExchange(&runtime->PlaneOffCount, (LONG)count);
  InterlockedExchange(&runtime->PlaneOffPending, 1);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutMpoPlaneOff(
    ADMISSION_CONTEXT *Context, ULONGLONG PresentId) {
  ADMISSION_MPO_COMPLETION completion;
  RtlZeroMemory(&completion, sizeof(completion));
  completion.Count = 1u;
  completion.LayerIndex[0] = 0u;
  completion.PresentId[0] = PresentId;
  return AdmissionScanoutMpoPlaneOffSet(Context, &completion);
}

/* ---- S2: two display planes (broker ABI V3), effective-only ---- */

_Use_decl_annotations_ BOOLEAN AdmissionScanoutMpoEffective(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  return runtime != NULL && AppleAgxFixedPanelTwoLayers(&runtime->Panel)
             ? TRUE
             : FALSE;
}

/* STRETCH-1: layer records may scale (broker CAPABILITIES bit 8,
 * CAP_SCALED_SOURCE, read at StartDevice) and two-layer scan-out is
 * effective (the only path that sends layer records). */
_Use_decl_annotations_ BOOLEAN AdmissionScanoutMpoScaledSource(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  return runtime != NULL && AppleAgxFixedPanelTwoLayers(&runtime->Panel) &&
                 (runtime->Panel.Scanout.Capabilities &
                  APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE) != 0u
             ? TRUE
             : FALSE;
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutMpoPool(
    ADMISSION_CONTEXT *Context, ULONGLONG *SegmentBase, ULONGLONG *PoolBytes) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_SCANOUT_MEMORY_VIEW window;
  *SegmentBase = 0ULL;
  *PoolBytes = 0ULL;
  if (runtime == NULL ||
      !NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(Context, &window)) ||
      window.GuestIpaAddress != runtime->Panel.PoolIpa ||
      window.Bytes != runtime->Panel.PoolBytes)
    return FALSE;
  /* The pool is the local segment (segment 2); a plane's pool offset is
   * its segment address minus the segment base, as for the V2 primary. */
  *SegmentBase = Context->Memory.Topology.Local.Base;
  *PoolBytes = window.Bytes;
  return TRUE;
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutMpoGateFree(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  return runtime != NULL &&
                 InterlockedCompareExchange(&runtime->PresentGate, 0, 0) == 0
             ? TRUE
             : FALSE;
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutMpoSlot1MayShow(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  APPLE_AGX_SCANOUT_U32 active = 0u;
  if (runtime == NULL || !AppleAgxFixedPanelTwoLayers(&runtime->Panel))
    return FALSE;
  if (InterlockedCompareExchange(&runtime->Slot1Shown, 0, 0) != 0)
    return TRUE;
  /* Unreadable counts as "may show": a removal swap is always valid. */
  if (AppleAgxScanoutReadActiveLayers(&runtime->Panel.Scanout, &active) !=
      AppleAgxScanoutOk)
    return TRUE;
  return (active & APPLE_AGX_SCANOUT_ACTIVE_SLOT1) != 0u ? TRUE : FALSE;
}

/* Queue a V3 commit with PresentGate already held.  Releases the gate on
 * failure; on a terminal refusal keeps the code and records in the receipt
 * and counts it in *RefusalCounter. */
static NTSTATUS AdmissionScanoutMpoQueueHeld(
    ADMISSION_CONTEXT *Context, ADMISSION_SCANOUT_RUNTIME *Runtime,
    const APPLE_AGX_SCANOUT_LAYER *Records, ULONG Count,
    const ADMISSION_MPO_COMPLETION *Completion, LONGLONG PrimaryAddress,
    BOOLEAN RequireVisible, volatile LONG *RefusalCounter) {
  APPLE_AGX_SCANOUT_RESULT result;
  APPLE_AGX_U64 sequence = 0ULL;
  ULONG index;

  InterlockedExchange64(&Runtime->PendingPhysicalAddress, PrimaryAddress);
  InterlockedExchange64(&Runtime->PendingQueuedTime100ns,
                        (LONG64)AdmissionScanoutTime100ns());
  /* AppleAgxFixedPanelQueueCommitLayers's state checks; visibility only
   * for a flip (the slot-1 retirement runs while the source is hidden). */
  if (!Runtime->Panel.Started || !Runtime->Panel.Committed ||
      (RequireVisible && !Runtime->Panel.Visible))
    result = AppleAgxScanoutNotQualified;
  else
    result = AppleAgxScanoutQueueCommitLayers(&Runtime->Panel.Scanout,
                                              Records, Count, &sequence);
  if (result != AppleAgxScanoutOk) {
    InterlockedExchange64(&Runtime->PendingPhysicalAddress, 0);
    InterlockedExchange(&Runtime->PresentGate, 0);
    if (result == AppleAgxScanoutBusy) {
      InterlockedIncrement(&Context->Mpo.Busy);
      return STATUS_DEVICE_BUSY;
    }
    if (result == AppleAgxScanoutBrokerRejected) {
      /* Contract defect: the table accepted what the broker refuses.
       * Keep the code and the records; no fallback swap is made. */
      InterlockedIncrement(RefusalCounter);
      InterlockedExchange(&Context->Mpo.LastBrokerRefusal,
                          (LONG)Runtime->Panel.Scanout.LastBrokerResult);
      InterlockedExchange(&Context->Mpo.LastRefusalCount, (LONG)Count);
      RtlZeroMemory(Context->Mpo.LastRefusalRecords,
                    sizeof(Context->Mpo.LastRefusalRecords));
      for (index = 0u; index < Count; ++index)
        Context->Mpo.LastRefusalRecords[index] = Records[index];
      /* S3-B: still a defect to surface (counter, records), but the
       * session must survive it: DEVICE_BUSY, not a failure dxgkrnl
       * re-offers forever. */
      InterlockedIncrement(&Context->Mpo.S3BusyBrokerRejected);
      return STATUS_DEVICE_BUSY;
    }
    if (result == AppleAgxScanoutNotQualified) {
      InterlockedIncrement(&Context->Mpo.NotVisible);
      InterlockedIncrement(&Context->Mpo.S3BusyNotQualified);
      return STATUS_DEVICE_BUSY;
    }
    InterlockedIncrement(&Context->Mpo.TransportFailures);
    InterlockedIncrement(&Context->Mpo.S3BusyTransport);
    InterlockedExchange(&Context->Mpo.LastTransportResult, (LONG)result);
    return STATUS_DEVICE_BUSY;
  }
  /* Like the broker (slot1_latched at the commit), count slot 1 as shown
   * from the moment a two-layer commit is accepted. */
  if (Count == 2u)
    InterlockedExchange(&Runtime->Slot1Shown, 1);
  AdmissionScanoutSetNextMpo(Runtime, Completion);
  InterlockedExchange64(&Runtime->PendingSequence, (LONG64)sequence);
  InterlockedExchange(&Runtime->PendingValid, 1);
  AdmissionScanoutTakeNextMpo(Runtime,
                              Count == 2u
                                  ? ADMISSION_SCANOUT_MPO_KIND_TWO_LAYERS
                                  : ADMISSION_SCANOUT_MPO_KIND_REMOVAL);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutMpoCommit(
    ADMISSION_CONTEXT *Context, const APPLE_AGX_SCANOUT_LAYER *Records,
    ULONG Count, const ADMISSION_MPO_COMPLETION *Completion,
    LONGLONG PrimaryAddress) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);

  if (Records == NULL || Completion == NULL || (Count != 1u && Count != 2u))
    return STATUS_INVALID_PARAMETER;
  /* S3-B: dxgkrnl's Set3 thunk knows SUCCESS and DEVICE_BUSY (a defined
   * retry state); any other status leaves the flip entry unadvanced and it
   * is offered again forever (EXP2374).  A runtime obstacle is DEVICE_BUSY. */
  if (runtime == NULL || !AppleAgxFixedPanelTwoLayers(&runtime->Panel) ||
      !Context->DisplayActive ||
      InterlockedCompareExchange(&runtime->IrqEnabled, 0, 0) == 0 ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0) {
    InterlockedIncrement(&Context->Mpo.S3BusyNoRuntime);
    return STATUS_DEVICE_BUSY;
  }
  /* A mode set hides the source: retried until it is shown again. */
  if (!Context->SourceVisible) {
    InterlockedIncrement(&Context->Mpo.NotVisible);
    InterlockedIncrement(&Context->Mpo.S3BusyNotVisible);
    return STATUS_DEVICE_BUSY;
  }
  /* One commit outstanding until its latch (broker BUSY rule). */
  if (InterlockedCompareExchange(&runtime->PresentGate, 1, 0) != 0) {
    InterlockedIncrement(&Context->Mpo.Busy);
    return STATUS_DEVICE_BUSY;
  }
  return AdmissionScanoutMpoQueueHeld(Context, runtime, Records, Count,
                                      Completion, PrimaryAddress, TRUE,
                                      &Context->Mpo.BrokerRefusals);
}

/* A1.2: no V2 command the driver issues removes DCP slot 1 except RELEASE
 * (m1n1 gpu/s2-broker-v3 hv_agx_scanout_broker.c:328, service.c:524-526):
 * SetVidPnSourceVisibility(FALSE) and a mode commit send nothing to the
 * broker, and the next V2 PRESENT is refused with SLOT1_LATCHED while slot
 * 1 is latched (broker.c:253-254).  So before the source is hidden and
 * before a new source mode is committed, the removal swap (one record, the
 * stored LayerIndex 0 plane, flags 0) is queued here; its latch clears
 * Slot1Shown (ISR).  The plane state is cleared at the same point.  A
 * failure is counted in Wom1MpoReceipt and never fails the hide or the
 * mode commit.  PASSIVE_LEVEL (both callers). */
#define ADMISSION_SCANOUT_RETIRE_WAIT_MS 50u
static VOID AdmissionScanoutMpoRetireSlot1(ADMISSION_CONTEXT *Context,
                                           ULONG Stage) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_MPO_COMPLETION completion;
  APPLE_AGX_SCANOUT_LAYER record;
  LARGE_INTEGER interval;
  LONGLONG primary;
  ULONG built;
  ULONG waited;

  if (runtime == NULL || !AppleAgxFixedPanelTwoLayers(&runtime->Panel) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  if (!AdmissionScanoutMpoSlot1MayShow(Context)) {
    RtlZeroMemory(Context->MpoPlanes, sizeof(Context->MpoPlanes));
    return;
  }
  InterlockedExchange(&Context->Mpo.LastSlot1RetireStage, (LONG)Stage);
  RtlZeroMemory(&record, sizeof(record));
  RtlZeroMemory(&completion, sizeof(completion));
  built = Context->MpoPlanes[0].Valid != 0u
              ? AdmissionMpoBuildRemoval(&Context->MpoPlanes[0].Input, &record)
              : 0u;
  primary = Context->MpoPlanes[0].PrimaryAddress;
  RtlZeroMemory(Context->MpoPlanes, sizeof(Context->MpoPlanes));
  if (built != 1u) {
    InterlockedIncrement(&Context->Mpo.Slot1RetireFailures);
    return;
  }
  /* One commit outstanding: wait (bounded) for the pending flip's latch. */
  interval.QuadPart = -10000LL;
  for (waited = 0u;
       InterlockedCompareExchange(&runtime->PresentGate, 1, 0) != 0;
       ++waited) {
    if (waited >= ADMISSION_SCANOUT_RETIRE_WAIT_MS ||
        InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0) {
      InterlockedIncrement(&Context->Mpo.Slot1RetireFailures);
      return;
    }
    (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
  }
  if (NT_SUCCESS(AdmissionScanoutMpoQueueHeld(
          Context, runtime, &record, 1u, &completion, primary, FALSE,
          &Context->Mpo.Slot1RetireRefusals)))
    InterlockedIncrement(&Context->Mpo.Slot1Retires);
  else
    InterlockedIncrement(&Context->Mpo.Slot1RetireFailures);
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutQueueDeferred(
    ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  APPLE_AGX_FIXED_PANEL_RESULT result;
  APPLE_AGX_U64 offset = 0ULL;
  APPLE_AGX_U64 sequence = 0ULL;
  LONG64 address;

  if (runtime == NULL || !Context->SourceVisible ||
      InterlockedCompareExchange(&runtime->IrqEnabled, 0, 0) == 0 ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0)
    return STATUS_SUCCESS;
  if (InterlockedCompareExchange(&runtime->PresentGate, 1, 0) != 0)
    return STATUS_DEVICE_BUSY;
  if (!AppleAgxFixedPanelTakeDeferred(&runtime->Panel, &offset)) {
    InterlockedExchange(&runtime->PresentGate, 0);
    return STATUS_SUCCESS;
  }
  address = InterlockedExchange64(&runtime->DeferredPrimaryAddress, 0);
  InterlockedExchange64(&runtime->PendingPhysicalAddress, address);
  InterlockedExchange64(&runtime->PendingQueuedTime100ns,
                        (LONG64)AdmissionScanoutTime100ns());
  /* rc110: the deferred primary keeps the format it was flipped with. */
  if (AppleAgxFixedPanelSetPresentFormat(
          &runtime->Panel,
          (APPLE_AGX_SCANOUT_U32)InterlockedCompareExchange(
              &runtime->DeferredFormat, 0, 0)) != AppleAgxFixedPanelOk) {
    InterlockedExchange64(&runtime->PendingPhysicalAddress, 0);
    InterlockedExchange(&runtime->PresentGate, 0);
    return STATUS_DEVICE_HARDWARE_ERROR;
  }
  result = AppleAgxFixedPanelQueuePresent(
      &runtime->Panel, ADMISSION_MEMORY_LOCAL_SEGMENT, offset, &sequence);
  if (result != AppleAgxFixedPanelOk) {
    InterlockedExchange64(&runtime->PendingPhysicalAddress, 0);
    InterlockedExchange(&runtime->PresentGate, 0);
    return result == AppleAgxFixedPanelPresentPending
               ? STATUS_DEVICE_BUSY
               : STATUS_DEVICE_HARDWARE_ERROR;
  }
  InterlockedExchange64(&runtime->PendingSequence, (LONG64)sequence);
  InterlockedExchange(&runtime->PendingValid, 1);
  AdmissionScanoutTakeNextMpo(runtime, ADMISSION_SCANOUT_MPO_KIND_V2);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutQueuePresent(
    ADMISSION_CONTEXT *Context,
    const DXGKARG_SETVIDPNSOURCEADDRESS *Args) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  ADMISSION_ALLOCATION_HANDLE *allocation;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  APPLE_AGX_LOCAL_SEGMENT_ADDRESS_RESULT address_result;
  APPLE_AGX_FIXED_PANEL_RESULT result;
  ADMISSION_SCANOUT_MEMORY_VIEW window;
  APPLE_AGX_U64 surface_offset = 0ULL;
  APPLE_AGX_U64 sequence = 0ULL;
  ULONGLONG surface_bytes = 0ULL;
  ULONG format_code = 0u;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
  ADMISSION_DISPLAY_OUTPUT_LEASE fallbackCandidate;
  ADMISSION_BACKEND_OUTPUT_VIEW fallbackView;
  ADMISSION_LOCAL_MEMORY_VIEW fallbackMemory;
  BOOLEAN fallbackCandidateValid = FALSE;
  ULONG fallbackGeneration;
  KIRQL oldIrql;
  AdmissionDisplayOutputLeaseInitialize(&fallbackCandidate);
  RtlZeroMemory(&fallbackView, sizeof(fallbackView));
  RtlZeroMemory(&fallbackMemory, sizeof(fallbackMemory));
#endif
  if (runtime == NULL || Args == NULL || Args->VidPnSourceId != 0u ||
      Args->hAllocation == NULL ||
      Args->PrimarySegment != ADMISSION_MEMORY_LOCAL_SEGMENT ||
      Args->PrimaryAddress.QuadPart <= 0 ||
      !Context->DisplayActive ||
      InterlockedCompareExchange(&runtime->IrqEnabled, 0, 0) == 0 ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0)
    return STATUS_INVALID_PARAMETER;
  allocation = (ADMISSION_ALLOCATION_HANDLE *)Args->hAllocation;
  if (allocation->Object.Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC)
    return STATUS_INVALID_HANDLE;
  description = &allocation->Object.Description;
  InterlockedExchange(&Context->DisplayModes.LastSourceAddressFormat,
                      (LONG)description->Format);
  InterlockedExchange(&Context->DisplayModes.LastSourceAddressWidth,
                      (LONG)description->Width);
  InterlockedExchange(&Context->DisplayModes.LastSourceAddressHeight,
                      (LONG)description->Height);
  /* rc110 (EXP2049): only a linear scanout-capable allocation; an R105
   * native resource is never scanned out, whatever its description says. */
  if (!allocation->ScanoutCapable) {
    InterlockedIncrement(&Context->DisplayModes.LayoutRefusals);
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
  }
  /* rc106: the flipped primary has exactly the committed source geometry
   * (2560x1600 unless a scaled source mode is committed). */
  surface_bytes = (ULONGLONG)Context->CommittedStride *
                  Context->CommittedHeight;
  if (!AdmissionAllocationDescriptionValid(description) ||
      !AdmissionScanoutSourceModeSupported(Context, Context->CommittedWidth,
                                           Context->CommittedHeight,
                                           Context->CommittedStride) ||
      description->Width != Context->CommittedWidth ||
      description->Height != Context->CommittedHeight ||
      description->Pitch != Context->CommittedStride ||
      description->BytesPerPixel != 4u ||
      description->Size != surface_bytes) {
    InterlockedIncrement(&Context->DisplayModes.GeometryRefusals);
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
  }
  /* rc110 (EXP2049): the primary's format need not equal the committed
   * source mode's pixel format (Context->CommittedFormat): dxgkrnl flips an
   * exclusive fullscreen D3D12 primary in the swapchain's RGBA8 while the
   * source mode is BGRA, and the broker scans either; the PRESENT carries
   * the primary's own format. */
  format_code = AdmissionScanoutFormatCode(Context,
                                           (D3DDDIFORMAT)description->Format);
  if (format_code == 0u) {
    InterlockedIncrement(&Context->DisplayModes.FormatRefusals);
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
  }
  if (!NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(Context, &window)) ||
      window.GuestIpaAddress != runtime->Panel.PoolIpa)
    return STATUS_INVALID_ADDRESS;
  /* EXP2055: a 1920x1080 primary is 7680*1080 = 0x7e9000 bytes, not a
   * 64 KiB multiple, and the translation refuses unaligned spans
   * (0xc0000141 on every flip, so the mode set rolled back).  The
   * allocation itself is 64 KiB-aligned in size and placement. */
  address_result = AppleAgxLocalSegmentAddressToGpuVa(
      ADMISSION_MEMORY_LOCAL_SEGMENT, Args->PrimarySegment,
      Context->Memory.Topology.Local.Base,
      window.Bytes, 0ULL,
      (APPLE_AGX_U64)Args->PrimaryAddress.QuadPart,
      (surface_bytes + (APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL)) &
          ~(APPLE_AGX_SCANOUT_ALIGNMENT - 1ULL),
      0ULL, &surface_offset);
  if (address_result != AppleAgxLocalSegmentAddressOk)
    return STATUS_INVALID_ADDRESS;
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
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
#endif
  /* EXP953: dxgkrnl programs a mode-set primary between
   * SetVidPnSourceVisibility(FALSE) and (TRUE).  Accept and retain it; it
   * is queued by AdmissionScanoutQueueDeferred when the source is shown. */
  if (!Context->SourceVisible) {
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
    if (fallbackCandidateValid)
      (void)AdmissionDisplayOutputLeaseRetire(&fallbackCandidate);
#endif
    result = AppleAgxFixedPanelQueuePresent(
        &runtime->Panel, Args->PrimarySegment, surface_offset, &sequence);
    if (result != AppleAgxFixedPanelDeferred)
      return STATUS_INVALID_PARAMETER;
    InterlockedExchange(&runtime->DeferredFormat, (LONG)format_code);
    InterlockedExchange64(&runtime->DeferredPrimaryAddress,
                          Args->PrimaryAddress.QuadPart);
    InterlockedIncrement(&Context->DisplayModes.DeferredFlips);
    return STATUS_SUCCESS;
  }
  if (InterlockedCompareExchange(&runtime->PresentGate, 1, 0) != 0) {
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
    if (fallbackCandidateValid)
      (void)AdmissionDisplayOutputLeaseRetire(&fallbackCandidate);
#endif
    return STATUS_DEVICE_BUSY;
  }
  InterlockedExchange64(
      &runtime->PendingPhysicalAddress,
      Args->PrimaryAddress.QuadPart);
  InterlockedExchange64(&runtime->PendingQueuedTime100ns,
                        (LONG64)AdmissionScanoutTime100ns());
  if (AppleAgxFixedPanelSetPresentFormat(&runtime->Panel, format_code) !=
      AppleAgxFixedPanelOk) {
    InterlockedExchange64(&runtime->PendingPhysicalAddress, 0);
    InterlockedExchange(&runtime->PresentGate, 0);
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
    if (fallbackCandidateValid)
      (void)AdmissionDisplayOutputLeaseRetire(&fallbackCandidate);
#endif
    InterlockedIncrement(&Context->DisplayModes.FormatRefusals);
    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
  }
  result = AppleAgxFixedPanelQueuePresent(
      &runtime->Panel, Args->PrimarySegment, surface_offset, &sequence);
  if (result != AppleAgxFixedPanelOk) {
    InterlockedExchange64(&runtime->PendingPhysicalAddress, 0);
    InterlockedExchange(&runtime->PresentGate, 0);
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
    if (fallbackCandidateValid)
      (void)AdmissionDisplayOutputLeaseRetire(&fallbackCandidate);
#endif
    return result == AppleAgxFixedPanelPresentPending
               ? STATUS_DEVICE_BUSY
               : STATUS_DEVICE_HARDWARE_ERROR;
  }
  InterlockedExchange64(&runtime->PendingSequence, (LONG64)sequence);
  InterlockedExchange(&runtime->PendingValid, 1);
  AdmissionScanoutTakeNextMpo(runtime, ADMISSION_SCANOUT_MPO_KIND_V2);
#if defined(APPLE_AGX_VISIBLE_AGX_QUALIFICATION)
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
#endif
  return STATUS_SUCCESS;
}

/* All timeline and receipt writes are serialized at the adapter's DIRQL.
 * TimerLock only serializes PASSIVE/DPC timer scheduling and teardown;
 * the ISR never acquires it. First32 receipts are immutable, last32 roll. */
/* EXP1106: a period's tick is armed this long after its predicted vblank,
 * so the hardware latch of that vblank (which re-anchors the grid) arrives
 * first and the period gets one vsync, not a tick and then a latch. */
#define ADMISSION_SCANOUT_TICK_MARGIN_100NS 5000ULL

static ULONGLONG AdmissionScanoutTime100ns(void) {
  ULONGLONG qpc = 0ULL;
  return KeQueryInterruptTimePrecise(&qpc);
}

static VOID AdmissionScanoutVsyncRecord(ADMISSION_SCANOUT_RUNTIME *Runtime,
                                      ULONG Kind, NTSTATUS Status) {
  APPLE_AGX_VSYNC_QUERY *q = &Runtime->VsyncReceipt;
  APPLE_AGX_VSYNC_EVENT *e;
  ULONGLONG n = q->EventCount;
  if (n == APPLE_AGX_VSYNC_MAX)
    return;
  e = &q->Events[n < 32ULL ? (ULONG)n : 32u + (ULONG)((n - 32ULL) % 32ULL)];
  RtlZeroMemory(e, sizeof(*e));
  e->Sequence = n + 1ULL;
  /* Kind 8 is emitted retrospectively at the matched latch, so all ring
   * writes remain serialized at DIRQL. Its timestamp is queue entry, not
   * event insertion time; PendingSequence/Address identify the same flip. */
  e->Time100ns = Kind == 8u ? (ULONGLONG)InterlockedCompareExchange64(
      &Runtime->PendingQueuedTime100ns, 0, 0) : AdmissionScanoutTime100ns();
  e->Period = Runtime->Timeline.LastPeriod;
  e->PendingSequence = (ULONGLONG)InterlockedCompareExchange64(
      &Runtime->PendingSequence, 0, 0);
  e->PendingAddress = (ULONGLONG)InterlockedCompareExchange64(
      &Runtime->PendingPhysicalAddress, 0, 0);
  e->ActiveSequence = Runtime->Timeline.ActiveSequence;
  e->ActiveAddress = Runtime->Timeline.ActiveAddress;
  e->NotifyOrdinal = q->NotifyCount;
  e->Kind = Kind;
  e->Enabled = Runtime->Timeline.Enabled;
  e->Status = (ULONG)Status;
  e->Irql = KeGetCurrentIrql();
  q->EventCount = n + 1ULL;
}

static BOOLEAN AdmissionScanoutProcessInterrupt(ADMISSION_CONTEXT *Context,
                                               BOOLEAN VerticalTick) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  APPLE_AGX_SCANOUT_U32 irq_status = 0u;
  APPLE_AGX_SCANOUT_U64 latched_sequence = 0ULL;
  APPLE_AGX_SCANOUT_RESULT result;
  DXGKARGCB_NOTIFY_INTERRUPT_DATA data;
  LONG64 pending_sequence;
  BOOLEAN matched = FALSE, notify_vsync = FALSE, mpo_flip = FALSE;
  ULONG mpo_count = 0u, mpo_index;
  LONG mpo_kind;
  DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO2 mpo_info[2];
  ULONGLONG now;
  if (runtime == NULL ||
      InterlockedCompareExchange(&runtime->IrqEnabled, 0, 0) == 0 ||
      !Context->InterfaceValid ||
      Context->Interface.DxgkCbNotifyInterrupt == NULL ||
      Context->Interface.DxgkCbQueueDpc == NULL)
    return FALSE;
  RtlZeroMemory(mpo_info, sizeof(mpo_info));
  result = AppleAgxScanoutConsumeInterrupt(
      &runtime->Panel.Scanout, &irq_status, &latched_sequence);
  if (result == AppleAgxScanoutNoInterrupt && !VerticalTick)
    return FALSE;
  now = AdmissionScanoutTime100ns();
  if (result != AppleAgxScanoutNoInterrupt) {
    InterlockedExchange(&Context->LastInterruptStatus, (LONG)irq_status);
    InterlockedIncrement(&Context->InterruptCount);
    InterlockedIncrement(&Context->InterruptAckCount);
    if (result != AppleAgxScanoutOk ||
        (irq_status & APPLE_AGX_SCANOUT_IRQ_ERROR) != 0u) {
      InterlockedExchange(&runtime->Faulted, 1);
      runtime->Timeline.Running = APPLE_AGX_SCANOUT_FALSE;
      AdmissionScanoutVsyncRecord(runtime, 7u, STATUS_DEVICE_HARDWARE_ERROR);
      return TRUE;
    }
    pending_sequence = InterlockedCompareExchange64(
        &runtime->PendingSequence, 0, 0);
    if ((irq_status & APPLE_AGX_SCANOUT_IRQ_LATCHED) != 0u &&
        InterlockedCompareExchange(&runtime->PendingValid, 0, 0) != 0 &&
        latched_sequence == (ULONGLONG)pending_sequence &&
        latched_sequence > (ULONGLONG)InterlockedCompareExchange64(
            &runtime->LastNotifiedSequence, 0, 0)) {
      matched = AppleAgxVsyncLatch(&runtime->Timeline, latched_sequence,
          (ULONGLONG)InterlockedCompareExchange64(
              &runtime->PendingPhysicalAddress, 0, 0), now) != 0;
      if (matched) {
        RtlZeroMemory(mpo_info, sizeof(mpo_info));
        mpo_count = (ULONG)InterlockedExchange(&runtime->PendingMpoCount, 0);
        if (mpo_count > 2u)
          mpo_count = 2u;
        for (mpo_index = 0u; mpo_index < 2u; ++mpo_index) {
          mpo_info[mpo_index].LayerIndex = (ULONG)InterlockedExchange(
              &runtime->PendingMpoLayer[mpo_index], 0);
          mpo_info[mpo_index].PresentId = (ULONGLONG)InterlockedExchange64(
              &runtime->PendingMpoPresentId[mpo_index], 0);
        }
        mpo_flip = mpo_count != 0u;
        /* S2: both planes of a two-layer commit latch on this one swap;
         * the removal's latch takes slot 1 off the screen. */
        mpo_kind = InterlockedExchange(&runtime->PendingMpoKind,
                                       ADMISSION_SCANOUT_MPO_KIND_V2);
        if (mpo_kind == ADMISSION_SCANOUT_MPO_KIND_REMOVAL) {
          InterlockedExchange(&runtime->Slot1Shown, 0);
          InterlockedIncrement(&Context->Mpo.RemovalLatches);
        } else if (mpo_kind == ADMISSION_SCANOUT_MPO_KIND_TWO_LAYERS) {
          InterlockedIncrement(&Context->Mpo.TwoPlaneLatches);
        }
        if (InterlockedCompareExchange64(
                &runtime->PendingQueuedTime100ns, 0, 0) != 0)
          AdmissionScanoutVsyncRecord(runtime, 8u, STATUS_SUCCESS);
        AdmissionScanoutVsyncRecord(runtime, 3u, STATUS_SUCCESS);
        InterlockedExchange64(&runtime->PendingQueuedTime100ns, 0);
        InterlockedExchange64(&runtime->LastNotifiedSequence,
                              (LONG64)latched_sequence);
        InterlockedExchange(&runtime->PendingValid, 0);
        InterlockedExchange64(&runtime->PendingSequence, 0);
        InterlockedExchange64(&runtime->PendingPhysicalAddress, 0);
        InterlockedExchange(&runtime->PresentGate, 0);
        /* HOLD-1: the held set goes out on this latch (its vsync info,
         * already taken above, belongs to the swap that just latched). */
        AdmissionMpoNoteLatch(Context);
        AdmissionMpoTimingRecord(Context, ADMISSION_MPO_TIMING_LATCH,
                                 (ULONG)mpo_count,
                                 (ULONGLONG)mpo_info[0].PresentId);
        /* HOLD-3b: hand a held set to the deadline (same vblank, free gate). */
        AdmissionMpoQueueCommit(Context, TRUE);
      }
    } else if ((irq_status & APPLE_AGX_SCANOUT_IRQ_LATCHED) != 0u) {
      AdmissionScanoutVsyncRecord(runtime, 4u, STATUS_SUCCESS);
    }
  }
  if (VerticalTick) {
    /* HOLD-3c: the software tick keeps the latch phase in frames without a
     * latch (tick - learned offset, mpo_hold.h). */
    AdmissionMpoNoteTick(Context);
    /* HOLD-1: a held set whose commit met an obstacle retries per tick. */
    if (InterlockedCompareExchange(&runtime->PresentGate, 0, 0) == 0)
      AdmissionMpoQueueCommit(Context, FALSE);
    notify_vsync = AppleAgxVsyncAdvance(&runtime->Timeline, now) != 0;
    AdmissionScanoutVsyncRecord(runtime, 5u, STATUS_SUCCESS);
  }
  if (matched && runtime->Timeline.Enabled)
    notify_vsync = TRUE;
  if (!mpo_flip && notify_vsync &&
      InterlockedCompareExchange(&runtime->PlaneOffPending, 0, 0) != 0) {
    /* A plane disable completes on the next vertical sync. */
    InterlockedExchange(&runtime->PlaneOffPending, 0);
    RtlZeroMemory(mpo_info, sizeof(mpo_info));
    mpo_count = (ULONG)InterlockedExchange(&runtime->PlaneOffCount, 0);
    if (mpo_count > 2u)
      mpo_count = 2u;
    for (mpo_index = 0u; mpo_index < 2u; ++mpo_index) {
      mpo_info[mpo_index].LayerIndex = (ULONG)InterlockedExchange(
          &runtime->PlaneOffLayer[mpo_index], 0);
      mpo_info[mpo_index].PresentId = (ULONGLONG)InterlockedExchange64(
          &runtime->PlaneOffPresentId[mpo_index], 0);
    }
    /* A zero-plane request (ForcePlanesOff) still reports layer 0. */
    if (mpo_count == 0u)
      mpo_count = 1u;
    mpo_flip = TRUE;
  }
  if (notify_vsync && runtime->Timeline.Running &&
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0) {
    RtlZeroMemory(&data, sizeof(data));
    if (mpo_flip) {
      /* One DXGK_MULTIPLANE_OVERLAY_VSYNC_INFO2 per plane that flipped:
       * its LayerIndex and PresentId (d3dkmddi.h CrtcVsyncWith
       * MultiPlaneOverlay2; MS Learn DXGK_MULTIPLANE_OVERLAY_PLANE3:
       * "per-plane identifier used by the driver to report completion"). */
      data.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2;
      data.CrtcVsyncWithMultiPlaneOverlay2.VidPnTargetId = 0u;
      data.CrtcVsyncWithMultiPlaneOverlay2.MultiPlaneOverlayVsyncInfoCount =
          mpo_count;
      data.CrtcVsyncWithMultiPlaneOverlay2.pMultiPlaneOverlayVsyncInfo =
          mpo_info;
    } else {
      data.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
      data.CrtcVsync.VidPnTargetId = 0u;
      data.CrtcVsync.PhysicalAddress.QuadPart =
          (LONGLONG)runtime->Timeline.ActiveAddress;
    }
    Context->Interface.DxgkCbNotifyInterrupt(
        Context->Interface.DeviceHandle, &data);
    if (mpo_flip) {
      /* HOLD-1 receipt: every PresentId reported, per layer. */
      for (mpo_index = 0u; mpo_index < mpo_count; ++mpo_index)
        InterlockedIncrement(mpo_info[mpo_index].LayerIndex != 0u
                                 ? &Context->Mpo.ReportedIds1
                                 : &Context->Mpo.ReportedIds0);
    }
    AdmissionMpoNoteNotify(Context);
    if (runtime->VsyncReceipt.NotifyCount != APPLE_AGX_VSYNC_MAX)
      runtime->VsyncReceipt.NotifyCount++;
    AdmissionScanoutVsyncRecord(runtime, 6u, STATUS_SUCCESS);
    InterlockedExchange(&Context->SchedulerDpcPending, 1);
    (void)Context->Interface.DxgkCbQueueDpc(Context->Interface.DeviceHandle);
  } else if (matched && runtime->Timeline.Running) {
    /* Arm the source from DPC even when the first latch preceded OS enable. */
    (void)Context->Interface.DxgkCbQueueDpc(Context->Interface.DeviceHandle);
  }
  return result != AppleAgxScanoutNoInterrupt || VerticalTick;
}

_Use_decl_annotations_ BOOLEAN AdmissionScanoutInterrupt(
    ADMISSION_CONTEXT *Context) {
  return AdmissionScanoutProcessInterrupt(Context, FALSE);
}

static BOOLEAN AdmissionScanoutVsyncTick(PVOID Opaque) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Opaque;
  if (runtime == NULL)
    return FALSE;
  if (InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0)
    return TRUE;
  (void)AdmissionScanoutProcessInterrupt(runtime->Adapter, TRUE);
  return TRUE;
}

static BOOLEAN AdmissionScanoutVsyncDpcReceipt(PVOID Opaque) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Opaque;
  if (runtime == NULL)
    return FALSE;
  if (runtime->VsyncReceipt.DpcCount != APPLE_AGX_VSYNC_MAX)
    runtime->VsyncReceipt.DpcCount++;
  runtime->VsyncReceipt.AcknowledgedNotifyCount =
      runtime->VsyncReceipt.NotifyCount;
  AdmissionScanoutVsyncRecord(runtime, 8u, STATUS_SUCCESS);
  return TRUE;
}

_Use_decl_annotations_ static BOOLEAN AdmissionScanoutVsyncControl(PVOID Opaque) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Opaque;
  if (runtime == NULL)
    return FALSE;
  runtime->Timeline.Enabled = InterlockedCompareExchange(
      &runtime->VsyncNotifyEnabled, 0, 0) != 0
          ? APPLE_AGX_SCANOUT_TRUE : APPLE_AGX_SCANOUT_FALSE;
  if (InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0)
    runtime->Timeline.Running = APPLE_AGX_SCANOUT_FALSE;
  AdmissionScanoutVsyncRecord(runtime, 2u, STATUS_SUCCESS);
  return TRUE;
}

static BOOLEAN AdmissionScanoutVsyncNext(PVOID Opaque) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Opaque;
  if (runtime == NULL)
    return FALSE;
  runtime->TimerDeadlineValid = AppleAgxVsyncDeadline(
      &runtime->Timeline, &runtime->TimerDeadline) != 0;
  return TRUE;
}

static VOID AdmissionScanoutScheduleTimer(ADMISSION_SCANOUT_RUNTIME *Runtime) {
  ULONGLONG next, now;
  LARGE_INTEGER due;
  BOOLEAN done = FALSE;
  /* Timer scheduling is <=DISPATCH. State/deadline sampling is at DIRQL. */
  if (Runtime->TimerArmed ||
      InterlockedCompareExchange(&Runtime->Stopping, 0, 0) != 0 ||
      !NT_SUCCESS(Runtime->Adapter->Interface.DxgkCbSynchronizeExecution(
          Runtime->Adapter->Interface.DeviceHandle, AdmissionScanoutVsyncNext,
          Runtime, 0u, &done)) || !done || !Runtime->TimerDeadlineValid)
    return;
  next = Runtime->TimerDeadline + ADMISSION_SCANOUT_TICK_MARGIN_100NS;
  Runtime->TimerDue = next;
  now = AdmissionScanoutTime100ns();
  due.QuadPart = next > now && next - now <= (ULONGLONG)MAXLONGLONG
                    ? -(LONGLONG)(next - now) : -1LL;
  (void)KeSetTimer(&Runtime->VsyncTimer, due, &Runtime->VsyncDpc);
  Runtime->TimerArmed = TRUE;
}

_Use_decl_annotations_ static VOID AdmissionScanoutTimerDpc(KDPC *Dpc, PVOID Opaque,
                                   PVOID Arg1, PVOID Arg2) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Opaque;
  KIRQL oldIrql;
  BOOLEAN done = FALSE;
  NTSTATUS status;
  if (runtime == NULL || runtime->Adapter == NULL)
    return;
  UNREFERENCED_PARAMETER(Dpc);
  UNREFERENCED_PARAMETER(Arg1);
  UNREFERENCED_PARAMETER(Arg2);
  KeAcquireSpinLock(&runtime->TimerLock, &oldIrql);
  runtime->TimerArmed = FALSE;
  if (InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0) {
    status = runtime->Adapter->Interface.DxgkCbSynchronizeExecution(
        runtime->Adapter->Interface.DeviceHandle, AdmissionScanoutVsyncTick,
        runtime, 0u, &done);
    if (NT_SUCCESS(status) && done)
      AdmissionScanoutScheduleTimer(runtime);
  }
  KeReleaseSpinLock(&runtime->TimerLock, oldIrql);
}

_Use_decl_annotations_ VOID AdmissionScanoutDpc(ADMISSION_CONTEXT *Context) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  KIRQL oldIrql;
  BOOLEAN done = FALSE;
  if (runtime == NULL || !Context->InterfaceValid ||
      Context->Interface.DxgkCbNotifyDpc == NULL)
    return;
  KeAcquireSpinLock(&runtime->TimerLock, &oldIrql);
  if (InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0 &&
      NT_SUCCESS(Context->Interface.DxgkCbSynchronizeExecution(
          Context->Interface.DeviceHandle, AdmissionScanoutVsyncDpcReceipt,
          runtime, 0u, &done)) && done)
    AdmissionScanoutScheduleTimer(runtime);
  KeReleaseSpinLock(&runtime->TimerLock, oldIrql);
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutControlInterrupt(
    ADMISSION_CONTEXT *Context, BOOLEAN Enable) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  KIRQL oldIrql;
  BOOLEAN done = FALSE;
  NTSTATUS status;
  if (runtime == NULL ||
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) != 0)
    return STATUS_DEVICE_NOT_READY;
  KeAcquireSpinLock(&runtime->TimerLock, &oldIrql);
  InterlockedExchange(&runtime->VsyncNotifyEnabled, Enable ? 1 : 0);
  status = Context->Interface.DxgkCbSynchronizeExecution(
      Context->Interface.DeviceHandle, AdmissionScanoutVsyncControl,
      runtime, 0u, &done);
  if (NT_SUCCESS(status) && done) {
    if (Enable)
      AdmissionScanoutScheduleTimer(runtime);
    else {
      (void)KeCancelTimer(&runtime->VsyncTimer);
      runtime->TimerArmed = FALSE;
    }
  }
  KeReleaseSpinLock(&runtime->TimerLock, oldIrql);
  return NT_SUCCESS(status) && !done ? STATUS_UNSUCCESSFUL : status;
}

static BOOLEAN AdmissionScanoutVsyncPauseControl(PVOID Opaque) {
  ADMISSION_SCANOUT_RUNTIME *runtime = Opaque;
  if (runtime == NULL)
    return FALSE;
  runtime->Timeline.Running =
      InterlockedCompareExchange(&runtime->TimelinePaused, 0, 0) == 0 &&
      InterlockedCompareExchange(&runtime->Stopping, 0, 0) == 0 &&
      InterlockedCompareExchange(&runtime->Faulted, 0, 0) == 0
          ? APPLE_AGX_SCANOUT_TRUE : APPLE_AGX_SCANOUT_FALSE;
  AdmissionScanoutVsyncRecord(runtime, 9u, STATUS_SUCCESS);
  return TRUE;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutSetTimelinePaused(
    ADMISSION_CONTEXT *Context, BOOLEAN Paused) {
  ADMISSION_SCANOUT_RUNTIME *runtime = AdmissionScanoutGet(Context);
  KIRQL oldIrql;
  BOOLEAN done = FALSE;
  NTSTATUS status;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  if (runtime == NULL)
    return STATUS_SUCCESS;
  KeAcquireSpinLock(&runtime->TimerLock, &oldIrql);
  InterlockedExchange(&runtime->TimelinePaused, Paused ? 1 : 0);
  status = Context->Interface.DxgkCbSynchronizeExecution(
      Context->Interface.DeviceHandle, AdmissionScanoutVsyncPauseControl,
      runtime, 0u, &done);
  if (Paused) {
    (void)KeCancelTimer(&runtime->VsyncTimer);
    (void)KeRemoveQueueDpc(&runtime->VsyncDpc);
    runtime->TimerArmed = FALSE;
  } else if (NT_SUCCESS(status) && done) {
    if (runtime->Timeline.Running)
      AdmissionScanoutScheduleTimer(runtime);
    else
      status = STATUS_DEVICE_NOT_READY;
  }
  KeReleaseSpinLock(&runtime->TimerLock, oldIrql);
  if (Paused)
    KeFlushQueuedDpcs();
  return NT_SUCCESS(status) && !done ? STATUS_UNSUCCESSFUL : status;
}

typedef struct _ADMISSION_VSYNC_SNAPSHOT_ARGS {
  ADMISSION_SCANOUT_RUNTIME *Runtime;
  APPLE_AGX_VSYNC_QUERY *Query;
} ADMISSION_VSYNC_SNAPSHOT_ARGS;

static BOOLEAN AdmissionScanoutVsyncSnapshot(PVOID Opaque) {
  ADMISSION_VSYNC_SNAPSHOT_ARGS *args = Opaque;
  ADMISSION_SCANOUT_RUNTIME *runtime;
  APPLE_AGX_VSYNC_QUERY *q;
  if (args == NULL || args->Runtime == NULL || args->Query == NULL)
    return FALSE;
  runtime = args->Runtime;
  q = &runtime->VsyncReceipt;
  q->Phase100ns = runtime->Timeline.Phase100ns;
  q->ActiveSequence = runtime->Timeline.ActiveSequence;
  q->ActiveAddress = runtime->Timeline.ActiveAddress;
  q->LastPeriod = runtime->Timeline.LastPeriod;
  q->Enabled = runtime->Timeline.Enabled;
  q->Running = runtime->Timeline.Running;
  q->Paused = (ULONG)InterlockedCompareExchange(&runtime->TimelinePaused, 0, 0);
  q->Stopping = (ULONG)InterlockedCompareExchange(&runtime->Stopping, 0, 0);
  RtlCopyMemory(args->Query, q, sizeof(*q));
  return TRUE;
}

_Use_decl_annotations_ NTSTATUS AdmissionScanoutQueryTimeline(
    ADMISSION_CONTEXT *Context, APPLE_AGX_VSYNC_QUERY *Query) {
  ADMISSION_VSYNC_SNAPSHOT_ARGS args;
  BOOLEAN done = FALSE;
  NTSTATUS status;
  if (Query == NULL || Query->Magic != APPLE_AGX_VSYNC_QUERY_MAGIC ||
      Query->Version != 1u || Query->Bytes != sizeof(*Query))
    return STATUS_INVALID_PARAMETER;
  args.Runtime = AdmissionScanoutGet(Context);
  if (args.Runtime == NULL)
    return STATUS_DEVICE_NOT_READY;
  /* EXP1091 0xD1: the escape's private-data buffer is pageable, while the
   * snapshot runs at DIRQL. Snapshot into nonpaged memory there and copy to
   * the caller's buffer at PASSIVE_LEVEL. */
  args.Query = ExAllocatePool2(
      POOL_FLAG_NON_PAGED, sizeof(*args.Query), ADMISSION_SCANOUT_TAG);
  if (args.Query == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  status = Context->Interface.DxgkCbSynchronizeExecution(
      Context->Interface.DeviceHandle, AdmissionScanoutVsyncSnapshot,
      &args, 0u, &done);
  if (NT_SUCCESS(status) && done)
    RtlCopyMemory(Query, args.Query, sizeof(*Query));
  ExFreePoolWithTag(args.Query, ADMISSION_SCANOUT_TAG);
  return NT_SUCCESS(status) && !done ? STATUS_UNSUCCESSFUL : status;
}
