#include "gpuva_g3_private.h"
#include <intrin.h> /* _ReturnAddress (cancel-caller receipt) */
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_g3_copy_gate.h"
#include "apple_agx_g4_submit.h"
#include "apple_agx_g4_preflight.h"
#include "apple_agx_g13_compute_work.h"
#include "apple_agx_present_job.h"
#include "render_job_timing.h"
#include "render_job_ahead.h"
/* L1 receipt-only lock profile: every fast mutex in this file is the G3
 * state Lock (gpuva_g3_private.h). */
#define ADMISSION_G3_LOCK_FILE 1u
#define ExAcquireFastMutex(m) ADMISSION_G3_PROFILED_ACQUIRE(m)
#define ExReleaseFastMutex(m) ADMISSION_G3_PROFILED_RELEASE(m)

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)

/* EXP1932: the rolling omission ring can be overwritten before collection.
 * Snapshot the first valid fault's current group while JOB_BEGIN still owns
 * the graph. Never wait behind paging/recovery or dereference a saved token. */
VOID AdmissionGpuvaG3CaptureFaultMapping(ADMISSION_CONTEXT *Adapter,
    const ADMISSION_GPU_FAULT_EVENT *Event, ADMISSION_GPU_FAULT_MAPPING *Receipt) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  const APPLE_AGX_GPUVA_G3_NODE *leaf;
  ULONG bucket, first, i;
  if (Receipt == NULL || Receipt->Version || Event == NULL ||
      (Event->Flags & (ADMISSION_GPU_FAULT_INFO_READ | ADMISSION_GPU_FAULT_INFO_VALID)) !=
          (ADMISSION_GPU_FAULT_INFO_READ | ADMISSION_GPU_FAULT_INFO_VALID))
    return;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = 2u;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->Fence = Event->Fence;
  Receipt->ContextToken = Event->ContextToken;
  Receipt->FaultInfo = Event->FaultInfo;
  Receipt->FaultVa = (Event->FaultInfo >> 30) << 6;
  if (Adapter == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      Adapter->GpuvaG3State == NULL) return;
  state = (ADMISSION_G3_STATE *)Adapter->GpuvaG3State;
  Receipt->Status = AdmissionGpuFaultMappingBusy;
  if (!ExTryToAcquireFastMutex(&state->Lock)) return;
  Receipt->Status = AdmissionGpuFaultMappingIdentity;
  process = state->ActiveProcess;
  if (process == NULL || state->ActiveFence != Event->Fence ||
      !(Event->Flags & ADMISSION_GPU_FAULT_JOB_KNOWN)) goto Done;
  for (context = process->Contexts; context != NULL;
       context = context->GpuvaG3NextContext)
    if ((ULONGLONG)(ULONG_PTR)context == Event->ContextToken) break;
  if (context == NULL) goto Done;
  if (process->FaultDrill.Fence == Event->Fence &&
      process->FaultDrill.GpuVa == Receipt->FaultVa &&
      process->Graph.Slot == ((Event->FaultInfo >> 17) & 0x3fu))
    process->FaultDrill.FaultInfo = Event->FaultInfo;
  Receipt->OsProcessId = process->OsProcessId;
  Receipt->VmSlot = process->Graph.Slot;
  Receipt->GraphUncertain = process->Graph.Uncertain;
  Receipt->CpuOnlyMappings = process->CpuOnlyMappings;
  Receipt->JobInFlight = process->Graph.JobInFlight;
  Receipt->GraphProcessId = process->Graph.ProcessId;
  Receipt->RootIpa = process->Graph.RootIpa;
  Receipt->ProcessGeneration = process->Graph.ProcessGeneration;
  Receipt->MappingGeneration = process->Graph.MappingGeneration;
  Receipt->LeaseToken = process->Graph.LeaseToken;
  Receipt->Status = AdmissionGpuFaultMappingVm;
  if (Receipt->VmSlot != ((Event->FaultInfo >> 17) & 0x3fu)) goto Done;
  Receipt->Status = AdmissionGpuFaultMappingUncertain;
  if (Receipt->GraphUncertain || !Receipt->JobInFlight ||
      !Receipt->LeaseToken || Receipt->CpuOnlyMappings) goto Done;
  Receipt->Status = AdmissionGpuFaultMappingNoTable;
  if (!AppleAgxGpuvaG3GraphLeafTableIpa(&process->Graph,
          Receipt->FaultVa, &Receipt->TableIpa)) goto Done;
  leaf = AppleAgxGpuvaG3GraphLeaf(&process->Graph, Receipt->TableIpa,
      (ULONG)((Receipt->FaultVa >> 14) & 2047u));
  if (leaf != NULL) {
    Receipt->NativePresent = leaf->AuxIpa != 0ULL;
    Receipt->NativeIpa = leaf->AuxIpa;
    Receipt->NativeWritable = leaf->Writable;
    Receipt->NativeKind = (ULONG)leaf->Kind;
  }
  Receipt->Status = AdmissionGpuFaultMappingNoShadow;
  bucket = (ULONG)((Receipt->TableIpa >> 14) ^ (Receipt->TableIpa >> 22) ^
      (Receipt->TableIpa >> 30)) & 255u;
  for (shadow = process->TableShadowBrokerBuckets[bucket]; shadow != NULL;
       shadow = shadow->NextBroker)
    if (shadow->BrokerIpa == Receipt->TableIpa) break;
  if (shadow == NULL || shadow->LogicalPtes == NULL) goto Done;
  first = (ULONG)((Receipt->FaultVa >> 12) & 8191u) & ~3u;
  for (i = 0u; i < 4u; ++i) {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte = &shadow->LogicalPtes[first + i];
    Receipt->Ptes[i].GuestIpa = pte->GuestIpa;
    Receipt->Ptes[i].Allocation = pte->Allocation;
    Receipt->Ptes[i].AllocationOffset = pte->AllocationOffset;
    Receipt->Ptes[i].Segment = pte->SegmentId;
    Receipt->Ptes[i].Flags = pte->Flags;
  }
  Receipt->Status = AdmissionGpuFaultMappingCaptured;
Done:
  /* No allocation, wait, handle lookup or paging decision here. Only inspect
   * the existing locked ring after process/fence/VM authentication. Include
   * all four logical pages: changing a neighbor can remove a native leaf.
   * VA matches survive table replacement; table/index matches cover records
   * made before linking a table and whole-table clear/retire events. */
  if (Receipt->Status >= AdmissionGpuFaultMappingNoTable) {
    ULONGLONG base = Receipt->FaultVa & ~0x3fffULL;
    ULONG group = (ULONG)((base >> 12) & 8191u), n;
    Receipt->HistoryNext = state->LeafHistoryNext;
    Receipt->HistoryScanned = state->LeafHistoryNext < ADMISSION_G3_LEAF_RING ?
        state->LeafHistoryNext : ADMISSION_G3_LEAF_RING;
    for (n = 0u; n < Receipt->HistoryScanned; ++n) {
      ULONG sequence = state->LeafHistoryNext - n;
      const ADMISSION_G3_LEAF_HISTORY *h =
          &state->LeafHistory[(sequence - 1u) % ADMISSION_G3_LEAF_RING];
      ULONG match = 0u;
      ADMISSION_GPU_FAULT_HISTORY *out;
      if (h->ProcessId != Receipt->GraphProcessId || !h->Count ||
          h->Count > 8192u) continue;
      if (h->FirstVa && (h->FirstVa <= base ?
          base - h->FirstVa < (ULONGLONG)h->Count * 0x1000ULL :
          h->FirstVa - base < 0x4000ULL)) match |= 1u;
      if (Receipt->TableIpa && h->TableIpa == Receipt->TableIpa &&
          h->First < 8192u && h->Count <= 8192u - h->First &&
          h->First < group + 4u && group < h->First + h->Count) match |= 2u;
      if (!match) continue;
      ++Receipt->HistoryMatches;
      if (Receipt->HistoryCount == ADMISSION_GPU_FAULT_HISTORY_COUNT) continue;
      out = &Receipt->History[Receipt->HistoryCount++];
      out->Qpc = h->Qpc; out->ProcessId = h->ProcessId;
      out->TableIpa = h->TableIpa; out->Allocation = h->Allocation;
      out->FirstVa = h->FirstVa; out->MappingGeneration = h->MappingGeneration;
      out->First = h->First; out->Count = h->Count;
      out->ValidCount = h->ValidCount; out->Flags = h->Flags;
      out->Status = h->Status; out->FirstSegment = h->FirstSegment;
      out->Sequence = sequence; out->Match = match;
    }
  }
  ExReleaseFastMutex(&state->Lock);
}

/* Branch IDs are a stable diagnostic ABI for Wom1G4SubmitFailure. */
enum {
  AdmissionG4RejectOuter = 1,
  AdmissionG4RejectContext = 2,
  AdmissionG4RejectPagingInput = 3,
  AdmissionG4RejectPagingShape = 4,
  AdmissionG4RejectPagingRecords = 5,
  AdmissionG4RejectPagingQueue = 6,
  AdmissionG4RejectEnvelopeState = 7,
  AdmissionG4RejectRoot = 8,
  AdmissionG4RejectParse = 9,
  AdmissionG4RejectPrepare = 10,
  AdmissionG4RejectBind = 11,
  AdmissionG4RejectQueue = 12,
  AdmissionG4RejectLegacyShape = 13,
  AdmissionG4RejectLegacyPacket = 14,
  AdmissionG4RejectLegacySubmit = 15,
  AdmissionG4RejectStatus = 16,
  /* Version 4 PrepareMask: path/result bits and observed predicates.
   * Queue reason bits are meaningful only with QueueRefused. */
  AdmissionG4PrepareDeferred = 0x1u,
  AdmissionG4PrepareImmediate = 0x2u,
  AdmissionG4PrepareAdmissible = 0x4u,
  AdmissionG4PrepareFenceOutstanding = 0x8u,
  AdmissionG4PrepareSecondFence = 0x10u,
  AdmissionG4PreparePendingFull = 0x20u,
  AdmissionG4PrepareTailBusy = 0x40u,
  AdmissionG4PreparePacketBusy = 0x80u,
  AdmissionG4PrepareFaulted = 0x100u,
  AdmissionG4PrepareQueueAttempted = 0x200u,
  AdmissionG4PrepareQueueRefused = 0x400u,
  AdmissionG4PrepareEntryChanged = 0x800u,
  AdmissionG4PrepareInvalidShape = 0x1000u,
  AdmissionG4PreparePacketRefused = 0x2000u,
  AdmissionG4PrepareQueueFull = 0x4000u,
  AdmissionG4PreparePreemptionBlocked = 0x8000u,
  AdmissionG4PrepareZeroFence = 0x10000u,
  AdmissionG4PrepareFenceNotAfter = 0x20000u,
  AdmissionG4PrepareInvalidEngine = 0x40000u,
  /* PrepareSite values, independent of render versus compute. */
  AdmissionG4PrepareShape = 1u,
  AdmissionG4PrepareReserve = 2u,
  AdmissionG4PreparePublish = 3u,
  AdmissionG4PreparePacket = 4u
};

static NTSTATUS AdmissionG4SubmitRejectDetail(
    ADMISSION_CONTEXT *adapter, const ADMISSION_RENDER_CONTEXT *context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args, ULONG branch,
    NTSTATUS status, ULONG downstream, BOOLEAN validContext,
    const struct _ADMISSION_G4_SUBMIT_FAILURE *detail) {
  if (adapter != NULL) {
    (void)InterlockedIncrement(&adapter->G4SubmitFailureCount);
    if (InterlockedCompareExchange(&adapter->G4SubmitFailureClaim, 1, 0) == 0) {
      struct _ADMISSION_G4_SUBMIT_FAILURE *first =
          &adapter->G4SubmitFailure;
      RtlZeroMemory(first, sizeof(*first));
      if (detail != NULL) *first = *detail;
      first->Version = 4u;
      first->Bytes = sizeof(*first);
      first->Branch = branch;
      first->Status = (ULONG)status;
      first->DownstreamStatus = downstream;
      if (args != NULL) {
        first->DmaBufferVirtualAddress = args->DmaBufferVirtualAddress;
        first->DmaBufferSize = args->DmaBufferSize;
        first->PrivateDataSize = args->DmaBufferPrivateDataSize;
        first->UmdPrivateDataSize = args->DmaBufferUmdPrivateDataSize;
        first->Flags = args->Flags.Value;
      }
      first->ContextFlags = validContext ? context->Object.Flags : 0u;
      first->Pid = HandleToULong(PsGetCurrentProcessId());
      first->TotalFailures = 1u;
      KeMemoryBarrier();
      InterlockedExchange(&adapter->G4SubmitFailureClaim, 2);
    }
    AdmissionRenderCorrelationSubmitFailureWindows(adapter);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    if (validContext && context != NULL && args != NULL)
      AdmissionDwmFrameRecordReject(adapter, (PVOID)context, branch, status);
#endif
  }
  return status;
}

/* EXP1901N receipt-only: the first prepared job whose backend-image bind
 * failed (1 compute, 2 present, 3 render). Read without the scheduler lock:
 * the bind itself runs outside it. */
static VOID AdmissionG4RecordBindRefusal(ADMISSION_CONTEXT *adapter,
    ULONG kind, ULONG fence) {
  struct _ADMISSION_G4_BIND_REFUSAL *r;
  const ADMISSION_BACKEND_IMAGE *image;
  if (adapter == NULL ||
      InterlockedCompareExchange(&adapter->G4BindRefusalClaim, 1, 0) != 0)
    return;
  r = &adapter->G4BindRefusal;
  image = &adapter->BackendImage;
  RtlZeroMemory(r, sizeof(*r));
  r->Version = 2u;
  r->Bytes = sizeof(*r);
  r->Kind = kind;
  r->Fence = fence;
  r->EntryMs = (ULONG)(KeQueryInterruptTime() / 10000ull);
  r->Ready = (ULONG)image->Ready;
  r->BoundFence = image->BoundFence;
  r->JobFence = image->JobFence;
  r->JobReady = (ULONG)image->JobReady;
  r->NativeBound = (ULONG)image->NativeBound;
  r->G4Native = (ULONG)image->G4Native;
  r->G4Compute = (ULONG)image->G4Compute;
  r->PresentCompute = (ULONG)image->PresentCompute;
  r->CompletedFence = adapter->Scheduler.CompletedFence;
  r->LastSubmittedFence = adapter->Scheduler.LastSubmittedFence;
  r->ActiveFence = adapter->Scheduler.ActiveFence;
  r->PreemptionPhase = (ULONG)adapter->Scheduler.PreemptionPhase;
  r->PacketState = (ULONG)AdmissionRenderPacketState(&adapter->RenderPacket);
  r->PacketFence = adapter->RenderPacket.Description.Fence;
  r->DispatchedFence = adapter->DispatchedFence;
  r->RenderWorkerScheduled = AdmissionPlatformRenderWorkerScheduled(adapter) ? 1u : 0u;
  r->BindFailure = image->BindFailure;
  KeMemoryBarrier();
  InterlockedExchange(&adapter->G4BindRefusalClaim, 2);
}

static NTSTATUS AdmissionG4SubmitReject(
    ADMISSION_CONTEXT *adapter, const ADMISSION_RENDER_CONTEXT *context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args, ULONG branch,
    NTSTATUS status, ULONG downstream, BOOLEAN validContext) {
  return AdmissionG4SubmitRejectDetail(adapter, context, args, branch,
      status, downstream, validContext, NULL);
}

C_ASSERT(FIELD_OFFSET(ADMISSION_BACKEND_IMAGE, G4Command) ==
    FIELD_OFFSET(ADMISSION_BACKEND_IMAGE, G4Header) +
    sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V2));

static int AdmissionG4GraphAccess(void *opaque, unsigned long long va,
                                  unsigned int bytes, int write);
static int AdmissionG4GraphAccessTyped(void *opaque, unsigned long long va,
    unsigned int bytes, int write, APPLE_AGX_G4_ACCESS_KIND kind,
    unsigned int ordinal);
static int AdmissionG4LogicalEnvelopeAccess(ADMISSION_G3_PROCESS *process,
    unsigned long long va, unsigned int bytes);
/* rc80 TVB growth (defined after AdmissionG3ManagerWord). */
static BOOLEAN AdmissionG3TvbRelease(ADMISSION_G3_PROCESS *p);
static BOOLEAN AdmissionG3TvbReturnLeases(ADMISSION_G3_PROCESS *p);

static void *AdmissionG3AllocateNode(void *opaque, unsigned long long bytes) {
  UNREFERENCED_PARAMETER(opaque);
  if (bytes == 0ULL || bytes > MAXSIZE_T) return NULL;
  return ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)bytes,
                         ADMISSION_POOL_TAG);
}

static void AdmissionG3FreeNode(void *opaque, void *node) {
  UNREFERENCED_PARAMETER(opaque);
  if (node != NULL) ExFreePoolWithTag(node, ADMISSION_POOL_TAG);
}

ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(
    ADMISSION_G3_STATE *state, HANDLE handle) {
  PLIST_ENTRY link;
  if (state == NULL || handle == NULL) return NULL;
  for (link = state->Processes.Flink; link != &state->Processes;
       link = link->Flink) {
    ADMISSION_G3_PROCESS *process = CONTAINING_RECORD(
        link, ADMISSION_G3_PROCESS, Link);
    if ((HANDLE)process == handle &&
        process->Magic == ADMISSION_G3_PROCESS_MAGIC)
      return process;
  }
  return NULL;
}

/* Start-time receipt only (PASSIVE_LEVEL, never on the paging path):
 * 1 = mailbox attached, 2 = allocation failed, 3 = broker refused. */
static VOID AdmissionG3RecordMailbox(ADMISSION_CONTEXT *Adapter, ULONG Value) {
  UNICODE_STRING name;
  HANDLE key = NULL;
  if (Adapter->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Adapter->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  RtlInitUnicodeString(&name, L"Wom1G3Mailbox");
  (void)ZwSetValueKey(key, &name, 0, REG_DWORD, &Value, sizeof(Value));
  ZwClose(key);
}

/* One 16-KiB page carries each broker request and response, so a call costs
 * one trapped doorbell instead of 25 window accesses.  The page is cached
 * normal memory like the broker's own view of guest RAM; a broker without
 * mailbox support refuses the attach and the window path stays in use. */
static VOID AdmissionG3AttachMailbox(ADMISSION_G3_STATE *state) {
  PHYSICAL_ADDRESS lowest, highest, boundary;
  ULONGLONG ipa;
  PVOID page;
  lowest.QuadPart = 0;
  highest.QuadPart = (LONGLONG)(ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL);
  boundary.QuadPart = AGX_GPUVA_V5_MAILBOX_BYTES;
  page = MmAllocateContiguousNodeMemory(AGX_GPUVA_V5_MAILBOX_BYTES, lowest,
      highest, boundary, PAGE_READWRITE, MM_ANY_NODE_OK);
  if (page == NULL) {
    AdmissionG3RecordMailbox(state->Adapter, 2u);
    return;
  }
  RtlZeroMemory(page, AGX_GPUVA_V5_MAILBOX_BYTES);
  ipa = (ULONGLONG)MmGetPhysicalAddress(page).QuadPart;
  if ((ipa & (AGX_GPUVA_V5_MAILBOX_BYTES - 1u)) != 0u ||
      !AppleAgxGpuvaV5ClientAttachMailbox(&state->Client, page, ipa)) {
    MmFreeContiguousMemory(page);
    AdmissionG3RecordMailbox(state->Adapter, 3u);
    return;
  }
  state->Mailbox = page;
  AdmissionG3RecordMailbox(state->Adapter, 1u);
}

static VOID AdmissionG3DetachMailbox(ADMISSION_G3_STATE *state) {
  if (state->Mailbox == NULL) return;
  /* An unacknowledged detach leaves the page owned by the broker: leak it
   * rather than let Windows reuse a page the broker may still store into. */
  if (AppleAgxGpuvaV5ClientDetachMailbox(&state->Client))
    MmFreeContiguousMemory(state->Mailbox);
  state->Mailbox = NULL;
}

/* RC26: explicit ABI probe request; default off. */
static BOOLEAN AdmissionSysmemGpuMapRequested(ADMISSION_CONTEXT *Context) {
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
  RtlInitUnicodeString(&name, L"Wom1SysmemGpuMap");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

static BOOLEAN AdmissionLiveAdditivePagingRequested(ADMISSION_CONTEXT *Context) {
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
  RtlInitUnicodeString(&name, L"Wom1LiveAdditivePaging");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* GC (experiment): device value Wom1GpuPrivateCached = 1. */
static BOOLEAN AdmissionGpuPrivateCachedRequested(ADMISSION_CONTEXT *Context) {
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
  RtlInitUnicodeString(&name, L"Wom1GpuPrivateCached");
  if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                                 value, sizeof(data.Buffer), &bytes)) &&
      value->Type == REG_DWORD && value->DataLength == sizeof(ULONG) &&
      *(const ULONG *)value->Data == 1u)
    enabled = TRUE;
  ZwClose(key);
  return enabled;
}

/* PASSIVE_LEVEL startup only, before process creation. Accepted means the
 * capability enabled adapter topology; each process still negotiates later.
 * QWORDs: version, bytes, requested, queried, answered, status (UINT_MAX if
 * unanswered), flags, caps, extension ABI, accepted, paging node, epoch. */
static VOID AdmissionG3RecordLiveAdditiveState(ADMISSION_CONTEXT *Context,
    BOOLEAN Requested, const APPLE_AGX_GPUVA_V5_CLIENT *Client) {
  ULONGLONG value[12] = {0};
  UNICODE_STRING name;
  HANDLE key = NULL;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
      Context->PhysicalDeviceObject == NULL) return;
  value[0] = 1u;
  value[1] = sizeof(value);
  value[2] = Requested ? 1u : 0u;
  value[3] = Client->LiveAdditiveQueried;
  value[4] = Client->LiveAdditiveAnswered;
  value[5] = Client->LiveAdditiveAnswered ? Client->LiveAdditiveCaps.Status : ~0u;
  value[6] = Client->LiveAdditiveCaps.Flags;
  value[7] = Client->LiveAdditiveCaps.Reserved[0];
  value[8] = Client->LiveAdditiveCaps.Reserved[1];
  value[9] = Context->LiveAdditivePaging;
  value[10] = Context->ObjectAdapter.PagingNode;
  value[11] = Client->Epoch;
  if (!NT_SUCCESS(IoOpenDeviceRegistryKey(Context->PhysicalDeviceObject,
          PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &key))) return;
  RtlInitUnicodeString(&name, L"Wom1LiveAdditiveState");
  (void)ZwSetValueKey(key, &name, 0, REG_BINARY, value, sizeof(value));
  ZwClose(key);
}

NTSTATUS AdmissionGpuvaG3Start(ADMISSION_CONTEXT *context) {
  ADMISSION_G3_STATE *state;
  BOOLEAN liveRequested;
  AGX_GPUVA_V5_REQUEST probe = {0};
  AGX_GPUVA_V5_RESPONSE response = {0};
  if (context == NULL || context->GpuvaG3State != NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_DEVICE_STATE;
  state = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*state),
                          ADMISSION_POOL_TAG);
  if (state == NULL) return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(state, sizeof(*state));
  state->Adapter = context;
  ExInitializeFastMutex(&state->Lock);
  KeInitializeEvent(&state->JobEvent, NotificationEvent, FALSE);
  InitializeListHead(&state->Processes);
  if (!AdmissionGpuvaV5ClientOpen(context, &state->Client)) {
    ExFreePoolWithTag(state, ADMISSION_POOL_TAG);
    return STATUS_INVALID_DEVICE_STATE;
  }
  probe.Command = AGX_GPUVA_V5_CREATE;
  if (!AppleAgxGpuvaV5ClientCall(&state->Client, &probe, &response) ||
      response.Status != 2u || response.Epoch == 0ULL || response.Flags) {
    ExFreePoolWithTag(state, ADMISSION_POOL_TAG);
    return STATUS_DEVICE_HARDWARE_ERROR;
  }
  /* RC26 is an ABI prerequisite, not permission to publish shadow backing.
   * Old brokers refuse the appended query; opt-in fails before attachment. */
  if (AdmissionSysmemGpuMapRequested(context) && !AppleAgxGpuvaV5ClientHasSysmem(&state->Client)) {
    ExFreePoolWithTag(state, ADMISSION_POOL_TAG);
    return STATUS_NOT_SUPPORTED;
  }
  liveRequested = AdmissionLiveAdditivePagingRequested(context);
  context->LiveAdditivePaging = liveRequested &&
      AppleAgxGpuvaV5ClientHasLiveAdditive(&state->Client) ? 1u : 0u;
  context->ObjectAdapter.PagingNode = context->LiveAdditivePaging;
  AdmissionG3RecordLiveAdditiveState(context, liveRequested, &state->Client);
  /* GC (experiment, measurement only): GPU-only allocations as cached
   * leaves; the broker refuses the leaf bit unless it advertises it. */
  context->GpuPrivateCached =
      AdmissionGpuPrivateCachedRequested(context) &&
      AppleAgxGpuvaV5ClientHasPrivateCached(&state->Client) ? 1u : 0u;
  AdmissionG3AttachMailbox(state);
  context->GpuvaG3State = state;
  return STATUS_SUCCESS;
}

NTSTATUS AdmissionGpuvaG3Stop(ADMISSION_CONTEXT *context) {
  ADMISSION_G3_STATE *state;
  if (context == NULL) return STATUS_INVALID_PARAMETER;
#ifdef APPLE_AGX_NATIVE_STAGING
  if (!AdmissionNativeStagingAdapterIdle(context)) return STATUS_DEVICE_BUSY;
#endif
  state = (ADMISSION_G3_STATE *)context->GpuvaG3State;
  if (state == NULL) return STATUS_SUCCESS;
  /* Lifecycle tears down G3 before ordinary paging. Live CPU workers borrow
   * this state: stop admission and drain their complete callbacks first. */
  if (context->LiveAdditivePaging &&
      !NT_SUCCESS(AdmissionPagingStop(context))) return STATUS_DEVICE_BUSY;
  ExAcquireFastMutex(&state->Lock);
  if (state->ProcessCount != 0u || !IsListEmpty(&state->Processes) ||
      state->ActiveProcess != NULL) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_DEVICE_BUSY;
  }
  AdmissionG3DetachMailbox(state);
  context->GpuvaG3State = NULL;
  ExReleaseFastMutex(&state->Lock);
  ExFreePoolWithTag(state, ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionG3BootstrapRoot(
    ADMISSION_G3_PROCESS *process) {
  ADMISSION_PHYSICAL_ALLOCATION *allocation;
  ULONGLONG offset;
  if (!NT_SUCCESS(AdmissionMemoryRuntimeBorrowIo(
          process->State->Adapter, &process->Io)) ||
      AppleAgxMemoryAllocateAligned(&process->Io, 0x4000ULL, 0x4000ULL,
                                    &process->BootstrapRoot) !=
          AppleAgxMemoryResultOk)
    return STATUS_INSUFFICIENT_RESOURCES;
  allocation = (ADMISSION_PHYSICAL_ALLOCATION *)
      process->BootstrapRoot.AllocationHandle;
  if (allocation == NULL || process->BootstrapRoot.CpuAddress == NULL ||
      process->BootstrapRoot.AllocationCpuBase == NULL ||
      (PUCHAR)process->BootstrapRoot.CpuAddress <
          (PUCHAR)process->BootstrapRoot.AllocationCpuBase ||
      process->BootstrapRoot.DeviceAddress == 0ULL ||
      (process->BootstrapRoot.DeviceAddress & 0x3fffULL))
    return STATUS_INVALID_ADDRESS;
  offset = (ULONGLONG)((PUCHAR)process->BootstrapRoot.CpuAddress -
                       (PUCHAR)process->BootstrapRoot.AllocationCpuBase);
  if (allocation->GuestIpaBase > MAXULONGLONG - offset)
    return STATUS_INTEGER_OVERFLOW;
  process->BootstrapIpa = allocation->GuestIpaBase + offset;
  if (process->BootstrapIpa & 0x3fffULL) return STATUS_INVALID_ADDRESS;
  RtlZeroMemory(process->BootstrapRoot.CpuAddress, 0x4000u);
  return STATUS_SUCCESS;
}

/* VidMm may map its own page-table page as a writable leaf.  The broker must
 * keep the hardware table on a different physical page so that GPU writes to
 * the VidMm allocation cannot bypass validated graph updates. */
NTSTATUS AdmissionGpuvaG3BrokerTable(
    ADMISSION_G3_PROCESS *process, ULONGLONG original_ipa,
    BOOLEAN create, ULONGLONG *broker_ipa) {
  ADMISSION_G3_TABLE_SHADOW *entry;
  ADMISSION_PHYSICAL_ALLOCATION *allocation;
  ULONGLONG offset;
  if (process == NULL || broker_ipa == NULL || original_ipa == 0ULL ||
      (original_ipa & 0x3fffULL)) return STATUS_INVALID_PARAMETER;
  for (entry = process->TableShadows; entry != NULL; entry = entry->Next)
    if (entry->OriginalIpa == original_ipa) {
      *broker_ipa = entry->BrokerIpa;
      return STATUS_SUCCESS;
    }
  if (!create) return STATUS_INVALID_ADDRESS;
  entry = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*entry),
                          ADMISSION_POOL_TAG);
  if (entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(entry, sizeof(*entry));
  if (AppleAgxMemoryAllocateAligned(&process->Io, 0x4000ULL, 0x4000ULL,
                                    &entry->Memory) != AppleAgxMemoryResultOk) {
    ExFreePoolWithTag(entry, ADMISSION_POOL_TAG);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  allocation = (ADMISSION_PHYSICAL_ALLOCATION *)entry->Memory.AllocationHandle;
  if (allocation == NULL || allocation->Adl == NULL ||
      !allocation->Adl->Flags.Contiguous ||
      entry->Memory.CpuAddress == NULL ||
      entry->Memory.AllocationCpuBase == NULL ||
      (PUCHAR)entry->Memory.CpuAddress <
          (PUCHAR)entry->Memory.AllocationCpuBase ||
      entry->Memory.DeviceAddress == 0ULL ||
      (entry->Memory.DeviceAddress & 0x3fffULL)) goto Invalid;
  offset = (ULONGLONG)((PUCHAR)entry->Memory.CpuAddress -
                       (PUCHAR)entry->Memory.AllocationCpuBase);
  if (offset > allocation->Size ||
      0x4000ULL > allocation->Size - offset ||
      allocation->GuestIpaBase > MAXULONGLONG - offset) goto Invalid;
  entry->BrokerIpa = allocation->GuestIpaBase + offset;
  if (entry->BrokerIpa == original_ipa ||
      (entry->BrokerIpa & 0x3fffULL)) goto Invalid;
  RtlZeroMemory(entry->Memory.CpuAddress, 0x4000u);
  KeMemoryBarrier();
  entry->OriginalIpa = original_ipa;
  entry->Next = process->TableShadows;
  process->TableShadows = entry;
  {
    ULONG bucket = (ULONG)((entry->BrokerIpa >> 14) ^
        (entry->BrokerIpa >> 22) ^ (entry->BrokerIpa >> 30)) & 255u;
    entry->NextBroker = process->TableShadowBrokerBuckets[bucket];
    process->TableShadowBrokerBuckets[bucket] = entry;
  }
  *broker_ipa = entry->BrokerIpa;
  return STATUS_SUCCESS;
Invalid:
  (void)AppleAgxMemoryRelease(&process->Io, &entry->Memory);
  ExFreePoolWithTag(entry, ADMISSION_POOL_TAG);
  return STATUS_INVALID_ADDRESS;
}

NTSTATUS AdmissionGpuvaG3MirrorTable(
    ADMISSION_G3_PROCESS *process, ULONGLONG original_ipa,
    PVOID original_cpu_address) {
  ADMISSION_G3_TABLE_SHADOW *entry;
  if (process == NULL || original_cpu_address == NULL)
    return STATUS_INVALID_PARAMETER;
  for (entry = process->TableShadows; entry != NULL; entry = entry->Next)
    if (entry->OriginalIpa == original_ipa &&
        entry->Memory.CpuAddress != NULL) {
      RtlCopyMemory(original_cpu_address, entry->Memory.CpuAddress, 0x4000u);
      KeMemoryBarrier();
      return STATUS_SUCCESS;
    }
  return STATUS_INVALID_ADDRESS;
}

/* State lock and object ownership are supplied by the typed escape caller. */
static NTSTATUS AdmissionG3PreparePrivateStorageObserved(ADMISSION_G3_PROCESS *process,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    APPLE_AGX_G3_PRIVATE_MANAGER *manager, APPLE_AGX_G3_PRIVATE_SCENE *scene,
    APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC *diagnostic) {
  ADMISSION_BACKEND_MEMORY_VIEW view;
  NTSTATUS status;
  if (process == NULL || process->Poisoned || process->Graph.Uncertain ||
      process->Graph.JobInFlight)
    return STATUS_INVALID_DEVICE_STATE;
  status = AdmissionMemoryRuntimePrivateView(process->State->Adapter, &view);
  if (!NT_SUCCESS(status)) return status;
  if (view.CpuAddress == NULL || view.Bytes != (ULONGLONG)APPLE_AGX_G3_PRIVATE_UNITS * APPLE_AGX_G3_PRIVATE_UNIT ||
      (view.GuestIpaAddress & 0xffffULL)) return STATUS_INVALID_ADDRESS;
  if (!AppleAgxG3PrivatePrepareObserved(&process->State->PrivatePool,
      process->Graph.ProcessId, view.CpuAddress, process->PrivateVa, render,
      manager, scene, diagnostic)) return STATUS_INSUFFICIENT_RESOURCES;
  KeMemoryBarrier();
  return STATUS_SUCCESS;
}

NTSTATUS AdmissionG3PreparePrivateStorage(ADMISSION_G3_PROCESS *process,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    APPLE_AGX_G3_PRIVATE_MANAGER *manager, APPLE_AGX_G3_PRIVATE_SCENE *scene) {
  return AdmissionG3PreparePrivateStorageObserved(process,render,manager,scene,NULL);
}

/* All private graph/pool operations below run at PASSIVE with State->Lock. */
static BOOLEAN AdmissionG3PrivateFreeExtent(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, APPLE_AGX_G3_PRIVATE_EXTENT *e) {
  if (!e->Generation) return TRUE;
  RtlZeroMemory((PUCHAR)view->CpuAddress + e->Offset, e->Bytes);
  KeMemoryBarrier();
  if (!AppleAgxG3PrivateFree(&p->State->PrivatePool,p->Graph.ProcessId,e))
    return FALSE;
  RtlZeroMemory(e,sizeof(*e));
  return TRUE;
}

static BOOLEAN AdmissionG3PrivateMapExtentObserved(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, const APPLE_AGX_G3_PRIVATE_EXTENT *e,
    BOOLEAN publish, UINT *failedOffset) {
  UINT offset;
  p->State->PrivateStats[publish ? ADMISSION_G3_PRIVATE_STAT_MAPPED_PAGES :
                                   ADMISSION_G3_PRIVATE_STAT_UNMAPPED_PAGES]+=e->Bytes/0x4000u;
  for (offset=0; offset<e->Bytes; offset+=0x4000u)
    if (!AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,p->PrivateLeafIpa,
            (e->VaOffset+offset)>>14,
            publish ? view->GuestIpaAddress+e->Offset+offset : 0ULL,
            publish != FALSE,AppleAgxGpuvaG3PrivateBacking)) {
      if (failedOffset) *failedOffset=offset;
      return FALSE;
    }
  return TRUE;
}

static BOOLEAN AdmissionG3PrivateMapExtent(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, const APPLE_AGX_G3_PRIVATE_EXTENT *e,
    BOOLEAN publish) {
  return AdmissionG3PrivateMapExtentObserved(p,view,e,publish,NULL);
}

static NTSTATUS AdmissionG3PrivateTables(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, UINT *predicate,
    APPLE_AGX_G3_PRIVATE_POOL_STATS *stats) {
  UINT i;
  if (!p->PrivateMiddleIpa) {
    for (i=0;i<2;++i) {
      if (!AppleAgxG3PrivateAllocate(&p->State->PrivatePool,p->Graph.ProcessId,
              0x10000u,&p->PrivateTables[i])) {
        *predicate=1u+i;
        AppleAgxG3PrivatePoolStats(&p->State->PrivatePool,p->Graph.ProcessId,stats);
        while (i) (void)AdmissionG3PrivateFreeExtent(p,view,&p->PrivateTables[--i]);
        return STATUS_INSUFFICIENT_RESOURCES;
      }
      RtlZeroMemory((PUCHAR)view->CpuAddress+p->PrivateTables[i].Offset,0x10000u);
    }
    KeMemoryBarrier();
    p->PrivateMiddleIpa=view->GuestIpaAddress+p->PrivateTables[0].Offset;
    p->PrivateLeafIpa=view->GuestIpaAddress+p->PrivateTables[1].Offset;
  }
  /* Cached empty private tables remain charged until process destruction.
   * Retry registration is idempotent; uncertain results quarantine the pool. */
  if (!AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,p->PrivateMiddleIpa,1u))
    *predicate=3u;
  else if (!AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,p->PrivateLeafIpa,2u))
    *predicate=4u;
  else if (!AdmissionG3AttachPrivateLeaves(p))
    *predicate=5u;
  /* The node a later root move needs to re-link the leaf (PrepareTableReuse). */
  else if (!AppleAgxGpuvaG3GraphReserveNode(&p->Graph))
    *predicate=6u;
  else return STATUS_SUCCESS;
  AppleAgxG3PrivatePoolStats(&p->State->PrivatePool,p->Graph.ProcessId,stats);
  return p->Graph.Uncertain ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_INSUFFICIENT_RESOURCES;
}

static BOOLEAN AdmissionG3PrivateReleaseScene(ADMISSION_G3_PROCESS *p,
    ADMISSION_G3_PRIVATE_SCENE *scene, ADMISSION_BACKEND_MEMORY_VIEW *view) {
  ADMISSION_G3_PRIVATE_SCENE **link=&p->PrivateScenes;
  UINT i;
  if (scene->Queued || scene->Submitting || scene->Quarantined || p->Graph.Uncertain ||
      p->Graph.JobInFlight) return FALSE;
  for (i=0;i<6;++i)
    if (!AdmissionG3PrivateMapExtent(p,view,&scene->Storage.Extents[i],FALSE)) {
      scene->Quarantined=1u;ADMISSION_G3_POISON(p,1u);return FALSE;
    }
  for (i=0;i<6;++i)
    if (!AdmissionG3PrivateFreeExtent(p,view,&scene->Storage.Extents[i])) {
      scene->Quarantined=1u;ADMISSION_G3_POISON(p,1u);return FALSE;
    }
  while (*link && *link!=scene) link=&(*link)->Next;
  if (*link) *link=scene->Next;
  ExFreePoolWithTag(scene,ADMISSION_POOL_TAG);
  ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_RELEASED];
  return TRUE;
}

/* EXP1085: every job mapped ~36 private-storage pages at ACQUIRE and revoked
 * them at release, one broker call per 16 KiB page (376 ms of hypervisor
 * traps per second under DWM), and ACQUIRE waited for the process's job in
 * flight because the broker refuses table changes then. A scene whose job
 * completed and was reported stays mapped instead and is reused, zeroed and
 * with a fresh generation, by an ACQUIRE of the same context and geometry.
 * Cancelled, unqueued and quarantined scenes keep the unmap/free path. */
static BOOLEAN AdmissionG3PrivateCacheScene(ADMISSION_G3_PROCESS *p,
    ADMISSION_G3_PRIVATE_SCENE *scene) {
  if (scene->Cached) return TRUE;
  if (!scene->ReleaseRequested || !scene->Reported || scene->Queued ||
      scene->Submitting || scene->Quarantined || p->Poisoned ||
      p->Graph.Uncertain || scene->Context->GpuvaG3Closing) return FALSE;
  /* EXP1087: always admit (most recent last); a miss trims the oldest. */
  scene->Cached=1u;
  scene->CachedAt=++p->PrivateCacheClock;
  return TRUE;
}

/* EXP1087: with no job in flight (an ACQUIRE miss), unmap the least recently
 * cached scenes beyond ADMISSION_G3_PRIVATE_SCENE_CACHE. */
static BOOLEAN AdmissionG3PrivateTrimCache(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view) {
  for (;;) {
    ADMISSION_G3_PRIVATE_SCENE *s,*oldest=NULL;
    UINT cached=0u;
    for (s=p->PrivateScenes;s;s=s->Next)
      if (s->Cached) {
        ++cached;
        if (!oldest || s->CachedAt<oldest->CachedAt) oldest=s;
      }
    if (cached<=ADMISSION_G3_PRIVATE_SCENE_CACHE) return TRUE;
    if (!AdmissionG3PrivateReleaseScene(p,oldest,view)) return FALSE;
    ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_TRIM];
  }
}

static ADMISSION_G3_PRIVATE_SCENE *AdmissionG3PrivateCachedScene(
    ADMISSION_G3_PROCESS *p, HANDLE context,
    const APPLE_AGX_G3_PRIVATE_REQUEST *q) {
  ADMISSION_G3_PRIVATE_SCENE *s;
  if (q->Operation!=APPLE_AGX_G3_PRIVATE_ACQUIRE || !p->PrivateManager.Generation)
    return NULL;
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Cached && (HANDLE)s->Context==context &&
        s->Geometry.WidthPx==q->Width && s->Geometry.HeightPx==q->Height &&
        s->Geometry.UtileWidthPx==q->UtileWidth &&
        s->Geometry.UtileHeightPx==q->UtileHeight) return s;
  return NULL;
}

/* Zero a cached scene's storage as construction does and renew every extent's
 * generation; no table or broker change. A failure quarantines the scene. */
static BOOLEAN AdmissionG3PrivateReuseScene(ADMISSION_G3_PROCESS *p,
    ADMISSION_G3_PRIVATE_SCENE *scene, ADMISSION_BACKEND_MEMORY_VIEW *view) {
  UINT i;
  for (i=0;i<6;++i) {
    APPLE_AGX_G3_PRIVATE_EXTENT *e=&scene->Storage.Extents[i];
    RtlZeroMemory((PUCHAR)view->CpuAddress+e->Offset,e->Bytes);
    if (!AppleAgxG3PrivateRenew(&p->State->PrivatePool,p->Graph.ProcessId,e)) {
      scene->Quarantined=1u;ADMISSION_G3_POISON(p,1u);return FALSE;
    }
  }
  KeMemoryBarrier();
  scene->Storage.Generation=scene->Storage.Extents[0].Generation;
  scene->Fence=scene->ResumeFence=0u;
  scene->Submitting=scene->Queued=scene->Started=scene->GpuDone=scene->Reported=0u;
  scene->ReleaseRequested=scene->Cached=0u;
  return TRUE;
}

/* Pool pressure (no job in flight): unmap and free the least recently cached
 * scene. FALSE when none is cached or its release failed. */
static BOOLEAN AdmissionG3PrivateEvictCached(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view) {
  ADMISSION_G3_PRIVATE_SCENE *s,*oldest=NULL;
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Cached && (!oldest || s->CachedAt<oldest->CachedAt)) oldest=s;
  if (!oldest || !AdmissionG3PrivateReleaseScene(p,oldest,view)) return FALSE;
  ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_PRESSURE];
  return TRUE;
}

/* The physical pool is adapter-wide. A miss must not fail merely because
 * another idle process retains released cache entries. Keep requester LRU
 * first (it can also relieve its VA/quota); foreign eviction only addresses
 * physical pressure, never a requester quota refusal. `failed` is the exact
 * allocation-failure snapshot, before Prepare rolls back temporary extents. */
static BOOLEAN AdmissionG3PrivateEvictPressure(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, UINT failedBytes,
    const APPLE_AGX_G3_PRIVATE_POOL_STATS *failed) {
  LIST_ENTRY *link;
  UINT units;
  if (!failedBytes || failedBytes>(8u<<20)) return FALSE; /* allocator hard limit */
  units=(failedBytes+APPLE_AGX_G3_PRIVATE_UNIT-1u)/APPLE_AGX_G3_PRIVATE_UNIT;
  if (AdmissionG3PrivateEvictCached(p,view)) return TRUE;
  if (p->Poisoned || p->Graph.Uncertain || !failed ||
      failed->OwnerUnits>APPLE_AGX_G3_PROCESS_UNITS ||
      units>APPLE_AGX_G3_PROCESS_UNITS-failed->OwnerUnits ||
      failed->LargestFreeUnits>=units) return FALSE;
  for (link=p->State->Processes.Flink;link!=&p->State->Processes;link=link->Flink) {
    ADMISSION_G3_PROCESS *owner=CONTAINING_RECORD(link,ADMISSION_G3_PROCESS,Link);
    ADMISSION_G3_PRIVATE_SCENE *s,*oldest=NULL;
    APPLE_AGX_G3_PRIVATE_POOL_STATS before,after;
    if (owner==p || owner->Poisoned || !owner->Graph.Created ||
        owner->Graph.Uncertain || owner->Graph.JobInFlight)
      continue;
    for (s=owner->PrivateScenes;s;s=s->Next)
      if (s->Cached && s->Reported && s->ReleaseRequested && !s->Queued &&
          !s->Submitting && !s->Quarantined &&
          (!oldest || s->CachedAt<oldest->CachedAt)) oldest=s;
    if (!oldest) continue;
    AppleAgxG3PrivatePoolStats(&p->State->PrivatePool,p->Graph.ProcessId,&before);
    /* This retains the victim's graph/root/owner through leaf invalidation,
     * backing revoke, zeroing and generation-checked free. Failure quarantines
     * that owner in the existing release path; never try a different victim. */
    if (!AdmissionG3PrivateReleaseScene(owner,oldest,view)) return FALSE;
    AppleAgxG3PrivatePoolStats(&p->State->PrivatePool,p->Graph.ProcessId,&after);
    ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_PRESSURE];
    p->State->PrivatePressure[0]=1u;
    ++p->State->PrivatePressure[1];
    p->State->PrivatePressure[2]=p->OsProcessId;
    p->State->PrivatePressure[3]=owner->OsProcessId;
    p->State->PrivatePressure[4]=before.GlobalUnits;
    p->State->PrivatePressure[5]=after.GlobalUnits;
    p->State->PrivatePressure[6]=before.GlobalUnits-after.GlobalUnits;
    p->State->PrivatePressure[7]=before.OwnerUnits;
    p->State->PrivatePressure[8]=failedBytes;
    p->State->PrivatePressure[9]=(ULONG)owner->Graph.RootIpa;
    p->State->PrivatePressure[10]=(ULONG)(owner->Graph.RootIpa>>32);
    p->State->PrivatePressure[11]=after.LargestFreeUnits;
    return TRUE;
  }
  return FALSE;
}

/* Interrupt/DPC owners hold SchedulerLock and publish only a cancellation
 * marker. The matching scene reference pins Context until PASSIVE reaping. */
VOID AdmissionGpuvaG3PrivateCancel(ADMISSION_RENDER_CONTEXT *context,
    ULONG fence, BOOLEAN uncertain) {
  volatile LONG *cancel, *caller;
  if (!context || !fence) return;
  /* Phase 5b: the marker goes to the slot that holds this fence. */
  if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)==fence) {
    cancel=&context->GpuvaG3CancelFence;
    caller=&context->GpuvaG3CancelCaller;
  } else if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0)==fence) {
    cancel=&context->GpuvaG3CancelFence2;
    caller=&context->GpuvaG3CancelCaller2;
  } else return;
  if (uncertain) InterlockedExchange(&context->GpuvaG3CancelUncertain,1);
  InterlockedExchange(caller,(LONG)((ULONG_PTR)_ReturnAddress() -
      (ULONG_PTR)AdmissionGpuvaG3PrivateCancel));
  InterlockedExchange(cancel,(LONG)fence);
}

/* Queued preemption transfers ownership back to VidSch, which can resubmit
 * this same scene with a new fence. Keep the queued hold until that transfer
 * or context teardown; an ordinary cancellation has different lifetime rules. */
VOID AdmissionGpuvaG3PrivatePreempt(ADMISSION_RENDER_CONTEXT *context,
    ULONG fence) {
  if (!context || !fence ||
      InterlockedCompareExchange(&context->GpuvaG3CancelUncertain,0,0)) return;
  if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)==fence &&
      !InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0))
    InterlockedExchange(&context->GpuvaG3PreemptFence,(LONG)fence);
  else if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0)==fence &&
      !InterlockedCompareExchange(&context->GpuvaG3CancelFence2,0,0))
    InterlockedExchange(&context->GpuvaG3PreemptFence2,(LONG)fence);
}

/* Phase 5b: free the private slot that holds fence (PASSIVE, process lock
 * held). Cancel markers stay until the slot is reassigned at submit. */
static VOID AdmissionG3PrivateSlotClear(ADMISSION_RENDER_CONTEXT *context,
    ULONG fence) {
  if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)==fence) {
    InterlockedExchange(&context->GpuvaG3PreemptFence,0);
    InterlockedExchange(&context->GpuvaG3PrivateFence,0);
  } else if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0)==fence) {
    InterlockedExchange(&context->GpuvaG3PreemptFence2,0);
    InterlockedExchange(&context->GpuvaG3PrivateFence2,0);
  }
}

/* Phase 5b: the queued private job with fence was preempted and awaits
 * resubmission (its slot carries the preempt marker). */
static BOOLEAN AdmissionG3PrivateSlotPreempted(ADMISSION_RENDER_CONTEXT *context,
    ULONG fence) {
  return fence &&
      (((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)==fence &&
        (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence,0,0)==fence) ||
       ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0)==fence &&
        (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence2,0,0)==fence))
      ? TRUE : FALSE;
}

/* Phase 5b: fence carries a cancel marker in its slot. */
static BOOLEAN AdmissionG3PrivateSlotCancelled(ADMISSION_RENDER_CONTEXT *context,
    ULONG fence) {
  return fence &&
      ((ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0)==fence ||
       (ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelFence2,0,0)==fence)
      ? TRUE : FALSE;
}

static BOOLEAN AdmissionG3PrivateReap(ADMISSION_G3_PROCESS *p) {
  ADMISSION_G3_PRIVATE_SCENE *s,*next;
  ADMISSION_BACKEND_MEMORY_VIEW view;
  if (!p->PrivateScenes) return TRUE;
  if (!NT_SUCCESS(AdmissionMemoryRuntimePrivateView(p->State->Adapter,&view))) return FALSE;
  for (s=p->PrivateScenes;s;s=next) {
    next=s->Next;
    if (s->Submitting) continue; /* Submit retains a local pointer through rollback. */
    if (s->Queued && AdmissionG3PrivateSlotCancelled(s->Context,s->Fence)) {
      if (s->Started || InterlockedCompareExchange(
              &s->Context->GpuvaG3CancelUncertain,0,0)) {
        s->Quarantined=1u;ADMISSION_G3_POISON(p,1u);
      } else {
        s->Queued=0u;
        AdmissionG3PrivateSlotClear(s->Context,s->Fence);
      }
    }
    if (s->Quarantined || p->Graph.Uncertain) continue;
    if (s->ReleaseRequested && !s->Queued && !s->Cached && AdmissionG3PrivateCacheScene(p,s))
      ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_CACHED_REAP];
    if (s->ReleaseRequested && !s->Queued && !AdmissionG3PrivateCacheScene(p,s) &&
        !p->Graph.JobInFlight &&
        !AdmissionG3PrivateReleaseScene(p,s,&view)) return FALSE;
  }
  return !p->Poisoned && !p->Graph.Uncertain;
}

BOOLEAN AdmissionGpuvaG3PrivateReset(ADMISSION_CONTEXT *adapter) {
  ADMISSION_G3_STATE *state;
  PLIST_ENTRY link;
  BOOLEAN safe=TRUE;
  if (!adapter || KeGetCurrentIrql()!=PASSIVE_LEVEL) return FALSE;
  state=(ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (!state) return TRUE;
  ExAcquireFastMutex(&state->Lock);
  for (link=state->Processes.Flink;link!=&state->Processes;link=link->Flink) {
    ADMISSION_G3_PROCESS *p=CONTAINING_RECORD(link,ADMISSION_G3_PROCESS,Link);
    ADMISSION_G3_PRIVATE_SCENE *s;
    for (s=p->PrivateScenes;s;s=s->Next)
      if (s->Queued && s->Started) {
        s->Quarantined=1u;ADMISSION_G3_POISON(p,1u);safe=FALSE;
      }
    /* V1: after a reset no idle process keeps a slot (the baseline state). */
    if (!p->Graph.Uncertain && !AppleAgxGpuvaG3GraphReleaseRetained(&p->Graph)) {
      ADMISSION_G3_POISON(p,1u);safe=FALSE;
    }
  }
  ExReleaseFastMutex(&state->Lock);
  return safe; /* No GPU stop/TLB proof exists for an active private reset. */
}

BOOLEAN AdmissionGpuvaG3PrivateReported(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence) {
  ADMISSION_G3_PROCESS *p;
  ADMISSION_G3_PRIVATE_SCENE *s;
  BOOLEAN ok=FALSE;
  ULONG poison_site;
  ULONG poison_status;
  if (!context || !context->GpuvaG3Process) return TRUE;
  if (!adapter || !fence || KeGetCurrentIrql()!=PASSIVE_LEVEL) return FALSE;
  p=(ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  if (p->State->Adapter!=adapter) return FALSE;
  ExAcquireFastMutex(&p->State->Lock);
  /* EXP997: a reclaim failure (poisoned process, quarantined scene) keeps the
   * scene's storage but must not withhold a fence whose GPU work finished.
   * EXP996: the stuck completion faulted the shared scheduler and VidSch
   * bugchecked 0x119 on the next paging submission; PrivateCompletionFence
   * also blocked BeginJob for every process. A poisoned process is refused
   * at its next submission instead. */
  (void)AdmissionG3PrivateReap(p);
  if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)!=fence &&
      (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0)!=fence) {
    ok=TRUE;goto Done; /* Legacy job, or an already reported exact transaction. */
  }
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Context==context && s->Fence==fence && s->Queued) break;
  if (!s || !s->Started || !s->GpuDone) goto Done;
  s->Reported=1u;s->Queued=0u;
  if (p->State->PrivateCompletionFence==fence) p->State->PrivateCompletionFence=0u;
  if ((ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)==fence)
    InterlockedExchange(&context->GpuvaG3PrivateFence,0);
  else
    InterlockedExchange(&context->GpuvaG3PrivateFence2,0);
  /* Failed reclaim must quarantine, but cannot undo a fence already reported
   * to Windows. The retained process record prevents reuse and destruction. */
  (void)AdmissionG3PrivateReap(p);
  ok=TRUE;
Done:
  poison_site=p->Poisoned ? p->PoisonSite : 0u;
  poison_status=p->PoisonBrokerStatus;
  ExReleaseFastMutex(&p->State->Lock);
#ifdef _MSC_VER
  if (poison_site) AdmissionRecordG3Poison(adapter, poison_site, p->OsProcessId,
                                           poison_status);
#endif
  return ok;
}

BOOLEAN AdmissionGpuvaG3PrivateRetireContext(ADMISSION_RENDER_CONTEXT *context) {
  ADMISSION_G3_PROCESS *p;
  ADMISSION_G3_PRIVATE_SCENE *s,*next;
  ADMISSION_RENDER_CONTEXT *c;
  ADMISSION_BACKEND_MEMORY_VIEW view;
  BOOLEAN ok=FALSE,manager_used=FALSE;
  UINT i;
  if (!context || !context->GpuvaG3Process) return TRUE;
  if (KeGetCurrentIrql()!=PASSIVE_LEVEL) return FALSE;
  p=(ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  ExAcquireFastMutex(&p->State->Lock);
  context->GpuvaG3Closing=TRUE;
  if (!AdmissionG3PrivateReap(p)) goto Done;
  for (s=p->PrivateScenes;s;s=s->Next) {
    if (s->Context!=context) continue;
    if (s->Queued && !s->Submitting && !s->Started && !s->Quarantined &&
        !InterlockedCompareExchange(&context->GpuvaG3CancelUncertain,0,0) &&
        AdmissionG3PrivateSlotPreempted(context,s->Fence)) {
      s->Queued=0u;
      AdmissionG3PrivateSlotClear(context,s->Fence);
    }
    if (s->Queued || s->Quarantined) goto Done;
  }
  if (!p->PrivateManager.Generation) {ok=TRUE;goto Done;}
  if (p->Graph.JobInFlight ||
      !NT_SUCCESS(AdmissionMemoryRuntimePrivateView(p->State->Adapter,&view))) goto Done;
  for (s=p->PrivateScenes;s;s=next) {
    next=s->Next;
    if (s->Context==context && !AdmissionG3PrivateReleaseScene(p,s,&view)) goto Done;
  }
  context->GpuvaG3PrivateManagerGeneration=0ULL;
  for (c=p->Contexts;c;c=c->GpuvaG3NextContext)
    if (c!=context && c->GpuvaG3PrivateManagerGeneration==p->PrivateManager.Generation)
      manager_used=TRUE;
  if (!manager_used && !p->PrivateScenes) {
    UINT extents=AppleAgxG3PrivateManagerExtentCount(&p->PrivateManager);
    /* rc80: the TVB window blocks end with the manager (graph idle above). */
    if (!AdmissionG3TvbRelease(p)) goto Done;
    for (i=0;i<extents;++i)
      if (!AdmissionG3PrivateMapExtent(p,&view,
              AppleAgxG3PrivateManagerExtent(&p->PrivateManager,i),FALSE)) {
        ADMISSION_G3_POISON(p,1u);goto Done;
      }
    for (i=0;i<extents;++i)
      if (!AdmissionG3PrivateFreeExtent(p,&view,
              AppleAgxG3PrivateManagerExtent(&p->PrivateManager,i))) {
        ADMISSION_G3_POISON(p,1u);goto Done;
      }
    RtlZeroMemory(&p->PrivateManager,sizeof(p->PrivateManager));
    RtlZeroMemory(&p->FirmwareManager,sizeof(p->FirmwareManager));
  }
  ok=TRUE;
Done:
  ExReleaseFastMutex(&p->State->Lock);
  return ok;
}

static BOOLEAN AdmissionG3PrivateDestroyStorage(ADMISSION_G3_PROCESS *p) {
  ADMISSION_BACKEND_MEMORY_VIEW view;
  UINT i;
  if (p->Graph.Created || p->Graph.Uncertain) return FALSE;
  if (!p->PrivateMiddleIpa) return TRUE;
  if (!NT_SUCCESS(AdmissionMemoryRuntimePrivateView(p->State->Adapter,&view))) return FALSE;
  /* GraphDestroy acknowledged leaf unlink, TLB ordering and every revoke.
   * Only now can any remaining owner extent return to the adapter pool. */
  for (i=0;i<APPLE_AGX_G3_PRIVATE_UNITS;++i) {
    APPLE_AGX_G3_PRIVATE_BLOCK *block=&p->State->PrivatePool.Blocks[i];
    if (block->Owner==p->Graph.ProcessId) {
      APPLE_AGX_G3_PRIVATE_EXTENT e={block->Generation,
          block->First*APPLE_AGX_G3_PRIVATE_UNIT,block->Count*APPLE_AGX_G3_PRIVATE_UNIT,
          block->VaFirst*APPLE_AGX_G3_PRIVATE_UNIT};
      if (!AdmissionG3PrivateFreeExtent(p,&view,&e)) return FALSE;
    }
  }
  while (p->PrivateScenes) {
    ADMISSION_G3_PRIVATE_SCENE *scene=p->PrivateScenes;
    p->PrivateScenes=scene->Next;ExFreePoolWithTag(scene,ADMISSION_POOL_TAG);
  }
  /* rc80: the same proof covers the TVB window granules. A granule that
   * could not be zeroed stays held, and so does the lease array. */
  if (AdmissionG3TvbReturnLeases(p) && p->TvbLeases) {
    ExFreePoolWithTag(p->TvbLeases,ADMISSION_POOL_TAG);
    p->TvbLeases=NULL;
  }
  return TRUE;
}

/* Resolve only current VidMm provenance. ResidentPtes covers both logical
 * page formats; LogicalPtes deliberately excludes the 64K CPU-envelope case. */
static ULONG AdmissionG3Fnv(ULONG hash, const UCHAR *data, SIZE_T bytes) {
  SIZE_T i;
  for (i=0;i<bytes;++i) hash=(hash^data[i])*16777619u;
  return hash;
}

static BOOLEAN AdmissionG3UscVa(ULONGLONG va, ULONGLONG bytes) {
  return va>=APPLE_AGX_G4_USC_EXECUTION_BASE &&
      bytes<=APPLE_AGX_G4_USC_WINDOW_BYTES &&
      va-APPLE_AGX_G4_USC_EXECUTION_BASE<=APPLE_AGX_G4_USC_WINDOW_BYTES-bytes;
}

/* R162 diagnostic: remember the bytes a USC-window upload wrote. */
static VOID AdmissionG3TraceUpload(ADMISSION_G3_STATE *state,
    ADMISSION_G3_PROCESS *p, ULONGLONG va, ULONG bytes, const UCHAR *data) {
  ADMISSION_G3_UPLOAD_TRACE *t;
  UINT i;
  if (!AdmissionG3UscVa(va,bytes)) return;
  for (i=0;i<ADMISSION_G3_UPLOAD_TRACE_COUNT;++i) {
    t=&state->UploadTrace[i];
    if (t->Bytes && t->ProcessId==p->Graph.ProcessId &&
        t->Va<va+bytes && va<t->Va+t->Bytes) RtlZeroMemory(t,sizeof(*t));
  }
  t=&state->UploadTrace[state->UploadTraceNext++ % ADMISSION_G3_UPLOAD_TRACE_COUNT];
  t->ProcessId=p->Graph.ProcessId;t->Va=va;t->Bytes=bytes;
  t->Hash=AdmissionG3Fnv(2166136261u,data,bytes);t->GpuHash=0u;t->Checks=0u;
}

#if ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB
/* Re-hash each traced upload through the published graph (the GPU view). */
static VOID AdmissionG3VerifyUploads(ADMISSION_CONTEXT *adapter,
    ADMISSION_G3_STATE *state, ADMISSION_G3_PROCESS *p) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  UINT i;
  if (!NT_SUCCESS(AdmissionMemoryRuntimeLocalView(adapter,&view)) ||
      !view.CpuAddress) return;
  for (i=0;i<ADMISSION_G3_UPLOAD_TRACE_COUNT;++i) {
    ADMISSION_G3_UPLOAD_TRACE *t=&state->UploadTrace[i];
    ULONG hash=2166136261u;
    ULONGLONG offset;
    BOOLEAN mapped=TRUE;
    if (!t->Bytes || t->ProcessId!=p->Graph.ProcessId) continue;
    for (offset=0;offset<t->Bytes;) {
      ULONGLONG va=t->Va+offset, ipa, part=0x4000ULL-(va&0x3fffULL);
      if (part>t->Bytes-offset) part=t->Bytes-offset;
      if (!AppleAgxGpuvaG3GraphTranslateVa(&p->Graph,va,&ipa) ||
          ipa<view.GuestIpaAddress || view.Bytes<part ||
          ipa-view.GuestIpaAddress>view.Bytes-part) {mapped=FALSE;break;}
      hash=AdmissionG3Fnv(hash,(const UCHAR *)view.CpuAddress+
          (SIZE_T)(ipa-view.GuestIpaAddress),(SIZE_T)part);
      offset+=part;
    }
    ++state->UploadVerifyChecks;++t->Checks;
    if (!mapped) {++state->UploadVerifyUnmapped;continue;}
    t->GpuHash=hash;
    if (hash!=t->Hash) {
      if (!state->UploadVerifyMismatch) state->UploadFirstMismatch=*t;
      ++state->UploadVerifyMismatch;
    }
  }
}
#endif

static const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *AdmissionG3CopyPte(
    const ADMISSION_G3_PROCESS *p, ULONGLONG va) {
  ADMISSION_G3_TABLE_SHADOW *shadow;
  ULONGLONG leaf;
  ULONG bucket;
  /* The graph helper only reads slot state; its API predates const callers. */
  if (!p || !AppleAgxGpuvaG3GraphLeafTableIpa(
          (APPLE_AGX_GPUVA_G3_GRAPH *)&p->Graph,va,&leaf))
    return NULL;
  bucket=(ULONG)((leaf>>14)^(leaf>>22)^(leaf>>30))&255u;
  for(shadow=p->TableShadowBrokerBuckets[bucket];shadow;shadow=shadow->NextBroker) {
#ifdef ADMISSION_G3_COPY_PTE_VISIT
    ADMISSION_G3_COPY_PTE_VISIT();
#endif
    if(shadow->BrokerIpa==leaf) break;
  }
  if(!shadow || !shadow->ResidentPtes) return NULL;
  return &shadow->ResidentPtes[(UINT)((va>>12)&8191u)];
}

/* Every native mutation uses the graph callback, including private helpers,
 * rollback and table retirement. Private-only leaves have no VidMm shadow
 * and cannot invalidate an authenticated user allocation. */
static void AdmissionG3NativeInvalidate(void *opaque, ULONGLONG table, UINT index) {
  ADMISSION_G3_PROCESS *p = (ADMISSION_G3_PROCESS *)opaque;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  ULONG bucket;
  if (!table) { AppleAgxG3NativeCacheInvalidateAll(&p->NativeReady); return; }
  bucket = (ULONG)((table >> 14) ^ (table >> 22) ^ (table >> 30)) & 255u;
  for (shadow = p->TableShadowBrokerBuckets[bucket]; shadow; shadow = shadow->NextBroker)
    if (shadow->BrokerIpa == table) {
      AdmissionG3NativeInvalidatePtes(p, shadow->ResidentPtes, index * 4u, 4u);
      break;
    }
}

/* Allocation addresses may be reused after destroy. Drop identity keys from
 * every process before freeing; never dereference an old cache/PTE key. */
void AdmissionGpuvaG3InvalidateAllocation(ADMISSION_CONTEXT *adapter, HANDLE allocation) {
  ADMISSION_G3_STATE *state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  PLIST_ENTRY link;
  if (!state) return;
  ExAcquireFastMutex(&state->Lock);
  for (link = state->Processes.Flink; link != &state->Processes; link = link->Flink) {
    ADMISSION_G3_PROCESS *p = CONTAINING_RECORD(link, ADMISSION_G3_PROCESS, Link);
    AppleAgxG3NativeCacheInvalidate(&p->NativeReady, (ULONGLONG)(ULONG_PTR)allocation);
  }
  ExReleaseFastMutex(&state->Lock);
}

#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
typedef struct _ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT {
  ULONG Version, Bytes, OsProcessId, TablesScanned;
  ULONG GroupsScanned, SystemGroups, IncompleteSystemGroups, Truncated;
  ULONG PartialGroups, UnalignedGroups, DiscontiguousGroups, MixedGroups;
  ULONG FirstIndex, FirstFlags[4];
  ULONGLONG GraphProcessId, FirstTableIpa, FirstAllocation;
  ULONGLONG FirstGuestIpa[4];
} ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT;

static volatile LONG gDwmSystemLeafSnapshotClaimed;

static VOID AdmissionG3SnapshotDwmSystemLeaves(
    const ADMISSION_G3_PROCESS *Process, ULONG OsProcessId,
    ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT *Receipt) {
  const ADMISSION_G3_TABLE_SHADOW *shadow;
  UINT group, part;
  RtlZeroMemory(Receipt, sizeof(*Receipt));
  Receipt->Version = 1u;
  Receipt->Bytes = sizeof(*Receipt);
  Receipt->OsProcessId = OsProcessId;
  Receipt->GraphProcessId = Process->Graph.ProcessId;
  Receipt->FirstIndex = MAXULONG;
  for (shadow = Process->TableShadows; shadow != NULL;
       shadow = shadow->Next) {
    if (shadow->LogicalPtes == NULL)
      continue;
    if (Receipt->TablesScanned == 16u) {
      Receipt->Truncated = 1u;
      break;
    }
    ++Receipt->TablesScanned;
    for (group = 0u; group < 2048u; ++group) {
      const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte =
          &shadow->LogicalPtes[group * 4u];
      ULONGLONG base = pte[0].GuestIpa;
      BOOLEAN anySystem = FALSE, full = TRUE, same = TRUE;
      BOOLEAN aligned, contiguous = base <= MAXULONGLONG - 0x3000ULL;
      ++Receipt->GroupsScanned;
      for (part = 0u; part < 4u; ++part) {
        if ((pte[part].Flags & APPLE_AGX_GPUVA_G3_VALID) != 0u &&
            pte[part].SegmentId == 0u)
          anySystem = TRUE;
        if ((pte[part].Flags & APPLE_AGX_GPUVA_G3_VALID) == 0u)
          full = FALSE;
        if (pte[part].SegmentId != 0u ||
            pte[part].Flags != pte[0].Flags)
          same = FALSE;
        if (pte[part].GuestIpa != base + (ULONGLONG)part * 0x1000ULL)
          contiguous = FALSE;
      }
      if (!anySystem)
        continue;
      ++Receipt->SystemGroups;
      aligned = (base & 0x3fffULL) == 0ULL;
      if (full && same && aligned && contiguous)
        continue;
      ++Receipt->IncompleteSystemGroups;
      if (!full) ++Receipt->PartialGroups;
      if (!aligned) ++Receipt->UnalignedGroups;
      if (!contiguous) ++Receipt->DiscontiguousGroups;
      if (!same) ++Receipt->MixedGroups;
      if (Receipt->FirstIndex == MAXULONG) {
        Receipt->FirstIndex = group;
        Receipt->FirstTableIpa = shadow->BrokerIpa;
        for (part = 0u; part < 4u; ++part) {
          Receipt->FirstFlags[part] = pte[part].Flags;
          Receipt->FirstGuestIpa[part] = pte[part].GuestIpa;
          if (Receipt->FirstAllocation == 0ULL &&
              (pte[part].Flags & APPLE_AGX_GPUVA_G3_VALID) != 0u)
            Receipt->FirstAllocation = pte[part].Allocation;
        }
      }
    }
  }
}

static VOID AdmissionG3WriteDwmSystemLeaves(
    ADMISSION_CONTEXT *Adapter,
    const ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT *Receipt) {
  UNICODE_STRING name;
  HANDLE key = NULL;
  if (Adapter->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Adapter->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  RtlInitUnicodeString(&name, L"Wom1G3DwmSystemLeafSnapshot");
  (void)ZwSetValueKey(key, &name, 0, REG_BINARY, (PVOID)Receipt,
                      sizeof(*Receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

_Use_decl_annotations_ NTSTATUS AdmissionGpuvaG3FrameArmEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  ADMISSION_DWM_FRAME_ARM request;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_DWM_SOURCE_MAP_RECEIPT map;
  ADMISSION_G3_DWM_SYSTEM_LEAF_SNAPSHOT systemLeaves;
  BOOLEAN haveSystemLeaves = FALSE;
  BOOLEAN valid = FALSE;
  /* Software-only metadata; our process lock and nonblocking receipt claim
   * provide the needed serialization. Accept the old synchronized caller,
   * but reject every unrelated escape flag. No MMIO, DMA or UAT operation. */
  if (adapter == NULL || args == NULL || !adapter->Started ||
      KeGetCurrentIrql() != PASSIVE_LEVEL || args->Flags.Value > 1u ||
      args->pPrivateDriverData == NULL ||
      args->PrivateDriverDataSize != sizeof(request))
    return STATUS_INVALID_PARAMETER;
  RtlCopyMemory(&request, args->pPrivateDriverData, sizeof(request));
  if (request.Magic != ADMISSION_DWM_FRAME_ARM_MAGIC ||
      request.Version != ADMISSION_DWM_FRAME_VERSION ||
      request.Bytes != sizeof(request) ||
      request.OsProcessId != HandleToULong(PsGetCurrentProcessId()) ||
      request.Allocation == 0ULL ||
      request.CanonicalGpuVa >= (1ULL << 39) ||
      (request.CanonicalGpuVa & 0xffffULL) != 0ULL)
    return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
  RtlZeroMemory(&map, sizeof(map));
  map.Version = ADMISSION_DWM_SOURCE_MAP_VERSION;
  map.Bytes = (ULONG)sizeof(map);
  map.OsProcessId = request.OsProcessId;
  map.Allocation = request.Allocation;
  map.CanonicalGpuVa = request.CanonicalGpuVa;
  ExAcquireFastMutex(&state->Lock);
  process = AdmissionGpuvaG3FindProcess(state, args->hKmdProcessHandle);
  for (context = process ? process->Contexts : NULL;
       context != NULL && (HANDLE)context != args->hContext;
       context = context->GpuvaG3NextContext) {}
  if (context != NULL && context->Win32Transport &&
      context->Object.Device != NULL &&
      context->Object.Device->Adapter == &adapter->ObjectAdapter &&
      (HANDLE)CONTAINING_RECORD(context->Object.Device, ADMISSION_DEVICE, Object) ==
          args->hDevice) {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte =
        request.CanonicalGpuVa != 0ULL
            ? AdmissionG3CopyPte(process, request.CanonicalGpuVa) : NULL;
    map.GraphProcessId = process->Graph.ProcessId;
    map.RootIpa = context->GpuvaG3RootIpa;
    map.MappingGeneration = context->GpuvaG3MappingGeneration;
    if (pte != NULL) {
      map.PteFound = 1u;
      map.PteAllocation = pte->Allocation;
      map.PteAllocationOffset = pte->AllocationOffset;
      map.PteGuestIpa = pte->GuestIpa;
      map.SegmentId = pte->SegmentId;
      map.PteFlags = pte->Flags;
      if ((pte->Flags & APPLE_AGX_GPUVA_G3_VALID) != 0u &&
          pte->GuestIpa <= MAXULONGLONG - (request.CanonicalGpuVa & 0xfffULL))
        map.ResolvedGuestIpa =
            pte->GuestIpa + (request.CanonicalGpuVa & 0xfffULL);
    }
    valid = AdmissionDwmFrameArmWindows(adapter, context,
        request.OsProcessId, process->Graph.ProcessId,
        request.Allocation, request.CanonicalGpuVa);
    if (valid &&
        InterlockedCompareExchange(&gDwmSystemLeafSnapshotClaimed, 1, 0) == 0) {
      AdmissionG3SnapshotDwmSystemLeaves(process, request.OsProcessId,
                                         &systemLeaves);
      haveSystemLeaves = TRUE;
    }
  }
  ExReleaseFastMutex(&state->Lock);
  if (haveSystemLeaves)
    AdmissionG3WriteDwmSystemLeaves(adapter, &systemLeaves);
  if (valid) {
    ULONG ordinal = (ULONG)InterlockedIncrement(
        &adapter->DwmSourceMapRecordCount) - 1u;
    map.Ordinal = ordinal;
    map.SourceReceiptState = (ULONG)InterlockedCompareExchange(
        &adapter->SourceAddressReceiptState, 0, 0);
    if (map.SourceReceiptState >= 2u) {
      map.SelectedHostPhysicalAddress =
          adapter->SourceAddressReceipt.SelectedHostPhysicalAddress;
      map.SelectedPrimaryAddress =
          (ULONGLONG)adapter->SourceAddressReceipt.PrimaryAddress;
      map.SelectedSurfaceBytes =
          (ULONGLONG)adapter->SourceAddressReceipt.Stride *
          adapter->SourceAddressReceipt.Height;
    }
    if (map.PteFound && map.SegmentId == ADMISSION_MEMORY_LOCAL_SEGMENT &&
        (map.PteFlags & (APPLE_AGX_GPUVA_G3_VALID |
                         APPLE_AGX_GPUVA_G3_WRITE)) ==
            (APPLE_AGX_GPUVA_G3_VALID | APPLE_AGX_GPUVA_G3_WRITE) &&
        map.SelectedHostPhysicalAddress != 0ULL &&
        map.SelectedSurfaceBytes != 0ULL &&
        map.ResolvedGuestIpa >= map.SelectedHostPhysicalAddress &&
        map.ResolvedGuestIpa - map.SelectedHostPhysicalAddress <
            map.SelectedSurfaceBytes) {
      map.InSelectedRange = 1u;
      map.InSelectedRangeCount = (ULONG)InterlockedIncrement(
          &adapter->DwmSourceMapInRangeCount);
    } else {
      map.InSelectedRangeCount = (ULONG)InterlockedCompareExchange(
          &adapter->DwmSourceMapInRangeCount, 0, 0);
    }
    /* EXP1109: DWM arms ~5 times per frame and each receipt write is a
     * registry open/set/close on its thread (~85 us, EXP1108). Keep the first
     * 16 ordinals and every 256th; the probe entry above is always current. */
    if (ordinal < 16u || (ordinal & 255u) == 0u)
      AdmissionRecordDwmSourceMap(adapter->PhysicalDeviceObject, &map, ordinal);
  }
  /* EXP1052 receipt-only: publish the paging profile from this PASSIVE escape
   * (never from the paging path), at most every two seconds. */
  AdmissionRecordPagingProfile(adapter, &state->Client, state->PrivateStats);
  return valid ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}
#endif

/* Caller holds the QUERY mutex when process/context are supplied. Claim and
 * fixed adapter storage precede unlock; only the winning caller persists it. */
static BOOLEAN AdmissionG3CaptureCopyQueryFailure(ADMISSION_CONTEXT *adapter,
    const ADMISSION_G3_PROCESS *p, const ADMISSION_RENDER_CONTEXT *context,
    const APPLE_AGX_G3_COPY_REQUEST *q, ULONG predicate, NTSTATUS status,
    BOOLEAN locked, ULONGLONG first, ULONGLONG length,
    const APPLE_AGX_GPUVA_G3_WALK_FAILURE *walk,
    const ADMISSION_ALLOCATION_HANDLE *allocation) {
  APPLE_AGX_G3_COPY_QUERY_RECEIPT *r;
  if (!adapter || !predicate || NT_SUCCESS(status) ||
      InterlockedCompareExchange(&adapter->G3CopyQueryFailureClaim, 1, 0) != 0)
    return FALSE;
  r = &adapter->G3CopyQueryFailure;
  RtlZeroMemory(r, sizeof(*r));
  r->Version = 3u; r->Bytes = sizeof(*r);
  r->Predicate = predicate; r->Status = (ULONG)status;
  r->MissingLevel = r->MissingIndex = MAXULONG;
  if (locked) r->Flags |= AppleAgxG3QueryLocked;
  if (q) {
    r->Flags |= AppleAgxG3QueryRequest;
    r->QueryVa = q->GpuVa;
    r->RequestAllocationHandle = q->Allocation;
  }
  /* The acquisition reference still protects the allocation. Guards 34-41
   * established its owner, identity, class and GPU-local non-CPU-visible type.
   * No object is dereferenced after handle release or without the mutex. */
  if (locked && allocation && predicate >= 42u) {
    r->Flags |= AppleAgxG3QueryCanonicalAllocationAvailable;
    r->CanonicalAllocationIdentity = (ULONGLONG)(ULONG_PTR)allocation;
    r->CanonicalAllocationBytes = allocation->Object.Description.Size;
  }
  if (p) {
    r->Flags |= AppleAgxG3QueryProcess;
    r->GraphRootIpa = p->Graph.RootIpa;
    r->BootstrapIpa = p->BootstrapIpa;
    if (p->Graph.RootIpa == p->BootstrapIpa)
      r->Flags |= AppleAgxG3QueryRootIsBootstrap;
    r->ProcessLastSetRootIpa = p->LastSetRootIpa;
    r->ProcessSetRootCount = p->SetRootCount;
    r->ProcessGeneration = p->Graph.ProcessGeneration;
    r->MappingGeneration = p->Graph.MappingGeneration;
    r->ProcessId = p->Graph.ProcessId;
  }
  if (context) {
    r->Flags |= AppleAgxG3QueryContext;
    r->ContextLastSetRootIpa = context->GpuvaG3LastSetRootIpa;
    r->ContextSetRootCount = context->GpuvaG3SetRootCount;
    r->ContextRootIpa = context->GpuvaG3RootIpa;
    r->ContextToken = (ULONGLONG)(ULONG_PTR)context;
  }
  /* Earlier guards have not established a bounded allocation range. */
  if (predicate >= 53u && locked) {
    r->Flags |= AppleAgxG3QueryRange;
    r->QueryVa = first; r->QueryBytes = length;
    if (walk) {
      r->MissingLevel = walk->Level; r->MissingIndex = walk->Index;
      r->MissingReason = walk->Reason; r->ComponentReason = walk->ComponentReason;
      r->MissingVa = walk->Va;
      /* Incomplete logical groups remove the native leaf node. Distinguish
       * that state from an entirely unmapped group using existing provenance,
       * without publishing, allocating or changing the failed predicate. */
      if (p && walk->Level == 2u &&
          walk->ComponentReason == AppleAgxG3WalkLeafAbsent) {
        const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *group =
            AdmissionG3CopyPte(p, walk->Va & ~0x3fffULL);
        UINT i;
        if (group) r->Flags |= AppleAgxG3QueryResidentGroupAvailable;
        for (i=0u; group && i<4u; ++i) {
          if (!(group[i].Flags & APPLE_AGX_GPUVA_G3_VALID)) continue;
          r->ComponentReason = AppleAgxG3WalkLeafNotPublished;
          if (r->MissingReason != AppleAgxG3WalkTailShort)
            r->MissingReason = AppleAgxG3WalkLeafNotPublished;
          break;
        }
      }
    }
  }
  /* QUERY's existing walk requests read access (write=FALSE). */
  r->Write = 0u;
  adapter->G3CopyQueryFailurePredicate = predicate;
  adapter->G3CopyQueryFailureStatus = (ULONG)status;
  KeMemoryBarrier();
  InterlockedExchange(&adapter->G3CopyQueryFailureClaim, 2);
  return TRUE;
}

/* Read-only metadata query, after the UMD's residency fence and while its
 * submission holds exclude trim. Unlike T12, inspect only the named canonical
 * allocation, never unrelated mappings in this process. No wait for an active
 * GPU job is needed: the G3 mutex serializes publication and this walk. */
NTSTATUS AdmissionGpuvaG3NativeQueryEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  APPLE_AGX_G3_NATIVE_QUERY q;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *p;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_OPEN_ALLOCATION *opened;
  ADMISSION_ALLOCATION_HANDLE *allocation;
  ADMISSION_DEVICE *device;
  DXGKARGCB_GETHANDLEDATA lookup = {0};
  DXGKARGCB_RELEASEHANDLEDATA reference = {0};
  APPLE_AGX_GPUVA_G3_WALK_FAILURE walk = {0};
  ULONGLONG page, end, ipa;
  ULONGLONG poolPages = 0ULL, otherPages = 0ULL;
  ULONGLONG firstOther = 0ULL, firstAbsent = 0ULL;
  NTSTATUS status = STATUS_INVALID_PARAMETER;
  if (!adapter || !adapter->Started || !args ||
      KeGetCurrentIrql() != PASSIVE_LEVEL || args->Flags.Value ||
      !args->pPrivateDriverData || args->PrivateDriverDataSize != sizeof(q) ||
      !adapter->Interface.DxgkCbAcquireHandleData ||
      !adapter->Interface.DxgkCbReleaseHandleData)
    return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (!state) return STATUS_INVALID_DEVICE_STATE;
  RtlCopyMemory(&q, args->pPrivateDriverData, sizeof(q));
  if (q.Magic != APPLE_AGX_G3_NATIVE_QUERY_MAGIC ||
      ((q.Version != APPLE_AGX_G3_NATIVE_QUERY_VERSION || q.Reserved != 0u) &&
       (q.Version != APPLE_AGX_G3_NATIVE_QUERY_POOL_VERSION ||
        q.Reserved != APPLE_AGX_G3_NATIVE_QUERY_POOL_ONLY) &&
       (q.Version != APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION ||
        q.Reserved > APPLE_AGX_G3_NATIVE_QUERY_POOL_ONLY) &&
       (q.Version != APPLE_AGX_G3_NATIVE_QUERY_PLACEMENT_VERSION ||
        q.Reserved != 0u)) ||
      q.Bytes != sizeof(q) ||
      !q.Allocation || !q.GpuVa || (q.GpuVa & 0xffffULL) ||
      q.GpuVa >= (1ULL << 39) || !q.AllocationBytes ||
      q.AllocationBytes > MAXULONG || q.AllocationBytes > (1ULL << 39) - q.GpuVa ||
      q.Write > 1u || q.Ready || q.Reason || q.FailedVa ||
      (q.Version != APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION && q.Reserved2))
    return STATUS_INVALID_PARAMETER;
  lookup.hObject = q.Allocation;
  lookup.Type = DXGK_HANDLE_ALLOCATION;
  lookup.Flags.DeviceSpecific = 1u;
  reference.Type = DXGK_HANDLE_ALLOCATION;
  opened = (ADMISSION_OPEN_ALLOCATION *)adapter->Interface.DxgkCbAcquireHandleData(
      &lookup, &reference.ReleaseHandle);
  if (!opened || !reference.ReleaseHandle) {
    status = STATUS_INVALID_HANDLE;
    goto Release;
  }
  ExAcquireFastMutex(&state->Lock);
  p = AdmissionGpuvaG3FindProcess(state, args->hKmdProcessHandle);
  if (!p || p->Poisoned || p->CpuOnlyMappings || !p->Graph.Created ||
      p->Graph.Uncertain) goto Unlock;
  for (context = p->Contexts; context && (HANDLE)context != args->hContext;
       context = context->GpuvaG3NextContext) {}
  if (!context || !context->Win32Transport || context->GpuvaG3Closing ||
      context->GpuvaG3Poisoned || !context->Object.Device) goto Unlock;
  /* R147: paging selects the process graph before pre-submit escapes. The
   * context SetRoot notification is only guaranteed before execution; do not
   * require it here. Submit/BeginJob retain their context-root checks. */
  device = CONTAINING_RECORD(context->Object.Device, ADMISSION_DEVICE, Object);
  if ((HANDLE)device != args->hDevice || device->Object.Adapter != &adapter->ObjectAdapter ||
      opened->Magic != ADMISSION_OPEN_ALLOCATION_MAGIC ||
      opened->Device != device || opened->RuntimeAllocation != q.Allocation ||
      !opened->Allocation || opened->Allocation->Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC ||
      (q.Write && opened->ReadOnly)) goto Unlock;
  allocation = CONTAINING_RECORD(opened->Allocation, ADMISSION_ALLOCATION_HANDLE, Object);
  /* Adopted/imported slots expose logical surface bytes, while their KMD
   * allocation is rounded up. Authenticate that extent, then prove the whole
   * description extent, never a caller-selected safe prefix. */
  if (q.AllocationBytes > allocation->Object.Description.Size ||
      allocation->Object.Description.Size > MAXULONG ||
      allocation->Object.Description.Size > (1ULL << 39) - q.GpuVa) goto Unlock;
  q.AllocationBytes = allocation->Object.Description.Size;
  /* A well-formed query reports not-ready as data. It is not a failed paging
   * DDI or a failed scheduled submit; the UMD can decline before submission. */
  status = STATUS_SUCCESS;
  if (q.Version == APPLE_AGX_G3_NATIVE_QUERY_PLACEMENT_VERSION) {
    /* rc107: where VidMm's current logical mapping of this allocation
     * lives. EXP1974 republished a locked CPU-visible pool allocation in
     * segment 0; the UMD needs to tell that from eviction (invalid pages)
     * before it asks VidMm to page the allocation back into the pool. No
     * graph walk, native leaf or token is consulted or issued here. */
    end = q.GpuVa + q.AllocationBytes;
    for (page = q.GpuVa; page < end; page += 0x1000ULL) {
      const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte = AdmissionG3CopyPte(p, page);
      if (!pte || !(pte->Flags & APPLE_AGX_GPUVA_G3_VALID)) {
        if (!firstAbsent) firstAbsent = page;
        continue;
      }
      if (pte->Allocation != (ULONGLONG)(ULONG_PTR)allocation ||
          pte->AllocationOffset != page - q.GpuVa) {
        q.Reason = APPLE_AGX_G3_NATIVE_QUERY_IDENTITY;
        q.FailedVa = page;
        goto Output;
      }
      if (pte->SegmentId == ADMISSION_MEMORY_POOL_SEGMENT) {
        ++poolPages;
      } else {
        if (!otherPages) firstOther = page;
        ++otherPages;
      }
    }
    q.Reserved2 = poolPages | (otherPages << 32);
    if (otherPages) {
      q.Reason = APPLE_AGX_G3_NATIVE_QUERY_NOT_POOL;
      q.FailedVa = firstOther;
    } else if (firstAbsent) {
      q.Reason = APPLE_AGX_G3_NATIVE_QUERY_NOT_RESIDENT;
      q.FailedVa = firstAbsent;
    } else {
      q.Ready = 1u;
    }
    goto Output;
  }
  if (q.Version == APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION) {
    if (AppleAgxG3NativeCacheHit(&p->NativeReady,
        (ULONGLONG)(ULONG_PTR)allocation, q.GpuVa, q.AllocationBytes,
        q.Write | (q.Reserved << 1), q.Reserved2)) {
      q.Ready = 1u;
      goto Output;
    }
    q.Reserved2 = 0ULL;
  }
  if (!AppleAgxGpuvaG3GraphInspectRangeAccess(&p->Graph, q.GpuVa,
          (UINT)q.AllocationBytes, q.Write != 0u, &walk)) {
    q.FailedVa = walk.Va;
    q.Reason = walk.Reason;
    goto Output;
  }
  end = q.GpuVa + q.AllocationBytes;
  for (page = q.GpuVa; page < end; page += 0x1000ULL) {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte = AdmissionG3CopyPte(p, page);
    q.FailedVa = page;
    q.Reason = APPLE_AGX_G3_NATIVE_QUERY_IDENTITY;
    if (!pte || !(pte->Flags & APPLE_AGX_GPUVA_G3_VALID) ||
        (q.Write && !(pte->Flags & APPLE_AGX_GPUVA_G3_WRITE)) ||
        pte->Allocation != (ULONGLONG)(ULONG_PTR)allocation ||
        pte->AllocationOffset != page - q.GpuVa) goto Output;
    if ((q.Reserved & APPLE_AGX_G3_NATIVE_QUERY_POOL_ONLY) &&
        pte->SegmentId != ADMISSION_MEMORY_POOL_SEGMENT) {
      q.Reason = APPLE_AGX_G3_NATIVE_QUERY_NOT_POOL;
      goto Output;
    }
    q.Reason = APPLE_AGX_G3_NATIVE_QUERY_TRANSLATION;
    /* Native identity is committed only after publication acknowledgement.
     * Until an ordered shadow owner exists, the only admitted backing mode
     * is direct: native and original paging identities must also agree. */
    if (!pte->NativeIpa || pte->NativeIpa != pte->GuestIpa ||
        !AppleAgxGpuvaG3GraphTranslateVa(&p->Graph, page, &ipa) ||
        ipa != pte->NativeIpa) goto Output;
  }
  q.Ready = 1u;
  q.FailedVa = 0ULL;
  q.Reason = 0u;
  if (q.Version == APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION)
    q.Reserved2 = AppleAgxG3NativeCacheStore(&p->NativeReady,
        (ULONGLONG)(ULONG_PTR)allocation, q.GpuVa, q.AllocationBytes,
        q.Write | (q.Reserved << 1));
Output:
  RtlCopyMemory(args->pPrivateDriverData, &q, sizeof(q));
Unlock:
  ExReleaseFastMutex(&state->Lock);
Release:
  if (reference.ReleaseHandle) adapter->Interface.DxgkCbReleaseHandleData(reference);
  return status;
}

/* Retain the single-query handle reference, graph mutex, full-description
 * extent and page-by-page identity/translation proof on every cache miss.
 * The UMD's submission holds retain residency across entries and submission,
 * exactly as for separate escapes. v2 returns hits/misses in Reserved[0..1].
 * Local entry copies also keep each proof independent of output writes. */
NTSTATUS AdmissionGpuvaG3NativeQueryBatchEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  APPLE_AGX_G3_NATIVE_QUERY_BATCH batch;
  APPLE_AGX_G3_NATIVE_QUERY query;
  DXGKARG_ESCAPE single;
  unsigned char *entries;
  ULONG i;
  NTSTATUS status;
  if (!adapter || !adapter->Started || !args ||
      KeGetCurrentIrql() != PASSIVE_LEVEL || args->Flags.Value ||
      !args->pPrivateDriverData || args->PrivateDriverDataSize < sizeof(batch))
    return STATUS_INVALID_PARAMETER;
  RtlCopyMemory(&batch, args->pPrivateDriverData, sizeof(batch));
  if (batch.Magic != APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAGIC ||
      (batch.Version != APPLE_AGX_G3_NATIVE_QUERY_BATCH_VERSION &&
       batch.Version != APPLE_AGX_G3_NATIVE_QUERY_BATCH_CACHED_VERSION) ||
      !batch.Count || batch.Count > APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAX ||
      batch.Bytes != sizeof(batch) + batch.Count * sizeof(query) ||
      batch.Bytes != args->PrivateDriverDataSize || batch.Ready ||
      batch.FailedIndex || batch.Reserved[0] || batch.Reserved[1])
    return STATUS_INVALID_PARAMETER;
  entries = (unsigned char *)args->pPrivateDriverData + sizeof(batch);
  single = *args;
  single.pPrivateDriverData = &query;
  single.PrivateDriverDataSize = sizeof(query);
  for (i = 0u; i < batch.Count; ++i) {
    ULONGLONG token;
    RtlCopyMemory(&query, entries + i * sizeof(query), sizeof(query));
    if (batch.Version == APPLE_AGX_G3_NATIVE_QUERY_BATCH_VERSION &&
        query.Version == APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION)
      return STATUS_INVALID_PARAMETER;
    /* rc107: a placement census never proves native coverage. */
    if (query.Version == APPLE_AGX_G3_NATIVE_QUERY_PLACEMENT_VERSION)
      return STATUS_INVALID_PARAMETER;
    token = query.Reserved2;
    status = AdmissionGpuvaG3NativeQueryEscape(adapter, &single);
    if (status != STATUS_SUCCESS) return status;
    RtlCopyMemory(entries + i * sizeof(query), &query, sizeof(query));
    if (query.Ready != 1u) {
      batch.FailedIndex = i;
      RtlCopyMemory(args->pPrivateDriverData, &batch, sizeof(batch));
      return STATUS_SUCCESS;
    }
    if (batch.Version == APPLE_AGX_G3_NATIVE_QUERY_BATCH_CACHED_VERSION)
      ++batch.Reserved[token && token == query.Reserved2 ? 0u : 1u];
  }
  batch.Ready = 1u;
  RtlCopyMemory(args->pPrivateDriverData, &batch, sizeof(batch));
  return STATUS_SUCCESS;
}

typedef struct _ADMISSION_G3_COPY_PAGING_QUIESCENCE {
  ULONG Version, Bytes, Status, OsProcessId;
  LONG RecordsUnsubmitted;
  ULONG CpuQueueCount, PagingPending, PagingWorkersActive;
  ULONG PagingDpcPending, PagingDpcsActive, SchedulerFaulted, WaitMilliseconds;
  ULONG PagingFence, LastSubmittedFence, LastCompletedFence, ActiveProcess;
  ULONGLONG GpuVa, Offset, Allocation, GraphProcessId;
} ADMISSION_G3_COPY_PAGING_QUIESCENCE;

static volatile LONG gCopyPagingQuiescenceClaimed;

static VOID AdmissionG3WriteCopyPagingQuiescence(
    ADMISSION_CONTEXT *Adapter,
    const ADMISSION_G3_COPY_PAGING_QUIESCENCE *Receipt) {
  UNICODE_STRING name;
  HANDLE key = NULL;
  if (Adapter->PhysicalDeviceObject == NULL ||
      !NT_SUCCESS(IoOpenDeviceRegistryKey(Adapter->PhysicalDeviceObject,
                                          PLUGPLAY_REGKEY_DEVICE,
                                          KEY_SET_VALUE, &key)))
    return;
  RtlInitUnicodeString(&name, L"Wom1G3CopyPagingQuiescence");
  (void)ZwSetValueKey(key, &name, 0, REG_BINARY, (PVOID)Receipt,
                      sizeof(*Receipt));
  (void)ZwFlushKey(key);
  ZwClose(key);
}

NTSTATUS AdmissionGpuvaG3CopyEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  APPLE_AGX_G3_COPY_REQUEST *q=NULL;
  ADMISSION_G3_STATE *state=NULL;
  ADMISSION_G3_PROCESS *p=NULL;
  ADMISSION_RENDER_CONTEXT *context=NULL;
  ADMISSION_OPEN_ALLOCATION *opened;
  ADMISSION_ALLOCATION_HANDLE *allocation=NULL;
  ADMISSION_SCANOUT_MEMORY_VIEW view, pool;
  DXGKARGCB_GETHANDLEDATA lookup={0};
  DXGKARGCB_RELEASEHANDLEDATA reference={0};
  ULONGLONG length=0, offset, end=0, page=0, first=0;
  APPLE_AGX_GPUVA_G3_WALK_FAILURE walk={0};
  BOOLEAN captured=FALSE, captureAttempted=FALSE, transferCaptured=FALSE;
  ADMISSION_G3_COPY_PAGING_QUIESCENCE pagingSnapshot = {0};
  BOOLEAN pagingSnapshotCaptured = FALSE, leafHistoryCaptured = FALSE;
  ULONG predicate=0u, operation=MAXULONG;
  BOOLEAN isQuery=FALSE;
  NTSTATUS status=STATUS_INVALID_PARAMETER;
  UINT wait_ms;
  ULONGLONG deadline;
  UINT pte_wait=0u;
  ULONGLONG pte_wait_start=0ULL;
  LARGE_INTEGER delay;
  /* RC68: job gate of this request (AppleAgxG3CopyJobGate); strict drops
   * the UMD proof once the allocation is known to be a presentation one. */
  UINT gate=APPLE_AGX_G3_COPY_GATE_IDLE, outcome=0u;
  BOOLEAN strict=FALSE;
  /* Read only the operation tag when the OS-buffered envelope covers it.
   * An absent/truncated tag is not guessed to be a QUERY. */
  if(args && KeGetCurrentIrql()==PASSIVE_LEVEL && args->pPrivateDriverData &&
     args->PrivateDriverDataSize>=(ULONG)FIELD_OFFSET(APPLE_AGX_G3_COPY_REQUEST,Allocation)) {
    RtlCopyMemory(&operation,(PUCHAR)args->pPrivateDriverData+
        FIELD_OFFSET(APPLE_AGX_G3_COPY_REQUEST,Operation),sizeof(operation));
    isQuery=operation==APPLE_AGX_G3_COPY_QUERY;
  }
  /* Stable predicate IDs: investigation/analysis/EXP858-query-contract.md.
   * All returns and admission conditions are preserved; emit after unlocking. */
#define COPY_REJECT_IF(condition, id, code, target) do { \
  if(condition) { predicate=(id); status=(code); goto target; } \
} while(0)
  COPY_REJECT_IF(!adapter || !adapter->Started, 1u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(!args, 2u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(KeGetCurrentIrql()!=PASSIVE_LEVEL, 3u, STATUS_INVALID_PARAMETER, Free);
  /* CPU-only buffered copies use software entry. Retain legacy Level-Two
   * callers, but reject every unrelated escape flag. Own locks, active-job
   * and paging admission below remain required in either entry mode. */
  COPY_REJECT_IF((args->Flags.Value&~1u)!=0u, 4u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(args->PrivateDriverDataSize!=sizeof(*q), 5u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(!args->pPrivateDriverData, 6u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(!adapter->Interface.DxgkCbAcquireHandleData ||
      !adapter->Interface.DxgkCbReleaseHandleData, 7u, STATUS_INVALID_PARAMETER, Free);
  state=(ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  COPY_REJECT_IF(!state, 8u, STATUS_INVALID_DEVICE_STATE, Free);
  q=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*q),ADMISSION_POOL_TAG);
  COPY_REJECT_IF(!q, 9u, STATUS_INSUFFICIENT_RESOURCES, Free);
  RtlCopyMemory(q,args->pPrivateDriverData,sizeof(*q));
  COPY_REJECT_IF(q->Magic!=APPLE_AGX_G3_COPY_MAGIC, 10u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(q->Version!=1u, 11u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(q->Bytes!=sizeof(*q), 12u, STATUS_INVALID_PARAMETER, Free);
  /* RC68: Reserved may carry the UMD proof and echo a previous outcome
   * (ignored on input); any other bit is invalid. */
  COPY_REJECT_IF((q->Reserved&~APPLE_AGX_G3_COPY_RESERVED_MASK) || q->Reserved2, 13u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(q->Operation>APPLE_AGX_G3_COPY_DOWNLOAD, 14u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(!q->Allocation, 15u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(!q->GpuVa, 16u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF((q->GpuVa&0xffffULL), 17u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(q->GpuVa>=(1ULL<<39), 18u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(q->TransferBytes>APPLE_AGX_G3_COPY_CAPACITY, 19u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(q->Operation==APPLE_AGX_G3_COPY_QUERY &&
      (q->Offset || q->TransferBytes || q->MappingGeneration || q->ProcessGeneration), 20u, STATUS_INVALID_PARAMETER, Free);
  COPY_REJECT_IF(q->Operation!=APPLE_AGX_G3_COPY_QUERY &&
      (!q->TransferBytes || !q->MappingGeneration || !q->ProcessGeneration), 21u, STATUS_INVALID_PARAMETER, Free);
  lookup.hObject=q->Allocation;lookup.Type=DXGK_HANDLE_ALLOCATION;
  lookup.Flags.DeviceSpecific=1u;reference.Type=DXGK_HANDLE_ALLOCATION;
  opened=(ADMISSION_OPEN_ALLOCATION *)adapter->Interface.DxgkCbAcquireHandleData(
      &lookup,&reference.ReleaseHandle);
  COPY_REJECT_IF(!opened || !reference.ReleaseHandle, 22u, STATUS_INVALID_HANDLE, Release);
  /* R157 (EXP871): a native job (any process, node 0 is serialized) is
   * normally milliseconds from joined completion. Wait for it with the G3
   * lock released rather than refusing the copy: a refused copy rejects the
   * UMD batch and the D3D device never presents. The bound exceeds TdrDelay;
   * predicates 42-44 still refuse if the job does not finish. */
  wait_ms=0u;
  /* EXP1138: the 3 s bound is wall-clock; event wakes make iterations short. */
  deadline=KeQueryInterruptTime()+3000ULL*10000ULL;
RetryPagingQuiescence:
  for(;;++wait_ms) {
    KeClearEvent(&state->JobEvent);
    ExAcquireFastMutex(&state->Lock);
    p=AdmissionGpuvaG3FindProcess(state,args->hKmdProcessHandle);
    /* R161 (EXP874): an UPLOAD/DOWNLOAD also waits until every built paging
     * FILL/TRANSFER ran; otherwise it writes the new placement and a later
     * transfer overwrites it (or it reads the placement before the move). */
    /* EXP1002: canonical copies are process-private and run under this
     * lock (BeginJob takes it too); only this process's job conflicts.
     * RC68: a job of the caller's own context does not conflict when the
     * UMD proved that no submission which may still run holds the slot. */
    gate=p ? AppleAgxG3CopyJobGate(q->Operation,strict ? 0u : q->Reserved,
        state->ActiveProcess==p || p->Graph.JobInFlight,
        state->ActiveProcess==p && state->ActiveContext==(ADMISSION_RENDER_CONTEXT *)args->hContext,
        FALSE) : APPLE_AGX_G3_COPY_GATE_WAIT;
    if(!p || (gate!=APPLE_AGX_G3_COPY_GATE_WAIT &&
        (q->Operation==APPLE_AGX_G3_COPY_QUERY || AdmissionPagingQuiescent(adapter))) ||
       KeQueryInterruptTime()>=deadline) break;
    if(gate==APPLE_AGX_G3_COPY_GATE_WAIT) outcome|=APPLE_AGX_G3_COPY_RESULT_WAITED;
    ExReleaseFastMutex(&state->Lock);
    /* EXP1138: wake when this process's job ends (1 ms backstop for the
     * paging-quiescence condition, which has no event). */
    delay.QuadPart=-10000LL;
    (void)KeWaitForSingleObject(&state->JobEvent,Executive,KernelMode,FALSE,&delay);
  }
  COPY_REJECT_IF(!p, 23u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(p->Poisoned, 24u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(p->Graph.Uncertain, 25u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(!p->Graph.Created, 26u, STATUS_INVALID_PARAMETER, Unlock);
  for(context=p->Contexts;context && (HANDLE)context!=args->hContext;
      context=context->GpuvaG3NextContext) {}
  COPY_REJECT_IF(!context, 27u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(!context->Win32Transport, 28u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(context->GpuvaG3Closing, 29u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(context->GpuvaG3Poisoned, 30u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(!context->Object.Device, 31u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF((HANDLE)CONTAINING_RECORD(context->Object.Device,ADMISSION_DEVICE,Object)!=args->hDevice, 32u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(context->Object.Device->Adapter!=&adapter->ObjectAdapter, 33u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(opened->Magic!=ADMISSION_OPEN_ALLOCATION_MAGIC, 34u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF((HANDLE)opened->Device!=args->hDevice, 35u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(!opened->Allocation, 36u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(opened->RuntimeAllocation!=q->Allocation, 37u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(opened->Allocation->Magic!=ADMISSION_ALLOCATION_OBJECT_MAGIC, 38u, STATUS_INVALID_PARAMETER, Unlock);
  allocation=CONTAINING_RECORD(opened->Allocation,ADMISSION_ALLOCATION_HANDLE,Object);
  /* EXP1060: a GPU-local presentation allocation is classless and typed by
   * its surface; its CPU updates (UpdateSubresourceUP, maps) reach it only
   * through this copy. The per-page owner checks below still apply. */
  COPY_REJECT_IF(!allocation->Win32ClassId && !allocation->Presentation, 39u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(allocation->Object.Description.CpuVisible, 40u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(!allocation->Presentation &&
      allocation->Object.Description.Type!=ADMISSION_WIN32_ALLOCATION_GPU_LOCAL, 41u, STATUS_INVALID_PARAMETER, Unlock);
  /* RC68: the proof never covers a presentation allocation (present and
   * flip work reads it outside the UMD holds): wait as before. */
  gate=AppleAgxG3CopyJobGate(q->Operation,strict ? 0u : q->Reserved,
      state->ActiveProcess==p || p->Graph.JobInFlight,
      state->ActiveProcess==p && state->ActiveContext==(ADMISSION_RENDER_CONTEXT *)args->hContext,
      allocation->Presentation!=0u);
  if(gate==APPLE_AGX_G3_COPY_GATE_WAIT && !strict &&
     (q->Reserved&APPLE_AGX_G3_COPY_FLAG_UNREFERENCED) && allocation->Presentation &&
     KeQueryInterruptTime()<deadline) {
    strict=TRUE;
    ExReleaseFastMutex(&state->Lock);
    goto RetryPagingQuiescence;
  }
  COPY_REJECT_IF(gate==APPLE_AGX_G3_COPY_GATE_WAIT && state->ActiveProcess==p, 42u, STATUS_DEVICE_BUSY, Unlock);
  COPY_REJECT_IF(gate==APPLE_AGX_G3_COPY_GATE_WAIT && p->Graph.JobInFlight, 43u, STATUS_DEVICE_BUSY, Unlock);
  COPY_REJECT_IF(gate==APPLE_AGX_G3_COPY_GATE_WAIT && p->Graph.LeaseToken, 44u, STATUS_DEVICE_BUSY, Unlock);
  if(gate==APPLE_AGX_G3_COPY_GATE_OVERLAP) outcome|=APPLE_AGX_G3_COPY_RESULT_OVERLAPPED;
  /* A paging buffer can be built between the initial check and either final
   * check. Retry the whole owner/context validation on either late change,
   * preserving the original cumulative 3 s bound and R161 guard. */
  if(q->Operation!=APPLE_AGX_G3_COPY_QUERY) {
    BOOLEAN pagingReady=AdmissionPagingQuiescent(adapter);
    if(pagingReady) pagingReady=AdmissionPagingQuiescent(adapter);
    if(!pagingReady && KeQueryInterruptTime()<deadline) {
      ExReleaseFastMutex(&state->Lock);
      delay.QuadPart=-10000LL;
      (void)KeDelayExecutionThread(KernelMode,FALSE,&delay);
      ++wait_ms;
      goto RetryPagingQuiescence;
    }
    COPY_REJECT_IF(!pagingReady, 62u, STATUS_DEVICE_BUSY, Unlock);
  }
  /* R159: only a different process instance invalidates the QUERY. An
   * unrelated mapping update advances MappingGeneration constantly; the
   * per-page range validation below (56-61) re-proves this exact range
   * under the lock, as R154 did for BeginJob. */
  COPY_REJECT_IF(q->Operation!=APPLE_AGX_G3_COPY_QUERY &&
      q->ProcessGeneration!=p->Graph.ProcessGeneration, 45u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(q->Operation==APPLE_AGX_G3_COPY_UPLOAD &&
      (opened->ReadOnly || (!allocation->Presentation &&
       !(opened->Win32Flags&AppleAgxWin32BufferCpuWrite))), 46u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(q->Operation==APPLE_AGX_G3_COPY_DOWNLOAD && !allocation->Presentation &&
      !(opened->Win32Flags&AppleAgxWin32BufferCpuRead), 47u, STATUS_INVALID_PARAMETER, Unlock);
  length=q->Operation==APPLE_AGX_G3_COPY_QUERY ?
      allocation->Object.Description.Size : q->TransferBytes;
  COPY_REJECT_IF(!length, 48u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(length>MAXULONG, 49u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(q->Offset>allocation->Object.Description.Size, 50u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(length>allocation->Object.Description.Size-q->Offset, 51u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(q->Offset>=(1ULL<<39)-q->GpuVa || length>(1ULL<<39)-q->GpuVa-q->Offset, 52u, STATUS_INVALID_PARAMETER, Unlock);
  first=q->GpuVa+q->Offset;end=first+length;
  status=AdmissionMemoryRuntimeLocalView(adapter,&view);
  COPY_REJECT_IF(!NT_SUCCESS(status), 54u, status, Unlock);
  COPY_REJECT_IF(!view.CpuAddress, 55u, STATUS_INVALID_PARAMETER, Unlock);
  /* M2-f2: CPU-visible class BOs may live in the segment-3 pool. */
  (void)AdmissionMemoryPoolView(adapter,&pool);
  /* Prevalidate the entire range before any copy, unchanged from R145. */
  for(page=first&~0xfffULL;page<end;page+=0x1000ULL) {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte=AdmissionG3CopyPte(p,page);
    if(isQuery && !pte &&
       AppleAgxGpuvaG3GraphInspectRangeAccess(&p->Graph,page,0x1000u,
                                               FALSE,&walk)) {
      /* The native graph has the leaf; only its CPU logical shadow is lost. */
      walk.Level=2u;walk.Index=(UINT)((page>>12)&8191u);
      walk.Reason=AppleAgxG3WalkLogicalShadowAbsent;
      walk.ComponentReason=AppleAgxG3WalkLogicalShadowAbsent;
      walk.Va=page;
    }
    COPY_REJECT_IF(!pte, 56u, STATUS_INVALID_PARAMETER, Unlock);
    /* EXP1005: no wait for an invalid PTE. The EXP988 re-validation loop
     * never recovered (EXP995-EXP1003: 0 of 70 waits) and each wait lasted
     * 18-33 s (1000 x 1 ms sleeps at ~16 ms timer granularity). */
    COPY_REJECT_IF(!(pte->Flags&APPLE_AGX_GPUVA_G3_VALID), 57u, STATUS_INVALID_PARAMETER, Unlock);
    COPY_REJECT_IF(pte->SegmentId!=ADMISSION_MEMORY_LOCAL_SEGMENT &&
        (pte->SegmentId!=ADMISSION_MEMORY_POOL_SEGMENT || !pool.CpuAddress), 58u, STATUS_INVALID_PARAMETER, Unlock);
    COPY_REJECT_IF(pte->Allocation!=(ULONGLONG)(ULONG_PTR)allocation, 59u, STATUS_INVALID_PARAMETER, Unlock);
    COPY_REJECT_IF(pte->AllocationOffset!=page-q->GpuVa, 60u, STATUS_INVALID_PARAMETER, Unlock);
    COPY_REJECT_IF(pte->SegmentId==ADMISSION_MEMORY_POOL_SEGMENT ?
        !AppleAgxGpuvaG3TableSpanWithinLocal(pool.GuestIpaAddress,pool.Bytes,
          pte->GuestIpa,0x1000ULL) :
        !AppleAgxGpuvaG3TableSpanWithinLocal(view.GuestIpaAddress,view.Bytes,
          pte->GuestIpa,0x1000ULL), 61u, STATUS_INVALID_PARAMETER, Unlock);
  }
  if(pte_wait) {
    ADMISSION_G3_PTE_WAIT_RECEIPT *w=&adapter->G3PteWait;
    LARGE_INTEGER frequency;
    ULONGLONG ticks=(ULONGLONG)KeQueryPerformanceCounter(&frequency).QuadPart-pte_wait_start;
    w->Version=1u;w->Bytes=sizeof(*w);w->QpcFrequency=(ULONGLONG)frequency.QuadPart;
    ++w->Waited;++w->Recovered;w->TotalTicks+=ticks;w->LastVa=q->GpuVa;
    if(ticks>w->MaxTicks){w->MaxTicks=ticks;w->MaxIterations=pte_wait;}
    InterlockedExchange(&adapter->G3PteWaitDirty,1);
  }
  if(q->Operation==APPLE_AGX_G3_COPY_QUERY) {
    q->ProcessGeneration=p->Graph.ProcessGeneration;
    q->MappingGeneration=p->Graph.MappingGeneration;
  } else {
    KeMemoryBarrier();
    for(offset=0;offset<length;) {
      ULONGLONG va=first+offset, part=0x1000ULL-(va&0xfffULL);
      const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte=AdmissionG3CopyPte(p,va);
      const ADMISSION_SCANOUT_MEMORY_VIEW *home=pte->SegmentId==ADMISSION_MEMORY_POOL_SEGMENT ? &pool : &view;
      PUCHAR cpu=(PUCHAR)home->CpuAddress+(SIZE_T)(pte->GuestIpa-home->GuestIpaAddress)+(SIZE_T)(va&0xfffULL);
      if(part>length-offset) part=length-offset;
      if(q->Operation==APPLE_AGX_G3_COPY_UPLOAD)
        RtlCopyMemory(cpu,q->Data+(SIZE_T)offset,(SIZE_T)part);
      else RtlCopyMemory(q->Data+(SIZE_T)offset,cpu,(SIZE_T)part);
      offset+=part;
    }
    KeMemoryBarrier();
    if(q->Operation==APPLE_AGX_G3_COPY_UPLOAD)
      AdmissionG3TraceUpload(state,p,first,(ULONG)length,q->Data);
  }
  /* RC68 receipt: the input flag is consumed; Reserved returns the gate
   * outcome (the UMD restores its flag before reusing the request). */
  q->Reserved=outcome;
  RtlCopyMemory(args->pPrivateDriverData,q,sizeof(*q));status=STATUS_SUCCESS;
Unlock:
  if(pte_wait && predicate) {
    ADMISSION_G3_PTE_WAIT_RECEIPT *w=&adapter->G3PteWait;
    LARGE_INTEGER frequency;
    ULONGLONG ticks=(ULONGLONG)KeQueryPerformanceCounter(&frequency).QuadPart-pte_wait_start;
    w->Version=1u;w->Bytes=sizeof(*w);w->QpcFrequency=(ULONGLONG)frequency.QuadPart;
    ++w->Waited;++w->TimedOut;w->TotalTicks+=ticks;w->LastVa=q?q->GpuVa:0ULL;
    if(ticks>w->MaxTicks){w->MaxTicks=ticks;w->MaxIterations=pte_wait;}
    InterlockedExchange(&adapter->G3PteWaitDirty,1);
  }
  if (predicate == 62u && q != NULL &&
      InterlockedCompareExchange(&gCopyPagingQuiescenceClaimed, 1, 0) == 0) {
    KIRQL oldIrql;
    pagingSnapshot.Version = 1u;
    pagingSnapshot.Bytes = sizeof(pagingSnapshot);
    pagingSnapshot.Status = (ULONG)status;
    pagingSnapshot.OsProcessId = HandleToULong(PsGetCurrentProcessId());
    pagingSnapshot.WaitMilliseconds = wait_ms;
    pagingSnapshot.GpuVa = q->GpuVa;
    pagingSnapshot.Offset = q->Offset;
    pagingSnapshot.Allocation = q->Allocation;
    pagingSnapshot.GraphProcessId = p ? p->Graph.ProcessId : 0ULL;
    KeAcquireSpinLock(&adapter->PagingLock, &oldIrql);
    pagingSnapshot.RecordsUnsubmitted = adapter->PagingRecordsUnsubmitted;
    pagingSnapshot.CpuQueueCount = adapter->CpuQueueCount;
    pagingSnapshot.PagingPending = (ULONG)InterlockedCompareExchange(
        &adapter->PagingPending, 0, 0);
    pagingSnapshot.PagingWorkersActive = (ULONG)InterlockedCompareExchange(
        &adapter->PagingWorkersActive, 0, 0);
    pagingSnapshot.PagingDpcPending = (ULONG)InterlockedCompareExchange(
        &adapter->PagingDpcPending, 0, 0);
    pagingSnapshot.PagingDpcsActive = (ULONG)InterlockedCompareExchange(
        &adapter->PagingDpcsActive, 0, 0);
    pagingSnapshot.SchedulerFaulted = (ULONG)InterlockedCompareExchange(
        &adapter->SchedulerFaulted, 0, 0);
    pagingSnapshot.PagingFence = adapter->PagingFence;
    pagingSnapshot.LastSubmittedFence = adapter->PagingLastSubmittedFence;
    pagingSnapshot.LastCompletedFence = adapter->PagingLastCompletedFence;
    pagingSnapshot.ActiveProcess = state->ActiveProcess != NULL;
    KeReleaseSpinLock(&adapter->PagingLock, oldIrql);
    pagingSnapshotCaptured = TRUE;
  }
  if (predicate && q &&
      (q->Operation==APPLE_AGX_G3_COPY_UPLOAD ||
       q->Operation==APPLE_AGX_G3_COPY_DOWNLOAD) &&
      InterlockedCompareExchange(&adapter->G3CopyTransferFailureClaim,1,0)==0) {
    APPLE_AGX_G3_COPY_TRANSFER_FAILURE *r=&adapter->G3CopyTransferFailure;
    RtlZeroMemory(r,sizeof(*r));
    r->Version=1u;r->Bytes=sizeof(*r);r->Operation=q->Operation;
    r->Predicate=predicate;r->Status=(ULONG)status;
    r->TransferBytes=q->TransferBytes;r->Pid=HandleToULong(PsGetCurrentProcessId());
    r->GpuVa=q->GpuVa;r->Offset=q->Offset;
    /* Only these predicates are evaluated inside the page-validation loop. */
    r->FailedPage=(predicate>=56u && predicate<=61u) ? page : 0ULL;
    r->Allocation=q->Allocation;r->Context=(ULONGLONG)(ULONG_PTR)args->hContext;
    r->RequestProcessGeneration=q->ProcessGeneration;
    r->RequestMappingGeneration=q->MappingGeneration;
    r->CurrentProcessGeneration=p ? p->Graph.ProcessGeneration : 0ULL;
    r->CurrentMappingGeneration=p ? p->Graph.MappingGeneration : 0ULL;
    KeMemoryBarrier();
    InterlockedExchange(&adapter->G3CopyTransferFailureClaim,2);
    transferCaptured=TRUE;
  }
  if(predicate==57u && p && q && state &&
     InterlockedCompareExchange(&adapter->G3LeafHistoryClaim,1,0)==0) {
    ADMISSION_G3_LEAF_HISTORY_SNAPSHOT *s=&adapter->G3LeafHistorySnapshot;
    LARGE_INTEGER frequency;
    ULONGLONG leaf=0ULL;
    s->Version=1u;s->Bytes=sizeof(*s);s->Predicate=predicate;
    s->Next=state->LeafHistoryNext;
    s->FailVa=page;s->FailProcessId=p->Graph.ProcessId;
    s->FailAllocation=q->Allocation;
    if(AppleAgxGpuvaG3GraphLeafTableIpa(&p->Graph,page,&leaf)) s->FailTableIpa=leaf;
    s->FailIndex=(ULONG)((page>>12)&8191u);
    s->Qpc=(ULONGLONG)KeQueryPerformanceCounter(&frequency).QuadPart;
    s->QpcFrequency=(ULONGLONG)frequency.QuadPart;
    {
      /* EXP1001: newest-first events naming the failing table, VA or
       * allocation; remaining records are the newest others. Reserved holds
       * the matched count; Next the ring position. */
      ULONG total=state->LeafHistoryNext<ADMISSION_G3_LEAF_RING ?
          state->LeafHistoryNext : ADMISSION_G3_LEAF_RING;
      ULONG out=0u,n;
      for(n=0u;n<total && out<ADMISSION_G3_LEAF_HISTORY_COUNT;++n) {
        const ADMISSION_G3_LEAF_HISTORY *e=&state->LeafHistory[
            (state->LeafHistoryNext-1u-n)%ADMISSION_G3_LEAF_RING];
        if((leaf && e->TableIpa==leaf) ||
           (allocation && e->Allocation==(ULONGLONG)(ULONG_PTR)allocation) ||
           (e->FirstVa<=page && page-e->FirstVa<(ULONGLONG)e->Count*0x1000ULL))
          s->Records[out++]=*e;
      }
      s->Reserved=out;
      for(n=0u;n<total && out<ADMISSION_G3_LEAF_HISTORY_COUNT;++n)
        s->Records[out++]=state->LeafHistory[
            (state->LeafHistoryNext-1u-n)%ADMISSION_G3_LEAF_RING];
    }
    s->Version=4u; /* T7: appended allocation metadata in Track. */
    if(allocation) {
      ULONGLONG key=(ULONGLONG)(ULONG_PTR)allocation;
      const ADMISSION_G3_ALLOC_TRACK *t=&state->AllocTrack[
          (ULONG)((key>>6)^(key>>16))%ADMISSION_G3_ALLOC_TRACK_COUNT];
      s->KmdAllocation=key;
      s->AllocationSize=allocation->Object.Description.Size;
      s->AllocationType=(ULONG)allocation->Object.Description.Type;
      if(t->Allocation==key) { s->Track=*t; s->TrackFound=1u; }
    }
    {
      const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *fp=AdmissionG3CopyPte(p,page);
      if(fp) {
        s->PteFound=1u;s->PteGuestIpa=fp->GuestIpa;s->PteSegment=fp->SegmentId;
        s->PteFlags=fp->Flags;s->PteAllocation=fp->Allocation;
        s->PteAllocationOffset=fp->AllocationOffset;
      }
    }
    KeMemoryBarrier();
    InterlockedExchange(&adapter->G3LeafHistoryClaim,2);
    leafHistoryCaptured=TRUE;
  }
  if(isQuery && predicate) {
    captureAttempted=TRUE;
    captured=AdmissionG3CaptureCopyQueryFailure(adapter,p,context,q,predicate,status,
        TRUE,first,length,&walk,allocation);
  }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (isQuery && q != NULL && context != NULL) {
    ULONG residentPages = 0u;
    if (p != NULL && allocation != NULL && length != 0ULL &&
        (predicate == 0u || predicate >= 53u))
      for (page = first & ~0xfffULL; page < end; page += 0x1000ULL) {
        const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte = AdmissionG3CopyPte(p, page);
        if (pte != NULL && (pte->Flags & APPLE_AGX_GPUVA_G3_VALID) != 0u &&
            pte->SegmentId == ADMISSION_MEMORY_LOCAL_SEGMENT &&
            pte->Allocation == (ULONGLONG)(ULONG_PTR)allocation &&
            pte->AllocationOffset == page - q->GpuVa)
          ++residentPages;
      }
    AdmissionDwmFrameRecordQuery(adapter, context, predicate, status,
        residentPages);
  }
#endif
  ExReleaseFastMutex(&state->Lock);
Release:
  if(reference.ReleaseHandle) adapter->Interface.DxgkCbReleaseHandleData(reference);
Free:
  if(isQuery && predicate && !captureAttempted)
    captured=AdmissionG3CaptureCopyQueryFailure(adapter,NULL,NULL,q,predicate,status,
        FALSE,0,0,NULL,NULL);
  if(q) ExFreePoolWithTag(q,ADMISSION_POOL_TAG);
  if(captured) AdmissionRecordG3CopyQueryFailure(adapter);
  if(leafHistoryCaptured) AdmissionRecordG3LeafHistory(adapter);
  if(adapter) AdmissionRecordG3PteWait(adapter);
  if(transferCaptured) AdmissionRecordG3CopyTransferFailure(adapter);
  if(pagingSnapshotCaptured)
    AdmissionG3WriteCopyPagingQuiescence(adapter, &pagingSnapshot);
#undef COPY_REJECT_IF
  return status;
}

/* Caller holds State->Lock. Capture before cleanup/rollback can erase a cause. */
static BOOLEAN AdmissionG3CapturePrivateFailure(ADMISSION_CONTEXT *adapter,
    ADMISSION_G3_STATE *state, ADMISSION_G3_PROCESS *p,
    const DXGKARG_ESCAPE *args, const APPLE_AGX_G3_PRIVATE_REQUEST *q,
    const UINT required[9], UINT branch, NTSTATUS status, UINT tablePredicate,
    UINT mapOffset, UINT range,
    const APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC *prepare,
    const APPLE_AGX_G3_PRIVATE_POOL_STATS *observed) {
  APPLE_AGX_G3_PRIVATE_FAILURE *r;
  APPLE_AGX_G3_PRIVATE_POOL_STATS stats;
  ADMISSION_G3_PRIVATE_SCENE *scene;
  if (q->Operation!=APPLE_AGX_G3_PRIVATE_ACQUIRE || NT_SUCCESS(status) ||
      InterlockedCompareExchange(&adapter->G3PrivateFailureClaim,1,0)!=0)
    return FALSE;
  r=&adapter->G3PrivateFailure;
  RtlZeroMemory(r,sizeof(*r));
  r->Version=2u;r->Bytes=sizeof(*r);r->Branch=branch;r->Status=(UINT)status;
  r->Pid=HandleToULong(PsGetCurrentProcessId());r->Operation=q->Operation;
  r->ProcessHandle=(ULONGLONG)(ULONG_PTR)args->hKmdProcessHandle;
  r->ContextHandle=(ULONGLONG)(ULONG_PTR)args->hContext;
  r->DeviceHandle=(ULONGLONG)(ULONG_PTR)args->hDevice;
  r->Width=q->Width;r->Height=q->Height;r->UtileWidth=q->UtileWidth;
  r->UtileHeight=q->UtileHeight;r->Layers=q->Layers;r->Samples=q->Samples;
  RtlCopyMemory(r->RequiredBytes,required,sizeof(r->RequiredBytes));
  r->TablePredicate=tablePredicate;r->MapLeafOffset=mapOffset;
  r->MapPredicate=(branch==12u || branch==13u) ? 1u : 0u;
  r->FailedRange=range;
  r->PoolUnits=APPLE_AGX_G3_PRIVATE_UNITS;
  r->OwnerLimitUnits=APPLE_AGX_G3_PROCESS_UNITS;
  AppleAgxG3PrivatePoolStats(&state->PrivatePool,p ? p->Graph.ProcessId : 0,&stats);
  if (observed) stats=*observed;
  if (prepare && prepare->Predicate) {
    r->PreparePredicate=prepare->Predicate;r->FailedRange=prepare->FailedRange;
    stats=prepare->Stats;
  }
  r->GlobalUnits=stats.GlobalUnits;r->OwnerUnits=stats.OwnerUnits;
  r->LargestFreeUnits=stats.LargestFreeUnits;
  if (p) {
    r->ProcessId=p->Graph.ProcessId;r->PrivateVa=p->PrivateVa;
    r->RootIpa=p->Graph.RootIpa;r->LeaseToken=p->Graph.LeaseToken;
    r->ManagerGeneration=p->PrivateManager.Generation;
    r->ManagerPresent=p->PrivateManager.Generation!=0;
    r->FreshManager=!r->ManagerPresent;r->Poisoned=p->Poisoned;
    /* v2 preserves the cause before the first scene exists: completion-time
     * Wom1G3Poison cannot run when an earlier paging failure prevented jobs. */
    r->Reserved=p->PoisonSite;
    r->GraphUncertain=p->Graph.Uncertain;r->JobInFlight=p->Graph.JobInFlight;
    for (scene=p->PrivateScenes;scene;scene=scene->Next) {
      ++r->SceneCount;
      if ((HANDLE)scene->Context==args->hContext) ++r->ContextSceneCount;
      if (scene->Queued) ++r->QueuedScenes;
      if (scene->ReleaseRequested) ++r->ReleaseRequestedScenes;
      if (scene->Quarantined) ++r->QuarantinedScenes;
    }
  }
  KeMemoryBarrier();
  InterlockedExchange(&adapter->G3PrivateFailureClaim,2);
  return TRUE;
}

/* Asahi buffer.rs ensure_blocks while the manager is idle (no job of this
 * process in flight, the ACQUIRE/PREPARE precondition): back and map heap
 * blocks up to `blocks`. A failed map is revoked and the extent returned. */
static NTSTATUS AdmissionG3GrowPrivateHeap(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, UINT blocks, UINT *failedOffset) {
  APPLE_AGX_G3_PRIVATE_MANAGER *m=&p->PrivateManager;
  APPLE_AGX_G3_PRIVATE_EXTENT *grown;
  UINT previous=m->Blocks;
  if (p->Graph.JobInFlight) return STATUS_DEVICE_BUSY;
  if (!AppleAgxG3PrivateGrowHeap(&p->State->PrivatePool,p->Graph.ProcessId,
          (unsigned char *)view->CpuAddress,m,blocks,&grown))
    return STATUS_INSUFFICIENT_RESOURCES;
  if (!grown) return STATUS_SUCCESS;
  KeMemoryBarrier();
  if (AdmissionG3PrivateMapExtentObserved(p,view,grown,TRUE,failedOffset)) {
    ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_HEAP_GROW];
    return STATUS_SUCCESS;
  }
  if (!AdmissionG3PrivateMapExtent(p,view,grown,FALSE)) {
    ADMISSION_G3_POISON(p,1u);return STATUS_DEVICE_HARDWARE_ERROR;
  }
  if (!AdmissionG3PrivateFreeExtent(p,view,grown)) {
    ADMISSION_G3_POISON(p,1u);return STATUS_DEVICE_HARDWARE_ERROR;
  }
  --m->GrownCount;m->Blocks=previous;
  return STATUS_INSUFFICIENT_RESOURCES;
}

static ULONG AdmissionG3ManagerWord(const unsigned char *bytes, ULONG offset) {
  ULONG value;
  RtlCopyMemory(&value,bytes+offset,sizeof(value));
  return value;
}

/* rc80 (apple_agx_g3_tvb.h): TVB growth. All below run under State->Lock at
 * PASSIVE (fast mutex: APC_LEVEL) and map only while this process has no job
 * or lease (Asahi grows while the buffer manager is idle). */

/* Firmware Stats max_pages of the current manager (Asahi fw/buffer.rs Stats
 * word 0, saved by AdmissionSaveG4Manager after a joined completion), or 0. */
static ULONG AdmissionG3TvbUsedPages(const ADMISSION_G3_PROCESS *p) {
  const APPLE_AGX_RENDER_MANAGER_STATE *m=&p->FirmwareManager;
  if (!m->Valid || m->PendingFence || !p->PrivateManager.Generation ||
      m->Key.Owner!=p->Graph.ProcessId ||
      m->Key.Generation!=p->PrivateManager.Generation) return 0u;
  return AdmissionG3ManagerWord(m->Stats,0x0u);
}

static UINT AdmissionG3TvbTargetFor(const ADMISSION_G3_PROCESS *p,
    UINT size_blocks) {
  return AppleAgxG3TvbTarget(AdmissionG3TvbUsedPages(p),size_blocks,
      p->PrivateManager.Blocks,p->TvbVa ? APPLE_AGX_G3_TVB_MAX_BLOCKS :
                                          APPLE_AGX_G3_TVB_FLOOR_BLOCKS);
}

static ULONGLONG AdmissionG3TvbNowMs(VOID) {
  return KeQueryInterruptTime()/10000ULL;
}

/* A growth step is due: an ACQUIRE that hits a cached scene waits (bounded)
 * for this process to be idle instead of returning at once. */
static BOOLEAN AdmissionG3TvbWantsIdle(const ADMISSION_G3_PROCESS *p,
    UINT size_blocks) {
  if (!p->PrivateManager.Generation || p->Poisoned || p->Graph.Uncertain ||
      !p->PrivateLeafIpa) return FALSE;
  return AppleAgxG3TvbShouldTry(&p->Tvb,AdmissionG3TvbTargetFor(p,size_blocks),
      p->PrivateManager.Blocks,AdmissionG3TvbNowMs()) ? TRUE : FALSE;
}

static BOOLEAN AdmissionG3TvbUnmapPages(ADMISSION_G3_PROCESS *p,
    ULONGLONG leaf_ipa, UINT pte, UINT count) {
  UINT i;
  for (i=0u;i<count;++i)
    if (!AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,leaf_ipa,pte+i,0ULL,
            FALSE,AppleAgxGpuvaG3PrivateBacking)) return FALSE;
  p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_UNMAPPED_PAGES]+=count;
  return TRUE;
}

/* A window leaf table: one private-pool unit, zeroed, registered at level 2
 * and linked below the private middle table (or the root's current one). */
static UINT AdmissionG3TvbEnsureLeaf(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, UINT leaf) {
  if (!p->TvbLeafIpa[leaf]) {
    APPLE_AGX_G3_PRIVATE_EXTENT *e=&p->TvbLeafTables[leaf];
    ULONGLONG ipa;
    if (!e->Generation) {
      if (!AppleAgxG3PrivateAllocate(&p->State->PrivatePool,p->Graph.ProcessId,
              0x10000u,e)) return APPLE_AGX_G3_TVB_FAIL_TABLE;
      RtlZeroMemory((PUCHAR)view->CpuAddress+e->Offset,e->Bytes);
      KeMemoryBarrier();
    }
    ipa=view->GuestIpaAddress+e->Offset;
    if (!AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,ipa,2u)) {
      /* An acknowledged refusal registered nothing; keep it if uncertain. */
      if (!p->Graph.Uncertain) (void)AdmissionG3PrivateFreeExtent(p,view,e);
      return APPLE_AGX_G3_TVB_FAIL_TABLE;
    }
    p->TvbLeafIpa[leaf]=ipa;
    p->Tvb.Leaves|=1u<<leaf;
  }
  if (!AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph,
          p->TvbVa+(ULONGLONG)leaf*APPLE_AGX_G3_TVB_LEAF_BYTES,
          p->PrivateMiddleIpa,p->TvbLeafIpa[leaf]))
    return APPLE_AGX_G3_TVB_FAIL_ATTACH;
  return APPLE_AGX_G3_TVB_FAIL_NONE;
}

/* Back window block `block` with two zeroed slab granules and map its eight
 * 16 KiB pages writable. All or nothing for the block; a failed unmap of a
 * partial block leaves its granules held and the process poisoned. */
static UINT AdmissionG3TvbMapBlock(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, UINT block) {
  ADMISSION_POOL_PRIVATE *leases;
  ULONGLONG ipa[APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK]={0};
  UINT leaf=0u, pte=0u, g, i, reason;
  if (!AppleAgxG3TvbBlockSlot(block,&leaf,&pte) || !p->TvbLeases)
    return APPLE_AGX_G3_TVB_FAIL_MAP;
  reason=AdmissionG3TvbEnsureLeaf(p,view,leaf);
  if (reason) return reason;
  leases=&p->TvbLeases[(block-APPLE_AGX_G3_TVB_FLOOR_BLOCKS)*
                       APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK];
  for (g=0u;g<APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK;++g)
    if (!AdmissionMemoryPoolTvbTake(p->State->Adapter,&leases[g],&ipa[g])) {
      while (g) (void)AdmissionMemoryPoolTvbGive(p->State->Adapter,&leases[--g]);
      return APPLE_AGX_G3_TVB_FAIL_LEASE;
    }
  for (i=0u;i<APPLE_AGX_G3_TVB_GPU_PAGES_PER_BLOCK;++i)
    if (!AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,p->TvbLeafIpa[leaf],
            pte+i,ipa[i/4u]+(ULONGLONG)(i%4u)*0x4000ULL,TRUE,
            AppleAgxGpuvaG3PrivateBacking)) {
      if (p->Graph.Uncertain ||
          !AdmissionG3TvbUnmapPages(p,p->TvbLeafIpa[leaf],pte,i)) {
        ADMISSION_G3_POISON(p,1u);
        return APPLE_AGX_G3_TVB_FAIL_MAP;
      }
      for (g=0u;g<APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK;++g)
        (void)AdmissionMemoryPoolTvbGive(p->State->Adapter,&leases[g]);
      return APPLE_AGX_G3_TVB_FAIL_MAP;
    }
  p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MAPPED_PAGES]+=
      APPLE_AGX_G3_TVB_GPU_PAGES_PER_BLOCK;
  return APPLE_AGX_G3_TVB_FAIL_NONE;
}

/* Asahi queue/render.rs auto_grow + ensure_blocks at a safe point. Fail-soft:
 * every block mapped before a refusal stays; the render is admitted with the
 * blocks backed (>= its capped minimum, which the caller still enforces). */
static VOID AdmissionG3TvbGrow(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, UINT size_blocks) {
  APPLE_AGX_G3_PRIVATE_MANAGER *m=&p->PrivateManager;
  ULONGLONG now=AdmissionG3TvbNowMs();
  UINT target, step=0u, before, reason=APPLE_AGX_G3_TVB_FAIL_NONE, offset=0u;
  if (!m->Generation || p->Poisoned || p->Graph.Uncertain ||
      p->Graph.JobInFlight || !p->PrivateLeafIpa) return;
  p->Tvb.LastUsedPages=AdmissionG3TvbUsedPages(p);
  target=AdmissionG3TvbTargetFor(p,size_blocks);
  if (!AppleAgxG3TvbShouldTry(&p->Tvb,target,m->Blocks,now)) return;
  before=m->Blocks;
  if (m->Blocks<APPLE_AGX_G3_TVB_FLOOR_BLOCKS &&
      !NT_SUCCESS(AdmissionG3GrowPrivateHeap(p,view,
          AppleAgxG3TvbStep(m->Blocks,target),&offset)))
    reason=APPLE_AGX_G3_TVB_FAIL_FLOOR;
  if (!reason && target>m->Blocks) {
    step=AppleAgxG3TvbStep(m->Blocks,target);
    if (!p->TvbVa) reason=APPLE_AGX_G3_TVB_FAIL_NO_WINDOW;
    else if (!p->TvbLeases) {
      p->TvbLeases=ExAllocatePool2(POOL_FLAG_NON_PAGED,
          (SIZE_T)APPLE_AGX_G3_TVB_WINDOW_BLOCKS*
              APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK*sizeof(*p->TvbLeases),
          ADMISSION_POOL_TAG);
      if (!p->TvbLeases) reason=APPLE_AGX_G3_TVB_FAIL_LIST;
    }
    while (!reason && m->Blocks<step) {
      reason=AdmissionG3TvbMapBlock(p,view,m->Blocks);
      if (!reason) { ++m->Blocks; ++p->Tvb.LeaseBlocks; }
    }
    if (!reason && m->Blocks<target) {
      /* One window step per call; keep the target due for the next one. */
      AppleAgxG3TvbNoteResult(&p->Tvb,target,before,m->Blocks,reason,now);
      if (m->Blocks>before) ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_HEAP_GROW];
      return;
    }
  }
  AppleAgxG3TvbNoteResult(&p->Tvb,target,before,m->Blocks,reason,now);
  if (m->Blocks>before) ++p->State->PrivateStats[ADMISSION_G3_PRIVATE_STAT_HEAP_GROW];
}

/* Manager teardown (no job or lease of this process): unmap every window
 * block, newest first, and return its granules. FALSE: the process is
 * poisoned and the remaining blocks stay mapped and held. */
static BOOLEAN AdmissionG3TvbRelease(ADMISSION_G3_PROCESS *p) {
  while (p->Tvb.LeaseBlocks) {
    UINT block=APPLE_AGX_G3_TVB_FLOOR_BLOCKS+p->Tvb.LeaseBlocks-1u, leaf=0u, pte=0u, g;
    ADMISSION_POOL_PRIVATE *leases;
    if (!p->TvbLeases || !AppleAgxG3TvbBlockSlot(block,&leaf,&pte) ||
        p->Graph.JobInFlight || p->Graph.Uncertain ||
        !AdmissionG3TvbUnmapPages(p,p->TvbLeafIpa[leaf],pte,
            APPLE_AGX_G3_TVB_GPU_PAGES_PER_BLOCK)) {
      ADMISSION_G3_POISON(p,1u);
      return FALSE;
    }
    leases=&p->TvbLeases[(block-APPLE_AGX_G3_TVB_FLOOR_BLOCKS)*
                         APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK];
    for (g=0u;g<APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK;++g)
      (void)AdmissionMemoryPoolTvbGive(p->State->Adapter,&leases[g]);
    --p->Tvb.LeaseBlocks;
    if (p->PrivateManager.Blocks>APPLE_AGX_G3_TVB_FLOOR_BLOCKS)
      --p->PrivateManager.Blocks;
  }
  return TRUE;
}

/* Process teardown after GraphDestroy revoked every grant: return all
 * granules. TRUE when none is still held (the lease array may be freed). */
static BOOLEAN AdmissionG3TvbReturnLeases(ADMISSION_G3_PROCESS *p) {
  BOOLEAN all=TRUE;
  UINT i;
  if (!p->TvbLeases) return TRUE;
  for (i=0u;i<APPLE_AGX_G3_TVB_WINDOW_BLOCKS*APPLE_AGX_G3_TVB_GRANULES_PER_BLOCK;++i)
    if (p->TvbLeases[i].Slab &&
        !AdmissionMemoryPoolTvbGive(p->State->Adapter,&p->TvbLeases[i])) all=FALSE;
  p->Tvb.LeaseBlocks=0u;
  return all;
}

/* EXP1114 receipt-only, under Lock, at most every two seconds: each process's
 * private units and scenes, the largest render it asked storage for, and its
 * saved firmware buffer-manager Stats/Info/BlockControl words (Asahi
 * fw/buffer.rs: Stats max_pages, max_b, overflow_count, gpu_c, reset; Info
 * page_count 0x28, block_count 0x30; BlockControl total, wptr). Header:
 * version, slots used, global units, largest free run, QPC low/high,
 * frequency, processes listed. */
static BOOLEAN AdmissionG3SnapshotProcesses(ADMISSION_G3_STATE *state,
    ULONG out[ADMISSION_G3_PROCESS_RECEIPT_WORDS]) {
  LARGE_INTEGER frequency;
  LONGLONG now=KeQueryPerformanceCounter(&frequency).QuadPart;
  APPLE_AGX_G3_PRIVATE_POOL_STATS stats;
  LIST_ENTRY *link;
  ULONG used=0u,total=0u,i;
  if (state->ProcessReceiptQpc && now-state->ProcessReceiptQpc<2*frequency.QuadPart)
    return FALSE;
  state->ProcessReceiptQpc=now;
  RtlZeroMemory(out,ADMISSION_G3_PROCESS_RECEIPT_WORDS*sizeof(ULONG));
  AppleAgxG3PrivatePoolStats(&state->PrivatePool,0ULL,&stats);
  out[0]=2u;out[2]=stats.GlobalUnits;out[3]=stats.LargestFreeUnits;
  out[4]=(ULONG)now;out[5]=(ULONG)((ULONGLONG)now>>32);out[6]=(ULONG)frequency.QuadPart;
  for (link=state->Processes.Flink;link!=&state->Processes;link=link->Flink) {
    ADMISSION_G3_PROCESS *p=CONTAINING_RECORD(link,ADMISSION_G3_PROCESS,Link);
    const APPLE_AGX_RENDER_MANAGER_STATE *m=&p->FirmwareManager;
    ADMISSION_G3_PRIVATE_SCENE *s;
    ULONG *r,slot,scenes=0u,cached=0u;
    ++total;
    AppleAgxG3PrivatePoolStats(&state->PrivatePool,p->Graph.ProcessId,&stats);
    if (!stats.OwnerUnits && !p->MaxTvbBlocks) continue;
    if (used<ADMISSION_G3_PROCESS_RECEIPT_SLOTS) slot=used++;
    else {
      for (slot=0u,i=1u;i<used;++i)
        if (out[8u+16u*i+1u]<out[8u+16u*slot+1u]) slot=i;
      if (stats.OwnerUnits<=out[8u+16u*slot+1u]) continue;
    }
    for (s=p->PrivateScenes;s;s=s->Next) { ++scenes; if (s->Cached) ++cached; }
    r=&out[8u+16u*slot];
    r[0]=p->OsProcessId;r[1]=stats.OwnerUnits;r[2]=scenes|(cached<<16);
    r[3]=p->MaxTvbBlocks;r[4]=p->MaxShape;
    r[5]=p->PrivateManager.Blocks;r[6]=m->Valid ? 1u : 0u;
    r[7]=AdmissionG3ManagerWord(m->Stats,0x0u);r[8]=AdmissionG3ManagerWord(m->Stats,0x4u);
    r[9]=AdmissionG3ManagerWord(m->Stats,0x8u);r[10]=AdmissionG3ManagerWord(m->Stats,0xcu);
    r[11]=AdmissionG3ManagerWord(m->Stats,0x20u);
    r[12]=AdmissionG3ManagerWord(m->Info,0x28u);r[13]=AdmissionG3ManagerWord(m->Info,0x30u);
    r[14]=AdmissionG3ManagerWord(m->BlockControl,0x0u);
    r[15]=AdmissionG3ManagerWord(m->BlockControl,0x4u);
    /* rc80 version 2 tail, same slot: window blocks backed | registered leaf
     * mask << 16 | window reserved << 31; growth steps | refused steps << 16;
     * last target | last reason << 24; Stats max_pages it was sized from. */
    r=&out[8u+16u*ADMISSION_G3_PROCESS_RECEIPT_SLOTS+
           ADMISSION_G3_PROCESS_RECEIPT_TVB_WORDS*slot];
    r[0]=(p->Tvb.LeaseBlocks&0xffffu)|((p->Tvb.Leaves&0xffu)<<16)|
         (p->TvbVa ? 0x80000000u : 0u);
    r[1]=(p->Tvb.Grows&0xffffu)|((p->Tvb.Failures&0xffffu)<<16);
    r[2]=(p->Tvb.LastTarget&0xffffffu)|((p->Tvb.LastReason&0xffu)<<24);
    r[3]=p->Tvb.LastUsedPages;
  }
  out[1]=used;out[7]=total;
  return TRUE;
}

NTSTATUS AdmissionGpuvaG3PrivateEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  APPLE_AGX_G3_PRIVATE_REQUEST q;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *p;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_G3_PRIVATE_SCENE *scene;
  ADMISSION_BACKEND_MEMORY_VIEW view;
  APPLE_AGX_G4_NATIVE_RENDER render;
  NTSTATUS status=STATUS_INVALID_PARAMETER;
  BOOLEAN fresh;
  UINT i;
  UINT wait_ms;
  ULONGLONG deadline;
  LARGE_INTEGER delay;
  UINT required[9]={0}, tablePredicate=0u, mapOffset=0u;
  APPLE_AGX_G3_PRIVATE_POOL_STATS observed={0};
  APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC prepare={0};
  BOOLEAN captured=FALSE, snapshot=FALSE, growWaited=FALSE;
  ULONGLONG growDeadline;
  ULONG receipt[ADMISSION_G3_PROCESS_RECEIPT_WORDS], pressure[12];
  ULONG pressureBefore;
#define PRIVATE_CAPTURE(b,t,o) do { \
  if (AdmissionG3CapturePrivateFailure(adapter,state,p,args,&q,required,b, \
      status,tablePredicate,mapOffset,t,&prepare,o)) captured=TRUE; \
} while (0)
  if (!adapter || !adapter->Started || !args ||
      KeGetCurrentIrql()!=PASSIVE_LEVEL || (args->Flags.Value&~1u)!=0u ||
      args->PrivateDriverDataSize!=sizeof(q) || !args->pPrivateDriverData)
    return STATUS_INVALID_PARAMETER;
  RtlCopyMemory(&q,args->pPrivateDriverData,sizeof(q));
  if (q.Magic!=APPLE_AGX_G3_PRIVATE_MAGIC || q.Version!=1u ||
      q.Bytes!=sizeof(q) || q.Reserved[0] || q.Reserved[1]) return status;
  for (i=0;i<9;++i)
    if (q.Ranges[i].Va || q.Ranges[i].Bytes || q.Ranges[i].Reserved) return status;
  state=(ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (!state) return STATUS_INVALID_DEVICE_STATE;
  /* R157: ACQUIRE/PREPARE map private tables, which is not allowed while this
   * process has a native job in flight. Wait (lock released, bounded above
   * TdrDelay) for joined completion instead of refusing with busy. */
  /* EXP1138: with two submissions in flight these 1 ms timer sleeps cost
   * DWM ~1 s per 15.9 s (1236 waits); wake on the job's end instead, with
   * the same 1 ms backstop and a 3 s wall-clock bound. */
  deadline=KeQueryInterruptTime()+3000ULL*10000ULL;
  /* rc80: a cached scene returns at once unless a TVB growth step is due;
   * then wait (at most 50 ms) for the idle point growth needs. */
  growDeadline=KeQueryInterruptTime()+50ULL*10000ULL;
  for (wait_ms=0u;;++wait_ms) {
    KeClearEvent(&state->JobEvent);
    ExAcquireFastMutex(&state->Lock);
    p=AdmissionGpuvaG3FindProcess(state,args->hKmdProcessHandle);
    if (!p || q.Operation==APPLE_AGX_G3_PRIVATE_RELEASE ||
        !p->Graph.JobInFlight ||
        KeQueryInterruptTime()>=deadline) break;
    if (AdmissionG3PrivateCachedScene(p,args->hContext,&q)) {
      if (KeQueryInterruptTime()>=growDeadline ||
          !AdmissionG3TvbWantsIdle(p,AppleAgxG4TvbBlocksForSize(q.Width,q.Height)))
        break;
      growWaited=TRUE;
    }
    ExReleaseFastMutex(&state->Lock);
    delay.QuadPart=-10000LL;
    (void)KeWaitForSingleObject(&state->JobEvent,Executive,KernelMode,FALSE,&delay);
  }
  pressureBefore=state->PrivatePressure[1];
  if (!p || p->Poisoned || p->Graph.Uncertain) {PRIVATE_CAPTURE(1u,~0u,NULL);goto Done;}
  /* Compare handles against attached objects before dereferencing them. */
  for (context=p->Contexts;context && (HANDLE)context!=args->hContext;
       context=context->GpuvaG3NextContext) {}
  if (!context || context->GpuvaG3Closing || !context->Win32Transport || context->GpuvaG3Poisoned ||
      context->Object.Magic!=ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device==NULL ||
      (HANDLE)CONTAINING_RECORD(context->Object.Device,ADMISSION_DEVICE,Object)!=args->hDevice ||
      context->Object.Device->Adapter!=&adapter->ObjectAdapter) {PRIVATE_CAPTURE(2u,~0u,NULL);goto Done;}
  /* Software-entry preparation must not reap or mutate tables after its
   * bounded quiescence wait timed out. The broker also checks owner jobs. */
  if (q.Operation!=APPLE_AGX_G3_PRIVATE_RELEASE &&
      !AdmissionG3PrivateCachedScene(p,args->hContext,&q) &&
      p->Graph.JobInFlight)
    {status=STATUS_DEVICE_BUSY;PRIVATE_CAPTURE(7u,~0u,NULL);goto Done;}
  if (!AdmissionG3PrivateReap(p)) {status=STATUS_DEVICE_HARDWARE_ERROR;PRIVATE_CAPTURE(3u,~0u,NULL);goto Done;}
  status=AdmissionMemoryRuntimePrivateView(adapter,&view);
  if (!NT_SUCCESS(status)) {PRIVATE_CAPTURE(4u,~0u,NULL);goto Done;}
  status=STATUS_INVALID_PARAMETER;
  if (q.Operation==APPLE_AGX_G3_PRIVATE_RELEASE) {
    if (q.Width || q.Height || q.UtileWidth || q.UtileHeight || q.Layers || q.Samples)
      goto Done;
    for (scene=p->PrivateScenes;scene;scene=scene->Next)
      if (scene->Storage.Generation==q.SceneId) break;
    if (!scene || scene->Context!=context || q.SceneGeneration!=scene->Storage.Generation ||
        q.ManagerId!=p->Graph.ProcessId || q.ManagerGeneration!=p->PrivateManager.Generation)
      goto Done;
    scene->ReleaseRequested=1u;
    /* RELEASE transfers ownership to the KMD. As with a queued scene, a
     * submission reference or another job/lease of this owner defers actual
     * unmap/free to the existing completion/acquire/context reaper. Never
     * require Windows to drain unrelated GPU work to acknowledge a release. */
    if (scene->Queued || scene->Submitting)
      ++state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_RELEASE_QUEUED];
    else if (AdmissionG3PrivateCacheScene(p,scene))
      ++state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_CACHED_RELEASE];
    if (scene->Queued || scene->Submitting || scene->Cached ||
        p->Graph.JobInFlight)
      { status=STATUS_SUCCESS;goto Done; }
    status=AdmissionG3PrivateReleaseScene(p,scene,&view) ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    goto Done;
  }
  if (q.SceneId || q.SceneGeneration || q.Width>16384u || q.Height>16384u ||
      q.Layers!=1u || (q.Samples!=1u && q.Samples!=2u && q.Samples!=4u) ||
      q.UtileWidth>32u || q.UtileHeight>32u)
    {PRIVATE_CAPTURE(5u,~0u,NULL);goto Done;}
  if (q.Operation==APPLE_AGX_G3_PRIVATE_ACQUIRE) {
    if (q.ManagerId || q.ManagerGeneration) {PRIVATE_CAPTURE(6u,~0u,NULL);goto Done;}
  } else if (q.Operation==APPLE_AGX_G3_PRIVATE_PREPARE) {
    if (!q.ManagerGeneration || q.ManagerId!=p->Graph.ProcessId ||
        q.ManagerGeneration!=p->PrivateManager.Generation ||
        context->GpuvaG3PrivateManagerGeneration!=q.ManagerGeneration) goto Done;
  } else goto Done;
  {
    ULONG blocks=AppleAgxG4MinTvbBlocks(q.Width,q.Height);
    if (blocks>p->MaxTvbBlocks) { p->MaxTvbBlocks=blocks;p->MaxShape=q.Width|(q.Height<<16); }
  }
  /* rc80: grow the TVB while this process is idle (Asahi auto_grow and
   * ensure_blocks before the scene). A cached hit that waited and still
   * found the process busy holds the step off instead of waiting again. */
  if (!p->Graph.JobInFlight) {
    AdmissionG3TvbGrow(p,&view,AppleAgxG4TvbBlocksForSize(q.Width,q.Height));
    if (p->Poisoned || p->Graph.Uncertain)
      {status=STATUS_DEVICE_HARDWARE_ERROR;PRIVATE_CAPTURE(14u,2u,NULL);goto Done;}
  } else if (growWaited)
    AppleAgxG3TvbNoteResult(&p->Tvb,AdmissionG3TvbTargetFor(p,
        AppleAgxG4TvbBlocksForSize(q.Width,q.Height)),p->PrivateManager.Blocks,
        p->PrivateManager.Blocks,APPLE_AGX_G3_TVB_FAIL_BUSY,AdmissionG3TvbNowMs());
  scene=AdmissionG3PrivateCachedScene(p,args->hContext,&q);
  if (scene) {
    if (!AdmissionG3PrivateReuseScene(p,scene,&view))
      {status=STATUS_DEVICE_HARDWARE_ERROR;goto Done;}
    ++state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_HIT];
    q.ManagerId=p->Graph.ProcessId;q.ManagerGeneration=p->PrivateManager.Generation;
    q.SceneId=q.SceneGeneration=scene->Storage.Generation;
    RtlCopyMemory(q.Ranges,scene->Storage.Ranges,sizeof(q.Ranges));
    context->GpuvaG3PrivateManagerGeneration=q.ManagerGeneration;
    RtlCopyMemory(args->pPrivateDriverData,&q,sizeof(q));status=STATUS_SUCCESS;goto Done;
  }
  RtlZeroMemory(&render,sizeof(render));
  render.WidthPx=(USHORT)q.Width;render.HeightPx=(USHORT)q.Height;
  render.UtileWidthPx=(UCHAR)q.UtileWidth;render.UtileHeightPx=(UCHAR)q.UtileHeight;
  render.Layers=1;render.Samples=1;
  if (!AppleAgxG4ProcessRequiredBytes(&render,required)) {PRIVATE_CAPTURE(8u,~0u,NULL);goto Done;}
  if (q.Operation==APPLE_AGX_G3_PRIVATE_ACQUIRE) {
    ADMISSION_G3_PRIVATE_SCENE *s;
    UINT cached=0u,same_context=0u;
    for (s=p->PrivateScenes;s;s=s->Next)
      if (s->Cached) { ++cached; if (s->Context==context) ++same_context; }
    ++state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS];
    if (same_context) ++state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS_OTHER_GEOMETRY];
    state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE+2]=
        state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE];
    state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE+3]=
        state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE+1];
    state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE]=
        (ULONGLONG)(q.Width|(q.Height<<16));
    state->PrivateStats[ADMISSION_G3_PRIVATE_STAT_MISS_SHAPE+1]=
        (ULONGLONG)(q.UtileWidth|(q.UtileHeight<<8)|(cached<<16));
    if (!AdmissionG3PrivateTrimCache(p,&view)) {status=STATUS_DEVICE_HARDWARE_ERROR;goto Done;}
  }
  status=AdmissionG3PrivateTables(p,&view,&tablePredicate,&observed);
  while (status==STATUS_INSUFFICIENT_RESOURCES && tablePredicate<=2u &&
      AdmissionG3PrivateEvictPressure(p,&view,0x10000u,&observed)) {
    tablePredicate=0u;
    status=AdmissionG3PrivateTables(p,&view,&tablePredicate,&observed);
  }
  if (!NT_SUCCESS(status)) {PRIVATE_CAPTURE(9u,~0u,&observed);goto Done;}
  scene=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*scene),ADMISSION_POOL_TAG);
  if (!scene) {status=STATUS_INSUFFICIENT_RESOURCES;PRIVATE_CAPTURE(10u,~0u,NULL);goto Done;}
  RtlZeroMemory(scene,sizeof(*scene));scene->Context=context;scene->Geometry=render;
  fresh=p->PrivateManager.Generation==0;
  if (!fresh && AppleAgxG4MinTvbBlocks(render.WidthPx,render.HeightPx)<=
      APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS) {
    UINT need=AppleAgxG4MinTvbBlocks(render.WidthPx,render.HeightPx);
    status=AdmissionG3GrowPrivateHeap(p,&view,need,&mapOffset);
    while (status==STATUS_INSUFFICIENT_RESOURCES) {
      UINT bytes=(need-p->PrivateManager.Blocks)*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK;
      AppleAgxG3PrivatePoolStats(&state->PrivatePool,p->Graph.ProcessId,&observed);
      if (!AdmissionG3PrivateEvictPressure(p,&view,bytes,&observed)) break;
      status=AdmissionG3GrowPrivateHeap(p,&view,need,&mapOffset);
    }
    if (!NT_SUCCESS(status)) {
      PRIVATE_CAPTURE(14u,2u,NULL);ExFreePoolWithTag(scene,ADMISSION_POOL_TAG);goto Done;
    }
  }
  status=AdmissionG3PreparePrivateStorageObserved(p,&render,&p->PrivateManager,&scene->Storage,&prepare);
  while (status==STATUS_INSUFFICIENT_RESOURCES && prepare.Predicate==3u &&
      prepare.FailedRange<9u && AdmissionG3PrivateEvictPressure(p,&view,
          fresh && prepare.FailedRange==2u ?
              AppleAgxG4MinTvbBlocks(render.WidthPx,render.HeightPx)*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK :
              required[prepare.FailedRange],&prepare.Stats)) {
    RtlZeroMemory(&prepare,sizeof(prepare));
    status=AdmissionG3PreparePrivateStorageObserved(p,&render,&p->PrivateManager,&scene->Storage,&prepare);
  }
  if (!NT_SUCCESS(status)) {PRIVATE_CAPTURE(11u,~0u,NULL);ExFreePoolWithTag(scene,ADMISSION_POOL_TAG);goto Done;}
  scene->Next=p->PrivateScenes;p->PrivateScenes=scene;
  if (fresh)
    for (i=0;i<3;++i)
      if (!AdmissionG3PrivateMapExtentObserved(p,&view,&p->PrivateManager.Extents[i],TRUE,&mapOffset)) {
        status=STATUS_INSUFFICIENT_RESOURCES;PRIVATE_CAPTURE(12u,i,NULL);break;
      }
  if (!fresh || i==3u) {
    for (i=0;i<6;++i)
      if (!AdmissionG3PrivateMapExtentObserved(p,&view,&scene->Storage.Extents[i],TRUE,&mapOffset)) {
        status=STATUS_INSUFFICIENT_RESOURCES;PRIVATE_CAPTURE(13u,i+3u,NULL);break;
      }
    if (i==6u) {
      q.ManagerId=p->Graph.ProcessId;q.ManagerGeneration=p->PrivateManager.Generation;
      q.SceneId=q.SceneGeneration=scene->Storage.Generation;
      RtlCopyMemory(q.Ranges,scene->Storage.Ranges,sizeof(q.Ranges));
      context->GpuvaG3PrivateManagerGeneration=q.ManagerGeneration;
      RtlCopyMemory(args->pPrivateDriverData,&q,sizeof(q));status=STATUS_SUCCESS;goto Done;
    }
  }
  status=STATUS_INSUFFICIENT_RESOURCES;
  if (!AdmissionG3PrivateReleaseScene(p,scene,&view)) {ADMISSION_G3_POISON(p,1u);goto Done;}
  if (fresh) {
    UINT extents=AppleAgxG3PrivateManagerExtentCount(&p->PrivateManager);
    for (i=0;i<extents;++i)
      if (!AdmissionG3PrivateMapExtent(p,&view,
              AppleAgxG3PrivateManagerExtent(&p->PrivateManager,i),FALSE)) {
        ADMISSION_G3_POISON(p,1u);goto Done;
      }
    for (i=0;i<extents;++i)
      (void)AdmissionG3PrivateFreeExtent(p,&view,
          AppleAgxG3PrivateManagerExtent(&p->PrivateManager,i));
    RtlZeroMemory(&p->PrivateManager,sizeof(p->PrivateManager));
    RtlZeroMemory(&p->FirmwareManager,sizeof(p->FirmwareManager));
  }
Done:
  if (p && p->Graph.Uncertain) ADMISSION_G3_POISON(p,1u);
  snapshot=AdmissionG3SnapshotProcesses(state,receipt);
  RtlCopyMemory(pressure,state->PrivatePressure,sizeof(pressure));
  ExReleaseFastMutex(&state->Lock);
  if (pressure[1]!=pressureBefore) AdmissionRecordG3PrivatePressure(adapter,pressure,sizeof(pressure));
  if (captured) AdmissionRecordG3PrivateFailure(adapter);
  if (snapshot) AdmissionRecordG3PrivateProcesses(adapter,receipt,sizeof(receipt));
#undef PRIVATE_CAPTURE
  return status;
}

/* The private pool's per-process VA window is the reserved span. */
C_ASSERT(APPLE_AGX_G4_PRIVATE_VA_BYTES == APPLE_AGX_G3_PRIVATE_VA_BYTES);
/* rc80: the TVB window follows the private window in the same root slot. */
C_ASSERT((APPLE_AGX_G4_PRIVATE_VA_BASE >> 36) ==
         ((APPLE_AGX_G4_PRIVATE_VA_BASE + APPLE_AGX_G4_PRIVATE_VA_BYTES +
           APPLE_AGX_G3_TVB_VA_BYTES - 1ULL) >> 36));

_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateProcess(
    PVOID MiniportDeviceContext, DXGKARG_CREATEPROCESS *Args) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_SCANOUT_MEMORY_VIEW local_view;
  NTSTATUS status;
  KIRQL irql = KeGetCurrentIrql();
  AdmissionRecordGpuvaG3CreateInput(
      adapter == NULL ? NULL : adapter->PhysicalDeviceObject, Args,
      adapter != NULL && adapter->Started, irql);
  if (adapter == NULL || Args == NULL || !adapter->Started ||
      irql != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
  process = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*process),
                            ADMISSION_POOL_TAG);
  if (process == NULL) return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(process, sizeof(*process));
  process->OsProcessId = HandleToULong(PsGetCurrentProcessId());
  process->State = state;
  process->Magic = ADMISSION_G3_PROCESS_MAGIC;
  process->CpuOnlyMappings = Args->Flags.SystemProcess != 0u;
  /* VidMm permits this callback only inside CreateProcess at PASSIVE_LEVEL.
   * Reserve VA/metadata now; private data is committed lazily by native render.
   * The OS releases the reservation with the process, including failed create. */
  if (adapter->Interface.DxgkCbReserveGpuVirtualAddressRange == NULL) {
    status = STATUS_NOT_SUPPORTED;
    goto Fail;
  }
  {
    DXGKARGCB_RESERVEGPUVIRTUALADDRESSRANGE reserve = {0};
    reserve.hDxgkProcess = Args->hDxgkProcess;
    reserve.SizeInBytes = APPLE_AGX_G4_PRIVATE_VA_BYTES;
    reserve.Alignment = (UINT)APPLE_AGX_G4_PRIVATE_VA_BYTES;
    /* G2 HW2: the top leaf span of the USC window, so kernels the KMD
     * places there execute (d3dkmddi.h: BaseAddress aligned to the leaf
     * coverage). The UMD may not map into it (AllowUserModeMapping 0). */
    reserve.BaseAddress = APPLE_AGX_G4_PRIVATE_VA_BASE;
    status = adapter->Interface.DxgkCbReserveGpuVirtualAddressRange(
        adapter->Interface.DeviceHandle, &reserve);
    /* Validate the OS-owned reservation in leaf-table spans (32 MiB): it
     * must be the requested span. */
    if (NT_SUCCESS(status) &&
        (reserve.StartVirtualAddress != APPLE_AGX_G4_PRIVATE_VA_BASE ||
         reserve.StartVirtualAddress < (1ULL << 25) ||
         reserve.StartVirtualAddress >= (1ULL << 39) ||
         (reserve.StartVirtualAddress & (0x02000000ULL - 1ULL)) ||
         reserve.SizeInBytes > (1ULL << 39) - reserve.StartVirtualAddress))
      status = STATUS_INVALID_ADDRESS;
    {
      ULONG receipt[ADMISSION_G3_RESERVE_RECEIPT_WORDS];
      receipt[0] = 1u;
      receipt[1] = (ULONG)(NT_SUCCESS(status) ?
          InterlockedIncrement(&state->ReserveMade) :
          InterlockedCompareExchange(&state->ReserveMade, 0, 0));
      receipt[2] = (ULONG)(NT_SUCCESS(status) ?
          InterlockedCompareExchange(&state->ReserveRefused, 0, 0) :
          InterlockedIncrement(&state->ReserveRefused));
      receipt[3] = (ULONG)status;
      receipt[4] = (ULONG)reserve.StartVirtualAddress;
      receipt[5] = (ULONG)(reserve.StartVirtualAddress >> 32);
      AdmissionRecordG3Reservation(adapter, receipt);
    }
    if (!NT_SUCCESS(status)) goto Fail;
    process->PrivateVa = reserve.StartVirtualAddress;
    process->DxgkProcess = Args->hDxgkProcess;
  }
  {
    /* rc80: the TVB window starts where the private window ends, in the same
     * root slot, so its leaves hang below the same middle table and the heap
     * stays VA-contiguous (apple_agx_g3_tvb.h). Fail-soft: without it the
     * heap keeps the 32-block floor. */
    DXGKARGCB_RESERVEGPUVIRTUALADDRESSRANGE reserve = {0};
    ULONGLONG base = process->PrivateVa + APPLE_AGX_G3_PRIVATE_VA_BYTES;
    reserve.hDxgkProcess = Args->hDxgkProcess;
    reserve.SizeInBytes = APPLE_AGX_G3_TVB_VA_BYTES;
    reserve.Alignment = (UINT)APPLE_AGX_G3_TVB_LEAF_BYTES;
    reserve.BaseAddress = base;
    if (NT_SUCCESS(adapter->Interface.DxgkCbReserveGpuVirtualAddressRange(
            adapter->Interface.DeviceHandle, &reserve)) &&
        reserve.StartVirtualAddress == base &&
        reserve.SizeInBytes >= APPLE_AGX_G3_TVB_VA_BYTES)
      process->TvbVa = base;
  }
  status = AdmissionG3BootstrapRoot(process);
  if (!NT_SUCCESS(status)) goto Fail;
  status = AdmissionMemoryRuntimeLocalView(adapter, &local_view);
  if (!NT_SUCCESS(status)) goto Fail;
  if (local_view.GuestIpaAddress == 0ULL ||
      local_view.Bytes < 0x4000ULL) {
    status = STATUS_INVALID_DEVICE_STATE;
    goto Fail;
  }
  ExAcquireFastMutex(&state->Lock);
  if (state->NextProcessId == MAXULONGLONG) {
    ExReleaseFastMutex(&state->Lock);
    status = STATUS_INTEGER_OVERFLOW;
    goto Fail;
  }
  ++state->NextProcessId;
  if (!AppleAgxGpuvaG3GraphInit(&process->Graph, &state->Client,
      state->NextProcessId, 1ULL, AdmissionG3AllocateNode,
      AdmissionG3FreeNode, NULL)) {
    ExReleaseFastMutex(&state->Lock);
    status = STATUS_INVALID_DEVICE_STATE;
    goto Fail;
  }
  /* All VidMm local pages belong to this one contiguous reservation.  Its
   * IPA base is stable for the broker epoch and common to every process. */
  process->Graph.SharedBackingGeneration = local_view.GuestIpaAddress;
  process->Graph.Registry = &state->Registry;
  process->Graph.InvalidateNative = AdmissionG3NativeInvalidate;
  process->Graph.InvalidateContext = process;
  if (!AppleAgxGpuvaG3GraphCreate(&process->Graph, process->BootstrapIpa,
                                  Args->Flags.SystemProcess != 0u)) {
    if (process->Graph.Uncertain) {
      ADMISSION_G3_POISON(process,1u);
      InsertTailList(&state->Processes, &process->Link);
      ++state->ProcessCount;
      ExReleaseFastMutex(&state->Lock);
      return STATUS_DEVICE_HARDWARE_ERROR;
    }
    ExReleaseFastMutex(&state->Lock);
    status = STATUS_DEVICE_HARDWARE_ERROR;
    goto Fail;
  }
  process->Graph.PrivateCached = adapter->GpuPrivateCached ? 1u : 0u;
  /* V1: with Wom1G3RetainedSlots the process keeps its VM slot between
   * jobs; without it the graph stays unattached (lease per job). */
  if (adapter->G3RetainedSlots &&
      AppleAgxGpuvaG3SlotsEnable(&state->Slots, ADMISSION_G3_RETAINED_FIRST_SLOT,
                                 ADMISSION_G3_RETAINED_SLOT_COUNT))
    process->Graph.SlotTable = &state->Slots;
  if (adapter->LiveAdditivePaging &&
      !AppleAgxGpuvaG3GraphNegotiateLive(&process->Graph, true)) {
    /* A topology already exposed to VidMm cannot change per process. */
    if (!AppleAgxGpuvaG3GraphDestroy(&process->Graph)) {
      ADMISSION_G3_POISON(process,1u);
      InsertTailList(&state->Processes, &process->Link);
      ++state->ProcessCount;
      ExReleaseFastMutex(&state->Lock);
      return STATUS_DEVICE_HARDWARE_ERROR;
    }
    ExReleaseFastMutex(&state->Lock);
    status = STATUS_NOT_SUPPORTED;
    goto Fail;
  }
  InsertTailList(&state->Processes, &process->Link);
  ++state->ProcessCount;
  ExReleaseFastMutex(&state->Lock);
  Args->hKmdProcess = process;
  return STATUS_SUCCESS;
Fail:
  if (process->BootstrapRoot.AllocationHandle != NULL)
    (void)AppleAgxMemoryRelease(&process->Io, &process->BootstrapRoot);
  ExFreePoolWithTag(process, ADMISSION_POOL_TAG);
  return status;
}

#ifdef APPLE_AGX_NATIVE_STAGING
NTSTATUS AdmissionGpuvaG3NativeStagingEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  ADMISSION_NATIVE_STAGING_REQUEST q;
  ADMISSION_DEVICE *device;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_G3_STATE *state;
  NTSTATUS status;
  BOOLEAN valid;
  if (!adapter || !adapter->Started || !args || args->Flags.Value ||
      KeGetCurrentIrql() != PASSIVE_LEVEL || !args->pPrivateDriverData ||
      args->PrivateDriverDataSize != sizeof(q)) return STATUS_INVALID_PARAMETER;
  RtlCopyMemory(&q, args->pPrivateDriverData, sizeof(q));
  if (q.Magic != ADMISSION_NATIVE_STAGING_MAGIC || q.Version != 1u ||
      q.Bytes != sizeof(q) || q.UserVa ||
      (q.Capacity ? q.Token != 0ULL : q.Token == 0ULL) ||
      (q.Capacity & 65535u) || q.Capacity > ADMISSION_NATIVE_STAGING_MAX_BYTES)
    return STATUS_INVALID_PARAMETER;
  device = (ADMISSION_DEVICE *)args->hDevice;
  context = (ADMISSION_RENDER_CONTEXT *)args->hContext;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (!state || !device || !context ||
      device->Object.Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      device->Object.Adapter != &adapter->ObjectAdapter ||
      context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device != &device->Object) return STATUS_INVALID_HANDLE;
  ExAcquireFastMutex(&state->Lock);
  process = AdmissionGpuvaG3FindProcess(state, device->GpuvaG3Process);
  valid = process && process == context->GpuvaG3Process &&
      process->NativeMapRefs != MAXULONG &&
      process->OsProcessId == HandleToULong(PsGetCurrentProcessId()) &&
      (!q.Capacity || (!process->NativeClosing && !process->Poisoned &&
                       !process->Graph.Uncertain));
  if (valid) ++process->NativeMapRefs;
  ExReleaseFastMutex(&state->Lock);
  if (!valid) return STATUS_INVALID_HANDLE;
  /* Hold our own process reference through manager registration and cleanup;
   * do not infer this lifetime from an OS handle supplied to a Level-0 DDI.
   * No GPU/pool lock may cover the process-notification registry. */
  status = q.Capacity ? AdmissionNativeStagingMap(adapter, process, q.Capacity,
      &q.Token, &q.UserVa) : AdmissionNativeStagingCancel(adapter, process, q.Token);
  ExAcquireFastMutex(&state->Lock);
  if (q.Capacity && NT_SUCCESS(status) && process->NativeClosing) {
    ExReleaseFastMutex(&state->Lock);
    AdmissionNativeStagingUnmapProcess(adapter, process);
    status = STATUS_DEVICE_BUSY;
    ExAcquireFastMutex(&state->Lock);
  }
  /* Publish the buffered reply before releasing our reference, in the same
   * critical section as the final closing check. */
  if (NT_SUCCESS(status)) RtlCopyMemory(args->pPrivateDriverData, &q, sizeof(q));
  --process->NativeMapRefs;
  ExReleaseFastMutex(&state->Lock);
  return status;
}
#endif

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyProcess(
    PVOID MiniportDeviceContext, HANDLE KmdProcessHandle) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  if (adapter == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
#ifdef APPLE_AGX_NATIVE_STAGING
  /* Close admission before dropping the GPU lock. An escape may still be
   * preparing a mapping and must finish cleanup before this object can die. */
  ExAcquireFastMutex(&state->Lock);
  process = AdmissionGpuvaG3FindProcess(state, KmdProcessHandle);
  if (process == NULL) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_INVALID_HANDLE;
  }
  process->NativeClosing = TRUE;
  ExReleaseFastMutex(&state->Lock);
  /* CPU teardown is independent of a later uncertain GPU retirement. */
  AdmissionNativeStagingUnmapProcess(adapter, KmdProcessHandle);
  if (!AdmissionNativeStagingProcessIdle(adapter, KmdProcessHandle))
    return STATUS_DEVICE_BUSY;
#endif
  ExAcquireFastMutex(&state->Lock);
  process = AdmissionGpuvaG3FindProcess(state, KmdProcessHandle);
  if (process == NULL) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_INVALID_HANDLE;
  }
  {
    ADMISSION_G3_PRIVATE_SCENE *s;
    for (s=process->PrivateScenes;s;s=s->Next)
      if (s->Queued || s->Quarantined) {
        ExReleaseFastMutex(&state->Lock);return STATUS_DEVICE_BUSY;
      }
  }
  if (process->DeviceRefs || process->ContextRefs ||
#ifdef APPLE_AGX_NATIVE_STAGING
      process->NativeMapRefs ||
#endif
      process->Graph.Uncertain ||
      (process->Graph.Created &&
       !AppleAgxGpuvaG3GraphDestroy(&process->Graph))) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_DEVICE_BUSY;
  }
  if (!AdmissionG3PrivateDestroyStorage(process)) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_DEVICE_BUSY;
  }
  while (process->TableShadows != NULL) {
    ADMISSION_G3_TABLE_SHADOW *entry = process->TableShadows;
    if (AppleAgxMemoryRelease(&process->Io, &entry->Memory) !=
        AppleAgxMemoryResultOk) {
      ExReleaseFastMutex(&state->Lock);
      return STATUS_DEVICE_BUSY;
    }
    process->TableShadows = entry->Next;
    {
      ULONG bucket = (ULONG)((entry->BrokerIpa >> 14) ^
          (entry->BrokerIpa >> 22) ^ (entry->BrokerIpa >> 30)) & 255u;
      ADMISSION_G3_TABLE_SHADOW **link =
          &process->TableShadowBrokerBuckets[bucket];
      while (*link != NULL && *link != entry) link = &(*link)->NextBroker;
      if (*link == entry) *link = entry->NextBroker;
    }
    if (entry->ResidentPtes != NULL) {
      for (UINT i = 0u; i < 8192u; ++i)
        if (entry->ResidentPtes[i].Flags &&
            ADMISSION_G3_FRAME_SEGMENT(entry->ResidentPtes[i].SegmentId))
          AppleAgxGpuvaG3MappingRelease(&process->Graph,
              entry->ResidentPtes[i].GuestIpa);
      ExFreePoolWithTag(entry->ResidentPtes, ADMISSION_POOL_TAG);
    }
    if (entry->LogicalPtes != NULL)
      ExFreePoolWithTag(entry->LogicalPtes, ADMISSION_POOL_TAG);
    ExFreePoolWithTag(entry, ADMISSION_POOL_TAG);
  }
  if (process->BootstrapRoot.AllocationHandle != NULL &&
      AppleAgxMemoryRelease(&process->Io, &process->BootstrapRoot) !=
          AppleAgxMemoryResultOk) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_DEVICE_BUSY;
  }
  RemoveEntryList(&process->Link);
  --state->ProcessCount;
  process->Magic = 0u;
  ExReleaseFastMutex(&state->Lock);
#ifdef APPLE_AGX_NATIVE_STAGING
  AdmissionNativeStagingDestroyProcess(adapter, process);
#endif
  ExFreePoolWithTag(process, ADMISSION_POOL_TAG);
  return STATUS_SUCCESS;
}

NTSTATUS AdmissionGpuvaG3AttachDevice(ADMISSION_CONTEXT *adapter,
    ADMISSION_DEVICE *device, HANDLE handle) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  if (adapter == NULL || device == NULL) return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
  if (handle == NULL) return STATUS_SUCCESS;
  ExAcquireFastMutex(&state->Lock);
  process = AdmissionGpuvaG3FindProcess(state, handle);
  if (process == NULL || process->Poisoned || process->Graph.Uncertain ||
      process->DeviceRefs == MAXULONG) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_INVALID_HANDLE;
  }
  ++process->DeviceRefs;
  device->GpuvaG3Process = process;
  ExReleaseFastMutex(&state->Lock);
  return STATUS_SUCCESS;
}

VOID AdmissionGpuvaG3DetachDevice(ADMISSION_DEVICE *device) {
  ADMISSION_G3_PROCESS *process;
  if (device == NULL || device->GpuvaG3Process == NULL) return;
  process = (ADMISSION_G3_PROCESS *)device->GpuvaG3Process;
  ExAcquireFastMutex(&process->State->Lock);
  if (process->DeviceRefs) --process->DeviceRefs;
  device->GpuvaG3Process = NULL;
  ExReleaseFastMutex(&process->State->Lock);
}

/* G2 HW2b: the KMD's present arena is an extent of the process's private
 * range, which lies in the USC window, so the present kernels execute in
 * the presenting process's VM. It is made while the process is idle (no
 * job, no lease: the leaf may change) and stays published until the
 * process ends (AdmissionG3PrivateDestroyStorage frees every owner extent).
 * Predicates: 1-5 tables, 10 busy, 11 view, 12 pool, 13 kernels, 14 map. */
static NTSTATUS AdmissionG3PresentArenaEnsure(ADMISSION_G3_PROCESS *p,
    UINT *predicate) {
  ADMISSION_BACKEND_MEMORY_VIEW view;
  APPLE_AGX_G3_PRIVATE_POOL_STATS stats;
  APPLE_AGX_PRESENT_ARENA arena;
  NTSTATUS status;
  *predicate = 0u;
  if (p->PresentArena.Generation) return STATUS_SUCCESS;
  if (p->Poisoned || p->Graph.Uncertain || p->Graph.JobInFlight ||
      p->CpuOnlyMappings) {
    *predicate = 10u;
    return STATUS_DEVICE_BUSY;
  }
  status = AdmissionMemoryRuntimePrivateView(p->State->Adapter, &view);
  if (!NT_SUCCESS(status) || view.CpuAddress == NULL) {
    *predicate = 11u;
    return NT_SUCCESS(status) ? STATUS_INVALID_ADDRESS : status;
  }
  status = AdmissionG3PrivateTables(p, &view, predicate, &stats);
  if (!NT_SUCCESS(status)) return status;
  if (!AppleAgxG3PrivateAllocate(&p->State->PrivatePool, p->Graph.ProcessId,
          APPLE_AGX_PRESENT_ARENA_BYTES, &p->PresentArena)) {
    RtlZeroMemory(&p->PresentArena, sizeof(p->PresentArena));
    *predicate = 12u;
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  RtlZeroMemory((PUCHAR)view.CpuAddress + p->PresentArena.Offset,
                APPLE_AGX_PRESENT_ARENA_BYTES);
  arena.Cpu = (unsigned char *)view.CpuAddress + p->PresentArena.Offset;
  arena.Va = p->PrivateVa + p->PresentArena.VaOffset;
  arena.Bytes = APPLE_AGX_PRESENT_ARENA_BYTES;
  arena.UscBase = APPLE_AGX_G4_USC_EXECUTION_BASE;
  if (!AppleAgxPresentArenaInstall(&arena)) {
    (void)AdmissionG3PrivateFreeExtent(p, &view, &p->PresentArena);
    *predicate = 13u;
    return STATUS_INVALID_IMAGE_FORMAT;
  }
  KeMemoryBarrier();
  if (!AdmissionG3PrivateMapExtent(p, &view, &p->PresentArena, TRUE)) {
    /* A partly published arena must leave the leaf before its pages can
     * return to the pool; otherwise the process cannot be trusted. */
    if (!AdmissionG3PrivateMapExtent(p, &view, &p->PresentArena, FALSE) ||
        !AdmissionG3PrivateFreeExtent(p, &view, &p->PresentArena))
      ADMISSION_G3_POISON(p, 1u);
    *predicate = 14u;
    return p->Graph.Uncertain ? STATUS_DEVICE_HARDWARE_ERROR
                              : STATUS_INSUFFICIENT_RESOURCES;
  }
  return STATUS_SUCCESS;
}

NTSTATUS AdmissionGpuvaG3EnsurePresentArena(ADMISSION_RENDER_CONTEXT *context) {
  ADMISSION_G3_PROCESS *process;
  ULONG receipt[ADMISSION_G2_PRESENT_ARENA_RECEIPT_WORDS];
  UINT predicate = 0u;
  NTSTATUS status;
  BOOLEAN made;
  if (context == NULL || context->GpuvaG3Process == NULL) return STATUS_SUCCESS;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  ExAcquireFastMutex(&process->State->Lock);
  made = process->PresentArena.Generation == 0u;
  status = AdmissionG3PresentArenaEnsure(process, &predicate);
  made = made && NT_SUCCESS(status);
  receipt[0] = 1u;
  receipt[1] = (ULONG)(made ?
      InterlockedIncrement(&process->State->PresentArenasMade) :
      InterlockedCompareExchange(&process->State->PresentArenasMade, 0, 0));
  receipt[2] = (ULONG)(NT_SUCCESS(status) ?
      InterlockedCompareExchange(&process->State->PresentArenasRefused, 0, 0) :
      InterlockedIncrement(&process->State->PresentArenasRefused));
  receipt[3] = predicate;
  receipt[4] = (ULONG)status;
  receipt[5] = (ULONG)(process->PresentArena.Generation ?
      process->PrivateVa + process->PresentArena.VaOffset : 0ULL);
  receipt[6] = (ULONG)((process->PresentArena.Generation ?
      process->PrivateVa + process->PresentArena.VaOffset : 0ULL) >> 32);
  ExReleaseFastMutex(&process->State->Lock);
  if (made || !NT_SUCCESS(status))
    AdmissionRecordG2PresentArena(process->State->Adapter, receipt);
  return status;
}

NTSTATUS AdmissionGpuvaG3AttachContext(ADMISSION_RENDER_CONTEXT *context,
    ADMISSION_DEVICE *device) {
  ADMISSION_G3_PROCESS *process;
  if (context == NULL || device == NULL) return STATUS_INVALID_PARAMETER;
  process = (ADMISSION_G3_PROCESS *)device->GpuvaG3Process;
  if (process == NULL) return STATUS_SUCCESS;
  ExAcquireFastMutex(&process->State->Lock);
  if (process->Poisoned || process->Graph.Uncertain ||
      process->ContextRefs == MAXULONG) {
    ExReleaseFastMutex(&process->State->Lock);
    return STATUS_INVALID_DEVICE_STATE;
  }
  ++process->ContextRefs;
  context->GpuvaG3Process = process;
  context->GpuvaG3NextContext = process->Contexts;
  process->Contexts = context;
  ExReleaseFastMutex(&process->State->Lock);
  return STATUS_SUCCESS;
}

VOID AdmissionGpuvaG3DetachContext(ADMISSION_RENDER_CONTEXT *context) {
  ADMISSION_G3_PROCESS *process;
  if (context == NULL || context->GpuvaG3Process == NULL) return;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  ExAcquireFastMutex(&process->State->Lock);
  {
    ADMISSION_RENDER_CONTEXT **link = &process->Contexts;
    while (*link && *link != context) link = &(*link)->GpuvaG3NextContext;
    if (*link) *link = context->GpuvaG3NextContext;
  }
  if (process->ContextRefs) --process->ContextRefs;
  context->GpuvaG3NextContext = NULL;
  context->GpuvaG3Process = NULL;
  ExReleaseFastMutex(&process->State->Lock);
}

NTSTATUS AdmissionGpuvaG3ResolveTable(
    ADMISSION_CONTEXT *adapter, const DXGK_PAGETABLEUPDATEADDRESS *address,
    DXGK_PAGETABLEUPDATEMODE mode, ULONGLONG *table_ipa) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  ULONGLONG offset;
  ULONG_PTR pointer;
  PHYSICAL_ADDRESS physical, tail;
  /* VidMm owns tables throughout the local segment, independently of the
   * fixed DCP scanout window. Keep private/backend storage excluded. */
  if (adapter == NULL || address == NULL || table_ipa == NULL ||
      !NT_SUCCESS(AdmissionMemoryRuntimeLocalView(adapter, &view)))
    return STATUS_INVALID_DEVICE_STATE;
  if (mode == DXGK_PAGETABLEUPDATE_GPU_PHYSICAL) {
    if (address->GpuPhysical.SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT ||
        address->GpuPhysical.Padding != 0u)
      return STATUS_INVALID_PARAMETER;
    offset = address->GpuPhysical.SegmentOffset;
  } else if (mode == DXGK_PAGETABLEUPDATE_CPU_VIRTUAL) {
    if (address->CpuVirtual == NULL)
      return STATUS_INVALID_PARAMETER;
    pointer = (ULONG_PTR)address->CpuVirtual;
    if (pointer > MAXULONG_PTR - 0x3fffu)
      return STATUS_INVALID_ADDRESS;
    physical = MmGetPhysicalAddress(address->CpuVirtual);
    if (physical.QuadPart <= 0 ||
        !AppleAgxGpuvaG3TableSpanWithinLocal(
            view.GuestIpaAddress, view.Bytes,
            (ULONGLONG)physical.QuadPart, 0x4000ULL))
      return STATUS_INVALID_ADDRESS;
    tail = MmGetPhysicalAddress((PUCHAR)address->CpuVirtual + 0x3fffu);
    if ((ULONGLONG)tail.QuadPart !=
        (ULONGLONG)physical.QuadPart + 0x3fffULL)
      return STATUS_INVALID_ADDRESS;
    offset = (ULONGLONG)physical.QuadPart - view.GuestIpaAddress;
  } else {
    return STATUS_NOT_SUPPORTED;
  }
  if ((offset & 0x3fffULL) || view.Bytes < 0x4000ULL ||
      offset > view.Bytes - 0x4000ULL ||
      AppleAgxGpuvaG3ResolvePageAddress(
          ADMISSION_MEMORY_LOCAL_SEGMENT, offset,
          ADMISSION_MEMORY_LOCAL_SEGMENT, view.GuestIpaAddress,
          view.Bytes, table_ipa) != AppleAgxGpuvaG3Ok)
    return STATUS_INVALID_ADDRESS;
  return STATUS_SUCCESS;
}

/* Diagnostic history only; all updates use the process mutex. A zero IPA
 * denotes a call that failed before address resolution. Counts saturate. */
static void AdmissionG3RecordSetRootSeen(ADMISSION_G3_PROCESS *process,
    ADMISSION_RENDER_CONTEXT *context, ULONGLONG ipa) {
  if (process->SetRootCount != MAXULONG) ++process->SetRootCount;
  if (context->GpuvaG3SetRootCount != MAXULONG) ++context->GpuvaG3SetRootCount;
  process->LastSetRootIpa = context->GpuvaG3LastSetRootIpa = ipa;
}

_Use_decl_annotations_ VOID AdmissionDdiSetRootPageTable(
    HANDLE Adapter, const DXGKARG_SETROOTPAGETABLE *Args) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)Adapter;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_G3_PROCESS *process;
  DXGK_PAGETABLEUPDATEADDRESS address;
  ULONGLONG root_ipa = 0ULL;
  ULONG step;
  if (adapter == NULL || Args == NULL || Args->hContext == NULL) return;
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter) return;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  context->GpuvaG3RootIpa = 0ULL;
  if (process == NULL || Args->NumEntries != 8u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) {
    if (process != NULL && KeGetCurrentIrql() == PASSIVE_LEVEL) {
      ExAcquireFastMutex(&process->State->Lock);
      AdmissionG3RecordSetRootSeen(process, context, 0ULL);
      ExReleaseFastMutex(&process->State->Lock);
    }
    context->GpuvaG3Poisoned = TRUE;
    return;
  }
  RtlZeroMemory(&address, sizeof(address));
  address.GpuPhysical = Args->Address;
  if (!NT_SUCCESS(AdmissionGpuvaG3ResolveTable(
          adapter, &address, DXGK_PAGETABLEUPDATE_GPU_PHYSICAL,
          &root_ipa))) {
    ExAcquireFastMutex(&process->State->Lock);
    AdmissionG3RecordSetRootSeen(process, context, 0ULL);
    ExReleaseFastMutex(&process->State->Lock);
    context->GpuvaG3Poisoned = TRUE;
    return;
  }
  ExAcquireFastMutex(&process->State->Lock);
  /* EXP1052 receipt-only: the same short-circuit order, with the failing
   * step encoded in the poison site's file field (0x11..0x15). */
  step = 0u;
  if (process->Poisoned || process->Graph.Uncertain) step = 1u;
  else if (!NT_SUCCESS(AdmissionGpuvaG3BrokerTable(
               process, root_ipa, TRUE, &root_ipa))) step = 2u;
  else if (!AppleAgxGpuvaG3GraphRegisterTable(&process->Graph, root_ipa, 0u))
    step = 3u;
  else if (!AppleAgxGpuvaG3GraphBindRoot(&process->Graph, root_ipa)) step = 4u;
  else if (process->PrivateLeafIpa &&
           !AdmissionG3AttachPrivateLeaves(process)) step = 5u;
  if (step != 0u) {
    context->GpuvaG3Poisoned = TRUE;
    /* Step 5 names the AttachPrivate refusal (file 0x20 + reason). */
    ADMISSION_G3_POISON(process, step == 5u ?
        0x20u + process->Graph.AttachFailure : 0x10u + step);
  } else {
    context->GpuvaG3RootIpa = root_ipa;
  }
  AdmissionG3RecordSetRootSeen(process, context, root_ipa);
  ExReleaseFastMutex(&process->State->Lock);
}

/* rc102: Packet is the job's own description: the render packet's at
 * depth 1, the kicked entry's for a depth-2 successor (the render packet
 * still describes the running job then). */
static BOOLEAN AdmissionG3OutputMatchesLocalFor(
    ADMISSION_CONTEXT *adapter, APPLE_AGX_GPUVA_G3_GRAPH *graph,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *packet) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  ULONGLONG ipa, offset, position;
  if (packet->DestinationGpuVa == 0ULL ||
      packet->DestinationBytes == 0u ||
      !NT_SUCCESS(adapter->BackendImage.G4Native
          ? AdmissionMemoryRuntimeLocalView(adapter, &view)
          : AdmissionMemoryRuntimeScanoutView(adapter, &view)) ||
      !AppleAgxGpuvaG3GraphTranslateVa(
          graph, packet->DestinationGpuVa, &ipa) ||
      ipa < view.GuestIpaAddress) return FALSE;
  offset = ipa - view.GuestIpaAddress;
  if (offset > view.Bytes || packet->DestinationBytes > view.Bytes - offset ||
      view.HostPhysicalAddress > MAXULONGLONG - offset ||
      packet->DestinationPhysical != view.HostPhysicalAddress + offset)
    return FALSE;
  for (position = 0ULL; position < packet->DestinationBytes;) {
    ULONGLONG mapped;
    if (!AppleAgxGpuvaG3GraphTranslateVa(
            graph, packet->DestinationGpuVa + position, &mapped) ||
        mapped != ipa + position) return FALSE;
    position += 0x4000ULL -
        ((packet->DestinationGpuVa + position) & 0x3fffULL);
  }
  return TRUE;
}

/* M2-c: a VA-only destination (outside the local reserve) must still be
 * mapped writable; a local one must still be its recorded local pages. */
static BOOLEAN AdmissionG3OutputAccessibleFor(
    ADMISSION_CONTEXT *adapter, APPLE_AGX_GPUVA_G3_GRAPH *graph,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *packet) {
  if (adapter->BackendImage.G4Native &&
      packet->DestinationPhysical == 0ULL &&
      packet->DestinationCpuToken == 0ULL)
    return packet->DestinationGpuVa != 0ULL &&
        packet->DestinationBytes != 0u &&
        /* L3-P1: the parse proved this target moments ago; the cached
         * proof answers exactly as the walk (graph mutations drop it). */
        AppleAgxGpuvaG3GraphContainsRangeAccessCached(graph,
            packet->DestinationGpuVa, packet->DestinationBytes, TRUE);
  return AdmissionG3OutputMatchesLocalFor(adapter, graph, packet);
}

static ADMISSION_G3_PRIVATE_SCENE *AdmissionG4FindPrivateScene(
    ADMISSION_G3_PROCESS *p, ADMISSION_RENDER_CONTEXT *context,
    const APPLE_AGX_G4_PRIVATE_LEASE *lease, ULONG fence, BOOLEAN begin) {
  ADMISSION_G3_PRIVATE_SCENE *s;
  if ((context->GpuvaG3Closing && !begin) || !lease || !lease->ManagerId || lease->ManagerId!=p->Graph.ProcessId ||
      lease->ManagerGeneration!=p->PrivateManager.Generation ||
      context->GpuvaG3PrivateManagerGeneration!=lease->ManagerGeneration)
    return NULL;
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Storage.Generation==lease->SceneId) break;
  if (!s || s->Context!=context || s->Storage.Generation!=lease->SceneGeneration ||
      s->Quarantined || (!begin && s->ReleaseRequested) || s->Started ||
      (begin ? (!s->Queued || s->Fence!=fence ||
          (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence,0,0)==fence ||
          (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence2,0,0)==fence ||
          AdmissionG3PrivateSlotCancelled(context,fence)) :
          /* Phase 5b: a second job may queue while one slot is free and no
           * preempted job awaits its resubmission. */
          (s->Queued ||
           (InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0) &&
            InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0)) ||
           InterlockedCompareExchange(&context->GpuvaG3PreemptFence,0,0) ||
           InterlockedCompareExchange(&context->GpuvaG3PreemptFence2,0,0)))) return NULL;
  return s;
}

static ADMISSION_G3_PRIVATE_SCENE *AdmissionG4FindPrivateResubmission(
    ADMISSION_G3_PROCESS *p, ADMISSION_RENDER_CONTEXT *context,
    const APPLE_AGX_G4_PRIVATE_LEASE *lease, ULONG fence) {
  ADMISSION_G3_PRIVATE_SCENE *s;
  if (context->GpuvaG3Closing || !lease || !lease->ManagerId ||
      lease->ManagerId!=p->Graph.ProcessId ||
      lease->ManagerGeneration!=p->PrivateManager.Generation ||
      context->GpuvaG3PrivateManagerGeneration!=lease->ManagerGeneration ||
      InterlockedCompareExchange(&context->GpuvaG3CancelUncertain,0,0)) return NULL;
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Storage.Generation==lease->SceneId) break;
  if (!s || s->Context!=context || s->Storage.Generation!=lease->SceneGeneration ||
      !s->Queued || s->Submitting || s->Started || s->Quarantined || !fence ||
      !s->Fence || fence==s->Fence ||
      AdmissionG3PrivateSlotCancelled(context,s->Fence) ||
      !AdmissionG3PrivateSlotPreempted(context,s->Fence))
    return NULL;
  return s;
}

/* EXP1210 receipt-only: which predicates of AdmissionG4FindPrivateResubmission
 * refuse this resubmission (same order; process lock held), and the scene. */
static ULONG AdmissionG4ResubmissionRefusal(ADMISSION_G3_PROCESS *p,
    ADMISSION_RENDER_CONTEXT *context, const APPLE_AGX_G4_PRIVATE_LEASE *lease,
    ULONG fence, ADMISSION_G3_PRIVATE_SCENE **scene) {
  ADMISSION_G3_PRIVATE_SCENE *s=NULL;
  ULONG r=0u;
  *scene=NULL;
  if (context->GpuvaG3Closing) r|=1u<<0;
  if (!lease || !lease->ManagerId || lease->ManagerId!=p->Graph.ProcessId) r|=1u<<1;
  if (lease && lease->ManagerGeneration!=p->PrivateManager.Generation) r|=1u<<2;
  if (lease && context->GpuvaG3PrivateManagerGeneration!=lease->ManagerGeneration) r|=1u<<3;
  if (InterlockedCompareExchange(&context->GpuvaG3CancelUncertain,0,0)) r|=1u<<4;
  if (lease) for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Storage.Generation==lease->SceneId) break;
  if (!s) return r|(1u<<5);
  *scene=s;
  if (s->Context!=context) r|=1u<<6;
  if (s->Storage.Generation!=lease->SceneGeneration) r|=1u<<7;
  if (!s->Queued) r|=1u<<8;
  if (s->Submitting) r|=1u<<9;
  if (s->Started) r|=1u<<10;
  if (s->Quarantined) r|=1u<<11;
  if (!fence) r|=1u<<12;
  if (!s->Fence) r|=1u<<13;
  if (fence && fence==s->Fence) r|=1u<<14;
  if (AdmissionG3PrivateSlotCancelled(context,s->Fence)) r|=1u<<15;
  if (!AdmissionG3PrivateSlotPreempted(context,s->Fence)) r|=1u<<16;
  return r;
}

static int AdmissionG4PrivateGraphAccess(void *opaque, unsigned long long va,
    unsigned int bytes, int write, APPLE_AGX_G4_ACCESS_KIND kind,
    unsigned int ordinal) {
  ADMISSION_G3_PRIVATE_SCENE *scene=(ADMISSION_G3_PRIVATE_SCENE *)opaque;
  ADMISSION_G3_PROCESS *p=(ADMISSION_G3_PROCESS *)scene->Context->GpuvaG3Process;
  if (kind==AppleAgxG4AccessProcess) {
    if (ordinal>=9u || va!=scene->Storage.Ranges[ordinal].Va ||
        bytes!=scene->Storage.Ranges[ordinal].Bytes || !write) return 0;
    /* EXP1115: of the TVB heap window only the backed prefix is mapped and
     * named to the firmware: the job's count, else the manager's now.
     * rc80: past the 32-block range the prefix continues into this
     * process's TVB window (VA-contiguous); prove every named block. */
    if (ordinal==2u) {
      ULONG blocks=scene->HeapBlocks ? scene->HeapBlocks : p->PrivateManager.Blocks;
      if (!blocks || ((ULONGLONG)blocks*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK>bytes &&
          (!p->TvbVa || bytes!=APPLE_AGX_G3_TVB_FLOOR_BLOCKS*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK ||
           va+bytes!=p->TvbVa || blocks>APPLE_AGX_G3_TVB_MAX_BLOCKS))) return 0;
      bytes=blocks*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK;
    }
    return AdmissionG4GraphAccess(&p->Graph,va,bytes,write);
  }
  return AdmissionG4GraphAccessTyped(p,va,bytes,write,kind,ordinal);
}

static BOOLEAN AdmissionG4PrivateGeometry(ADMISSION_G3_PRIVATE_SCENE *scene,
    const APPLE_AGX_G4_SUBMIT_VIEW *view) {
  APPLE_AGX_G4_NATIVE_RENDER r;
  if (!scene || !view->Render || view->RenderBytes!=sizeof(r)) return FALSE;
  RtlCopyMemory(&r,view->Render,sizeof(r));
  /* EXP1290: no sample count -- scene storage does not depend on it (Asahi
   * get_tiling_params; the cache key omits it), only the job's utile config
   * and tile-buffer blocks do, and the builder derives those per render. */
  return r.WidthPx==scene->Geometry.WidthPx && r.HeightPx==scene->Geometry.HeightPx &&
      r.UtileWidthPx==scene->Geometry.UtileWidthPx &&
      r.UtileHeightPx==scene->Geometry.UtileHeightPx &&
      r.Layers==scene->Geometry.Layers &&
      RtlCompareMemory(view->Process,scene->Storage.Ranges,sizeof(view->Process))==sizeof(view->Process);
}

static VOID AdmissionG4PrivateUnqueue(ADMISSION_G3_PROCESS *p,
    ADMISSION_G3_PRIVATE_SCENE *scene, ULONG fence) {
  if (!scene) return;
  ExAcquireFastMutex(&p->State->Lock);
  if (scene->Queued && scene->Fence==fence && !scene->Started) {
    scene->Submitting=0;
    if (scene->ResumeFence) {
      /* Failed admission did not consume VidSch's suspended transaction. */
      BOOLEAN second=(ULONG)InterlockedCompareExchange(
          &scene->Context->GpuvaG3PrivateFence2,0,0)==fence;
      scene->Fence=scene->ResumeFence;scene->ResumeFence=0;
      InterlockedExchange(second ? &scene->Context->GpuvaG3PrivateFence2 :
          &scene->Context->GpuvaG3PrivateFence,(LONG)scene->Fence);
      InterlockedExchange(second ? &scene->Context->GpuvaG3PreemptFence2 :
          &scene->Context->GpuvaG3PreemptFence,(LONG)scene->Fence);
    } else {
      scene->Queued=0;scene->Fence=0;
      if ((ULONG)InterlockedCompareExchange(&scene->Context->GpuvaG3PrivateFence2,0,0)==fence)
        InterlockedExchange(&scene->Context->GpuvaG3PrivateFence2,0);
      else
        InterlockedExchange(&scene->Context->GpuvaG3PrivateFence,0);
    }
  }
  ExReleaseFastMutex(&p->State->Lock);
}

BOOLEAN AdmissionGpuvaG3PrivateContextBusy(ADMISSION_RENDER_CONTEXT *context) {
  ADMISSION_G3_PROCESS *p;
  ADMISSION_G3_PRIVATE_SCENE *s;
  BOOLEAN busy=FALSE;
  if (!context || !context->GpuvaG3Process) return FALSE;
  if (KeGetCurrentIrql()!=PASSIVE_LEVEL) return TRUE;
  p=(ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  ExAcquireFastMutex(&p->State->Lock);
  (void)AdmissionG3PrivateReap(p);
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Context==context && (s->Queued || s->Quarantined)) {busy=TRUE;break;}
  ExReleaseFastMutex(&p->State->Lock);
  return busy;
}

#ifdef _MSC_VER
/* EXP990 receipt-only: copy bytes the GPU will read at a process VA through
 * the logical resident PTEs (G3 lock held). Returns bit0 any page resolved,
 * bit1 every page valid local. */
static ULONG AdmissionG4SnapRead(ADMISSION_CONTEXT *adapter,
    const ADMISSION_G3_PROCESS *p, ULONGLONG va, UCHAR *dst, ULONG bytes,
    ULONGLONG *firstIpa) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  ULONG done = 0u, state = 2u;
  if (!va || !NT_SUCCESS(AdmissionMemoryRuntimeLocalView(adapter, &view)) ||
      !view.CpuAddress) return 0u;
  while (done < bytes) {
    ULONGLONG at = va + done;
    ULONG part = (ULONG)(0x1000ULL - (at & 0xfffULL));
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte = AdmissionG3CopyPte(p, at);
    if (part > bytes - done) part = bytes - done;
    if (!pte || !(pte->Flags & APPLE_AGX_GPUVA_G3_VALID) ||
        pte->SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT ||
        !AppleAgxGpuvaG3TableSpanWithinLocal(view.GuestIpaAddress, view.Bytes,
            pte->GuestIpa, 0x1000ULL)) {
      state &= ~2u; done += part; continue;
    }
    if (firstIpa && !*firstIpa) *firstIpa = pte->GuestIpa + (at & 0xfffULL);
    RtlCopyMemory(dst + done, (PUCHAR)view.CpuAddress +
        (SIZE_T)(pte->GuestIpa - view.GuestIpaAddress) + (SIZE_T)(at & 0xfffULL),
        part);
    state |= 1u; done += part;
  }
  return state;
}
#endif

/* G2 HW2b: a present job reads its source and writes its destination in
 * the process graph (never in the private range, which holds the arena),
 * and its arena is this process's present arena. */
static ULONG AdmissionG3PresentSurfaceBytes(const APPLE_AGX_PRESENT_SURFACE *s) {
  ULONGLONG bytes;
  if (s->Width == 0u || s->Height == 0u) return 0u;
  bytes = (ULONGLONG)(s->Height - 1u) * s->Pitch + (ULONGLONG)s->Width * 4u;
  return bytes > 0xffffffffULL ? 0u : (ULONG)bytes;
}

static BOOLEAN AdmissionG3PresentAccessible(ADMISSION_G3_PROCESS *process,
    const ADMISSION_PRESENT_COMPUTE *present) {
  ULONG source = present->Fill ? 0u :
      AdmissionG3PresentSurfaceBytes(&present->Source);
  ULONG destination = AdmissionG3PresentSurfaceBytes(&present->Destination);
  return process->PresentArena.Generation &&
      present->Arena.Va == process->PrivateVa + process->PresentArena.VaOffset &&
      present->Arena.Bytes == APPLE_AGX_PRESENT_ARENA_BYTES &&
      destination != 0u &&
      AdmissionG4GraphAccessTyped(process, present->Destination.Va,
          destination, 1, AppleAgxG4AccessProcess, 0u) &&
      (present->Fill ||
       (source != 0u &&
        AdmissionG4GraphAccessTyped(process, present->Source.Va, source, 0,
            AppleAgxG4AccessProcess, 0u))) ? TRUE : FALSE;
}

/* RC37 diagnostic. The caller can arm only its existing process object;
 * query is still available after that device has entered error. */
NTSTATUS AdmissionGpuvaG3FaultDrillEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  ADMISSION_GPU_FAULT_DRILL query;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *owner = NULL;
  PLIST_ENTRY link;
  ULONG pid;
  NTSTATUS status = STATUS_INVALID_HANDLE;
  if (adapter == NULL || args == NULL || args->pPrivateDriverData == NULL ||
      args->PrivateDriverDataSize != sizeof(query) ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  if (!adapter->GpuFaultDrillAllowed)
    return STATUS_NOT_SUPPORTED;
  RtlCopyMemory(&query, args->pPrivateDriverData, sizeof(query));
  if (query.Magic != ADMISSION_GPU_FAULT_DRILL_MAGIC || query.Version != 1u ||
      query.Bytes != sizeof(query) || query.Operation > 1u)
    return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
  pid = HandleToULong(PsGetCurrentProcessId());
  ExAcquireFastMutex(&state->Lock);
  for (link = state->Processes.Flink; link != &state->Processes; link = link->Flink) {
    ADMISSION_G3_PROCESS *p = CONTAINING_RECORD(link, ADMISSION_G3_PROCESS, Link);
    if (p->Magic != ADMISSION_G3_PROCESS_MAGIC || p->OsProcessId != pid ||
        p->CpuOnlyMappings) continue;
    if (owner != NULL) { status = STATUS_DEVICE_BUSY; goto Done; }
    owner = p;
  }
  if (owner == NULL) goto Done;
  if (query.Operation == 1u) {
    if (owner->Poisoned || owner->Graph.Uncertain || !owner->Graph.Created) {
      status = STATUS_INVALID_DEVICE_STATE;
      goto Done;
    }
    RtlZeroMemory(&owner->FaultDrill, sizeof(owner->FaultDrill));
    owner->FaultDrill.Armed = 1u;
  }
  query.Armed = owner->FaultDrill.Armed;
  query.Fence = owner->FaultDrill.Fence;
  query.GpuVa = owner->FaultDrill.GpuVa;
  query.FaultInfo = owner->FaultDrill.FaultInfo;
  RtlCopyMemory(args->pPrivateDriverData, &query, sizeof(query));
  status = STATUS_SUCCESS;
Done:
  ExReleaseFastMutex(&state->Lock);
  return status;
}

/* Called only after ordinary BeginJob validation/JOB_BEGIN and before work
 * encoding/publication. The leased graph cannot be paged underneath the test.
 * This changes CDM instruction fetch, not a shader pointer or another process. */
BOOLEAN AdmissionGpuvaG3FaultDrillApply(ADMISSION_CONTEXT *adapter,
    ULONG fence, unsigned long long *streamBase, unsigned int *streamBytes) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  APPLE_AGX_GPUVA_G3_WALK_FAILURE walk;
  const ULONGLONG faultVa = 0x10000ULL;
  BOOLEAN injected = FALSE;
  if (adapter == NULL || !adapter->GpuFaultDrillAllowed ||
      streamBase == NULL || streamBytes == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL || !fence)
    return FALSE;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return FALSE;
  ExAcquireFastMutex(&state->Lock);
  process = state->ActiveProcess;
  if (state->ActiveFence != fence || process == NULL ||
      !process->FaultDrill.Armed) goto Done;
  process->FaultDrill.Armed = 0u; /* consume before any GPU-visible effect */
  RtlZeroMemory(&walk, sizeof(walk));
  if (process->Poisoned || process->CpuOnlyMappings || !process->Graph.Created ||
      process->Graph.Uncertain || !process->Graph.JobInFlight ||
      !process->Graph.RootTable || !process->Graph.RootIpa || !process->Graph.LeaseToken ||
      process->Graph.Slot == 0u ||
      AppleAgxGpuvaG3GraphInspectRangeAccess(&process->Graph, faultVa, 64u,
          FALSE, &walk) ||
      /* NoRoot means an absent root EDGE here, with RootTable proven above. */
      (walk.Reason != AppleAgxG3WalkNoRoot && walk.Reason != AppleAgxG3WalkNoTable &&
       walk.Reason != AppleAgxG3WalkLeafAbsent)) goto Done;
  *streamBase = faultVa;
  *streamBytes = 64u;
  process->FaultDrill.Fence = fence;
  process->FaultDrill.GpuVa = faultVa;
  injected = TRUE;
Done:
  ExReleaseFastMutex(&state->Lock);
  return injected;
}

/* L1b: the graph begin (V5 LEASE + JOB_BEGIN) timed for the begin split. */
static bool AdmissionG3TimedGraphBeginJob(ADMISSION_G3_STATE *state,
    APPLE_AGX_GPUVA_G3_GRAPH *graph, unsigned int slot) {
  bool ok;
  const BOOLEAN on = state->LockProfile.Enabled ? TRUE : FALSE;
  if (on) state->BeginLeaseStart = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
  ok = AppleAgxGpuvaG3GraphBeginJob(graph, slot);
  if (on) state->BeginLeaseEnd = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
  return ok;
}

/* Packet: the job's own description (rc102: a successor's is its kicked
 * entry's). *Refusal (rc102 receipt, may be NULL): 0 begun, 1 the active-
 * job state (depth 1: another job; successor: not N's process, or a
 * successor already), 2 a private completion pending, 3 process poisoned,
 * 4 the bound image's re-proof, 5 context poisoned or another root,
 * 6 envelope access, 7 output access, 8 the graph lease (JOB_BEGIN). */
#ifdef ADMISSION_G3_BEGIN_SPLIT_RECEIPT
/* L1b: inside BeginJobAs only, the graph begin is the timed one. */
#define AppleAgxGpuvaG3GraphBeginJob(g, s) AdmissionG3TimedGraphBeginJob(state, (g), (s))
#endif
static NTSTATUS AdmissionGpuvaG3BeginJobAs(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence, BOOLEAN successor,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *packet, ULONG *Refusal) {
  ULONG refusal = 0u;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  APPLE_AGX_G4_SUBMIT_VIEW g4_view = {0};
  BOOLEAN g4_valid = TRUE;
  ADMISSION_G3_PRIVATE_SCENE *private_scene = NULL;
  NTSTATUS status = STATUS_INVALID_DEVICE_STATE;
#ifdef ADMISSION_G3_BEGIN_SPLIT_RECEIPT
  ADMISSION_G3_BEGIN_SPLIT split = {0};
#endif
  if (adapter == NULL || context == NULL || fence == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  /* The paging process has no GPU-visible leaves; it never runs a GPU job. */
  if (state == NULL || process == NULL || process->State != state ||
      process->CpuOnlyMappings)
    return STATUS_INVALID_DEVICE_STATE;
  ExAcquireFastMutex(&state->Lock);
#ifdef ADMISSION_G3_BEGIN_SPLIT_RECEIPT
  if (state->LockProfile.Enabled) {
    split.Acquired = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
    state->BeginLeaseStart = state->BeginLeaseEnd = 0ULL;
  }
#endif
#if ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB
  AdmissionG3VerifyUploads(adapter,state,process);
#endif
  if (adapter->BackendImage.G4Native) {
    ADMISSION_BACKEND_IMAGE *image = &adapter->BackendImage;
    if (image->G4Lease.SceneId)
      private_scene=AdmissionG4FindPrivateScene(process,context,&image->G4Lease,fence,TRUE);
    BOOLEAN prelude_valid = TRUE;
    if (image->G4WithCompute) {
      /* D3D-G1: re-prove the whole v5 stream, compute prelude included. */
      APPLE_AGX_G4_PRIVATE_HEADER_V3 v3;
      RtlZeroMemory(&v3, sizeof(v3));
      v3.V2 = image->G4Header;
      v3.Lease = image->G4Lease;
      prelude_valid = image->G4CommandBytes != 0u &&
          image->G4CommandBytes <= APPLE_AGX_G4_NATIVE_MAX_BYTES &&
          AppleAgxG4ComposeHeaderV5(&state->G4ReparseV5.Header, &v3,
              image->G4Header.Base.CommandVa, image->G4CommandBytes,
              &image->G4PreludePreemption);
      if (prelude_valid) {
        RtlCopyMemory(state->G4ReparseV5.Command, image->G4Command,
                      image->G4CommandBytes);
        prelude_valid = AppleAgxG4ParseSubmitComputeEx(&state->G4ReparseV5,
            (unsigned int)sizeof(state->G4ReparseV5.Header) + image->G4CommandBytes,
            (unsigned int)sizeof(state->G4ReparseV5.Header) + image->G4CommandBytes,
            image->G4Header.Base.CommandVa, image->G4CommandBytes,
            APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES,
            private_scene ? AdmissionG4PrivateGraphAccess : AdmissionG4GraphAccessTyped,
            private_scene ? (void *)private_scene : (void *)process,
            &g4_view, NULL) == AppleAgxG4ParseOk && g4_view.HasCompute &&
            RtlCompareMemory(&g4_view.Compute, &image->G4PreludeCompute,
                             sizeof(g4_view.Compute)) == sizeof(g4_view.Compute);
      }
    }
    if ((image->G4Lease.SceneId && !private_scene) || image->BoundFence != fence ||
        image->G4CommandBytes == 0u ||
        image->G4CommandBytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
        !prelude_valid ||
        (!image->G4WithCompute && AppleAgxG4ParseSubmitEx(&image->G4Header,
            (unsigned int)sizeof(image->G4Header) + image->G4CommandBytes,
            (unsigned int)sizeof(image->G4Header) + image->G4CommandBytes,
            image->G4Header.Base.CommandVa, image->G4CommandBytes,
            private_scene ? AdmissionG4PrivateGraphAccess : AdmissionG4GraphAccessTyped,
            private_scene ? (void *)private_scene : (void *)process,
            &g4_view, NULL) != AppleAgxG4ParseOk) ||
        (private_scene && !AdmissionG4PrivateGeometry(private_scene,&g4_view)) ||
        image->G4Header.Base.CommandVa != context->GpuvaG3DmaBufferVa ||
        image->G4CommandBytes != context->GpuvaG3DmaBufferBytes)
      g4_valid = FALSE;
  } else if (adapter->BackendImage.PresentCompute) {
    /* G2 HW2b: re-prove the bound present under the same lock as
     * JOB_BEGIN: the arena is this process's and its surfaces are still
     * readable and writable in its graph. */
    if (adapter->BackendImage.BoundFence != fence ||
        !AdmissionG3PresentAccessible(process, &adapter->BackendImage.Present))
      g4_valid = FALSE;
  } else if (adapter->BackendImage.G4Compute) {
    /* G2: the mappings may have changed since submit; re-prove the bound
     * envelope against the graph under the same lock as JOB_BEGIN. */
    ADMISSION_BACKEND_IMAGE *image = &adapter->BackendImage;
    APPLE_AGX_G4_COMPUTE_VIEW compute_view;
    if (image->BoundFence != fence ||
        AppleAgxG4ParseComputeEx(&image->G4ComputeEnvelope,
            (unsigned int)sizeof(image->G4ComputeEnvelope),
            (unsigned int)sizeof(image->G4ComputeEnvelope),
            image->G4ComputeEnvelope.Header.Base.CommandVa,
            image->G4ComputeEnvelope.Header.Base.CommandBytes,
            APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES, AdmissionG4GraphAccessTyped,
            process, &compute_view, NULL) != AppleAgxG4ParseOk ||
        image->G4ComputeEnvelope.Header.Base.CommandVa !=
            context->GpuvaG3DmaBufferVa ||
        image->G4ComputeEnvelope.Header.Base.CommandBytes !=
            context->GpuvaG3DmaBufferBytes)
      g4_valid = FALSE;
  }
#ifdef ADMISSION_G3_BEGIN_SPLIT_RECEIPT
  if (state->LockProfile.Enabled)
    split.Parsed = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
#endif
  /* rc102: a successor joins the active job of its own process. The same
   * checks in the same order as before; the first that fails is noted. */
  if (!(successor ? state->ActiveProcess == process && state->ActiveFence != 0u &&
                        state->ActiveFence != fence && state->SuccessorFence == 0u
                  : state->ActiveProcess == NULL))
    refusal = 1u;
  else if (state->PrivateCompletionFence)
    refusal = 2u;
  else if (process->Poisoned)
    refusal = 3u;
  else if (!g4_valid)
    refusal = 4u;
  else if (context->GpuvaG3Poisoned || context->GpuvaG3RootIpa == 0ULL ||
           context->GpuvaG3RootIpa != process->Graph.RootIpa)
    refusal = 5u;
  /* A present job's GPU work is in its arena, not its DMA buffer. */
  else if (!(adapter->BackendImage.PresentCompute ? TRUE :
             adapter->BackendImage.G4Native || adapter->BackendImage.G4Compute ?
                AdmissionG4LogicalEnvelopeAccess(process,
                    context->GpuvaG3DmaBufferVa,
                    context->GpuvaG3DmaBufferBytes) :
                AppleAgxGpuvaG3GraphContainsRange(&process->Graph,
                    context->GpuvaG3DmaBufferVa,
                    context->GpuvaG3DmaBufferBytes)))
    refusal = 6u;
  /* A compute job has no destination allocation to prove; a present job's
   * destination was proven above. rc102 (EXP2042): the job's own output,
   * not the running job's packet. */
  else if (!(adapter->BackendImage.G4Compute || adapter->BackendImage.PresentCompute ||
             AdmissionG3OutputAccessibleFor(adapter, &process->Graph, packet)))
    refusal = 7u;
  else if (!(successor ? AppleAgxGpuvaG3GraphBeginNestedJob(&process->Graph)
                       : AppleAgxGpuvaG3GraphBeginJob(&process->Graph,
                             ADMISSION_GPUVA_G3_JOB_VM_SLOT)))
    refusal = 8u;
  if (Refusal != NULL) *Refusal = refusal;
  if (refusal == 0u) {
    if (private_scene) {
      ADMISSION_BACKEND_IMAGE *image=&adapter->BackendImage;
      private_scene->Started=1u;
      /* The exact scene hold pins this snapshot through joined completion. */
      image->G4Manager=&process->FirmwareManager;
      image->G4ManagerKey.Owner=process->Graph.ProcessId;
      image->G4ManagerKey.Generation=process->PrivateManager.Generation;
      image->G4ManagerKey.RootIpa=process->Graph.RootIpa;
      image->G4ManagerKey.VmSlot=process->Graph.Slot;
      RtlCopyMemory(image->G4ManagerKey.Backing,private_scene->Storage.Ranges,
          sizeof(image->G4ManagerKey.Backing));
      /* A different backed block count is a different manager: InitBM. */
      image->G4ManagerKey.Backing[2].Bytes=
          private_scene->HeapBlocks*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK;
    }
    /* V1: the builders put the leased slot into this job's firmware
     * commands (compute, prelude, present VmSlot; render context_id). */
    if (process->Graph.SlotTable)
      adapter->BackendImage.JobVmSlot = process->Graph.Slot;
    /* rc81 receipt: would the generation alone have proven this reparse
     * redundant (no page-table update since the submission's parse)? */
    AdmissionJobTimingNoteWindows(adapter, fence, ADMISSION_JOB_FLAG_BEGIN_JOB |
        (context->GpuvaG3MappingGeneration == process->Graph.MappingGeneration ?
             ADMISSION_JOB_FLAG_MAPPING_UNCHANGED : 0u) |
        (private_scene != NULL ? ADMISSION_JOB_FLAG_PRIVATE_SCENE : 0u));
    /* The graph lock covers fresh range/output validation and JOB_BEGIN.
     * A process-wide epoch also changes for unrelated mappings; it cannot
     * invalidate an otherwise valid job. JOB_BEGIN pins the validated graph. */
    context->GpuvaG3MappingGeneration = process->Graph.MappingGeneration;
    if (successor) {
      state->SuccessorContext = context;
      state->SuccessorFence = fence;
    } else {
      state->ActiveProcess = process;
      state->ActiveContext = context;
      state->ActiveFence = fence;
    }
    status = STATUS_SUCCESS;
#ifdef _MSC_VER
    if (adapter->BackendImage.G4Native && g4_valid && g4_view.Render &&
        g4_view.RenderBytes == sizeof(APPLE_AGX_G4_NATIVE_RENDER) &&
        ((const APPLE_AGX_G4_NATIVE_RENDER *)g4_view.Render)->WidthPx == 77u) {
      APPLE_AGX_G4_NATIVE_RENDER r;
      ADMISSION_G4_DRAW_SNAPSHOT *s = &adapter->G4DrawSnapshot;
      ADMISSION_G4_DRAW_SNAP *slot =
          &s->Slot[s->Next++ % ADMISSION_G4_DRAW_SNAP_COUNT];
      RtlCopyMemory(&r, g4_view.Render, sizeof(r));
      RtlZeroMemory(slot, sizeof(*slot));
      s->Version = 1u; s->Bytes = sizeof(*s);
      slot->Fence = fence; slot->Flags = r.Flags; slot->PppCtrl = r.PppCtrl;
      slot->Width = r.WidthPx; slot->Height = r.HeightPx;
      slot->BgUsc = r.Bg.Usc; slot->EotUsc = r.Eot.Usc;
      slot->Process = (ULONG)process->Graph.ProcessId;
      slot->VdmBase = r.VdmCtrlStreamBase;
      slot->ScissorBase = r.IspScissorBase; slot->DbiasBase = r.IspDbiasBase;
      slot->VdmState = AdmissionG4SnapRead(adapter, process, r.VdmCtrlStreamBase,
          slot->Vdm, sizeof(slot->Vdm), &slot->VdmIpa);
      slot->ScissorState = AdmissionG4SnapRead(adapter, process, r.IspScissorBase,
          slot->Scissor, sizeof(slot->Scissor), NULL);
      slot->DbiasState = AdmissionG4SnapRead(adapter, process, r.IspDbiasBase,
          slot->Dbias, sizeof(slot->Dbias), NULL);
      {
        /* Walk Mesa's VDM words (cmdbuf.xml): 0 PPP state (hi:8,size:8 |
         * lo:32), 1 barrier, 2 VDM state (+1 word per present bit), 3 index
         * list, 4 stream link, 6 terminate. */
        ULONG words[64], i = 0u, n;
        RtlCopyMemory(words, slot->Vdm, sizeof(words));
        while (i < 64u) {
          ULONG w = words[i], type = w >> 29;
          if (type == 0u && i + 1u < 64u) {
            if (slot->PppCount < 4u) {
              slot->PppAddr[slot->PppCount] =
                  ((ULONGLONG)(w & 0xffu) << 32) | words[i + 1u];
              ++slot->PppCount;
            }
            i += 2u;
          } else if (type == 1u) {
            i += 1u;
          } else if (type == 2u) {
            ULONG at = i + 1u;
            if (w & 1u) ++at;
            if ((w & 2u) && (w & 4u) && at + 1u < 64u)
              slot->PipeAddr = 0x1100000000ULL + (words[at + 1u] & ~0x3fu);
            n = 0u;
            for (ULONG bit = 0u; bit < 8u; ++bit)
              if (bit != 6u && (w & (1u << bit))) ++n;
            i += 1u + n;
          } else {
            if (type == 3u) { slot->IndexWord = w; slot->IndexAt = i; }
            break;
          }
        }
        for (i = 0u; i < slot->PppCount; ++i)
          slot->PppState[i] = AdmissionG4SnapRead(adapter, process,
              slot->PppAddr[i], slot->Ppp[i], sizeof(slot->Ppp[i]), NULL);
        if (slot->PipeAddr)
          slot->PipeState = AdmissionG4SnapRead(adapter, process,
              slot->PipeAddr, slot->Pipe, sizeof(slot->Pipe), NULL);
      }
      {
        ADMISSION_G4_FW_SNAPSHOT *fw = &adapter->G4FwSnapshot;
        ADMISSION_G4_FW_SNAP *f = &fw->Slot[fw->Next++ % 4u];
        static const ULONG index[4] = {19u, 18u, 15u, 17u};
        UCHAR *dst[4];
        ULONG cap[4];
        dst[0] = f->Ta; cap[0] = sizeof(f->Ta);
        dst[1] = f->D3; cap[1] = sizeof(f->D3);
        dst[2] = f->Seq15; cap[2] = sizeof(f->Seq15);
        dst[3] = f->Seq17; cap[3] = sizeof(f->Seq17);
        fw->Version = 1u; fw->Bytes = sizeof(*fw);
        RtlZeroMemory(f, sizeof(*f));
        f->Fence = fence;
        for (ULONG k = 0u; k < 4u; ++k) {
          const APPLE_AGX_EXP208_RELOCATION_OBJECT *o =
              &adapter->BackendImage.Objects[index[k]];
          ULONG n = o->Size < cap[k] ? (ULONG)o->Size : cap[k];
          f->Sizes[k] = (ULONG)o->Size; f->GpuVa[k] = o->GpuVa;
          if (o->Data && n) RtlCopyMemory(dst[k], o->Data, n);
        }
        {
          /* WorkCommandTA offsets (m1n1 cmdqueue.py, G13/V13_5): struct_2
           * at 0x40: tvb_tilemap +0x10, tpc +0x20, heapmeta +0x28 (bit63),
           * heapmeta2 +0x58, deflake1/2/3 +0x70/+0x78/+0x88, encoder +0x90;
           * unkptr_45c at 0x464. */
          static const ULONG off[9] = {0x50u, 0x60u, 0x68u, 0x98u, 0xb0u,
                                       0xb8u, 0xc8u, 0xd0u, 0x464u};
          for (ULONG k = 0u; k < 9u; ++k) {
            ULONGLONG va = 0ULL;
            APPLE_AGX_GPUVA_G3_WALK_FAILURE walk;
            RtlCopyMemory(&va, f->Ta + off[k], sizeof(va));
            va &= 0x000000ffffffffffULL;
            RtlZeroMemory(&walk, sizeof(walk));
            f->CheckVa[k] = va;
            if (va) {
              f->CheckOk[k] = AppleAgxGpuvaG3GraphInspectRangeAccess(
                  &process->Graph, va & ~0x3fffULL, 0x4000u, TRUE, &walk) ? 1u : 0u;
              f->CheckReason[k] = walk.Reason; f->CheckLevel[k] = walk.Level;
            }
          }
          f->CheckCount = 9u; f->PrivateVa = process->PrivateVa;
        }
      }
      InterlockedExchange(&adapter->G4DrawSnapshotDirty, 1);
    }
#endif
  } else if (process->Graph.Uncertain) {
    ADMISSION_G3_POISON(process,1u);
    status = STATUS_DEVICE_HARDWARE_ERROR;
  }
#ifdef ADMISSION_G3_BEGIN_SPLIT_RECEIPT
  if (state->LockProfile.Enabled && split.Acquired) {
    ADMISSION_G3_BEGIN_SPLIT *e = &state->BeginSplits.Ring[
        state->BeginSplits.Next++ % ADMISSION_G3_BEGIN_SPLIT_RING];
    split.Fence = fence; split.Successor = successor ? 1u : 0u;
    split.Refusal = refusal;
    split.Native = adapter->BackendImage.G4Native ? 1u :
        adapter->BackendImage.G4Compute ? 2u :
        adapter->BackendImage.PresentCompute ? 3u : 0u;
    split.LeaseStart = state->BeginLeaseStart;
    split.LeaseEnd = state->BeginLeaseEnd;
    split.Released = (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart;
    *e = split;
  }
#endif
  ExReleaseFastMutex(&state->Lock);
#ifdef _MSC_VER
  if (KeGetCurrentIrql() == PASSIVE_LEVEL) AdmissionRecordG4DrawSnapshot(adapter);
#endif
  return status;
}
#ifdef ADMISSION_G3_BEGIN_SPLIT_RECEIPT
#undef AppleAgxGpuvaG3GraphBeginJob
#endif

NTSTATUS AdmissionGpuvaG3BeginJob(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence) {
  if (adapter == NULL) return STATUS_INVALID_PARAMETER;
  return AdmissionGpuvaG3BeginJobAs(adapter, context, fence, FALSE,
                                    &adapter->RenderPacket.Description, NULL);
}

/* q1: eviction retains keys, not process pointers, across process teardown. */
BOOLEAN AdmissionGpuvaG3StoreManagerSnapshot(ADMISSION_CONTEXT *adapter,
    const APPLE_AGX_RENDER_MANAGER_STATE *snapshot) {
  ADMISSION_G3_STATE *state;
  PLIST_ENTRY link;
  BOOLEAN ok=TRUE;
  if (!adapter || !snapshot || KeGetCurrentIrql()!=PASSIVE_LEVEL) return FALSE;
  state=(ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (!state) return TRUE; /* registry already destroyed; no owner to restore */
  ExAcquireFastMutex(&state->Lock);
  for (link=state->Processes.Flink;link!=&state->Processes;link=link->Flink) {
    ADMISSION_G3_PROCESS *p=CONTAINING_RECORD(link,ADMISSION_G3_PROCESS,Link);
    if (p->Graph.ProcessId!=snapshot->Key.Owner) continue;
    ok=AppleAgxRenderManagerAcceptSnapshot(&p->FirmwareManager,snapshot) ? TRUE : FALSE;
    break;
  }
  ExReleaseFastMutex(&state->Lock);
  return ok;
}

/* rc102 (EXP2051): gpuva_g3_private.h. The same lookup and key as
 * AdmissionGpuvaG3BeginJobAs's private-scene branch. */
BOOLEAN AdmissionGpuvaG3PendingManager(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence,
    APPLE_AGX_RENDER_MANAGER_STATE **Manager, APPLE_AGX_RENDER_MANAGER_KEY *Key) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_G3_PRIVATE_SCENE *scene = NULL;
  ADMISSION_BACKEND_IMAGE *image;
  if (adapter == NULL || context == NULL || fence == 0u || Manager == NULL ||
      Key == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return FALSE;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  image = &adapter->BackendImage;
  if (state == NULL || process == NULL || process->State != state)
    return FALSE;
  ExAcquireFastMutex(&state->Lock);
  if (image->G4Native && image->BoundFence == fence && image->G4Lease.SceneId)
    scene = AdmissionG4FindPrivateScene(process, context, &image->G4Lease,
                                        fence, TRUE);
  if (scene != NULL) {
    RtlZeroMemory(Key, sizeof(*Key));
    *Manager = &process->FirmwareManager;
    Key->Owner = process->Graph.ProcessId;
    Key->Generation = process->PrivateManager.Generation;
    Key->RootIpa = process->Graph.RootIpa;
    /* V1: the successor joins this process's lease; an unleased process
     * (0) cannot match, so the decision stays conservative. */
    Key->VmSlot = process->Graph.LeaseToken ? process->Graph.Slot : 0u;
    RtlCopyMemory(Key->Backing, scene->Storage.Ranges, sizeof(Key->Backing));
    Key->Backing[2].Bytes = scene->HeapBlocks * APPLE_AGX_G3_PRIVATE_HEAP_BLOCK;
  }
  ExReleaseFastMutex(&state->Lock);
  return scene != NULL ? TRUE : FALSE;
}

/* rc102: the successor is the Kicked head entry; its packet describes it
 * (the render packet still holds the running job). */
NTSTATUS AdmissionGpuvaG3BeginSuccessorJob(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence, ULONG *Refusal) {
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  const ADMISSION_G4_PENDING *entry;
  BOOLEAN found = FALSE;
  KIRQL old_irql;
  if (Refusal != NULL) *Refusal = 9u;
  if (adapter == NULL || context == NULL || fence == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&packet, sizeof(packet));
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  if (adapter->G4PendingCount != 0u && entry->State == AdmissionG4PendingKicked &&
      entry->Fence == fence && entry->Context == context) {
    packet = entry->Packet;
    found = TRUE;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (!found) return STATUS_INVALID_DEVICE_STATE;
  return AdmissionGpuvaG3BeginJobAs(adapter, context, fence, TRUE, &packet,
                                    Refusal);
}

/* RC40: the graph job still pins this process mapping. Recheck its published
 * write access and its resident CPU shadow before writing through the native
 * local/pool view; never turn an arbitrary process GPUVA into a CPU pointer. */
static BOOLEAN AdmissionG3PublishTimestampLocked(ADMISSION_CONTEXT *adapter,
    ADMISSION_G3_PROCESS *process, ULONGLONG va, ULONGLONG endNs) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte;
  ULONGLONG ipa, offset;
  if (!process || process->Poisoned || process->CpuOnlyMappings ||
      !process->Graph.JobInFlight || !process->Graph.LeaseToken ||
      process->Graph.Uncertain || !endNs || va < 0x10000ULL ||
      va >= (1ULL << 39) || (va & 7ULL) ||
      !AppleAgxGpuvaG3GraphContainsRangeAccess(&process->Graph, va, 8u, TRUE) ||
      !AppleAgxGpuvaG3GraphTranslateVa(&process->Graph, va, &ipa))
    return FALSE;
  pte = AdmissionG3CopyPte(process, va);
  if (!pte || !pte->Allocation ||
      pte->Flags != (APPLE_AGX_GPUVA_G3_VALID | APPLE_AGX_GPUVA_G3_WRITE) ||
      pte->GuestIpa > (~0ULL) - (va & 0xfffULL) ||
      ipa != pte->GuestIpa + (va & 0xfffULL))
    return FALSE;
  RtlZeroMemory(&view, sizeof(view));
  if (pte->SegmentId == ADMISSION_MEMORY_LOCAL_SEGMENT) {
    if (!NT_SUCCESS(AdmissionMemoryRuntimeLocalView(adapter, &view))) return FALSE;
  } else if (pte->SegmentId == ADMISSION_MEMORY_POOL_SEGMENT) {
    if (!NT_SUCCESS(AdmissionMemoryPoolView(adapter, &view))) return FALSE;
  } else {
    return FALSE;
  }
  if (!view.CpuAddress ||
      !AppleAgxGpuvaG3TableSpanWithinLocal(view.GuestIpaAddress, view.Bytes,
                                         ipa, sizeof(endNs)))
    return FALSE;
  offset = ipa - view.GuestIpaAddress;
  KeMemoryBarrier();
  RtlCopyMemory((PUCHAR)view.CpuAddress + (SIZE_T)offset, &endNs, sizeof(endNs));
  KeMemoryBarrier();
  return TRUE;
}

VOID AdmissionGpuvaG3LockProfileExport(ADMISSION_CONTEXT *adapter, HANDLE key,
    BOOLEAN dense) {
  ADMISSION_G3_STATE *state;
  UNICODE_STRING name;
  LARGE_INTEGER frequency;
  /* V1 receipt (Wom1G3RetainedSlots only): QWORDs version, bytes, first
   * slot, count, quarantined mask, leases, reuses, steals, releases,
   * quarantines, recovered quarantines, clock; v2: 8 release source lines
   * and their counts. */
  ULONGLONG slots[28];
  ULONG site;
  if (adapter == NULL || key == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return;
  (void)KeQueryPerformanceCounter(&frequency);
  /* Raw lock: this export is not one of the profiled sites. */
  ExAcquireFastMutex(&state->Lock);
  if (!state->LockProfile.Enabled) {
    state->LockProfile.Version = 1u;
    state->LockProfile.Bytes = sizeof(state->LockProfile);
    state->LockProfile.QpcFrequency = (ULONGLONG)frequency.QuadPart;
    /* 100 us */
    state->LockProfile.ThresholdTicks = (ULONGLONG)frequency.QuadPart / 10000ULL;
    state->LockProfile.Enabled = 1u;
  }
  RtlCopyMemory(&state->LockProfileSnapshot, &state->LockProfile,
                sizeof(state->LockProfileSnapshot));
  state->BeginSplits.Version = 1u;
  state->BeginSplits.Bytes = sizeof(state->BeginSplits);
  RtlCopyMemory(&state->BeginSplitsSnapshot, &state->BeginSplits,
                sizeof(state->BeginSplitsSnapshot));
  slots[0] = 2u; slots[1] = sizeof(slots);
  for (site = 0u; site < 8u; ++site) {
    slots[12u + site] = state->Slots.ReleaseSite[site];
    slots[20u + site] = state->Slots.ReleaseSiteCount[site];
  }
  slots[2] = state->Slots.First; slots[3] = state->Slots.Count;
  slots[4] = state->Slots.Quarantined; slots[5] = state->Slots.Leases;
  slots[6] = state->Slots.Reuses; slots[7] = state->Slots.Steals;
  slots[8] = state->Slots.Releases; slots[9] = state->Slots.Quarantines;
  slots[10] = state->Slots.Recovered; slots[11] = state->Slots.Clock;
  ExReleaseFastMutex(&state->Lock);
  if (slots[3]) {
    RtlInitUnicodeString(&name, L"Wom1G3RetainedSlotsReceipt");
    (void)ZwSetValueKey(key, &name, 0u, REG_BINARY, slots, sizeof(slots));
  }
  RtlInitUnicodeString(&name, L"Wom1G3LockProfile");
  (void)ZwSetValueKey(key, &name, 0u, REG_BINARY, &state->LockProfileSnapshot,
                      sizeof(state->LockProfileSnapshot));
  RtlInitUnicodeString(&name, L"Wom1G3BeginSplit");
  (void)ZwSetValueKey(key, &name, 0u, REG_BINARY, &state->BeginSplitsSnapshot,
                      sizeof(state->BeginSplitsSnapshot));
  if (dense) {
    RtlInitUnicodeString(&name, L"Wom1G3LockProfileHot");
    (void)ZwSetValueKey(key, &name, 0u, REG_BINARY,
                        &state->LockProfileSnapshot,
                        sizeof(state->LockProfileSnapshot));
  }
}

BOOLEAN AdmissionGpuvaG3CompleteJobTimestamp(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence, ULONGLONG timestampVa,
    ULONGLONG timestampEndNs) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  BOOLEAN complete;
  if (adapter == NULL || fence == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return FALSE;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return FALSE;
  ExAcquireFastMutex(&state->Lock);
  process = state->ActiveProcess;
  /* A retried completion; rc102: the successor may run meanwhile. */
  if ((process == NULL || state->ActiveFence != fence) &&
      state->LastCompletedFence == fence) {
    ExReleaseFastMutex(&state->Lock);
    return TRUE;
  }
  if (process == NULL || state->ActiveFence != fence) {
    ExReleaseFastMutex(&state->Lock);
    return FALSE;
  }
  if (timestampVa &&
      (context == NULL || context->GpuvaG3Process != process ||
       context->GpuvaG3Poisoned ||
       !AdmissionG3PublishTimestampLocked(adapter, process, timestampVa,
                                         timestampEndNs))) {
    ExReleaseFastMutex(&state->Lock);
    return FALSE;
  }
  complete = AppleAgxGpuvaG3GraphEndJob(&process->Graph) ? TRUE : FALSE;
  if (complete) {
    ADMISSION_G3_PRIVATE_SCENE *s;
    for (s=process->PrivateScenes;s;s=s->Next)
      if (s->Queued && s->Started && s->Fence==fence) {
        s->GpuDone=1u;state->PrivateCompletionFence=fence;
      }
    if (state->SuccessorFence != 0u) {
      /* rc102: the job kicked behind it is the active job now; the graph
       * lease stays with it (GraphEndJob ended only the older job). */
      state->ActiveContext = state->SuccessorContext;
      state->ActiveFence = state->SuccessorFence;
      state->SuccessorContext = NULL;
      state->SuccessorFence = 0u;
    } else {
      state->ActiveProcess = NULL;
      state->ActiveContext = NULL;
      state->ActiveFence = 0u;
    }
    state->LastCompletedFence = fence;
  } else {
    ADMISSION_G3_POISON(process,1u);
  }
  ExReleaseFastMutex(&state->Lock);
  /* EXP1138: wake escapes waiting for this process's job to end. */
  if (complete) KeSetEvent(&state->JobEvent, IO_NO_INCREMENT, FALSE);
  return complete;
}

BOOLEAN AdmissionGpuvaG3CompleteJob(ADMISSION_CONTEXT *adapter, ULONG fence) {
  return AdmissionGpuvaG3CompleteJobTimestamp(adapter, NULL, fence, 0ULL, 0ULL);
}

/* rc102: the job of this fence never reached the firmware (render_admission.h). */
_Use_decl_annotations_ VOID AdmissionGpuvaG3AbortJob(ADMISSION_CONTEXT *adapter,
    ULONG fence) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  if (adapter == NULL || fence == 0u || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return;
  ExAcquireFastMutex(&state->Lock);
  process = state->ActiveProcess;
  if (process != NULL &&
      (state->SuccessorFence == fence || state->ActiveFence == fence)) {
    ADMISSION_G3_PRIVATE_SCENE *s;
    /* The reset proved the firmware never received this job (a kicked
     * successor or an active job whose submit failed): its graph job ends
     * unrun and its scene was never started on the GPU. */
    /* V1: an aborted job leaves no retained lease (reset baseline). */
    if (!AppleAgxGpuvaG3GraphEndJob(&process->Graph) ||
        !AppleAgxGpuvaG3GraphReleaseRetained(&process->Graph))
      ADMISSION_G3_POISON(process,1u);
    for (s = process->PrivateScenes; s; s = s->Next)
      if (s->Queued && s->Started && s->Fence == fence && !s->GpuDone)
        s->Started = 0u;
    if (state->SuccessorFence == fence) {
      state->SuccessorContext = NULL;
      state->SuccessorFence = 0u;
    } else if (state->SuccessorFence != 0u) {
      state->ActiveContext = state->SuccessorContext;
      state->ActiveFence = state->SuccessorFence;
      state->SuccessorContext = NULL;
      state->SuccessorFence = 0u;
    } else {
      state->ActiveProcess = NULL;
      state->ActiveContext = NULL;
      state->ActiveFence = 0u;
    }
  }
  ExReleaseFastMutex(&state->Lock);
  KeSetEvent(&state->JobEvent, IO_NO_INCREMENT, FALSE);
}

NTSTATUS AdmissionGpuvaG3SubmitVirtualPaging(
    ADMISSION_CONTEXT *adapter, ADMISSION_RENDER_CONTEXT *context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args) {
  const ADMISSION_PAGING_RECORD *records;
  DXGKARG_SUBMITCOMMAND physical;
  DXGK_SUBMITCOMMANDFLAGS pagingFlags;
  UINT count;
  NTSTATUS status;
  RtlZeroMemory(&pagingFlags, sizeof(pagingFlags));
  pagingFlags.Paging = 1u;
  if (adapter == NULL || context == NULL || args == NULL ||
      !adapter->Started || args->hContext != (HANDLE)context ||
      context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter ||
      (context->Object.Flags & ADMISSION_CONTEXT_SYSTEM) == 0u ||
      context->GpuvaG3Process == NULL || context->GpuvaG3Poisoned ||
      context->GpuvaG3RootIpa == 0ULL ||
      args->DmaBufferVirtualAddress == 0ULL ||
      args->DmaBufferVirtualAddress >= (1ULL << 39) ||
      args->DmaBufferSize == 0u ||
      args->DmaBufferSize > (1ULL << 39) - args->DmaBufferVirtualAddress ||
      args->pDmaBufferPrivateData == NULL ||
      args->DmaBufferUmdPrivateDataSize != 0u ||
      args->DmaBufferPrivateDataSize == 0u ||
      args->DmaBufferPrivateDataSize % sizeof(ADMISSION_PAGING_RECORD) != 0u ||
      (args->Flags.Value & ~(1u << 7u)) != pagingFlags.Value ||
      args->NodeOrdinal != adapter->LiveAdditivePaging ||
      context->Object.NodeOrdinal != args->NodeOrdinal ||
      args->EngineOrdinal != 0u || args->SubmissionFenceId == 0u ||
      KeGetCurrentIrql() > DISPATCH_LEVEL)
    return AdmissionG4SubmitReject(adapter, context, args,
        AdmissionG4RejectPagingInput, STATUS_INVALID_PARAMETER, 0u, FALSE);
  count = args->DmaBufferPrivateDataSize / sizeof(ADMISSION_PAGING_RECORD);
  if (count == 0u || count > ADMISSION_MAX_PAGING_RECORDS ||
      count > MAXULONG / sizeof(ADMISSION_PAGING_MARKER) ||
      args->DmaBufferSize != count * sizeof(ADMISSION_PAGING_MARKER))
    return AdmissionG4SubmitReject(adapter, context, args,
        AdmissionG4RejectPagingShape, STATUS_INVALID_PARAMETER, 0u, TRUE);
  records = (const ADMISSION_PAGING_RECORD *)args->pDmaBufferPrivateData;
  if (!AdmissionPagingRecordsValid(records, count,
                                   ADMISSION_MAX_PAGING_RECORDS,
                                   args->DmaBufferSize))
    return AdmissionG4SubmitReject(adapter, context, args,
        AdmissionG4RejectPagingRecords, STATUS_INVALID_PARAMETER, 0u, TRUE);
  RtlZeroMemory(&physical, sizeof(physical));
  physical.hContext = args->hContext;
  physical.SubmissionFenceId = args->SubmissionFenceId;
  physical.NodeOrdinal = args->NodeOrdinal;
  physical.EngineOrdinal = args->EngineOrdinal;
  physical.Flags.Paging = 1u;
  physical.Flags.Resubmission = args->Flags.Resubmission;
  status = AdmissionCpuQueueSubmit(adapter, &physical,
      ADMISSION_CPU_PACKET_PAGING, records,
      args->DmaBufferPrivateDataSize);
  return NT_SUCCESS(status) ? status : AdmissionG4SubmitReject(
      adapter, context, args, AdmissionG4RejectPagingQueue,
      status, (ULONG)status, TRUE);
}

/* rc81 (EXP2011): G4 parse access runs under the G3 state lock, at submit
 * and again at BeginJob. A large range (the grown TVB heap: 6624 pages at
 * 828 blocks) is proven once and then answered from the graph's proof
 * cache until a mutation of a crossed leaf table or the topology drops it,
 * so a job's proof no longer scales with the TVB size. */
static int AdmissionG4GraphAccess(void *opaque, unsigned long long va,
                                  unsigned int bytes, int write) {
  APPLE_AGX_GPUVA_G3_GRAPH *graph = (APPLE_AGX_GPUVA_G3_GRAPH *)opaque;
  return AppleAgxGpuvaG3GraphContainsRangeAccessCached(
      graph, va, bytes, write != 0) ? 1 : 0;
}

typedef struct _ADMISSION_G3_PRESENT_COPY {
  ADMISSION_G3_PROCESS *Process;
  ADMISSION_SCANOUT_MEMORY_VIEW Local;
  /* M2-f2: segment-3 sources are read through the pool's WC view. */
  ADMISSION_SCANOUT_MEMORY_VIEW Pool;
  NTSTATUS Status;
  ULONGLONG SourceVa, SourceAllocation;
  ULONGLONG CurrentVa, FaultVa;
  ULONG FaultWrite;
} ADMISSION_G3_PRESENT_COPY;

static int AdmissionG3PresentTranslateSource(void *Opaque,
    unsigned long long GpuVa, unsigned long long *GuestIpa) {
  ADMISSION_G3_PRESENT_COPY *copy = Opaque;
  const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte;
  ULONGLONG offset;
  if (copy == NULL || GuestIpa == NULL ||
      GpuVa < copy->SourceVa || copy->SourceAllocation == 0ULL)
    return 0;
  copy->CurrentVa = GpuVa;
  offset = GpuVa - copy->SourceVa;
  pte = AdmissionG3CopyPte(copy->Process, GpuVa);
  if (pte == NULL || pte->GuestIpa == 0ULL ||
      (pte->Flags & APPLE_AGX_GPUVA_G3_VALID) == 0u ||
      (pte->Flags & ~(APPLE_AGX_GPUVA_G3_VALID |
                      APPLE_AGX_GPUVA_G3_WRITE)) != 0u ||
      pte->SegmentId > ADMISSION_MEMORY_POOL_SEGMENT ||
      pte->Allocation != copy->SourceAllocation ||
      pte->AllocationOffset != (offset & ~0xfffULL)) {
    copy->FaultVa = GpuVa;
    copy->FaultWrite = 0u;
    return 0;
  }
  *GuestIpa = pte->GuestIpa + (GpuVa & 0xfffULL);
  return 1;
}

static int AdmissionG3PresentTranslate(void *Opaque,
    unsigned long long GpuVa, unsigned long long *GuestIpa) {
  ADMISSION_G3_PRESENT_COPY *copy = Opaque;
  copy->CurrentVa = GpuVa;
  if (!AppleAgxGpuvaG3GraphTranslateVa(&copy->Process->Graph,
      GpuVa, GuestIpa)) {
    copy->FaultVa = GpuVa;
    copy->FaultWrite = 1u;
    return 0;
  }
  return 1;
}

static int AdmissionG3PresentRead(void *Opaque, unsigned long long GuestIpa,
    void *Bytes, unsigned int ByteCount) {
  ADMISSION_G3_PRESENT_COPY *copy = Opaque;
  MM_COPY_ADDRESS address;
  SIZE_T copied = 0u;
  if (GuestIpa >= copy->Local.GuestIpaAddress &&
      GuestIpa - copy->Local.GuestIpaAddress <= copy->Local.Bytes &&
      ByteCount <= copy->Local.Bytes -
          (GuestIpa - copy->Local.GuestIpaAddress)) {
    RtlCopyMemory(Bytes, (PUCHAR)copy->Local.CpuAddress +
        (SIZE_T)(GuestIpa - copy->Local.GuestIpaAddress), ByteCount);
    return 1;
  }
  if (copy->Pool.CpuAddress != NULL &&
      GuestIpa >= copy->Pool.GuestIpaAddress &&
      GuestIpa - copy->Pool.GuestIpaAddress <= copy->Pool.Bytes &&
      ByteCount <= copy->Pool.Bytes -
          (GuestIpa - copy->Pool.GuestIpaAddress)) {
    RtlCopyMemory(Bytes, (PUCHAR)copy->Pool.CpuAddress +
        (SIZE_T)(GuestIpa - copy->Pool.GuestIpaAddress), ByteCount);
    return 1;
  }
  address.PhysicalAddress.QuadPart = (LONGLONG)GuestIpa;
  copy->Status = MmCopyMemory(Bytes, address, ByteCount,
      MM_COPY_MEMORY_PHYSICAL, &copied);
  if (NT_SUCCESS(copy->Status) && copied != ByteCount)
    copy->Status = STATUS_PARTIAL_COPY;
  if (!NT_SUCCESS(copy->Status)) {
    copy->FaultVa = copy->CurrentVa;
    copy->FaultWrite = 0u;
  }
  return NT_SUCCESS(copy->Status) ? 1 : 0;
}

static int AdmissionG3PresentWrite(void *Opaque, unsigned long long GuestIpa,
    const void *Bytes, unsigned int ByteCount, int Commit) {
  ADMISSION_G3_PRESENT_COPY *copy = Opaque;
  ULONGLONG offset;
  if (GuestIpa < copy->Local.GuestIpaAddress) {
    copy->FaultVa = copy->CurrentVa;
    copy->FaultWrite = 1u;
    return 0;
  }
  offset = GuestIpa - copy->Local.GuestIpaAddress;
  if (offset > copy->Local.Bytes || ByteCount > copy->Local.Bytes - offset) {
    copy->FaultVa = copy->CurrentVa;
    copy->FaultWrite = 1u;
    return 0;
  }
  if (Commit)
    RtlCopyMemory((PUCHAR)copy->Local.CpuAddress + (SIZE_T)offset,
        Bytes, ByteCount);
  return 1;
}

_Use_decl_annotations_ NTSTATUS AdmissionGpuvaG3ExecutePresentVirtual(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    const VOID *Command, UINT Bytes, ULONGLONG *BytesCopied) {
  ADMISSION_PRESENT_BLT_COMMAND command;
  ADMISSION_G3_PRESENT_COPY copy;
  ADMISSION_G3_PROCESS *process;
  const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *sourcePte;
  ULONGLONG offset;
  APPLE_AGX_GPUVA_G3_WALK_FAILURE destinationWalk;
  UINT scratchBytes;
  PVOID scratch;
  int completed;
  if (Adapter == NULL || Context == NULL || Command == NULL ||
      BytesCopied == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !AdmissionPresentBltValidate(Command, Bytes, 1, &command) ||
      command.Version != ADMISSION_PRESENT_BLT_GPUVA_VERSION ||
      command.ContextToken != (ULONGLONG)(ULONG_PTR)Context ||
      !AdmissionPresentBltScratchBytes(&command, &scratchBytes) ||
      Context->GpuvaG3Process == NULL || Context->GpuvaG3Poisoned)
    return STATUS_INVALID_PARAMETER;
  *BytesCopied = 0ULL;
  Adapter->PresentCopyFaultVa = 0ULL;
  Adapter->PresentCopyFaultWrite = 0u;
  process = (ADMISSION_G3_PROCESS *)Context->GpuvaG3Process;
  if (process->State == NULL || process->State->Adapter != Adapter)
    return STATUS_INVALID_HANDLE;
  RtlZeroMemory(&copy, sizeof(copy));
  copy.Process = process;
  copy.Status = STATUS_SUCCESS;
  copy.Status = AdmissionMemoryRuntimeLocalView(Adapter, &copy.Local);
  (void)AdmissionMemoryPoolView(Adapter, &copy.Pool);
  if (!NT_SUCCESS(copy.Status))
    return copy.Status;
  scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, scratchBytes,
      ADMISSION_POOL_TAG);
  if (scratch == NULL)
    return STATUS_INSUFFICIENT_RESOURCES;
  ExAcquireFastMutex(&process->State->Lock);
  RtlZeroMemory(&destinationWalk, sizeof(destinationWalk));
  completed = !process->Poisoned && !process->Graph.Uncertain &&
      Context->GpuvaG3RootIpa != 0ULL &&
      Context->GpuvaG3RootIpa == process->Graph.RootIpa &&
      command.SourceDescription.Size <= MAXUINT &&
      (command.SourceLocation & 0xfffULL) == 0ULL &&
      command.DestinationDescription.Size <= MAXUINT &&
      AppleAgxGpuvaG3GraphInspectRangeAccess(&process->Graph,
          command.DestinationLocation,
          (UINT)command.DestinationDescription.Size, TRUE,
          &destinationWalk);
  if (!completed && destinationWalk.Va != 0ULL) {
    copy.FaultVa = destinationWalk.Va;
    copy.FaultWrite = 1u;
  }
  copy.SourceVa = command.SourceLocation;
  for (offset = 0ULL; completed &&
       offset < command.SourceDescription.Size; offset += 0x1000ULL) {
    sourcePte = AdmissionG3CopyPte(process, command.SourceLocation + offset);
    if (sourcePte == NULL || sourcePte->GuestIpa == 0ULL ||
        (sourcePte->Flags & APPLE_AGX_GPUVA_G3_VALID) == 0u ||
        (sourcePte->Flags & ~(APPLE_AGX_GPUVA_G3_VALID |
                              APPLE_AGX_GPUVA_G3_WRITE)) != 0u ||
        sourcePte->SegmentId > ADMISSION_MEMORY_POOL_SEGMENT ||
        sourcePte->Allocation == 0ULL ||
        sourcePte->AllocationOffset != offset ||
        (copy.SourceAllocation != 0ULL &&
         sourcePte->Allocation != copy.SourceAllocation)) {
      copy.FaultVa = command.SourceLocation + offset;
      copy.FaultWrite = 0u;
      completed = 0;
      break;
    }
    copy.SourceAllocation = sourcePte->Allocation;
  }
  if (completed)
    completed = AdmissionPresentBltExecuteGpuvaSeparate(Command, Bytes,
        AdmissionG3PresentTranslateSource, AdmissionG3PresentTranslate,
        AdmissionG3PresentRead, AdmissionG3PresentWrite, &copy, scratch,
        scratchBytes, BytesCopied);
  if (!completed) {
    Adapter->PresentCopyFaultVa = copy.FaultVa;
    Adapter->PresentCopyFaultWrite = copy.FaultWrite;
  }
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *destinationPte =
        AdmissionG3CopyPte(process, command.DestinationLocation);
    AdmissionDwmFrameRecordCopy(Adapter, Context,
        destinationPte != NULL &&
            (destinationPte->Flags & APPLE_AGX_GPUVA_G3_VALID) != 0u
                ? destinationPte->GuestIpa : 0ULL,
        *BytesCopied, completed ? STATUS_SUCCESS :
            NT_SUCCESS(copy.Status) ? STATUS_INVALID_ADDRESS : copy.Status);
  }
#endif
  KeMemoryBarrier();
  ExReleaseFastMutex(&process->State->Lock);
  ExFreePoolWithTag(scratch, ADMISSION_POOL_TAG);
  return completed ? STATUS_SUCCESS :
      NT_SUCCESS(copy.Status) ? STATUS_INVALID_ADDRESS : copy.Status;
}

static int AdmissionG4GraphAccessTyped(void *opaque, unsigned long long va,
    unsigned int bytes, int write, APPLE_AGX_G4_ACCESS_KIND kind,
    unsigned int ordinal) {
  ADMISSION_G3_PROCESS *process = (ADMISSION_G3_PROCESS *)opaque;
  UNREFERENCED_PARAMETER(ordinal);
  if (process == NULL) return 0;
  if (bytes && va < process->PrivateVa + APPLE_AGX_G3_PRIVATE_VA_BYTES &&
      va + bytes > process->PrivateVa) return 0;
  /* rc80: the TVB window is KMD-owned like the private window. */
  if (bytes && process->TvbVa && va < process->TvbVa + APPLE_AGX_G3_TVB_VA_BYTES &&
      va + bytes > process->TvbVa) return 0;
  if (kind == AppleAgxG4AccessCpuEnvelope)
    return !write && AdmissionG4LogicalEnvelopeAccess(process, va, bytes);
  return AdmissionG4GraphAccess(&process->Graph, va, bytes, write);
}

static int AdmissionG4LogicalEnvelopeAccess(ADMISSION_G3_PROCESS *process,
    unsigned long long va, unsigned int bytes) {
  unsigned long long end, page;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  ULONGLONG table_ipa, previous_table = 0ULL;
  const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte;
  if (process == NULL || !process->Graph.Created ||
      process->Graph.Uncertain || process->Graph.RootIpa == 0ULL ||
      !bytes || va < 0x10000ULL || va >= (1ULL << 39) ||
      bytes > (1ULL << 39) - va) return 0;
  end = va + bytes;
  shadow = NULL;
  for (page = va & ~0xfffULL; page < end; page += 0x1000ULL) {
    if (!AppleAgxGpuvaG3GraphLeafTableIpa(&process->Graph,
            page, &table_ipa)) return 0;
    if (table_ipa != previous_table || shadow == NULL) {
      ULONG bucket = (ULONG)((table_ipa >> 14) ^ (table_ipa >> 22) ^
          (table_ipa >> 30)) & 255u;
      for (shadow = process->TableShadowBrokerBuckets[bucket];
           shadow != NULL; shadow = shadow->NextBroker)
        if (shadow->BrokerIpa == table_ipa) break;
      previous_table = table_ipa;
    }
    if (shadow == NULL || shadow->LogicalPtes == NULL) return 0;
    pte = &shadow->LogicalPtes[(ULONG)((page >> 12) & 8191u)];
    if ((pte->Flags & APPLE_AGX_GPUVA_G3_VALID) == 0u ||
        (pte->Flags & ~(APPLE_AGX_GPUVA_G3_VALID |
                        APPLE_AGX_GPUVA_G3_WRITE)) != 0u ||
        pte->GuestIpa == 0ULL ||
        pte->SegmentId > ADMISSION_MEMORY_POOL_SEGMENT) return 0;
  }
  return 1;
}

static void AdmissionG4SnapshotFailure(
    ADMISSION_G3_PROCESS *process, const APPLE_AGX_G4_FAILURE *failure,
    struct _ADMISSION_G4_SUBMIT_FAILURE *snapshot) {
  ULONGLONG group, table_ipa;
  APPLE_AGX_GPUVA_G3_NODE *edge;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  ULONG i;
  if (process == NULL || failure == NULL || snapshot == NULL) return;
  snapshot->Subsite = failure->Subsite;
  snapshot->Kind = failure->Kind;
  snapshot->Ordinal = failure->Ordinal;
  snapshot->Va = failure->Va;
  snapshot->AccessBytes = failure->Bytes;
  snapshot->Write = failure->Write;
  snapshot->OwnerProcessId = process->Graph.ProcessId;
  snapshot->OwnerOsProcessId = process->OsProcessId;
  snapshot->RootIpa = process->Graph.RootIpa;
  snapshot->ProcessGeneration = process->Graph.ProcessGeneration;
  snapshot->MappingGeneration = process->Graph.MappingGeneration;
  snapshot->GraphPresent = AppleAgxGpuvaG3GraphContainsRangeAccess(
      &process->Graph, failure->Va, failure->Bytes, failure->Write != 0);
  if (failure->Va >= (1ULL << 39)) return;
  group = failure->Va & ~0x3fffULL;
  for (edge = process->Graph.Parents; edge != NULL; edge = edge->Next)
    if (edge->Ipa == process->Graph.RootIpa &&
        edge->Index == (ULONG)((group >> 36) & 7u)) break;
  if (edge == NULL) return;
  table_ipa = edge->AuxIpa;
  for (edge = process->Graph.Parents; edge != NULL; edge = edge->Next)
    if (edge->Ipa == table_ipa &&
        edge->Index == (ULONG)((group >> 25) & 2047u)) break;
  if (edge == NULL) return;
  for (shadow = process->TableShadows; shadow != NULL; shadow = shadow->Next)
    if (shadow->BrokerIpa == edge->AuxIpa) break;
  if (shadow == NULL || shadow->LogicalPtes == NULL) return;
  for (i = 0u; i < 4u; ++i) {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte =
        &shadow->LogicalPtes[((ULONG)(group >> 12) & 8191u) + i];
    snapshot->LogicalIpa[i] = pte->GuestIpa;
    snapshot->LogicalSegment[i] = pte->SegmentId;
    snapshot->LogicalFlags[i] = pte->Flags;
    snapshot->LogicalAllocation[i] = pte->Allocation;
    snapshot->LogicalAllocationOffset[i] = pte->AllocationOffset;
  }
  /* Select the actual failing 4-KiB page, not necessarily group[0]. Both
   * the shadow and leaf metadata writers use the caller's G3 state lock.
   * Existing fill/transfer counters are best-effort unlocked diagnostics;
   * validate the copied identity too. Never dereference a shadow handle. */
  if (process->State != NULL) {
    ULONGLONG key = snapshot->LogicalAllocation[(failure->Va >> 12) & 3u];
    const ADMISSION_G3_ALLOC_TRACK *track = &process->State->AllocTrack[
        (ULONG)((key >> 6) ^ (key >> 16)) % ADMISSION_G3_ALLOC_TRACK_COUNT];
    if (key != 0ULL && track->Allocation == key &&
        track->ProcessId == process->Graph.ProcessId) {
      snapshot->Track = *track;
      if (snapshot->Track.Allocation == key &&
          snapshot->Track.ProcessId == process->Graph.ProcessId &&
          track->Allocation == key &&
          track->ProcessId == process->Graph.ProcessId)
        snapshot->TrackFound = 1u;
      else
        RtlZeroMemory(&snapshot->Track, sizeof(snapshot->Track));
    }
  }
}

/* The attachment's offset in the local reserve when all of it lies there
 * IPA-contiguously. */
static BOOLEAN AdmissionG4OutputInLocal(
    ADMISSION_G3_PROCESS *process,
    const APPLE_AGX_G4_ATTACHMENT *attachment,
    const ADMISSION_SCANOUT_MEMORY_VIEW *local, ULONGLONG *offset) {
  ULONGLONG ipa, position;
  if (!AppleAgxGpuvaG3GraphTranslateVa(&process->Graph,
          attachment->Pointer, &ipa) || ipa < local->GuestIpaAddress)
    return FALSE;
  *offset = ipa - local->GuestIpaAddress;
  if (*offset > local->Bytes ||
      attachment->Size > local->Bytes - *offset ||
      local->HostPhysicalAddress > MAXULONGLONG - *offset)
    return FALSE;
  for (position = 0ULL; position < attachment->Size;) {
    ULONGLONG mapped;
    if (!AppleAgxGpuvaG3GraphTranslateVa(&process->Graph,
            attachment->Pointer + position, &mapped) ||
        mapped != ipa + position) return FALSE;
    position += 0x4000ULL -
        ((attachment->Pointer + position) & 0x3fffULL);
  }
  return TRUE;
}

/* The GPU writes the colour attachment through the process graph (GPU VA).
 * A target in the local reserve also gets its local CPU token and host
 * physical address (the CPU output capture and GDI paths read them); any
 * other target -- M2-c: system memory, 64 KB-backed -- is addressed by VA
 * only and needs its range mapped writable for this process. */
static BOOLEAN AdmissionG4ResolveOutput(
    ADMISSION_G3_PROCESS *process,
    const APPLE_AGX_G4_ATTACHMENT *attachment,
    const ADMISSION_SCANOUT_MEMORY_VIEW *local,
    ADMISSION_RENDER_PACKET_DESCRIPTION *packet) {
  ULONGLONG offset;
  if (process == NULL || attachment == NULL || local == NULL ||
      packet == NULL || attachment->Pointer == 0ULL ||
      attachment->Size == 0ULL || attachment->Size > MAXULONG)
    return FALSE;
  if (AdmissionG4OutputInLocal(process, attachment, local, &offset)) {
    packet->DestinationCpuToken =
        (ULONGLONG)(ULONG_PTR)((PUCHAR)local->CpuAddress + (SIZE_T)offset);
    packet->DestinationPhysical = local->HostPhysicalAddress + offset;
  } else if (AppleAgxGpuvaG3GraphContainsRangeAccessCached(&process->Graph,
                 attachment->Pointer, (UINT)attachment->Size, TRUE)) {
    packet->DestinationCpuToken = 0ULL;
    packet->DestinationPhysical = 0ULL;
  } else {
    return FALSE;
  }
  packet->DestinationGpuVa = attachment->Pointer;
  packet->DestinationBytes = (ULONG)attachment->Size;
  return TRUE;
}

#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
/* Stage2 is called while State.Lock still protects process/scenes. Stage1
 * records only context-owned scalars; it never dereferences a failed process. */
static VOID AdmissionG4ObserveEnvelopeReject(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args, ULONG stage,
    ULONG predicate, ULONG runtimePredicate, ADMISSION_G3_PROCESS *process, const APPLE_AGX_G4_PRIVATE_LEASE *lease) {
  ADMISSION_DWM_ENVELOPE_RECEIPT r = {0};
  ADMISSION_G3_PRIVATE_SCENE *scene;
  r.Predicate = predicate; r.RuntimePredicate = runtimePredicate;
  r.Stage = stage; r.Irql = (ULONG)KeGetCurrentIrql();
  r.Flags = args->Flags.Value; r.Fence = args->SubmissionFenceId;
  r.InterruptTime = KeQueryInterruptTime();
  r.Device = (ULONGLONG)(ULONG_PTR)context->Object.Device;
  r.ContextState = (context->GpuvaG3Closing ? 1u : 0u) |
      (context->GpuvaG3Poisoned ? 2u : 0u) |
      (InterlockedCompareExchange(&context->GpuvaG3CancelUncertain,0,0) ? 4u : 0u) |
      (context->SchedulerContext.Active ? 8u : 0u) |
      (context->Win32Transport ? 16u : 0u);
  r.PrivateFence = (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0);
  r.PreemptFence = (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence,0,0);
  r.CancelFence = (ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0);
  r.ContextGeneration = context->GpuvaG3PrivateManagerGeneration;
  if (stage == 2u && process != NULL && lease != NULL) {
    r.ManagerGeneration = process->PrivateManager.Generation;
    r.LeaseManagerId = lease->ManagerId;
    r.LeaseManagerGeneration = lease->ManagerGeneration;
    r.LeaseSceneId = lease->SceneId;
    r.LeaseSceneGeneration = lease->SceneGeneration;
    for (scene=process->PrivateScenes; scene; scene=scene->Next)
      if (scene->Storage.Generation == lease->SceneId) break;
    if (scene != NULL) {
      r.SceneGeneration = scene->Storage.Generation;
      r.SceneFence = scene->Fence;
      r.SceneState = 1u | (scene->Context != context ? 2u : 0u) |
          (scene->Quarantined ? 4u : 0u) | (scene->Queued ? 8u : 0u) |
          (scene->Submitting ? 16u : 0u) | (scene->Started ? 32u : 0u) |
          (scene->ReleaseRequested ? 64u : 0u);
    }
  }
  AdmissionDwmFrameRecordEnvelope(adapter,context,&r);
}
#endif

/* Phase 5a: a submission may be queued behind the render slot when its
 * context has nothing outstanding, a pending entry is free and the runtime
 * is only busy: another job bound or running, entries queued, or (EXP1135)
 * the worker still finishing the previous job with the slot already empty,
 * the most frequent wait in EXP1130 (ReadyEx predicate 8). */
static BOOLEAN AdmissionG4PendingAdmissible(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context) {
  KIRQL old_irql;
  BOOLEAN admissible, worker_busy = FALSE;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !AdmissionPlatformRuntimeQueueable(adapter, &worker_busy))
    return FALSE;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  /* Phase 5b: up to two jobs per context (oldest + second). */
  admissible = (context->Object.FenceOutstanding == 0u ||
                context->GpuvaG3SecondFence == 0u) &&
      adapter->G4PendingCount < ADMISSION_G4_PENDING_CAPACITY &&
      (AdmissionRenderPacketState(&adapter->RenderPacket) !=
           AdmissionRenderPacketEmpty || adapter->G4PendingCount != 0u ||
       worker_busy) &&
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0) == 0
      ? TRUE : FALSE;
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  return admissible;
}

/* Receipt only. Caller holds SchedulerLock at the failing decision, before
 * rollback. The Admissible bit records the original path selection; never
 * re-run PendingAdmissible here (it also checks runtime state outside this
 * lock). Raw state flags are observations, not all causal on every path. */
static VOID AdmissionG4CapturePrepareLocked(ADMISSION_CONTEXT *adapter,
    const ADMISSION_RENDER_CONTEXT *context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args, ULONG site, ULONG mask,
    struct _ADMISSION_G4_SUBMIT_FAILURE *detail) {
  ULONG tail, distance;
  if (detail == NULL) return;
  tail = (adapter->G4PendingHead + adapter->G4PendingCount) %
      ADMISSION_G4_PENDING_CAPACITY;
  detail->PrepareSite = site;
  detail->PrepareFence = args->SubmissionFenceId;
  detail->PrepareFenceOutstanding = context->Object.FenceOutstanding;
  detail->PrepareSecondFence = context->GpuvaG3SecondFence;
  detail->PreparePendingCount = adapter->G4PendingCount;
  detail->PreparePendingCapacity = ADMISSION_G4_PENDING_CAPACITY;
  detail->PrepareTail = tail;
  detail->PrepareTailState = (ULONG)adapter->G4Pending[tail].State;
  detail->PreparePacketState = (ULONG)AdmissionRenderPacketState(&adapter->RenderPacket);
  detail->PrepareSchedulerFaulted = (ULONG)InterlockedCompareExchange(
      &adapter->SchedulerFaulted, 0, 0);
  detail->PreparePreemptionPhase = (ULONG)adapter->Scheduler.PreemptionPhase;
  detail->PrepareSchedulerQueueCount = adapter->Scheduler.QueueCount;
  detail->PrepareSchedulerQueueCapacity = APPLE_AGX_SCHEDULER_QUEUE_CAPACITY;
  detail->PrepareSchedulerLastSubmittedFence = adapter->Scheduler.LastSubmittedFence;
  detail->PrepareEntryState = 0u; /* Set by the publish caller, if applicable. */
  if (detail->PrepareFenceOutstanding != 0u)
    mask |= AdmissionG4PrepareFenceOutstanding;
  if (detail->PrepareSecondFence != 0u)
    mask |= AdmissionG4PrepareSecondFence;
  if (detail->PreparePendingCount >= ADMISSION_G4_PENDING_CAPACITY)
    mask |= AdmissionG4PreparePendingFull;
  if (detail->PrepareTailState != (ULONG)AdmissionG4PendingFree)
    mask |= AdmissionG4PrepareTailBusy;
  if (detail->PreparePacketState != AdmissionRenderPacketEmpty)
    mask |= AdmissionG4PreparePacketBusy;
  if (detail->PrepareSchedulerFaulted != 0u)
    mask |= AdmissionG4PrepareFaulted;
  if ((mask & AdmissionG4PrepareQueueRefused) != 0u) {
    /* Mirror QueueFence's read-only predicates; do not call it a second
     * time. Regression cases compare these with the real scheduler. */
    if (!AppleAgxSchedulerValidateEngine(&adapter->Scheduler, 0u, 0u))
      mask |= AdmissionG4PrepareInvalidEngine;
    if (args->SubmissionFenceId == 0u)
      mask |= AdmissionG4PrepareZeroFence;
    if (adapter->Scheduler.QueueCount >= APPLE_AGX_SCHEDULER_QUEUE_CAPACITY)
      mask |= AdmissionG4PrepareQueueFull;
    if (adapter->Scheduler.PreemptionPhase != AppleAgxPreemptionIdle &&
        adapter->Scheduler.PreemptionPhase != AppleAgxPreemptionNotificationClaimed)
      mask |= AdmissionG4PreparePreemptionBlocked;
    distance = args->SubmissionFenceId - adapter->Scheduler.LastSubmittedFence;
    if (distance == 0u || distance >= 0x80000000u)
      mask |= AdmissionG4PrepareFenceNotAfter;
  }
  if (site == AdmissionG4PreparePacket &&
      detail->PreparePacketState == AdmissionRenderPacketEmpty &&
      detail->PreparePendingCount == 0u &&
      detail->PrepareFenceOutstanding == 0u &&
      detail->PrepareSchedulerFaulted == 0u)
    mask |= AdmissionG4PreparePacketRefused;
  detail->PrepareMask = mask;
}

/* Phase 5a: copy a validated submission into the tail pending entry and
 * queue its fence. The entry is reserved under SchedulerLock, filled outside
 * it, and published with the fence; the worker binds it in order. */
static BOOLEAN AdmissionG4PendingQueue(ADMISSION_CONTEXT *adapter,
    ADMISSION_G3_STATE *state, ADMISSION_RENDER_CONTEXT *context,
    ADMISSION_G3_PRIVATE_SCENE *private_scene,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *packet,
    const APPLE_AGX_G4_SUBMIT_VIEW *view,
    const APPLE_AGX_G4_COMPUTE_ENVELOPE *compute,
    const ADMISSION_PRESENT_COMPUTE *present,
    ULONGLONG mapping_generation,
    struct _ADMISSION_G4_SUBMIT_FAILURE *detail) {
  ADMISSION_G4_PENDING *entry = NULL;
  KIRQL old_irql;
  ULONG slot;
  BOOLEAN queued = FALSE;
  /* G2: exactly one of a render view, a compute envelope or a present. */
  if ((compute != NULL && present != NULL) ||
      (compute != NULL || present != NULL ?
      (view != NULL || private_scene != NULL ||
       packet->Kind != ADMISSION_RENDER_PACKET_KIND_COMPUTE) :
      (view == NULL || view->Native == NULL || view->Render == NULL ||
       view->Attachments == NULL || view->AttachmentCount == 0u ||
       view->AttachmentCount > APPLE_AGX_G4_MAX_ATTACHMENTS ||
       view->RenderBytes != sizeof(APPLE_AGX_G4_NATIVE_RENDER) ||
       view->CommandBytes == 0u ||
       view->CommandBytes > APPLE_AGX_G4_NATIVE_MAX_BYTES))) {
    if (detail != NULL) {
      KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
      AdmissionG4CapturePrepareLocked(adapter, context, args,
          AdmissionG4PrepareShape, AdmissionG4PrepareDeferred |
          AdmissionG4PrepareAdmissible | AdmissionG4PrepareInvalidShape, detail);
      KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
    }
    return FALSE;
  }
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  slot = (adapter->G4PendingHead + adapter->G4PendingCount) %
      ADMISSION_G4_PENDING_CAPACITY;
  if ((context->Object.FenceOutstanding == 0u ||
       context->GpuvaG3SecondFence == 0u) &&
      adapter->G4PendingCount < ADMISSION_G4_PENDING_CAPACITY &&
      adapter->G4Pending[slot].State == AdmissionG4PendingFree &&
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0) == 0) {
    entry = &adapter->G4Pending[slot];
    entry->State = AdmissionG4PendingFilling;
    if (context->Object.FenceOutstanding == 0u)
      context->Object.FenceOutstanding = args->SubmissionFenceId;
    else
      context->GpuvaG3SecondFence = args->SubmissionFenceId;
  }
  if (entry == NULL)
    AdmissionG4CapturePrepareLocked(adapter, context, args,
        AdmissionG4PrepareReserve,
        AdmissionG4PrepareDeferred | AdmissionG4PrepareAdmissible, detail);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (entry == NULL)
    return FALSE;
  entry->Fence = args->SubmissionFenceId;
  entry->Context = context;
  entry->Packet = *packet;
  entry->MappingGeneration = mapping_generation;
  entry->DmaBufferVa = args->DmaBufferVirtualAddress;
  entry->DmaBufferBytes = args->DmaBufferSize;
  entry->Compute = compute != NULL ? TRUE : FALSE;
  entry->Present = present != NULL ? TRUE : FALSE;
  if (present != NULL) {
    RtlZeroMemory(&entry->View, sizeof(entry->View));
    RtlCopyMemory(&entry->PresentJob, present, sizeof(entry->PresentJob));
  } else if (compute != NULL) {
    RtlZeroMemory(&entry->View, sizeof(entry->View));
    RtlCopyMemory(&entry->ComputeEnvelope, compute,
                  sizeof(entry->ComputeEnvelope));
  } else {
    entry->View = *view;
    entry->View.Native = NULL;
    entry->View.Render = NULL;
    entry->View.Attachments = NULL;
    /* Attachment 0 (the first colour target) is the bound output; every
     * attachment is kept, proven at parse time, for the firmware list. */
    RtlCopyMemory(entry->Attachments, view->Attachments,
                  view->AttachmentCount * sizeof(entry->Attachments[0]));
    entry->View.AttachmentCount = view->AttachmentCount;
    RtlCopyMemory(entry->Render, view->Render, sizeof(entry->Render));
    RtlCopyMemory(entry->Native, view->Native, view->CommandBytes);
  }
  ExAcquireFastMutex(&state->Lock);
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (entry->State == AdmissionG4PendingFilling &&
      AppleAgxSchedulerQueueFence(&adapter->Scheduler, 0u, 0u,
          args->SubmissionFenceId)) {
    entry->State = AdmissionG4PendingQueued;
    ++adapter->G4PendingCount;
    queued = TRUE;
  } else {
    AdmissionG4CapturePrepareLocked(adapter, context, args,
        AdmissionG4PreparePublish, AdmissionG4PrepareDeferred |
        AdmissionG4PrepareAdmissible |
        (entry->State == AdmissionG4PendingFilling ?
            AdmissionG4PrepareQueueAttempted | AdmissionG4PrepareQueueRefused :
            AdmissionG4PrepareEntryChanged), detail);
    if (detail != NULL) detail->PrepareEntryState = (ULONG)entry->State;
    entry->State = AdmissionG4PendingFree;
    entry->Context = NULL;
    AdmissionContextRetireFence(context, args->SubmissionFenceId);
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (queued && private_scene) {
    private_scene->ResumeFence=0u;private_scene->Submitting=0u;
  }
  ExReleaseFastMutex(&state->Lock);
  return queued;
}

/* Phase 5a: bind the head pending submission into the empty render slot.
 * The packet is prepared under SchedulerLock (reserving the slot), bound
 * outside it, then queued unless preemption or reset dropped the entry
 * meanwhile. Returns FALSE only when an accepted job could not be bound. */
_Use_decl_annotations_ BOOLEAN AdmissionG4PendingBindHead(
    ADMISSION_CONTEXT *adapter) {
  ADMISSION_G4_PENDING *entry;
  ADMISSION_RENDER_CONTEXT *context;
  APPLE_AGX_G4_SUBMIT_VIEW view;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  KIRQL old_irql;
  BOOLEAN bound, dropped, ok = TRUE;
  if (adapter == NULL)
    return TRUE;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  if (adapter->G4PendingCount == 0u ||
      entry->State != AdmissionG4PendingQueued ||
      AdmissionRenderPacketState(&adapter->RenderPacket) !=
          AdmissionRenderPacketEmpty ||
      AppleAgxSchedulerQueuedFence(&adapter->Scheduler, 0u, 0u) != entry->Fence ||
      !AdmissionRenderPacketPrepare(&adapter->RenderPacket, &entry->Packet)) {
    KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
    return TRUE;
  }
  entry->State = AdmissionG4PendingBinding;
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  context = entry->Context;
  view = entry->View;
  view.Native = entry->Native;
  view.Render = entry->Render;
  view.Attachments = entry->Attachments;
  RtlZeroMemory(&binding, sizeof(binding));
  bound = (entry->Present ?
      AdmissionBackendImageBindPresentCompute(&adapter->BackendImage,
          entry->Fence, &entry->PresentJob) :
      entry->Compute ?
      AdmissionBackendImageBindG4Compute(&adapter->BackendImage,
          entry->Fence, &entry->ComputeEnvelope) :
      AdmissionBackendImageBindG4Submission(&adapter->BackendImage,
          &entry->Packet, (PVOID)(ULONG_PTR)entry->Packet.DestinationCpuToken,
          &view, &binding)) ? TRUE : FALSE;
  if (bound) {
    context->GpuvaG3MappingGeneration = entry->MappingGeneration;
    context->GpuvaG3DmaBufferVa = entry->DmaBufferVa;
    context->GpuvaG3DmaBufferBytes = entry->DmaBufferBytes;
  }
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  dropped = entry->State == AdmissionG4PendingDropped ? TRUE : FALSE;
  if (!bound || dropped ||
      !AdmissionRenderPacketQueue(&adapter->RenderPacket, entry->Fence,
          (ULONGLONG)(ULONG_PTR)context, entry->Packet.PrivateDataToken, 0u,
          entry->Packet.DmaEnd)) {
    /* Clear before the slot empties: a resubmission may bind right after. */
    context->GpuvaG3DmaBufferVa = 0ULL;
    context->GpuvaG3DmaBufferBytes = 0u;
    (void)AdmissionRenderPacketCancelPrepared(&adapter->RenderPacket,
        (ULONGLONG)(ULONG_PTR)context);
    if (bound)
      (void)AdmissionBackendImageReleaseSubmission(&adapter->BackendImage,
          entry->Fence);
    if (!dropped) {
      /* An accepted fence cannot complete: cancel the scene and let the
       * caller fault the scheduler (TDR recovery). */
      AdmissionGpuvaG3PrivateCancel(context, entry->Fence, FALSE);
      AdmissionContextRetireFence(context, entry->Fence);
      ok = FALSE;
    }
  }
  if (ok && !dropped) InterlockedIncrement(&adapter->SubmitPath.Binds);
  entry->State = AdmissionG4PendingFree;
  entry->Context = NULL;
  adapter->G4PendingHead = (adapter->G4PendingHead + 1u) %
      ADMISSION_G4_PENDING_CAPACITY;
  --adapter->G4PendingCount;
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  return ok;
}

/* Phase 5a: preemption and reset treat every pending submission like the
 * queued packet: the context is released and its private scene preempted
 * (VidSch resubmits it) or cancelled. An entry the worker is binding is
 * marked dropped; the worker then cancels its prepared packet. Called with
 * SchedulerLock held. */
_Use_decl_annotations_ VOID AdmissionG4PendingDropLocked(
    ADMISSION_CONTEXT *adapter, BOOLEAN preempt) {
  ULONG index, kept = 0u;
  if (adapter == NULL)
    return;
  for (index = 0u; index < adapter->G4PendingCount; ++index) {
    ADMISSION_G4_PENDING *entry = &adapter->G4Pending[
        (adapter->G4PendingHead + index) % ADMISSION_G4_PENDING_CAPACITY];
    ADMISSION_RENDER_CONTEXT *context = entry->Context;
    /* rc102: a kicked entry is with the firmware: it completes (or a reset
     * aborts it, AdmissionG4PendingAbortKickedLocked). */
    if (entry->State == AdmissionG4PendingDropped ||
        entry->State == AdmissionG4PendingKicked) {
      kept = 1u;
      continue;
    }
    if (context != NULL &&
        (context->Object.FenceOutstanding == entry->Fence ||
         context->GpuvaG3SecondFence == entry->Fence)) {
      if (preempt) AdmissionGpuvaG3PrivatePreempt(context, entry->Fence);
      else AdmissionGpuvaG3PrivateCancel(context, entry->Fence, FALSE);
      AdmissionContextRetireFence(context, entry->Fence);
    }
    if (adapter->DispatchedFence == entry->Fence)
      adapter->DispatchedFence = 0u;
    /* rc90: a Prebound entry holds the backend image like a Binding one;
     * the worker that bound it releases the image and frees it. */
    if (entry->State == AdmissionG4PendingBinding ||
        entry->State == AdmissionG4PendingPrebound) {
      entry->State = AdmissionG4PendingDropped;
      kept = 1u;
    } else {
      entry->State = AdmissionG4PendingFree;
      entry->Context = NULL;
    }
  }
  adapter->G4PendingCount = kept;
}

/* rc90 build-ahead (render_job_ahead.h). The decision values mirror the
 * enums they are compared with. SchedulerFaulted set here: file tag 5. */
C_ASSERT((UINT)AdmissionG4PendingQueued == ADMISSION_JOB_AHEAD_PENDING_QUEUED);
C_ASSERT((UINT)AdmissionG4PendingDropped == ADMISSION_JOB_AHEAD_PENDING_DROPPED);
C_ASSERT((UINT)AdmissionG4PendingPrebound == ADMISSION_JOB_AHEAD_PENDING_PREBOUND);
C_ASSERT((UINT)AdmissionRenderPacketEmpty == ADMISSION_JOB_AHEAD_PACKET_EMPTY);
C_ASSERT((UINT)AdmissionRenderPacketActive == ADMISSION_JOB_AHEAD_PACKET_ACTIVE);
C_ASSERT(AppleAgxPreemptionIdle == 0);

_Use_decl_annotations_ VOID AdmissionJobAheadFillLocked(
    ADMISSION_CONTEXT *adapter, ADMISSION_JOB_AHEAD_STATE *s) {
  const ADMISSION_BACKEND_IMAGE *image;
  const ADMISSION_G4_PENDING *head;
  if (adapter == NULL || s == NULL) return;
  image = &adapter->BackendImage;
  head = &adapter->G4Pending[adapter->G4PendingHead];
  s->Faulted = InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0) != 0
      ? 1u : 0u;
  s->PreemptionPhase =
      (unsigned int)AppleAgxSchedulerPreemptionPhase(&adapter->Scheduler);
  s->PagingPending =
      InterlockedCompareExchange(&adapter->PagingPending, 0, 0) != 0 ? 1u : 0u;
  s->QueuedFence = AppleAgxSchedulerQueuedFence(&adapter->Scheduler, 0u, 0u);
  s->ActiveFence = AppleAgxSchedulerActiveFence(&adapter->Scheduler, 0u, 0u);
  s->DispatchedFence = adapter->DispatchedFence;
  s->PacketState = (unsigned int)AdmissionRenderPacketState(&adapter->RenderPacket);
  s->PacketFence = adapter->RenderPacket.Description.Fence;
  s->PendingCount = adapter->G4PendingCount;
  s->PendingHeadState = adapter->G4PendingCount != 0u ? (unsigned int)head->State : 0u;
  s->PendingHeadFence = adapter->G4PendingCount != 0u ? head->Fence : 0u;
  s->ImageBoundFence = image->BoundFence;
  s->ImageKind = image->BoundFence == 0u ? ADMISSION_JOB_AHEAD_IMAGE_NONE :
      image->G4Compute ? ADMISSION_JOB_AHEAD_IMAGE_G4_COMPUTE :
      image->PresentCompute ? ADMISSION_JOB_AHEAD_IMAGE_PRESENT :
      image->G4Native ? ADMISSION_JOB_AHEAD_IMAGE_G4_RENDER :
      ADMISSION_JOB_AHEAD_IMAGE_OTHER;
  s->ImageFastRelease = image->BoundFence != 0u &&
      AdmissionBackendImageReleasesWithoutPrepare(image,
          &adapter->BackendSnapshot, image->BoundFence) ? 1u : 0u;
}

/* rc90 build-ahead: while the running job (State->JobFence, its image
 * released right after its kick) runs, bind the head pending submission,
 * the scheduler's next fence, into the free backend image: the same binds
 * as AdmissionG4PendingBindHead, without the render packet, which still
 * holds the running job. The context's DMA range and mapping generation
 * are applied only when the worker promotes the entry (BeginJob of the
 * running job already read them). A refused or dropped bind leaves the
 * entry for the ordinary path. */
_Use_decl_annotations_ BOOLEAN AdmissionG4PendingPrebindHead(
    ADMISSION_CONTEXT *adapter, ADMISSION_JOB_AHEAD_STATE *State,
    ULONG *Refusal) {
  ADMISSION_G4_PENDING *entry;
  APPLE_AGX_G4_SUBMIT_VIEW view;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  KIRQL old_irql;
  ULONG refusal;
  BOOLEAN bound, dropped, prebound = FALSE;
  if (Refusal != NULL) *Refusal = 0u;
  if (adapter == NULL || State == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return FALSE;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  AdmissionJobAheadFillLocked(adapter, State);
  refusal = AdmissionJobAheadPrebindRefusal(State);
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  if (refusal == 0u && adapter->G4PrebindFence != 0u) refusal = 27u;
  if (refusal == 0u) {
    /* Binding: a preemption or reset meanwhile marks it Dropped. */
    entry->State = AdmissionG4PendingBinding;
    adapter->G4PrebindFence = entry->Fence;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (Refusal != NULL) *Refusal = refusal;
  if (refusal != 0u) return FALSE;
  view = entry->View;
  view.Native = entry->Native;
  view.Render = entry->Render;
  view.Attachments = entry->Attachments;
  RtlZeroMemory(&binding, sizeof(binding));
  bound = (entry->Present ?
      AdmissionBackendImageBindPresentCompute(&adapter->BackendImage,
          entry->Fence, &entry->PresentJob) :
      entry->Compute ?
      AdmissionBackendImageBindG4Compute(&adapter->BackendImage,
          entry->Fence, &entry->ComputeEnvelope) :
      AdmissionBackendImageBindG4Submission(&adapter->BackendImage,
          &entry->Packet, (PVOID)(ULONG_PTR)entry->Packet.DestinationCpuToken,
          &view, &binding)) ? TRUE : FALSE;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  dropped = entry->State == AdmissionG4PendingDropped ? TRUE : FALSE;
  if (bound && !dropped) {
    entry->State = AdmissionG4PendingPrebound;
    prebound = TRUE;
  } else {
    /* As AdmissionG4PendingBindHead: the release of a G4 binding restores
     * the snapshot; a refused bind left nothing bound (the next bind
     * re-Prepares the arena). */
    if (bound)
      (void)AdmissionBackendImageReleaseSubmissionRestore(
          &adapter->BackendImage, &adapter->BackendSnapshot, entry->Fence);
    if (dropped) {
      entry->State = AdmissionG4PendingFree;
      entry->Context = NULL;
      adapter->G4PendingHead = (adapter->G4PendingHead + 1u) %
          ADMISSION_G4_PENDING_CAPACITY;
      --adapter->G4PendingCount;
    } else {
      entry->State = AdmissionG4PendingQueued;
    }
    adapter->G4PrebindFence = 0u;
    if (Refusal != NULL) *Refusal = dropped ? 28u : 29u;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  return prebound;
}

/* rc90 build-ahead: the running job completed; its worker moves the
 * Prebound head into the empty render packet as Queued, exactly the state
 * AdmissionG4PendingBindHead leaves (the image already holds the binding). */
_Use_decl_annotations_ BOOLEAN AdmissionG4PendingAheadPromoteLocked(
    ADMISSION_CONTEXT *adapter, ULONG fence) {
  ADMISSION_G4_PENDING *entry;
  ADMISSION_RENDER_CONTEXT *context;
  if (adapter == NULL || fence == 0u || adapter->G4PendingCount == 0u)
    return FALSE;
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  context = entry->Context;
  if (entry->State != AdmissionG4PendingPrebound || entry->Fence != fence ||
      adapter->G4PrebindFence != fence || context == NULL ||
      adapter->BackendImage.BoundFence != fence ||
      AdmissionRenderPacketState(&adapter->RenderPacket) !=
          AdmissionRenderPacketEmpty ||
      AppleAgxSchedulerQueuedFence(&adapter->Scheduler, 0u, 0u) != fence ||
      !AdmissionRenderPacketPrepare(&adapter->RenderPacket, &entry->Packet))
    return FALSE;
  if (!AdmissionRenderPacketQueue(&adapter->RenderPacket, entry->Fence,
          (ULONGLONG)(ULONG_PTR)context, entry->Packet.PrivateDataToken, 0u,
          entry->Packet.DmaEnd)) {
    (void)AdmissionRenderPacketCancelPrepared(&adapter->RenderPacket,
        (ULONGLONG)(ULONG_PTR)context);
    return FALSE;
  }
  context->GpuvaG3MappingGeneration = entry->MappingGeneration;
  context->GpuvaG3DmaBufferVa = entry->DmaBufferVa;
  context->GpuvaG3DmaBufferBytes = entry->DmaBufferBytes;
  InterlockedIncrement(&adapter->SubmitPath.Binds);
  entry->State = AdmissionG4PendingFree;
  entry->Context = NULL;
  adapter->G4PendingHead = (adapter->G4PendingHead + 1u) %
      ADMISSION_G4_PENDING_CAPACITY;
  --adapter->G4PendingCount;
  adapter->G4PrebindFence = 0u;
  return TRUE;
}

/* rc102 (render_admission.h): the Prebound head is kicked behind the
 * running job. */
_Use_decl_annotations_ BOOLEAN AdmissionG4PendingKickLocked(
    ADMISSION_CONTEXT *adapter, ULONG fence) {
  ADMISSION_G4_PENDING *entry;
  ADMISSION_RENDER_CONTEXT *context;
  if (adapter == NULL || fence == 0u || adapter->G4PendingCount == 0u)
    return FALSE;
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  context = entry->Context;
  if (entry->State != AdmissionG4PendingPrebound || entry->Fence != fence ||
      adapter->G4PrebindFence != fence || context == NULL ||
      adapter->BackendImage.BoundFence != fence ||
      !AppleAgxSchedulerKickQueuedFence(&adapter->Scheduler, 0u, 0u, fence))
    return FALSE;
  context->GpuvaG3MappingGeneration = entry->MappingGeneration;
  context->GpuvaG3DmaBufferVa = entry->DmaBufferVa;
  context->GpuvaG3DmaBufferBytes = entry->DmaBufferBytes;
  entry->State = AdmissionG4PendingKicked;
  return TRUE;
}

/* rc102: the kicked head becomes the active packet: the binds and the
 * activation the dispatch and the worker would do for it (its image and
 * the firmware already hold it). */
_Use_decl_annotations_ BOOLEAN AdmissionG4PendingAdoptLocked(
    ADMISSION_CONTEXT *adapter, ULONG fence) {
  ADMISSION_G4_PENDING *entry;
  ADMISSION_RENDER_CONTEXT *context;
  if (adapter == NULL || fence == 0u || adapter->G4PendingCount == 0u)
    return FALSE;
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  context = entry->Context;
  if (entry->State != AdmissionG4PendingKicked || entry->Fence != fence ||
      context == NULL ||
      AppleAgxSchedulerKickedFence(&adapter->Scheduler) != fence ||
      AppleAgxSchedulerActiveFence(&adapter->Scheduler, 0u, 0u) != 0u ||
      AdmissionRenderPacketState(&adapter->RenderPacket) !=
          AdmissionRenderPacketEmpty ||
      !AdmissionRenderPacketPrepare(&adapter->RenderPacket, &entry->Packet))
    return FALSE;
  if (!AdmissionRenderPacketQueue(&adapter->RenderPacket, entry->Fence,
          (ULONGLONG)(ULONG_PTR)context, entry->Packet.PrivateDataToken, 0u,
          entry->Packet.DmaEnd)) {
    (void)AdmissionRenderPacketCancelPrepared(&adapter->RenderPacket,
        (ULONGLONG)(ULONG_PTR)context);
    return FALSE;
  }
  if (!AppleAgxSchedulerActivateFence(&adapter->Scheduler, 0u, 0u, fence) ||
      !AdmissionRenderPacketActivate(&adapter->RenderPacket, fence)) {
    /* The packet holds the kicked job; nothing can complete it now. */
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x50000L | __LINE__, 0);
    return FALSE;
  }
  adapter->DispatchedFence = fence;
  InterlockedIncrement(&adapter->SubmitPath.Binds);
  entry->State = AdmissionG4PendingFree;
  entry->Context = NULL;
  adapter->G4PendingHead = (adapter->G4PendingHead + 1u) %
      ADMISSION_G4_PENDING_CAPACITY;
  --adapter->G4PendingCount;
  adapter->G4PrebindFence = 0u;
  return TRUE;
}

_Use_decl_annotations_ VOID AdmissionG4PendingAbortKickedLocked(
    ADMISSION_CONTEXT *adapter) {
  ADMISSION_G4_PENDING *entry;
  ADMISSION_RENDER_CONTEXT *context;
  if (adapter == NULL || adapter->G4PendingCount == 0u)
    return;
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  if (entry->State != AdmissionG4PendingKicked)
    return;
  context = entry->Context;
  if (context != NULL &&
      (context->Object.FenceOutstanding == entry->Fence ||
       context->GpuvaG3SecondFence == entry->Fence)) {
    /* It may have run: cancelled, as the aborted active packet's scene. */
    AdmissionGpuvaG3PrivateCancel(context, entry->Fence, FALSE);
    AdmissionContextRetireFence(context, entry->Fence);
  }
  if (adapter->DispatchedFence == entry->Fence)
    adapter->DispatchedFence = 0u;
  /* The image still holds its binding (it was never adopted). */
  if (adapter->BackendImage.BoundFence == entry->Fence &&
      !AdmissionBackendImageReleaseSubmission(&adapter->BackendImage,
                                             entry->Fence))
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x50000L | __LINE__, 0);
  entry->State = AdmissionG4PendingFree;
  entry->Context = NULL;
  adapter->G4PendingHead = (adapter->G4PendingHead + 1u) %
      ADMISSION_G4_PENDING_CAPACITY;
  --adapter->G4PendingCount;
  adapter->G4PrebindFence = 0u;
}

/* rc90 build-ahead: the worker leaves (or does not chain) with an entry
 * still bound ahead: release the image; a dropped entry is freed (its
 * context was released by the drop), any other returns to Queued for
 * AdmissionG4PendingBindHead. PASSIVE_LEVEL, no lock held. */
_Use_decl_annotations_ VOID AdmissionG4PendingAheadUnbind(
    ADMISSION_CONTEXT *adapter) {
  ADMISSION_G4_PENDING *entry;
  KIRQL old_irql;
  ULONG fence;
  BOOLEAN dropped, released;
  if (adapter == NULL) return;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  fence = adapter->G4PrebindFence;
  entry = &adapter->G4Pending[adapter->G4PendingHead];
  /* rc102: a kicked entry belongs to the firmware: it is adopted or a
   * reset aborts it, never unbound. */
  if (fence != 0u && adapter->G4PendingCount != 0u && entry->Fence == fence &&
      entry->State == AdmissionG4PendingKicked) {
    KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
    return;
  }
  if (fence == 0u || adapter->G4PendingCount == 0u || entry->Fence != fence ||
      (entry->State != AdmissionG4PendingPrebound &&
       entry->State != AdmissionG4PendingDropped)) {
    if (fence != 0u)
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x50000L | __LINE__, 0);
    adapter->G4PrebindFence = 0u;
    KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
    return;
  }
  dropped = entry->State == AdmissionG4PendingDropped ? TRUE : FALSE;
  if (!dropped) entry->State = AdmissionG4PendingBinding;
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  released = AdmissionBackendImageReleaseSubmissionRestore(
      &adapter->BackendImage, &adapter->BackendSnapshot, fence) ? TRUE : FALSE;
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (dropped || entry->State == AdmissionG4PendingDropped) {
    entry->State = AdmissionG4PendingFree;
    entry->Context = NULL;
    adapter->G4PendingHead = (adapter->G4PendingHead + 1u) %
        ADMISSION_G4_PENDING_CAPACITY;
    --adapter->G4PendingCount;
  } else {
    entry->State = AdmissionG4PendingQueued;
  }
  adapter->G4PrebindFence = 0u;
  /* The image could not return to its Prepare state: no later bind can
   * succeed; the TDR reset recovers. */
  if (!released)
    InterlockedCompareExchange(&adapter->SchedulerFaulted, 0x50000L | __LINE__, 0);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
}

/* G2: a compute envelope becomes one standalone RunCompute job. It takes
 * the render slot exactly like a render job (one GPU job at a time, fence
 * order, preemption, reset and TDR) but binds no destination and no private
 * scene: its stores are process-VM accesses that the GPU MMU confines to
 * this process's VM slot, as for Asahi drm_asahi_cmd_compute. Called with
 * state->Lock held after the envelope-state and graph-root checks; releases
 * the lock. */
static NTSTATUS AdmissionG4SubmitComputeLocked(ADMISSION_CONTEXT *adapter,
    ADMISSION_G3_STATE *state, ADMISSION_G3_PROCESS *process,
    ADMISSION_RENDER_CONTEXT *context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args, BOOLEAN deferred) {
  APPLE_AGX_G4_COMPUTE_ENVELOPE envelope;
  APPLE_AGX_G4_COMPUTE_VIEW view;
  APPLE_AGX_G4_PARSE_RESULT result = AppleAgxG4ParseInvalid;
  APPLE_AGX_G4_FAILURE failure = {0};
  struct _ADMISSION_G4_SUBMIT_FAILURE detail = {0};
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  ULONGLONG mapping_generation;
  KIRQL old_irql;
  ULONG rollbackBranch = AdmissionG4RejectBind;
  BOOLEAN prepared = FALSE, queued = FALSE;
  RtlZeroMemory(&envelope, sizeof(envelope));
  RtlZeroMemory(&view, sizeof(view));
  /* Parse a private copy: the job runs exactly the envelope checked. */
  if (args->DmaBufferUmdPrivateDataSize == sizeof(envelope) &&
      args->DmaBufferPrivateDataSize >= sizeof(envelope)) {
    RtlCopyMemory(&envelope, args->pDmaBufferPrivateData, sizeof(envelope));
    result = AppleAgxG4ParseComputeEx(&envelope, (unsigned int)sizeof(envelope),
        (unsigned int)sizeof(envelope), args->DmaBufferVirtualAddress,
        args->DmaBufferSize, APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES,
        AdmissionG4GraphAccessTyped, process, &view, &failure);
  }
  mapping_generation = process->Graph.MappingGeneration;
  if (result != AppleAgxG4ParseOk)
    AdmissionG4SnapshotFailure(process, &failure, &detail);
  ExReleaseFastMutex(&state->Lock);
  if (result != AppleAgxG4ParseOk)
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectParse, STATUS_INVALID_PARAMETER, (ULONG)result,
        TRUE, &detail);
  RtlZeroMemory(&packet, sizeof(packet));
  packet.Kind = ADMISSION_RENDER_PACKET_KIND_COMPUTE;
  packet.Fence = args->SubmissionFenceId;
  packet.ContextToken = (ULONGLONG)(ULONG_PTR)context;
  packet.AllocationToken = (ULONGLONG)(ULONG_PTR)context;
  packet.PrivateDataToken =
      (ULONGLONG)(ULONG_PTR)&adapter->BackendImage.G4ComputeEnvelope;
  packet.PrivateDataBytes = (ULONG)sizeof(envelope);
  packet.PrivateDataEnd = packet.PrivateDataBytes;
  packet.DmaEnd = view.CommandBytes;
  if (deferred) {
    if (AdmissionG4PendingQueue(adapter, state, context, NULL, args,
            &packet, NULL, &envelope, NULL, mapping_generation, &detail)) {
      InterlockedIncrement(&adapter->SubmitPath.Deferred);
      AdmissionDispatchQueuedWork(adapter);
      return STATUS_SUCCESS;
    }
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectPrepare, STATUS_INVALID_PARAMETER, 0u, TRUE, &detail);
  }
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketEmpty &&
      adapter->G4PendingCount == 0u &&
      context->Object.FenceOutstanding == 0u &&
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0) == 0 &&
      AdmissionRenderPacketPrepare(&adapter->RenderPacket, &packet)) {
    context->Object.FenceOutstanding = args->SubmissionFenceId;
    prepared = TRUE;
  }
  if (!prepared)
    AdmissionG4CapturePrepareLocked(adapter, context, args,
        AdmissionG4PreparePacket, AdmissionG4PrepareImmediate, &detail);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (!prepared)
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectPrepare, STATUS_INVALID_PARAMETER, 0u, TRUE, &detail);
  if (!AdmissionBackendImageBindG4Compute(&adapter->BackendImage,
          args->SubmissionFenceId, &envelope)) {
    AdmissionG4RecordBindRefusal(adapter, 1u, args->SubmissionFenceId);
    goto Rollback;
  }
  context->GpuvaG3MappingGeneration = mapping_generation;
  context->GpuvaG3DmaBufferVa = args->DmaBufferVirtualAddress;
  context->GpuvaG3DmaBufferBytes = args->DmaBufferSize;
  rollbackBranch = AdmissionG4RejectQueue;
  ExAcquireFastMutex(&state->Lock);
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketQueue(&adapter->RenderPacket,
          args->SubmissionFenceId, (ULONGLONG)(ULONG_PTR)context,
          packet.PrivateDataToken, 0u, packet.DmaEnd)) {
    if (AppleAgxSchedulerQueueFence(&adapter->Scheduler, 0u, 0u,
            args->SubmissionFenceId))
      queued = TRUE;
    else
      (void)AdmissionRenderPacketReset(&adapter->RenderPacket,
          args->SubmissionFenceId, 0u);
  }
  if (!queued) AdmissionContextRetireFence(context, args->SubmissionFenceId);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  ExReleaseFastMutex(&state->Lock);
  if (queued) {
    InterlockedIncrement(&adapter->SubmitPath.Immediate);
    AdmissionDispatchQueuedWork(adapter);
    return STATUS_SUCCESS;
  }
Rollback:
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketPrepared)
    (void)AdmissionRenderPacketCancelPrepared(&adapter->RenderPacket,
        (ULONGLONG)(ULONG_PTR)context);
  AdmissionContextRetireFence(context, args->SubmissionFenceId);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  AdmissionPlatformRuntimeSlotChanged(adapter);
  context->GpuvaG3DmaBufferVa = 0ULL;
  context->GpuvaG3DmaBufferBytes = 0u;
  if (adapter->BackendImage.G4Compute)
    (void)AdmissionBackendImageReleaseSubmission(&adapter->BackendImage,
        args->SubmissionFenceId);
  return AdmissionG4SubmitReject(adapter, context, args,
      rollbackBranch, STATUS_INVALID_PARAMETER, 0u, TRUE);
}

/* G2 HW2b: DxgkDdiPresent Blt (GPUVA) as a compute job in the presenting
 * process's VM, queued exactly like a G4 compute job. What the worker builds
 * is proven here (AppleAgxPresentValidate, graph access), so the worker has
 * no refusal path. A present the GPU path cannot take (no arena, other
 * format, more rects than one job, a context or process not ready) returns
 * STATUS_NOT_SUPPORTED before claiming its fence and stays on the CPU path. */
static BOOLEAN AdmissionG3PresentRect(const APPLE_AGX_GDI_RECT *in,
    APPLE_AGX_PRESENT_RECT *out) {
  if (in->Left > 0x7fffffffu || in->Top > 0x7fffffffu ||
      in->Right > 0x7fffffffu || in->Bottom > 0x7fffffffu) return FALSE;
  out->Left = (int)in->Left; out->Top = (int)in->Top;
  out->Right = (int)in->Right; out->Bottom = (int)in->Bottom;
  return TRUE;
}

/* G2 HW2b receipt: why a present stayed on the CPU path (1 arguments,
 * 2 context, format or rect count, 3 rect, 4 overlapping surfaces, 5 runtime
 * busy, 6 no present arena, 7 root, validation or graph access, 8 queue or
 * render slot full); 0 is a GPU job, 9 a GPU refusal after its fence was
 * claimed. */
#define ADMISSION_PRESENT_CPU(why) \
  do { *reason = (why); return STATUS_NOT_SUPPORTED; } while (0)
static NTSTATUS AdmissionG3SubmitPresentComputeInner(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, const DXGKARG_SUBMITCOMMANDVIRTUAL *args,
    const ADMISSION_PRESENT_BLT_COMMAND *command,
    const APPLE_AGX_GDI_RECT *rects, ULONG count, ULONG *reason) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_BACKEND_MEMORY_VIEW view;
  ADMISSION_PRESENT_COMPUTE present;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  ULONGLONG mapping_generation;
  KIRQL old_irql;
  BOOLEAN deferred, prepared = FALSE, queued = FALSE;
  ULONG i;
  if (adapter == NULL || context == NULL || args == NULL || command == NULL ||
      rects == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    ADMISSION_PRESENT_CPU(1u);
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  if (state == NULL || process == NULL || process->State != state ||
      process->Poisoned || context->GpuvaG3Poisoned ||
      context->GpuvaG3RootIpa == 0ULL || !context->SchedulerContext.Active ||
      (context->Object.Flags & ADMISSION_CONTEXT_VIRTUAL_ADDRESSING) == 0u ||
      command->Version != ADMISSION_PRESENT_BLT_GPUVA_VERSION ||
      command->SourceDescription.BytesPerPixel != 4u ||
      command->DestinationDescription.BytesPerPixel != 4u ||
      count == 0u || count > APPLE_AGX_PRESENT_MAX_RECTS ||
      !NT_SUCCESS(AdmissionMemoryRuntimePrivateView(adapter, &view)) ||
      view.CpuAddress == NULL)
    ADMISSION_PRESENT_CPU(2u);
  RtlZeroMemory(&present, sizeof(present));
  present.Source.Va = command->SourceLocation;
  present.Source.Pitch = command->SourceDescription.Pitch;
  present.Source.Width = command->SourceDescription.Width;
  present.Source.Height = command->SourceDescription.Height;
  present.Destination.Va = command->DestinationLocation;
  present.Destination.Pitch = command->DestinationDescription.Pitch;
  present.Destination.Width = command->DestinationDescription.Width;
  present.Destination.Height = command->DestinationDescription.Height;
  if (!AdmissionG3PresentRect(&command->SourceRect, &present.SourceRect) ||
      !AdmissionG3PresentRect(&command->DestinationRect,
                              &present.DestinationRect))
    ADMISSION_PRESENT_CPU(3u);
  for (i = 0u; i < count; ++i)
    if (!AdmissionG3PresentRect(&rects[i], &present.Rects[i]))
      ADMISSION_PRESENT_CPU(3u);
  present.RectCount = count;
  /* The kernel reads and writes in parallel: overlapping source and
   * destination bytes (a scroll within one allocation) need the CPU path's
   * source snapshot. */
  {
    ULONG source = AdmissionG3PresentSurfaceBytes(&present.Source);
    ULONG destination = AdmissionG3PresentSurfaceBytes(&present.Destination);
    if (source == 0u || destination == 0u ||
        (present.Source.Va < present.Destination.Va + destination &&
         present.Destination.Va < present.Source.Va + source))
      ADMISSION_PRESENT_CPU(4u);
  }
  deferred = AdmissionG4PendingAdmissible(adapter, context);
  if (!deferred && !AdmissionPlatformRuntimeAwaitWork(adapter, 10000u, NULL))
    ADMISSION_PRESENT_CPU(5u);
  ExAcquireFastMutex(&state->Lock);
  /* The arena is made once and never moves until the process ends. */
  if (process->PresentArena.Generation) {
    present.Arena.Cpu = (unsigned char *)view.CpuAddress +
        process->PresentArena.Offset;
    present.Arena.Va = process->PrivateVa + process->PresentArena.VaOffset;
    present.Arena.Bytes = APPLE_AGX_PRESENT_ARENA_BYTES;
    present.Arena.UscBase = APPLE_AGX_G4_USC_EXECUTION_BASE;
  }
  if (!process->PresentArena.Generation ||
      context->GpuvaG3RootIpa != process->Graph.RootIpa ||
      process->Graph.Uncertain ||
      (!deferred && state->PrivateCompletionFence) ||
      !AppleAgxPresentValidate(&present.Arena, 0, &present.Source,
          &present.SourceRect, &present.Destination, &present.DestinationRect,
          present.Rects, present.RectCount) ||
      !AdmissionG3PresentAccessible(process, &present)) {
    ULONG why = process->PresentArena.Generation ? 7u : 6u;
    ExReleaseFastMutex(&state->Lock);
    ADMISSION_PRESENT_CPU(why);
  }
  mapping_generation = process->Graph.MappingGeneration;
  ExReleaseFastMutex(&state->Lock);
  RtlZeroMemory(&packet, sizeof(packet));
  packet.Kind = ADMISSION_RENDER_PACKET_KIND_COMPUTE;
  packet.Fence = args->SubmissionFenceId;
  packet.ContextToken = (ULONGLONG)(ULONG_PTR)context;
  packet.AllocationToken = (ULONGLONG)(ULONG_PTR)context;
  packet.PrivateDataToken =
      (ULONGLONG)(ULONG_PTR)&adapter->BackendImage.Present;
  packet.PrivateDataBytes = (ULONG)sizeof(present);
  packet.PrivateDataEnd = packet.PrivateDataBytes;
  packet.DmaEnd = (ULONG)sizeof(present);
  if (deferred) {
    if (AdmissionG4PendingQueue(adapter, state, context, NULL, args,
            &packet, NULL, NULL, &present, mapping_generation, NULL)) {
      InterlockedIncrement(&adapter->SubmitPath.Deferred);
      AdmissionDispatchQueuedWork(adapter);
      return STATUS_SUCCESS;
    }
    ADMISSION_PRESENT_CPU(8u);
  }
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketEmpty &&
      adapter->G4PendingCount == 0u &&
      context->Object.FenceOutstanding == 0u &&
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0) == 0 &&
      AdmissionRenderPacketPrepare(&adapter->RenderPacket, &packet)) {
    context->Object.FenceOutstanding = args->SubmissionFenceId;
    prepared = TRUE;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (!prepared) ADMISSION_PRESENT_CPU(8u);
  /* From here the fence is this job's: a failure is a rejected submit. */
  if (!AdmissionBackendImageBindPresentCompute(&adapter->BackendImage,
          args->SubmissionFenceId, &present)) {
    AdmissionG4RecordBindRefusal(adapter, 2u, args->SubmissionFenceId);
    goto Rollback;
  }
  context->GpuvaG3MappingGeneration = mapping_generation;
  context->GpuvaG3DmaBufferVa = args->DmaBufferVirtualAddress;
  context->GpuvaG3DmaBufferBytes = args->DmaBufferSize;
  ExAcquireFastMutex(&state->Lock);
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketQueue(&adapter->RenderPacket,
          args->SubmissionFenceId, (ULONGLONG)(ULONG_PTR)context,
          packet.PrivateDataToken, 0u, packet.DmaEnd)) {
    if (AppleAgxSchedulerQueueFence(&adapter->Scheduler, 0u, 0u,
            args->SubmissionFenceId))
      queued = TRUE;
    else
      (void)AdmissionRenderPacketReset(&adapter->RenderPacket,
          args->SubmissionFenceId, 0u);
  }
  if (!queued) AdmissionContextRetireFence(context, args->SubmissionFenceId);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  ExReleaseFastMutex(&state->Lock);
  if (queued) {
    InterlockedIncrement(&adapter->SubmitPath.Immediate);
    AdmissionDispatchQueuedWork(adapter);
    return STATUS_SUCCESS;
  }
Rollback:
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketPrepared)
    (void)AdmissionRenderPacketCancelPrepared(&adapter->RenderPacket,
        (ULONGLONG)(ULONG_PTR)context);
  AdmissionContextRetireFence(context, args->SubmissionFenceId);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  AdmissionPlatformRuntimeSlotChanged(adapter);
  context->GpuvaG3DmaBufferVa = 0ULL;
  context->GpuvaG3DmaBufferBytes = 0u;
  if (adapter->BackendImage.PresentCompute)
    (void)AdmissionBackendImageReleaseSubmission(&adapter->BackendImage,
        args->SubmissionFenceId);
  return AdmissionG4SubmitReject(adapter, context, args,
      AdmissionG4RejectQueue, STATUS_INVALID_PARAMETER, 0u, TRUE);
}

#undef ADMISSION_PRESENT_CPU

static VOID AdmissionG3NotePresentPath(ADMISSION_CONTEXT *adapter,
    ADMISSION_G3_STATE *state, ULONG reason, ULONG fence) {
  LARGE_INTEGER frequency;
  LONGLONG now, last;
  ULONG receipt[ADMISSION_G2_PRESENT_PATH_RECEIPT_WORDS], i;
  if (reason >= ADMISSION_G2_PRESENT_PATH_REASONS) return;
  InterlockedIncrement(&state->PresentPath[reason]);
  state->PresentPathLast = (LONG)reason;
  state->PresentPathFence = (LONG)fence;
  if (KeGetCurrentIrql() != PASSIVE_LEVEL) return;
  /* At most one registry write every 2 s (DWM presents every frame). */
  now = KeQueryPerformanceCounter(&frequency).QuadPart;
  last = state->PresentPathQpc;
  if (last && now - last < 2 * frequency.QuadPart) return;
  if (InterlockedCompareExchange64(&state->PresentPathQpc, now, last) != last)
    return;
  receipt[0] = 1u;
  for (i = 0u; i < ADMISSION_G2_PRESENT_PATH_REASONS; ++i)
    receipt[1u + i] = (ULONG)InterlockedCompareExchange(
        &state->PresentPath[i], 0, 0);
  receipt[1u + ADMISSION_G2_PRESENT_PATH_REASONS] = reason;
  receipt[2u + ADMISSION_G2_PRESENT_PATH_REASONS] = fence;
  AdmissionRecordG2PresentPath(adapter, receipt);
}

NTSTATUS AdmissionGpuvaG3SubmitPresentCompute(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, const DXGKARG_SUBMITCOMMANDVIRTUAL *args,
    const ADMISSION_PRESENT_BLT_COMMAND *command,
    const APPLE_AGX_GDI_RECT *rects, ULONG count) {
  ULONG reason = 0u;
  NTSTATUS status = AdmissionG3SubmitPresentComputeInner(adapter, context,
      args, command, rects, count, &reason);
  ADMISSION_G3_STATE *state = adapter == NULL ? NULL :
      (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (status == STATUS_SUCCESS) reason = 0u;
  else if (status != STATUS_NOT_SUPPORTED) reason = 9u;
  if (state != NULL)
    AdmissionG3NotePresentPath(adapter, state, reason,
        args == NULL ? 0u : args->SubmissionFenceId);
  return status;
}

static NTSTATUS AdmissionG4SubmitVirtualEnvelope(
    ADMISSION_CONTEXT *adapter, ADMISSION_RENDER_CONTEXT *context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *args) {
  ADMISSION_G3_STATE *state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  ADMISSION_G3_PROCESS *process =
      (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  ULONGLONG mapping_generation;
  ADMISSION_G3_PRIVATE_SCENE *private_scene=NULL;
  APPLE_AGX_G4_PRIVATE_HEADER private_header;
  APPLE_AGX_G4_PRIVATE_HEADER_V3 private_v3;
  APPLE_AGX_G4_SUBMIT_VIEW view;
  APPLE_AGX_G4_PARSE_RESULT result;
  APPLE_AGX_G4_FAILURE failure = {0};
  struct _ADMISSION_G4_SUBMIT_FAILURE detail = {0};
  APPLE_AGX_G4_ATTACHMENT color;
  ADMISSION_SCANOUT_MEMORY_VIEW local;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  KIRQL old_irql;
  NTSTATUS viewStatus = STATUS_SUCCESS;
  BOOLEAN prepared = FALSE, queued = FALSE, deferred;
  ULONG rollbackBranch = AdmissionG4RejectBind;
  DXGK_SUBMITCOMMANDFLAGS unsupportedFlags=args->Flags;
  unsupportedFlags.Resubmission=0;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  ULONG envelopePredicate = 0u, runtimePredicate = 0u;
/* Record the first failing expression as it is evaluated, not a later reread. */
#define ADMISSION_G4_REJECTS(id, expression) \
  ((expression) ? (envelopePredicate = (id), TRUE) : FALSE)
/* EXP1014/EXP1021: wait at most 10 s for the previous job (EXP1020: backend
 * jobs outlived 1 s); stopping and resetting (TDR) still refuse at once. */
#define ADMISSION_G4_RUNTIME_READY() \
  AdmissionPlatformRuntimeAwaitWork(adapter, 10000u, &runtimePredicate)
#else
#define ADMISSION_G4_REJECTS(id, expression) (expression)
#define ADMISSION_G4_RUNTIME_READY() \
  AdmissionPlatformRuntimeAwaitWork(adapter, 10000u, NULL)
#endif
  /* Phase 5a: while the render slot holds another context's job, queue
   * instead of waiting on dxgkrnl's VidSch worker thread. */
  deferred = AdmissionG4PendingAdmissible(adapter, context);
  if (ADMISSION_G4_REJECTS(1u, KeGetCurrentIrql() != PASSIVE_LEVEL) ||
      ADMISSION_G4_REJECTS(2u, state == NULL) ||
      ADMISSION_G4_REJECTS(3u, process == NULL) ||
      ADMISSION_G4_REJECTS(4u, process->State != state) ||
      ADMISSION_G4_REJECTS(5u, process->Poisoned) ||
      ADMISSION_G4_REJECTS(6u, context->GpuvaG3Poisoned) ||
      ADMISSION_G4_REJECTS(7u, context->GpuvaG3RootIpa == 0ULL) ||
      ADMISSION_G4_REJECTS(8u, !context->Win32Transport) ||
      ADMISSION_G4_REJECTS(9u,
          (context->Object.Flags & ADMISSION_CONTEXT_VIRTUAL_ADDRESSING) == 0u) ||
      ADMISSION_G4_REJECTS(10u,
          (context->Object.Flags & (ADMISSION_CONTEXT_SYSTEM |
                                    ADMISSION_CONTEXT_GDI)) != 0u) ||
      ADMISSION_G4_REJECTS(11u, unsupportedFlags.Value != 0u) ||
      ADMISSION_G4_REJECTS(12u, !context->SchedulerContext.Active) ||
      ADMISSION_G4_REJECTS(13u, !deferred && !ADMISSION_G4_RUNTIME_READY()) ||
      ADMISSION_G4_REJECTS(14u, !NT_SUCCESS(viewStatus =
          AdmissionMemoryRuntimeLocalView(adapter, &local)))) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
    AdmissionG4ObserveEnvelopeReject(adapter,context,args,1u,
        envelopePredicate,runtimePredicate,NULL,NULL);
    /* EXP1013: name the envelope-state predicate in the first-failure receipt. */
    detail.Subsite = 0x100u | envelopePredicate;
    detail.Kind = runtimePredicate;
#endif
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectEnvelopeState, STATUS_INVALID_PARAMETER,
        (ULONG)viewStatus, TRUE, &detail);
  }
#undef ADMISSION_G4_RUNTIME_READY
#undef ADMISSION_G4_REJECTS
  RtlZeroMemory(&packet, sizeof(packet));
  RtlZeroMemory(&binding, sizeof(binding));
  ExAcquireFastMutex(&state->Lock);
  /* A deferred job binds and begins only after the worker has reported the
   * previous private completion, so only the immediate path checks it here. */
  if (context->GpuvaG3RootIpa != process->Graph.RootIpa ||
      process->Graph.Uncertain || (!deferred && state->PrivateCompletionFence)) {
    ExReleaseFastMutex(&state->Lock);
    return AdmissionG4SubmitReject(adapter, context, args,
        AdmissionG4RejectRoot, STATUS_INVALID_PARAMETER, 0u, TRUE);
  }
  /* G2: the compute envelope (private version 4) is a standalone job. */
  if (args->pDmaBufferPrivateData != NULL &&
      args->DmaBufferPrivateDataSize >= args->DmaBufferUmdPrivateDataSize &&
      AppleAgxG4IsCompute(args->pDmaBufferPrivateData,
                          args->DmaBufferUmdPrivateDataSize))
    return AdmissionG4SubmitComputeLocked(adapter, state, process, context,
                                          args, deferred);
  if (args->pDmaBufferPrivateData && args->DmaBufferUmdPrivateDataSize>=sizeof(private_header) &&
      args->DmaBufferPrivateDataSize>=args->DmaBufferUmdPrivateDataSize) {
    RtlCopyMemory(&private_header,args->pDmaBufferPrivateData,sizeof(private_header));
    /* D3D-G1: a v5 render (compute prelude) starts with the v3 header. */
    if ((private_header.Version==APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA ||
         private_header.Version==APPLE_AGX_G4_PRIVATE_VERSION_RENDER_COMPUTE) &&
        args->DmaBufferUmdPrivateDataSize>=sizeof(private_v3)) {
      RtlCopyMemory(&private_v3,args->pDmaBufferPrivateData,sizeof(private_v3));
      private_scene=args->Flags.Resubmission ?
          AdmissionG4FindPrivateResubmission(process,context,&private_v3.Lease,
                                             args->SubmissionFenceId) :
          AdmissionG4FindPrivateScene(process,context,&private_v3.Lease,
                                      args->SubmissionFenceId,FALSE);
      if (!private_scene) {
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
        AdmissionG4ObserveEnvelopeReject(adapter,context,args,2u,0u,0u,process,&private_v3.Lease);
#endif
        /* EXP1013: private scene lookup failed; record the lease. */
        detail.Subsite = 0x200u | (args->Flags.Resubmission ? 1u : 0u);
        detail.Kind = (ULONG)private_v3.Lease.SceneId;
        detail.Ordinal = (ULONG)private_v3.Lease.SceneGeneration;
        detail.ProcessGeneration = process->PrivateManager.Generation;
        detail.MappingGeneration = private_v3.Lease.ManagerGeneration;
        detail.Va = args->SubmissionFenceId;
        if (args->Flags.Resubmission) {
          /* EXP1210 receipt-only: AccessBytes = refused predicates (bit n =
           * nth check of AdmissionG4FindPrivateResubmission); LogicalIpa =
           * scene fence|resume<<32, private slots, preempt marks, cancel
           * marks (slot 1 | slot 2 << 32); LogicalSegment[0] = scene
           * Queued|Submitting<<1|Started<<2|Quarantined<<3|ReleaseRequested<<4,
           * [1] = calls in DxgkDdiSubmitCommandVirtual; LogicalFlags = the
           * context's outstanding and second fence, pending count, render
           * packet state. */
          ADMISSION_G3_PRIVATE_SCENE *seen;
          detail.AccessBytes = AdmissionG4ResubmissionRefusal(process,context,
              &private_v3.Lease,args->SubmissionFenceId,&seen);
          if (seen) {
            detail.LogicalIpa[0] = (ULONGLONG)seen->Fence | ((ULONGLONG)seen->ResumeFence << 32);
            detail.LogicalSegment[0] = (seen->Queued ? 1u : 0u) | (seen->Submitting ? 2u : 0u) |
                (seen->Started ? 4u : 0u) | (seen->Quarantined ? 8u : 0u) |
                (seen->ReleaseRequested ? 16u : 0u);
          }
          detail.LogicalIpa[1] = (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0) |
              ((ULONGLONG)(ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0) << 32);
          detail.LogicalIpa[2] = (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence,0,0) |
              ((ULONGLONG)(ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence2,0,0) << 32);
          detail.LogicalIpa[3] = (ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0) |
              ((ULONGLONG)(ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelFence2,0,0) << 32);
          detail.LogicalSegment[1] = (ULONG)InterlockedCompareExchange(&adapter->G4SubmitVirtualInFlight,0,0);
          /* Which cancel path marked each slot (caller - PrivateCancel). */
          detail.LogicalSegment[2] = (ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelCaller,0,0);
          detail.LogicalSegment[3] = (ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelCaller2,0,0);
          detail.LogicalFlags[0] = context->Object.FenceOutstanding;
          detail.LogicalFlags[1] = context->GpuvaG3SecondFence;
          detail.LogicalFlags[2] = adapter->G4PendingCount;
          detail.LogicalFlags[3] = AdmissionRenderPacketState(&adapter->RenderPacket);
        }
        ExReleaseFastMutex(&state->Lock);
        return AdmissionG4SubmitRejectDetail(adapter,context,args,
            AdmissionG4RejectEnvelopeState,STATUS_INVALID_PARAMETER,0u,TRUE,&detail);
      }
    }
  }
  result = AppleAgxG4ParseSubmitComputeEx(
      args->pDmaBufferPrivateData, args->DmaBufferPrivateDataSize,
      args->DmaBufferUmdPrivateDataSize, args->DmaBufferVirtualAddress,
      args->DmaBufferSize, APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES,
      private_scene ? AdmissionG4PrivateGraphAccess : AdmissionG4GraphAccessTyped,
      private_scene ? (void *)private_scene : (void *)process, &view, &failure);
  if (result==AppleAgxG4ParseOk &&
      ((view.Lease.SceneId && !private_scene) ||
       (private_scene && (!AdmissionG4PrivateGeometry(private_scene,&view) ||
        RtlCompareMemory(&view.Lease,&private_v3.Lease,sizeof(view.Lease))!=sizeof(view.Lease)))))
    result=AppleAgxG4ParseInvalid;
  if (result == AppleAgxG4ParseOk && view.AttachmentCount != 0u &&
      view.AttachmentCount <= APPLE_AGX_G4_MAX_ATTACHMENTS) {
    RtlCopyMemory(&color, view.Attachments, sizeof(color));
    packet.Fence = args->SubmissionFenceId;
    packet.AllocationCount = 1u;
    packet.ContextToken = (ULONGLONG)(ULONG_PTR)context;
    /* G3 virtual submit has no allocation-list member. The graph and local
     * reserve prove the target; this token is opaque to the G3 completion
     * path, which does not enable the GDI output-capture qualification. */
    packet.AllocationToken = (ULONGLONG)(ULONG_PTR)context;
    packet.PrivateDataToken =
        (ULONGLONG)(ULONG_PTR)&adapter->BackendImage.G4Header;
    packet.PrivateDataBytes =
        (ULONG)sizeof(adapter->BackendImage.G4Header) + view.CommandBytes;
    packet.PrivateDataStart = 0u;
    packet.PrivateDataEnd = packet.PrivateDataBytes;
    packet.DmaStart = 0u;
    packet.DmaEnd = view.CommandBytes;
    packet.DestinationIndex = 0u;
    if (!AdmissionG4ResolveOutput(process, &color, &local, &packet)) {
      result = AppleAgxG4ParseUnmapped;
      failure.Subsite = AppleAgxG4FailureOutput;
      failure.Kind = AppleAgxG4AccessAttachment;
      failure.Ordinal = 0u;
      failure.Va = color.Pointer;
      failure.Bytes = (ULONG)color.Size;
      failure.Write = 1u;
    }
  } else if (result == AppleAgxG4ParseOk) {
    result = AppleAgxG4ParseUnsupported;
  }
  mapping_generation = process->Graph.MappingGeneration;
  if (result != AppleAgxG4ParseOk)
    AdmissionG4SnapshotFailure(process, &failure, &detail);
  if (result==AppleAgxG4ParseOk && private_scene) {
    /* Dynamic TVB: the job is built for the blocks backed now and BeginJob
     * keys the manager by the same count; a resubmission keeps its count. */
    if (!args->Flags.Resubmission || !private_scene->HeapBlocks)
      private_scene->HeapBlocks=process->PrivateManager.Blocks;
    view.HeapBlocks=private_scene->HeapBlocks;
    private_scene->ResumeFence=args->Flags.Resubmission ? private_scene->Fence : 0u;
    private_scene->Submitting=1u;private_scene->Queued=1u;
    private_scene->Fence=args->SubmissionFenceId;
    {
      /* Phase 5b: a resubmission reuses the slot of the preempted fence; a
       * new job takes a free slot (FindPrivateScene proved one exists). */
      ULONG previous=private_scene->ResumeFence;
      BOOLEAN second=previous ?
          (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0)==previous :
          InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)!=0;
      if (!second && !InterlockedCompareExchange(&context->GpuvaG3PrivateFence2,0,0))
        InterlockedExchange(&context->GpuvaG3CancelUncertain,0);
      InterlockedExchange(second ? &context->GpuvaG3PreemptFence2 :
          &context->GpuvaG3PreemptFence,0);
      InterlockedExchange(second ? &context->GpuvaG3CancelFence2 :
          &context->GpuvaG3CancelFence,0);
      InterlockedExchange(second ? &context->GpuvaG3PrivateFence2 :
          &context->GpuvaG3PrivateFence,(LONG)args->SubmissionFenceId);
    }
  }
  ExReleaseFastMutex(&state->Lock);
  if (result != AppleAgxG4ParseOk)
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectParse, STATUS_INVALID_PARAMETER, (ULONG)result,
        TRUE, &detail);
  if (deferred) {
    if (AdmissionG4PendingQueue(adapter, state, context, private_scene, args,
            &packet, &view, NULL, NULL, mapping_generation, &detail)) {
      InterlockedIncrement(&adapter->SubmitPath.Deferred);
      AdmissionDispatchQueuedWork(adapter);
      return STATUS_SUCCESS;
    }
    AdmissionG4PrivateUnqueue(process,private_scene,args->SubmissionFenceId);
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectPrepare, STATUS_INVALID_PARAMETER, 0u, TRUE, &detail);
  }
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketEmpty &&
      adapter->G4PendingCount == 0u &&
      context->Object.FenceOutstanding == 0u &&
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0) == 0 &&
      AdmissionRenderPacketPrepare(&adapter->RenderPacket, &packet)) {
    context->Object.FenceOutstanding = args->SubmissionFenceId;
    prepared = TRUE;
  }
  if (!prepared)
    AdmissionG4CapturePrepareLocked(adapter, context, args,
        AdmissionG4PreparePacket, AdmissionG4PrepareImmediate, &detail);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (!prepared) {
    AdmissionG4PrivateUnqueue(process,private_scene,args->SubmissionFenceId);
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectPrepare, STATUS_INVALID_PARAMETER, 0u, TRUE, &detail);
  }
  if (!AdmissionBackendImageBindG4Submission(&adapter->BackendImage,
          &packet, (PVOID)(ULONG_PTR)packet.DestinationCpuToken,
          &view, &binding)) {
    AdmissionG4RecordBindRefusal(adapter, 3u, args->SubmissionFenceId);
    goto Rollback;
  }
  context->GpuvaG3MappingGeneration = mapping_generation;
  context->GpuvaG3DmaBufferVa = args->DmaBufferVirtualAddress;
  context->GpuvaG3DmaBufferBytes = args->DmaBufferSize;
  rollbackBranch = AdmissionG4RejectQueue;
  ExAcquireFastMutex(&state->Lock);
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketQueue(&adapter->RenderPacket,
          args->SubmissionFenceId, (ULONGLONG)(ULONG_PTR)context,
          packet.PrivateDataToken, 0u, packet.DmaEnd)) {
    if (AppleAgxSchedulerQueueFence(&adapter->Scheduler, 0u, 0u,
            args->SubmissionFenceId))
      queued = TRUE;
    else
      (void)AdmissionRenderPacketReset(&adapter->RenderPacket,
          args->SubmissionFenceId, 0u);
  }
  if (!queued) AdmissionContextRetireFence(context, args->SubmissionFenceId);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (queued && private_scene) {
    private_scene->ResumeFence=0u;private_scene->Submitting=0u;
  }
  ExReleaseFastMutex(&state->Lock);
  if (!queued) goto Rollback;
  InterlockedIncrement(&adapter->SubmitPath.Immediate);
  AdmissionDispatchQueuedWork(adapter);
  return STATUS_SUCCESS;
Rollback:
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketPrepared)
    (void)AdmissionRenderPacketCancelPrepared(&adapter->RenderPacket,
        (ULONGLONG)(ULONG_PTR)context);
  AdmissionContextRetireFence(context, args->SubmissionFenceId);
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  AdmissionPlatformRuntimeSlotChanged(adapter);
  context->GpuvaG3DmaBufferVa = 0ULL;
  context->GpuvaG3DmaBufferBytes = 0u;
  if (adapter->BackendImage.G4Native)
    (void)AdmissionBackendImageReleaseSubmission(&adapter->BackendImage,
        args->SubmissionFenceId);
  AdmissionG4PrivateUnqueue(process,private_scene,args->SubmissionFenceId);
  return AdmissionG4SubmitReject(adapter, context, args,
      rollbackBranch, STATUS_INVALID_PARAMETER, 0u, TRUE);
}

/* Caller holds the process graph mutex. Validate immutable submission
 * inputs without reserving a scheduler/private-fence slot or binding an image.
 * Context root installation and slot availability belong to scheduled submit. */
static NTSTATUS AdmissionG4PreflightLocked(ADMISSION_CONTEXT *adapter,
    ADMISSION_G3_PROCESS *process, ADMISSION_RENDER_CONTEXT *context,
    APPLE_AGX_G4_PREFLIGHT *q) {
  APPLE_AGX_G4_PRIVATE_HEADER header;
  APPLE_AGX_G4_PRIVATE_HEADER_V3 v3;
  APPLE_AGX_G4_SUBMIT_VIEW view;
  APPLE_AGX_G4_COMPUTE_VIEW compute;
  ADMISSION_G3_PRIVATE_SCENE *scene = NULL;
  ADMISSION_SCANOUT_MEMORY_VIEW local;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_G4_ATTACHMENT color;
  NTSTATUS status;
  if (q->PrivateBytes < sizeof(header)) return STATUS_INVALID_PARAMETER;
  RtlCopyMemory(&header, q->Private, sizeof(header));
  if (header.Version == APPLE_AGX_G4_PRIVATE_VERSION_COMPUTE) {
    q->ParseResult = AppleAgxG4ParseComputeEx(q->Private, q->PrivateBytes,
        q->PrivateBytes, q->CommandVa, q->CommandBytes,
        APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES, AdmissionG4GraphAccessTyped,
        process, &compute, &q->Failure);
    return q->ParseResult == AppleAgxG4ParseOk ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
  }
  if (header.Version == APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA ||
      header.Version == APPLE_AGX_G4_PRIVATE_VERSION_RENDER_COMPUTE) {
    if (q->PrivateBytes < sizeof(v3)) return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&v3, q->Private, sizeof(v3));
    if (!v3.Lease.ManagerId || v3.Lease.ManagerId != process->Graph.ProcessId ||
        v3.Lease.ManagerGeneration != process->PrivateManager.Generation ||
        v3.Lease.ManagerGeneration != context->GpuvaG3PrivateManagerGeneration)
      return STATUS_INVALID_PARAMETER;
    for (scene = process->PrivateScenes; scene; scene = scene->Next)
      if (scene->Storage.Generation == v3.Lease.SceneId) break;
    if (!scene || scene->Context != context ||
        scene->Storage.Generation != v3.Lease.SceneGeneration ||
        scene->ReleaseRequested || scene->Quarantined || scene->Queued ||
        scene->Submitting || scene->Started) return STATUS_INVALID_PARAMETER;
  }
  q->ParseResult = AppleAgxG4ParseSubmitComputeEx(q->Private, q->PrivateBytes,
      q->PrivateBytes, q->CommandVa, q->CommandBytes,
      APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES,
      scene ? AdmissionG4PrivateGraphAccess : AdmissionG4GraphAccessTyped,
      scene ? (void *)scene : (void *)process, &view, &q->Failure);
  if (q->ParseResult != AppleAgxG4ParseOk) return STATUS_INVALID_PARAMETER;
  if ((scene && !AdmissionG4PrivateGeometry(scene, &view)) ||
      !view.AttachmentCount || view.AttachmentCount > APPLE_AGX_G4_MAX_ATTACHMENTS)
    return STATUS_INVALID_PARAMETER;
  status = AdmissionMemoryRuntimeLocalView(adapter, &local);
  if (!NT_SUCCESS(status)) return status;
  RtlZeroMemory(&packet, sizeof(packet));
  RtlCopyMemory(&color, view.Attachments, sizeof(color));
  if (!AdmissionG4ResolveOutput(process, &color, &local, &packet)) {
    q->Failure.Subsite = AppleAgxG4FailureOutput;
    q->Failure.Kind = AppleAgxG4AccessAttachment;
    q->Failure.Va = color.Pointer; q->Failure.Bytes = (ULONG)color.Size;
    q->Failure.Write = 1u;
    return STATUS_INVALID_PARAMETER;
  }
  return STATUS_SUCCESS;
}

NTSTATUS AdmissionGpuvaG4PreflightEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  APPLE_AGX_G4_PREFLIGHT q;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_DEVICE *device;
  if (!adapter || !adapter->Started || !args ||
      KeGetCurrentIrql() != PASSIVE_LEVEL || args->Flags.Value ||
      !args->pPrivateDriverData || args->PrivateDriverDataSize != sizeof(q))
    return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (!state) return STATUS_INVALID_DEVICE_STATE;
  RtlCopyMemory(&q, args->pPrivateDriverData, sizeof(q));
  if (q.Magic != APPLE_AGX_G4_PREFLIGHT_MAGIC ||
      q.Version != APPLE_AGX_G4_PREFLIGHT_VERSION || q.Bytes != sizeof(q) ||
      !q.PrivateBytes || q.PrivateBytes > sizeof(q.Private) ||
      q.Ready || q.Status || q.ParseResult || q.Failure.Subsite ||
      q.Failure.Kind || q.Failure.Ordinal || q.Failure.Va ||
      q.Failure.Bytes || q.Failure.Write) return STATUS_INVALID_PARAMETER;
  q.Status = (ULONG)STATUS_INVALID_DEVICE_STATE;
  ExAcquireFastMutex(&state->Lock);
  process = AdmissionGpuvaG3FindProcess(state, args->hKmdProcessHandle);
  if (!process || process->Poisoned || process->CpuOnlyMappings ||
      !process->Graph.Created || process->Graph.Uncertain ||
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0)) goto Output;
  for (context = process->Contexts; context && (HANDLE)context != args->hContext;
       context = context->GpuvaG3NextContext) {}
  if (!context || !context->Win32Transport || context->GpuvaG3Closing ||
      context->GpuvaG3Poisoned || !context->Object.Device ||
      !(context->Object.Flags & ADMISSION_CONTEXT_VIRTUAL_ADDRESSING) ||
      (context->Object.Flags & (ADMISSION_CONTEXT_SYSTEM | ADMISSION_CONTEXT_GDI))) goto Output;
  device = CONTAINING_RECORD(context->Object.Device, ADMISSION_DEVICE, Object);
  if ((HANDLE)device != args->hDevice ||
      device->Object.Adapter != &adapter->ObjectAdapter) goto Output;
  /* R147: SetRoot is only guaranteed before scheduled execution. */
  q.Status = (ULONG)AdmissionG4PreflightLocked(adapter, process, context, &q);
  q.Ready = q.Status == (ULONG)STATUS_SUCCESS;
Output:
  ExReleaseFastMutex(&state->Lock);
  RtlCopyMemory(args->pPrivateDriverData, &q, sizeof(q));
  return STATUS_SUCCESS;
}

/* EXP1003: residency touch, completed by the CPU queue in fence order. */
static NTSTATUS AdmissionG4SubmitTouch(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, const DXGKARG_SUBMITCOMMANDVIRTUAL *args) {
  DXGKARG_SUBMITCOMMAND physical;
  DXGK_SUBMITCOMMANDFLAGS unsupported = args->Flags;
  unsupported.Resubmission = 0;
  if (context->GpuvaG3Process == NULL || context->GpuvaG3Poisoned ||
      (context->Object.Flags & (ADMISSION_CONTEXT_SYSTEM | ADMISSION_CONTEXT_GDI)) != 0u ||
      unsupported.Value != 0u) {
    struct _ADMISSION_G4_SUBMIT_FAILURE detail = {0};
    detail.Subsite = 0x300u; /* EXP1013: touch rejected */
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectEnvelopeState, STATUS_INVALID_PARAMETER, 0u, TRUE, &detail);
  }
  RtlZeroMemory(&physical, sizeof(physical));
  physical.hContext = args->hContext;
  physical.DmaBufferVirtualAddress = args->DmaBufferVirtualAddress;
  physical.DmaBufferSize = args->DmaBufferSize;
  physical.SubmissionFenceId = args->SubmissionFenceId;
  physical.Flags = args->Flags;
  return AdmissionCpuQueueSubmit(adapter, &physical, ADMISSION_CPU_PACKET_NOP, NULL, 0u);
}

static NTSTATUS AdmissionDdiSubmitCommandVirtualInner(
    HANDLE Adapter, const DXGKARG_SUBMITCOMMANDVIRTUAL *Args) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)Adapter;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_DMA_SHADOW shadow;
  DXGKARG_SUBMITCOMMAND physical;
  NTSTATUS status;
  if (adapter == NULL || Args == NULL || !adapter->Started ||
      Args->hContext == NULL || Args->DmaBufferVirtualAddress == 0ULL ||
      Args->DmaBufferSize == 0u ||
      Args->pDmaBufferPrivateData == NULL ||
      Args->NodeOrdinal != (Args->Flags.Paging ? adapter->LiveAdditivePaging : 0u) ||
      Args->EngineOrdinal != 0u || Args->SubmissionFenceId == 0u ||
      KeGetCurrentIrql() > DISPATCH_LEVEL)
    return AdmissionG4SubmitReject(adapter, NULL, Args,
        AdmissionG4RejectOuter, STATUS_INVALID_PARAMETER, 0u, FALSE);
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter ||
      context->Object.NodeOrdinal != Args->NodeOrdinal)
    return AdmissionG4SubmitReject(adapter, NULL, Args,
        AdmissionG4RejectContext, STATUS_INVALID_PARAMETER, 0u, FALSE);
  if ((context->Object.Flags & ADMISSION_CONTEXT_SYSTEM) != 0u)
    return AdmissionGpuvaG3SubmitVirtualPaging(adapter, context, Args);
  if (Args->Flags.Present)
    return AdmissionPresentSubmitVirtual(adapter, context, Args);
  if (context->GpuvaG3Process != NULL)
    AdmissionJobTimingStartWindows(adapter, context,
        ((ADMISSION_G3_PROCESS *)context->GpuvaG3Process)->OsProcessId,
        Args->SubmissionFenceId, Args->DmaBufferSize);
#if defined(APPLE_AGX_BLT_PROBE_QUALIFICATION)
  InterlockedIncrement((volatile LONG *)&adapter->BltProbe.VirtualSubmitCalls);
#endif
  /* dxgkrnl places the UMD private data at the start of the DMA private data. */
  if (Args->DmaBufferPrivateDataSize >= Args->DmaBufferUmdPrivateDataSize &&
      AppleAgxG4IsTouch(Args->pDmaBufferPrivateData,
                        Args->DmaBufferUmdPrivateDataSize))
    return AdmissionG4SubmitTouch(adapter, context, Args);
  if (Args->DmaBufferUmdPrivateDataSize != 0u)
    return AdmissionG4SubmitVirtualEnvelope(adapter, context, Args);
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter ||
      Args->DmaBufferPrivateDataSize != ADMISSION_GDI_DMA_PRIVATE_SIZE ||
      Args->Flags.Value != 0u ||
      context->GpuvaG3Process == NULL || context->GpuvaG3Poisoned ||
      context->GpuvaG3RootIpa == 0ULL ||
      Args->DmaBufferVirtualAddress >= (1ULL << 39) ||
      Args->DmaBufferSize > (1ULL << 39) -
          Args->DmaBufferVirtualAddress ||
      !AppleAgxDmaShadowOpen(&shadow, Args->pDmaBufferPrivateData,
                             Args->DmaBufferPrivateDataSize))
    return AdmissionG4SubmitReject(adapter, context, Args,
        AdmissionG4RejectLegacyShape, STATUS_INVALID_PARAMETER, 0u, TRUE);
  packet = adapter->RenderPacket.Description;
  if (packet.ContextToken != (ULONGLONG)(ULONG_PTR)context ||
      packet.Fence != Args->SubmissionFenceId ||
      packet.DmaStart >= packet.DmaEnd ||
      packet.DmaEnd > Args->DmaBufferSize ||
      packet.PrivateDataToken !=
          (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData ||
      packet.PrivateDataEnd != shadow.BytesUsed)
    return AdmissionG4SubmitReject(adapter, context, Args,
        AdmissionG4RejectLegacyPacket, STATUS_INVALID_PARAMETER, 0u, TRUE);
  RtlZeroMemory(&physical, sizeof(physical));
  physical.hContext = Args->hContext;
  physical.DmaBufferVirtualAddress = Args->DmaBufferVirtualAddress;
  physical.DmaBufferSize = Args->DmaBufferSize;
  physical.DmaBufferSubmissionStartOffset = packet.DmaStart;
  physical.DmaBufferSubmissionEndOffset = packet.DmaEnd;
  physical.pDmaBufferPrivateData = Args->pDmaBufferPrivateData;
  physical.DmaBufferPrivateDataSize = Args->DmaBufferPrivateDataSize;
  physical.DmaBufferPrivateDataSubmissionStartOffset =
      packet.PrivateDataStart;
  physical.DmaBufferPrivateDataSubmissionEndOffset =
      packet.PrivateDataEnd;
  physical.SubmissionFenceId = Args->SubmissionFenceId;
  physical.VidPnSourceId = Args->VidPnSourceId;
  physical.FlipInterval = Args->FlipInterval;
  physical.Flags = Args->Flags;
  physical.EngineOrdinal = Args->EngineOrdinal;
  physical.NodeOrdinal = Args->NodeOrdinal;
  context->GpuvaG3DmaBufferVa = Args->DmaBufferVirtualAddress;
  context->GpuvaG3DmaBufferBytes = Args->DmaBufferSize;
  status = AdmissionDdiSubmitRender(adapter, &physical);
  if (!NT_SUCCESS(status)) {
    context->GpuvaG3DmaBufferVa = 0ULL;
    context->GpuvaG3DmaBufferBytes = 0u;
    return AdmissionG4SubmitReject(adapter, context, Args,
        AdmissionG4RejectLegacySubmit, STATUS_INVALID_PARAMETER,
        (ULONG)status, TRUE);
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiSubmitCommandVirtual(
    HANDLE Adapter, const DXGKARG_SUBMITCOMMANDVIRTUAL *Args) {
  ADMISSION_CONTEXT *overlap = (ADMISSION_CONTEXT *)Adapter;
  NTSTATUS status;
  /* EXP1210 receipt-only: the call is inside the driver (preempt overlap). */
  if (overlap != NULL) {
    InterlockedIncrement(&overlap->G4SubmitVirtualInFlight);
    if (Args != NULL) {
      InterlockedExchange(&overlap->G4SubmitVirtualFence, (LONG)Args->SubmissionFenceId);
      InterlockedExchange(&overlap->G4SubmitVirtualFlags, (LONG)Args->Flags.Value);
      InterlockedIncrement(Args->Flags.Resubmission ?
          &overlap->G4PreemptOverlap.Resubmissions : &overlap->G4PreemptOverlap.Submissions);
    }
  }
  status = AdmissionDdiSubmitCommandVirtualInner(Adapter, Args);
  /* WDDM permits exactly SUCCESS or INVALID_PARAMETER here. In particular,
   * CPU touch/paging queue backpressure must not leak DEVICE_BUSY (0x119/2).
   * Retain the original failure, and attach the rejected fence to its actual
   * predecessor: it becomes completed only after that work retires. This
   * never releases a GPU job, fabricates an interrupt or clears a fault. */
  if (status != STATUS_SUCCESS) {
    if (overlap != NULL && Args != NULL &&
        Args->NodeOrdinal <= overlap->LiveAdditivePaging && Args->EngineOrdinal == 0u &&
        InterlockedCompareExchange(&overlap->SchedulerInitialized, 0, 0) != 0) {
      KIRQL oldIrql;
      KeAcquireSpinLock(&overlap->SchedulerLock, &oldIrql);
      (void)AppleAgxSchedulerRejectFence(Args->NodeOrdinal ?
          &overlap->PagingScheduler : &overlap->Scheduler, 0u, 0u,
                                        Args->SubmissionFenceId);
      KeReleaseSpinLock(&overlap->SchedulerLock, oldIrql);
    }
    if (status != STATUS_INVALID_PARAMETER)
      (void)AdmissionG4SubmitReject(overlap, NULL, Args,
          AdmissionG4RejectStatus, STATUS_INVALID_PARAMETER, (ULONG)status,
          FALSE);
    status = STATUS_INVALID_PARAMETER;
  }
  if (overlap != NULL) InterlockedDecrement(&overlap->G4SubmitVirtualInFlight);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (Args != NULL)
    AdmissionDwmFrameRecordSubmit((ADMISSION_CONTEXT *)Adapter,
        Args->hContext, Args->DmaBufferVirtualAddress,
        Args->SubmissionFenceId, 0u, status,
        Args->Flags.Present ? TRUE : FALSE);
#endif
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION) || defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  ADMISSION_DWM_DDI_EVENT event;
  RtlZeroMemory(&event, sizeof(event));
  event.Kind = Args != NULL && Args->Flags.Present
      ? AdmissionDwmDdiSubmitPresent : AdmissionDwmDdiSubmitOther;
  event.Status = (ULONG)status;
  if (Args != NULL) {
    event.Flags = Args->Flags.Value;
    event.SourceId = Args->VidPnSourceId;
    event.Address = Args->DmaBufferVirtualAddress;
    event.Context = (ULONGLONG)(ULONG_PTR)Args->hContext;
    event.Fence = Args->SubmissionFenceId;
  }
  AdmissionDwmDdiProbeRecordWindows((ADMISSION_CONTEXT *)Adapter, &event);
#endif
  return status;
}

#endif
