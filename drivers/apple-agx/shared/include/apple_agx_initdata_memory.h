#ifndef APPLE_AGX_INITDATA_MEMORY_H
#define APPLE_AGX_INITDATA_MEMORY_H

#include "apple_agx_initdata.h"
#include "apple_agx_firmware_prefix.h"
#include "apple_agx_channel_memory.h"
#include "apple_agx_firmware_status.h"
#include "apple_agx_memory.h"
#include "apple_agx_regionb_memory.h"
#include "apple_agx_regionc.h"
#include "apple_agx_render_shared_memory.h"
#include "apple_agx_uat_memory.h"

#define APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT 8u
/* Match m1n1 AGX's RTKit allocator in kernel VM / context0 TTBR1. */
#define APPLE_AGX_RTKIT_CRASHLOG_GPU_VA \
  (J313_AGX_G2_KERNEL_VA_BASE + 0x80000000ULL)
#define APPLE_AGX_RTKIT_CRASHLOG_BYTES 0x4000u
#define APPLE_AGX_INITDATA_MEMORY_MAPPING_CAPACITY \
  (APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT + \
   APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT + \
   APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT + \
   APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT + 1u)
#define APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY 8u

typedef enum _APPLE_AGX_INITDATA_MEMORY_OBJECT_INDEX {
  AppleAgxInitdataMemoryEnvelope = 0,
  AppleAgxInitdataMemoryRegionA,
  AppleAgxInitdataMemoryRegionB,
  AppleAgxInitdataMemoryRegionC,
  AppleAgxInitdataMemoryFirmwareStatus,
  AppleAgxInitdataMemoryFwctlState,
  AppleAgxInitdataMemoryFwctlRing,
  AppleAgxInitdataMemoryCrashlog,
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
      UatMappings[APPLE_AGX_INITDATA_MEMORY_MAPPING_CAPACITY];
  APPLE_AGX_MEMORY_OBJECT *MappingObjects[APPLE_AGX_INITDATA_MEMORY_MAPPING_CAPACITY];
  APPLE_AGX_UAT_INVENTORY Inventory;
  APPLE_AGX_UAT_ROOTS Roots;
  APPLE_AGX_UAT_TTBR_PAIR TtbrPair;
  APPLE_AGX_INITDATA_MANIFEST Manifest;
  APPLE_AGX_FIRMWARE_STATUS_MANIFEST FirmwareStatusManifest;
  APPLE_AGX_CHANNEL_MEMORY_OWNER ChannelMemory;
  APPLE_AGX_CHANNEL_INFO_MANIFEST ChannelInfoManifest;
  APPLE_AGX_REGIONB_MEMORY_OWNER RegionBMemory;
  APPLE_AGX_REGIONB_MANIFEST RegionBManifest;
  APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory;
  APPLE_AGX_REGIONC_MANIFEST RegionCManifest;
  unsigned long long InitdataVirtualAddress;
  unsigned long long InitdataDeviceAddress;
  unsigned char Initialized;
  unsigned char Built;
  unsigned char MappingsReady;
  unsigned char BrokerOnly;
  unsigned int BrokerOutstanding;
  APPLE_AGX_INITDATA_MEMORY_RESULT LastResult;
} APPLE_AGX_INITDATA_MEMORY_GRAPH;

APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryBuild(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot);
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryDestroy(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph);

/* Legacy/diagnostic owned-root helpers, not reachable from full production. */
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryPrepare(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot);
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryImportAndMap(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph, const AGX_FW_PREFIX *Prefix);
/* Production preparation: allocations and range metadata only; no roots,
 * tables, private-prefix copy or Windows-owned system crashlog. */
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryPrepareBroker(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot);

#endif /* APPLE_AGX_INITDATA_MEMORY_H */
