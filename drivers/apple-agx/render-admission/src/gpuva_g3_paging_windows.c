#include "gpuva_g3_private.h"
#include "apple_agx_gpuva_g3_caps.h"

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

/* A removed edge retires its subtree only when no other parent retains it.
 * Tables populated before their first link are left alone. Local native leaves
 * keep their existing policy; only VidMm system residency is retired here. */
static BOOLEAN AdmissionG3RetireSystemSubtree(ADMISSION_G3_PROCESS *process,
    ULONGLONG table_ipa, ULONGLONG retired_parent, UINT depth) {
  APPLE_AGX_GPUVA_G3_NODE *edge, *leaf;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  if (!table_ipa || table_ipa == process->Graph.RootIpa) return TRUE;
  if (depth > 2u) return FALSE;
  for (edge = process->Graph.Parents; edge; edge = edge->Next)
    if (edge->AuxIpa == table_ipa && !edge->SystemRetired &&
        edge->Ipa != retired_parent) return TRUE;
  for (edge = process->Graph.Parents; edge; edge = edge->Next) {
    if (edge->Ipa != table_ipa) continue;
    edge->SystemRetired = 1u;
    if (!AdmissionG3RetireSystemSubtree(process, edge->AuxIpa, table_ipa, depth + 1u))
      return FALSE;
  }
  for (;;) {
    for (leaf = process->Graph.Leaves; leaf; leaf = leaf->Next)
      if (leaf->Ipa == table_ipa && leaf->Kind == AppleAgxGpuvaG3SystemBacking) break;
    if (!leaf) break;
    if (!AppleAgxGpuvaG3GraphUpdateLeaf(&process->Graph,
            table_ipa, leaf->Index, 0ULL, false)) return FALSE;
  }
  for (shadow = process->TableShadows; shadow; shadow = shadow->Next) {
    if (shadow->BrokerIpa != table_ipa) continue;
    for (UINT i = 0u; i < 8192u; ++i) {
      if (shadow->ResidentPtes && shadow->ResidentPtes[i].Flags &&
          shadow->ResidentPtes[i].SegmentId == 0u) {
        AppleAgxGpuvaG3MappingRelease(&process->Graph, shadow->ResidentPtes[i].GuestIpa);
        RtlZeroMemory(&shadow->ResidentPtes[i], sizeof(*shadow->ResidentPtes));
      }
      if (shadow->LogicalPtes && shadow->LogicalPtes[i].SegmentId == 0u)
        RtlZeroMemory(&shadow->LogicalPtes[i], sizeof(*shadow->LogicalPtes));
    }
  }
  return !process->Graph.Uncertain;
}

static void AdmissionG3ActivateSystemSubtree(ADMISSION_G3_PROCESS *process,
    ULONGLONG table_ipa, UINT depth) {
  APPLE_AGX_GPUVA_G3_NODE *edge;
  if (depth > 2u) return;
  for (edge = process->Graph.Parents; edge; edge = edge->Next) {
    if (edge->Ipa != table_ipa) continue;
    edge->SystemRetired = 0u;
    AdmissionG3ActivateSystemSubtree(process, edge->AuxIpa, depth + 1u);
  }
}

/* GraphRegisterTable has already removed the old native edges. */
static void AdmissionG3ResetTableShadow(ADMISSION_G3_PROCESS *process,
    ULONGLONG table_ipa) {
  ADMISSION_G3_TABLE_SHADOW *s;
  for (s = process->TableShadows; s; s = s->Next) {
    if (s->BrokerIpa != table_ipa) continue;
    if (s->ResidentPtes) {
      for (UINT i = 0u; i < 8192u; ++i)
        if (s->ResidentPtes[i].Flags && s->ResidentPtes[i].SegmentId == 0u)
          AppleAgxGpuvaG3MappingRelease(&process->Graph, s->ResidentPtes[i].GuestIpa);
      RtlZeroMemory(s->ResidentPtes, 8192u * sizeof(*s->ResidentPtes));
    }
    if (s->LogicalPtes) RtlZeroMemory(s->LogicalPtes, 8192u * sizeof(*s->LogicalPtes));
  }
}

/* Level reuse removes parent edges inside the graph as well. Remember those
 * children until after broker retirement, then release only unaliased system
 * descendants. A failed/uncertain broker operation never drops their records. */
