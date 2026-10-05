#include "render_admission.h"

C_ASSERT(sizeof(ADMISSION_PRESENT_BLT_COMMAND) == 168);
C_ASSERT(sizeof(RECT) == sizeof(APPLE_AGX_GDI_RECT));
C_ASSERT(sizeof(DXGK_PRESENTALLOCATIONINFO) == 32);
C_ASSERT(FIELD_OFFSET(DXGK_PRESENTALLOCATIONINFO, AllocationVirtualAddress) == 8);

static ADMISSION_OPEN_ALLOCATION *AdmissionPresentOpen(
    ADMISSION_DEVICE *Device, const DXGK_ALLOCATIONLIST *List,
    UINT Index, BOOLEAN Write, ADMISSION_PRESENT_OPEN_ENDPOINT *Evidence) {
  ADMISSION_OPEN_ALLOCATION *opened =
      (ADMISSION_OPEN_ALLOCATION *)List[Index].hDeviceSpecificAllocation;
  if (Evidence != NULL) {
    RtlZeroMemory(Evidence, sizeof(*Evidence));
    Evidence->Index = Index;
    Evidence->Handle = (unsigned long long)(ULONG_PTR)opened;
    Evidence->Segment = List[Index].SegmentId;
    Evidence->WriteOperation = List[Index].WriteOperation;
    Evidence->GpuVirtualAddress = List[Index].VirtualAddress;
  }
  if (opened == NULL) {
    if (Evidence != NULL) Evidence->Reason = ADMISSION_PRESENT_OPEN_NULL_HANDLE;
    return NULL;
  }
  if (Evidence != NULL) Evidence->OpenedMagic = opened->Magic;
  if (opened->Magic != ADMISSION_OPEN_ALLOCATION_MAGIC) {
    if (Evidence != NULL) Evidence->Reason = ADMISSION_PRESENT_OPEN_BAD_MAGIC;
    return NULL;
  }
  if (Evidence != NULL)
    Evidence->OpenedDevice = (unsigned long long)(ULONG_PTR)opened->Device;
  if (opened->Device != Device) {
    if (Evidence != NULL) Evidence->Reason = ADMISSION_PRESENT_OPEN_FOREIGN_DEVICE;
    return NULL;
  }
  if (Evidence != NULL)
    Evidence->Allocation = (unsigned long long)(ULONG_PTR)opened->Allocation;
  if (opened->Allocation == NULL) {
    if (Evidence != NULL) Evidence->Reason = ADMISSION_PRESENT_OPEN_NULL_ALLOCATION;
    return NULL;
  }
  if (Evidence != NULL) Evidence->AllocationMagic = opened->Allocation->Magic;
  if (opened->Allocation->Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC) {
    if (Evidence != NULL) Evidence->Reason = ADMISSION_PRESENT_OPEN_BAD_ALLOCATION_MAGIC;
    return NULL;
  }
  if (Evidence != NULL)
    Evidence->DescriptionMagic = opened->Allocation->Description.Magic;
  if (!AdmissionAllocationDescriptionValid(&opened->Allocation->Description)) {
    if (Evidence != NULL) Evidence->Reason = ADMISSION_PRESENT_OPEN_BAD_DESCRIPTION;
    return NULL;
  }
  if (Evidence != NULL) Evidence->ReadOnly = opened->ReadOnly ? 1u : 0u;
  if (Write && opened->ReadOnly) {
    if (Evidence != NULL) Evidence->Reason = ADMISSION_PRESENT_OPEN_READ_ONLY;
    return NULL;
  }
  return opened;
}

