#include "apple_agx_win32_abi.h"

#include <stddef.h>

#define APPLE_AGX_WIN32_NULL ((void *)0)
#define APPLE_AGX_U64_MAX_VALUE (~(APPLE_AGX_U64)0ULL)
#define APPLE_AGX_U32_MAX_VALUE (~(APPLE_AGX_U32)0u)

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
  APPLE_AGX_U32 referenceBytes;
  APPLE_AGX_U32 payloadOffset;
  APPLE_AGX_U32 minimumPitch;

  if (bytes == APPLE_AGX_WIN32_NULL || View == APPLE_AGX_WIN32_NULL ||
      ExpectedGeneration == 0u || AllocationCount == 0u ||
      CommandBytes < sizeof(APPLE_AGX_WIN32_COMMAND_HEADER) ||
      CommandBytes > APPLE_AGX_WIN32_COMMAND_MAX_BYTES)
    return AppleAgxWin32AbiArgument;
  View->Header = APPLE_AGX_WIN32_NULL;
  View->References = APPLE_AGX_WIN32_NULL;
  View->Clear = APPLE_AGX_WIN32_NULL;
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
  if (header->Opcode != (APPLE_AGX_U32)AppleAgxWin32OpcodeClear)
    return AppleAgxWin32AbiOpcode;

  referenceBytes = header->ReferenceCount *
                   (APPLE_AGX_U32)sizeof(APPLE_AGX_WIN32_ALLOCATION_REFERENCE);
  payloadOffset = (APPLE_AGX_U32)sizeof(*header) + referenceBytes;
  if (header->ReferencesOffset != sizeof(*header) ||
      (header->ReferencesOffset & (sizeof(APPLE_AGX_U64) - 1u)) != 0u ||
      header->PayloadOffset != payloadOffset ||
      (header->PayloadOffset & (sizeof(APPLE_AGX_U64) - 1u)) != 0u ||
      header->PayloadBytes != sizeof(APPLE_AGX_WIN32_CLEAR_PAYLOAD) ||
      header->PayloadOffset > header->TotalBytes ||
      header->PayloadBytes > header->TotalBytes - header->PayloadOffset ||
      header->PayloadOffset + header->PayloadBytes != header->TotalBytes)
    return AppleAgxWin32AbiLayout;
  if (AppleAgxWin32CommandHash(Command, CommandBytes) != header->ContentHash)
    return AppleAgxWin32AbiHash;

  references = (const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *)(
      bytes + header->ReferencesOffset);
  if (header->ReferenceCount != 1u)
    return AppleAgxWin32AbiReferenceCount;
  if (references[0].AllocationIndex >= AllocationCount)
    return AppleAgxWin32AbiAllocationIndex;
  if (references[0].Access != (APPLE_AGX_U32)AppleAgxWin32AccessWrite)
    return AppleAgxWin32AbiAccess;
  if (references[0].Role !=
      (APPLE_AGX_U32)AppleAgxWin32RoleRenderTarget)
    return AppleAgxWin32AbiRole;
  if (references[0].Reserved != 0u)
    return AppleAgxWin32AbiReserved;
  if (references[0].Bytes == 0ULL ||
      references[0].Offset > APPLE_AGX_U64_MAX_VALUE - references[0].Bytes)
    return AppleAgxWin32AbiRange;

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