static BOOLEAN AdmissionG3RegisterTable(ADMISSION_G3_PROCESS *process,
    ULONGLONG table_ipa, UINT level) {
  APPLE_AGX_GPUVA_G3_NODE *table, *edge;
  ULONGLONG *children = NULL;
  UINT count = 0u, i = 0u;
  BOOLEAN reused, ok;
  for (table = process->Graph.Tables; table; table = table->Next)
    if (table->Ipa == table_ipa) break;
  reused = table && table->Level != level;
  if (reused) {
    for (edge = process->Graph.Parents; edge; edge = edge->Next)
      if (edge->Ipa == table_ipa) ++count;
    if (count) {
      children = ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(*children),
                                ADMISSION_POOL_TAG);
      if (!children) return FALSE;
      for (edge = process->Graph.Parents; edge; edge = edge->Next)
        if (edge->Ipa == table_ipa) children[i++] = edge->AuxIpa;
    }
  }
  ok = AppleAgxGpuvaG3GraphRegisterTable(&process->Graph, table_ipa, level);
  if (ok && reused) {
    AdmissionG3ResetTableShadow(process, table_ipa);
    for (i = 0u; i < count; ++i)
      if (!AdmissionG3RetireSystemSubtree(process, children[i], 0ULL, 0u)) {
        process->Graph.Uncertain = 1u;
        ok = FALSE;
        break;
      }
  }
  if (children) ExFreePoolWithTag(children, ADMISSION_POOL_TAG);
  return ok;
}

/* InitialUpdate is VidMm's new-residency boundary. An empty former root
 * can be reused at another level before SetRootPageTable names the next root
 * (EXP854B). Park the broker on our private bootstrap root first; neither
 * GraphRegisterTable nor the broker may revoke a current root. */
static BOOLEAN AdmissionG3PrepareTableReuse(ADMISSION_G3_PROCESS *process,
    ULONGLONG table_ipa, UINT level, BOOLEAN initial_update) {
  APPLE_AGX_GPUVA_G3_GRAPH *graph = &process->Graph;
  APPLE_AGX_GPUVA_G3_NODE *entry;
  BOOLEAN bootstrap_found = FALSE;
  if (table_ipa != graph->RootIpa || level == 0u) return TRUE;
  if (!initial_update || table_ipa == process->BootstrapIpa ||
      graph->JobInFlight || graph->LeaseToken || graph->Slot ||
      graph->Uncertain) return FALSE;
  for (entry = graph->Tables; entry; entry = entry->Next)
    if (entry->Ipa == process->BootstrapIpa && entry->Level == 0u)
      bootstrap_found = TRUE;
  if (!bootstrap_found) return FALSE;
  if (process->PrivateLeafIpa) {
    ULONGLONG roots[2] = { table_ipa, process->BootstrapIpa };
    ULONGLONG middles[2] = { 0ULL, 0ULL };
    UINT i, rejected;
    /* Inspect BOTH roots before detaching either. Refusal must preserve the
     * private mapping and MappingGeneration, including an ordinary parking
     * root whose reserved edge is absent. */
    for (i = 0u; i < 2u; ++i) {
      if (!AppleAgxGpuvaG3GraphCanDetachPrivateRoot(graph, process->PrivateVa,
              roots[i], process->PrivateLeafIpa)) return FALSE;
      for (entry = graph->Parents; entry; entry = entry->Next)
        if (entry->Ipa == roots[i] &&
            entry->Index == (UINT)(process->PrivateVa >> 36))
          middles[i] = entry->AuxIpa;
    }
    for (i = 0u; i < 2u; ++i)
      if (!AppleAgxGpuvaG3GraphDetachPrivateRoot(graph, process->PrivateVa,
              roots[i], process->PrivateLeafIpa)) break;
    if (i == 2u && AppleAgxGpuvaG3GraphBindRoot(graph, process->BootstrapIpa)) {
      if (AppleAgxGpuvaG3GraphAttachPrivate(graph, process->PrivateVa,
              process->PrivateMiddleIpa, process->PrivateLeafIpa)) return TRUE;
      /* The root was relocated; failure to restore private reachability is
       * an incomplete transaction and must not permit a clean retry. */
      graph->Uncertain = 1u;
      return FALSE;
    }
    rejected = graph->LastStatus;
    for (i = 0u; i < 2u && !graph->Uncertain; ++i)
      if (middles[i] && !AppleAgxGpuvaG3GraphUpdateParent(graph, roots[i],
              (UINT)(process->PrivateVa >> 36), middles[i]))
        graph->Uncertain = 1u;
    graph->LastStatus = rejected;
    return FALSE;
  }
  for (entry = graph->Parents; entry; entry = entry->Next)
    if (entry->Ipa == table_ipa || entry->Ipa == process->BootstrapIpa)
      return FALSE;
  /* BindRoot advances MappingGeneration; queued former-root jobs stay stale. */
  return AppleAgxGpuvaG3GraphBindRoot(graph, process->BootstrapIpa);
}

