#include "gpuva_g3_private.h"
#include "apple_agx_g3_copy_abi.h"
#include "apple_agx_g4_submit.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)

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
  AdmissionG4RejectLegacySubmit = 15
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
      first->Version = 2u;
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

NTSTATUS AdmissionGpuvaG3Start(ADMISSION_CONTEXT *context) {
  ADMISSION_G3_STATE *state;
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
  context->GpuvaG3State = state;
  return STATUS_SUCCESS;
}

NTSTATUS AdmissionGpuvaG3Stop(ADMISSION_CONTEXT *context) {
  ADMISSION_G3_STATE *state;
  if (context == NULL) return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)context->GpuvaG3State;
  if (state == NULL) return STATUS_SUCCESS;
  ExAcquireFastMutex(&state->Lock);
  if (state->ProcessCount != 0u || !IsListEmpty(&state->Processes) ||
      state->ActiveProcess != NULL) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_DEVICE_BUSY;
  }
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
      process->Graph.JobInFlight || process->Graph.LeaseToken)
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
  else if (!AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph,p->PrivateVa,
          p->PrivateMiddleIpa,p->PrivateLeafIpa))
    *predicate=5u;
  else return STATUS_SUCCESS;
  AppleAgxG3PrivatePoolStats(&p->State->PrivatePool,p->Graph.ProcessId,stats);
  return p->Graph.Uncertain ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_INSUFFICIENT_RESOURCES;
}

static BOOLEAN AdmissionG3PrivateReleaseScene(ADMISSION_G3_PROCESS *p,
    ADMISSION_G3_PRIVATE_SCENE *scene, ADMISSION_BACKEND_MEMORY_VIEW *view) {
  ADMISSION_G3_PRIVATE_SCENE **link=&p->PrivateScenes;
  UINT i;
  if (scene->Queued || scene->Submitting || scene->Quarantined || p->Graph.Uncertain ||
      p->Graph.JobInFlight || p->Graph.LeaseToken) return FALSE;
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
  return TRUE;
}

/* Interrupt/DPC owners hold SchedulerLock and publish only a cancellation
 * marker. The matching scene reference pins Context until PASSIVE reaping. */
VOID AdmissionGpuvaG3PrivateCancel(ADMISSION_RENDER_CONTEXT *context,
    ULONG fence, BOOLEAN uncertain) {
  if (!context || !fence ||
      (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)!=fence)
    return;
  if (uncertain) InterlockedExchange(&context->GpuvaG3CancelUncertain,1);
  InterlockedExchange(&context->GpuvaG3CancelFence,(LONG)fence);
}

/* Queued preemption transfers ownership back to VidSch, which can resubmit
 * this same scene with a new fence. Keep the queued hold until that transfer
 * or context teardown; an ordinary cancellation has different lifetime rules. */
VOID AdmissionGpuvaG3PrivatePreempt(ADMISSION_RENDER_CONTEXT *context,
    ULONG fence) {
  if (!context || !fence ||
      (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)!=fence ||
      InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0) ||
      InterlockedCompareExchange(&context->GpuvaG3CancelUncertain,0,0)) return;
  InterlockedExchange(&context->GpuvaG3PreemptFence,(LONG)fence);
}

