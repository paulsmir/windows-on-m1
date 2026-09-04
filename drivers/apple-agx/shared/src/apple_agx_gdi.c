#include "apple_agx_gdi.h"

#define APPLE_AGX_GDI_NULL ((void *)0)

void AppleAgxGdiMinimumCapsProfile(APPLE_AGX_GDI_CAPS_PROFILE *Profile) {
  if (Profile == APPLE_AGX_GDI_NULL)
    return;
  Profile->OpcodeMask = APPLE_AGX_GDI_MINIMUM_OPCODE_MASK;
  Profile->BitBltRopMask = APPLE_AGX_GDI_MINIMUM_BITBLT_ROP_MASK;
  Profile->ColorFillRopMask = APPLE_AGX_GDI_MINIMUM_COLORFILL_ROP_MASK;
  Profile->ExcludedVariantMask =
      APPLE_AGX_GDI_EXCLUDE_ALL_SAME_BITMAP_VARIANTS;
  Profile->SupportKernelModeCommandBuffer = APPLE_AGX_TRUE;
  Profile->CacheCoherentAperture = APPLE_AGX_TRUE;
  Profile->NoCacheCoherentApertureMemory = APPLE_AGX_FALSE;
  Profile->SupportAllBltRops = APPLE_AGX_FALSE;
  Profile->SupportMirrorStretchBlt = APPLE_AGX_FALSE;
  Profile->SupportMonoStretchBltModes = APPLE_AGX_FALSE;
  Profile->NoTempSurfaceForClearTypeBlend = APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxGdiCapsProfileValid(
    const APPLE_AGX_GDI_CAPS_PROFILE *Profile) {
  return Profile != APPLE_AGX_GDI_NULL &&
                 Profile->OpcodeMask == APPLE_AGX_GDI_MINIMUM_OPCODE_MASK &&
                 Profile->BitBltRopMask ==
                     APPLE_AGX_GDI_MINIMUM_BITBLT_ROP_MASK &&
                 Profile->ColorFillRopMask ==
                     APPLE_AGX_GDI_MINIMUM_COLORFILL_ROP_MASK &&
                 Profile->ExcludedVariantMask ==
                     APPLE_AGX_GDI_EXCLUDE_ALL_SAME_BITMAP_VARIANTS &&
                 Profile->SupportKernelModeCommandBuffer ==
                     APPLE_AGX_TRUE &&
                 Profile->CacheCoherentAperture == APPLE_AGX_TRUE &&
                 Profile->NoCacheCoherentApertureMemory == APPLE_AGX_FALSE &&
                 Profile->SupportAllBltRops == APPLE_AGX_FALSE &&
                 Profile->SupportMirrorStretchBlt == APPLE_AGX_FALSE &&
                 Profile->SupportMonoStretchBltModes == APPLE_AGX_FALSE &&
                 Profile->NoTempSurfaceForClearTypeBlend == APPLE_AGX_FALSE
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxGdiCapsSupportsOperation(
    const APPLE_AGX_GDI_CAPS_PROFILE *Profile, APPLE_AGX_U32 Opcode,
    APPLE_AGX_U32 Rop) {
  APPLE_AGX_U32 ropMask;

  if (!AppleAgxGdiCapsProfileValid(Profile) || Opcode >= 32u ||
      (Profile->OpcodeMask & APPLE_AGX_GDI_OPCODE_BIT(Opcode)) == 0u)
    return APPLE_AGX_FALSE;
  if (Rop == 0u)
    return APPLE_AGX_TRUE;
  if (Rop >= 32u)
    return APPLE_AGX_FALSE;
  if (Opcode == (APPLE_AGX_U32)AppleAgxGdiBitBlt)
    ropMask = Profile->BitBltRopMask;
  else if (Opcode == (APPLE_AGX_U32)AppleAgxGdiColorFill)
    ropMask = Profile->ColorFillRopMask;
  else
    return APPLE_AGX_FALSE;
  return (ropMask & (1u << Rop)) != 0u ? APPLE_AGX_TRUE
                                       : APPLE_AGX_FALSE;
}

#define APPLE_AGX_GDI_U32_MAX 0xffffffffu

static APPLE_AGX_U32 AppleAgxGdiReadU32(const unsigned char *Bytes) {
  return ((APPLE_AGX_U32)Bytes[0]) |
         ((APPLE_AGX_U32)Bytes[1] << 8) |
         ((APPLE_AGX_U32)Bytes[2] << 16) |
         ((APPLE_AGX_U32)Bytes[3] << 24);
}

static void AppleAgxGdiCopyBytes(unsigned char *Destination,
                                 const unsigned char *Source,
                                 APPLE_AGX_U32 ByteCount) {
  APPLE_AGX_U32 index;

  for (index = 0; index < ByteCount; ++index)
    Destination[index] = Source[index];
}

static APPLE_AGX_BOOL AppleAgxGdiOpcodeIsDefined(APPLE_AGX_U32 Opcode) {
  return Opcode >= (APPLE_AGX_U32)AppleAgxGdiBitBlt &&
         Opcode <= (APPLE_AGX_U32)AppleAgxGdiClearTypeBlend;
}

/*
 * Fixed sizes and NumSubRects offsets from the ARM64 WDDM 3.0
 * DXGK_RENDERKM_COMMAND ABI.  The Windows wrapper has compile-time assertions
 * that bind these portable checks to the pinned WDK structures.
 */
static APPLE_AGX_BOOL AppleAgxGdiCommandLayout(
    APPLE_AGX_U32 Opcode, APPLE_AGX_U32 *MinimumBytes,
    APPLE_AGX_U32 *SubRectCountOffset) {
  if (MinimumBytes == APPLE_AGX_GDI_NULL ||
      SubRectCountOffset == APPLE_AGX_GDI_NULL)
    return APPLE_AGX_FALSE;

  switch ((APPLE_AGX_GDI_OPCODE)Opcode) {
  case AppleAgxGdiBitBlt:
    *MinimumBytes = 80u;
    *SubRectCountOffset = 48u;
    return APPLE_AGX_TRUE;
  case AppleAgxGdiColorFill:
    *MinimumBytes = 48u;
    *SubRectCountOffset = 28u;
    return APPLE_AGX_TRUE;
  case AppleAgxGdiAlphaBlend:
  case AppleAgxGdiStretchBlt:
    *MinimumBytes = 72u;
    *SubRectCountOffset = 48u;
    return APPLE_AGX_TRUE;
  case AppleAgxGdiEscape:
    *MinimumBytes = APPLE_AGX_GDI_COMMAND_HEADER_SIZE;
    *SubRectCountOffset = 0u;
    return APPLE_AGX_TRUE;
  case AppleAgxGdiTransparentBlt:
    *MinimumBytes = 72u;
    *SubRectCountOffset = 52u;
    return APPLE_AGX_TRUE;
  case AppleAgxGdiClearTypeBlend:
    *MinimumBytes = 80u;
    *SubRectCountOffset = 56u;
    return APPLE_AGX_TRUE;
  default:
    return APPLE_AGX_FALSE;
  }
}

APPLE_AGX_BOOL AppleAgxGdiDescribeSurface(
    APPLE_AGX_U32 Width, APPLE_AGX_U32 Height,
    APPLE_AGX_U32 BytesPerPixel, APPLE_AGX_U32 AlignmentShift,
    APPLE_AGX_GDI_SURFACE *Surface) {
  APPLE_AGX_U64 rawPitch;
  APPLE_AGX_U64 alignment;
  APPLE_AGX_U64 pitch;
  APPLE_AGX_U64 size;

  if (Surface == APPLE_AGX_GDI_NULL || Width == 0 || Height == 0 ||
      BytesPerPixel == 0 || AlignmentShift >= 32)
    return APPLE_AGX_FALSE;

  alignment = (APPLE_AGX_U64)1u << AlignmentShift;
  rawPitch = (APPLE_AGX_U64)Width * BytesPerPixel;
  pitch = (rawPitch + alignment - 1u) & ~(alignment - 1u);
  if (pitch == 0 || pitch > APPLE_AGX_GDI_U32_MAX)
    return APPLE_AGX_FALSE;
  size = pitch * Height;
  if (size == 0 || size / Height != pitch)
    return APPLE_AGX_FALSE;

  Surface->Width = Width;
  Surface->Height = Height;
  Surface->BytesPerPixel = BytesPerPixel;
  Surface->Pitch = (APPLE_AGX_U32)pitch;
  Surface->Size = size;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxGdiValidateCommandStream(
    const unsigned char *Commands, APPLE_AGX_U32 CommandBytes,
    APPLE_AGX_U32 *CommandCount) {
  APPLE_AGX_U32 offset = 0;
  APPLE_AGX_U32 count = 0;

  if (Commands == APPLE_AGX_GDI_NULL || CommandCount == APPLE_AGX_GDI_NULL ||
      CommandBytes == 0)
    return APPLE_AGX_FALSE;

  while (offset < CommandBytes) {
    APPLE_AGX_U32 remaining = CommandBytes - offset;
    APPLE_AGX_U32 opcode;
    APPLE_AGX_U32 commandSize;
    APPLE_AGX_U32 minimumBytes;
    APPLE_AGX_U32 subRectCountOffset;
    APPLE_AGX_U32 subRectCount;

    if (remaining < APPLE_AGX_GDI_COMMAND_HEADER_SIZE)
      return APPLE_AGX_FALSE;
    opcode = AppleAgxGdiReadU32(Commands + offset);
    commandSize = AppleAgxGdiReadU32(Commands + offset + 4u);
    if (!AppleAgxGdiOpcodeIsDefined(opcode) ||
        !AppleAgxGdiCommandLayout(opcode, &minimumBytes,
                                  &subRectCountOffset) ||
        commandSize < minimumBytes || commandSize > remaining)
      return APPLE_AGX_FALSE;
    if (subRectCountOffset != 0u) {
      subRectCount = AppleAgxGdiReadU32(Commands + offset +
                                       subRectCountOffset);
      if (subRectCount > (commandSize - minimumBytes) / 16u)
        return APPLE_AGX_FALSE;
    }
    offset += commandSize;
    ++count;
  }

  *CommandCount = count;
  return count != 0 ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxGdiDmaRecordBytes(
    APPLE_AGX_U32 SubRectCount, APPLE_AGX_U32 *RecordBytes) {
  APPLE_AGX_U32 fixedBytes =
      (APPLE_AGX_U32)sizeof(APPLE_AGX_GDI_DMA_COMMAND);
  APPLE_AGX_U32 rectBytes = (APPLE_AGX_U32)sizeof(APPLE_AGX_GDI_RECT);

  if (RecordBytes == APPLE_AGX_GDI_NULL)
    return APPLE_AGX_FALSE;
  *RecordBytes = 0u;
  if (SubRectCount > (APPLE_AGX_GDI_U32_MAX - fixedBytes) / rectBytes)
    return APPLE_AGX_FALSE;
  *RecordBytes = fixedBytes + SubRectCount * rectBytes;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxGdiEncodeDmaCommand(
    const APPLE_AGX_GDI_COMMAND_DESCRIPTION *Description,
    unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_U32 *BytesWritten) {
  APPLE_AGX_GDI_DMA_COMMAND command;
  APPLE_AGX_U32 recordBytes;
  APPLE_AGX_U32 subRectBytes;

  if (Description == APPLE_AGX_GDI_NULL ||
      DmaBuffer == APPLE_AGX_GDI_NULL ||
      BytesWritten == APPLE_AGX_GDI_NULL)
    return APPLE_AGX_FALSE;
  if (!AppleAgxGdiDmaRecordBytes(Description->Command.SubRectCount,
                                 &recordBytes))
    return APPLE_AGX_FALSE;
  *BytesWritten = recordBytes;
  if (!AppleAgxGdiOpcodeIsDefined(Description->Command.Opcode) ||
      (Description->Command.Opcode == (APPLE_AGX_U32)AppleAgxGdiEscape &&
       Description->Command.SubRectCount != 0u) ||
      (Description->Command.SubRectCount != 0u &&
       Description->SubRects == APPLE_AGX_GDI_NULL) ||
      DmaBytes < recordBytes)
    return APPLE_AGX_FALSE;

  command = Description->Command;
  command.Magic = APPLE_AGX_GDI_DMA_MAGIC;
  command.Version = APPLE_AGX_GDI_DMA_VERSION;
  command.RecordBytes = recordBytes;
  command.Reserved = 0u;
  AppleAgxGdiCopyBytes(DmaBuffer, (const unsigned char *)&command,
                       (APPLE_AGX_U32)sizeof(command));
  subRectBytes = Description->Command.SubRectCount *
                 (APPLE_AGX_U32)sizeof(APPLE_AGX_GDI_RECT);
  if (subRectBytes != 0u)
    AppleAgxGdiCopyBytes(DmaBuffer + sizeof(command),
                         (const unsigned char *)Description->SubRects,
                         subRectBytes);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxGdiValidateDmaStream(
    const unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_U32 *CommandCount) {
  APPLE_AGX_U32 offset = 0u;
  APPLE_AGX_U32 count = 0u;

  if (DmaBuffer == APPLE_AGX_GDI_NULL || CommandCount == APPLE_AGX_GDI_NULL ||
      DmaBytes == 0u)
    return APPLE_AGX_FALSE;
  while (offset < DmaBytes) {
    APPLE_AGX_GDI_DMA_COMMAND command;
    APPLE_AGX_U32 expectedBytes;
    APPLE_AGX_U32 remaining = DmaBytes - offset;

    if (remaining < sizeof(command))
      return APPLE_AGX_FALSE;
    AppleAgxGdiCopyBytes((unsigned char *)&command, DmaBuffer + offset,
                         (APPLE_AGX_U32)sizeof(command));
    if (command.Magic != APPLE_AGX_GDI_DMA_MAGIC ||
        command.Version != APPLE_AGX_GDI_DMA_VERSION ||
        command.Reserved != 0u ||
        !AppleAgxGdiOpcodeIsDefined(command.Opcode) ||
        !AppleAgxGdiDmaRecordBytes(command.SubRectCount, &expectedBytes) ||
        command.RecordBytes != expectedBytes || command.RecordBytes > remaining ||
        (command.Opcode == (APPLE_AGX_U32)AppleAgxGdiEscape &&
         command.SubRectCount != 0u))
      return APPLE_AGX_FALSE;
    offset += command.RecordBytes;
    ++count;
  }
  *CommandCount = count;
  return count != 0u ? APPLE_AGX_TRUE : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL AppleAgxGdiRectValid(
    const APPLE_AGX_GDI_RECT *Rect) {
  return Rect != APPLE_AGX_GDI_NULL && Rect->Left < Rect->Right &&
                 Rect->Top < Rect->Bottom
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL AppleAgxGdiRectContains(
    const APPLE_AGX_GDI_RECT *Outer, const APPLE_AGX_GDI_RECT *Inner) {
  return AppleAgxGdiRectValid(Outer) && AppleAgxGdiRectValid(Inner) &&
                 Inner->Left >= Outer->Left && Inner->Top >= Outer->Top &&
                 Inner->Right <= Outer->Right &&
                 Inner->Bottom <= Outer->Bottom
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL AppleAgxGdiCommandPrimitiveMask(
    const APPLE_AGX_GDI_DMA_COMMAND *Command,
    APPLE_AGX_U32 *RequiredPrimitiveMask) {
  APPLE_AGX_U32 required;

  if (Command == APPLE_AGX_GDI_NULL ||
      RequiredPrimitiveMask == APPLE_AGX_GDI_NULL ||
      !AppleAgxGdiRectValid(&Command->Destination) ||
      Command->DestinationGpuAddress == 0ULL)
    return APPLE_AGX_FALSE;

  required = APPLE_AGX_GDI_PRIMITIVE_DESTINATION_WRITE;
  switch ((APPLE_AGX_GDI_OPCODE)Command->Opcode) {
  case AppleAgxGdiBitBlt:
    if (!AppleAgxGdiRectValid(&Command->Source) ||
        Command->SourceGpuAddress == 0ULL ||
        Command->SourceAllocationIndex ==
            Command->DestinationAllocationIndex ||
        Command->SourceGpuAddress == Command->DestinationGpuAddress ||
        Command->Rop < (APPLE_AGX_U32)AppleAgxGdiBitBltSrcCopy ||
        Command->Rop > (APPLE_AGX_U32)AppleAgxGdiBitBltSrcOr)
      return APPLE_AGX_FALSE;
    required |= APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ;
    if (Command->Rop != (APPLE_AGX_U32)AppleAgxGdiBitBltSrcCopy)
      required |= APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ |
                  APPLE_AGX_GDI_PRIMITIVE_BOOLEAN_ROP;
    break;
  case AppleAgxGdiColorFill:
    if (Command->Rop < (APPLE_AGX_U32)AppleAgxGdiColorFillPatCopy ||
        Command->Rop > (APPLE_AGX_U32)AppleAgxGdiColorFillPatOr)
      return APPLE_AGX_FALSE;
    if (Command->Rop != (APPLE_AGX_U32)AppleAgxGdiColorFillPatCopy)
      required |= APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ |
                  APPLE_AGX_GDI_PRIMITIVE_BOOLEAN_ROP;
    break;
  case AppleAgxGdiAlphaBlend:
    if (!AppleAgxGdiRectValid(&Command->Source) ||
        Command->SourceGpuAddress == 0ULL ||
        Command->SourceAllocationIndex ==
            Command->DestinationAllocationIndex ||
        Command->SourceGpuAddress == Command->DestinationGpuAddress ||
        Command->SourceHasAlpha > 1u)
      return APPLE_AGX_FALSE;
    required |= APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ |
                APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ |
                APPLE_AGX_GDI_PRIMITIVE_SOURCE_ALPHA_BLEND;
    break;
  case AppleAgxGdiStretchBlt:
    if (!AppleAgxGdiRectValid(&Command->Source) ||
        Command->SourceGpuAddress == 0ULL ||
        Command->SourceAllocationIndex ==
            Command->DestinationAllocationIndex ||
        Command->SourceGpuAddress == Command->DestinationGpuAddress ||
        Command->Flags != 3u)
      return APPLE_AGX_FALSE;
    required |= APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ |
                APPLE_AGX_GDI_PRIMITIVE_NEAREST_SAMPLE;
    break;
  case AppleAgxGdiTransparentBlt:
    if (!AppleAgxGdiRectValid(&Command->Source) ||
        Command->SourceGpuAddress == 0ULL ||
        Command->SourceAllocationIndex ==
            Command->DestinationAllocationIndex ||
        Command->SourceGpuAddress == Command->DestinationGpuAddress ||
        (Command->Flags & ~1u) != 0u)
      return APPLE_AGX_FALSE;
    required |= APPLE_AGX_GDI_PRIMITIVE_SOURCE_READ |
                APPLE_AGX_GDI_PRIMITIVE_COLOR_KEY;
    if ((Command->Flags & 1u) != 0u)
      required |= APPLE_AGX_GDI_PRIMITIVE_SOURCE_ALPHA_BLEND;
    break;
  case AppleAgxGdiClearTypeBlend:
    if (Command->TemporaryGpuAddress == 0ULL ||
        Command->GammaGpuAddress == 0ULL ||
        Command->AlphaGpuAddress == 0ULL)
      return APPLE_AGX_FALSE;
    required |= APPLE_AGX_GDI_PRIMITIVE_DESTINATION_READ |
                APPLE_AGX_GDI_PRIMITIVE_CLEARTYPE |
                APPLE_AGX_GDI_PRIMITIVE_AUXILIARY_SURFACES;
    break;
  case AppleAgxGdiEscape:
  default:
    return APPLE_AGX_FALSE;
  }

  *RequiredPrimitiveMask = required;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxGdiBuildLoweringReceipt(
    const unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_GDI_LOWERING_RECEIPT *Receipt) {
  APPLE_AGX_GDI_LOWERING_RECEIPT candidate = {0};
  APPLE_AGX_U32 offset = 0u;
  APPLE_AGX_U32 index;

  if (DmaBuffer == APPLE_AGX_GDI_NULL || Receipt == APPLE_AGX_GDI_NULL ||
      DmaBytes == 0u)
    return APPLE_AGX_FALSE;

  candidate.StreamHash = 14695981039346656037ULL;
  candidate.StreamBytes = DmaBytes;
  for (index = 0u; index < DmaBytes; ++index) {
    candidate.StreamHash ^= DmaBuffer[index];
    candidate.StreamHash *= 1099511628211ULL;
  }

  while (offset < DmaBytes) {
    APPLE_AGX_GDI_DMA_COMMAND command;
    APPLE_AGX_U32 expected_bytes;
    APPLE_AGX_U32 required;
    APPLE_AGX_U32 sub_rect_index;
    APPLE_AGX_U32 remaining = DmaBytes - offset;

    if (remaining < (APPLE_AGX_U32)sizeof(command))
      return APPLE_AGX_FALSE;
    AppleAgxGdiCopyBytes((unsigned char *)&command, DmaBuffer + offset,
                         (APPLE_AGX_U32)sizeof(command));
    if (command.Magic != APPLE_AGX_GDI_DMA_MAGIC ||
        command.Version != APPLE_AGX_GDI_DMA_VERSION ||
        command.Reserved != 0u ||
        command.Opcode >= 32u ||
        !AppleAgxGdiDmaRecordBytes(command.SubRectCount, &expected_bytes) ||
        command.RecordBytes != expected_bytes ||
        command.RecordBytes > remaining ||
        !AppleAgxGdiCommandPrimitiveMask(&command, &required))
      return APPLE_AGX_FALSE;

    for (sub_rect_index = 0u; sub_rect_index < command.SubRectCount;
         ++sub_rect_index) {
      APPLE_AGX_GDI_RECT sub_rect;
      APPLE_AGX_U32 sub_rect_offset =
          offset + (APPLE_AGX_U32)sizeof(command) +
          sub_rect_index * (APPLE_AGX_U32)sizeof(sub_rect);
      AppleAgxGdiCopyBytes((unsigned char *)&sub_rect,
                           DmaBuffer + sub_rect_offset,
                           (APPLE_AGX_U32)sizeof(sub_rect));
      if (!AppleAgxGdiRectContains(&command.Destination, &sub_rect))
        return APPLE_AGX_FALSE;
    }

    candidate.OperationMask |= APPLE_AGX_GDI_OPCODE_BIT(command.Opcode);
    candidate.RequiredPrimitiveMask |= required;
    ++candidate.CommandCount;
    offset += command.RecordBytes;
  }

  if (candidate.CommandCount == 0u)
    return APPLE_AGX_FALSE;
  *Receipt = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxGdiMemoryPoolCreate(
    const APPLE_AGX_MEMORY_IO *Io, APPLE_AGX_U64 Length,
    APPLE_AGX_GDI_MEMORY_POOL *Pool) {
  APPLE_AGX_U64 index;

  if (Pool == APPLE_AGX_GDI_NULL || Pool->Active != APPLE_AGX_FALSE ||
      Pool->Object.State != AppleAgxMemoryEmpty ||
      Length < APPLE_AGX_GDI_MEMORY_ALIGNMENT ||
      (Length & (APPLE_AGX_GDI_MEMORY_ALIGNMENT - 1ULL)) != 0ULL)
    return APPLE_AGX_FALSE;

  if (AppleAgxMemoryAllocateAligned(Io, Length,
                                    APPLE_AGX_GDI_MEMORY_ALIGNMENT,
                                    &Pool->Object) != AppleAgxMemoryResultOk)
    return APPLE_AGX_FALSE;

  for (index = 0; index < Length; ++index)
    ((unsigned char *)Pool->Object.CpuAddress)[index] = 0u;
  if (AppleAgxMemoryMarkCpuWritten(&Pool->Object) != AppleAgxMemoryResultOk ||
      AppleAgxMemoryMarkPrepared(&Pool->Object) != AppleAgxMemoryResultOk) {
    (void)AppleAgxMemoryRelease(Io, &Pool->Object);
    return APPLE_AGX_FALSE;
  }
  Pool->Active = APPLE_AGX_TRUE;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxGdiMemoryPoolDestroy(
    const APPLE_AGX_MEMORY_IO *Io, APPLE_AGX_GDI_MEMORY_POOL *Pool) {
  if (Pool == APPLE_AGX_GDI_NULL)
    return APPLE_AGX_FALSE;
  if (Pool->Active == APPLE_AGX_FALSE)
    return Pool->Object.State == AppleAgxMemoryEmpty ? APPLE_AGX_TRUE
                                                     : APPLE_AGX_FALSE;
  if (AppleAgxMemoryRelease(Io, &Pool->Object) != AppleAgxMemoryResultOk)
    return APPLE_AGX_FALSE;
  Pool->Active = APPLE_AGX_FALSE;
  return APPLE_AGX_TRUE;
}
