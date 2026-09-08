#ifndef APPLE_AGX_WIN32_ABI_H
#define APPLE_AGX_WIN32_ABI_H

#include "apple_agx_state.h"

typedef unsigned short APPLE_AGX_U16;

#define APPLE_AGX_WIN32_COMMAND_MAGIC 0x43474157u /* "WAGC" */
#define APPLE_AGX_WIN32_COMMAND_VERSION 1u
#define APPLE_AGX_WIN32_COMMAND_MAX_BYTES 4096u
#define APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES 16u

typedef enum _APPLE_AGX_WIN32_OPCODE {
  AppleAgxWin32OpcodeClear = 1u,
} APPLE_AGX_WIN32_OPCODE;

typedef enum _APPLE_AGX_WIN32_ACCESS {
  AppleAgxWin32AccessRead = 0x1u,
  AppleAgxWin32AccessWrite = 0x2u,
  AppleAgxWin32AccessExecute = 0x4u,
} APPLE_AGX_WIN32_ACCESS;

typedef enum _APPLE_AGX_WIN32_ROLE {
  AppleAgxWin32RoleRenderTarget = 1u,
  AppleAgxWin32RoleVertex = 2u,
  AppleAgxWin32RoleIndex = 3u,
  AppleAgxWin32RoleConstant = 4u,
  AppleAgxWin32RoleTexture = 5u,
  AppleAgxWin32RoleShader = 6u,
  AppleAgxWin32RoleDescriptor = 7u,
} APPLE_AGX_WIN32_ROLE;

typedef enum _APPLE_AGX_WIN32_FORMAT {
  AppleAgxWin32FormatBgra8Unorm = 1u,
} APPLE_AGX_WIN32_FORMAT;

typedef enum _APPLE_AGX_WIN32_ABI_RESULT {
  AppleAgxWin32AbiSuccess = 0,
  AppleAgxWin32AbiArgument,
  AppleAgxWin32AbiMagic,
  AppleAgxWin32AbiVersion,
  AppleAgxWin32AbiLayout,
  AppleAgxWin32AbiFlags,
  AppleAgxWin32AbiStaleGeneration,
  AppleAgxWin32AbiReferenceCount,
  AppleAgxWin32AbiHash,
  AppleAgxWin32AbiOpcode,
  AppleAgxWin32AbiAllocationIndex,
  AppleAgxWin32AbiAccess,
  AppleAgxWin32AbiRole,
  AppleAgxWin32AbiReserved,
  AppleAgxWin32AbiRange,
  AppleAgxWin32AbiPayload,
} APPLE_AGX_WIN32_ABI_RESULT;

typedef struct _APPLE_AGX_WIN32_COMMAND_HEADER {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U16 Version;
  APPLE_AGX_U16 HeaderBytes;
  APPLE_AGX_U32 TotalBytes;
  APPLE_AGX_U32 Opcode;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 ReferenceCount;
  APPLE_AGX_U32 ReferencesOffset;
  APPLE_AGX_U32 PayloadOffset;
  APPLE_AGX_U32 PayloadBytes;
  APPLE_AGX_U64 ContentHash;
} APPLE_AGX_WIN32_COMMAND_HEADER;

typedef struct _APPLE_AGX_WIN32_ALLOCATION_REFERENCE {
  APPLE_AGX_U32 AllocationIndex;
  APPLE_AGX_U32 Access;
  APPLE_AGX_U32 Role;
  APPLE_AGX_U32 Reserved;
  APPLE_AGX_U64 Offset;
  APPLE_AGX_U64 Bytes;
} APPLE_AGX_WIN32_ALLOCATION_REFERENCE;

typedef struct _APPLE_AGX_WIN32_CLEAR_PAYLOAD {
  APPLE_AGX_U32 StructBytes;
  APPLE_AGX_U32 Format;
  APPLE_AGX_U32 Color;
  APPLE_AGX_U32 SurfaceWidth;
  APPLE_AGX_U32 SurfaceHeight;
  APPLE_AGX_U32 SurfacePitch;
  APPLE_AGX_U32 Left;
  APPLE_AGX_U32 Top;
  APPLE_AGX_U32 Right;
  APPLE_AGX_U32 Bottom;
  APPLE_AGX_U32 DestinationReference;
  APPLE_AGX_U32 Reserved;
} APPLE_AGX_WIN32_CLEAR_PAYLOAD;

typedef struct _APPLE_AGX_WIN32_COMMAND_VIEW {
  const APPLE_AGX_WIN32_COMMAND_HEADER *Header;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *References;
  const APPLE_AGX_WIN32_CLEAR_PAYLOAD *Clear;
} APPLE_AGX_WIN32_COMMAND_VIEW;

APPLE_AGX_U64 AppleAgxWin32CommandHash(const void *Command,
                                       APPLE_AGX_U32 CommandBytes);
APPLE_AGX_WIN32_ABI_RESULT AppleAgxWin32CommandValidate(
    const void *Command, APPLE_AGX_U32 CommandBytes,
    APPLE_AGX_U32 ExpectedGeneration, APPLE_AGX_U32 AllocationCount,
    APPLE_AGX_WIN32_COMMAND_VIEW *View);

#endif /* APPLE_AGX_WIN32_ABI_H */
