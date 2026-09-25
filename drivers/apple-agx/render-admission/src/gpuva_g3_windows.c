#include "gpuva_g3_private.h"
#include "apple_agx_g4_submit.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)

C_ASSERT(FIELD_OFFSET(ADMISSION_BACKEND_IMAGE, G4Command) ==
    FIELD_OFFSET(ADMISSION_BACKEND_IMAGE, G4Header) +
    sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V2));

static int AdmissionG4GraphAccess(void *opaque, unsigned long long va,
                                  unsigned int bytes, int write);

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
  status = AdmissionG3BootstrapRoot(process);
  if (!NT_SUCCESS(status)) goto Fail;
  status = AdmissionMemoryRuntimeScanoutView(adapter, &local_view);
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
  if (process->DeviceRefs || process->ContextRefs ||
      process->Graph.Uncertain ||
      (process->Graph.Created &&
       !AppleAgxGpuvaG3GraphDestroy(&process->Graph))) {
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
  ExReleaseFastMutex(&process->State->Lock);
  return STATUS_SUCCESS;
}

VOID AdmissionGpuvaG3DetachContext(ADMISSION_RENDER_CONTEXT *context) {
  ADMISSION_G3_PROCESS *process;
  if (context == NULL || context->GpuvaG3Process == NULL) return;
  process = (ADMISSION_G3_PROCESS *)context->GpuvaG3Process;
  ExAcquireFastMutex(&process->State->Lock);
  if (process->ContextRefs) --process->ContextRefs;
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
  if (adapter == NULL || address == NULL || table_ipa == NULL ||
      !NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(adapter, &view)))
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
      !AppleAgxGpuvaG3GraphBindRoot(&process->Graph, root_ipa)) {
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

NTSTATUS AdmissionGpuvaG3BeginJob(ADMISSION_CONTEXT *adapter,
    ADMISSION_RENDER_CONTEXT *context, ULONG fence) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  APPLE_AGX_G4_SUBMIT_VIEW g4_view;
  BOOLEAN g4_valid = TRUE;
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
    if (image->BoundFence != fence ||
        image->G4CommandBytes == 0u ||
        image->G4CommandBytes > APPLE_AGX_G4_NATIVE_MAX_BYTES ||
        AppleAgxG4ParseSubmit(&image->G4Header,
            (unsigned int)sizeof(image->G4Header) + image->G4CommandBytes,
            (unsigned int)sizeof(image->G4Header) + image->G4CommandBytes,
            image->G4Header.Base.CommandVa, image->G4CommandBytes,
            AdmissionG4GraphAccess, &process->Graph,
            &g4_view) != AppleAgxG4ParseOk)
      g4_valid = FALSE;
  }
  if (state->ActiveProcess == NULL && !process->Poisoned &&
      g4_valid &&
      !context->GpuvaG3Poisoned && context->GpuvaG3RootIpa != 0ULL &&
      context->GpuvaG3RootIpa == process->Graph.RootIpa &&
      AppleAgxGpuvaG3GraphContainsRange(&process->Graph,
          context->GpuvaG3DmaBufferVa,
          context->GpuvaG3DmaBufferBytes) &&
      AdmissionG3OutputMatchesLocal(adapter, &process->Graph) &&
      AppleAgxGpuvaG3GraphBeginJob(&process->Graph, 1u)) {
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
    return STATUS_INVALID_PARAMETER;
  count = args->DmaBufferPrivateDataSize / sizeof(ADMISSION_PAGING_RECORD);
  if (count == 0u || count > ADMISSION_MAX_PAGING_RECORDS ||
      count > MAXULONG / sizeof(ADMISSION_PAGING_MARKER) ||
      args->DmaBufferSize != count * sizeof(ADMISSION_PAGING_MARKER))
    return STATUS_INVALID_PARAMETER;
  records = (const ADMISSION_PAGING_RECORD *)args->pDmaBufferPrivateData;
  if (!AdmissionPagingRecordsValid(records, count,
                                   ADMISSION_MAX_PAGING_RECORDS,
                                   args->DmaBufferSize))
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&physical, sizeof(physical));
  physical.hContext = args->hContext;
  physical.SubmissionFenceId = args->SubmissionFenceId;
  physical.NodeOrdinal = args->NodeOrdinal;
  physical.EngineOrdinal = args->EngineOrdinal;
  physical.Flags.Paging = 1u;
  return AdmissionCpuQueueSubmit(adapter, &physical,
      ADMISSION_CPU_PACKET_PAGING, records,
      args->DmaBufferPrivateDataSize);
}

