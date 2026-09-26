#include "gpuva_g3_private.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)

enum {
  AdmissionG3PagingFailureTableAddress = 1u,
  AdmissionG3PagingFailureTableGraph = 2u,
  AdmissionG3PagingFailureParentFlags = 3u,
  AdmissionG3PagingFailureChildAddress = 4u,
  AdmissionG3PagingFailureChildGraph = 5u,
  AdmissionG3PagingFailureParentLink = 6u,
  AdmissionG3PagingFailureLeafGraph = 7u,
  AdmissionG3PagingTableInitialized = 8u,
  AdmissionG3PagingFailureTableMirror = 9u,
  AdmissionG3PagingFailureSubpage = 10u
};

static NTSTATUS AdmissionG3RejectPaging(
    ADMISSION_G3_PAGING_FAILURE *failure, ULONG branch,
    UINT index, const DXGK_PTE *pte, ULONGLONG child_ipa, NTSTATUS status) {
  failure->Branch = branch;
  failure->Index = index;
  failure->Status = (ULONG)status;
  failure->ChildIpa = child_ipa;
  if (pte != NULL) {
    failure->PteFlags = pte->Flags;
    failure->PageAddress = pte->PageTableAddress;
    failure->PageTablePageSize = (ULONG)pte->PageTablePageSize;
  }
  return status;
}