_Use_decl_annotations_ NTSTATUS AdmissionPresentBlt(
    ADMISSION_DEVICE *Device, HANDLE Context, DXGKARG_PRESENT *Present) {
  ADMISSION_PRESENT_BLT_INPUT input;
  ADMISSION_OPEN_ALLOCATION *source, *destination;
  ADMISSION_PRESENT_OPEN_ENDPOINT sourceEvidence, destinationEvidence;
  DXGK_ALLOCATIONLIST virtualList[3];
  const DXGK_ALLOCATIONLIST *allocationList;
  APPLE_AGX_DMA_SHADOW shadow;
  D3DDDI_PATCHLOCATIONLIST *patch;
  BOOLEAN virtualAddressing;
  UINT capacity, bytes, next;
  virtualAddressing = Context != NULL &&
      ((ADMISSION_RENDER_CONTEXT *)Context)->Object.Magic ==
          ADMISSION_OBJECT_CONTEXT_MAGIC &&
      (((ADMISSION_RENDER_CONTEXT *)Context)->Object.Flags &
       ADMISSION_CONTEXT_VIRTUAL_ADDRESSING) != 0u;
  if (Device == NULL || Context == NULL || Present == NULL ||
      Present->Flags.Value != 1u || Present->pDmaBuffer == NULL ||
      Present->pDmaBufferPrivateData == NULL || Present->pAllocationList == NULL ||
      (!virtualAddressing &&
       (Present->pPatchLocationListOut == NULL ||
        Present->PatchLocationListOutSize < 2u)) ||
      Present->pPrivateDriverData != NULL || Present->PrivateDriverDataSize != 0u ||
      Present->pDstSubRects == NULL || Present->SubRectCnt == 0u ||
      Present->DmaBufferPrivateDataSize <=
          sizeof(APPLE_AGX_DMA_SHADOW_HEADER) + sizeof(APPLE_AGX_DMA_SHADOW_RECORD))
    return STATUS_INVALID_PARAMETER;
  allocationList = Present->pAllocationList;
  if (virtualAddressing) {
    if (Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].SegmentId != 0u ||
        Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX].SegmentId != 0u ||
        Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].PhysicalAdapterIndex != 0u ||
        Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX].PhysicalAdapterIndex != 0u)
      return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(virtualList, sizeof(virtualList));
    virtualList[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation =
        Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation;
    virtualList[DXGK_PRESENT_SOURCE_INDEX].VirtualAddress =
        Present->pAllocationInfo[DXGK_PRESENT_SOURCE_INDEX].AllocationVirtualAddress;
    virtualList[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation =
        Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation;
    virtualList[DXGK_PRESENT_DESTINATION_INDEX].VirtualAddress =
        Present->pAllocationInfo[DXGK_PRESENT_DESTINATION_INDEX].AllocationVirtualAddress;
    virtualList[DXGK_PRESENT_DESTINATION_INDEX].WriteOperation = 1u;
    allocationList = virtualList;
  }
  source = AdmissionPresentOpen(Device, allocationList, 1u, FALSE,
                                &sourceEvidence);
  destination = AdmissionPresentOpen(Device, allocationList, 2u, TRUE,
                                     &destinationEvidence);
  if (source == NULL || destination == NULL) {
    AdmissionRecordPresentOpenFailure(Device, Context, Present,
                                      &sourceEvidence, &destinationEvidence);
    return STATUS_INVALID_HANDLE;
  }
  RtlZeroMemory(&input, sizeof(input));
  input.Command.SourceDescription = source->Allocation->Description;
  input.Command.DestinationDescription = destination->Allocation->Description;
  input.Command.ContextToken = (ULONGLONG)(ULONG_PTR)Context;
  if (virtualAddressing) {
    input.Command.Version = ADMISSION_PRESENT_BLT_GPUVA_VERSION;
    input.Command.SourceLocation =
        allocationList[DXGK_PRESENT_SOURCE_INDEX].VirtualAddress;
    input.Command.DestinationLocation =
        allocationList[DXGK_PRESENT_DESTINATION_INDEX].VirtualAddress;
  }
  input.SameAllocation = source->Allocation == destination->Allocation;
  RtlCopyMemory(&input.Command.SourceRect, &Present->SrcRect, sizeof(RECT));
  RtlCopyMemory(&input.Command.DestinationRect, &Present->DstRect,
                sizeof(RECT));
  input.Rects = (const APPLE_AGX_GDI_RECT *)Present->pDstSubRects;
  input.RectCount = Present->SubRectCnt;
  input.MultipassOffset = Present->MultipassOffset;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  AdmissionDwmFrameRecordBlt(CONTAINING_RECORD(Device->Object.Adapter,
      ADMISSION_CONTEXT, ObjectAdapter), Context,
      (ULONGLONG)source->RuntimeAllocation,
      (ULONGLONG)destination->RuntimeAllocation,
      input.Command.SourceLocation, input.Command.DestinationLocation);
#endif
  if (!virtualAddressing && Present->pAllocationList[1].SegmentId != 0u &&
      !AdmissionPresentLocationEncode(Present->pAllocationList[1].SegmentId,
          (ULONGLONG)Present->pAllocationList[1].PhysicalAddress.QuadPart,
          &input.Command.SourceLocation))
    return STATUS_INVALID_ADDRESS;
  if (!virtualAddressing && Present->pAllocationList[2].SegmentId != 0u &&
      (Present->pAllocationList[2].SegmentId != 2u ||
       !AdmissionPresentLocationEncode(2u,
          (ULONGLONG)Present->pAllocationList[2].PhysicalAddress.QuadPart,
          &input.Command.DestinationLocation)))
    return STATUS_INVALID_ADDRESS;
  capacity = Present->DmaBufferPrivateDataSize -
      (UINT)sizeof(APPLE_AGX_DMA_SHADOW_HEADER) - (UINT)sizeof(APPLE_AGX_DMA_SHADOW_RECORD);
  if (capacity > Present->DmaSize)
    capacity = Present->DmaSize;
  if (capacity < sizeof(ADMISSION_PRESENT_BLT_COMMAND) + sizeof(RECT))
    return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
  if (!AdmissionPresentBltEncode(&input, Present->pDmaBuffer, capacity, &bytes, &next))
    return STATUS_INVALID_PARAMETER;
  AppleAgxDmaShadowInitialize(&shadow, Present->pDmaBufferPrivateData,
                             Present->DmaBufferPrivateDataSize);
  if (!AppleAgxDmaShadowAppend(&shadow, 0u, Present->pDmaBuffer, bytes))
    return STATUS_INVALID_USER_BUFFER;
  if (!virtualAddressing) {
    patch = Present->pPatchLocationListOut;
    RtlZeroMemory(patch, 2u * sizeof(*patch));
    patch[0].AllocationIndex = 1u;
    patch[0].SlotId = 0u;
    patch[0].PatchOffset = FIELD_OFFSET(ADMISSION_PRESENT_BLT_COMMAND, SourceLocation);
    patch[1].AllocationIndex = 2u;
    patch[1].SlotId = 1u;
    patch[1].PatchOffset = FIELD_OFFSET(ADMISSION_PRESENT_BLT_COMMAND, DestinationLocation);
    Present->pPatchLocationListOut += 2u;
    Present->PatchLocationListOutSize -= 2u;
  }
  Present->pDmaBuffer = (PUCHAR)Present->pDmaBuffer + bytes;
  Present->MultipassOffset = next;
  return next == input.RectCount
             ? STATUS_SUCCESS
             : STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
}

