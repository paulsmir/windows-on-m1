#include "render_gdi.h"

#include <stddef.h>

#define ADMISSION_GDI_NULL ((void *)0)

static int AdmissionGdiRectValid(const APPLE_AGX_GDI_RECT *Rect) {
  return Rect != ADMISSION_GDI_NULL && Rect->Left < Rect->Right &&
         Rect->Top < Rect->Bottom;
}

static int AdmissionGdiRectContains(const APPLE_AGX_GDI_RECT *Outer,
                                    const APPLE_AGX_GDI_RECT *Inner) {
  return AdmissionGdiRectValid(Outer) && AdmissionGdiRectValid(Inner) &&
         Inner->Left >= Outer->Left && Inner->Top >= Outer->Top &&
         Inner->Right <= Outer->Right && Inner->Bottom <= Outer->Bottom;
}

int AdmissionGdiPrepareColorFill(
    const ADMISSION_GDI_COLOR_FILL_INPUT *Input,
    unsigned int DmaOffset, unsigned char *DmaBuffer,
    unsigned int DmaCapacity, ADMISSION_GDI_PREPARED *Prepared) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  ADMISSION_GDI_PREPARED candidate;
  unsigned int record_bytes = 0u;
  unsigned int written = 0u;
  unsigned int index;

  if (Input == ADMISSION_GDI_NULL || DmaBuffer == ADMISSION_GDI_NULL ||
      Prepared == ADMISSION_GDI_NULL ||
      !AdmissionGdiRectValid(&Input->Destination) ||
      Input->AllocationCount == 0u ||
      Input->DestinationAllocationIndex >= Input->AllocationCount ||
      Input->DestinationWritable != 1u ||
      Input->Rop != (unsigned int)AppleAgxGdiColorFillPatCopy ||
      Input->Rop3 != 0u ||
      (Input->SubRectCount != 0u &&
       Input->SubRects == ADMISSION_GDI_NULL) ||
      !AppleAgxGdiDmaRecordBytes(Input->SubRectCount, &record_bytes) ||
      record_bytes > DmaCapacity || DmaOffset > ~0u - record_bytes)
    return 0;

  for (index = 0u; index < Input->SubRectCount; ++index) {
    if (!AdmissionGdiRectContains(&Input->Destination,
                                  &Input->SubRects[index]))
      return 0;
  }

  description.Command =
      (APPLE_AGX_GDI_DMA_COMMAND){0};
  description.Command.Opcode = (unsigned int)AppleAgxGdiColorFill;
  description.Command.SubRectCount = Input->SubRectCount;
  description.Command.Destination = Input->Destination;
  description.Command.DestinationAllocationIndex =
      Input->DestinationAllocationIndex;
  description.Command.Color = Input->Color;
  description.Command.Rop = Input->Rop;
  description.Command.Rop3 = Input->Rop3;
  description.SubRects = Input->SubRects;

  candidate = (ADMISSION_GDI_PREPARED){0};
  candidate.Magic = ADMISSION_GDI_PREPARED_MAGIC;
  candidate.Version = ADMISSION_GDI_PREPARED_VERSION;
  candidate.DmaOffset = DmaOffset;
  candidate.DmaBytes = record_bytes;
  candidate.PatchCount = ADMISSION_GDI_COLOR_FILL_PATCH_COUNT;
  candidate.Patches[0].AllocationIndex =
      Input->DestinationAllocationIndex;
  candidate.Patches[0].SlotId = ADMISSION_GDI_DESTINATION_SLOT;
  candidate.Patches[0].PatchOffset =
      DmaOffset +
      (unsigned int)offsetof(APPLE_AGX_GDI_DMA_COMMAND,
                             DestinationGpuAddress);
  candidate.Patches[0].SplitOffset = DmaOffset;

  if (!AppleAgxGdiEncodeDmaCommand(
          &description, DmaBuffer, DmaCapacity, &written) ||
      written != record_bytes)
    return 0;
  *Prepared = candidate;
  return 1;
}

