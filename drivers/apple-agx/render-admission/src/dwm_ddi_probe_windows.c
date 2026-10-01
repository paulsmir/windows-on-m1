#include "render_admission.h"

#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)

_Use_decl_annotations_ VOID AdmissionDwmDdiProbeRecordWindows(
    ADMISSION_CONTEXT *Context, const ADMISSION_DWM_DDI_EVENT *Event) {
  ADMISSION_DWM_DDI_ENTRY *entry;
  LONG sequence;
  if (Context == NULL || Event == NULL ||
      Event->Kind >= AdmissionDwmDdiMpo)
    return;
  entry = &Context->DwmDdiProbe.Entries[Event->Kind];
  InterlockedIncrement(&entry->Count);
  sequence = InterlockedCompareExchange(&entry->Sequence, 0, 0);
  if ((sequence & 1) != 0 ||
      InterlockedCompareExchange(&entry->Sequence, sequence + 1, sequence) !=
          sequence) {
    InterlockedIncrement(&entry->Dropped);
    return;
  }
  entry->Last = *Event;
  KeMemoryBarrier();
  InterlockedExchange(&entry->Sequence, sequence + 2);
}

_Use_decl_annotations_ NTSTATUS AdmissionDwmDdiProbeQueryWindows(
    ADMISSION_CONTEXT *Context, ADMISSION_DWM_DDI_PROBE *Query) {
  ADMISSION_DWM_DDI_PROBE snapshot;
  unsigned int index;
  if (Context == NULL || Query == NULL || !Context->Started ||
      Query->Magic != ADMISSION_DWM_DDI_PROBE_MAGIC ||
      Query->Version != ADMISSION_DWM_DDI_PROBE_VERSION ||
      Query->Bytes != sizeof(*Query))
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&snapshot, sizeof(snapshot));
  snapshot.Magic = ADMISSION_DWM_DDI_PROBE_MAGIC;
  snapshot.Version = ADMISSION_DWM_DDI_PROBE_VERSION;
  snapshot.Bytes = sizeof(snapshot);
  snapshot.CandidateBuild = APPLE_AGX_VERSION_BUILD;
  snapshot.BootGeneration = Context->Win32BootGeneration;
  for (index = 0; index < AdmissionDwmDdiMpo; ++index) {
    ADMISSION_DWM_DDI_ENTRY *entry = &Context->DwmDdiProbe.Entries[index];
    LONG before = InterlockedCompareExchange(&entry->Sequence, 0, 0);
    snapshot.Entries[index].Count =
        InterlockedCompareExchange(&entry->Count, 0, 0);
    snapshot.Entries[index].Dropped =
        InterlockedCompareExchange(&entry->Dropped, 0, 0);
    if ((before & 1) == 0) {
      snapshot.Entries[index].Last = entry->Last;
      KeMemoryBarrier();
      if (before == InterlockedCompareExchange(&entry->Sequence, 0, 0)) {
        snapshot.Entries[index].Sequence = before;
        continue;
      }
    }
    snapshot.Incomplete = 1u;
  }
  *Query = snapshot;
  return STATUS_SUCCESS;
}

#endif

#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)

static BOOLEAN AdmissionDwmFrameClaim(ADMISSION_CONTEXT *adapter) {
  if (adapter == NULL) return FALSE;
  if (InterlockedCompareExchange(&adapter->DwmFrameProbe.WriteClaim, 1, 0) == 0) {
    InterlockedIncrement(&adapter->DwmFrameProbe.Sequence);
    return TRUE;
  }
  InterlockedIncrement((volatile LONG *)&adapter->DwmFrameProbe.Dropped);
  return FALSE;
}

static VOID AdmissionDwmFrameRelease(ADMISSION_CONTEXT *adapter) {
  KeMemoryBarrier();
  InterlockedIncrement(&adapter->DwmFrameProbe.Sequence);
  InterlockedExchange(&adapter->DwmFrameProbe.WriteClaim, 0);
}

static ADMISSION_DWM_FRAME_ENTRY *AdmissionDwmFrameBegin(
    ADMISSION_CONTEXT *adapter, PVOID context) {
  UINT index;
  if (context == NULL || !AdmissionDwmFrameClaim(adapter)) return NULL;
  for (index = 0u; index < ADMISSION_DWM_FRAME_CAPACITY; ++index)
    if (adapter->DwmFrameProbe.Entries[index].Context ==
        (ULONGLONG)(ULONG_PTR)context)
      return &adapter->DwmFrameProbe.Entries[index];
  AdmissionDwmFrameRelease(adapter);
  return NULL;
}