static NTSTATUS AdmissionG3UpdateParent(
    ADMISSION_G3_PROCESS *process, ULONGLONG table_ipa,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update,
    ADMISSION_CONTEXT *adapter, ADMISSION_G3_PAGING_FAILURE *failure) {
  UINT index, end, child_level;
  DXGK_PAGETABLEUPDATEADDRESS address;
  ULONGLONG child_ipa = 0ULL, child_offset;
  if (update->PageTableLevel != 1u && update->PageTableLevel != 2u)
    return STATUS_INVALID_PARAMETER;
  child_level = 2u - update->PageTableLevel + 1u;
  end = update->StartIndex + update->NumPageTableEntries;
  for (index = update->StartIndex; index < end; ++index) {
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(index - update->StartIndex,
                                     update->Flags.Repeat)];
    if (!pte->Valid) continue;
    /* The parent PTE chooses its child leaf type independently of the
     * current table's UpdatePageTable Use64KBPages flag. */
    if ((pte->Flags & ~(1ULL << 17)) !=
            (1ULL | ((ULONGLONG)ADMISSION_MEMORY_LOCAL_SEGMENT << 5)) ||
        (update->PageTableLevel == 2u && pte->PageTablePageSize != 0u))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureParentFlags, index, pte, 0ULL,
          STATUS_NOT_SUPPORTED);
    if (!AppleAgxGpuvaG3PteAddressBytes(pte->PageTableAddress,
                                         &child_offset))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureChildAddress, index, pte, 0ULL,
          STATUS_INVALID_ADDRESS);
    RtlZeroMemory(&address, sizeof(address));
    address.GpuPhysical.SegmentId = (UINT)pte->Segment;
    address.GpuPhysical.SegmentOffset = child_offset;
    if (!NT_SUCCESS(AdmissionGpuvaG3ResolveTable(adapter, &address,
            DXGK_PAGETABLEUPDATE_GPU_PHYSICAL, &child_ipa)))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureChildAddress, index, pte, 0ULL,
          STATUS_INVALID_ADDRESS);
  }
  for (index = update->StartIndex; index < end; ++index) {
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(index - update->StartIndex,
                                     update->Flags.Repeat)];
    if (!pte->Valid) {
      if (!AppleAgxGpuvaG3GraphUpdateParent(&process->Graph, table_ipa,
              index, 0ULL))
        return AdmissionG3RejectPaging(failure,
            AdmissionG3PagingFailureParentLink, index, pte, 0ULL,
            STATUS_INVALID_PARAMETER);
      continue;
    }
    if (!AppleAgxGpuvaG3PteAddressBytes(pte->PageTableAddress,
                                         &child_offset))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureChildAddress, index, pte, 0ULL,
          STATUS_INVALID_ADDRESS);
    RtlZeroMemory(&address, sizeof(address));
    address.GpuPhysical.SegmentId = (UINT)pte->Segment;
    address.GpuPhysical.SegmentOffset = child_offset;
    if (!NT_SUCCESS(AdmissionGpuvaG3ResolveTable(adapter, &address,
            DXGK_PAGETABLEUPDATE_GPU_PHYSICAL, &child_ipa)))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureChildAddress, index, pte, 0ULL,
          STATUS_INVALID_ADDRESS);
    if (!NT_SUCCESS(AdmissionGpuvaG3BrokerTable(
            process, child_ipa, TRUE, &child_ipa)))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureChildGraph, index, pte, 0ULL,
          STATUS_INSUFFICIENT_RESOURCES);
    if (!AppleAgxGpuvaG3GraphRegisterTable(&process->Graph,
            child_ipa, child_level))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureChildGraph, index, pte, child_ipa,
          STATUS_INVALID_ADDRESS);
    if (!AppleAgxGpuvaG3GraphUpdateParent(&process->Graph, table_ipa,
            index, child_ipa))
      return AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureParentLink, index, pte, child_ipa,
          STATUS_INVALID_ADDRESS);
  }
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionG3UpdateLeaf(
    ADMISSION_G3_PROCESS *process, ULONGLONG table_ipa,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update,
    ADMISSION_CONTEXT *adapter, ADMISSION_G3_PAGING_FAILURE *failure) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *logical = NULL;
  APPLE_AGX_GPUVA_G3_NATIVE_LEAF *leaves = NULL;
  APPLE_AGX_GPUVA_G3_RESULT plan = AppleAgxGpuvaG3Invalid;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  unsigned int count = 0u, index;
  NTSTATUS status = STATUS_INVALID_PARAMETER;
  if (update->NumPageTableEntries == 0u ||
      update->NumPageTableEntries >
          (update->Flags.Use64KBPages ? 512u : 8192u))
    return STATUS_INVALID_PARAMETER;
  if (!update->Flags.Use64KBPages &&
      ((update->FirstPteVirtualAddress & 0xfffULL) != 0ULL ||
       update->FirstPteVirtualAddress >= (1ULL << 39) ||
       (ULONGLONG)update->NumPageTableEntries * 0x1000ULL >
           (1ULL << 39) - update->FirstPteVirtualAddress))
    return STATUS_INVALID_PARAMETER;
  if (!NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(adapter, &view)))
    return STATUS_INVALID_DEVICE_STATE;
  logical = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      8192u * sizeof(*logical), ADMISSION_POOL_TAG);
  leaves = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      2048u * sizeof(*leaves),
      ADMISSION_POOL_TAG);
  if (logical == NULL || leaves == NULL) {
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto Done;
  }
  RtlZeroMemory(logical, 8192u * sizeof(*logical));
  for (index = 0u; index < update->NumPageTableEntries && index < 8192u;
       ++index) {
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(index, update->Flags.Repeat)];
    ULONGLONG ipa, page_offset;
    if (!pte->Valid) {
      continue;
    }
    if (pte->Zero || pte->CacheCoherent || pte->NoExecute || pte->LargePage ||
        pte->PhysicalAdapterIndex || pte->PageTablePageSize ||
        pte->SystemReserved0 || pte->Reserved ||
        !AppleAgxGpuvaG3PteAddressBytes(pte->PageAddress,
                                         &page_offset) ||
        AppleAgxGpuvaG3ResolvePageAddress((UINT)pte->Segment,
            page_offset, ADMISSION_MEMORY_LOCAL_SEGMENT,
            view.GuestIpaAddress, view.Bytes, &ipa) != AppleAgxGpuvaG3Ok)
      goto Done;
    if (update->Flags.Use64KBPages &&
        (view.Bytes < 0x10000u ||
         (UINT)pte->Segment != ADMISSION_MEMORY_LOCAL_SEGMENT ||
         (page_offset & 0xffffu) ||
         page_offset > view.Bytes - 0x10000u))
      goto Done;
    logical[index].GuestIpa = ipa;
    logical[index].SegmentId = (unsigned int)pte->Segment;
    logical[index].Flags = APPLE_AGX_GPUVA_G3_VALID |
        (pte->ReadOnly ? 0u : APPLE_AGX_GPUVA_G3_WRITE);
  }
  if (!update->Flags.Use64KBPages) {
    UINT first_group = update->StartIndex / 4u;
    UINT last_group = (update->StartIndex + update->NumPageTableEntries - 1u) / 4u;
    for (shadow = process->TableShadows; shadow != NULL; shadow = shadow->Next)
      if (shadow->BrokerIpa == table_ipa) break;
    if (shadow == NULL) goto Done;
    if (shadow->LogicalPtes == NULL) {
      shadow->LogicalPtes = ExAllocatePool2(POOL_FLAG_NON_PAGED,
          8192u * sizeof(*shadow->LogicalPtes), ADMISSION_POOL_TAG);
      if (shadow->LogicalPtes == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
      }
      RtlZeroMemory(shadow->LogicalPtes,
                    8192u * sizeof(*shadow->LogicalPtes));
    }
    for (index = 0u; index < update->NumPageTableEntries; ++index)
      shadow->LogicalPtes[update->StartIndex + index] = logical[index];
    for (index = first_group; index <= last_group; ++index) {
      const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *group =
          &shadow->LogicalPtes[index * 4u];
      ULONGLONG ipa = group[0].GuestIpa;
      UINT flags = group[0].Flags, segment = group[0].SegmentId;
      UINT sub;
      BOOLEAN complete = (flags & APPLE_AGX_GPUVA_G3_VALID) != 0u &&
          ipa != 0ULL && (ipa & 0x3fffULL) == 0ULL &&
          ipa <= MAXULONGLONG - 0x3000ULL;
      for (sub = 1u; complete && sub < 4u; ++sub)
        if (group[sub].Flags != flags ||
            group[sub].SegmentId != segment ||
            group[sub].GuestIpa != ipa + (ULONGLONG)sub * 0x1000ULL)
          complete = FALSE;
      /* Only the KMD-owned local reserve can be granted to this graph.
       * VidMm's aperture/system pages have no process backing grant. */
      if (complete && (segment != ADMISSION_MEMORY_LOCAL_SEGMENT ||
          ipa < view.GuestIpaAddress || view.Bytes < 0x4000ULL ||
          ipa - view.GuestIpaAddress > view.Bytes - 0x4000ULL))
        complete = FALSE;
      if (!complete && (flags & APPLE_AGX_GPUVA_G3_VALID) != 0u &&
          segment < 32u &&
          process->State->UnpublishedGroups[segment] != MAXULONGLONG)
        ++process->State->UnpublishedGroups[segment];
      if (!AppleAgxGpuvaG3GraphUpdateLeaf(&process->Graph, table_ipa,
              index, complete ? ipa : 0ULL,
              complete && (flags & APPLE_AGX_GPUVA_G3_WRITE) != 0u)) {
        UINT source_index = index * 4u < update->StartIndex ? 0u :
            index * 4u - update->StartIndex;
        const DXGK_PTE *pte = &update->pPageTableEntries[
            AppleAgxGpuvaG3PteInputIndex(source_index,
                                         update->Flags.Repeat)];
        status = AdmissionG3RejectPaging(failure,
            AdmissionG3PagingFailureLeafGraph, index * 4u, pte,
            complete ? ipa : 0ULL, STATUS_DEVICE_HARDWARE_ERROR);
        goto Done;
      }
    }
    status = STATUS_SUCCESS;
    goto Done;
  }
  if (update->Flags.Use64KBPages)
    plan = AppleAgxGpuvaG3Plan64KSpan(logical, update->StartIndex,
        update->NumPageTableEntries, update->FirstPteVirtualAddress,
        ADMISSION_MEMORY_LOCAL_SEGMENT, leaves, 2048u, &count);
  if (plan != AppleAgxGpuvaG3Ok && plan != AppleAgxGpuvaG3Unmap)
    goto Done;
  for (index = 0u; index < count && index < 2048u; ++index) {
    if (!AppleAgxGpuvaG3GraphUpdateLeaf(&process->Graph, table_ipa,
            (update->Flags.Use64KBPages ? update->StartIndex * 4u :
                update->StartIndex / 4u) + index, leaves[index].GuestIpa,
            leaves[index].WritableMask != 0u)) {
      UINT source_index = update->Flags.Use64KBPages ? index / 4u :
                          index * 4u;
      const DXGK_PTE *pte = &update->pPageTableEntries[
          AppleAgxGpuvaG3PteInputIndex(source_index,
                                       update->Flags.Repeat)];
      status = AdmissionG3RejectPaging(failure,
          AdmissionG3PagingFailureLeafGraph,
          update->StartIndex + source_index, pte, leaves[index].GuestIpa,
          STATUS_DEVICE_HARDWARE_ERROR);
      goto Done;
    }
  }
  status = STATUS_SUCCESS;
