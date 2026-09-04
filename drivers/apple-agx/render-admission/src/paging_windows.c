#include "render_admission.h"

static NTSTATUS AdmissionEncodePaging(
    _Inout_ DXGKARG_BUILDPAGINGBUFFER *Args,
    _In_ const APPLE_AGX_PHYSICAL_PAGING_PLAN *Plan, _In_opt_ PMDL Mdl) {
  ADMISSION_PAGING_MARKER marker;
  ADMISSION_PAGING_RECORD record;
  if (Args->pDmaBuffer == NULL || Args->pDmaBufferPrivateData == NULL ||
      Args->DmaSize < sizeof(marker) ||
      Args->DmaBufferPrivateDataSize < sizeof(record))
    return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
  RtlZeroMemory(&marker, sizeof(marker));
  marker.Magic = ADMISSION_PAGING_MAGIC;
  marker.Version = ADMISSION_PAGING_VERSION;
  marker.RecordBytes = sizeof(record);
  RtlZeroMemory(&record, sizeof(record));
  record.Header = marker;
  record.Plan = *Plan;
  record.SystemMdl = Mdl;
  RtlCopyMemory(Args->pDmaBuffer, &marker, sizeof(marker));
  RtlCopyMemory(Args->pDmaBufferPrivateData, &record, sizeof(record));
  Args->pDmaBuffer = (PUCHAR)Args->pDmaBuffer + sizeof(marker);
  Args->DmaSize -= sizeof(marker);
  Args->pDmaBufferPrivateData =
      (PUCHAR)Args->pDmaBufferPrivateData + sizeof(record);
  Args->DmaBufferPrivateDataSize -= sizeof(record);
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiBuildPagingBuffer(
    HANDLE Adapter, DXGKARG_BUILDPAGINGBUFFER *Args) {
  ADMISSION_CONTEXT *context = (ADMISSION_CONTEXT *)Adapter;
  APPLE_AGX_PHYSICAL_PAGING_PLAN plan;
  APPLE_AGX_PHYSICAL_PAGING_RESULT result;
  PMDL mdl;
  ULONGLONG systemOffset;
  if (context == NULL || Args == NULL ||
      context->Memory.Initialized != APPLE_AGX_TRUE ||
      context->Memory.UatReady != APPLE_AGX_TRUE)
    return STATUS_INVALID_DEVICE_STATE;
  switch (Args->Operation) {
  case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
    if (Args->MapApertureSegment.SegmentId !=
            ADMISSION_MEMORY_APERTURE_SEGMENT ||
        Args->MapApertureSegment.OffsetInPages >
            (MAXULONGLONG >> PAGE_SHIFT) ||
        Args->MapApertureSegment.NumberOfPages > MAXULONG)
      return STATUS_INVALID_PARAMETER;
    return AdmissionMemoryRuntimeMapAperture(
        context,
        (ULONGLONG)Args->MapApertureSegment.OffsetInPages << PAGE_SHIFT,
        Args->MapApertureSegment.pMdl,
        Args->MapApertureSegment.MdlOffset,
        (UINT)Args->MapApertureSegment.NumberOfPages);
  case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
    if (Args->UnmapApertureSegment.SegmentId !=
            ADMISSION_MEMORY_APERTURE_SEGMENT ||
        Args->UnmapApertureSegment.OffsetInPages >
            (MAXULONGLONG >> PAGE_SHIFT) ||
        Args->UnmapApertureSegment.NumberOfPages !=
            (SIZE_T)APPLE_AGX_SYSTEM_PAGES_PER_WDDM_PAGE)
      return STATUS_INVALID_PARAMETER;
    return AdmissionMemoryRuntimeUnmapAperture(
        context,
        (ULONGLONG)Args->UnmapApertureSegment.OffsetInPages << PAGE_SHIFT,
        (ULONGLONG)Args->UnmapApertureSegment.DummyPage.QuadPart);
  case DXGK_OPERATION_TRANSFER:
    if (Args->Transfer.Flags.Reserved != 0u ||
        Args->Transfer.Flags.Swizzle || Args->Transfer.Flags.Unswizzle ||
        Args->Transfer.TransferSize == 0u ||
        Args->Transfer.MdlOffset > (MAXULONGLONG >> PAGE_SHIFT))
      return STATUS_INVALID_PARAMETER;
    if (Args->Transfer.Source.SegmentId == 0u)
      mdl = Args->Transfer.Source.pMdl;
    else if (Args->Transfer.Destination.SegmentId == 0u)
      mdl = Args->Transfer.Destination.pMdl;
    else
      mdl = NULL;
    if (mdl == NULL)
      return STATUS_INVALID_PARAMETER;
    systemOffset = (ULONGLONG)Args->Transfer.MdlOffset << PAGE_SHIFT;
    result = AdmissionMemoryPlanTransfer(
        &context->Memory, Args->Transfer.Source.SegmentId,
        (ULONGLONG)Args->Transfer.Source.SegmentAddress.QuadPart,
        Args->Transfer.Destination.SegmentId,
        (ULONGLONG)Args->Transfer.Destination.SegmentAddress.QuadPart,
        Args->Transfer.TransferOffset, systemOffset,
        Args->Transfer.TransferSize, &plan);
    break;
  case DXGK_OPERATION_FILL:
    result = AdmissionMemoryPlanFill(
        &context->Memory, Args->Fill.Destination.SegmentId,
        (ULONGLONG)Args->Fill.Destination.SegmentAddress.QuadPart,
        Args->Fill.FillSize, Args->Fill.FillPattern, &plan);
    mdl = NULL;
    break;
  case DXGK_OPERATION_DISCARD_CONTENT:
    if (Args->DiscardContent.Flags.Reserved != 0u)
      return STATUS_INVALID_PARAMETER;
    result = AdmissionMemoryPlanDiscard(
        &context->Memory, Args->DiscardContent.SegmentId,
        (ULONGLONG)Args->DiscardContent.SegmentAddress.QuadPart, &plan);
    mdl = NULL;
    break;
  default:
    return STATUS_NOT_SUPPORTED;
  }
  if (result == AppleAgxPhysicalPagingOutOfRange)
    return STATUS_INVALID_ADDRESS;
  if (result == AppleAgxPhysicalPagingUnsupportedEndpoint)
    return STATUS_NOT_SUPPORTED;
  if (result != AppleAgxPhysicalPagingOk)
    return STATUS_INVALID_PARAMETER;
  return AdmissionEncodePaging(Args, &plan, mdl);
}
