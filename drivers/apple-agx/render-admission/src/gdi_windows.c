#include "render_admission.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, AdmissionDdiPatch)
#endif

C_ASSERT(sizeof(DXGK_RENDERKM_COMMAND) == 80);
C_ASSERT((UINT)DXGK_GDIOP_COLORFILL == (UINT)AppleAgxGdiColorFill);
C_ASSERT(sizeof(RECT) == sizeof(APPLE_AGX_GDI_RECT));
C_ASSERT(FIELD_OFFSET(DXGK_RENDERKM_COMMAND,
                      Command.ColorFill.NumSubRects) == 28);
C_ASSERT(FIELD_OFFSET(DXGK_RENDERKM_COMMAND, Command.ColorFill) +
             sizeof(DXGK_GDIARG_COLORFILL) ==
         48);

static VOID AdmissionGdiCopyRect(APPLE_AGX_GDI_RECT *Destination,
                                 const RECT *Source) {
  Destination->Left = (APPLE_AGX_U32)Source->left;
  Destination->Top = (APPLE_AGX_U32)Source->top;
  Destination->Right = (APPLE_AGX_U32)Source->right;
  Destination->Bottom = (APPLE_AGX_U32)Source->bottom;
}

static BOOLEAN AdmissionGdiOpenValid(
    const ADMISSION_RENDER_CONTEXT *Context,
    const DXGK_ALLOCATIONLIST *Allocations, UINT AllocationCount,
    UINT Index, BOOLEAN Write) {
  const ADMISSION_OPEN_ALLOCATION *opened;

  if (Context == NULL || Context->Object.Device == NULL ||
      Allocations == NULL || Index >= AllocationCount)
    return FALSE;
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      Allocations[Index].hDeviceSpecificAllocation;
  return opened != NULL &&
         opened->Magic == ADMISSION_OPEN_ALLOCATION_MAGIC &&
         opened->Device != NULL &&
         &opened->Device->Object == Context->Object.Device &&
         opened->Allocation != NULL &&
         AdmissionAllocationDescriptionValid(
             &opened->Allocation->Description) &&
         (!Write || !opened->ReadOnly);
}

static NTSTATUS AdmissionGdiTranslatePatch(
    ADMISSION_CONTEXT *Adapter, const ADMISSION_RENDER_CONTEXT *Context,
    const DXGK_ALLOCATIONLIST *Allocations, UINT AllocationCount,
    const ADMISSION_GDI_PATCH *Patch,
    ADMISSION_LOCAL_MEMORY_VIEW *View) {
  const DXGK_ALLOCATIONLIST *allocation;
  const ADMISSION_OPEN_ALLOCATION *opened;
  ULONGLONG aligned_size;

  if (Adapter == NULL || Patch == NULL || View == NULL ||
      !AdmissionGdiOpenValid(Context, Allocations, AllocationCount,
                             Patch->AllocationIndex, TRUE))
    return STATUS_INVALID_PARAMETER;
  allocation = &Allocations[Patch->AllocationIndex];
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      allocation->hDeviceSpecificAllocation;
  if (allocation->Reserved != 0u ||
      allocation->WriteOperation == 0u ||
      allocation->SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT ||
      allocation->PhysicalAddress.QuadPart <= 0 ||
      !AdmissionAllocationAlign64K(
          opened->Allocation->Description.Size, &aligned_size))
    return STATUS_INVALID_PARAMETER;
  return AdmissionMemoryRuntimeResolveLocal(
      Adapter, (ULONGLONG)allocation->PhysicalAddress.QuadPart,
      aligned_size, 0u, View);
}

