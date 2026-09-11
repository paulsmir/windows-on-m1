#ifndef APPLE_AGX_DYNAMIC_JOB_H
#define APPLE_AGX_DYNAMIC_JOB_H

#include "render_win32_transport.h"

#define APPLE_AGX_DYNAMIC_JOB_MAGIC 0x4a444741u /* AGDJ */
#define APPLE_AGX_DYNAMIC_JOB_VERSION 1u
#define APPLE_AGX_DYNAMIC_JOB_MAX_OBJECT_BYTES 0x10000u
#define APPLE_AGX_DYNAMIC_JOB_MAX_STORAGE_BYTES 0x40000u

typedef enum _APPLE_AGX_DYNAMIC_JOB_RESULT {
  AppleAgxDynamicJobSuccess = 0,
  AppleAgxDynamicJobArgument,
  AppleAgxDynamicJobLayout,
  AppleAgxDynamicJobRange,
  AppleAgxDynamicJobRead,
  AppleAgxDynamicJobResolve,
  AppleAgxDynamicJobRelocation,
} APPLE_AGX_DYNAMIC_JOB_RESULT;

typedef struct _APPLE_AGX_DYNAMIC_JOB_OBJECT {
  APPLE_AGX_U32 ReferenceIndex;
  APPLE_AGX_U32 Role;
  APPLE_AGX_U32 StorageOffset;
  APPLE_AGX_U32 Bytes;
  APPLE_AGX_U64 AllocationToken;
  APPLE_AGX_U64 SourceHash;
} APPLE_AGX_DYNAMIC_JOB_OBJECT;

typedef struct _APPLE_AGX_DYNAMIC_JOB_RELOCATION {
  APPLE_AGX_U32 Kind;
  APPLE_AGX_U32 DestinationReference;
  APPLE_AGX_U32 TargetReference;
  APPLE_AGX_U32 Reserved;
  APPLE_AGX_U64 DestinationOffset;
  APPLE_AGX_U64 ResolvedAddress;
  APPLE_AGX_U64 EncodedValue;
} APPLE_AGX_DYNAMIC_JOB_RELOCATION;

typedef struct _APPLE_AGX_DYNAMIC_JOB {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U32 Version;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 ObjectCount;
  APPLE_AGX_U32 RelocationCount;
  APPLE_AGX_U32 StorageBytes;
  APPLE_AGX_U32 Reserved[2];
  APPLE_AGX_U64 MaterializedHash;
  APPLE_AGX_DYNAMIC_JOB_OBJECT
      Objects[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  APPLE_AGX_DYNAMIC_JOB_RELOCATION
      Relocations[APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS];
} APPLE_AGX_DYNAMIC_JOB;

typedef int (*APPLE_AGX_DYNAMIC_JOB_READ)(
    void *Context, APPLE_AGX_U64 AllocationToken,
    APPLE_AGX_U32 ReferenceIndex, APPLE_AGX_U32 Role,
    APPLE_AGX_U64 Offset, APPLE_AGX_U32 Bytes, void *Destination);
typedef int (*APPLE_AGX_DYNAMIC_JOB_RESOLVE)(
    void *Context, APPLE_AGX_U64 AllocationToken, APPLE_AGX_U32 ClassId,
    APPLE_AGX_U32 ReferenceIndex, APPLE_AGX_U32 Role,
    APPLE_AGX_U64 Offset, APPLE_AGX_U32 Bytes,
    APPLE_AGX_U64 *GpuVirtualAddress);

APPLE_AGX_DYNAMIC_JOB_RESULT AppleAgxDynamicJobMaterialize(
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    const ADMISSION_WIN32_ALLOCATION_FACT *Facts,
    APPLE_AGX_U32 FactCount, APPLE_AGX_U64 ShaderBase,
    APPLE_AGX_DYNAMIC_JOB_READ Read,
    APPLE_AGX_DYNAMIC_JOB_RESOLVE Resolve, void *CallbackContext,
    void *Storage, APPLE_AGX_U32 StorageCapacity,
    APPLE_AGX_DYNAMIC_JOB *Job);

#endif