static BOOLEAN AdmissionPresentPrivateView(PVOID Data, UINT Bytes,
    APPLE_AGX_DMA_SHADOW *Shadow, APPLE_AGX_DMA_SHADOW_VIEW *View,
    ADMISSION_PRESENT_BLT_COMMAND *Command, ULONG *Stage) {
  UINT extent;
  if (Stage != NULL)
    *Stage = AdmissionPresentPrivateMissing;
  if (Data == NULL)
    return FALSE;
  if (Stage != NULL)
    *Stage = AdmissionPresentPrivateShadow;
  if (!AppleAgxDmaShadowOpen(Shadow, Data, Bytes))
    return FALSE;
  if (Stage != NULL)
    *Stage = AdmissionPresentPrivateExtent;
  if (!AppleAgxDmaShadowExtent(Data, Shadow->BytesUsed, &extent))
    return FALSE;
  if (Stage != NULL)
    *Stage = AdmissionPresentPrivateRecord;
  if (!AppleAgxDmaShadowFind(Data, Shadow->BytesUsed, 0u, extent, View))
    return FALSE;
  if (Stage != NULL)
    *Stage = AdmissionPresentPrivateCommand;
  if (!AdmissionPresentBltValidate(View->Bytes, View->DmaBytes, 0, Command))
    return FALSE;
  if (Stage != NULL)
    *Stage = AdmissionPresentPrivateValid;
  return TRUE;
}