static NTSTATUS AdmissionGdiPreparePacket(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    ADMISSION_OPEN_ALLOCATION *Opened, const DXGKARG_PATCH *Args,
    APPLE_AGX_U32 PrivateBytesUsed,
    const ADMISSION_LOCAL_MEMORY_VIEW *Destination) {
  ADMISSION_RENDER_PACKET_DESCRIPTION description;
  KIRQL old_irql;
  BOOLEAN accepted = FALSE;

  if (Adapter == NULL || Context == NULL || Opened == NULL ||
      Args == NULL || Destination == NULL ||
      Destination->CpuAddress == NULL ||
      Destination->GpuVirtualAddress == 0ULL ||
      Destination->HostPhysicalAddress == 0ULL ||
      Destination->Bytes == 0ULL || Destination->Bytes > MAXUINT32)
    return STATUS_INVALID_PARAMETER;

  RtlZeroMemory(&description, sizeof(description));
  description.Fence = Args->SubmissionFenceId;
  description.ContextToken = (ULONGLONG)(ULONG_PTR)Context;
  description.AllocationToken = (ULONGLONG)(ULONG_PTR)Opened;
  description.PrivateDataToken =
      (ULONGLONG)(ULONG_PTR)Args->pDmaBufferPrivateData;
  description.PrivateDataBytes = Args->DmaBufferPrivateDataSize;
  description.PrivateDataStart = 0u;
  description.PrivateDataEnd = PrivateBytesUsed;
  description.DmaStart = Args->DmaBufferSubmissionStartOffset;
  description.DmaEnd = Args->DmaBufferSubmissionEndOffset;
  description.DestinationCpuToken =
      (ULONGLONG)(ULONG_PTR)Destination->CpuAddress;
  description.DestinationGpuVa = Destination->GpuVirtualAddress;
  description.DestinationPhysical =
      Destination->HostPhysicalAddress;
  description.DestinationBytes = (UINT)Destination->Bytes;

  KeAcquireSpinLock(&Adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&Adapter->RenderPacket) ==
          AdmissionRenderPacketEmpty) {
    if (Context->Object.FenceOutstanding == 0u &&
        AdmissionRenderPacketPrepare(
            &Adapter->RenderPacket, &description)) {
      Context->Object.FenceOutstanding = Args->SubmissionFenceId;
      Context->PrepatchedRender.Active = FALSE;
      accepted = TRUE;
    }
  } else if (Context->Object.FenceOutstanding ==
                 Args->SubmissionFenceId &&
             AdmissionRenderPacketMatches(
                 &Adapter->RenderPacket, &description,
                 AdmissionRenderPacketPrepared)) {
    accepted = TRUE;
  }
  KeReleaseSpinLock(&Adapter->SchedulerLock, old_irql);
  return accepted ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
}

