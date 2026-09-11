#include "render_admission.h"

static VOID AdmissionDescribeSegment(
    _In_ const APPLE_AGX_PHYSICAL_SEGMENT *Segment,
    _Out_ DXGK_SEGMENTDESCRIPTOR4 *Descriptor) {
  RtlZeroMemory(Descriptor, sizeof(*Descriptor));
  Descriptor->Flags.Aperture = Segment->Aperture != APPLE_AGX_FALSE;
  Descriptor->Flags.Use64KBPages = Segment->Use64KPages != APPLE_AGX_FALSE;
  Descriptor->Flags.CpuVisible = Segment->CpuVisible != APPLE_AGX_FALSE;
  Descriptor->Flags.PopulatedFromSystemMemory =
      Segment->PopulatedFromSystemMemory != APPLE_AGX_FALSE;
  Descriptor->BaseAddress.QuadPart = (LONGLONG)Segment->Base;
  Descriptor->Size = (SIZE_T)Segment->Size;
  Descriptor->CommitLimit = (SIZE_T)Segment->CommitLimit;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiQuerySegment4(
    ADMISSION_CONTEXT *Context,
    const DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo) {
  const DXGK_QUERYSEGMENTIN4 *input;
  DXGK_QUERYSEGMENTOUT4 *output;
  DXGK_SEGMENTDESCRIPTOR4 *aperture;
  DXGK_SEGMENTDESCRIPTOR4 *local;
  APPLE_AGX_PHYSICAL_SEGMENT local_segment;

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
  AdmissionDescribeSegment(&Context->Memory.Topology.Aperture, aperture);
  local_segment = Context->Memory.Topology.Local;
  local_segment.Size = Context->Memory.LocalAllocationBytes;
  local_segment.CommitLimit = Context->Memory.LocalAllocationBytes;
  AdmissionDescribeSegment(&local_segment, local);
  output->NbSegment = Context->Memory.Topology.SegmentCount;
  output->PagingBufferSegmentId =
      Context->Memory.Topology.PagingBufferSegmentId;
  output->PagingBufferSize =
      (UINT)Context->Memory.Topology.PagingBufferSize;
  output->PagingBufferPrivateDataSize = PAGE_SIZE;
  return STATUS_SUCCESS;
}
