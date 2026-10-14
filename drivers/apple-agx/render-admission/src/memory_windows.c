#include "render_admission.h"

/* M2-f4b: budget groups. EXP1263 (agx_vidmem): with no group flag every
 * segment counted as non-budget (2360 MiB) and the LOCAL budget (1081 MiB)
 * came from the implicit system-memory segment, which the AGX cannot use for
 * GPU buffers, while CurrentUsage read 0 with 218 MiB resident in segment 3.
 * The memory segments (2: firmware-reserved DDR, 3: demand-backed pool) are
 * what this UMA GPU works from, so they form the LOCAL group (DXGI: the
 * fastest memory, the target of an application's working set; D3D12: on a
 * UMA adapter L0 and LOCAL are the same memory); the aperture is NON_LOCAL.
 * Every segment gets a group: VidMm refuses an allocation whose segment set
 * mixes budgeted and non-budgeted segments (dxgmms2 VerifyBudgetGroups), and
 * this driver's sets span all three (CPU-visible class BOs: 3 and 1). */
static VOID AdmissionSegmentBudgetGroup(
    _In_ const APPLE_AGX_PHYSICAL_SEGMENT *Segment,
    _Inout_ DXGK_SEGMENTFLAGS *Flags) {
  Flags->LocalBudgetGroup = Segment->Aperture ? 0u : 1u;
  Flags->NonLocalBudgetGroup = Segment->Aperture ? 1u : 0u;
}

static VOID AdmissionDescribeSegment(
    _In_ const APPLE_AGX_PHYSICAL_SEGMENT *Segment,
    _Out_ DXGK_SEGMENTDESCRIPTOR4 *Descriptor) {
  RtlZeroMemory(Descriptor, sizeof(*Descriptor));
  Descriptor->Flags.Aperture = Segment->Aperture != APPLE_AGX_FALSE;
  Descriptor->Flags.Use64KBPages = Segment->Use64KPages != APPLE_AGX_FALSE;
#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
  if (!Segment->Aperture)
    Descriptor->Flags.Use64KBPages = ADMISSION_G1B_USE64K;
#endif
  Descriptor->Flags.CpuVisible = Segment->CpuVisible != APPLE_AGX_FALSE;
  Descriptor->Flags.PopulatedFromSystemMemory =
      Segment->PopulatedFromSystemMemory != APPLE_AGX_FALSE;
  AdmissionSegmentBudgetGroup(Segment, &Descriptor->Flags);
  Descriptor->BaseAddress.QuadPart = (LONGLONG)Segment->Base;
  Descriptor->Size = (SIZE_T)Segment->Size;
  Descriptor->CommitLimit = (SIZE_T)Segment->CommitLimit;
}