static NTSTATUS AdmissionG3UpdateParent(
    ADMISSION_G3_PROCESS *process, ULONGLONG table_ipa,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update,
    ADMISSION_CONTEXT *adapter, ADMISSION_G3_PAGING_FAILURE *failure) {
  UINT index, end, child_level, applied = 0u;
  struct { ULONGLONG Ipa; UINT Retired; } *old_children = NULL;
  NTSTATUS result = STATUS_SUCCESS;
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
  if (process->Graph.MappingGeneration > MAXULONGLONG -
      (2ULL * update->NumPageTableEntries + 1ULL)) return STATUS_INTEGER_OVERFLOW;
  old_children = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      update->NumPageTableEntries * sizeof(*old_children), ADMISSION_POOL_TAG);
  if (!old_children) return STATUS_INSUFFICIENT_RESOURCES;
  RtlZeroMemory(old_children, update->NumPageTableEntries * sizeof(*old_children));
  for (index = update->StartIndex; index < end; ++index) {
    APPLE_AGX_GPUVA_G3_NODE *edge;
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(index - update->StartIndex, update->Flags.Repeat)];
    for (edge = process->Graph.Parents; edge; edge = edge->Next)
      if (edge->Ipa == table_ipa && edge->Index == index) {
        old_children[index - update->StartIndex].Ipa = edge->AuxIpa;
        old_children[index - update->StartIndex].Retired = edge->SystemRetired;
        break;
      }
    child_ipa = 0ULL;
    if (pte->Valid) {
      (void)AppleAgxGpuvaG3PteAddressBytes(pte->PageTableAddress, &child_offset);
      RtlZeroMemory(&address, sizeof(address));
      address.GpuPhysical.SegmentId = (UINT)pte->Segment;
      address.GpuPhysical.SegmentOffset = child_offset;
      if (!NT_SUCCESS(AdmissionGpuvaG3ResolveTable(adapter, &address,
              DXGK_PAGETABLEUPDATE_GPU_PHYSICAL, &child_ipa)) ||
          !NT_SUCCESS(AdmissionGpuvaG3BrokerTable(process, child_ipa, TRUE, &child_ipa))) {
        result = AdmissionG3RejectPaging(failure, AdmissionG3PagingFailureChildAddress,
            index, pte, child_ipa, STATUS_INVALID_ADDRESS); goto Rollback;
      }
      if (!AdmissionG3RegisterTable(process, child_ipa, child_level)) {
        result = AdmissionG3RejectPaging(failure, AdmissionG3PagingFailureChildGraph,
            index, pte, child_ipa, STATUS_INVALID_ADDRESS); goto Rollback;
      }
    }
    if (!AppleAgxGpuvaG3GraphUpdateParent(&process->Graph, table_ipa, index, child_ipa)) {
      result = AdmissionG3RejectPaging(failure, AdmissionG3PagingFailureParentLink,
          index, pte, child_ipa, STATUS_INVALID_ADDRESS); goto Rollback;
    }
    ++applied;
  }
  /* All replacement aliases are installed before retiring any old child. */
  {
    APPLE_AGX_GPUVA_G3_NODE *edge;
    for (edge = process->Graph.Parents; edge; edge = edge->Next)
      if (edge->Ipa == table_ipa && edge->Index >= update->StartIndex && edge->Index < end) {
        edge->SystemRetired = 0u;
        AdmissionG3ActivateSystemSubtree(process, edge->AuxIpa, 0u);
      }
  }
  for (index = 0u; index < update->NumPageTableEntries; ++index)
    if (old_children[index].Ipa &&
        !AdmissionG3RetireSystemSubtree(process, old_children[index].Ipa, 0ULL, 0u)) {
      process->Graph.Uncertain = 1u;
      result = STATUS_DEVICE_HARDWARE_ERROR;
      break;
    }
  ExFreePoolWithTag(old_children, ADMISSION_POOL_TAG);
  return result;
