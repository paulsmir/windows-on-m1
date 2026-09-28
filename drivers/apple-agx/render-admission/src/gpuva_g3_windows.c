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
NTSTATUS AdmissionG3PreparePrivateStorage(ADMISSION_G3_PROCESS *process,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    APPLE_AGX_G3_PRIVATE_MANAGER *manager, APPLE_AGX_G3_PRIVATE_SCENE *scene) {
  ADMISSION_BACKEND_MEMORY_VIEW view;
  NTSTATUS status;
  if (process == NULL || process->Poisoned || process->Graph.Uncertain ||
      process->Graph.JobInFlight || process->Graph.LeaseToken)
    return STATUS_INVALID_DEVICE_STATE;
  status = AdmissionMemoryRuntimePrivateView(process->State->Adapter, &view);
  if (!NT_SUCCESS(status)) return status;
  if (view.CpuAddress == NULL || view.Bytes != (16ULL << 20) ||
      (view.GuestIpaAddress & 0xffffULL)) return STATUS_INVALID_ADDRESS;
  if (!AppleAgxG3PrivatePrepare(&process->State->PrivatePool,
      process->Graph.ProcessId, view.CpuAddress, process->PrivateVa, render,
      manager, scene)) return STATUS_INSUFFICIENT_RESOURCES;
  KeMemoryBarrier();
  return STATUS_SUCCESS;
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

static BOOLEAN AdmissionG3PrivateMapExtent(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view, const APPLE_AGX_G3_PRIVATE_EXTENT *e,
    BOOLEAN publish) {
  UINT offset;
  for (offset=0; offset<e->Bytes; offset+=0x4000u)
    if (!AppleAgxGpuvaG3GraphUpdateLeafBacking(&p->Graph,p->PrivateLeafIpa,
            (e->Offset+offset)>>14,
            publish ? view->GuestIpaAddress+e->Offset+offset : 0ULL,
            publish != FALSE,AppleAgxGpuvaG3PrivateBacking)) return FALSE;
  return TRUE;
}

static NTSTATUS AdmissionG3PrivateTables(ADMISSION_G3_PROCESS *p,
    ADMISSION_BACKEND_MEMORY_VIEW *view) {
  UINT i;
  if (!p->PrivateMiddleIpa) {
    for (i=0;i<2;++i) {
      if (!AppleAgxG3PrivateAllocate(&p->State->PrivatePool,p->Graph.ProcessId,
              0x10000u,&p->PrivateTables[i])) {
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
  if (!AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,p->PrivateMiddleIpa,1u) ||
      !AppleAgxGpuvaG3GraphRegisterTable(&p->Graph,p->PrivateLeafIpa,2u) ||
      !AppleAgxGpuvaG3GraphAttachPrivate(&p->Graph,p->PrivateVa,
          p->PrivateMiddleIpa,p->PrivateLeafIpa))
    return p->Graph.Uncertain ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_INSUFFICIENT_RESOURCES;
  return STATUS_SUCCESS;
}

static BOOLEAN AdmissionG3PrivateReleaseScene(ADMISSION_G3_PROCESS *p,
    ADMISSION_G3_PRIVATE_SCENE *scene, ADMISSION_BACKEND_MEMORY_VIEW *view) {
  ADMISSION_G3_PRIVATE_SCENE **link=&p->PrivateScenes;
  UINT i;
  if (scene->Queued || scene->Quarantined || p->Graph.Uncertain ||
      p->Graph.JobInFlight || p->Graph.LeaseToken) return FALSE;
  for (i=0;i<6;++i)
    if (!AdmissionG3PrivateMapExtent(p,view,&scene->Storage.Extents[i],FALSE)) {
      scene->Quarantined=1u;p->Poisoned=TRUE;return FALSE;
    }
  for (i=0;i<6;++i)
    if (!AdmissionG3PrivateFreeExtent(p,view,&scene->Storage.Extents[i])) {
      scene->Quarantined=1u;p->Poisoned=TRUE;return FALSE;
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
        s->Quarantined=1u;p->Poisoned=TRUE;
      } else {
        s->Queued=0u;
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
        s->Quarantined=1u;p->Poisoned=TRUE;safe=FALSE;
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
  if (!context || !context->GpuvaG3Process) return TRUE;
  if (!adapter || !fence || KeGetCurrentIrql()!=PASSIVE_LEVEL) return FALSE;
  p=(ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  if (p->State->Adapter!=adapter) return FALSE;
  ExAcquireFastMutex(&p->State->Lock);
  if (!AdmissionG3PrivateReap(p)) goto Done;
  if (!InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)) {
    ok=TRUE;goto Done; /* Legacy job, or an already reported exact transaction. */
  }
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Context==context && s->Fence==fence && s->Queued) break;
  if (!s || !s->Started || !s->GpuDone || s->Quarantined) goto Done;
  s->Reported=1u;s->Queued=0u;
  if (p->State->PrivateCompletionFence==fence) p->State->PrivateCompletionFence=0u;
  InterlockedExchange(&context->GpuvaG3PrivateFence,0);
  /* Failed reclaim must quarantine, but cannot undo a fence already reported
   * to Windows. The retained process record prevents reuse and destruction. */
  (void)AdmissionG3PrivateReap(p);
  ok=TRUE;
Done:
  ExReleaseFastMutex(&p->State->Lock);
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
  for (s=p->PrivateScenes;s;s=s->Next)
    if (s->Context==context && (s->Queued || s->Quarantined)) goto Done;
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
        p->Poisoned=TRUE;goto Done;
      }
    for (i=0;i<3;++i)
      if (!AdmissionG3PrivateFreeExtent(p,&view,&p->PrivateManager.Extents[i])) {
        p->Poisoned=TRUE;goto Done;
      }
    RtlZeroMemory(&p->PrivateManager,sizeof(p->PrivateManager));
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
          block->First*APPLE_AGX_G3_PRIVATE_UNIT,block->Count*APPLE_AGX_G3_PRIVATE_UNIT};
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
static const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *AdmissionG3CopyPte(
    ADMISSION_G3_PROCESS *p, ULONGLONG va) {
  APPLE_AGX_GPUVA_G3_NODE *edge;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  ULONGLONG middle, leaf;
  for(edge=p->Graph.Parents;edge;edge=edge->Next)
    if(edge->Ipa==p->Graph.RootIpa && edge->Index==(UINT)(va>>36)) break;
  if(!edge) return NULL;
  middle=edge->AuxIpa;
  for(edge=p->Graph.Parents;edge;edge=edge->Next)
    if(edge->Ipa==middle && edge->Index==(UINT)((va>>25)&2047u)) break;
  if(!edge) return NULL;
  leaf=edge->AuxIpa;
  for(shadow=p->TableShadows;shadow;shadow=shadow->Next)
    if(shadow->BrokerIpa==leaf) break;
  if(!shadow || !shadow->ResidentPtes) return NULL;
  return &shadow->ResidentPtes[(UINT)((va>>12)&8191u)];
}

NTSTATUS AdmissionGpuvaG3CopyEscape(ADMISSION_CONTEXT *adapter,
    const DXGKARG_ESCAPE *args) {
  APPLE_AGX_G3_COPY_REQUEST *q;
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *p;
  ADMISSION_RENDER_CONTEXT *context;
  ADMISSION_OPEN_ALLOCATION *opened;
  ADMISSION_ALLOCATION_HANDLE *allocation;
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  DXGKARGCB_GETHANDLEDATA lookup={0};
  DXGKARGCB_RELEASEHANDLEDATA reference={0};
  ULONGLONG length, offset, end, page, first;
  NTSTATUS status=STATUS_INVALID_PARAMETER;
  if(!adapter || !adapter->Started || !args ||
     KeGetCurrentIrql()!=PASSIVE_LEVEL || args->Flags.Value!=1u ||
     args->PrivateDriverDataSize!=sizeof(*q) || !args->pPrivateDriverData ||
     !adapter->Interface.DxgkCbAcquireHandleData ||
     !adapter->Interface.DxgkCbReleaseHandleData) return status;
  state=(ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if(!state) return STATUS_INVALID_DEVICE_STATE;
  q=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*q),ADMISSION_POOL_TAG);
  if(!q) return STATUS_INSUFFICIENT_RESOURCES;
  RtlCopyMemory(q,args->pPrivateDriverData,sizeof(*q));
  if(q->Magic!=APPLE_AGX_G3_COPY_MAGIC || q->Version!=1u ||
     q->Bytes!=sizeof(*q) || q->Reserved || q->Reserved2 ||
     q->Operation>APPLE_AGX_G3_COPY_DOWNLOAD || !q->Allocation ||
     !q->GpuVa || (q->GpuVa&0xffffULL) || q->GpuVa>=(1ULL<<39) ||
     q->TransferBytes>APPLE_AGX_G3_COPY_CAPACITY) goto Free;
  if(q->Operation==APPLE_AGX_G3_COPY_QUERY ?
     (q->Offset || q->TransferBytes || q->MappingGeneration || q->ProcessGeneration) :
     (!q->TransferBytes || !q->MappingGeneration || !q->ProcessGeneration)) goto Free;
  lookup.hObject=q->Allocation;lookup.Type=DXGK_HANDLE_ALLOCATION;
  lookup.Flags.DeviceSpecific=1u;reference.Type=DXGK_HANDLE_ALLOCATION;
  opened=(ADMISSION_OPEN_ALLOCATION *)adapter->Interface.DxgkCbAcquireHandleData(
      &lookup,&reference.ReleaseHandle);
  if(!opened || !reference.ReleaseHandle) {status=STATUS_INVALID_HANDLE;goto Release;}
  ExAcquireFastMutex(&state->Lock);
  p=AdmissionGpuvaG3FindProcess(state,args->hKmdProcessHandle);
  if(!p || p->Poisoned || p->Graph.Uncertain || !p->Graph.Created) goto Unlock;
  for(context=p->Contexts;context && (HANDLE)context!=args->hContext;
      context=context->GpuvaG3NextContext) {}
  if(!context || !context->Win32Transport || context->GpuvaG3Closing ||
     context->GpuvaG3Poisoned || !context->Object.Device ||
     (HANDLE)CONTAINING_RECORD(context->Object.Device,ADMISSION_DEVICE,Object)!=args->hDevice ||
     context->Object.Device->Adapter!=&adapter->ObjectAdapter ||
     opened->Magic!=ADMISSION_OPEN_ALLOCATION_MAGIC ||
     (HANDLE)opened->Device!=args->hDevice || !opened->Allocation ||
     opened->RuntimeAllocation!=q->Allocation ||
     opened->Allocation->Magic!=ADMISSION_ALLOCATION_OBJECT_MAGIC) goto Unlock;
  allocation=CONTAINING_RECORD(opened->Allocation,ADMISSION_ALLOCATION_HANDLE,Object);
  if(!allocation->Win32ClassId || allocation->Object.Description.CpuVisible ||
     allocation->Object.Description.Type!=ADMISSION_WIN32_ALLOCATION_GPU_LOCAL) goto Unlock;
  if(state->ActiveProcess || p->Graph.JobInFlight || p->Graph.LeaseToken) {
    status=STATUS_DEVICE_BUSY;goto Unlock;
  }
  if(q->Operation!=APPLE_AGX_G3_COPY_QUERY &&
     (q->ProcessGeneration!=p->Graph.ProcessGeneration ||
      q->MappingGeneration!=p->Graph.MappingGeneration)) goto Unlock;
  if(q->Operation==APPLE_AGX_G3_COPY_UPLOAD &&
     (opened->ReadOnly || !(opened->Win32Flags&AppleAgxWin32BufferCpuWrite))) goto Unlock;
  if(q->Operation==APPLE_AGX_G3_COPY_DOWNLOAD &&
     !(opened->Win32Flags&AppleAgxWin32BufferCpuRead)) goto Unlock;
  length=q->Operation==APPLE_AGX_G3_COPY_QUERY ?
      allocation->Object.Description.Size : q->TransferBytes;
  if(!length || length>MAXULONG || q->Offset>allocation->Object.Description.Size ||
     length>allocation->Object.Description.Size-q->Offset ||
     q->Offset>=(1ULL<<39)-q->GpuVa || length>(1ULL<<39)-q->GpuVa-q->Offset)
    goto Unlock;
  first=q->GpuVa+q->Offset;end=first+length;
  if(!AppleAgxGpuvaG3GraphContainsRangeAccess(&p->Graph,first,(UINT)length,FALSE)) goto Unlock;
  status=AdmissionMemoryRuntimeLocalView(adapter,&view);
  if(!NT_SUCCESS(status)) goto Unlock;
  status=STATUS_INVALID_PARAMETER;
  if(!view.CpuAddress) goto Unlock;
  /* Validate every page before the first store: a malformed tail cannot
   * partially overwrite canonical data. Table/private storage has no matching
   * allocation provenance and cannot pass this join. */
  for(page=first&~0xfffULL;page<end;page+=0x1000ULL) {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte=AdmissionG3CopyPte(p,page);
    if(!pte || !(pte->Flags&APPLE_AGX_GPUVA_G3_VALID) ||
       pte->SegmentId!=ADMISSION_MEMORY_LOCAL_SEGMENT ||
       pte->Allocation!=(ULONGLONG)(ULONG_PTR)allocation ||
       pte->AllocationOffset!=page-q->GpuVa ||
       !AppleAgxGpuvaG3TableSpanWithinLocal(view.GuestIpaAddress,view.Bytes,
           pte->GuestIpa,0x1000ULL)) goto Unlock;
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
  }
  RtlCopyMemory(args->pPrivateDriverData,q,sizeof(*q));status=STATUS_SUCCESS;
Unlock:
  ExReleaseFastMutex(&state->Lock);
Release:
  if(reference.ReleaseHandle) adapter->Interface.DxgkCbReleaseHandleData(reference);
Free:
  ExFreePoolWithTag(q,ADMISSION_POOL_TAG);return status;
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
  if (!adapter || !adapter->Started || !args ||
      KeGetCurrentIrql()!=PASSIVE_LEVEL || args->Flags.Value!=1u ||
      args->PrivateDriverDataSize!=sizeof(q) || !args->pPrivateDriverData)
    return STATUS_INVALID_PARAMETER;
  RtlCopyMemory(&q,args->pPrivateDriverData,sizeof(q));
  if (q.Magic!=APPLE_AGX_G3_PRIVATE_MAGIC || q.Version!=1u ||
      q.Bytes!=sizeof(q) || q.Reserved[0] || q.Reserved[1]) return status;
  for (i=0;i<9;++i)
    if (q.Ranges[i].Va || q.Ranges[i].Bytes || q.Ranges[i].Reserved) return status;
  state=(ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (!state) return STATUS_INVALID_DEVICE_STATE;
  ExAcquireFastMutex(&state->Lock);
  p=AdmissionGpuvaG3FindProcess(state,args->hKmdProcessHandle);
  if (!p || p->Poisoned || p->Graph.Uncertain) goto Done;
  /* Compare handles against attached objects before dereferencing them. */
  for (context=p->Contexts;context && (HANDLE)context!=args->hContext;
       context=context->GpuvaG3NextContext) {}
  if (!context || context->GpuvaG3Closing || !context->Win32Transport || context->GpuvaG3Poisoned ||
      context->Object.Magic!=ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device==NULL ||
      (HANDLE)CONTAINING_RECORD(context->Object.Device,ADMISSION_DEVICE,Object)!=args->hDevice ||
      context->Object.Device->Adapter!=&adapter->ObjectAdapter) goto Done;
  if (!AdmissionG3PrivateReap(p)) {status=STATUS_DEVICE_HARDWARE_ERROR;goto Done;}
  status=AdmissionMemoryRuntimePrivateView(adapter,&view);
  if (!NT_SUCCESS(status)) goto Done;
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
    if (scene->Queued) { status=STATUS_SUCCESS;goto Done; }
    status=AdmissionG3PrivateReleaseScene(p,scene,&view) ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    goto Done;
  }
  if (q.SceneId || q.SceneGeneration || q.Width>16384u || q.Height>16384u ||
      q.Layers!=1u || q.Samples!=1u || q.UtileWidth>32u || q.UtileHeight>32u)
    goto Done;
  if (q.Operation==APPLE_AGX_G3_PRIVATE_ACQUIRE) {
    if (q.ManagerId || q.ManagerGeneration) goto Done;
  } else if (q.Operation==APPLE_AGX_G3_PRIVATE_PREPARE) {
    if (!q.ManagerGeneration || q.ManagerId!=p->Graph.ProcessId ||
        q.ManagerGeneration!=p->PrivateManager.Generation ||
        context->GpuvaG3PrivateManagerGeneration!=q.ManagerGeneration) goto Done;
  } else goto Done;
  if (p->Graph.JobInFlight || p->Graph.LeaseToken) {status=STATUS_DEVICE_BUSY;goto Done;}
  RtlZeroMemory(&render,sizeof(render));
  render.WidthPx=(USHORT)q.Width;render.HeightPx=(USHORT)q.Height;
  render.UtileWidthPx=(UCHAR)q.UtileWidth;render.UtileHeightPx=(UCHAR)q.UtileHeight;
  render.Layers=1;render.Samples=1;
  { UINT required[9]; if (!AppleAgxG4ProcessRequiredBytes(&render,required)) goto Done; }
  status=AdmissionG3PrivateTables(p,&view);
  if (!NT_SUCCESS(status)) goto Done;
  scene=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*scene),ADMISSION_POOL_TAG);
  if (!scene) {status=STATUS_INSUFFICIENT_RESOURCES;goto Done;}
  RtlZeroMemory(scene,sizeof(*scene));scene->Context=context;scene->Geometry=render;
  fresh=p->PrivateManager.Generation==0;
  status=AdmissionG3PreparePrivateStorage(p,&render,&p->PrivateManager,&scene->Storage);
  if (!NT_SUCCESS(status)) {ExFreePoolWithTag(scene,ADMISSION_POOL_TAG);goto Done;}
  scene->Next=p->PrivateScenes;p->PrivateScenes=scene;
  if (fresh)
    for (i=0;i<3;++i)
      if (!AdmissionG3PrivateMapExtent(p,&view,&p->PrivateManager.Extents[i],TRUE)) break;
  if (!fresh || i==3u) {
    for (i=0;i<6;++i)
      if (!AdmissionG3PrivateMapExtent(p,&view,&scene->Storage.Extents[i],TRUE)) break;
    if (i==6u) {
      q.ManagerId=p->Graph.ProcessId;q.ManagerGeneration=p->PrivateManager.Generation;
      q.SceneId=q.SceneGeneration=scene->Storage.Generation;
      RtlCopyMemory(q.Ranges,scene->Storage.Ranges,sizeof(q.Ranges));
      context->GpuvaG3PrivateManagerGeneration=q.ManagerGeneration;
      RtlCopyMemory(args->pPrivateDriverData,&q,sizeof(q));status=STATUS_SUCCESS;goto Done;
    }
  }
  status=STATUS_INSUFFICIENT_RESOURCES;
  if (!AdmissionG3PrivateReleaseScene(p,scene,&view)) {p->Poisoned=TRUE;goto Done;}
  if (fresh) {
    for (i=0;i<3;++i)
      if (!AdmissionG3PrivateMapExtent(p,&view,&p->PrivateManager.Extents[i],FALSE)) {
        p->Poisoned=TRUE;goto Done;
      }
    for (i=0;i<3;++i) (void)AdmissionG3PrivateFreeExtent(p,&view,&p->PrivateManager.Extents[i]);
    RtlZeroMemory(&p->PrivateManager,sizeof(p->PrivateManager));
  }