#if ADMISSION_GPUVA_G1B_PAGE_PROFILE != 0
static VOID AdmissionDescribeSegment5(
    _In_ const APPLE_AGX_PHYSICAL_SEGMENT *Segment,
    _Out_ DXGK_SEGMENTDESCRIPTOR5 *Descriptor) {
  RtlZeroMemory(Descriptor, sizeof(*Descriptor));
  Descriptor->SegmentType = Segment->Aperture ? DXGK_SEGMENTTYPE_SYSMEM :
                                                 DXGK_SEGMENTTYPE_LOCAL;
  Descriptor->Flags.Aperture = Segment->Aperture != APPLE_AGX_FALSE;
  Descriptor->Flags.CpuVisible = Segment->CpuVisible != APPLE_AGX_FALSE;
  Descriptor->Flags.PopulatedFromSystemMemory =
      Segment->PopulatedFromSystemMemory != APPLE_AGX_FALSE;
  Descriptor->Flags.Use64KBPages = Segment->Aperture ? 0u :
                                                      ADMISSION_G1B_USE64K;
  AdmissionSegmentBudgetGroup(Segment, &Descriptor->Flags);
  Descriptor->BaseAddress.QuadPart = (LONGLONG)Segment->Base;
  Descriptor->Size = Segment->Size;
  Descriptor->SlabSize = Segment->Aperture ? DXGK_PAGESIZE_4KB :
                                             ADMISSION_G1B_SLAB_SIZE;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQuerySegment5(
    ADMISSION_CONTEXT *Context,
    const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo) {
  const DXGK_QUERYSEGMENTIN5 *input;
  DXGK_QUERYSEGMENTOUT5 *output;
  APPLE_AGX_PHYSICAL_SEGMENT local;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  ULONGLONG cpuTranslated;
#endif
  if (Context == NULL || QueryAdapterInfo == NULL ||
      QueryAdapterInfo->pInputData == NULL ||
      QueryAdapterInfo->InputDataSize < sizeof(*input) ||
      QueryAdapterInfo->pOutputData == NULL ||
      QueryAdapterInfo->OutputDataSize < sizeof(*output))
    return STATUS_INVALID_PARAMETER;
  input = (const DXGK_QUERYSEGMENTIN5 *)QueryAdapterInfo->pInputData;
  output = (DXGK_QUERYSEGMENTOUT5 *)QueryAdapterInfo->pOutputData;
  if (input->PhysicalAdapterIndex != 0u || input->Reserved != 0u ||
      !AdmissionMemoryReady(&Context->Memory))
    return STATUS_INVALID_PARAMETER;
  if (output->SegmentDescriptors == NULL)
    return STATUS_BUFFER_TOO_SMALL;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!NT_SUCCESS(AdmissionMemoryRuntimeLocalView(Context, &view)) ||
      !AdmissionMemoryCpuVisibleLocalBase(
          &Context->Memory, view.CpuAddress, view.GuestIpaAddress,
          view.HostPhysicalAddress, view.GpuVirtualAddress, view.Bytes,
          &cpuTranslated))
    return STATUS_INVALID_DEVICE_STATE;
#endif
  RtlZeroMemory(output->Reserved, sizeof(output->Reserved));
  AdmissionDescribeSegment5(&Context->Memory.Topology.Aperture,
                            &output->SegmentDescriptors[0]);
  local = Context->Memory.Topology.Local;
  local.Size = Context->Memory.LocalAllocationBytes;
  local.CommitLimit = Context->Memory.LocalAllocationBytes;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  local.CpuVisible = APPLE_AGX_TRUE;
#endif
  AdmissionDescribeSegment5(&local, &output->SegmentDescriptors[1]);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  output->SegmentDescriptors[1].CpuTranslatedAddress.QuadPart =
      (LONGLONG)cpuTranslated;
  /* R64: segment 2 is the firmware-reserved DDR slab, not memory the driver
   * allocated at initialization. DXGK_SEGMENTFLAGS: do not set
   * PopulatedFromSystemMemory for BIOS/firmware-reserved memory; report
   * PopulatedByReservedDDRByFirmware (WDDM 2.9+). EXP837 bugchecked in
   * VIDMM_GLOBAL::Rotate on the first CPU-visible lock of this segment. */
  output->SegmentDescriptors[1].Flags.PopulatedFromSystemMemory = 0u;
  output->SegmentDescriptors[1].Flags.PopulatedByReservedDDRByFirmware = 1u;
  /* M2-f2: segment 3 is Windows memory this driver allocated at start, so it
   * is PopulatedFromSystemMemory; the CPU sees it at the block's address. */
  if (Context->Memory.Topology.SegmentCount == 3u &&
      Context->Memory.Topology.Pool.Id == 3u) {
    ADMISSION_SCANOUT_MEMORY_VIEW pool;
    if (!AdmissionMemoryPoolView(Context, &pool))
      return STATUS_INVALID_DEVICE_STATE;
    AdmissionDescribeSegment5(&Context->Memory.Topology.Pool,
                              &output->SegmentDescriptors[2]);
    output->SegmentDescriptors[2].CpuTranslatedAddress.QuadPart =
        (LONGLONG)pool.GuestIpaAddress;
  }
#endif
  return STATUS_SUCCESS;
}
#endif