Rollback:
  while (applied && !process->Graph.Uncertain) {
    --applied;
    if (!AppleAgxGpuvaG3GraphUpdateParent(&process->Graph, table_ipa,
            update->StartIndex + applied, old_children[applied].Ipa))
      process->Graph.Uncertain = 1u;
    else {
      APPLE_AGX_GPUVA_G3_NODE *edge;
      for (edge = process->Graph.Parents; edge; edge = edge->Next)
        if (edge->Ipa == table_ipa && edge->Index == update->StartIndex + applied) {
          edge->SystemRetired = old_children[applied].Retired;
          break;
        }
    }
  }
  ExFreePoolWithTag(old_children, ADMISSION_POOL_TAG);
  return result;
}

static NTSTATUS AdmissionG3UpdateLeaf(
    ADMISSION_G3_PROCESS *process, ULONGLONG table_ipa,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update,
    ADMISSION_CONTEXT *adapter, ADMISSION_G3_PAGING_FAILURE *failure) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *candidate = NULL;
  APPLE_AGX_GPUVA_G3_NATIVE_LEAF *before = NULL;
  ADMISSION_G3_TABLE_SHADOW *shadow;
  UINT i, j, first, count, first_group, groups, acquired = 0u, published = 0u;
  UINT scale = update->Flags.Use64KBPages ? 16u : 1u;
  UINT limit = update->Flags.Use64KBPages ? 512u : 8192u;
  ULONGLONG step = (ULONGLONG)scale * 0x1000ULL;
  NTSTATUS status = STATUS_INVALID_PARAMETER;
  if (process->Graph.JobInFlight || process->Graph.LeaseToken)
    return STATUS_DEVICE_BUSY;
  if (process->Graph.MappingGeneration == MAXULONGLONG ||
      update->NumPageTableEntries == 0u || update->StartIndex >= limit ||
      update->NumPageTableEntries > limit - update->StartIndex ||
      (update->FirstPteVirtualAddress & (step - 1ULL)) ||
      update->FirstPteVirtualAddress >= (1ULL << 39) ||
      (ULONGLONG)update->NumPageTableEntries * step >
          (1ULL << 39) - update->FirstPteVirtualAddress)
    return STATUS_INVALID_PARAMETER;
  if (!NT_SUCCESS(AdmissionMemoryRuntimeLocalView(adapter, &view)))
    return STATUS_INVALID_DEVICE_STATE;
  first = update->StartIndex * scale;
  count = update->NumPageTableEntries * scale;
  first_group = first / 4u;
  groups = (first + count - 1u) / 4u - first_group + 1u;
  for (shadow = process->TableShadows; shadow; shadow = shadow->Next)
    if (shadow->BrokerIpa == table_ipa) break;
  if (!shadow || shadow->PendingPtes) return STATUS_INVALID_DEVICE_STATE;
  candidate = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      8192u * sizeof(*candidate), ADMISSION_POOL_TAG);
  before = ExAllocatePool2(POOL_FLAG_NON_PAGED,
      groups * sizeof(*before), ADMISSION_POOL_TAG);
  if (!candidate || !before) { status = STATUS_INSUFFICIENT_RESOURCES; goto Done; }
  RtlZeroMemory(candidate, 8192u * sizeof(*candidate));
  RtlZeroMemory(before, groups * sizeof(*before));
  if (shadow->ResidentPtes)
    RtlCopyMemory(candidate, shadow->ResidentPtes, 8192u * sizeof(*candidate));
  RtlZeroMemory(candidate + first, count * sizeof(*candidate));
  if (!shadow->LogicalPtes) {
    shadow->LogicalPtes = ExAllocatePool2(POOL_FLAG_NON_PAGED,
        8192u * sizeof(*candidate), ADMISSION_POOL_TAG);
    if (!shadow->LogicalPtes) { status = STATUS_INSUFFICIENT_RESOURCES; goto Done; }
    RtlZeroMemory(shadow->LogicalPtes, 8192u * sizeof(*candidate));
  }
  /* Validate the entire input before acquiring references or publishing. */
  for (i = 0u; i < update->NumPageTableEntries; ++i) {
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(i, update->Flags.Repeat)];
    ULONGLONG ipa, offset, allocation_offset;
    if (!pte->Valid) continue;
    if (pte->Zero || pte->CacheCoherent || pte->NoExecute || pte->LargePage ||
        pte->PhysicalAdapterIndex || pte->PageTablePageSize ||
        pte->SystemReserved0 || pte->Reserved ||
        !AppleAgxGpuvaG3PteAddressBytes(pte->PageAddress, &offset) ||
        AppleAgxGpuvaG3ResolvePageAddress((UINT)pte->Segment, offset,
            ADMISSION_MEMORY_LOCAL_SEGMENT, view.GuestIpaAddress,
            view.Bytes, &ipa) != AppleAgxGpuvaG3Ok ||
        ipa > MAXULONGLONG - (step - 1ULL) ||
        (update->Flags.Use64KBPages &&
         ((pte->Segment != 0u && pte->Segment != ADMISSION_MEMORY_LOCAL_SEGMENT) ||
          (pte->Segment == ADMISSION_MEMORY_LOCAL_SEGMENT &&
           ((ipa & 0xffffULL) || view.Bytes < step ||
            offset > view.Bytes - step)))) ||
        update->AllocationOffsetInBytes > MAXULONGLONG - (ULONGLONG)i * step)
      goto Done;
    allocation_offset = update->AllocationOffsetInBytes + (ULONGLONG)i * step;
    if (allocation_offset > MAXULONGLONG - (step - 1ULL)) goto Done;
    for (j = 0u; j < scale; ++j) {
      APPLE_AGX_GPUVA_G3_LOGICAL_PTE *out = &candidate[first + i * scale + j];
      out->GuestIpa = ipa + (ULONGLONG)j * 0x1000ULL;
      out->SegmentId = (UINT)pte->Segment;
      out->Flags = APPLE_AGX_GPUVA_G3_VALID |
          (pte->ReadOnly ? 0u : APPLE_AGX_GPUVA_G3_WRITE);
      out->Allocation = (ULONGLONG)(ULONG_PTR)update->hAllocation;
      out->AllocationOffset = allocation_offset + (ULONGLONG)j * 0x1000ULL;
    }
  }
  if (process->Graph.MappingGeneration > MAXULONGLONG - (2ULL * groups + 1ULL)) {
    status = STATUS_INTEGER_OVERFLOW;
    goto Done;
  }
  for (i = 0u; i < count; ++i) {
    APPLE_AGX_GPUVA_G3_LOGICAL_PTE *pte = &candidate[first + i];
    if (pte->Flags && pte->SegmentId == 0u &&
        !AppleAgxGpuvaG3MappingAcquire(&process->Graph, pte->GuestIpa)) {
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto Done;
    }
    ++acquired;
  }
  for (i = 0u; i < groups; ++i) {
    APPLE_AGX_GPUVA_G3_NODE *leaf;
    for (leaf = process->Graph.Leaves; leaf; leaf = leaf->Next)
      if (leaf->Ipa == table_ipa && leaf->Index == first_group + i) break;
    if (leaf) {
      before[i].GuestIpa = leaf->AuxIpa;
      before[i].WritableMask = leaf->Writable;
      before[i].SegmentId = leaf->Kind == AppleAgxGpuvaG3SystemBacking ?
          0u : ADMISSION_MEMORY_LOCAL_SEGMENT;
    }
  }
  for (i = 0u; i < groups; ++i) {
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *group = candidate + (first_group + i) * 4u;
    ULONGLONG ipa = group[0].GuestIpa;
    UINT segment = group[0].SegmentId, flags = group[0].Flags;
    BOOLEAN complete = (flags & APPLE_AGX_GPUVA_G3_VALID) && ipa &&
        !(ipa & 0x3fffULL) && ipa <= MAXULONGLONG - 0x3fffULL;
    for (j = 1u; complete && j < 4u; ++j)
      if (group[j].Flags != flags || group[j].SegmentId != segment ||
          group[j].GuestIpa != ipa + (ULONGLONG)j * 0x1000ULL) complete = FALSE;
    if (complete && segment != 0u &&
        (segment != ADMISSION_MEMORY_LOCAL_SEGMENT ||
         !AppleAgxGpuvaG3TableSpanWithinLocal(view.GuestIpaAddress, view.Bytes,
                                              ipa, 0x4000ULL))) complete = FALSE;
    bool unavailable = false;
    BOOLEAN updated = AppleAgxGpuvaG3GraphTryLeafBacking(&process->Graph, table_ipa,
        first_group + i, complete ? ipa : 0ULL,
        complete && (flags & APPLE_AGX_GPUVA_G3_WRITE),
        segment == 0u ? AppleAgxGpuvaG3SystemBacking : AppleAgxGpuvaG3LocalBacking,
        &unavailable);
    if (!updated && unavailable) {
      /* VidMm's logical mapping may be CPU-only under the retained broker
       * contract. Retire any previous GPU leaf before acknowledging it. */
      complete = FALSE;
      updated = AppleAgxGpuvaG3GraphUpdateLeafBacking(&process->Graph, table_ipa,
          first_group + i, 0ULL, false, AppleAgxGpuvaG3SystemBacking);
    }
    if (!updated) {
      UINT rejected_broker_status = process->Graph.LastStatus;
      UINT input = (first_group + i) * 4u < first ? 0u :
          ((first_group + i) * 4u - first) / scale;
      status = AdmissionG3RejectPaging(failure, AdmissionG3PagingFailureLeafGraph,
          (first_group + i) * 4u, &update->pPageTableEntries[
              AppleAgxGpuvaG3PteInputIndex(input, update->Flags.Repeat)],
          complete ? ipa : 0ULL, STATUS_DEVICE_HARDWARE_ERROR);
      /* Restore all acknowledged publications before returning failure.
       * Uncertain TLB state retains both sets of mapping/grant references. */
      while (published && !process->Graph.Uncertain) {
        --published;
        if (!AppleAgxGpuvaG3GraphUpdateLeafBacking(&process->Graph, table_ipa,
                first_group + published, before[published].GuestIpa,
                before[published].WritableMask != 0u,
                before[published].SegmentId == 0u ? AppleAgxGpuvaG3SystemBacking :
                                                   AppleAgxGpuvaG3LocalBacking))
          process->Graph.Uncertain = 1u;
      }
      /* Successful rollback calls must not erase the original refusal. */
      process->Graph.LastStatus = rejected_broker_status;
      if (process->Graph.Uncertain) {
        /* Only the updated range acquired new references. */
        RtlZeroMemory(candidate, first * sizeof(*candidate));
        RtlZeroMemory(candidate + first + count,
            (8192u - first - count) * sizeof(*candidate));
        shadow->PendingPtes = candidate;
        candidate = NULL;
        acquired = 0u;
      }
      goto Done;
    }
    ++published;
    if (!complete && flags && segment < 32u &&
        process->State->UnpublishedGroups[segment] != MAXULONGLONG)
      ++process->State->UnpublishedGroups[segment];
  }
  /* Publication and TLB acknowledgement precede release of the old lifetime. */
  if (shadow->ResidentPtes) {
    for (i = first; i < first + count; ++i)
      if (shadow->ResidentPtes[i].Flags && shadow->ResidentPtes[i].SegmentId == 0u)
        AppleAgxGpuvaG3MappingRelease(&process->Graph, shadow->ResidentPtes[i].GuestIpa);
    ExFreePoolWithTag(shadow->ResidentPtes, ADMISSION_POOL_TAG);
  }
  shadow->ResidentPtes = candidate;
  candidate = NULL;
  acquired = 0u;
  if (update->Flags.Use64KBPages)
    (void)AppleAgxGpuvaG3InvalidateLogical64K(shadow->LogicalPtes,
        update->StartIndex, update->NumPageTableEntries);
  else
    RtlCopyMemory(shadow->LogicalPtes + first, shadow->ResidentPtes + first,
                  count * sizeof(*shadow->LogicalPtes));
  ++process->Graph.MappingGeneration;
  status = STATUS_SUCCESS;