Done:
  if (leaves != NULL) ExFreePoolWithTag(leaves, ADMISSION_POOL_TAG);
  if (logical != NULL) ExFreePoolWithTag(logical, ADMISSION_POOL_TAG);
  return status;
}

static APPLE_AGX_GPUVA_G3_NODE *AdmissionG3FindPagingEdge(
    APPLE_AGX_GPUVA_G3_NODE *nodes, ULONGLONG table_ipa, UINT index) {
  for (; nodes != NULL; nodes = nodes->Next)
    if (nodes->Ipa == table_ipa && nodes->Index == index)
      return nodes;
  return NULL;
}

static NTSTATUS AdmissionG3ResolveLogicalVa(
    ADMISSION_G3_PROCESS *process, ULONGLONG root_ipa, ULONGLONG va,
    BOOLEAN writable,
    ULONGLONG *ipa, UINT *segment) {
  APPLE_AGX_GPUVA_G3_NODE *edge;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte;
  ULONGLONG table_ipa;
  if (process == NULL || ipa == NULL || segment == NULL ||
      !process->Graph.Created || process->Graph.Uncertain ||
      root_ipa == 0ULL || va >= (1ULL << 39))
    return STATUS_INVALID_ADDRESS;
  edge = AdmissionG3FindPagingEdge(process->Graph.Parents,
      root_ipa, (UINT)((va >> 36) & 7u));
  if (edge == NULL) return STATUS_INVALID_ADDRESS;
  edge = AdmissionG3FindPagingEdge(process->Graph.Parents,
      edge->AuxIpa, (UINT)((va >> 25) & 2047u));
  if (edge == NULL) return STATUS_INVALID_ADDRESS;
  table_ipa = edge->AuxIpa;
  for (shadow = process->TableShadows; shadow != NULL; shadow = shadow->Next)
    if (shadow->BrokerIpa == table_ipa) break;
  if (shadow == NULL || shadow->LogicalPtes == NULL)
    return STATUS_INVALID_ADDRESS;
  pte = &shadow->LogicalPtes[(UINT)((va >> 12) & 8191u)];
  if ((pte->Flags & APPLE_AGX_GPUVA_G3_VALID) == 0u ||
      (writable && (pte->Flags & APPLE_AGX_GPUVA_G3_WRITE) == 0u) ||
      pte->GuestIpa == 0ULL || pte->GuestIpa > MAXULONGLONG - (va & 0xfffu))
    return STATUS_INVALID_ADDRESS;
  *ipa = pte->GuestIpa + (va & 0xfffu);
  *segment = pte->SegmentId;
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionG3SnapshotAperture(
    ADMISSION_CONTEXT *adapter, ULONGLONG *ipa, UINT *segment) {
  ULONGLONG physical;
  if (adapter == NULL || ipa == NULL || segment == NULL)
    return STATUS_INVALID_PARAMETER;
  if (*segment != ADMISSION_MEMORY_APERTURE_SEGMENT)
    return STATUS_SUCCESS;
  if (AppleAgxSoftwareApertureResolve(
          &adapter->Memory.Aperture, *ipa, &physical) !=
      AppleAgxSoftwareApertureOk)
    return STATUS_INVALID_ADDRESS;
  *ipa = physical;
  *segment = 0u;
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionG3EncodeVirtualPaging(
    ADMISSION_CONTEXT *adapter, DXGKARG_BUILDPAGINGBUFFER *args) {
  ADMISSION_G3_STATE *state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  ADMISSION_RENDER_CONTEXT *context =
      (ADMISSION_RENDER_CONTEXT *)args->hSystemContext;
  ADMISSION_G3_PROCESS *process;
  ADMISSION_PAGING_RECORD record;
  ADMISSION_PAGING_MARKER marker;
  ULONGLONG source_va = 0ULL, destination_va, total, offset;
  UINT remaining, slots, produced = 0u;
  NTSTATUS status;
  if (state == NULL || context == NULL ||
      context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter ||
      context->GpuvaG3Process == NULL || context->GpuvaG3Poisoned)
    return STATUS_INVALID_PARAMETER;
  process = context->GpuvaG3Process;
  if (args->Operation == DXGK_OPERATION_VIRTUAL_FILL) {
    destination_va = args->FillVirtual.DestinationVirtualAddress;
    total = args->FillVirtual.FillSizeInBytes;
  } else if (args->Operation == DXGK_OPERATION_SIGNAL_MONITORED_FENCE) {
    destination_va = args->SignalMonitoredFence.MonitoredFenceGpuVa;
    total = sizeof(args->SignalMonitoredFence.MonitoredFenceValue);
  } else {
    source_va = args->TransferVirtual.SourceVirtualAddress;
    destination_va = args->TransferVirtual.DestinationVirtualAddress;
    total = args->TransferVirtual.TransferSizeInBytes;
    if (args->TransferVirtual.Flags.Flags != 0u ||
        args->TransferVirtual.TransferDirection >
            DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL)
      return STATUS_INVALID_PARAMETER;
  }
  if (total == 0ULL || total > MAXULONG ||
      destination_va >= (1ULL << 39) ||
      total > (1ULL << 39) - destination_va ||
      (args->Operation == DXGK_OPERATION_VIRTUAL_TRANSFER &&
       (source_va >= (1ULL << 39) ||
        total > (1ULL << 39) - source_va)) ||
      args->MultipassOffset > total)
    return STATUS_INVALID_PARAMETER;
  offset = args->MultipassOffset;
  remaining = args->DmaSize / sizeof(marker);
  if (args->DmaBufferPrivateDataSize / sizeof(record) < remaining)
    remaining = args->DmaBufferPrivateDataSize / sizeof(record);
  slots = args->DmaBufferWriteOffset / sizeof(marker);
  if (slots >= ADMISSION_MAX_PAGING_RECORDS) remaining = 0u;
  else if (remaining > ADMISSION_MAX_PAGING_RECORDS - slots)
    remaining = ADMISSION_MAX_PAGING_RECORDS - slots;
  if (remaining == 0u || args->pDmaBuffer == NULL ||
      args->pDmaBufferPrivateData == NULL)
    return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
  RtlZeroMemory(&marker, sizeof(marker));
  marker.Magic = ADMISSION_PAGING_MAGIC;
  marker.Version = ADMISSION_PAGING_VERSION;
  marker.RecordBytes = sizeof(record);
  while (offset < total && produced < remaining) {
    ULONGLONG dst = destination_va + offset;
    ULONGLONG src = source_va + offset;
    UINT bytes = (UINT)(total - offset);
    UINT boundary = 0x1000u - (UINT)(dst & 0xfffu);
    if (bytes > boundary) bytes = boundary;
    if (args->Operation == DXGK_OPERATION_VIRTUAL_TRANSFER) {
      boundary = 0x1000u - (UINT)(src & 0xfffu);
      if (bytes > boundary) bytes = boundary;
    }
    RtlZeroMemory(&record, sizeof(record));
    record.Header = marker;
    record.Kind = args->Operation == DXGK_OPERATION_VIRTUAL_FILL ?
        AdmissionPagingVirtualFill :
        args->Operation == DXGK_OPERATION_SIGNAL_MONITORED_FENCE ?
        AdmissionPagingMonitoredFence : AdmissionPagingVirtualTransfer;
    record.Bytes = bytes;
    record.PatternOffset = (UINT)(offset & 3u);
    if (record.Kind == AdmissionPagingVirtualFill)
      record.FillPattern = args->FillVirtual.FillPattern;
    if (record.Kind == AdmissionPagingMonitoredFence) {
      record.PatternOffset = (UINT)offset;
      record.FenceValue = args->SignalMonitoredFence.MonitoredFenceValue;
    }
    ExAcquireFastMutex(&state->Lock);
    status = AdmissionG3ResolveLogicalVa(process, context->GpuvaG3RootIpa,
        dst, TRUE,
        &record.DestinationIpa, &record.DestinationSegment);
    if (NT_SUCCESS(status) &&
        args->Operation == DXGK_OPERATION_VIRTUAL_TRANSFER)
      status = AdmissionG3ResolveLogicalVa(process,
          context->GpuvaG3RootIpa, src, FALSE,
          &record.SourceIpa, &record.SourceSegment);
    ExReleaseFastMutex(&state->Lock);
    if (!NT_SUCCESS(status)) return status;
    if (record.Kind == AdmissionPagingVirtualTransfer) {
      BOOLEAN source_local =
          record.SourceSegment == ADMISSION_MEMORY_LOCAL_SEGMENT;
      BOOLEAN destination_local =
          record.DestinationSegment == ADMISSION_MEMORY_LOCAL_SEGMENT;
      if ((args->TransferVirtual.TransferDirection ==
               DXGK_MEMORY_TRANSFER_LOCAL_TO_SYSTEM &&
           (!source_local || destination_local)) ||
          (args->TransferVirtual.TransferDirection ==
               DXGK_MEMORY_TRANSFER_SYSTEM_TO_LOCAL &&
           (source_local || !destination_local)) ||
          (args->TransferVirtual.TransferDirection ==
               DXGK_MEMORY_TRANSFER_LOCAL_TO_LOCAL &&
           (!source_local || !destination_local)))
        return STATUS_INVALID_PARAMETER;
    }
    status = AdmissionG3SnapshotAperture(adapter,
        &record.DestinationIpa, &record.DestinationSegment);
    if (!NT_SUCCESS(status)) return status;
    if (record.Kind == AdmissionPagingVirtualTransfer) {
      status = AdmissionG3SnapshotAperture(adapter,
          &record.SourceIpa, &record.SourceSegment);
      if (!NT_SUCCESS(status)) return status;
    }
    RtlCopyMemory(args->pDmaBuffer, &marker, sizeof(marker));
    RtlCopyMemory(args->pDmaBufferPrivateData, &record, sizeof(record));
    args->pDmaBuffer = (PUCHAR)args->pDmaBuffer + sizeof(marker);
    args->DmaSize -= sizeof(marker);
    args->pDmaBufferPrivateData =
        (PUCHAR)args->pDmaBufferPrivateData + sizeof(record);
    args->DmaBufferPrivateDataSize -= sizeof(record);
    offset += bytes;
    ++produced;
  }
  args->MultipassOffset = (UINT)offset;
  return offset == total ? STATUS_SUCCESS :
      STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
}

static NTSTATUS AdmissionG3MapPagingIpa(
    ADMISSION_CONTEXT *adapter, const ADMISSION_SCANOUT_MEMORY_VIEW *view,
    ULONGLONG ipa, UINT segment, UINT bytes, PUCHAR *address,
    PVOID *system_mapping) {
  PHYSICAL_ADDRESS physical;
  ULONGLONG page = ipa & ~0xfffULL;
  if (address == NULL || system_mapping == NULL || bytes == 0u ||
      bytes > 0x1000u - (UINT)(ipa & 0xfffu))
    return STATUS_INVALID_PARAMETER;
  *address = NULL;
  *system_mapping = NULL;
  if (segment == ADMISSION_MEMORY_LOCAL_SEGMENT) {
    if (view == NULL || view->CpuAddress == NULL ||
        ipa < view->GuestIpaAddress || bytes > view->Bytes ||
        ipa - view->GuestIpaAddress > view->Bytes - bytes)
      return STATUS_INVALID_ADDRESS;
    *address = (PUCHAR)view->CpuAddress +
        (SIZE_T)(ipa - view->GuestIpaAddress);
    return STATUS_SUCCESS;
  }
  if (segment != 0u && segment != ADMISSION_MEMORY_APERTURE_SEGMENT)
    return STATUS_INVALID_PARAMETER;
  if (page > 0x7fffffffffffffffULL) return STATUS_INVALID_ADDRESS;
  physical.QuadPart = (LONGLONG)page;
  *system_mapping = MmMapIoSpace(physical, 0x1000u, MmCached);
  if (*system_mapping == NULL) return STATUS_INSUFFICIENT_RESOURCES;
  *address = (PUCHAR)*system_mapping + (SIZE_T)(ipa & 0xfffu);
  UNREFERENCED_PARAMETER(adapter);
  return STATUS_SUCCESS;
}

NTSTATUS AdmissionG3ExecuteVirtualPaging(
    ADMISSION_CONTEXT *adapter, const ADMISSION_PAGING_RECORD *record) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  PUCHAR destination = NULL, source = NULL;
  PVOID destination_mapping = NULL, source_mapping = NULL;
  NTSTATUS status;
  UINT index;
  if (adapter == NULL || record == NULL ||
      (record->Kind != AdmissionPagingVirtualFill &&
       record->Kind != AdmissionPagingVirtualTransfer &&
       record->Kind != AdmissionPagingMonitoredFence) ||
      record->Bytes == 0u || record->Bytes > 0x1000u ||
      KeGetCurrentIrql() != PASSIVE_LEVEL)
    return STATUS_INVALID_PARAMETER;
  status = AdmissionMemoryRuntimeScanoutView(adapter, &view);
  if (!NT_SUCCESS(status)) return status;
  status = AdmissionG3MapPagingIpa(adapter, &view,
      record->DestinationIpa, record->DestinationSegment, record->Bytes,
      &destination, &destination_mapping);
  if (!NT_SUCCESS(status)) return status;
  if (record->Kind == AdmissionPagingVirtualTransfer) {
    status = AdmissionG3MapPagingIpa(adapter, &view,
        record->SourceIpa, record->SourceSegment, record->Bytes,
        &source, &source_mapping);
    if (NT_SUCCESS(status)) RtlMoveMemory(destination, source, record->Bytes);
  } else if (record->Kind == AdmissionPagingVirtualFill) {
    const UCHAR *pattern = (const UCHAR *)&record->FillPattern;
    for (index = 0u; index < record->Bytes; ++index)
      destination[index] = pattern[(record->PatternOffset + index) & 3u];
  } else {
    const UCHAR *value = (const UCHAR *)&record->FenceValue;
    if (record->PatternOffset >= sizeof(record->FenceValue) ||
        record->Bytes > sizeof(record->FenceValue) - record->PatternOffset)
      status = STATUS_INVALID_PARAMETER;
    else
      for (index = 0u; index < record->Bytes; ++index)
        destination[index] = value[record->PatternOffset + index];
  }
  KeMemoryBarrier();
  if (source_mapping != NULL) MmUnmapIoSpace(source_mapping, 0x1000u);
  if (destination_mapping != NULL)
    MmUnmapIoSpace(destination_mapping, 0x1000u);
  return status;
}

NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(ADMISSION_CONTEXT *adapter,
                                            DXGKARG_BUILDPAGINGBUFFER *args) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update;
  DXGK_PAGETABLEUPDATEADDRESS address;
  ULONGLONG table_ipa = 0ULL, original_table_ipa, root_ipa;
  ADMISSION_G3_PAGING_FAILURE failure;
  ULONGLONG unpublished_before[32], unpublished_after[32];
  BOOLEAN unpublished_changed = FALSE;
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  APPLE_AGX_GPUVA_G3_NODE *table;
  ULONGLONG *table_words;
  UINT limit;
  UINT word_index;
  NTSTATUS status;
  if (adapter == NULL || args == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
  if (args->Operation == DXGK_OPERATION_VIRTUAL_FILL ||
      args->Operation == DXGK_OPERATION_VIRTUAL_TRANSFER ||
      args->Operation == DXGK_OPERATION_SIGNAL_MONITORED_FENCE)
    return AdmissionG3EncodeVirtualPaging(adapter, args);
  if (args->Operation == DXGK_OPERATION_FLUSH_TLB) {
    ADMISSION_G3_FLUSH_RECEIPT receipt;
    ADMISSION_G3_TABLE_SHADOW *shadow;
    BOOLEAN owned = FALSE;
    ULONGLONG start = args->FlushTlb.StartVirtualAddress;
    ULONGLONG end = args->FlushTlb.EndVirtualAddress;
    const ULONGLONG va_limit = 1ULL << 39;
    RtlZeroMemory(&receipt, sizeof(receipt));
    receipt.Version = 1u;
    receipt.Bytes = sizeof(receipt);
    receipt.Process = (ULONGLONG)(ULONG_PTR)args->FlushTlb.hProcess;
    receipt.RootSegment = args->FlushTlb.RootPageTableAddress.SegmentId;
    receipt.RootOffset = args->FlushTlb.RootPageTableAddress.SegmentOffset;
    receipt.InputStart = start;
    receipt.InputEnd = end;
    if (start == 0ULL && end == 0ULL) {
      receipt.Branch = 2u; /* Explicit full-ASID flush. */
    } else if (start >= end || start >= va_limit) {
      receipt.Branch = 3u; /* Anomalous range: full-ASID flush. */
      start = end = 0ULL;
    } else {
      receipt.Branch = 1u;
      start &= ~0x3fffULL;
      end = end >= va_limit ? va_limit : (end + 0x3fffULL) & ~0x3fffULL;
    }
    receipt.FlushStart = start;
    receipt.FlushEnd = end;
    ExAcquireFastMutex(&state->Lock);
    process = AdmissionGpuvaG3FindProcess(state, args->FlushTlb.hProcess);
    address.GpuPhysical = args->FlushTlb.RootPageTableAddress;
    status = AdmissionGpuvaG3ResolveTable(adapter, &address,
        DXGK_PAGETABLEUPDATE_GPU_PHYSICAL, &root_ipa);
    receipt.ResolveStatus = (ULONG)status;
    if (!NT_SUCCESS(status) && process != NULL &&
        address.GpuPhysical.SegmentId == ADMISSION_MEMORY_LOCAL_SEGMENT &&
        NT_SUCCESS(AdmissionMemoryRuntimeScanoutView(adapter, &view)) &&
        address.GpuPhysical.SegmentOffset <=
            MAXULONGLONG - view.GuestIpaAddress) {
      ULONGLONG candidate = view.GuestIpaAddress +
          address.GpuPhysical.SegmentOffset;
      if (candidate == process->BootstrapIpa) {
        root_ipa = candidate;
        status = STATUS_SUCCESS;
      } else {
        for (shadow = process->TableShadows; shadow != NULL;
             shadow = shadow->Next)
          if (candidate == shadow->BrokerIpa) {
            root_ipa = candidate;
            status = STATUS_SUCCESS;
            break;
          }
      }
    }
    if (NT_SUCCESS(status) && process != NULL) {
      receipt.ResolvedRootIpa = root_ipa;
      if (root_ipa == process->BootstrapIpa ||
          root_ipa == process->Graph.RootIpa)
        owned = TRUE;
      for (shadow = process->TableShadows; shadow != NULL;
           shadow = shadow->Next) {
        if (root_ipa == shadow->OriginalIpa) {
          root_ipa = shadow->BrokerIpa;
          owned = TRUE;
          break;
        }
        if (root_ipa == shadow->BrokerIpa) {
          owned = TRUE;
          break;
        }
      }
    }
    receipt.BrokerStatus = (ULONG)(owned ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER);
    if (process != NULL) receipt.GraphRootIpa = process->Graph.RootIpa;
    if (process == NULL || !NT_SUCCESS(status) || !owned) {
      receipt.Branch = 4u; /* Foreign process or root. */
      status = STATUS_INVALID_PARAMETER;
    } else if (!process->Graph.Created || process->Graph.Slot == 0u ||
               process->Poisoned) {
      receipt.Branch = 5u; /* No active translation slot. */
      status = STATUS_SUCCESS;
    } else if (root_ipa != process->Graph.RootIpa) {
      receipt.Branch = 6u; /* Owned root is not the active root. */
      status = STATUS_SUCCESS;
    } else if (!AppleAgxGpuvaG3GraphFlush(&process->Graph, start, end)) {
      status = STATUS_DEVICE_HARDWARE_ERROR;
    }
    ExReleaseFastMutex(&state->Lock);
    AdmissionRecordGpuvaG3Flush(adapter, &receipt);
    return status;
  }
  if (args->Operation != DXGK_OPERATION_UPDATE_PAGE_TABLE)
    return STATUS_NOT_SUPPORTED;
  update = &args->UpdatePageTable;
  RtlZeroMemory(&failure, sizeof(failure));
  failure.Version = 3u;
  failure.Bytes = sizeof(failure);
  failure.TableFirstNonzeroIndex = MAXULONG;
  failure.Level = update->PageTableLevel;
  failure.UpdateMode = (ULONG)update->UpdateMode;
  failure.Index = MAXULONG;
  failure.TableAddress = update->UpdateMode ==
      DXGK_PAGETABLEUPDATE_CPU_VIRTUAL ?
      (ULONGLONG)(ULONG_PTR)update->PageTableAddress.CpuVirtual :
      update->PageTableAddress.GpuPhysical.SegmentOffset;
  limit = update->PageTableLevel == 0u ?
              (update->Flags.Use64KBPages ? 512u : 8192u) :
          update->PageTableLevel == 1u ? 2048u :
          update->PageTableLevel == 2u ? 8u : 0u;
  if (limit == 0u || update->NumPageTableEntries == 0u ||
      update->StartIndex >= limit ||
      update->NumPageTableEntries > limit - update->StartIndex ||
      update->pPageTableEntries == NULL || update->pPageTableEntries64KB != NULL ||
      update->Reserved0 != 0u ||
      update->Flags.NativeFence ||
      update->Flags.Reserved ||
      (update->Flags.Use64KBPages &&
       (ADMISSION_GPUVA_G1B_PAGE_PROFILE != 64 ||
        update->PageTableLevel == 2u)) ||
      (update->UpdateMode != DXGK_PAGETABLEUPDATE_GPU_PHYSICAL &&
       update->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL))
    return STATUS_INVALID_PARAMETER;
  /* CPU_VIRTUAL updates complete now; supplied DMA buffers stay untouched. */
  status = AdmissionGpuvaG3ResolveTable(adapter, &update->PageTableAddress,
                                        update->UpdateMode, &table_ipa);
  if (!NT_SUCCESS(status)) {
    (void)AdmissionG3RejectPaging(&failure,
        AdmissionG3PagingFailureTableAddress, MAXULONG, NULL, 0ULL, status);
    AdmissionRecordGpuvaG3PagingFailure(adapter, &failure);
    return status;
  }
  failure.TableIpa = table_ipa;
  original_table_ipa = table_ipa;
  ExAcquireFastMutex(&state->Lock);
  RtlCopyMemory(unpublished_before, state->UnpublishedGroups,
                sizeof(unpublished_before));
  process = AdmissionGpuvaG3FindProcess(state, update->hProcess);
  if (process == NULL || process->Poisoned || process->Graph.Uncertain) {
    status = STATUS_INVALID_DEVICE_STATE;
  } else {
    status = AdmissionMemoryRuntimeScanoutView(adapter, &view);
    if (!NT_SUCCESS(status) || view.CpuAddress == NULL ||
        original_table_ipa < view.GuestIpaAddress ||
        view.Bytes < 0x4000ULL ||
        original_table_ipa - view.GuestIpaAddress > view.Bytes - 0x4000ULL) {
      status = AdmissionG3RejectPaging(&failure,
          AdmissionG3PagingFailureTableAddress, MAXULONG, NULL, 0ULL,
          STATUS_INVALID_ADDRESS);
      goto PagingDone;
    }
    table_words = (ULONGLONG *)((PUCHAR)view.CpuAddress +
        (SIZE_T)(original_table_ipa - view.GuestIpaAddress));
    status = AdmissionGpuvaG3BrokerTable(
        process, original_table_ipa, TRUE, &table_ipa);
    if (!NT_SUCCESS(status)) {
      status = AdmissionG3RejectPaging(&failure,
          AdmissionG3PagingFailureTableGraph, MAXULONG, NULL, 0ULL, status);
      goto PagingDone;
    }
    failure.BrokerTableIpa = table_ipa;
    /* GraphRegisterTable is idempotent for an existing page.  Clear only a
     * newly admitted page, before the broker sees its physical contents.  A
     * page VidMm reuses at another level (EXP846) is new at that level. */
    for (table = process->Graph.Tables; table != NULL; table = table->Next)
      if (table->Ipa == table_ipa &&
          table->Level == 2u - update->PageTableLevel) break;
    if (table == NULL) {
      for (word_index = 0u; word_index < 0x4000u / sizeof(*table_words);
           ++word_index) {
        if (table_words[word_index] != 0ULL) {
          failure.TableFirstNonzeroIndex = word_index;
          failure.TableFirstNonzeroWord = table_words[word_index];
          break;
        }
      }
      RtlZeroMemory(table_words, 0x4000u);
      KeMemoryBarrier();
    }
    /* 1 = broker registration attempted, 2 = refused, 3 = graph cache.
     * Exact broker subreason is intentionally not inferred from OWNERSHIP. */
    failure.TableAddBranch = table == NULL ? 1u : 3u;
    if (!AppleAgxGpuvaG3GraphRegisterTable(&process->Graph, table_ipa,
                                            2u - update->PageTableLevel)) {
      failure.TableAddBranch = 2u;
      status = AdmissionG3RejectPaging(&failure,
          AdmissionG3PagingFailureTableGraph, MAXULONG, NULL, 0ULL,
          STATUS_INVALID_ADDRESS);
    } else if (update->PageTableLevel == 0u) {
      status = AdmissionG3UpdateLeaf(process, table_ipa, update, adapter,
                                     &failure);
    } else {
      status = AdmissionG3UpdateParent(process, table_ipa, update, adapter,
                                       &failure);
    }
    if (NT_SUCCESS(status) &&
        !NT_SUCCESS(AdmissionGpuvaG3MirrorTable(
            process, original_table_ipa, table_words)))
      status = AdmissionG3RejectPaging(&failure,
          AdmissionG3PagingFailureTableMirror, MAXULONG, NULL, 0ULL,
          STATUS_DEVICE_HARDWARE_ERROR);
    if (failure.Branch == 0u &&
        failure.TableFirstNonzeroIndex != MAXULONG)
      failure.Branch = AdmissionG3PagingTableInitialized;
  }
PagingDone:
  RtlCopyMemory(unpublished_after, state->UnpublishedGroups,
                sizeof(unpublished_after));
  unpublished_changed = RtlCompareMemory(unpublished_before,
      unpublished_after, sizeof(unpublished_before)) !=
      sizeof(unpublished_before);
  if (process != NULL && failure.Branch != 0u) {
    failure.GraphLastStatus = process->Graph.LastStatus;
    failure.GraphUncertain = process->Graph.Uncertain;
  }
  if (process != NULL && !NT_SUCCESS(status) &&
      process->Graph.Uncertain) process->Poisoned = TRUE;
  ExReleaseFastMutex(&state->Lock);
  if (unpublished_changed)
    AdmissionRecordGpuvaG3UnpublishedGroups(adapter, unpublished_after);
  AdmissionRecordGpuvaG3PagingFailure(adapter, &failure);
  return status;
}

#endif