_Use_decl_annotations_ NTSTATUS AdmissionDdiQuerySegment4(
    ADMISSION_CONTEXT *Context,
    const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo) {
  const DXGK_QUERYSEGMENTIN4 *input;
  DXGK_QUERYSEGMENTOUT4 *output;
  DXGK_SEGMENTDESCRIPTOR4 *aperture;
  DXGK_SEGMENTDESCRIPTOR4 *local;
  APPLE_AGX_PHYSICAL_SEGMENT local_segment;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  ADMISSION_SCANOUT_MEMORY_VIEW view;
  ULONGLONG cpuTranslated;
#endif

  if (Context == NULL || QueryAdapterInfo == NULL ||
      QueryAdapterInfo->pInputData == NULL ||
      QueryAdapterInfo->InputDataSize < sizeof(*input) ||
      QueryAdapterInfo->pOutputData == NULL ||
      QueryAdapterInfo->OutputDataSize < sizeof(*output))
    return STATUS_INVALID_PARAMETER;
  input = (const DXGK_QUERYSEGMENTIN4 *)QueryAdapterInfo->pInputData;
  output = (DXGK_QUERYSEGMENTOUT4 *)QueryAdapterInfo->pOutputData;
  if (input->PhysicalAdapterIndex != 0u)
    return STATUS_INVALID_PARAMETER;
  if (!AdmissionMemoryReady(&Context->Memory))
    return STATUS_NOT_SUPPORTED;

  if (output->NbSegment == 0u || output->pSegmentDescriptor == NULL) {
    output->NbSegment = Context->Memory.Topology.SegmentCount;
    return STATUS_SUCCESS;
  }
  if (output->NbSegment < Context->Memory.Topology.SegmentCount ||
      output->SegmentDescriptorStride < sizeof(*aperture)) {
    output->NbSegment = Context->Memory.Topology.SegmentCount;
    return STATUS_BUFFER_TOO_SMALL;
  }
  aperture = (DXGK_SEGMENTDESCRIPTOR4 *)output->pSegmentDescriptor;
  local = (DXGK_SEGMENTDESCRIPTOR4 *)(
      (PUCHAR)output->pSegmentDescriptor + output->SegmentDescriptorStride);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  if (!NT_SUCCESS(AdmissionMemoryRuntimeLocalView(Context, &view)) ||
      !AdmissionMemoryCpuVisibleLocalBase(
          &Context->Memory, view.CpuAddress, view.GuestIpaAddress,
          view.HostPhysicalAddress, view.GpuVirtualAddress, view.Bytes,
          &cpuTranslated))
    return STATUS_INVALID_DEVICE_STATE;
#endif
  AdmissionDescribeSegment(&Context->Memory.Topology.Aperture, aperture);
  local_segment = Context->Memory.Topology.Local;
  local_segment.Size = Context->Memory.LocalAllocationBytes;
  local_segment.CommitLimit = Context->Memory.LocalAllocationBytes;
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  local_segment.CpuVisible = APPLE_AGX_TRUE;
#endif
  AdmissionDescribeSegment(&local_segment, local);
#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
  local->CpuTranslatedAddress.QuadPart = (LONGLONG)cpuTranslated;
  local->Flags.PopulatedFromSystemMemory = 0u;
  local->Flags.PopulatedByReservedDDRByFirmware = 1u;
  if (Context->Memory.Topology.SegmentCount == 3u &&
      Context->Memory.Topology.Pool.Id == 3u) {
    DXGK_SEGMENTDESCRIPTOR4 *pool_descriptor = (DXGK_SEGMENTDESCRIPTOR4 *)(
        (PUCHAR)output->pSegmentDescriptor +
        2u * output->SegmentDescriptorStride);
    ADMISSION_SCANOUT_MEMORY_VIEW pool;
    if (!AdmissionMemoryPoolView(Context, &pool))
      return STATUS_INVALID_DEVICE_STATE;
    AdmissionDescribeSegment(&Context->Memory.Topology.Pool, pool_descriptor);
    pool_descriptor->CpuTranslatedAddress.QuadPart =
        (LONGLONG)pool.GuestIpaAddress;
  }
#endif
  output->NbSegment = Context->Memory.Topology.SegmentCount;
  output->PagingBufferSegmentId =
      Context->Memory.Topology.PagingBufferSegmentId;
  output->PagingBufferSize =
      (UINT)Context->Memory.Topology.PagingBufferSize;
  output->PagingBufferPrivateDataSize = PAGE_SIZE;
  return STATUS_SUCCESS;
}
