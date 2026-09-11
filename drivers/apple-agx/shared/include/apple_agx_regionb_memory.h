#ifndef APPLE_AGX_REGIONB_MEMORY_H
#define APPLE_AGX_REGIONB_MEMORY_H

#include "apple_agx_memory.h"
#include "apple_agx_regionb.h"

#define APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT 11u

typedef enum _APPLE_AGX_REGIONB_MEMORY_OBJECT_INDEX {
  AppleAgxRegionBMemoryStatsTa = 0,
  AppleAgxRegionBMemoryStats3d,
  AppleAgxRegionBMemoryStatsCp,
  AppleAgxRegionBMemoryHwdataA,
  AppleAgxRegionBMemoryFaultInfo,
  AppleAgxRegionBMemoryTimestamp,
  AppleAgxRegionBMemoryHwdataB,
  AppleAgxRegionBMemoryUnknown1b8,
  AppleAgxRegionBMemoryUnknown1c0,
  AppleAgxRegionBMemoryUnknown1c8,
  AppleAgxRegionBMemoryBufferManager,
} APPLE_AGX_REGIONB_MEMORY_OBJECT_INDEX;

typedef enum _APPLE_AGX_REGIONB_MEMORY_RESULT {
  AppleAgxRegionBMemoryResultOk = 0,
  AppleAgxRegionBMemoryResultInvalidArgument,
  AppleAgxRegionBMemoryResultAllocationFailed,
  AppleAgxRegionBMemoryResultReleaseFailed,
} APPLE_AGX_REGIONB_MEMORY_RESULT;

typedef struct _APPLE_AGX_REGIONB_MEMORY_OWNER {
  const APPLE_AGX_MEMORY_IO *MemoryIo;
  APPLE_AGX_MEMORY_OBJECT Objects[APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT];
  unsigned long long VirtualAddresses[APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT];
  APPLE_AGX_REGIONB_INPUT Input;
  unsigned int ObjectCount;
  unsigned char Initialized;
  unsigned char Built;
  APPLE_AGX_REGIONB_MEMORY_RESULT LastResult;
} APPLE_AGX_REGIONB_MEMORY_OWNER;

APPLE_AGX_REGIONB_MEMORY_RESULT AppleAgxRegionBMemoryBuild(
    APPLE_AGX_REGIONB_MEMORY_OWNER *Owner,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    unsigned long long FirstVirtualAddress,
    unsigned long long FwlogRingAddress);
APPLE_AGX_REGIONB_MEMORY_RESULT AppleAgxRegionBMemoryDestroy(
    APPLE_AGX_REGIONB_MEMORY_OWNER *Owner);

#endif /* APPLE_AGX_REGIONB_MEMORY_H */