_Use_decl_annotations_ NTSTATUS AdmissionGdiAdoptPrepatchedPacket(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    const DXGKARG_SUBMITCOMMAND *Args) {
  ADMISSION_PREPATCHED_RENDER pending;
  APPLE_AGX_DMA_SHADOW shadow;
  DXGKARG_PATCH patchArgs;
  KIRQL oldIrql;
#define PREPATCH_ADOPT_RETURN(guard, value)                                  \
  do {                                                                       \
    NTSTATUS adoptStatus = (value);                                          \
    AdmissionPrepatchAdoptGuardWindows(Adapter, (guard), adoptStatus);       \
    return adoptStatus;                                                      \
  } while (0)

  if (Adapter == NULL || Context == NULL || Args == NULL)
    PREPATCH_ADOPT_RETURN(AdmissionPrepatchAdoptGuardArguments,
                          STATUS_INVALID_PARAMETER);
  KeAcquireSpinLock(&Adapter->SchedulerLock, &oldIrql);
  pending = Context->PrepatchedRender;
  KeReleaseSpinLock(&Adapter->SchedulerLock, oldIrql);
  if (!pending.Active || pending.OpenedAllocation == NULL ||
      pending.PrivateData != Args->pDmaBufferPrivateData ||
      pending.DmaStart != Args->DmaBufferSubmissionStartOffset ||
      pending.DmaEnd != Args->DmaBufferSubmissionEndOffset ||
      pending.PrivateBytesUsed > Args->DmaBufferPrivateDataSize)
    PREPATCH_ADOPT_RETURN(AdmissionPrepatchAdoptGuardPending,
                          STATUS_INVALID_HANDLE);
  if (!AppleAgxDmaShadowOpen(
          &shadow, Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize) ||
      AppleAgxDmaShadowIsSealed(shadow.Storage, shadow.BytesUsed) ||
      shadow.BytesUsed != pending.PrivateBytesUsed ||
      !AppleAgxDmaShadowMatchesWritableU64(
          shadow.Storage, shadow.BytesUsed, pending.PatchOffset,
          pending.Destination.GpuVirtualAddress) ||
      !AppleAgxDmaShadowSeal(&shadow, Args->SubmissionFenceId))
    PREPATCH_ADOPT_RETURN(AdmissionPrepatchAdoptGuardShadow,
                          STATUS_INVALID_USER_BUFFER);
  RtlZeroMemory(&patchArgs, sizeof(patchArgs));
  patchArgs.SubmissionFenceId = Args->SubmissionFenceId;
  patchArgs.pDmaBufferPrivateData = Args->pDmaBufferPrivateData;
  patchArgs.DmaBufferPrivateDataSize = Args->DmaBufferPrivateDataSize;
  patchArgs.DmaBufferSubmissionStartOffset =
      Args->DmaBufferSubmissionStartOffset;
  patchArgs.DmaBufferSubmissionEndOffset =
      Args->DmaBufferSubmissionEndOffset;
  {
    NTSTATUS status = AdmissionGdiPreparePacket(
        Adapter, Context,
        (ADMISSION_OPEN_ALLOCATION *)pending.OpenedAllocation,
        &patchArgs, shadow.BytesUsed, &pending.Destination);
    PREPATCH_ADOPT_RETURN(
        NT_SUCCESS(status) ? AdmissionPrepatchAdoptGuardAccepted
                           : AdmissionPrepatchAdoptGuardPrepare,
        status);
  }
#undef PREPATCH_ADOPT_RETURN
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiRenderKm(
    HANDLE Context, DXGKARG_RENDER *Args) {
  ADMISSION_RENDER_CONTEXT *context = (ADMISSION_RENDER_CONTEXT *)Context;
  const DXGK_RENDERKM_COMMAND *command;
  const RECT *expected_sub_rects;
  const ADMISSION_OPEN_ALLOCATION *opened;
  APPLE_AGX_U32 command_count = 0u;
  APPLE_AGX_U32 required_bytes = 0u;
  ADMISSION_GDI_COLOR_FILL_INPUT input;
  ADMISSION_GDI_PREPARED prepared;
  APPLE_AGX_DMA_SHADOW shadow;
  D3DDDI_PATCHLOCATIONLIST *location;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  ADMISSION_CONTEXT *adapter;
#endif

  if (context == NULL ||
      context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      (context->Object.Flags & ADMISSION_CONTEXT_GDI) == 0u ||
      !context->SchedulerContext.Active || Args == NULL ||
      Args->pCommand == NULL || Args->CommandLength < 48u ||
      Args->pDmaBuffer == NULL || Args->DmaSize == 0u ||
      Args->pDmaBufferPrivateData == NULL ||
      Args->DmaBufferPrivateDataSize != ADMISSION_GDI_DMA_PRIVATE_SIZE ||
      Args->pAllocationList == NULL || Args->AllocationListSize == 0u ||
      Args->pPatchLocationListOut == NULL ||
      Args->PatchLocationListOutSize <
          ADMISSION_GDI_COLOR_FILL_PATCH_COUNT ||
      Args->pPatchLocationListIn != NULL ||
      Args->PatchLocationListInSize != 0u ||
      Args->MultipassOffset != 0u)
    return STATUS_INVALID_PARAMETER;

  command = (const DXGK_RENDERKM_COMMAND *)Args->pCommand;
  if (command->OpCode != DXGK_GDIOP_COLORFILL ||
      command->CommandSize != Args->CommandLength ||
      !AppleAgxGdiValidateCommandStream(
          (const unsigned char *)Args->pCommand, Args->CommandLength,
          &command_count) ||
      command_count != 1u ||
      command->Command.ColorFill.Rop != DXGK_GDIROPCF_PATCOPY ||
      command->Command.ColorFill.Rop3 != 0u ||
      command->Command.ColorFill.NumSubRects >
          (MAXUINT - 48u) / sizeof(RECT) ||
      command->CommandSize !=
          48u + command->Command.ColorFill.NumSubRects * sizeof(RECT) ||
      !AppleAgxGdiDmaRecordBytes(
          command->Command.ColorFill.NumSubRects, &required_bytes) ||
      !AdmissionGdiOpenValid(
          context, Args->pAllocationList, Args->AllocationListSize,
          command->Command.ColorFill.DstAllocationIndex, TRUE))
    return STATUS_NOT_SUPPORTED;

  expected_sub_rects =
      command->Command.ColorFill.NumSubRects == 0u
          ? NULL
          : (const RECT *)((const UCHAR *)command + 48u);
  if (command->Command.ColorFill.pSubRects != expected_sub_rects)
    return STATUS_INVALID_USER_BUFFER;
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      Args->pAllocationList[
          command->Command.ColorFill.DstAllocationIndex]
          .hDeviceSpecificAllocation;

  if (!AppleAgxDmaShadowIsVirgin(
          Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize))
    return STATUS_INVALID_USER_BUFFER;
  if (required_bytes > Args->DmaSize)
    return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

  RtlZeroMemory(&input, sizeof(input));
  AdmissionGdiCopyRect(&input.Destination,
                       &command->Command.ColorFill.DstRect);
  input.DestinationAllocationIndex =
      command->Command.ColorFill.DstAllocationIndex;
  input.AllocationCount = Args->AllocationListSize;
  input.DestinationWritable = 1u;
  input.Color = command->Command.ColorFill.Color;
  input.DestinationPitch =
      opened->Allocation->Description.Pitch;
  input.Rop = (UINT)AppleAgxGdiColorFillPatCopy;
  input.Rop3 = 0u;
  input.SubRectCount = command->Command.ColorFill.NumSubRects;
  input.SubRects = (const APPLE_AGX_GDI_RECT *)expected_sub_rects;
  if (!AdmissionGdiPrepareColorFill(
          &input, 0u, (unsigned char *)Args->pDmaBuffer,
          Args->DmaSize, &prepared)) {
    return STATUS_INVALID_USER_BUFFER;
  }
  AppleAgxDmaShadowInitialize(
      &shadow, Args->pDmaBufferPrivateData,
      Args->DmaBufferPrivateDataSize);
  if (!AppleAgxDmaShadowAppend(
          &shadow, prepared.DmaOffset, Args->pDmaBuffer,
          prepared.DmaBytes)) {
    RtlZeroMemory(Args->pDmaBufferPrivateData,
                  Args->DmaBufferPrivateDataSize);
    return STATUS_INVALID_USER_BUFFER;
  }

  location = Args->pPatchLocationListOut;
  RtlZeroMemory(location, sizeof(*location));
  location->AllocationIndex =
      prepared.Patches[0].AllocationIndex;
  location->SlotId = prepared.Patches[0].SlotId;
  location->AllocationOffset = 0u;
  location->PatchOffset = prepared.Patches[0].PatchOffset;
  location->SplitOffset = prepared.Patches[0].SplitOffset;
  Args->pDmaBuffer =
      (PUCHAR)Args->pDmaBuffer + prepared.DmaBytes;
  Args->pPatchLocationListOut = location + 1;
  --Args->PatchLocationListOutSize;
  Args->MultipassOffset = Args->CommandLength;
#if defined(APPLE_AGX_SUBMIT_QUALIFICATION)
  adapter = CONTAINING_RECORD(context->Object.Device->Adapter,
                              ADMISSION_CONTEXT, ObjectAdapter);
  AdmissionGdiReceiptBeginWindows(adapter,
      (ULONGLONG)(ULONG_PTR)context, (ULONG)command->OpCode,
      command->Command.ColorFill.Color,
      command->Command.ColorFill.NumSubRects, prepared.DmaBytes);
#endif
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionPatchPaging(
    ADMISSION_CONTEXT *Adapter, const DXGKARG_PATCH *Args) {
  const ADMISSION_PAGING_RECORD *records;
  const UCHAR *markers;
  ADMISSION_RENDER_CONTEXT *context;
  UINT privateBytes;
  UINT dmaBytes;
  UINT count;
  UINT index;

  if (!Adapter->Started || Args->Flags.Value != 1u ||
      Args->EngineOrdinal != 0u || Args->SubmissionFenceId == 0u ||
      Args->pDmaBuffer == NULL || Args->pDmaBufferPrivateData == NULL ||
      Args->pAllocationList != NULL || Args->AllocationListSize != 0u ||
      Args->pPatchLocationList != NULL || Args->PatchLocationListSize != 0u ||
      Args->PatchLocationListSubmissionStart != 0u ||
      Args->PatchLocationListSubmissionLength != 0u ||
      Args->DmaBufferSubmissionStartOffset >=
          Args->DmaBufferSubmissionEndOffset ||
      Args->DmaBufferSubmissionEndOffset > Args->DmaBufferSize ||
      Args->DmaBufferPrivateDataSubmissionStartOffset >=
          Args->DmaBufferPrivateDataSubmissionEndOffset ||
      Args->DmaBufferPrivateDataSubmissionEndOffset >
          Args->DmaBufferPrivateDataSize ||
      Args->DmaBufferSubmissionStartOffset % sizeof(ADMISSION_PAGING_MARKER) != 0u ||
      Args->DmaBufferPrivateDataSubmissionStartOffset %
          sizeof(ADMISSION_PAGING_RECORD) != 0u ||
      ((ULONG_PTR)Args->pDmaBufferPrivateData & (sizeof(void *) - 1u)) != 0u)
    return STATUS_INVALID_PARAMETER;
  /* Paging during power transitions may have no context. A supplied context
     must belong to this adapter, but is not required to be a GDI context. */
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context != NULL &&
      (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
       context->Object.Device == NULL ||
       context->Object.Device->Adapter != &Adapter->ObjectAdapter))
    return STATUS_INVALID_HANDLE;
  privateBytes = Args->DmaBufferPrivateDataSubmissionEndOffset -
                 Args->DmaBufferPrivateDataSubmissionStartOffset;
  dmaBytes = Args->DmaBufferSubmissionEndOffset -
             Args->DmaBufferSubmissionStartOffset;
  if (privateBytes % sizeof(ADMISSION_PAGING_RECORD) != 0u)
    return STATUS_INVALID_PARAMETER;
  count = (UINT)(privateBytes / sizeof(ADMISSION_PAGING_RECORD));
  records = (const ADMISSION_PAGING_RECORD *)(
      (const UCHAR *)Args->pDmaBufferPrivateData +
      Args->DmaBufferPrivateDataSubmissionStartOffset);
  if (!AdmissionPagingRecordsValid(records, count,
                                   ADMISSION_MAX_PAGING_RECORDS, dmaBytes))
    return STATUS_INVALID_PARAMETER;
  markers = (const UCHAR *)Args->pDmaBuffer +
            Args->DmaBufferSubmissionStartOffset;
  for (index = 0u; index < count; ++index) {
    if (RtlCompareMemory(markers + index * sizeof(ADMISSION_PAGING_MARKER),
                         &records[index].Header,
                         sizeof(ADMISSION_PAGING_MARKER)) !=
        sizeof(ADMISSION_PAGING_MARKER))
      return STATUS_INVALID_PARAMETER;
  }
  /* BuildPagingBuffer already resolved the plan. These markers contain no
     relocatable GPU addresses; the existing Submit/worker owns execution. */
  return STATUS_SUCCESS;
}

_Use_decl_annotations_ NTSTATUS AdmissionDdiPatch(
    HANDLE Adapter, const DXGKARG_PATCH *Args) {
  ADMISSION_CONTEXT *adapter = (ADMISSION_CONTEXT *)Adapter;
  ADMISSION_RENDER_CONTEXT *context;
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  ADMISSION_GDI_PREPARED prepared;
  ADMISSION_GDI_PATCH patch;
  const D3DDDI_PATCHLOCATIONLIST *location;
  ADMISSION_OPEN_ALLOCATION *opened;
  BOOLEAN sealed;
  ADMISSION_LOCAL_MEMORY_VIEW destination;

#define PATCH_RENDER_RETURN(guard, value)                                    \
  do {                                                                       \
    NTSTATUS patchStatus = (value);                                          \
    AdmissionPatchRenderGuardWindows(adapter, (guard), patchStatus);         \
    return patchStatus;                                                      \
  } while (0)

  PAGED_CODE();
  if (adapter != NULL && Args != NULL && Args->Flags.Paging)
    return AdmissionPatchPaging(adapter, Args);
  if (adapter != NULL && Args != NULL &&
      AdmissionPresentIsBltPrivate(Args->pDmaBufferPrivateData, Args->DmaBufferPrivateDataSize))
    return AdmissionPresentPatch(adapter, Args);
  if (adapter == NULL || Args == NULL || Args->hContext == NULL ||
      Args->pDmaBuffer == NULL || Args->DmaBufferSize == 0u ||
      Args->pDmaBufferPrivateData == NULL ||
      Args->DmaBufferPrivateDataSize != ADMISSION_GDI_DMA_PRIVATE_SIZE ||
      Args->pAllocationList == NULL || Args->AllocationListSize == 0u ||
      Args->pPatchLocationList == NULL ||
      Args->PatchLocationListSubmissionLength !=
          ADMISSION_GDI_COLOR_FILL_PATCH_COUNT ||
      Args->PatchLocationListSubmissionStart >=
          Args->PatchLocationListSize ||
      Args->DmaBufferSubmissionStartOffset >=
          Args->DmaBufferSubmissionEndOffset ||
      Args->DmaBufferSubmissionEndOffset > Args->DmaBufferSize ||
      Args->DmaBufferPrivateDataSubmissionStartOffset != 0u ||
      Args->DmaBufferPrivateDataSubmissionEndOffset >
          Args->DmaBufferPrivateDataSize ||
      Args->Flags.Value != 0u || Args->EngineOrdinal != 0u ||
      Args->SubmissionFenceId == 0u)
    PATCH_RENDER_RETURN(AdmissionPatchRenderGuardArguments,
                        STATUS_INVALID_PARAMETER);
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter ||
      (context->Object.Flags & ADMISSION_CONTEXT_SYSTEM) != 0u ||
      !context->SchedulerContext.Active)
    PATCH_RENDER_RETURN(AdmissionPatchRenderGuardContext,
                        STATUS_INVALID_HANDLE);
  if (!AppleAgxDmaShadowOpen(
          &shadow, Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize) ||
      !AdmissionNonPagingPrivateRangeCovers(
          shadow.BytesUsed,
          Args->DmaBufferPrivateDataSubmissionStartOffset,
          Args->DmaBufferPrivateDataSubmissionEndOffset,
          Args->DmaBufferPrivateDataSize) ||
      !AppleAgxDmaShadowFind(
          shadow.Storage, shadow.BytesUsed,
          Args->DmaBufferSubmissionStartOffset,
          Args->DmaBufferSubmissionEndOffset -
              Args->DmaBufferSubmissionStartOffset,
          &view) ||
      !AdmissionGdiDescribePreparedRecord(
          view.Bytes, view.DmaBytes, view.DmaOffset, &prepared))
    PATCH_RENDER_RETURN(AdmissionPatchRenderGuardShadow,
                        STATUS_INVALID_USER_BUFFER);

  location =
      &Args->pPatchLocationList[
          Args->PatchLocationListSubmissionStart];
  patch.AllocationIndex = location->AllocationIndex;
  patch.SlotId = location->SlotId;
  patch.PatchOffset = location->PatchOffset;
  patch.SplitOffset = location->SplitOffset;
  if (location->Reserved != 0u || location->DriverId != 0u ||
      location->AllocationOffset != 0u ||
      !AdmissionGdiPatchAuthorized(
          &prepared, &patch,
          Args->DmaBufferSubmissionStartOffset,
          Args->DmaBufferSubmissionEndOffset) ||
      Args->DmaBufferSize < sizeof(destination.GpuVirtualAddress) ||
      patch.PatchOffset >
          Args->DmaBufferSize - sizeof(destination.GpuVirtualAddress))
    PATCH_RENDER_RETURN(AdmissionPatchRenderGuardLocation,
                        STATUS_INVALID_PARAMETER);
  if (!NT_SUCCESS(AdmissionGdiTranslatePatch(
          adapter, context, Args->pAllocationList,
          Args->AllocationListSize, &patch, &destination)))
    PATCH_RENDER_RETURN(AdmissionPatchRenderGuardTranslate,
                        STATUS_INVALID_ADDRESS);
  opened = (ADMISSION_OPEN_ALLOCATION *)
      Args->pAllocationList[patch.AllocationIndex]
          .hDeviceSpecificAllocation;

  sealed = AppleAgxDmaShadowIsSealed(
               shadow.Storage, shadow.BytesUsed)
               ? TRUE
               : FALSE;
  if (sealed) {
    if (!AppleAgxDmaShadowIsSealedForFence(
            shadow.Storage, shadow.BytesUsed,
            Args->SubmissionFenceId) ||
        !AppleAgxDmaShadowMatchesU64(
            shadow.Storage, shadow.BytesUsed,
            patch.PatchOffset, destination.GpuVirtualAddress))
      PATCH_RENDER_RETURN(AdmissionPatchRenderGuardSeal,
                          STATUS_INVALID_DEVICE_STATE);
  } else {
    if (!AppleAgxDmaShadowPatchU64(
            shadow.Storage, shadow.BytesUsed,
            patch.PatchOffset, destination.GpuVirtualAddress) ||
        !AppleAgxDmaShadowSeal(&shadow, Args->SubmissionFenceId))
      PATCH_RENDER_RETURN(AdmissionPatchRenderGuardSeal,
                          STATUS_INVALID_DEVICE_STATE);
  }
  if (!NT_SUCCESS(AdmissionGdiPreparePacket(
          adapter, context, opened, Args, shadow.BytesUsed,
          &destination)))
    PATCH_RENDER_RETURN(AdmissionPatchRenderGuardPrepare,
                        STATUS_DEVICE_BUSY);
  AdmissionGdiReceiptPatchWindows(adapter,
      (ULONGLONG)(ULONG_PTR)context, Args->SubmissionFenceId,
      destination.GpuVirtualAddress, destination.HostPhysicalAddress,
      (ULONG)destination.Bytes);
  RtlCopyMemory(
      (PUCHAR)Args->pDmaBuffer + patch.PatchOffset,
      &destination.GpuVirtualAddress,
      sizeof(destination.GpuVirtualAddress));
  PATCH_RENDER_RETURN(AdmissionPatchRenderGuardAccepted, STATUS_SUCCESS);
#undef PATCH_RENDER_RETURN
}
