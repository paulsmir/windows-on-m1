#include "gpuva_g3_private.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)

static NTSTATUS AdmissionG3UpdateParent(
    ADMISSION_G3_PROCESS *process, ULONGLONG table_ipa,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update,
    ADMISSION_CONTEXT *adapter) {
  UINT index, end, child_level;
  DXGK_PAGETABLEUPDATEADDRESS address;
  ULONGLONG child_ipa;
  if (update->PageTableLevel != 1u && update->PageTableLevel != 2u)
    return STATUS_INVALID_PARAMETER;
  child_level = 2u - update->PageTableLevel + 1u;
  end = update->StartIndex + update->NumPageTableEntries;
  for (index = update->StartIndex; index < end; ++index) {
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(index - update->StartIndex,
                                     update->Flags.Repeat)];
    if (!pte->Valid) continue;
    if (pte->Flags !=
        (1ULL | ((ULONGLONG)ADMISSION_MEMORY_LOCAL_SEGMENT << 5) |
         ((update->PageTableLevel == 1u && update->Flags.Use64KBPages) ?
              (1ULL << 17) : 0ULL)))
      return STATUS_NOT_SUPPORTED;
    RtlZeroMemory(&address, sizeof(address));
    address.GpuPhysical.SegmentId = ADMISSION_MEMORY_LOCAL_SEGMENT;
    address.GpuPhysical.SegmentOffset = pte->PageTableAddress;
    if (!NT_SUCCESS(AdmissionGpuvaG3ResolveTable(adapter, &address,
            DXGK_PAGETABLEUPDATE_GPU_PHYSICAL, &child_ipa)))
      return STATUS_INVALID_ADDRESS;
  }
  for (index = update->StartIndex; index < end; ++index) {
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(index - update->StartIndex,
                                     update->Flags.Repeat)];
    if (!pte->Valid) {
      if (!AppleAgxGpuvaG3GraphUpdateParent(&process->Graph, table_ipa,
              index, 0ULL)) return STATUS_INVALID_PARAMETER;
      continue;
    }
    RtlZeroMemory(&address, sizeof(address));
    address.GpuPhysical.SegmentId = ADMISSION_MEMORY_LOCAL_SEGMENT;
    address.GpuPhysical.SegmentOffset = pte->PageTableAddress;
    if (!NT_SUCCESS(AdmissionGpuvaG3ResolveTable(adapter, &address,
            DXGK_PAGETABLEUPDATE_GPU_PHYSICAL, &child_ipa)) ||
        !AppleAgxGpuvaG3GraphRegisterTable(&process->Graph,
            child_ipa, child_level) ||
        !AppleAgxGpuvaG3GraphUpdateParent(&process->Graph, table_ipa,
            index, child_ipa)) return STATUS_INVALID_ADDRESS;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionG3UpdateLeaf(
    ADMISSION_G3_PROCESS *process, ULONGLONG table_ipa,
    const DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update,
    ADMISSION_CONTEXT *adapter) {
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE *logical = NULL;
  APPLE_AGX_GPUVA_G3_NATIVE_LEAF *leaves = NULL;
  APPLE_AGX_GPUVA_G3_RESULT plan;
  unsigned int count = 0u, index;
  NTSTATUS status = STATUS_INVALID_PARAMETER;
  if (update->NumPageTableEntries == 0u ||
      update->NumPageTableEntries >
          (update->Flags.Use64KBPages ? 512u : 8192u))
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
  RtlZeroMemory(logical, (SIZE_T)update->NumPageTableEntries * sizeof(*logical));
  for (index = 0u; index < update->NumPageTableEntries && index < 8192u;
       ++index) {
    const DXGK_PTE *pte = &update->pPageTableEntries[
        AppleAgxGpuvaG3PteInputIndex(index, update->Flags.Repeat)];
    ULONGLONG ipa;
    if (!pte->Valid) {
      continue;
    }
    if (pte->Zero || pte->CacheCoherent || pte->NoExecute || pte->LargePage ||
        pte->PhysicalAdapterIndex || pte->PageTablePageSize ||
        pte->SystemReserved0 || pte->Reserved ||
        AppleAgxGpuvaG3ResolvePageAddress((UINT)pte->Segment,
            pte->PageAddress, ADMISSION_MEMORY_LOCAL_SEGMENT,
            view.GuestIpaAddress, view.Bytes, &ipa) != AppleAgxGpuvaG3Ok)
      goto Done;
    if (update->Flags.Use64KBPages &&
        (view.Bytes < 0x10000u ||
         (UINT)pte->Segment != ADMISSION_MEMORY_LOCAL_SEGMENT ||
         (pte->PageAddress & 0xffffu) ||
         pte->PageAddress > view.Bytes - 0x10000u))
      goto Done;
    logical[index].GuestIpa = ipa;
    logical[index].SegmentId = (unsigned int)pte->Segment;
    logical[index].Flags = APPLE_AGX_GPUVA_G3_VALID |
        (pte->ReadOnly ? 0u : APPLE_AGX_GPUVA_G3_WRITE);
  }
  if (update->Flags.Use64KBPages)
    plan = AppleAgxGpuvaG3Plan64KSpan(logical, update->StartIndex,
        update->NumPageTableEntries, update->FirstPteVirtualAddress,
        ADMISSION_MEMORY_LOCAL_SEGMENT, leaves, 2048u, &count);
  else
    plan = AppleAgxGpuvaG3PlanSpan(logical, update->StartIndex,
        update->NumPageTableEntries, update->FirstPteVirtualAddress,
        ADMISSION_MEMORY_LOCAL_SEGMENT,
        ADMISSION_GPUVA_G1B_PAGE_PROFILE == 64 ? 0x10000u : 0x4000u,
        leaves, update->NumPageTableEntries / 4u, &count);
  if (plan != AppleAgxGpuvaG3Ok && plan != AppleAgxGpuvaG3Unmap)
    goto Done;
  for (index = 0u; index < count && index < 2048u; ++index) {
    if (!AppleAgxGpuvaG3GraphUpdateLeaf(&process->Graph, table_ipa,
            (update->Flags.Use64KBPages ? update->StartIndex * 4u :
                update->StartIndex / 4u) + index, leaves[index].GuestIpa,
            leaves[index].WritableMask != 0u)) {
      status = STATUS_DEVICE_HARDWARE_ERROR;
      goto Done;
    }
  }
  status = STATUS_SUCCESS;
Done:
  if (leaves != NULL) ExFreePoolWithTag(leaves, ADMISSION_POOL_TAG);
  if (logical != NULL) ExFreePoolWithTag(logical, ADMISSION_POOL_TAG);
  return status;
}

