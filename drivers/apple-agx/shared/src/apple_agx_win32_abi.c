#include "apple_agx_win32_abi.h"

#include <stddef.h>

#define APPLE_AGX_WIN32_NULL ((void *)0)
#define APPLE_AGX_U64_MAX_VALUE (~(APPLE_AGX_U64)0ULL)
#define APPLE_AGX_U32_MAX_VALUE (~(APPLE_AGX_U32)0u)

static int AppleAgxWin32ReferencePolicy(
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *Reference) {
  if (Reference == APPLE_AGX_WIN32_NULL)
    return 0;
  switch (Reference->Role) {
  case AppleAgxWin32RoleRenderTarget:
    return Reference->Access == AppleAgxWin32AccessWrite ||
           Reference->Access ==
               (AppleAgxWin32AccessRead | AppleAgxWin32AccessWrite);
  case AppleAgxWin32RoleShader:
    return Reference->Access ==
           (AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute);
  case AppleAgxWin32RoleVertex:
  case AppleAgxWin32RoleIndex:
  case AppleAgxWin32RoleConstant:
  case AppleAgxWin32RoleTexture:
  case AppleAgxWin32RoleDescriptor:
  case AppleAgxWin32RoleShaderRodata:
  case AppleAgxWin32RoleUscPipeline:
  case AppleAgxWin32RoleEncoder:
  case AppleAgxWin32RoleScissor:
  case AppleAgxWin32RoleDepthBias:
    return Reference->Access == AppleAgxWin32AccessRead;
  default:
    return 0;
  }
}

static APPLE_AGX_WIN32_ABI_RESULT AppleAgxWin32DrawReference(
    APPLE_AGX_U32 Index, APPLE_AGX_U32 Role, int Optional,
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *References,
    APPLE_AGX_U32 ReferenceCount, unsigned char *Reachable) {
  if (Optional && Index == APPLE_AGX_WIN32_OPTIONAL_REFERENCE)
    return AppleAgxWin32AbiSuccess;
  if (Index >= ReferenceCount)
    return AppleAgxWin32AbiAllocationIndex;
  if (References[Index].Role != Role)
    return AppleAgxWin32AbiRole;
  Reachable[Index] = 1u;
  return AppleAgxWin32AbiSuccess;
}

static int AppleAgxWin32RelocationPolicy(
    const APPLE_AGX_WIN32_RELOCATION *Relocation,
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *References) {
  APPLE_AGX_U32 destinationRole =
      References[Relocation->DestinationReference].Role;
  APPLE_AGX_U32 targetRole = References[Relocation->TargetReference].Role;
  switch (Relocation->Kind) {
  case AppleAgxWin32RelocationEncoderAddress:
    if (destinationRole != AppleAgxWin32RoleEncoder)
      return 0;
    return targetRole == AppleAgxWin32RoleRenderTarget ||
           targetRole == AppleAgxWin32RoleVertex ||
           targetRole == AppleAgxWin32RoleIndex ||
           targetRole == AppleAgxWin32RoleConstant ||
           targetRole == AppleAgxWin32RoleTexture ||
           targetRole == AppleAgxWin32RoleShader ||
           targetRole == AppleAgxWin32RoleShaderRodata ||
           targetRole == AppleAgxWin32RoleUscPipeline ||
           targetRole == AppleAgxWin32RoleDescriptor ||
           targetRole == AppleAgxWin32RoleScissor ||
           targetRole == AppleAgxWin32RoleDepthBias;
  case AppleAgxWin32RelocationPipelineAddress:
    if (destinationRole != AppleAgxWin32RoleUscPipeline)
      return 0;
    return targetRole == AppleAgxWin32RoleShader ||
           targetRole == AppleAgxWin32RoleShaderRodata ||
           targetRole == AppleAgxWin32RoleDescriptor ||
           targetRole == AppleAgxWin32RoleConstant;
  case AppleAgxWin32RelocationDescriptorAddress:
    if (destinationRole != AppleAgxWin32RoleDescriptor)
      return 0;
    return targetRole == AppleAgxWin32RoleVertex ||
           targetRole == AppleAgxWin32RoleTexture ||
           targetRole == AppleAgxWin32RoleConstant ||
           targetRole == AppleAgxWin32RoleRenderTarget;
  case AppleAgxWin32RelocationUscShaderOffset32:
    return destinationRole == AppleAgxWin32RoleUscPipeline &&
           targetRole == AppleAgxWin32RoleShader;
  case AppleAgxWin32RelocationUscBufferAddress40:
    return destinationRole == AppleAgxWin32RoleUscPipeline &&
           (targetRole == AppleAgxWin32RoleShaderRodata ||
            targetRole == AppleAgxWin32RoleDescriptor ||
            targetRole == AppleAgxWin32RoleConstant ||
            targetRole == AppleAgxWin32RoleTexture);
  case AppleAgxWin32RelocationVdmPipelineOffset32:
    return destinationRole == AppleAgxWin32RoleEncoder &&
           targetRole == AppleAgxWin32RoleUscPipeline;
  case AppleAgxWin32RelocationPppStateAddress40:
    return destinationRole == AppleAgxWin32RoleEncoder &&
           targetRole == AppleAgxWin32RoleEncoder;
  default:
    return 0;
  }
}