static VOID AdmissionDwmFrameEnd(ADMISSION_CONTEXT *adapter,
    ADMISSION_DWM_FRAME_ENTRY *entry) {
  entry->Sequence += 2;
  AdmissionDwmFrameRelease(adapter);
}

_Use_decl_annotations_ BOOLEAN AdmissionDwmFrameArmWindows(
    ADMISSION_CONTEXT *adapter, PVOID context, ULONG osPid,
    ULONGLONG graphPid, ULONGLONG allocation, ULONGLONG canonicalVa) {
  ADMISSION_DWM_FRAME_ENTRY *entry = NULL;
  UINT index;
  if (adapter == NULL || context == NULL || osPid == 0u ||
      !AdmissionDwmFrameClaim(adapter)) return FALSE;
  for (index = 0u; index < ADMISSION_DWM_FRAME_CAPACITY; ++index)
    if (adapter->DwmFrameProbe.Entries[index].Context ==
        (ULONGLONG)(ULONG_PTR)context) {
      entry = &adapter->DwmFrameProbe.Entries[index];
      break;
    }
  if (entry == NULL)
    for (index = 0u; index < ADMISSION_DWM_FRAME_CAPACITY; ++index)
      if (adapter->DwmFrameProbe.Entries[index].Context == 0ULL) {
        entry = &adapter->DwmFrameProbe.Entries[index];
        RtlZeroMemory(entry, sizeof(*entry));
        ++adapter->DwmFrameProbe.ArmedCount;
        break;
      }
  if (entry != NULL) {
    entry->OsProcessId = osPid;
    entry->GraphProcessId = graphPid;
    entry->Allocation = allocation;
    if (canonicalVa != 0ULL) entry->CanonicalGpuVa = canonicalVa;
    KeMemoryBarrier();
    entry->Context = (ULONGLONG)(ULONG_PTR)context;
    entry->Sequence += 2;
  } else {
    ++adapter->DwmFrameProbe.Dropped;
  }
  AdmissionDwmFrameRelease(adapter);
  return entry != NULL;
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordQuery(
    ADMISSION_CONTEXT *adapter, PVOID context, ULONG predicate,
    NTSTATUS status, ULONG residentPages) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  ++entry->QueryCount;
  entry->QueryPredicate = predicate;
  entry->QueryStatus = (ULONG)status;
  entry->QueryResidentPages = residentPages;
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordSubmit(
    ADMISSION_CONTEXT *adapter, PVOID context, ULONGLONG commandVa,
    ULONGLONG fence, ULONG branch, NTSTATUS status, BOOLEAN present) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  if (present) {
    ++entry->VirtualPresentCount;
    entry->VirtualPresentStatus = (ULONG)status;
    entry->PresentFence = fence;
  } else {
    ++entry->SubmitCount;
    entry->SubmitStatus = (ULONG)status;
    if (branch != 0u || NT_SUCCESS(status)) entry->SubmitBranch = branch;
    entry->CommandGpuVa = commandVa;
    entry->SubmittedFence = fence;
  }
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordReject(
    ADMISSION_CONTEXT *adapter, PVOID context, ULONG branch, NTSTATUS status) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  entry->SubmitBranch = branch;
  entry->SubmitStatus = (ULONG)status;
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordEnvelope(
    ADMISSION_CONTEXT *adapter, PVOID context,
    const ADMISSION_DWM_ENVELOPE_RECEIPT *receipt) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  if (entry->Envelope.Stage == 0u) entry->Envelope = *receipt;
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordCompletion(
    ADMISSION_CONTEXT *adapter, PVOID context, ULONGLONG fence) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  ++entry->CompleteCount;
  entry->CompletedFence = fence;
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordPresent(
    ADMISSION_CONTEXT *adapter, PVOID context, NTSTATUS status, BOOLEAN count) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  if (count) ++entry->PresentCount;
  entry->PresentStatus = (ULONG)status;
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordBlt(
    ADMISSION_CONTEXT *adapter, PVOID context, ULONGLONG sourceAllocation,
    ULONGLONG destinationAllocation, ULONGLONG sourceVa,
    ULONGLONG destinationVa) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  entry->SourceAllocation = sourceAllocation;
  entry->DestinationAllocation = destinationAllocation;
  entry->SourceGpuVa = sourceVa;
  entry->DestinationGpuVa = destinationVa;
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordCopy(
    ADMISSION_CONTEXT *adapter, PVOID context, ULONGLONG destinationIpa,
    ULONGLONG bytesCopied, NTSTATUS status) {
  ADMISSION_DWM_FRAME_ENTRY *entry = AdmissionDwmFrameBegin(adapter, context);
  if (entry == NULL) return;
  entry->DestinationGuestIpa = destinationIpa;
  entry->CopiedBytes = bytesCopied;
  entry->CopyStatus = (ULONG)status;
  AdmissionDwmFrameEnd(adapter, entry);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordTdr(
    ADMISSION_CONTEXT *adapter, NTSTATUS status, BOOLEAN afterReset) {
  ADMISSION_DWM_FRAME_TDR snapshot = {0};
  KIRQL oldIrql;
  if (adapter == NULL) return;
  if (!afterReset) {
    RtlZeroMemory(&snapshot, sizeof(snapshot));
    KeAcquireSpinLock(&adapter->SchedulerLock, &oldIrql);
    snapshot.Captured = 1u;
    snapshot.PacketState = (ULONG)AdmissionRenderPacketState(&adapter->RenderPacket);
    snapshot.PacketContext = adapter->RenderPacket.Description.ContextToken;
    snapshot.PacketFence = adapter->RenderPacket.Description.Fence;
    snapshot.SchedulerCompletedFence = adapter->Scheduler.CompletedFence;
    snapshot.SchedulerLastSubmittedFence = adapter->Scheduler.LastSubmittedFence;
    snapshot.SchedulerActiveFence = AppleAgxSchedulerActiveFence(
        &adapter->Scheduler, 0u, 0u);
    snapshot.PagingPending =
        (ULONG)InterlockedCompareExchange(&adapter->PagingPending, 0, 0);
    KeReleaseSpinLock(&adapter->SchedulerLock, oldIrql);
    snapshot.ResetStatus = (ULONG)STATUS_PENDING;
    snapshot.PrivateResetStatus = MAXULONG;
  }
  if (!AdmissionDwmFrameClaim(adapter)) return;
  if (afterReset)
    adapter->DwmFrameProbe.Tdr.ResetStatus = (ULONG)status;
  else
    adapter->DwmFrameProbe.Tdr = snapshot;
  AdmissionDwmFrameRelease(adapter);
}

_Use_decl_annotations_ VOID AdmissionDwmFrameRecordPrivateReset(
    ADMISSION_CONTEXT *adapter, BOOLEAN succeeded) {
  if (!AdmissionDwmFrameClaim(adapter)) return;
  adapter->DwmFrameProbe.Tdr.PrivateResetStatus =
      succeeded ? (ULONG)STATUS_SUCCESS : (ULONG)STATUS_DEVICE_HARDWARE_ERROR;
  AdmissionDwmFrameRelease(adapter);
}

_Use_decl_annotations_ NTSTATUS AdmissionDwmFrameProbeQueryWindows(
    ADMISSION_CONTEXT *adapter, ADMISSION_DWM_FRAME_PROBE *probe) {
  ADMISSION_DWM_FRAME_PROBE snapshot;
  LONG before;
  if (adapter == NULL || probe == NULL || !adapter->Started ||
      probe->Magic != ADMISSION_DWM_FRAME_PROBE_MAGIC ||
      probe->Version != ADMISSION_DWM_FRAME_VERSION ||
      probe->Bytes != sizeof(*probe) || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  before = InterlockedCompareExchange(&adapter->DwmFrameProbe.Sequence, 0, 0);
  if ((before & 1) != 0)
    return STATUS_DEVICE_BUSY;
  RtlCopyMemory(&snapshot, &adapter->DwmFrameProbe, sizeof(snapshot));
  KeMemoryBarrier();
  if (before != InterlockedCompareExchange(&adapter->DwmFrameProbe.Sequence, 0, 0))
    return STATUS_DEVICE_BUSY;
  snapshot.Magic = ADMISSION_DWM_FRAME_PROBE_MAGIC;
  snapshot.Version = ADMISSION_DWM_FRAME_VERSION;
  snapshot.Bytes = sizeof(snapshot);
  snapshot.CandidateBuild = APPLE_AGX_VERSION_BUILD;
  snapshot.BootGeneration = adapter->Win32BootGeneration;
  snapshot.WriteClaim = 0;
  *probe = snapshot;
  return STATUS_SUCCESS;
}

#endif