static BOOLEAN AdmissionG3PrivateReap(ADMISSION_G3_PROCESS *p) {
  ADMISSION_G3_PRIVATE_SCENE *s,*next;
  ADMISSION_BACKEND_MEMORY_VIEW view;
  if (!p->PrivateScenes) return TRUE;
  if (!NT_SUCCESS(AdmissionMemoryRuntimePrivateView(p->State->Adapter,&view))) return FALSE;
  for (s=p->PrivateScenes;s;s=next) {
    next=s->Next;
    if (s->Submitting) continue; /* Submit retains a local pointer through rollback. */
    if (s->Queued && (ULONG)InterlockedCompareExchange(
            &s->Context->GpuvaG3CancelFence,0,0)==s->Fence) {
      if (s->Started || InterlockedCompareExchange(
              &s->Context->GpuvaG3CancelUncertain,0,0)) {
        s->Quarantined=1u;ADMISSION_G3_POISON(p,1u);
      } else {
        s->Queued=0u;
        InterlockedExchange(&s->Context->GpuvaG3PreemptFence,0);
        InterlockedExchange(&s->Context->GpuvaG3PrivateFence,0);
      }
    }
    if (s->Quarantined || p->Graph.Uncertain) continue;
    if (s->ReleaseRequested && !s->Queued &&
        !p->Graph.JobInFlight && !p->Graph.LeaseToken &&
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
  if (!InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)) {
    ok=TRUE;goto Done; /* Legacy job, or an already reported exact transaction. */
  }
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Context==context && s->Fence==fence && s->Queued) break;
  if (!s || !s->Started || !s->GpuDone) goto Done;
  s->Reported=1u;s->Queued=0u;
  if (p->State->PrivateCompletionFence==fence) p->State->PrivateCompletionFence=0u;
  InterlockedExchange(&context->GpuvaG3PrivateFence,0);
  /* Failed reclaim must quarantine, but cannot undo a fence already reported
   * to Windows. The retained process record prevents reuse and destruction. */
  (void)AdmissionG3PrivateReap(p);
  ok=TRUE;
Done:
  poison_site=p->Poisoned ? p->PoisonSite : 0u;
  ExReleaseFastMutex(&p->State->Lock);
#ifdef _MSC_VER
  if (poison_site) AdmissionRecordG3Poison(adapter, poison_site, p->OsProcessId);
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
        (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)==s->Fence &&
        (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence,0,0)==s->Fence) {
      s->Queued=0u;
      InterlockedExchange(&context->GpuvaG3PreemptFence,0);
      InterlockedExchange(&context->GpuvaG3PrivateFence,0);
    }
    if (s->Queued || s->Quarantined) goto Done;
  }
  if (!p->PrivateManager.Generation) {ok=TRUE;goto Done;}
  if (p->Graph.JobInFlight || p->Graph.LeaseToken ||
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
    for (i=0;i<3;++i)
      if (!AdmissionG3PrivateMapExtent(p,&view,&p->PrivateManager.Extents[i],FALSE)) {
        ADMISSION_G3_POISON(p,1u);goto Done;
      }
    for (i=0;i<3;++i)
      if (!AdmissionG3PrivateFreeExtent(p,&view,&p->PrivateManager.Extents[i])) {
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
    AdmissionRecordDwmSourceMap(adapter->PhysicalDeviceObject, &map, ordinal);
  }
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
  ADMISSION_SCANOUT_MEMORY_VIEW view;
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
  UINT pte_wait=0u;
  ULONGLONG pte_wait_start=0ULL;
  LARGE_INTEGER delay;
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
  COPY_REJECT_IF(q->Reserved || q->Reserved2, 13u, STATUS_INVALID_PARAMETER, Free);
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
RetryPagingQuiescence:
  for(;;++wait_ms) {
    ExAcquireFastMutex(&state->Lock);
    p=AdmissionGpuvaG3FindProcess(state,args->hKmdProcessHandle);
    /* R161 (EXP874): an UPLOAD/DOWNLOAD also waits until every built paging
     * FILL/TRANSFER ran; otherwise it writes the new placement and a later
     * transfer overwrites it (or it reads the placement before the move). */
    /* EXP1002: canonical copies are process-private and run under this
     * lock (BeginJob takes it too); only this process's job conflicts. */
    if(!p || (state->ActiveProcess!=p && !p->Graph.JobInFlight && !p->Graph.LeaseToken &&
        (q->Operation==APPLE_AGX_G3_COPY_QUERY || AdmissionPagingQuiescent(adapter))) ||
       wait_ms>=3000u) break;
    ExReleaseFastMutex(&state->Lock);
    delay.QuadPart=-10000LL;
    (void)KeDelayExecutionThread(KernelMode,FALSE,&delay);
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
  COPY_REJECT_IF(!allocation->Win32ClassId, 39u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(allocation->Object.Description.CpuVisible, 40u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(allocation->Object.Description.Type!=ADMISSION_WIN32_ALLOCATION_GPU_LOCAL, 41u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(state->ActiveProcess==p, 42u, STATUS_DEVICE_BUSY, Unlock);
  COPY_REJECT_IF(p->Graph.JobInFlight, 43u, STATUS_DEVICE_BUSY, Unlock);
  COPY_REJECT_IF(p->Graph.LeaseToken, 44u, STATUS_DEVICE_BUSY, Unlock);
  /* A paging buffer can be built between the initial check and either final
   * check. Retry the whole owner/context validation on either late change,
   * preserving the original cumulative 3 s bound and R161 guard. */
  if(q->Operation!=APPLE_AGX_G3_COPY_QUERY) {
    BOOLEAN pagingReady=AdmissionPagingQuiescent(adapter);
    if(pagingReady) pagingReady=AdmissionPagingQuiescent(adapter);
    if(!pagingReady && wait_ms<3000u) {
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
      (opened->ReadOnly || !(opened->Win32Flags&AppleAgxWin32BufferCpuWrite)), 46u, STATUS_INVALID_PARAMETER, Unlock);
  COPY_REJECT_IF(q->Operation==APPLE_AGX_G3_COPY_DOWNLOAD &&
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
    COPY_REJECT_IF(pte->SegmentId!=ADMISSION_MEMORY_LOCAL_SEGMENT, 58u, STATUS_INVALID_PARAMETER, Unlock);
    COPY_REJECT_IF(pte->Allocation!=(ULONGLONG)(ULONG_PTR)allocation, 59u, STATUS_INVALID_PARAMETER, Unlock);
    COPY_REJECT_IF(pte->AllocationOffset!=page-q->GpuVa, 60u, STATUS_INVALID_PARAMETER, Unlock);
    COPY_REJECT_IF(!AppleAgxGpuvaG3TableSpanWithinLocal(view.GuestIpaAddress,view.Bytes,
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
      PUCHAR cpu=(PUCHAR)view.CpuAddress+(SIZE_T)(pte->GuestIpa-view.GuestIpaAddress)+(SIZE_T)(va&0xfffULL);
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
    s->Version=3u;
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
  r->Version=1u;r->Bytes=sizeof(*r);r->Branch=branch;r->Status=(UINT)status;
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
  LARGE_INTEGER delay;
  UINT required[9]={0}, tablePredicate=0u, mapOffset=0u;
  APPLE_AGX_G3_PRIVATE_POOL_STATS observed={0};
  APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC prepare={0};
  BOOLEAN captured=FALSE;
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
  for (wait_ms=0u;;++wait_ms) {
    ExAcquireFastMutex(&state->Lock);
    p=AdmissionGpuvaG3FindProcess(state,args->hKmdProcessHandle);
    if (!p || q.Operation==APPLE_AGX_G3_PRIVATE_RELEASE ||
        (!p->Graph.JobInFlight && !p->Graph.LeaseToken) || wait_ms>=3000u) break;
    ExReleaseFastMutex(&state->Lock);
    delay.QuadPart=-10000LL;
    (void)KeDelayExecutionThread(KernelMode,FALSE,&delay);
  }
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
      (p->Graph.JobInFlight || p->Graph.LeaseToken))
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
    if (scene->Queued || scene->Submitting ||
        p->Graph.JobInFlight || p->Graph.LeaseToken)
      { status=STATUS_SUCCESS;goto Done; }
    status=AdmissionG3PrivateReleaseScene(p,scene,&view) ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    goto Done;
  }
  if (q.SceneId || q.SceneGeneration || q.Width>16384u || q.Height>16384u ||
      q.Layers!=1u || q.Samples!=1u || q.UtileWidth>32u || q.UtileHeight>32u)
    {PRIVATE_CAPTURE(5u,~0u,NULL);goto Done;}
  if (q.Operation==APPLE_AGX_G3_PRIVATE_ACQUIRE) {
    if (q.ManagerId || q.ManagerGeneration) {PRIVATE_CAPTURE(6u,~0u,NULL);goto Done;}
  } else if (q.Operation==APPLE_AGX_G3_PRIVATE_PREPARE) {
    if (!q.ManagerGeneration || q.ManagerId!=p->Graph.ProcessId ||
        q.ManagerGeneration!=p->PrivateManager.Generation ||
        context->GpuvaG3PrivateManagerGeneration!=q.ManagerGeneration) goto Done;
  } else goto Done;
  RtlZeroMemory(&render,sizeof(render));
  render.WidthPx=(USHORT)q.Width;render.HeightPx=(USHORT)q.Height;
  render.UtileWidthPx=(UCHAR)q.UtileWidth;render.UtileHeightPx=(UCHAR)q.UtileHeight;
  render.Layers=1;render.Samples=1;
  if (!AppleAgxG4ProcessRequiredBytes(&render,required)) {PRIVATE_CAPTURE(8u,~0u,NULL);goto Done;}
  status=AdmissionG3PrivateTables(p,&view,&tablePredicate,&observed);
  if (!NT_SUCCESS(status)) {PRIVATE_CAPTURE(9u,~0u,&observed);goto Done;}
  scene=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*scene),ADMISSION_POOL_TAG);
  if (!scene) {status=STATUS_INSUFFICIENT_RESOURCES;PRIVATE_CAPTURE(10u,~0u,NULL);goto Done;}
  RtlZeroMemory(scene,sizeof(*scene));scene->Context=context;scene->Geometry=render;
  fresh=p->PrivateManager.Generation==0;
  status=AdmissionG3PreparePrivateStorageObserved(p,&render,&p->PrivateManager,&scene->Storage,&prepare);
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
    for (i=0;i<3;++i)
      if (!AdmissionG3PrivateMapExtent(p,&view,&p->PrivateManager.Extents[i],FALSE)) {
        ADMISSION_G3_POISON(p,1u);goto Done;
      }
    for (i=0;i<3;++i) (void)AdmissionG3PrivateFreeExtent(p,&view,&p->PrivateManager.Extents[i]);
    RtlZeroMemory(&p->PrivateManager,sizeof(p->PrivateManager));
    RtlZeroMemory(&p->FirmwareManager,sizeof(p->FirmwareManager));
  }
Done:
  if (p && p->Graph.Uncertain) ADMISSION_G3_POISON(p,1u);
  ExReleaseFastMutex(&state->Lock);
  if (captured) AdmissionRecordG3PrivateFailure(adapter);
#undef PRIVATE_CAPTURE
  return status;
}

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
    reserve.SizeInBytes = 0x02000000ULL;
    reserve.Alignment = 0x02000000u;
    status = adapter->Interface.DxgkCbReserveGpuVirtualAddressRange(
        adapter->Interface.DeviceHandle, &reserve);
    if (!NT_SUCCESS(status)) goto Fail;
    /* Validate the OS-owned reservation in leaf-table spans (32 MiB).
     * VidMm may return a slot within native root entry zero; requiring a
     * whole root-entry span (64 GiB) rejects its successful reservation. */
    if (reserve.StartVirtualAddress < (1ULL << 25) ||
        reserve.StartVirtualAddress >= (1ULL << 39) ||
        (reserve.StartVirtualAddress & (0x02000000ULL - 1ULL)) ||
        reserve.SizeInBytes > (1ULL << 39) - reserve.StartVirtualAddress) {
      status = STATUS_INVALID_ADDRESS;
      goto Fail;
    }
    process->PrivateVa = reserve.StartVirtualAddress;
    process->DxgkProcess = Args->hDxgkProcess;
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

_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyProcess(
    PVOID MiniportDeviceContext, HANDLE KmdProcessHandle) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)MiniportDeviceContext;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  if (adapter == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
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
            entry->ResidentPtes[i].SegmentId == 0u)
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
  if (process->Poisoned || process->Graph.Uncertain ||
      !NT_SUCCESS(AdmissionGpuvaG3BrokerTable(
          process, root_ipa, TRUE, &root_ipa)) ||
      !AppleAgxGpuvaG3GraphRegisterTable(&process->Graph, root_ipa, 0u) ||
      !AppleAgxGpuvaG3GraphBindRoot(&process->Graph, root_ipa) ||
      (process->PrivateLeafIpa &&
       !AppleAgxGpuvaG3GraphAttachPrivate(&process->Graph, process->PrivateVa,
           process->PrivateMiddleIpa, process->PrivateLeafIpa))) {
    context->GpuvaG3Poisoned = TRUE;
    ADMISSION_G3_POISON(process,1u);
  } else {
    context->GpuvaG3RootIpa = root_ipa;
  }
  AdmissionG3RecordSetRootSeen(process, context, root_ipa);
  ExReleaseFastMutex(&process->State->Lock);
}

static BOOLEAN AdmissionG3OutputMatchesLocal(
    ADMISSION_CONTEXT *adapter, APPLE_AGX_GPUVA_G3_GRAPH *graph) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  const ADMISSION_RENDER_PACKET_DESCRIPTION *packet =
      &adapter->RenderPacket.Description;
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
          (ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0)==fence) :
          (s->Queued || InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)))) return NULL;
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
      InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0) ||
      InterlockedCompareExchange(&context->GpuvaG3CancelUncertain,0,0)) return NULL;
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Storage.Generation==lease->SceneId) break;
  if (!s || s->Context!=context || s->Storage.Generation!=lease->SceneGeneration ||
      !s->Queued || s->Submitting || s->Started || s->Quarantined || !fence ||
      !s->Fence || fence==s->Fence ||
      (ULONG)InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)!=s->Fence ||
      (ULONG)InterlockedCompareExchange(&context->GpuvaG3PreemptFence,0,0)!=s->Fence)
    return NULL;
  return s;
}