NTSTATUS AdmissionGpuvaG3BuildPagingBuffer(ADMISSION_CONTEXT *adapter,
                                            DXGKARG_BUILDPAGINGBUFFER *args) {
  ADMISSION_G3_STATE *state;
  ADMISSION_G3_PROCESS *process;
  DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *update;
  DXGK_PAGETABLEUPDATEADDRESS address;
  ULONGLONG table_ipa, root_ipa;
  UINT limit;
  NTSTATUS status;
  if (adapter == NULL || args == NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_PARAMETER;
  state = (ADMISSION_G3_STATE *)adapter->GpuvaG3State;
  if (state == NULL) return STATUS_INVALID_DEVICE_STATE;
  if (args->Operation == DXGK_OPERATION_FLUSH_TLB) {
    ExAcquireFastMutex(&state->Lock);
    process = AdmissionGpuvaG3FindProcess(state, args->FlushTlb.hProcess);
    address.GpuPhysical = args->FlushTlb.RootPageTableAddress;
    status = AdmissionGpuvaG3ResolveTable(adapter, &address,
        DXGK_PAGETABLEUPDATE_GPU_PHYSICAL, &root_ipa);
    if (process == NULL || !NT_SUCCESS(status) || process->Poisoned ||
        root_ipa != process->Graph.RootIpa ||
        !AppleAgxGpuvaG3GraphFlush(&process->Graph,
            args->FlushTlb.StartVirtualAddress,
            args->FlushTlb.EndVirtualAddress)) status = STATUS_INVALID_PARAMETER;
    else status = STATUS_SUCCESS;
    ExReleaseFastMutex(&state->Lock);
    return status;
  }
  if (args->Operation != DXGK_OPERATION_UPDATE_PAGE_TABLE)
    return STATUS_NOT_SUPPORTED;
  update = &args->UpdatePageTable;
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
      (update->PageTableLevel == 0u &&
       !update->Flags.Use64KBPages &&
       ((update->StartIndex | update->NumPageTableEntries) & 3u)) ||
      (update->UpdateMode != DXGK_PAGETABLEUPDATE_GPU_PHYSICAL &&
       update->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL))
    return STATUS_INVALID_PARAMETER;
  /* CPU_VIRTUAL updates complete now; supplied DMA buffers stay untouched. */
  status = AdmissionGpuvaG3ResolveTable(adapter, &update->PageTableAddress,
                                        update->UpdateMode, &table_ipa);
  if (!NT_SUCCESS(status)) return status;
  ExAcquireFastMutex(&state->Lock);
  process = AdmissionGpuvaG3FindProcess(state, update->hProcess);
  if (process == NULL || process->Poisoned || process->Graph.Uncertain) {
    status = STATUS_INVALID_DEVICE_STATE;
  } else if (!AppleAgxGpuvaG3GraphRegisterTable(&process->Graph, table_ipa,
                                                 2u - update->PageTableLevel)) {
    status = STATUS_INVALID_ADDRESS;
  } else if (update->PageTableLevel == 0u) {
    status = AdmissionG3UpdateLeaf(process, table_ipa, update, adapter);
  } else {
    status = AdmissionG3UpdateParent(process, table_ipa, update, adapter);
  }
  if (process != NULL && !NT_SUCCESS(status) &&
      process->Graph.Uncertain) process->Poisoned = TRUE;
  ExReleaseFastMutex(&state->Lock);
  return status;
}

#endif