Done:
  if (p && p->Graph.Uncertain) p->Poisoned=TRUE;
  ExReleaseFastMutex(&state->Lock);
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
      process->Poisoned = TRUE;
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
    context->GpuvaG3Poisoned = TRUE;
    return;
  }
  RtlZeroMemory(&address, sizeof(address));
  address.GpuPhysical = Args->Address;
  if (!NT_SUCCESS(AdmissionGpuvaG3ResolveTable(
          adapter, &address, DXGK_PAGETABLEUPDATE_GPU_PHYSICAL,
          &root_ipa))) {
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
    process->Poisoned = TRUE;
  } else {
    context->GpuvaG3RootIpa = root_ipa;
  }
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
      !NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(adapter, &view)) ||
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
          (ULONG)InterlockedCompareExchange(&context->GpuvaG3CancelFence,0,0)==fence) :
          (s->Queued || InterlockedCompareExchange(&context->GpuvaG3PrivateFence,0,0)))) return NULL;
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
    scene->Submitting=0;scene->Queued=0;scene->Fence=0;
    InterlockedExchange(&scene->Context->GpuvaG3PrivateFence,0);
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

NTSTATUS AdmissionGpuvaG3BeginJob(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  APPLE_AGX_G4_SUBMIT_VIEW g4_view;
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
      (!adapter->BackendImage.G4Native ||
       context->GpuvaG3MappingGeneration == process->Graph.MappingGeneration) &&
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
    if (private_scene) private_scene->Started=1u;
    state->ActiveProcess = process;
    state->ActiveFence = fence;
    status = STATUS_SUCCESS;
  } else if (process->Graph.Uncertain) {
    process->Poisoned = TRUE;
    status = STATUS_DEVICE_HARDWARE_ERROR;
  }
  ExReleaseFastMutex(&state->Lock);
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
    process->Poisoned = TRUE;
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
      args->Flags.Value != pagingFlags.Value || args->NodeOrdinal != 0u ||
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
  APPLE_AGX_GPUVA_G3_NODE *edge;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte;
  if (process == NULL || !process->Graph.Created ||
      process->Graph.Uncertain || process->Graph.RootIpa == 0ULL ||
      !bytes || va < 0x10000ULL || va >= (1ULL << 39) ||
      bytes > (1ULL << 39) - va) return 0;
  end = va + bytes;
  for (page = va & ~0xfffULL; page < end; page += 0x1000ULL) {
    for (edge = process->Graph.Parents; edge != NULL; edge = edge->Next)
      if (edge->Ipa == process->Graph.RootIpa &&
          edge->Index == (ULONG)((page >> 36) & 7u)) break;
    if (edge == NULL) return 0;
    {
      unsigned long long middle = edge->AuxIpa;
      for (edge = process->Graph.Parents; edge != NULL; edge = edge->Next)
        if (edge->Ipa == middle &&
            edge->Index == (ULONG)((page >> 25) & 2047u)) break;
    }
    if (edge == NULL) return 0;
    for (shadow = process->TableShadows; shadow != NULL;
         shadow = shadow->Next)
      if (shadow->BrokerIpa == edge->AuxIpa) break;
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
  if (KeGetCurrentIrql() != PASSIVE_LEVEL || state == NULL ||
      process == NULL || process->State != state || process->Poisoned ||
      context->GpuvaG3Poisoned || context->GpuvaG3RootIpa == 0ULL ||
      !context->Win32Transport ||
      (context->Object.Flags & ADMISSION_CONTEXT_VIRTUAL_ADDRESSING) == 0u ||
      (context->Object.Flags & (ADMISSION_CONTEXT_SYSTEM |
                                ADMISSION_CONTEXT_GDI)) != 0u ||
      args->Flags.Value != 0u ||
      !context->SchedulerContext.Active ||
      !AdmissionPlatformRuntimeReady(adapter) ||
      !NT_SUCCESS(viewStatus =
          AdmissionMemoryRuntimeScanoutView(adapter, &local)))
    return AdmissionG4SubmitReject(adapter, context, args,
        AdmissionG4RejectEnvelopeState, STATUS_INVALID_PARAMETER,
        (ULONG)viewStatus, TRUE);
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
      private_scene=AdmissionG4FindPrivateScene(process,context,&private_v3.Lease,
                                               args->SubmissionFenceId,FALSE);
      if (!private_scene) {
        ExReleaseFastMutex(&state->Lock);
        return AdmissionG4SubmitReject(adapter,context,args,
            AdmissionG4RejectEnvelopeState,STATUS_INVALID_PARAMETER,0u,TRUE);
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
    private_scene->Submitting=1u;private_scene->Queued=1u;
    private_scene->Fence=args->SubmissionFenceId;
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
  if (queued && private_scene) private_scene->Submitting=0u;
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

_Use_decl_annotations_ NTSTATUS AdmissionDdiSubmitCommandVirtual(
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
#if defined(APPLE_AGX_BLT_PROBE_QUALIFICATION)
  InterlockedIncrement((volatile LONG *)&adapter->BltProbe.VirtualSubmitCalls);
#endif
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

#endif