Done:
  for (i = 0u; i < acquired; ++i)
    if (candidate[first + i].Flags && candidate[first + i].SegmentId == 0u)
      AppleAgxGpuvaG3MappingRelease(&process->Graph, candidate[first + i].GuestIpa);
  if (before) ExFreePoolWithTag(before, ADMISSION_POOL_TAG);
  if (candidate) ExFreePoolWithTag(candidate, ADMISSION_POOL_TAG);
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
  AdmissionPagingNoteEncoded(adapter, produced);
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
  status = AdmissionMemoryRuntimeLocalView(adapter, &view);
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
  UINT wait_ms;
  const UINT job_wait_limit_ms = 3000u; /* > TdrDelay (2 s) */
  LARGE_INTEGER delay;
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
        NT_SUCCESS(AdmissionMemoryRuntimeLocalView(adapter, &view)) &&
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
       (AppleAgxGpuvaG3AdmissionContract(
            1u, ADMISSION_GPUVA_G1B_PAGE_PROFILE).Leaf64KBytes == 0u ||
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
  /* R155: VidMm may build a GPU_PHYSICAL update for a process whose native
   * job is in flight. This paging buffer executes on node 0 only after that
   * job's fence (joined completion ends the job and releases the lease before
   * the fence completes), and DxgkDdiBuildPagingBuffer may not return a busy
   * status for UpdatePageTable. Wait, with the G3 lock released, for the job
   * to finish; the bound exceeds TdrDelay so a genuine hang stays a TDR. */
  for (wait_ms = 0u;; ++wait_ms) {
    ExAcquireFastMutex(&state->Lock);
    process = AdmissionGpuvaG3FindProcess(state, update->hProcess);
    if (process == NULL || process->Poisoned || process->Graph.Uncertain ||
        (!process->Graph.JobInFlight && !process->Graph.LeaseToken) ||
        wait_ms >= job_wait_limit_ms)
      break;
    ExReleaseFastMutex(&state->Lock);
    delay.QuadPart = -10000LL; /* 1 ms */
    (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);
  }
  RtlCopyMemory(unpublished_before, state->UnpublishedGroups,
                sizeof(unpublished_before));
  if (process == NULL || process->Poisoned || process->Graph.Uncertain) {
    status = STATUS_INVALID_DEVICE_STATE;
  } else if (process->Graph.JobInFlight || process->Graph.LeaseToken) {
    status = STATUS_DEVICE_BUSY;
  } else {
    status = AdmissionMemoryRuntimeLocalView(adapter, &view);
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
    if (!AdmissionG3PrepareTableReuse(process, table_ipa,
            2u - update->PageTableLevel,
            (BOOLEAN)(update->Flags.InitialUpdate != 0u))) {
      failure.TableAddBranch = 2u;
      status = AdmissionG3RejectPaging(&failure,
          AdmissionG3PagingFailureTableGraph, MAXULONG, NULL, 0ULL,
          STATUS_INVALID_ADDRESS);
      goto PagingDone;
    }
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
    if (!AdmissionG3RegisterTable(process, table_ipa,
                                 2u - update->PageTableLevel)) {
      failure.TableAddBranch = 2u;
      status = AdmissionG3RejectPaging(&failure,
          AdmissionG3PagingFailureTableGraph, MAXULONG, NULL, 0ULL,
          STATUS_INVALID_ADDRESS);
    } else {
      if (update->PageTableLevel == 0u) {
        status = AdmissionG3UpdateLeaf(process, table_ipa, update, adapter,
                                       &failure);
      } else {
        status = AdmissionG3UpdateParent(process, table_ipa, update, adapter,
                                         &failure);
      }
    }
    if (NT_SUCCESS(status) &&
        !NT_SUCCESS(AdmissionGpuvaG3MirrorTable(
            process, original_table_ipa, table_words)))
      status = AdmissionG3RejectPaging(&failure,
          AdmissionG3PagingFailureTableMirror, MAXULONG, NULL, 0ULL,
          STATUS_DEVICE_HARDWARE_ERROR);
    /* UpdatePageTable names both the owning process and the target table.
     * WDDM level2 is our native root (level0). Select its acknowledged shadow
     * here: CPU staging can precede the context's SetRootPageTable callback.
     * An eviction of a former root must not select it again. BindRoot uses
     * the same inactive broker relocation/generation contract as SetRoot. */
    if (NT_SUCCESS(status) && update->PageTableLevel == 2u &&
        !update->Flags.NotifyEviction &&
        !AppleAgxGpuvaG3GraphBindRoot(&process->Graph, table_ipa)) {
      process->Poisoned = TRUE;
      status = STATUS_DEVICE_HARDWARE_ERROR;
    }
    if (NT_SUCCESS(status) && process->PrivateLeafIpa &&
        !AppleAgxGpuvaG3GraphAttachPrivate(&process->Graph, process->PrivateVa,
            process->PrivateMiddleIpa, process->PrivateLeafIpa)) {
      process->Poisoned = TRUE;
      status = STATUS_DEVICE_HARDWARE_ERROR;
    }
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
