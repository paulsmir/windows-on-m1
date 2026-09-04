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
    const ADMISSION_GDI_PATCH *Patch, ULONGLONG *GpuVa) {
  const DXGK_ALLOCATIONLIST *allocation;
  const ADMISSION_OPEN_ALLOCATION *opened;
  ULONGLONG aligned_size;

  if (Adapter == NULL || Patch == NULL || GpuVa == NULL ||
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
          opened->Allocation->Description.Size, &aligned_size) ||
      AdmissionMemoryLocalAddressToGpuVa(
          &Adapter->Memory, allocation->SegmentId,
          (ULONGLONG)allocation->PhysicalAddress.QuadPart, aligned_size,
          0u, GpuVa) != AppleAgxLocalSegmentAddressOk)
    return STATUS_INVALID_PARAMETER;
  return STATUS_SUCCESS;
}

static NTSTATUS AdmissionGdiPreparePacket(
    ADMISSION_CONTEXT *Adapter, ADMISSION_RENDER_CONTEXT *Context,
    ADMISSION_OPEN_ALLOCATION *Opened, const DXGKARG_PATCH *Args,
    APPLE_AGX_U32 PrivateBytesUsed) {
  ADMISSION_RENDER_PACKET_DESCRIPTION description;
  KIRQL old_irql;
  BOOLEAN accepted = FALSE;

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

  KeAcquireSpinLock(&Adapter->SchedulerLock, &old_irql);
  if (AdmissionRenderPacketState(&Adapter->RenderPacket) ==
          AdmissionRenderPacketEmpty) {
    if (Context->Object.FenceOutstanding == 0u &&
        AdmissionRenderPacketPrepare(
            &Adapter->RenderPacket, &description)) {
      Context->Object.FenceOutstanding = Args->SubmissionFenceId;
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
  ULONGLONG gpu_va;

  PAGED_CODE();
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
    return STATUS_INVALID_PARAMETER;
  context = (ADMISSION_RENDER_CONTEXT *)Args->hContext;
  if (context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Adapter != &adapter->ObjectAdapter ||
      (context->Object.Flags & ADMISSION_CONTEXT_GDI) == 0u ||
      !context->SchedulerContext.Active)
    return STATUS_INVALID_HANDLE;
  if (!AppleAgxDmaShadowOpen(
          &shadow, Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize) ||
      Args->DmaBufferPrivateDataSubmissionEndOffset < shadow.BytesUsed ||
      !AppleAgxDmaShadowFind(
          shadow.Storage, shadow.BytesUsed,
          Args->DmaBufferSubmissionStartOffset,
          Args->DmaBufferSubmissionEndOffset -
              Args->DmaBufferSubmissionStartOffset,
          &view) ||
      !AdmissionGdiDescribePreparedRecord(
          view.Bytes, view.DmaBytes, view.DmaOffset, &prepared))
    return STATUS_INVALID_USER_BUFFER;

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
      Args->DmaBufferSize < sizeof(gpu_va) ||
      patch.PatchOffset > Args->DmaBufferSize - sizeof(gpu_va))
    return STATUS_INVALID_PARAMETER;
  if (!NT_SUCCESS(AdmissionGdiTranslatePatch(
          adapter, context, Args->pAllocationList,
          Args->AllocationListSize, &patch, &gpu_va)))
    return STATUS_INVALID_ADDRESS;
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
            patch.PatchOffset, gpu_va))
      return STATUS_INVALID_DEVICE_STATE;
  } else {
    if (!AppleAgxDmaShadowPatchU64(
            shadow.Storage, shadow.BytesUsed,
            patch.PatchOffset, gpu_va) ||
        !AppleAgxDmaShadowSeal(&shadow, Args->SubmissionFenceId))
      return STATUS_INVALID_DEVICE_STATE;
  }
  if (!NT_SUCCESS(AdmissionGdiPreparePacket(
          adapter, context, opened, Args, shadow.BytesUsed)))
    return STATUS_DEVICE_BUSY;
  RtlCopyMemory(
      (PUCHAR)Args->pDmaBuffer + patch.PatchOffset,
      &gpu_va, sizeof(gpu_va));
  return STATUS_SUCCESS;
}