static int AdmissionG4GraphAccess(void *opaque, unsigned long long va,
                                  unsigned int bytes, int write) {
  APPLE_AGX_GPUVA_G3_GRAPH *graph = (APPLE_AGX_GPUVA_G3_GRAPH *)opaque;
  return AppleAgxGpuvaG3GraphContainsRangeAccess(
      graph, va, bytes, write != 0) ? 1 : 0;
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
  APPLE_AGX_G4_SUBMIT_VIEW view;
  APPLE_AGX_G4_PARSE_RESULT result;
  APPLE_AGX_G4_ATTACHMENT color;
  ADMISSION_SCANOUT_MEMORY_VIEW local;
  ADMISSION_RENDER_PACKET_DESCRIPTION packet;
  APPLE_AGX_EXP208_GDI_BINDING binding;
  KIRQL old_irql;
  BOOLEAN prepared = FALSE, queued = FALSE;
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
      !NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(adapter, &local)))
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&packet, sizeof(packet));
  RtlZeroMemory(&binding, sizeof(binding));
  ExAcquireFastMutex(&state->Lock);
  if (context->GpuvaG3RootIpa != process->Graph.RootIpa ||
      process->Graph.Uncertain) {
    ExReleaseFastMutex(&state->Lock);
    return STATUS_INVALID_PARAMETER;
  }
  result = AppleAgxG4ParseSubmit(
      args->pDmaBufferPrivateData, args->DmaBufferPrivateDataSize,
      args->DmaBufferUmdPrivateDataSize, args->DmaBufferVirtualAddress,
      args->DmaBufferSize, AdmissionG4GraphAccess, &process->Graph, &view);
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
    if (!AdmissionG4ResolveOutput(process, &color, &local, &packet))
      result = AppleAgxG4ParseUnmapped;
  } else if (result == AppleAgxG4ParseOk) {
    result = AppleAgxG4ParseUnsupported;
  }
  ExReleaseFastMutex(&state->Lock);
  if (result != AppleAgxG4ParseOk) return STATUS_INVALID_PARAMETER;
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
  if (!prepared) return STATUS_INVALID_PARAMETER;
  if (!AdmissionBackendImageBindG4Submission(&adapter->BackendImage,
          &packet, (PVOID)(ULONG_PTR)packet.DestinationCpuToken,
          &view, &binding)) goto Rollback;
  context->GpuvaG3DmaBufferVa = args->DmaBufferVirtualAddress;
  context->GpuvaG3DmaBufferBytes = args->DmaBufferSize;
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
  return STATUS_INVALID_PARAMETER;
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
    return STATUS_INVALID_PARAMETER;
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter)
    return STATUS_INVALID_PARAMETER;
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
    return STATUS_INVALID_PARAMETER;
  packet = adapter->RenderPacket.Description;
  if (packet.ContextToken != (ULONGLONG)(ULONG_PTR)context ||
      packet.Fence != Args->SubmissionFenceId ||
      packet.DmaStart >= packet.DmaEnd ||
      packet.DmaEnd > Args->DmaBufferSize ||
      packet.PrivateDataToken !=
          (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData ||
      packet.PrivateDataEnd != shadow.BytesUsed)
    return STATUS_INVALID_PARAMETER;
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
    return STATUS_INVALID_PARAMETER;
  }
  return STATUS_SUCCESS;
}

#endif