_Use_decl_annotations_ BOOLEAN AdmissionPresentIsBltPrivate(PVOID Data, UINT Bytes) {
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_PRESENT_BLT_COMMAND command;
  return AdmissionPresentPrivateView(Data, Bytes, &shadow, &view, &command, NULL);
}

_Use_decl_annotations_ ULONG AdmissionPresentPrivateStage(
    PVOID Data, UINT Bytes, ADMISSION_PRESENT_BLT_COMMAND *Command) {
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_PRESENT_BLT_COMMAND local;
  ULONG stage = AdmissionPresentPrivateMissing;
  RtlZeroMemory(&local, sizeof(local));
  (void)AdmissionPresentPrivateView(Data, Bytes, &shadow, &view, &local, &stage);
  if (Command != NULL)
    *Command = local;
  return stage;
}

_Use_decl_annotations_ NTSTATUS AdmissionPresentPatch(
    ADMISSION_CONTEXT *Context, const DXGKARG_PATCH *Args) {
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_PRESENT_BLT_COMMAND command;
  ADMISSION_RENDER_CONTEXT *render;
  ADMISSION_DEVICE *device;
  ADMISSION_OPEN_ALLOCATION *opened;
  const D3DDDI_PATCHLOCATIONLIST *patch;
  ULONGLONG locations[2];
  UINT index, offset;
  if (Context == NULL || Args == NULL || Args->hContext == NULL ||
      Args->Flags.Value != 2u || Args->EngineOrdinal != 0u ||
      Args->pDmaBuffer == NULL || Args->pAllocationList == NULL ||
      Args->AllocationListSize < 3u || Args->pPatchLocationList == NULL ||
      Args->PatchLocationListSubmissionLength != 2u ||
      Args->PatchLocationListSubmissionStart > Args->PatchLocationListSize ||
      2u > Args->PatchLocationListSize - Args->PatchLocationListSubmissionStart ||
      !AdmissionPresentPrivateView(Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize, &shadow, &view, &command, NULL) ||
      command.Version != ADMISSION_PRESENT_BLT_VERSION ||
      command.ContextToken != (ULONGLONG)(ULONG_PTR)Args->hContext ||
      Args->DmaBufferSubmissionStartOffset != 0u ||
      Args->DmaBufferSubmissionEndOffset != view.DmaBytes ||
      view.DmaBytes > Args->DmaBufferSize ||
      !AdmissionNonPagingPrivateRangeCovers(shadow.BytesUsed,
          Args->DmaBufferPrivateDataSubmissionStartOffset,
          Args->DmaBufferPrivateDataSubmissionEndOffset,
          Args->DmaBufferPrivateDataSize))
    return STATUS_INVALID_PARAMETER;
  render = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (render->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      render->Object.Device == NULL || render->Object.Device->Adapter != &Context->ObjectAdapter)
    return STATUS_INVALID_HANDLE;
  device = CONTAINING_RECORD(render->Object.Device, ADMISSION_DEVICE, Object);
  for (index = 0u; index < 2u; ++index) {
    patch = &Args->pPatchLocationList[Args->PatchLocationListSubmissionStart + index];
    offset = index == 0u ? FIELD_OFFSET(ADMISSION_PRESENT_BLT_COMMAND, SourceLocation) :
                           FIELD_OFFSET(ADMISSION_PRESENT_BLT_COMMAND, DestinationLocation);
    opened = AdmissionPresentOpen(device, Args->pAllocationList, index + 1u,
                                  index != 0u, NULL);
    if (opened == NULL || patch->AllocationIndex != index + 1u || patch->SlotId != index ||
        patch->AllocationOffset != 0u || patch->PatchOffset != offset || patch->SplitOffset != 0u ||
        RtlCompareMemory(&opened->Allocation->Description,
            index == 0u ? &command.SourceDescription : &command.DestinationDescription,
            sizeof(ADMISSION_ALLOCATION_DESCRIPTION)) != sizeof(ADMISSION_ALLOCATION_DESCRIPTION) ||
        (index == 1u && Args->pAllocationList[2].SegmentId != 2u) ||
        !AdmissionPresentLocationEncode(Args->pAllocationList[index + 1u].SegmentId,
            (ULONGLONG)Args->pAllocationList[index + 1u].PhysicalAddress.QuadPart, &locations[index]))
      return STATUS_INVALID_PARAMETER;
  }
  /* Validate the entire pair before updating either endpoint. The private
   * shadow stays patchable; Submit takes its own immutable command snapshot. */
  for (index = 0u; index < 2u; ++index) {
    offset = index == 0u ? FIELD_OFFSET(ADMISSION_PRESENT_BLT_COMMAND, SourceLocation) :
                           FIELD_OFFSET(ADMISSION_PRESENT_BLT_COMMAND, DestinationLocation);
    if (!AppleAgxDmaShadowPatchU64(shadow.Storage, shadow.BytesUsed, offset, locations[index]))
      return STATUS_INVALID_USER_BUFFER;
    RtlCopyMemory((PUCHAR)Args->pDmaBuffer + offset, &locations[index], sizeof(locations[index]));
  }
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionPresentSubmitTraced(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args,
    BOOLEAN Trace) {
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_PRESENT_BLT_COMMAND command;
  ADMISSION_RENDER_CONTEXT *render;
  ULONG guard = AdmissionPresentSubmitAccepted;
#if !defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  (void)Trace;
#endif
#define PRESENT_GUARD(condition, value)                                      \
  do {                                                                       \
    if (condition) {                                                         \
      guard = (value);                                                       \
      AdmissionSubmitTraceValueWindows(Context, Trace,                       \
          AdmissionSubmitTracePresentGuard, guard);                          \
      return STATUS_INVALID_PARAMETER;                                       \
    }                                                                        \
  } while (0)
  if (Context == NULL || Args == NULL)
    return STATUS_INVALID_PARAMETER;
  PRESENT_GUARD(!Context->Started, AdmissionPresentSubmitNotStarted);
  PRESENT_GUARD(Args->hContext == NULL, AdmissionPresentSubmitContextMissing);
  PRESENT_GUARD((Args->Flags.Value & ~0x82u) != 0u,
                AdmissionPresentSubmitFlags);
  PRESENT_GUARD(!Args->Flags.Present, AdmissionPresentSubmitPresentFlag);
  PRESENT_GUARD(Args->SubmissionFenceId == 0u, AdmissionPresentSubmitFence);
  PRESENT_GUARD(Args->NodeOrdinal != 0u, AdmissionPresentSubmitNode);
  PRESENT_GUARD(Args->EngineOrdinal != 0u, AdmissionPresentSubmitEngine);
  PRESENT_GUARD(!AdmissionPresentPrivateView(Args->pDmaBufferPrivateData,
      Args->DmaBufferPrivateDataSize, &shadow, &view, &command, NULL),
      AdmissionPresentSubmitPrivate);
  PRESENT_GUARD(!AdmissionPresentBltValidate(view.Bytes, view.DmaBytes, 1,
      &command), AdmissionPresentSubmitResidency);
  PRESENT_GUARD(command.ContextToken != (ULONGLONG)(ULONG_PTR)Args->hContext,
                AdmissionPresentSubmitContextToken);
  PRESENT_GUARD(Args->DmaBufferSubmissionStartOffset != 0u,
                AdmissionPresentSubmitDmaStart);
  PRESENT_GUARD(Args->DmaBufferSubmissionEndOffset != view.DmaBytes,
                AdmissionPresentSubmitDmaEnd);
  PRESENT_GUARD(view.DmaBytes > Args->DmaBufferSize,
                AdmissionPresentSubmitDmaSize);
  PRESENT_GUARD(Args->DmaBufferPrivateDataSubmissionStartOffset != 0u,
                AdmissionPresentSubmitPrivateStart);
  PRESENT_GUARD(Args->DmaBufferPrivateDataSubmissionEndOffset != 0u &&
      Args->DmaBufferPrivateDataSubmissionEndOffset < shadow.BytesUsed,
                AdmissionPresentSubmitPrivateEndLow);
  PRESENT_GUARD(Args->DmaBufferPrivateDataSubmissionEndOffset >
      Args->DmaBufferPrivateDataSize, AdmissionPresentSubmitPrivateEndHigh);
  render = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (render->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC) {
    guard = AdmissionPresentSubmitObject;
    goto InvalidHandle;
  }
  if (render->Object.Device == NULL) {
    guard = AdmissionPresentSubmitDevice;
    goto InvalidHandle;
  }
  if (render->Object.Device->Adapter != &Context->ObjectAdapter) {
    guard = AdmissionPresentSubmitAdapter;
    goto InvalidHandle;
  }
  if (!render->SchedulerContext.Active) {
    guard = AdmissionPresentSubmitInactive;
    goto InvalidHandle;
  }
  /* Binding is deliberately at Submit: legal prepatch can skip Patch. */
  AdmissionSubmitTraceValueWindows(Context, Trace,
      AdmissionSubmitTracePresentGuard, AdmissionPresentSubmitAccepted);
  return AdmissionPagingSubmitPresent(Context, Args, view.Bytes, view.DmaBytes);
InvalidHandle:
  AdmissionSubmitTraceValueWindows(Context, Trace,
      AdmissionSubmitTracePresentGuard, guard);
  return STATUS_INVALID_HANDLE;
#undef PRESENT_GUARD
}

_Use_decl_annotations_ NTSTATUS AdmissionPresentSubmit(
    ADMISSION_CONTEXT *Context, const DXGKARG_SUBMITCOMMAND *Args) {
  return AdmissionPresentSubmitTraced(Context, Args, FALSE);
}

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
_Use_decl_annotations_ NTSTATUS AdmissionPresentSubmitVirtual(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    const DXGKARG_SUBMITCOMMANDVIRTUAL *Args) {
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_PRESENT_BLT_COMMAND command;
  DXGKARG_SUBMITCOMMAND submit;
  if (Adapter == NULL || Context == NULL || Args == NULL ||
      (Context->Object.Flags & ADMISSION_CONTEXT_VIRTUAL_ADDRESSING) == 0u ||
      Context->GpuvaG3Process == NULL || Context->GpuvaG3Poisoned ||
      (Args->Flags.Value & ~0x82u) != 0u || !Args->Flags.Present ||
      Args->DmaBufferUmdPrivateDataSize != 0u ||
      Args->DmaBufferVirtualAddress < 0x4000ULL ||
      Args->DmaBufferVirtualAddress >= (1ULL << 39) ||
      !AdmissionPresentPrivateView(Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize, &shadow, &view, &command, NULL) ||
      command.Version != ADMISSION_PRESENT_BLT_GPUVA_VERSION)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&submit, sizeof(submit));
  submit.hContext = Args->hContext;
  submit.Flags.Value = Args->Flags.Value;
  submit.NodeOrdinal = Args->NodeOrdinal;
  submit.EngineOrdinal = Args->EngineOrdinal;
  submit.SubmissionFenceId = Args->SubmissionFenceId;
  submit.pDmaBufferPrivateData = Args->pDmaBufferPrivateData;
  submit.DmaBufferPrivateDataSize = Args->DmaBufferPrivateDataSize;
  submit.DmaBufferSize = Args->DmaBufferSize;
  submit.DmaBufferSubmissionEndOffset = view.DmaBytes;
  submit.DmaBufferPrivateDataSubmissionEndOffset = shadow.BytesUsed;
  return AdmissionPresentSubmit(Adapter, &submit);
}
#endif
