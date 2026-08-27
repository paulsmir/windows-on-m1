#ifndef APPLE_AGX_INITDATA_MEMORY_H
#define APPLE_AGX_INITDATA_MEMORY_H

#include "apple_agx_initdata.h"
#include "apple_agx_firmware_status.h"
#include "apple_agx_memory.h"
#include "apple_agx_uat_memory.h"

#define APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT 7u
#define APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY 8u

typedef enum _APPLE_AGX_INITDATA_MEMORY_OBJECT_INDEX {
  AppleAgxInitdataMemoryEnvelope = 0,
  AppleAgxInitdataMemoryRegionA,
  AppleAgxInitdataMemoryRegionB,
  AppleAgxInitdataMemoryRegionC,
  AppleAgxInitdataMemoryFirmwareStatus,
  AppleAgxInitdataMemoryFwctlState,
  AppleAgxInitdataMemoryFwctlRing,
} APPLE_AGX_INITDATA_MEMORY_OBJECT_INDEX;

typedef enum _APPLE_AGX_INITDATA_MEMORY_RESULT {
  AppleAgxInitdataMemoryResultOk = 0,
  AppleAgxInitdataMemoryResultInvalidArgument,
  AppleAgxInitdataMemoryResultAllocationFailed,
  AppleAgxInitdataMemoryResultUatFailed,
  AppleAgxInitdataMemoryResultEncodeFailed,
  AppleAgxInitdataMemoryResultReleaseFailed,
} APPLE_AGX_INITDATA_MEMORY_RESULT;

typedef struct _APPLE_AGX_INITDATA_MEMORY_GRAPH {
  const APPLE_AGX_MEMORY_IO *MemoryIo;
  APPLE_AGX_MEMORY_OBJECT DataObjects[APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT];
  unsigned long long
      VirtualAddresses[APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT];
  unsigned int DataObjectCount;
  APPLE_AGX_MEMORY_OBJECT
      UatMemoryObjects[APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY];
  APPLE_AGX_UAT_MEMORY_OWNER UatMemoryOwner;
  APPLE_AGX_UAT_ALLOCATOR UatAllocator;
  APPLE_AGX_UAT_PAGE UatPages[APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY];
  APPLE_AGX_UAT_MAPPING
      UatMappings[APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT];
  APPLE_AGX_UAT_INVENTORY Inventory;
  APPLE_AGX_UAT_ROOTS Roots;
  APPLE_AGX_UAT_TTBR_PAIR TtbrPair;
  APPLE_AGX_INITDATA_MANIFEST Manifest;
  APPLE_AGX_FIRMWARE_STATUS_MANIFEST FirmwareStatusManifest;
  unsigned long long InitdataVirtualAddress;
  unsigned long long InitdataDeviceAddress;
  unsigned char Initialized;
  unsigned char Built;
  APPLE_AGX_INITDATA_MEMORY_RESULT LastResult;
} APPLE_AGX_INITDATA_MEMORY_GRAPH;

APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryBuild(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    const APPLE_AGX_MEMORY_IO *MemoryIo);
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryDestroy(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph);

#endif /* APPLE_AGX_INITDATA_MEMORY_H */
