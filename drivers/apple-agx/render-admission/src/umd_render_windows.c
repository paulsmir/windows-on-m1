#include "render_admission.h"

C_ASSERT(sizeof(ADMISSION_UMD_COLOR_FILL_COMMAND) == 48u);

static BOOLEAN AdmissionUmdRenderOpenValid(
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

_Use_decl_annotations_ NTSTATUS AdmissionDdiRender(
    HANDLE Context, DXGKARG_RENDER *Args) {
  ADMISSION_RENDER_CONTEXT *context = (ADMISSION_RENDER_CONTEXT *)Context;
  ADMISSION_CONTEXT *adapter;
  ADMISSION_UMD_COLOR_FILL_COMMAND command;
  const ADMISSION_OPEN_ALLOCATION *opened;
  ADMISSION_GDI_COLOR_FILL_INPUT input;
  ADMISSION_GDI_PREPARED prepared;
  APPLE_AGX_DMA_SHADOW shadow;
  D3DDDI_PATCHLOCATIONLIST *location;

  if (context == NULL ||
      context->Object.Magic != ADMISSION_OBJECT_CONTEXT_MAGIC ||
      context->Object.Device == NULL ||
      context->Object.Device->Magic != ADMISSION_OBJECT_DEVICE_MAGIC ||
      (context->Object.Flags & ADMISSION_CONTEXT_SYSTEM) != 0u ||
      !context->SchedulerContext.Active || Args == NULL ||
      Args->pCommand == NULL || Args->CommandLength != sizeof(command) ||
      Args->pDmaBuffer == NULL || Args->DmaSize == 0u ||
      Args->pDmaBufferPrivateData == NULL ||
      Args->DmaBufferPrivateDataSize != ADMISSION_GDI_DMA_PRIVATE_SIZE ||
      Args->pAllocationList == NULL || Args->AllocationListSize == 0u ||
      Args->pPatchLocationListIn != NULL ||
      Args->PatchLocationListInSize != 0u ||
      Args->pPatchLocationListOut == NULL ||
      Args->PatchLocationListOutSize <
          ADMISSION_GDI_COLOR_FILL_PATCH_COUNT ||
      Args->MultipassOffset != 0u)
    return STATUS_INVALID_PARAMETER;

  __try {
    RtlCopyMemory(&command, Args->pCommand, sizeof(command));
  }
  __except(EXCEPTION_EXECUTE_HANDLER) {
    return STATUS_INVALID_USER_BUFFER;
  }

  if (!AdmissionUmdColorFillCommandValid(
          &command, Args->AllocationListSize) ||
      !AdmissionUmdRenderOpenValid(
          context, Args->pAllocationList, Args->AllocationListSize,
          command.DestinationAllocationIndex, TRUE))
    return STATUS_INVALID_USER_BUFFER;
  opened = (const ADMISSION_OPEN_ALLOCATION *)
      Args->pAllocationList[command.DestinationAllocationIndex]
          .hDeviceSpecificAllocation;
  if (command.Destination.Right >
          opened->Allocation->Description.Width ||
      command.Destination.Bottom >
          opened->Allocation->Description.Height)
    return STATUS_INVALID_USER_BUFFER;
  if (!AppleAgxDmaShadowIsVirgin(
          Args->pDmaBufferPrivateData,
          Args->DmaBufferPrivateDataSize))
    return STATUS_INVALID_USER_BUFFER;

  RtlZeroMemory(&input, sizeof(input));
  input.Destination.Left = command.Destination.Left;
  input.Destination.Top = command.Destination.Top;
  input.Destination.Right = command.Destination.Right;
  input.Destination.Bottom = command.Destination.Bottom;
  input.DestinationAllocationIndex =
      command.DestinationAllocationIndex;
  input.AllocationCount = Args->AllocationListSize;
  input.DestinationWritable = 1u;
  input.Color = command.Color;
  input.DestinationPitch = opened->Allocation->Description.Pitch;
  input.Rop = (UINT)AppleAgxGdiColorFillPatCopy;
  input.Rop3 = 0u;
  if (!AdmissionGdiPrepareColorFill(
          &input, 0u, (unsigned char *)Args->pDmaBuffer,
          Args->DmaSize, &prepared))
    return STATUS_INVALID_USER_BUFFER;

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
  location->AllocationIndex = prepared.Patches[0].AllocationIndex;
  location->SlotId = prepared.Patches[0].SlotId;
  location->AllocationOffset = 0u;
  location->PatchOffset = prepared.Patches[0].PatchOffset;
  location->SplitOffset = prepared.Patches[0].SplitOffset;
  Args->pDmaBuffer = (PUCHAR)Args->pDmaBuffer + prepared.DmaBytes;
  Args->pPatchLocationListOut = location + 1;
  --Args->PatchLocationListOutSize;
  Args->MultipassOffset = sizeof(command);

  adapter = CONTAINING_RECORD(context->Object.Device->Adapter,
                              ADMISSION_CONTEXT, ObjectAdapter);
  AdmissionGdiReceiptBeginWindows(
      adapter, (ULONGLONG)(ULONG_PTR)context, command.Opcode,
      command.Color, 0u, prepared.DmaBytes);
  return STATUS_SUCCESS;
}
