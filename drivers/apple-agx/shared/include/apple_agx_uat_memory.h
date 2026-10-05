#ifndef APPLE_AGX_UAT_MEMORY_H
#define APPLE_AGX_UAT_MEMORY_H

#include "apple_agx_memory.h"
#include "apple_agx_uat_table.h"

typedef enum _APPLE_AGX_UAT_MEMORY_RESULT {
  AppleAgxUatMemoryResultOk = 0,
  AppleAgxUatMemoryResultInvalidArgument,
  AppleAgxUatMemoryResultCapacity,
  AppleAgxUatMemoryResultAllocationFailed,
  AppleAgxUatMemoryResultReleaseFailed,
  AppleAgxUatMemoryResultNotOwned,
} APPLE_AGX_UAT_MEMORY_RESULT;

typedef struct _APPLE_AGX_UAT_MEMORY_OWNER {
  const APPLE_AGX_MEMORY_IO *MemoryIo;
  APPLE_AGX_MEMORY_OBJECT *Objects;
  unsigned int ObjectCapacity;
  unsigned int ObjectCount;
  APPLE_AGX_UAT_MEMORY_RESULT LastResult;
} APPLE_AGX_UAT_MEMORY_OWNER;

APPLE_AGX_UAT_MEMORY_RESULT AppleAgxUatMemoryOwnerInitialize(
    APPLE_AGX_UAT_MEMORY_OWNER *Owner, const APPLE_AGX_MEMORY_IO *MemoryIo,
    APPLE_AGX_MEMORY_OBJECT *Objects, unsigned int ObjectCapacity);
APPLE_AGX_UAT_MEMORY_RESULT AppleAgxUatMemoryOwnerGetAllocator(
    APPLE_AGX_UAT_MEMORY_OWNER *Owner, APPLE_AGX_UAT_ALLOCATOR *Allocator);
APPLE_AGX_UAT_MEMORY_RESULT
AppleAgxUatMemoryOwnerDestroy(APPLE_AGX_UAT_MEMORY_OWNER *Owner);

#endif /* APPLE_AGX_UAT_MEMORY_H */