int AdmissionGdiPatchAuthorized(
    const ADMISSION_GDI_PREPARED *Prepared,
    const ADMISSION_GDI_PATCH *Patch,
    unsigned int SubmissionStart, unsigned int SubmissionEnd) {
  unsigned int prepared_end;
  const ADMISSION_GDI_PATCH *expected;

  if (Prepared == ADMISSION_GDI_NULL || Patch == ADMISSION_GDI_NULL ||
      Prepared->Magic != ADMISSION_GDI_PREPARED_MAGIC ||
      Prepared->Version != ADMISSION_GDI_PREPARED_VERSION ||
      Prepared->PatchCount != ADMISSION_GDI_COLOR_FILL_PATCH_COUNT ||
      Prepared->DmaBytes == 0u ||
      Prepared->DmaOffset > ~0u - Prepared->DmaBytes)
    return 0;
  prepared_end = Prepared->DmaOffset + Prepared->DmaBytes;
  if (SubmissionStart != Prepared->DmaOffset ||
      SubmissionEnd != prepared_end)
    return 0;
  expected = &Prepared->Patches[0];
  return Patch->AllocationIndex == expected->AllocationIndex &&
         Patch->SlotId == expected->SlotId &&
         Patch->PatchOffset == expected->PatchOffset &&
         Patch->SplitOffset == expected->SplitOffset;
}

int AdmissionGdiDescribePreparedRecord(
    const unsigned char *DmaBuffer, unsigned int DmaBytes,
    unsigned int DmaOffset, ADMISSION_GDI_PREPARED *Prepared) {
  APPLE_AGX_GDI_DMA_COMMAND command;
  ADMISSION_GDI_PREPARED candidate;
  unsigned int command_count = 0u;
  unsigned int expected_bytes = 0u;
  unsigned int index;

  if (DmaBuffer == ADMISSION_GDI_NULL || Prepared == ADMISSION_GDI_NULL ||
      DmaBytes < (unsigned int)sizeof(command) ||
      DmaOffset > ~0u - DmaBytes ||
      !AppleAgxGdiValidateDmaStream(
          DmaBuffer, DmaBytes, &command_count) ||
      command_count != 1u)
    return 0;
  command = *(const APPLE_AGX_GDI_DMA_COMMAND *)DmaBuffer;
  if (command.Opcode != (unsigned int)AppleAgxGdiColorFill ||
      command.Rop != (unsigned int)AppleAgxGdiColorFillPatCopy ||
      command.Rop3 != 0u || command.DestinationGpuAddress != 0ULL ||
      !AdmissionGdiRectValid(&command.Destination) ||
      !AppleAgxGdiDmaRecordBytes(command.SubRectCount, &expected_bytes) ||
      expected_bytes != DmaBytes)
    return 0;
  for (index = 0u; index < command.SubRectCount; ++index) {
    const APPLE_AGX_GDI_RECT *sub_rect =
        (const APPLE_AGX_GDI_RECT *)(
            DmaBuffer + sizeof(command) +
            index * (unsigned int)sizeof(*sub_rect));
    if (!AdmissionGdiRectContains(&command.Destination, sub_rect))
      return 0;
  }
  candidate = (ADMISSION_GDI_PREPARED){0};
  candidate.Magic = ADMISSION_GDI_PREPARED_MAGIC;
  candidate.Version = ADMISSION_GDI_PREPARED_VERSION;
  candidate.DmaOffset = DmaOffset;
  candidate.DmaBytes = DmaBytes;
  candidate.PatchCount = ADMISSION_GDI_COLOR_FILL_PATCH_COUNT;
  candidate.Patches[0].AllocationIndex =
      command.DestinationAllocationIndex;
  candidate.Patches[0].SlotId = ADMISSION_GDI_DESTINATION_SLOT;
  candidate.Patches[0].PatchOffset =
      DmaOffset +
      (unsigned int)offsetof(APPLE_AGX_GDI_DMA_COMMAND,
                             DestinationGpuAddress);
  candidate.Patches[0].SplitOffset = DmaOffset;
  *Prepared = candidate;
  return 1;
}