static int AdmissionG4PrivateGraphAccess(void *opaque, unsigned long long va,
    unsigned int bytes, int write, APPLE_AGX_G4_ACCESS_KIND kind,
    unsigned int ordinal) {
  ADMISSION_G3_PRIVATE_SCENE *scene=(ADMISSION_G3_PRIVATE_SCENE *)opaque;
  ADMISSION_G3_PROCESS *p=(ADMISSION_G3_PROCESS *)scene->Context->GpuvaG3Process;
  if (kind==AppleAgxG4AccessProcess) {
    if (ordinal>=9u || va!=scene->Storage.Ranges[ordinal].Va ||
        bytes!=scene->Storage.Ranges[ordinal].Bytes || !write) return 0;
    return AdmissionG4GraphAccess(&p->Graph,va,bytes,write);
  }
  return AdmissionG4GraphAccessTyped(p,va,bytes,write,kind,ordinal);
}

static BOOLEAN AdmissionG4PrivateGeometry(ADMISSION_G3_PRIVATE_SCENE *scene,
    const APPLE_AGX_G4_SUBMIT_VIEW *view) {
  APPLE_AGX_G4_NATIVE_RENDER r;
  if (!scene || !view->Render || view->RenderBytes!=sizeof(r)) return FALSE;
  RtlCopyMemory(&r,view->Render,sizeof(r));
  return r.WidthPx==scene->Geometry.WidthPx && r.HeightPx==scene->Geometry.HeightPx &&
      r.UtileWidthPx==scene->Geometry.UtileWidthPx &&
      r.UtileHeightPx==scene->Geometry.UtileHeightPx &&
      r.Layers==scene->Geometry.Layers && r.Samples==scene->Geometry.Samples &&
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
      scene->Fence=scene->ResumeFence;scene->ResumeFence=0;
      InterlockedExchange(&scene->Context->GpuvaG3PrivateFence,(LONG)scene->Fence);
      InterlockedExchange(&scene->Context->GpuvaG3PreemptFence,(LONG)scene->Fence);
    } else {
      scene->Queued=0;scene->Fence=0;
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

NTSTATUS AdmissionGpuvaG3BeginJob(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  APPLE_AGX_G4_SUBMIT_VIEW g4_view = {0};
  BOOLEAN g4_valid = TRUE;
  ADMISSION_G3_PRIVATE_SCENE *private_scene = NULL;
  NTSTATUS status = STATUS_INVALID_DEVICE_STATE;
  if (adapter == NULL || context == NULL || fence == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  if (state == NULL || process == NULL || process->State != state)
    return STATUS_INVALID_DEVICE_STATE;
  ExAcquireFastMutex(&state->Lock);
#if ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB
  AdmissionG3VerifyUploads(adapter,state,process);
#endif
  if (adapter->BackendImage.G4Native) {
    ADMISSION_BACKEND_IMAGE *image = &adapter->BackendImage;
    if (image->G4Lease.SceneId)
      private_scene=AdmissionG4FindPrivateScene(process,context,&image->G4Lease,fence,TRUE);
    if ((image->G4Lease.SceneId && !private_scene) || image->BoundFence != fence ||
        image->G4CommandBytes == 0u ||
        image->G4CommandBytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
        AppleAgxG4ParseSubmitEx(&image->G4Header,
            (unsigned int)sizeof(image->G4Header) + image->G4CommandBytes,
            (unsigned int)sizeof(image->G4Header) + image->G4CommandBytes,
            image->G4Header.Base.CommandVa, image->G4CommandBytes,
            private_scene ? AdmissionG4PrivateGraphAccess : AdmissionG4GraphAccessTyped,
            private_scene ? (void *)private_scene : (void *)process,
            &g4_view, NULL) != AppleAgxG4ParseOk ||
        (private_scene && !AdmissionG4PrivateGeometry(private_scene,&g4_view)) ||
        image->G4Header.Base.CommandVa != context->GpuvaG3DmaBufferVa ||
        image->G4CommandBytes != context->GpuvaG3DmaBufferBytes)
      g4_valid = FALSE;
  }
  if (state->ActiveProcess == NULL && !state->PrivateCompletionFence && !process->Poisoned &&
      g4_valid &&
      !context->GpuvaG3Poisoned && context->GpuvaG3RootIpa != 0ULL &&
      context->GpuvaG3RootIpa == process->Graph.RootIpa &&
      (adapter->BackendImage.G4Native ?
          AdmissionG4LogicalEnvelopeAccess(process,
              context->GpuvaG3DmaBufferVa,
              context->GpuvaG3DmaBufferBytes) :
          AppleAgxGpuvaG3GraphContainsRange(&process->Graph,
              context->GpuvaG3DmaBufferVa,
              context->GpuvaG3DmaBufferBytes)) &&
      AdmissionG3OutputMatchesLocal(adapter, &process->Graph) &&
      AppleAgxGpuvaG3GraphBeginJob(&process->Graph, 1u)) {
    if (private_scene) {
      ADMISSION_BACKEND_IMAGE *image=&adapter->BackendImage;
      private_scene->Started=1u;
      /* The exact scene hold pins this snapshot through joined completion. */
      image->G4Manager=&process->FirmwareManager;
      image->G4ManagerKey.Owner=process->Graph.ProcessId;
      image->G4ManagerKey.Generation=process->PrivateManager.Generation;
      image->G4ManagerKey.RootIpa=process->Graph.RootIpa;
      RtlCopyMemory(image->G4ManagerKey.Backing,private_scene->Storage.Ranges,
          sizeof(image->G4ManagerKey.Backing));
    }
    /* The graph lock covers fresh range/output validation and JOB_BEGIN.
     * A process-wide epoch also changes for unrelated mappings; it cannot
     * invalidate an otherwise valid job. JOB_BEGIN pins the validated graph. */
    context->GpuvaG3MappingGeneration = process->Graph.MappingGeneration;
    state->ActiveProcess = process;
    state->ActiveFence = fence;
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
  ExReleaseFastMutex(&state->Lock);
#ifdef _MSC_VER
  if (KeGetCurrentIrql() == PASSIVE_LEVEL) AdmissionRecordG4DrawSnapshot(adapter);
#endif
  return status;
}

BOOLEAN AdmissionGpuvaG3CompleteJob(ADMISSION_CONTEXT *adapter, ULONG fence) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  BOOLEAN complete;
  if (adapter == NULL || fence == 0u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return FALSE;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return FALSE;
  ExAcquireFastMutex(&state->Lock);
  process = state->ActiveProcess;
  if (process == NULL && state->LastCompletedFence == fence) {
    ExReleaseFastMutex(&state->Lock);
    return TRUE;
  }
  if (process == NULL || state->ActiveFence != fence) {
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
    state->ActiveProcess = NULL;
    state->ActiveFence = 0u;
    state->LastCompletedFence = fence;
  } else {
    ADMISSION_G3_POISON(process,1u);
  }
  ExReleaseFastMutex(&state->Lock);
  return complete;
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
      args->NodeOrdinal != 0u ||
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

static int AdmissionG4GraphAccess(void *opaque, unsigned long long va,
                                  unsigned int bytes, int write) {
  APPLE_AGX_GPUVA_G3_GRAPH *graph = (APPLE_AGX_GPUVA_G3_GRAPH *)opaque;
  return AppleAgxGpuvaG3GraphContainsRangeAccess(
      graph, va, bytes, write != 0) ? 1 : 0;
}

typedef struct _ADMISSION_G3_PRESENT_COPY {
  ADMISSION_G3_PROCESS *Process;
  ADMISSION_SCANOUT_MEMORY_VIEW Local;
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
      pte->SegmentId > ADMISSION_MEMORY_LOCAL_SEGMENT ||
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
        sourcePte->SegmentId > ADMISSION_MEMORY_LOCAL_SEGMENT ||
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
        pte->SegmentId > ADMISSION_MEMORY_LOCAL_SEGMENT) return 0;
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
  }
}

static BOOLEAN AdmissionG4ResolveOutput(
    ADMISSION_G3_PROCESS *process,
    const APPLE_AGX_G4_ATTACHMENT *attachment,
    const ADMISSION_SCANOUT_MEMORY_VIEW *local,
    ADMISSION_RENDER_PACKET_DESCRIPTION *packet) {
  ULONGLONG ipa, offset, position;
  if (process == NULL || attachment == NULL || local == NULL ||
      packet == NULL || attachment->Pointer == 0ULL ||
      attachment->Size == 0ULL || attachment->Size > MAXULONG ||
      !AppleAgxGpuvaG3GraphTranslateVa(&process->Graph,
          attachment->Pointer, &ipa) || ipa < local->GuestIpaAddress)
    return FALSE;
  offset = ipa - local->GuestIpaAddress;
  if (offset > local->Bytes ||
      attachment->Size > local->Bytes - offset ||
      local->HostPhysicalAddress > MAXULONGLONG - offset)
    return FALSE;
  for (position = 0ULL; position < attachment->Size;) {
    ULONGLONG mapped;
    if (!AppleAgxGpuvaG3GraphTranslateVa(&process->Graph,
            attachment->Pointer + position, &mapped) ||
        mapped != ipa + position) return FALSE;
    position += 0x4000ULL -
        ((attachment->Pointer + position) & 0x3fffULL);
  }
  packet->DestinationCpuToken =
      (ULONGLONG)(ULONG_PTR)((PUCHAR)local->CpuAddress + (SIZE_T)offset);
  packet->DestinationGpuVa = attachment->Pointer;
  packet->DestinationPhysical = local->HostPhysicalAddress + offset;
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
  BOOLEAN prepared = FALSE, queued = FALSE;
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
      ADMISSION_G4_REJECTS(13u, !ADMISSION_G4_RUNTIME_READY()) ||
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
  if (context->GpuvaG3RootIpa != process->Graph.RootIpa ||
      process->Graph.Uncertain || state->PrivateCompletionFence) {
    ExReleaseFastMutex(&state->Lock);
    return AdmissionG4SubmitReject(adapter, context, args,
        AdmissionG4RejectRoot, STATUS_INVALID_PARAMETER, 0u, TRUE);
  }
  if (args->pDmaBufferPrivateData && args->DmaBufferUmdPrivateDataSize>=sizeof(private_header) &&
      args->DmaBufferPrivateDataSize>=args->DmaBufferUmdPrivateDataSize) {
    RtlCopyMemory(&private_header,args->pDmaBufferPrivateData,sizeof(private_header));
    if (private_header.Version==APPLE_AGX_G4_PRIVATE_VERSION_PRIVATE_VA &&
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
        ExReleaseFastMutex(&state->Lock);
        return AdmissionG4SubmitRejectDetail(adapter,context,args,
            AdmissionG4RejectEnvelopeState,STATUS_INVALID_PARAMETER,0u,TRUE,&detail);
      }
    }
  }
  result = AppleAgxG4ParseSubmitEx(
      args->pDmaBufferPrivateData, args->DmaBufferPrivateDataSize,
      args->DmaBufferUmdPrivateDataSize, args->DmaBufferVirtualAddress,
      args->DmaBufferSize,
      private_scene ? AdmissionG4PrivateGraphAccess : AdmissionG4GraphAccessTyped,
      private_scene ? (void *)private_scene : (void *)process, &view, &failure);
  if (result==AppleAgxG4ParseOk &&
      ((view.Lease.SceneId && !private_scene) ||
       (private_scene && (!AdmissionG4PrivateGeometry(private_scene,&view) ||
        RtlCompareMemory(&view.Lease,&private_v3.Lease,sizeof(view.Lease))!=sizeof(view.Lease)))))
    result=AppleAgxG4ParseInvalid;
  if (result == AppleAgxG4ParseOk && view.AttachmentCount == 1u) {
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
    private_scene->ResumeFence=args->Flags.Resubmission ? private_scene->Fence : 0u;
    private_scene->Submitting=1u;private_scene->Queued=1u;
    private_scene->Fence=args->SubmissionFenceId;
    InterlockedExchange(&context->GpuvaG3PreemptFence,0);
    InterlockedExchange(&context->GpuvaG3CancelFence,0);
    InterlockedExchange(&context->GpuvaG3CancelUncertain,0);
    InterlockedExchange(&context->GpuvaG3PrivateFence,(LONG)args->SubmissionFenceId);
  }
  ExReleaseFastMutex(&state->Lock);
  if (result != AppleAgxG4ParseOk)
    return AdmissionG4SubmitRejectDetail(adapter, context, args,
        AdmissionG4RejectParse, STATUS_INVALID_PARAMETER, (ULONG)result,
        TRUE, &detail);
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketEmpty &&
      context->Object.FenceOutstanding == 0u &&
      InterlockedCompareExchange(&adapter->SchedulerFaulted, 0, 0) == 0 &&
      AdmissionRenderPacketPrepare(&adapter->RenderPacket, &packet)) {
    context->Object.FenceOutstanding = args->SubmissionFenceId;
    prepared = TRUE;
  }
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (!prepared) {
    AdmissionG4PrivateUnqueue(process,private_scene,args->SubmissionFenceId);
    return AdmissionG4SubmitReject(adapter, context, args,
        AdmissionG4RejectPrepare, STATUS_INVALID_PARAMETER, 0u, TRUE);
  }
  if (!AdmissionBackendImageBindG4Submission(&adapter->BackendImage,
          &packet, (PVOID)(ULONG_PTR)packet.DestinationCpuToken,
          &view, &binding)) goto Rollback;
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
  if (!queued) context->Object.FenceOutstanding = 0u;
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  if (queued && private_scene) {
    private_scene->ResumeFence=0u;private_scene->Submitting=0u;
  }
  ExReleaseFastMutex(&state->Lock);
  if (!queued) goto Rollback;
  AdmissionDispatchQueuedWork(adapter);
  return STATUS_SUCCESS;
Rollback:
  KeAcquireSpinLock(&adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&adapter->RenderPacket) ==
          AdmissionRenderPacketPrepared)
    (void)AdmissionRenderPacketCancelPrepared(&adapter->RenderPacket,
        (ULONGLONG)(ULONG_PTR)context);
  context->Object.FenceOutstanding = 0u;
  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);
  context->GpuvaG3DmaBufferVa = 0ULL;
  context->GpuvaG3DmaBufferBytes = 0u;
  if (adapter->BackendImage.G4Native)
    (void)AdmissionBackendImageReleaseSubmission(&adapter->BackendImage,
        args->SubmissionFenceId);
  AdmissionG4PrivateUnqueue(process,private_scene,args->SubmissionFenceId);
  return AdmissionG4SubmitReject(adapter, context, args,
      rollbackBranch, STATUS_INVALID_PARAMETER, 0u, TRUE);
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
      Args->NodeOrdinal != 0u ||
      Args->EngineOrdinal != 0u || Args->SubmissionFenceId == 0u ||
      KeGetCurrentIrql() > DISPATCH_LEVEL)
    return AdmissionG4SubmitReject(adapter, NULL, Args,
        AdmissionG4RejectOuter, STATUS_INVALID_PARAMETER, 0u, FALSE);
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter)
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
  NTSTATUS status = AdmissionDdiSubmitCommandVirtualInner(Adapter, Args);
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