APPLE_AGX_U64 AppleAgxWin32CommandHash(const void *Command,
                                       APPLE_AGX_U32 CommandBytes) {
  const unsigned char *bytes = (const unsigned char *)Command;
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  APPLE_AGX_U32 index;
  const APPLE_AGX_U32 hashOffset =
      (APPLE_AGX_U32)offsetof(APPLE_AGX_WIN32_COMMAND_HEADER, ContentHash);
  if (bytes == APPLE_AGX_WIN32_NULL ||
      CommandBytes < sizeof(APPLE_AGX_WIN32_COMMAND_HEADER) ||
      CommandBytes > APPLE_AGX_WIN32_COMMAND_MAX_BYTES)
    return 0ULL;
  for (index = 0u; index < CommandBytes; ++index) {
    unsigned char value =
        index >= hashOffset && index < hashOffset + sizeof(APPLE_AGX_U64)
            ? 0u
            : bytes[index];
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  return hash;
}

APPLE_AGX_WIN32_ABI_RESULT AppleAgxWin32CommandValidate(
    const void *Command, APPLE_AGX_U32 CommandBytes,
    APPLE_AGX_U32 ExpectedGeneration, APPLE_AGX_U32 AllocationCount,
    APPLE_AGX_WIN32_COMMAND_VIEW *View) {
  const unsigned char *bytes = (const unsigned char *)Command;
  const APPLE_AGX_WIN32_COMMAND_HEADER *header;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *references;
  const APPLE_AGX_WIN32_CLEAR_PAYLOAD *clear;
  const APPLE_AGX_WIN32_DRAW_PAYLOAD *draw;
  const APPLE_AGX_WIN32_RELOCATION *relocations;
  unsigned char reachable[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES] = {0};
  APPLE_AGX_U32 referenceBytes;
  APPLE_AGX_U32 payloadOffset;
  APPLE_AGX_U32 minimumPitch;
  APPLE_AGX_U32 index;

  if (bytes == APPLE_AGX_WIN32_NULL || View == APPLE_AGX_WIN32_NULL ||
      ExpectedGeneration == 0u || AllocationCount == 0u ||
      CommandBytes < sizeof(APPLE_AGX_WIN32_COMMAND_HEADER) ||
      CommandBytes > APPLE_AGX_WIN32_COMMAND_MAX_BYTES)
    return AppleAgxWin32AbiArgument;
  View->Header = APPLE_AGX_WIN32_NULL;
  View->References = APPLE_AGX_WIN32_NULL;
  View->Clear = APPLE_AGX_WIN32_NULL;
  View->Draw = APPLE_AGX_WIN32_NULL;
  View->Relocations = APPLE_AGX_WIN32_NULL;
  header = (const APPLE_AGX_WIN32_COMMAND_HEADER *)bytes;
  if (header->Magic != APPLE_AGX_WIN32_COMMAND_MAGIC)
    return AppleAgxWin32AbiMagic;
  if (header->Version != APPLE_AGX_WIN32_COMMAND_VERSION)
    return AppleAgxWin32AbiVersion;
  if (header->HeaderBytes != sizeof(*header) ||
      header->TotalBytes != CommandBytes)
    return AppleAgxWin32AbiLayout;
  if (header->Flags != 0u)
    return AppleAgxWin32AbiFlags;
  if (header->Generation != ExpectedGeneration)
    return AppleAgxWin32AbiStaleGeneration;
  if (header->ReferenceCount == 0u ||
      header->ReferenceCount > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES)
    return AppleAgxWin32AbiReferenceCount;
  if (header->Opcode != (APPLE_AGX_U32)AppleAgxWin32OpcodeClear &&
      header->Opcode != (APPLE_AGX_U32)AppleAgxWin32OpcodeDraw)
    return AppleAgxWin32AbiOpcode;

  referenceBytes = header->ReferenceCount *
                   (APPLE_AGX_U32)sizeof(APPLE_AGX_WIN32_ALLOCATION_REFERENCE);
  payloadOffset = (APPLE_AGX_U32)sizeof(*header) + referenceBytes;
  if (header->ReferencesOffset != sizeof(*header) ||
      (header->ReferencesOffset & (sizeof(APPLE_AGX_U64) - 1u)) != 0u ||
      header->PayloadOffset != payloadOffset ||
      (header->PayloadOffset & (sizeof(APPLE_AGX_U64) - 1u)) != 0u ||
      header->PayloadOffset > header->TotalBytes ||
      header->PayloadBytes > header->TotalBytes - header->PayloadOffset ||
      header->PayloadOffset + header->PayloadBytes != header->TotalBytes)
    return AppleAgxWin32AbiLayout;
  if (AppleAgxWin32CommandHash(Command, CommandBytes) != header->ContentHash)
    return AppleAgxWin32AbiHash;

  references = (const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *)(
      bytes + header->ReferencesOffset);
  for (index = 0u; index < header->ReferenceCount; ++index) {
    if (references[index].AllocationIndex >= AllocationCount)
      return AppleAgxWin32AbiAllocationIndex;
    if (references[index].Role < AppleAgxWin32RoleRenderTarget ||
        references[index].Role > AppleAgxWin32RoleDepthBias)
      return AppleAgxWin32AbiRole;
    if (references[index].Access == 0u ||
        (references[index].Access &
         ~(AppleAgxWin32AccessRead | AppleAgxWin32AccessWrite |
           AppleAgxWin32AccessExecute)) != 0u)
      return AppleAgxWin32AbiAccess;
    if (references[index].Reserved != 0u)
      return AppleAgxWin32AbiReserved;
    if (references[index].Bytes == 0ULL ||
        references[index].Offset >
            APPLE_AGX_U64_MAX_VALUE - references[index].Bytes)
      return AppleAgxWin32AbiRange;
  }

  if (header->Opcode == (APPLE_AGX_U32)AppleAgxWin32OpcodeClear) {
    if (header->ReferenceCount != 1u ||
        header->PayloadBytes != sizeof(APPLE_AGX_WIN32_CLEAR_PAYLOAD))
      return header->ReferenceCount != 1u
                 ? AppleAgxWin32AbiReferenceCount
                 : AppleAgxWin32AbiLayout;
    if (references[0].Access != (APPLE_AGX_U32)AppleAgxWin32AccessWrite)
      return AppleAgxWin32AbiAccess;
    if (references[0].Role !=
        (APPLE_AGX_U32)AppleAgxWin32RoleRenderTarget)
      return AppleAgxWin32AbiRole;
    clear = (const APPLE_AGX_WIN32_CLEAR_PAYLOAD *)(bytes +
                                                    header->PayloadOffset);
    if (clear->Reserved != 0u)
      return AppleAgxWin32AbiReserved;
    if (clear->StructBytes != sizeof(*clear) ||
        clear->Format != (APPLE_AGX_U32)AppleAgxWin32FormatBgra8Unorm ||
        clear->SurfaceWidth == 0u || clear->SurfaceHeight == 0u ||
        clear->SurfaceWidth > APPLE_AGX_U32_MAX_VALUE / 4u)
      return AppleAgxWin32AbiPayload;
    minimumPitch = clear->SurfaceWidth * 4u;
    if (clear->SurfacePitch < minimumPitch || clear->Left >= clear->Right ||
        clear->Top >= clear->Bottom || clear->Right > clear->SurfaceWidth ||
        clear->Bottom > clear->SurfaceHeight ||
        clear->DestinationReference != 0u)
      return AppleAgxWin32AbiPayload;
    View->Header = header;
    View->References = references;
    View->Clear = clear;
    return AppleAgxWin32AbiSuccess;
  }

  if (header->PayloadBytes < sizeof(APPLE_AGX_WIN32_DRAW_PAYLOAD))
    return AppleAgxWin32AbiLayout;
  for (index = 0u; index < header->ReferenceCount; ++index)
    if (!AppleAgxWin32ReferencePolicy(&references[index]))
      return AppleAgxWin32AbiAccess;
  draw = (const APPLE_AGX_WIN32_DRAW_PAYLOAD *)(bytes +
                                                header->PayloadOffset);
  if (draw->StructBytes != sizeof(*draw) ||
      draw->Format != (APPLE_AGX_U32)AppleAgxWin32FormatBgra8Unorm ||
      draw->SurfaceWidth == 0u || draw->SurfaceHeight == 0u ||
      draw->SurfaceWidth > APPLE_AGX_U32_MAX_VALUE / 4u ||
      draw->SurfacePitch < draw->SurfaceWidth * 4u ||
      draw->Topology != AppleAgxWin32TopologyTriangleList ||
      draw->VertexCount == 0u || (draw->VertexCount % 3u) != 0u ||
      draw->VertexCount > 0x01000000u || draw->InstanceCount != 1u ||
      draw->FirstVertex != 0u || draw->FirstInstance != 0u)
    return AppleAgxWin32AbiPayload;
  if (draw->Flags != 0u)
    return AppleAgxWin32AbiFlags;
  for (index = 0u; index < 5u; ++index)
    if (draw->Reserved[index] != 0u)
      return AppleAgxWin32AbiReserved;
  if (draw->RelocationsOffset != sizeof(*draw))
    return AppleAgxWin32AbiLayout;
  if (draw->RelocationCount == 0u ||
      draw->RelocationCount > APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS)
    return AppleAgxWin32AbiRelocation;
  if (header->PayloadBytes != sizeof(*draw) +
      draw->RelocationCount * sizeof(APPLE_AGX_WIN32_RELOCATION))
    return AppleAgxWin32AbiLayout;

#define DRAW_REFERENCE(Field, Role, Optional)                                \
  do {                                                                       \
    APPLE_AGX_WIN32_ABI_RESULT referenceResult =                             \
        AppleAgxWin32DrawReference(draw->Field, Role, Optional, references,  \
                                   header->ReferenceCount, reachable);       \
    if (referenceResult != AppleAgxWin32AbiSuccess)                          \
      return referenceResult;                                                \
  } while (0)
  DRAW_REFERENCE(DestinationReference, AppleAgxWin32RoleRenderTarget, 0);
  DRAW_REFERENCE(VertexReference, AppleAgxWin32RoleVertex, 0);
  DRAW_REFERENCE(IndexReference, AppleAgxWin32RoleIndex, 1);
  DRAW_REFERENCE(ConstantReference, AppleAgxWin32RoleConstant, 1);
  DRAW_REFERENCE(TextureReference, AppleAgxWin32RoleTexture, 1);
  DRAW_REFERENCE(VertexShaderReference, AppleAgxWin32RoleShader, 0);
  DRAW_REFERENCE(FragmentShaderReference, AppleAgxWin32RoleShader, 0);
  DRAW_REFERENCE(VertexRodataReference, AppleAgxWin32RoleShaderRodata, 1);
  DRAW_REFERENCE(FragmentRodataReference, AppleAgxWin32RoleShaderRodata, 1);
  DRAW_REFERENCE(UscPipelineReference, AppleAgxWin32RoleUscPipeline, 0);
  DRAW_REFERENCE(DescriptorReference, AppleAgxWin32RoleDescriptor, 0);
  DRAW_REFERENCE(ScissorReference, AppleAgxWin32RoleScissor, 0);
  DRAW_REFERENCE(DepthBiasReference, AppleAgxWin32RoleDepthBias, 0);
  DRAW_REFERENCE(EncoderReference, AppleAgxWin32RoleEncoder, 0);
#undef DRAW_REFERENCE

  relocations = (const APPLE_AGX_WIN32_RELOCATION *)(
      (const unsigned char *)draw + draw->RelocationsOffset);
  for (index = 0u; index < draw->RelocationCount; ++index) {
    const APPLE_AGX_WIN32_RELOCATION *relocation = &relocations[index];
    APPLE_AGX_U32 other;
    if (relocation->Reserved != 0u)
      return AppleAgxWin32AbiReserved;
    if (relocation->AddressFlags != 0ULL ||
        relocation->DestinationReference >= header->ReferenceCount ||
        relocation->TargetReference >= header->ReferenceCount ||
        !AppleAgxWin32RelocationPolicy(relocation, references))
      return AppleAgxWin32AbiRelocation;
    if ((relocation->Kind == AppleAgxWin32RelocationUscShaderOffset32 &&
         relocation->WidthBytes != 6u) ||
        (relocation->Kind == AppleAgxWin32RelocationVdmPipelineOffset32 &&
         relocation->WidthBytes != 4u) ||
        (relocation->Kind != AppleAgxWin32RelocationUscShaderOffset32 &&
         relocation->Kind != AppleAgxWin32RelocationVdmPipelineOffset32 &&
         relocation->WidthBytes != 8u))
      return AppleAgxWin32AbiRelocation;
    if ((relocation->Kind == AppleAgxWin32RelocationVdmPipelineOffset32 &&
         (relocation->DestinationOffset & 3ULL) != 0ULL) ||
        (relocation->Kind == AppleAgxWin32RelocationPppStateAddress40 &&
         (((relocation->DestinationOffset | relocation->TargetOffset) &
           3ULL) != 0ULL)) ||
        (relocation->Kind == AppleAgxWin32RelocationUscBufferAddress40 &&
         (relocation->DestinationOffset & 3ULL) != 0ULL) ||
        relocation->DestinationOffset >
            references[relocation->DestinationReference].Bytes ||
        relocation->WidthBytes >
            references[relocation->DestinationReference].Bytes -
                relocation->DestinationOffset ||
        relocation->TargetOffset >=
            references[relocation->TargetReference].Bytes)
      return AppleAgxWin32AbiRange;
    for (other = 0u; other < index; ++other) {
      if (relocations[other].DestinationReference ==
              relocation->DestinationReference &&
          relocations[other].DestinationOffset <
              relocation->DestinationOffset + relocation->WidthBytes &&
          relocation->DestinationOffset <
              relocations[other].DestinationOffset +
                  relocations[other].WidthBytes)
        return AppleAgxWin32AbiRelocation;
    }
    reachable[relocation->DestinationReference] = 1u;
    reachable[relocation->TargetReference] = 1u;
  }
  for (index = 0u; index < header->ReferenceCount; ++index)
    if (!reachable[index])
      return AppleAgxWin32AbiReachability;

  View->Header = header;
  View->References = references;
  View->Draw = draw;
  View->Relocations = relocations;
  return AppleAgxWin32AbiSuccess;
}
